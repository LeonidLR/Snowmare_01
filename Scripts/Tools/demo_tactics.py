#!/usr/bin/env python3
"""Offline tactical pattern tagger for CodexTactics session recordings (.jsonl).

Supports both legacy v1 recordings (orders + snapshots) and additive v1 recordings
with shot_hit / damage / kill / down / stealth_on / stealth_off / cover_enter /
cover_leave / climb events and per-op stealth / in_cover / height / seen_by_enemy /
under_fire / scan_pitch fields.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional, Tuple


# --- Threshold constants -----------------------------------------------------
FALLBACK_COVER_WINDOW_S = 15.0
HIGH_GROUND_MIN_DZ_CM = 250.0  # ~1 storey
DISTRACT_FLANK_MIN_ANGLE_DEG = 90.0
FLANK_REAR_MIN_ANGLE_DEG = 120.0
OVERWATCH_FIRE_WINDOW_S = 3.0
UNDER_FIRE_RECENT_S = 6.0
UNWATCHED_SECTOR_HALF_ANGLE_DEG = 60.0
UNWATCHED_FLANK_MIN_SECONDS = 5.0
UNWATCHED_MOVE_LOOK_WINDOW_S = 1.5
PRONE_STANCE_INDEX = 2
LURE_RETREAT_MIN_CM = 300.0
LURE_SNIPE_MIN_DIST_CM = 2000.0
LURE_SNIPE_WINDOW_S = 15.0


@dataclass
class DetectedTactic:
    tag: str
    t_start: float
    t_end: float
    actor: str
    detail: str


@dataclass
class KillRecord:
    t: float
    event_type: str  # "kill" | "down"
    killer: str
    victim: str
    weapon: str
    dist_cm: float
    was_victim_aware: Optional[bool]
    victim_covered_by_ally: Optional[bool]


@dataclass
class TaggerReport:
    session: str
    map_name: str
    duration_s: float
    tactics: List[DetectedTactic] = field(default_factory=list)
    kills: List[KillRecord] = field(default_factory=list)
    unwatched_flank_seconds: float = 0.0
    scan_pitch_min: float = 0.0
    scan_pitch_max: float = 0.0
    scan_pitch_range: float = 0.0


def _angle_diff_deg(a: float, b: float) -> float:
    d = (a - b + 180.0) % 360.0 - 180.0
    return abs(d)


def _bearing_deg(src: List[float], dst: List[float]) -> float:
    return math.degrees(math.atan2(dst[1] - src[1], dst[0] - src[0]))


def _flank_angle_deg(p_start: List[float], p_end: List[float], target: List[float]) -> float:
    ax, ay = p_start[0] - target[0], p_start[1] - target[1]
    bx, by = p_end[0] - target[0], p_end[1] - target[1]
    la = math.hypot(ax, ay)
    lb = math.hypot(bx, by)
    if la < 1.0 or lb < 1.0:
        return 0.0
    dot = max(-1.0, min(1.0, (ax * bx + ay * by) / (la * lb)))
    return math.degrees(math.acos(dot))


def _dist2d(a: List[float], b: List[float]) -> float:
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _op_is_cover(op: Dict[str, Any]) -> bool:
    return bool(op.get("in_cover", op.get("cov", False)))


def _op_is_stealth(op: Dict[str, Any]) -> bool:
    return bool(op.get("stealth", False))


def _op_under_fire_recent(op: Dict[str, Any]) -> bool:
    uf = op.get("under_fire", None)
    if uf is None:
        return False
    try:
        v = float(uf)
    except (TypeError, ValueError):
        return False
    return 0.0 <= v <= UNDER_FIRE_RECENT_S


def _find_first_contact_time(records: List[Dict[str, Any]]) -> Optional[float]:
    for r in records:
        rtype = r.get("type")
        if rtype in ("shot_hit", "damage", "kill", "down"):
            return float(r.get("t", 0.0))
        if rtype == "order" and r.get("kind") in ("Shot", "QuickFire", "Suppress", "Grenade"):
            return float(r.get("t", 0.0))
        if rtype in ("snap", "order"):
            for op in r.get("ops", []):
                if int(op.get("thr_n", 0)) > 0 or _op_under_fire_recent(op):
                    return float(r.get("t", 0.0))
    return None


def analyze_records(records: List[Dict[str, Any]]) -> TaggerReport:
    header = next((r for r in records if r.get("type") == "header"), {})
    footer = next((r for r in reversed(records) if r.get("type") == "footer"), {})
    report = TaggerReport(
        session=str(header.get("session", "")),
        map_name=str(header.get("map", "")),
        duration_s=float(footer.get("duration_s", 0.0)),
    )

    # Base ground Z reference for legacy recordings without op.height
    all_op_z: List[float] = []
    for r in records:
        if r.get("type") in ("snap", "order"):
            for op in r.get("ops", []):
                p = op.get("p")
                if isinstance(p, list) and len(p) >= 3:
                    all_op_z.append(float(p[2]))
    base_z = min(all_op_z) if all_op_z else 0.0

    # Collect kills / downs
    for r in records:
        if r.get("type") in ("kill", "down"):
            report.kills.append(
                KillRecord(
                    t=float(r.get("t", 0.0)),
                    event_type=str(r["type"]),
                    killer=str(r.get("killer", "")),
                    victim=str(r.get("victim", "")),
                    weapon=str(r.get("weapon", "")),
                    dist_cm=float(r.get("dist", -1.0)),
                    was_victim_aware=r.get("was_victim_aware"),
                    victim_covered_by_ally=r.get("victim_covered_by_ally"),
                )
            )

    first_contact_t = _find_first_contact_time(records)

    # 1. fallback_to_cover_after_contact
    if first_contact_t is not None:
        for r in records:
            t = float(r.get("t", 0.0))
            if t < first_contact_t or t > first_contact_t + FALLBACK_COVER_WINDOW_S:
                continue
            rtype = r.get("type")
            if rtype == "cover_enter":
                report.tactics.append(
                    DetectedTactic(
                        "fallback_to_cover_after_contact",
                        first_contact_t,
                        t,
                        str(r.get("op", "")),
                        f"cover_enter {r.get('height', '')} within {t - first_contact_t:.1f}s of contact",
                    )
                )
                break
            if rtype == "order" and r.get("kind") in ("Cover", "CornerPeek") and r.get("ok", True):
                report.tactics.append(
                    DetectedTactic(
                        "fallback_to_cover_after_contact",
                        first_contact_t,
                        t,
                        str(r.get("actor", "")),
                        f"cover order ({r.get('pmc_order')}) within {t - first_contact_t:.1f}s of contact",
                    )
                )
                break
            if rtype == "snap":
                entered = next((op for op in r.get("ops", []) if _op_is_cover(op)), None)
                if entered is not None:
                    report.tactics.append(
                        DetectedTactic(
                            "fallback_to_cover_after_contact",
                            first_contact_t,
                            t,
                            str(entered.get("id", "")),
                            f"in cover within {t - first_contact_t:.1f}s of contact",
                        )
                    )
                    break

    # Track per-op history for multi-frame tactics
    op_first_pos: Dict[str, Tuple[float, List[float]]] = {}
    op_stealth_start_pos: Dict[str, Tuple[float, List[float]]] = {}
    op_prone_moving_since: Dict[str, float] = {}
    op_moving_to_cover_until: Dict[str, float] = {}
    cover_ops_firing: List[Tuple[float, str]] = []
    high_ground_spans: Dict[str, List[float]] = {}
    pitch_values: List[float] = []
    unwatched_sectors_s: Dict[str, float] = {"front": 0.0, "right": 0.0, "rear": 0.0, "left": 0.0}
    last_snap_t: Optional[float] = None
    last_shot_time_by_op: Dict[str, float] = {}
    last_under_fire_by_op: Dict[str, float] = {}
    retreat_events: List[Tuple[float, str, List[float]]] = []

    for r in records:
        rtype = r.get("type")
        t = float(r.get("t", 0.0))

        if rtype in ("shot_hit", "damage"):
            who = str(r.get("who", ""))
            whom = str(r.get("whom", ""))
            if who:
                last_shot_time_by_op[who] = t
            if whom:
                last_under_fire_by_op[whom] = t

        if rtype == "order":
            actor = str(r.get("actor", ""))
            kind = str(r.get("kind", ""))
            if kind in ("Shot", "QuickFire", "Suppress") and actor:
                last_shot_time_by_op[actor] = t
            if kind == "Cover" and actor:
                op_moving_to_cover_until[actor] = max(op_moving_to_cover_until.get(actor, 0.0), t + 8.0)
            if kind in ("Move", "Cover") and actor:
                facts = r.get("facts", {})
                td = float(facts.get("thr_d", -1.0))
                td_new = float(facts.get("thr_d_new", -1.0))
                if td >= 0.0 and td_new - td >= LURE_RETREAT_MIN_CM:
                    dest = r.get("dest")
                    if isinstance(dest, list) and len(dest) >= 2:
                        retreat_events.append((t, actor, dest))

        if rtype == "cover_enter":
            op_id = str(r.get("op", ""))
            if op_id in op_prone_moving_since:
                report.tactics.append(
                    DetectedTactic(
                        "prone_then_crawl_to_cover",
                        op_prone_moving_since[op_id],
                        t,
                        op_id,
                        "crawled prone into cover",
                    )
                )
                del op_prone_moving_since[op_id]

        if rtype not in ("snap", "order"):
            continue

        ops = r.get("ops", [])
        threats = r.get("threats", [])
        moving_ops = [op for op in ops if op.get("alive", 1) and op.get("mv", False)]

        # Scan pitch & unwatched flanks on snaps
        if rtype == "snap":
            dt = (t - last_snap_t) if last_snap_t is not None else 0.0
            last_snap_t = t
            for op in ops:
                if not op.get("alive", 1):
                    continue
                if "scan_pitch" in op:
                    pitch_values.append(float(op["scan_pitch"]))

            if moving_ops and dt > 0.0 and dt <= 5.0:
                mean_yaw = float(moving_ops[0].get("yaw", 0.0))
                sector_offsets = {"front": 0.0, "right": 90.0, "rear": 180.0, "left": -90.0}
                for sec_name, offset in sector_offsets.items():
                    sec_yaw = mean_yaw + offset
                    watched = any(
                        _angle_diff_deg(float(op.get("yaw", 0.0)), sec_yaw) <= UNWATCHED_SECTOR_HALF_ANGLE_DEG
                        for op in ops
                        if op.get("alive", 1)
                    )
                    if not watched:
                        unwatched_sectors_s[sec_name] += dt

        # Per-op tracking
        for op in ops:
            if not op.get("alive", 1):
                continue
            op_id = str(op.get("id", ""))
            pos = op.get("p")
            if not isinstance(pos, list) or len(pos) < 3:
                continue
            if op_id not in op_first_pos:
                op_first_pos[op_id] = (t, pos)
            if _op_is_stealth(op):
                if op_id not in op_stealth_start_pos:
                    op_stealth_start_pos[op_id] = (t, pos)
            else:
                op_stealth_start_pos.pop(op_id, None)

            if int(op.get("st", 0)) == PRONE_STANCE_INDEX and op.get("mv", False):
                op_prone_moving_since.setdefault(op_id, t)
            if _op_is_cover(op) and op_id in op_prone_moving_since:
                report.tactics.append(
                    DetectedTactic(
                        "prone_then_crawl_to_cover",
                        op_prone_moving_since[op_id],
                        t,
                        op_id,
                        "crawled prone into cover",
                    )
                )
                del op_prone_moving_since[op_id]

            # High ground while engaging (firing or visible threat)
            op_h = float(op.get("height", pos[2] - base_z))
            engaging = (t - last_shot_time_by_op.get(op_id, -1e9) <= OVERWATCH_FIRE_WINDOW_S) or int(op.get("thr_n", 0)) > 0
            if engaging and threats:
                nearest_thr = min(threats, key=lambda th: _dist2d(pos, th["p"]) if isinstance(th.get("p"), list) else 1e9)
                thr_p = nearest_thr.get("p")
                if isinstance(thr_p, list) and len(thr_p) >= 3:
                    dz = pos[2] - float(thr_p[2])
                    if dz >= HIGH_GROUND_MIN_DZ_CM or op_h >= HIGH_GROUND_MIN_DZ_CM:
                        high_ground_spans.setdefault(op_id, []).append(t)

            # First in cover overwatch: op in cover firing while an ally is moving to cover
            if _op_is_cover(op) and (t - last_shot_time_by_op.get(op_id, -1e9) <= OVERWATCH_FIRE_WINDOW_S):
                ally_moving_to_cover = any(
                    str(other.get("id", "")) != op_id
                    and other.get("alive", 1)
                    and other.get("mv", False)
                    and not _op_is_cover(other)
                    for other in ops
                )
                if ally_moving_to_cover:
                    cover_ops_firing.append((t, op_id))

            # Distract & flank / flank into rear via order or kill or threat bearing shift
            if threats:
                nearest_thr = min(threats, key=lambda th: _dist2d(pos, th["p"]) if isinstance(th.get("p"), list) else 1e9)
                thr_p = nearest_thr.get("p")
                if isinstance(thr_p, list) and len(thr_p) >= 2:
                    start_t, start_p = op_stealth_start_pos.get(op_id, op_first_pos[op_id])
                    ang = _flank_angle_deg(start_p, pos, thr_p)
                    if ang >= FLANK_REAR_MIN_ANGLE_DEG:
                        if not any(x.tag == "flank_into_rear" and x.actor == op_id for x in report.tactics):
                            report.tactics.append(
                                DetectedTactic("flank_into_rear", start_t, t, op_id, f"flanked {ang:.0f} deg around target")
                            )
                    if ang >= DISTRACT_FLANK_MIN_ANGLE_DEG:
                        ally_distracting = any(
                            str(other.get("id", "")) != op_id
                            and (
                                _op_under_fire_recent(other)
                                or (t - last_shot_time_by_op.get(str(other.get("id", "")), -1e9) <= UNDER_FIRE_RECENT_S)
                                or (t - last_under_fire_by_op.get(str(other.get("id", "")), -1e9) <= UNDER_FIRE_RECENT_S)
                            )
                            for other in ops
                        )
                        stealth_or_unseen = _op_is_stealth(op) or int(op.get("seen_by_enemy", 0)) == 0
                        has_kill = any(k.killer == op_id and k.event_type == "kill" for k in report.kills)
                        if ally_distracting and stealth_or_unseen and (has_kill or not report.kills):
                            if not any(x.tag == "distract_and_flank" and x.actor == op_id for x in report.tactics):
                                report.tactics.append(
                                    DetectedTactic(
                                        "distract_and_flank",
                                        start_t,
                                        t,
                                        op_id,
                                        f"flanked {ang:.0f} deg while ally drew fire",
                                    )
                                )

    for op_id, ts in high_ground_spans.items():
        report.tactics.append(
            DetectedTactic("high_ground", min(ts), max(ts), op_id, f"elevated >= {HIGH_GROUND_MIN_DZ_CM:.0f} cm while engaging")
        )

    if cover_ops_firing:
        by_op: Dict[str, List[float]] = {}
        for ts, oid in cover_ops_firing:
            by_op.setdefault(oid, []).append(ts)
        for oid, ts in by_op.items():
            report.tactics.append(
                DetectedTactic("first_in_cover_overwatch", min(ts), max(ts), oid, "fired from cover while ally moved")
            )

    # Lure then snipe
    for rt, r_actor, _ in retreat_events:
        for k in report.kills:
            if k.event_type == "kill" and k.killer != r_actor and k.dist_cm >= LURE_SNIPE_MIN_DIST_CM and 0.0 <= k.t - rt <= LURE_SNIPE_WINDOW_S:
                report.tactics.append(
                    DetectedTactic("lure_then_snipe", rt, k.t, k.killer, f"long-range kill {k.dist_cm/100:.1f}m after {r_actor} retreated")
                )

    max_unwatched = max(unwatched_sectors_s.values()) if unwatched_sectors_s else 0.0
    report.unwatched_flank_seconds = round(max_unwatched, 2)
    if max_unwatched > UNWATCHED_FLANK_MIN_SECONDS:
        worst_sec = max(unwatched_sectors_s, key=lambda k: unwatched_sectors_s[k])
        report.tactics.append(
            DetectedTactic(
                "unwatched_flank_seconds",
                0.0,
                report.duration_s,
                "squad",
                f"sector '{worst_sec}' unwatched for {max_unwatched:.1f}s while moving",
            )
        )

    if pitch_values:
        report.scan_pitch_min = round(min(pitch_values), 1)
        report.scan_pitch_max = round(max(pitch_values), 1)
        report.scan_pitch_range = round(report.scan_pitch_max - report.scan_pitch_min, 1)
        report.tactics.append(
            DetectedTactic(
                "scan_pitch_range",
                0.0,
                report.duration_s,
                "squad",
                f"pitch {report.scan_pitch_min:.1f}..{report.scan_pitch_max:.1f} deg (range {report.scan_pitch_range:.1f} deg)",
            )
        )

    report.tactics.sort(key=lambda x: (x.t_start, x.tag))
    return report


def load_jsonl(path: Path) -> List[Dict[str, Any]]:
    records: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def format_report(report: TaggerReport) -> str:
    lines = [
        f"Session: {report.session} | Map: {report.map_name} | Duration: {report.duration_s:.1f}s",
        f"Unwatched flank max: {report.unwatched_flank_seconds:.1f}s | Scan pitch range: {report.scan_pitch_range:.1f} deg ({report.scan_pitch_min:.1f}..{report.scan_pitch_max:.1f})",
        "",
        "TACTICS DETECTED:",
        f"{'Tactic':<32} {'Start':>7} {'End':>7} {'Actor':<16} Detail",
        "-" * 96,
    ]
    if not report.tactics:
        lines.append("  (none)")
    else:
        for tac in report.tactics:
            lines.append(f"{tac.tag:<32} {tac.t_start:7.1f} {tac.t_end:7.1f} {tac.actor:<16} {tac.detail}")
    lines.append("")
    lines.append("KILLS / DOWNS:")
    if not report.kills:
        lines.append("  (none)")
    else:
        for k in report.kills:
            lines.append(
                f"  t={k.t:6.1f}s [{k.event_type}] {k.killer} -> {k.victim} ({k.weapon}, {k.dist_cm/100:.1f}m, aware={k.was_victim_aware}, covered={k.victim_covered_by_ally})"
            )
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Tag tactical patterns in a CodexTactics .jsonl recording.")
    parser.add_argument("jsonl", type=Path, help="Path to .jsonl session recording")
    parser.add_argument("--json", action="store_true", help="Emit JSON summary")
    args = parser.parse_args(argv)
    records = load_jsonl(args.jsonl)
    report = analyze_records(records)
    if args.json:
        payload = {
            "session": report.session,
            "map": report.map_name,
            "duration_s": report.duration_s,
            "unwatched_flank_seconds": report.unwatched_flank_seconds,
            "scan_pitch_range": report.scan_pitch_range,
            "tactics": [vars(t) for t in report.tactics],
            "kills": [vars(k) for k in report.kills],
        }
        print(json.dumps(payload, indent=2))
    else:
        print(format_report(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
