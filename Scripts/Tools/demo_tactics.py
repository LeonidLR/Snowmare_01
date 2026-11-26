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
from typing import Any, Dict, List, Optional


# --- Threshold constants -----------------------------------------------------
FALLBACK_COVER_WINDOW_S = 15.0
HIGH_GROUND_MIN_DZ_CM = 250.0  # ~1 storey
DISTRACT_FLANK_MIN_ANGLE_DEG = 90.0
FLANK_REAR_MIN_ANGLE_DEG = 120.0
OVERWATCH_FIRE_WINDOW_S = 3.0
UNDER_FIRE_RECENT_S = 6.0
UNWATCHED_SECTOR_HALF_ANGLE_DEG = 60.0
UNWATCHED_FLANK_MIN_SECONDS = 5.0
PRONE_STANCE_INDEX = 2
CRAWL_MIN_DISTANCE_CM = 50.0
FLANK_MIN_MOVEMENT_CM = 300.0
COVER_ORDER_WINDOW_S = 8.0
MAX_SAMPLE_GAP_S = 5.0
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
    evidence: str = "recorded"
    round_id: str = ""


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


def analyze_records(records: List[Dict[str, Any]]) -> TaggerReport:
    """Use only evidence in the same round; missing fields remain unknown.

    Stable sorting also accepts inputs that group events and frames separately,
    and preserves event order when timestamps are equal.
    """
    header = next((r for r in records if r.get("type") == "header"), {})
    footer = next((r for r in reversed(records) if r.get("type") == "footer"), {})
    result = TaggerReport(str(header.get("session", "")), str(header.get("map", "")),
                          float(footer.get("duration_s", 0)))
    ordered = sorted((r for r in records if r.get("type") not in ("header", "footer")),
                     key=lambda r: (float(r.get("t", 0)),
                         -1 if r.get("type") == "round_start" else 1 if r.get("type") == "round_end" else 0))
    segments, current, round_id = [], [], "legacy"
    for r in ordered:
        if r.get("type") == "round_start":
            if current:
                segments.append((round_id, current))
            current, round_id = [], str(r.get("round", ""))
        elif r.get("type") == "round_end":
            segments.append((round_id, current))
            current, round_id = [], "between_rounds"
        else:
            current.append(r)
    if current:
        segments.append((round_id, current))
    pitches = []
    for rid, segment in segments:
        if rid == "between_rounds":
            continue
        _analyze_segment(segment, result, rid, pitches)
    if pitches:
        result.scan_pitch_min, result.scan_pitch_max = min(pitches), max(pitches)
        result.scan_pitch_range = result.scan_pitch_max - result.scan_pitch_min
        result.tactics.append(DetectedTactic("scan_pitch_range", 0, result.duration_s, "squad",
                              f"pitch {min(pitches):.1f}..{max(pitches):.1f} deg"))
    result.tactics.sort(key=lambda x: (x.t_start, x.tag))
    return result


def _analyze_segment(records, report, round_id, pitches):
    previous, stealth_starts, prone, shots, incoming = {}, {}, {}, {}, {}
    flank_candidates, retreats, threats_previous = {}, {}, {}
    flank_starts = {}
    cover_orders, unwatched, high_spans, overwatch = {}, {}, {}, {}
    engagements = {}
    contact = None
    fallback_done = set()
    last_t = None
    emitted = set()

    def emit(tag, start, end, actor, detail, evidence="recorded"):
        key = tag, actor
        if key not in emitted:
            emitted.add(key)
            report.tactics.append(DetectedTactic(tag, start, end, actor, detail, evidence, round_id))

    def recent(table, actor, t, window=UNDER_FIRE_RECENT_S):
        return 0 <= t - table.get(actor, -1e9) <= window

    def target_id(r):
        target = r.get("target", {})
        return str(target.get("id", "")) if isinstance(target, dict) else str(target) if target else ""

    for r in records:
        t, typ = float(r.get("t", 0)), r.get("type")
        if typ in ("shot_hit", "damage"):
            shots[str(r.get("who", ""))] = t
            incoming[str(r.get("whom", ""))] = t
            engagements[(str(r.get("who", "")), str(r.get("whom", "")))] = t
            if contact is None:
                contact = t
        if typ == "order" and r.get("ok", True):
            actor, kind = str(r.get("actor", "")), r.get("kind")
            if kind in ("Shot", "QuickFire", "Suppress"):
                shots[actor] = t
                if contact is None:
                    contact = t
            if kind in ("Cover", "CornerPeek"):
                cover_orders[actor] = t
            # An engaged retreat with a named target is only the start of a lure.
            facts = r.get("facts", {})
            tid = target_id(r)
            if not tid:
                actor_view = next((o for o in r.get("ops", []) if o.get("id") == actor), {})
                candidates = [th for th in r.get("threats", []) if isinstance(th.get("p"), list)]
                if actor_view.get("p") and candidates:
                    tid = str(min(candidates, key=lambda th: _dist2d(actor_view["p"], th["p"])).get("id", ""))
            if kind in ("Move", "Cover") and tid and facts.get("thr_d", -1) >= 0:
                if facts.get("thr_d_new", -1) - facts["thr_d"] >= LURE_RETREAT_MIN_CM:
                    if tid in threats_previous and isinstance(r.get("dest"), list):
                        retreats[tid] = (t, actor, threats_previous[tid]["p"], r["dest"])
        if typ in ("kill", "down"):
            killer, victim = str(r.get("killer", "")), str(r.get("victim", ""))
            report.kills.append(KillRecord(t, typ, killer, victim, str(r.get("weapon", "")),
                               float(r.get("dist", -1)), r.get("was_victim_aware"),
                               r.get("victim_covered_by_ally")))
            candidate = flank_candidates.get((killer, victim))
            if typ == "kill" and candidate and 0 <= t - candidate[1] <= UNDER_FIRE_RECENT_S:
                emit("distract_and_flank", candidate[0], t, killer,
                     f"stealth flank around {victim}; ally engaged; matching kill")
            lure = retreats.get(victim)
            target = threats_previous.get(victim, {})
            # Open ground must be explicitly recorded, never inferred from absence.
            if (typ == "kill" and lure and killer != lure[1]
                    and 0 <= t - lure[0] <= LURE_SNIPE_WINDOW_S
                    and r.get("dist", -1) >= LURE_SNIPE_MIN_DIST_CM
                    and target.get("in_cover") is False
                    and 0 <= t - target.get("_observed_t", -1e9) <= MAX_SAMPLE_GAP_S
                    and _dist2d(target["p"], lure[2]) >= LURE_RETREAT_MIN_CM
                    and _dist2d(lure[2], lure[3]) - _dist2d(target["p"], lure[3]) >= LURE_RETREAT_MIN_CM):
                emit("lure_then_snipe", lure[0], t, killer,
                     f"{victim} advanced into open ground after {lure[1]} retreated")
        if typ not in ("snap", "order"):
            continue
        ops = {str(o.get("id", "")): o for o in r.get("ops", []) if o.get("alive", True)}
        threats = {str(th.get("id", "")): th for th in r.get("threats", [])
                   if isinstance(th.get("p"), list) and len(th["p"]) >= 3}
        if contact is None and any(o.get("thr_n", 0) > 0 or _op_under_fire_recent(o) for o in ops.values()):
            contact = t
        # Clip drops recover firing from legacy snapshots; a reload never does.
        for oid, op in ops.items():
            prev = previous.get(oid, {})
            if "clip" in op and "clip" in prev and op["clip"] < prev["clip"]:
                shots[oid] = t
            if "scan_pitch" in op:
                pitches.append(float(op["scan_pitch"]))
            pos = op.get("p")
            if not isinstance(pos, list) or len(pos) < 3:
                continue
            entered_cover = _op_is_cover(op) and prev and not _op_is_cover(prev)
            if (contact is not None and 0 <= t - contact <= FALLBACK_COVER_WINDOW_S
                    and entered_cover and oid not in fallback_done):
                emit("fallback_to_cover_after_contact", contact, t, oid,
                     "cover transition after contact", "snapshot transition")
                fallback_done.add(oid)
            if int(op.get("st", -1)) != PRONE_STANCE_INDEX:
                prone.pop(oid, None)
            elif op.get("mv"):
                prone.setdefault(oid, (t, pos))
            if entered_cover and oid in prone:
                start, start_pos = prone.pop(oid)
                if _dist2d(start_pos, pos) >= CRAWL_MIN_DISTANCE_CM:
                    emit("prone_then_crawl_to_cover", start, t, oid, "prone movement into cover")
            if _op_is_stealth(op):
                for tid, th in threats.items():
                    stealth_starts.setdefault((oid, tid), (t, pos))
            else:
                stealth_starts = {k: v for k, v in stealth_starts.items() if k[0] != oid}
            firing = recent(shots, oid, t, OVERWATCH_FIRE_WINDOW_S)
            if threats:
                tid, th = min(threats.items(), key=lambda item: _dist2d(pos, item[1]["p"]))
                # Both actor positions are capsule centres: height above own roof
                # cannot substitute for height relative to the nearest enemy.
                if (firing or op.get("thr_n", 0) > 0) and pos[2] - th["p"][2] >= HIGH_GROUND_MIN_DZ_CM:
                    high_spans.setdefault(oid, []).append(t)
                for tid, th in threats.items():
                    rear_start = flank_starts.setdefault((oid, tid), (t, pos))
                    rear_angle = _flank_angle_deg(rear_start[1], pos, th["p"])
                    if (rear_angle > FLANK_REAR_MIN_ANGLE_DEG and "yaw" in th
                            and _dist2d(rear_start[1], pos) >= FLANK_MIN_MOVEMENT_CM
                            and _angle_diff_deg(_bearing_deg(th["p"], pos), th["yaw"]) > FLANK_REAR_MIN_ANGLE_DEG):
                        emit("flank_into_rear", rear_start[0], t, oid, f"behind {tid}'s recorded facing")
                    start = stealth_starts.get((oid, tid))
                    if not start or _dist2d(start[1], pos) < FLANK_MIN_MOVEMENT_CM:
                        continue
                    angle = _flank_angle_deg(start[1], pos, th["p"])
                    distract = any(other != oid and (recent(engagements, (other, tid), t)
                                   or recent(engagements, (tid, other), t))
                                   for other in ops)
                    high_overwatch = any(aid != oid and ally.get("p")
                        and ally["p"][2] - th["p"][2] >= HIGH_GROUND_MIN_DZ_CM
                        and recent(shots, aid, t, OVERWATCH_FIRE_WINDOW_S) for aid, ally in ops.items())
                    if (op.get("seen_by_enemy") == 0 and high_overwatch
                            and _dist2d(start[1], th["p"]) - _dist2d(pos, th["p"]) >= FLANK_MIN_MOVEMENT_CM):
                        emit("approach_unseen", start[0], t, oid, f"stealth approach to {tid} under elevated overwatch")
                    if angle > DISTRACT_FLANK_MIN_ANGLE_DEG and distract:
                        flank_candidates[(oid, tid)] = (start[0], t)
            if _op_is_cover(op) and firing:
                for aid, ally in ops.items():
                    if aid == oid or not ally.get("mv") or _op_is_cover(ally):
                        continue
                    if recent(cover_orders, aid, t, COVER_ORDER_WINDOW_S):
                        overwatch.setdefault(oid, []).append(t)
        # Consecutive gaps only. Reset on a watched sector, stopped squad or sample gap.
        if typ == "snap":
            delta = t - last_t if last_t is not None else 0
            moving = [o for o in ops.values() if o.get("mv")]
            anchor = float(moving[0].get("yaw", 0)) if moving else 0
            for offset in (0, 90, 180, -90):
                watched = any(("look_yaw" in o or "yaw" in o) and _angle_diff_deg(o.get("look_yaw", o.get("yaw")), anchor + offset) <= UNWATCHED_SECTOR_HALF_ANGLE_DEG for o in ops.values())
                key = offset
                if moving and not watched and 0 < delta <= MAX_SAMPLE_GAP_S:
                    unwatched[key] = unwatched.get(key, 0) + delta
                    report.unwatched_flank_seconds = max(report.unwatched_flank_seconds, unwatched[key])
                    if unwatched[key] > UNWATCHED_FLANK_MIN_SECONDS:
                        emit("unwatched_flank_seconds", t-unwatched[key], t, "squad",
                             f"relative sector {offset:+} deg unwatched continuously")
                else:
                    unwatched[key] = 0
            last_t = t
        previous.update(ops)
        threats_previous.update({tid: dict(th, _observed_t=t) for tid, th in threats.items()})
    for oid, times in high_spans.items():
        emit("high_ground", min(times), max(times), oid, "one storey above nearest enemy while engaging", "snapshot geometry")
    for oid, times in overwatch.items():
        emit("first_in_cover_overwatch", min(times), max(times), oid, "fired from cover while ally executed cover order")


def load_jsonl(path: Path) -> List[Dict[str, Any]]:
    records: List[Dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def format_report(report: TaggerReport) -> str:
    pitch = (f"{report.scan_pitch_range:.1f} deg ({report.scan_pitch_min:.1f}..{report.scan_pitch_max:.1f})"
             if any(t.tag == "scan_pitch_range" for t in report.tactics) else "unknown (not recorded)")
    lines = [
        f"Session: {report.session} | Map: {report.map_name} | Duration: {report.duration_s:.1f}s",
        f"Unwatched flank max: {report.unwatched_flank_seconds:.1f}s | Scan pitch range: {pitch}",
        "",
        "TACTICS DETECTED:",
        f"{'Tactic':<32} {'Start':>7} {'End':>7} {'Actor':<16} Detail",
        "-" * 96,
    ]
    if not report.tactics:
        lines.append("  (none)")
    else:
        for tac in report.tactics:
            lines.append(f"{tac.tag:<32} {tac.t_start:7.1f} {tac.t_end:7.1f} {tac.actor:<16} {tac.detail} [round={tac.round_id}; {tac.evidence}]")
    lines.append("")
    lines.append("KILLS / DOWNS:")
    if not report.kills:
        lines.append("  (none recorded; legacy absence does not establish zero kills)")
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
            "scan_pitch_range": report.scan_pitch_range if any(t.tag == "scan_pitch_range" for t in report.tactics) else None,
            "tactics": [vars(t) for t in report.tactics],
            "kills": [vars(k) for k in report.kills],
        }
        print(json.dumps(payload, indent=2))
    else:
        print(format_report(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
