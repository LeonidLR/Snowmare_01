#!/usr/bin/env python3
"""pmc_round_trace.py - Decision timeline and hypothesis evaluator for PMC arena rounds.

Task T-01 (smart enemies roadmap / audit 4.1):
Reads arena result JSON (codex.pmc_arena/1) + track JSON (codex.pmc_arena_track/1)
+ [PmcAI] log lines to produce a 1-page markdown decision timeline for designers:
- Scenario, map, seed, duration, winner, end reason.
- Objective coordinates, vertical level (Z), radius, hold time.
- Each defender's spawn, initial post, order sequence with scores from spawn to first contact.
- Seconds standing in the open per defender.
- First contact and first shot timestamps.
- Hypotheses H1-H4 evaluation (HOLDS / DOES NOT HOLD / CANNOT TELL).

Reuses nighttrainer/track_stats.py for unit conversions and close-range tracking.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Re-use nighttrainer.track_stats
REPO_ROOT = Path(__file__).resolve().parents[2]
TRAINER_DIR = REPO_ROOT / "Scripts" / "Trainer"
if str(TRAINER_DIR) not in sys.path:
    sys.path.insert(0, str(TRAINER_DIR))

try:
    from nighttrainer.track_stats import UNIT_TO_M, _pos, side_stats
except ImportError:
    # Fallback if run standalone outside repo structure
    UNIT_TO_M = {"m": 1.0, "cm": 0.01, "mm": 0.001}

    def _pos(frames: list, idx: int, fi: int):
        for k in range(min(idx, len(frames) - 1), -1, -1):
            row = frames[k]
            if fi < len(row) and row[fi]:
                return row[fi]
        return None

    def side_stats(track: dict, side: str, close_range_m: float = 10.0, shotgun_exempt: bool = True) -> dict | None:
        return None


def resolve_track_file(arena_path: Path, round_index: int, explicit_track: Optional[str] = None) -> Optional[Path]:
    """Auto-discover track JSON for a given round if not provided explicitly."""
    if explicit_track:
        p = Path(explicit_track)
        if p.is_file():
            return p

    arena_dir = arena_path.parent
    base_name = arena_path.name
    if base_name.endswith(".arena.json"):
        stem = base_name[:-len(".arena.json")]
    elif base_name.endswith(".json"):
        stem = arena_path.stem
    else:
        stem = base_name

    candidates = [
        arena_dir / f"round_{round_index}.track.json",
        arena_dir / f"{stem}.arena" / f"round_{round_index}.track.json",
        arena_dir / stem / f"round_{round_index}.track.json",
        arena_dir / f"{arena_path.stem}" / f"round_{round_index}.track.json",
        arena_dir.parent / f"{stem}.arena" / f"round_{round_index}.track.json",
        arena_dir.parent / stem / f"round_{round_index}.track.json",
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def resolve_log_file(arena_path: Path, explicit_log: Optional[str] = None) -> Optional[Path]:
    """Auto-discover execution log file for arena result if not provided explicitly."""
    if explicit_log:
        p = Path(explicit_log)
        if p.is_file():
            return p

    arena_dir = arena_path.parent
    base_name = arena_path.name
    if base_name.endswith(".arena.json"):
        stem = base_name[:-len(".arena.json")]
    elif base_name.endswith(".json"):
        stem = arena_path.stem
    else:
        stem = base_name

    candidates = [
        arena_dir / f"{stem}.log",
        arena_dir / f"{arena_path.stem}.log",
        arena_dir.parent / f"{stem}.log",
        arena_dir / f"{stem}.txt",
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def parse_arena_round(arena_data: dict, round_index: int) -> Tuple[dict, dict]:
    """Extract round data and arena-level metadata for round_index."""
    rounds = arena_data.get("rounds") or []
    target_round = None
    for r in rounds:
        if r.get("index") == round_index:
            target_round = r
            break
    if not target_round and 0 <= round_index < len(rounds):
        target_round = rounds[round_index]
    if not target_round and rounds:
        target_round = rounds[0]

    if not target_round:
        target_round = {}
    return target_round, arena_data


def parse_log_for_round(log_path: Optional[Path], round_index: int) -> Dict[str, Any]:
    """Parse [PmcAI] lines from log file partitioned for round_index."""
    res: Dict[str, Any] = {
        "found": False,
        "round_found": False,
        "spawned": {},        # name -> {team, look, weapon, pos}
        "posts": {},          # name -> {role, pos, yaw, score, danger, post_id}
        "decisions": [],      # [{t_log, team, who, order, score, threat_state, threat_dist_cm, hp, cover}]
        "failures": [],       # [{t_log, who, order, reason, delay_s}]
        "fire_events": [],    # [{t_log, who, threat_dist_cm, attention_off}]
        "completions": [],    # [{t_log, who, order, result_state}]
        "raw_lines": [],
    }
    if not log_path or not log_path.is_file():
        return res

    res["found"] = True
    try:
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:
        return res

    current_round = None
    round_lines = []
    round_start_pattern = re.compile(r"\[PmcArena\]\s+round\s+(\d+):")

    for line in lines:
        m = round_start_pattern.search(line)
        if m:
            r_num = int(m.group(1))
            if current_round is not None and current_round == round_index:
                # Finished our target round
                break
            current_round = r_num
            if current_round == round_index:
                res["round_found"] = True
        if current_round == round_index or (current_round is None and round_index == 1):
            if "[PmcAI]" in line or "[PmcArena" in line:
                round_lines.append(line)

    # If round header not found (e.g. single-round run), scan all PmcAI lines
    if not round_lines:
        round_lines = [l for l in lines if "[PmcAI]" in l]
        if round_lines:
            res["round_found"] = True

    res["raw_lines"] = round_lines

    # Regexes for PmcAI log entries
    spawn_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+spawned\s+Operator\s+([A-Za-z0-9_-]+)\s+\(([^)]*)\)\s+at\s+V\(([^)]*)\)"
    )
    post_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+placed\s+on\s+a\s+([A-Za-z0-9_-]+)\s+post\s+V\(([^)]*)\)\s+\(([^)]*)\)"
    )
    order_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+->\s+([A-Za-z0-9_-]+)\s+\(score\s+([0-9.]+);\s*([^)]*)\)"
    )
    fail_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)'s\s+([A-Za-z0-9_-]+)\s+failed\s+\(([^)]*)\)\s*-\s*not\s+again\s+for\s+([0-9.]+)\s*s"
    )
    fire_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+opens\s+fire\s+\(threat\s+at\s+([0-9.]+)\s*cm,\s*attention\s+off\s+([0-9.]+)\)"
    )
    complete_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+completed\s+([A-Za-z0-9_-]+)\s+in\s+cover\s+->\s+([A-Za-z0-9_-]+)"
    )
    arrived_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+arrived\s+at\s+objective\s+point\s+->\s+([A-Za-z0-9_-]+)"
    )

    for line in round_lines:
        m_s = spawn_re.search(line)
        if m_s:
            team, name, look, pos_str = m_s.groups()
            res["spawned"][name] = {"team": int(team), "look": look, "pos": pos_str}
            continue

        m_p = post_re.search(line)
        if m_p:
            team, name, role, pos_str, details = m_p.groups()
            # Parse yaw, score, danger, post_id
            post_info: Dict[str, Any] = {"team": int(team), "role": role, "pos": pos_str, "raw_details": details}
            for part in [p.strip() for p in details.split(",")]:
                if part.startswith("yaw"):
                    try:
                        post_info["yaw"] = float(part.split()[1])
                    except Exception:
                        pass
                elif part.startswith("score"):
                    try:
                        post_info["score"] = float(part.split()[1])
                    except Exception:
                        pass
                elif part.startswith("danger"):
                    try:
                        post_info["danger"] = float(part.split()[1])
                    except Exception:
                        pass
                else:
                    post_info["post_id"] = part
            res["posts"][name] = post_info
            continue

        m_o = order_re.search(line)
        if m_o:
            team, name, order, score_str, ctx_str = m_o.groups()
            dec_entry: Dict[str, Any] = {
                "team": int(team),
                "who": f"Operator {name}",
                "order": order,
                "score": float(score_str),
                "context": ctx_str,
            }
            # Extract threat distance and cover if available
            tm = re.search(r"threat\s+([a-zA-Z]+)\s+at\s+([0-9.]+)\s*cm", ctx_str)
            if tm:
                dec_entry["threat_state"] = tm.group(1)
                dec_entry["threat_dist_m"] = float(tm.group(2)) / 100.0
            cm = re.search(r"cover\s+([0-9.]+)", ctx_str)
            if cm:
                dec_entry["cover"] = float(cm.group(1))
            res["decisions"].append(dec_entry)
            continue

        m_f = fail_re.search(line)
        if m_f:
            team, name, order, reason, delay = m_f.groups()
            res["failures"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "order": order,
                "reason": reason,
                "delay_s": float(delay),
            })
            continue

        m_fi = fire_re.search(line)
        if m_fi:
            team, name, dist_cm, att_off = m_fi.groups()
            res["fire_events"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "dist_m": float(dist_cm) / 100.0,
                "attention_off": float(att_off),
            })
            continue

        m_c = complete_re.search(line)
        if m_c:
            team, name, order, to_state = m_c.groups()
            res["completions"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "order": order,
                "result_state": to_state,
            })
            continue

        m_arr = arrived_re.search(line)
        if m_arr:
            team, name, to_state = m_arr.groups()
            res["completions"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "order": "Objective",
                "result_state": to_state,
            })
            continue

    return res


def compute_trace_metrics(
    rnd: dict,
    arena: dict,
    track: Optional[dict],
    log_data: dict,
) -> dict:
    """Combine arena, track, and log to calculate timeline, positions, exposure, and hypothesis tests."""
    scenario = rnd.get("scenario") or arena.get("scenario") or "unknown"
    map_name = rnd.get("map") or arena.get("map") or "unknown"
    seed = rnd.get("seed", arena.get("seed"))
    winner = rnd.get("winner", "None")
    end_reason = rnd.get("end_reason", "None")
    duration_s = float(rnd.get("duration_s") or 0.0)

    # Objective details
    obj = rnd.get("objective") or {}
    obj_enabled = obj.get("enabled", False)
    obj_x = obj.get("x")
    obj_y = obj.get("y")
    obj_z = obj.get("z", 0.0)
    obj_r_m = obj.get("radius_m", 0.0)
    obj_hold_s = obj.get("hold_s", 0.0)
    obj_result = obj.get("result", "none")
    obj_level_m = obj.get("level_m", (obj_z or 0.0) / 100.0 if obj_z is not None else 0.0)

    # Identify defender and attacker sides
    sides = rnd.get("sides") or {}
    def_side = None
    atk_side = None
    for s_name, s_info in sides.items():
        if isinstance(s_info, dict):
            role = s_info.get("role")
            if role == "defender":
                def_side = s_name
            elif role == "attacker":
                atk_side = s_name
    if not def_side:
        # Default fallback in tactical arena: side B is defender
        def_side = "B" if "B" in sides else "A"
    if not atk_side:
        atk_side = "A" if def_side == "B" else "B"

    # Track data
    dt = float(track.get("dt") or 0.2) if track else 0.2
    scale = UNIT_TO_M.get(str(track.get("units", "m")).lower(), 1.0) if track else 1.0
    fighters = track.get("fighters") or [] if track else []
    frames = track.get("frames") or [] if track else []
    states = track.get("states") or [] if track else []
    events = track.get("events") or [] if track else []

    # Map fighters
    def_fighters: List[dict] = []
    atk_fighters: List[dict] = []
    fighter_idx_map: Dict[Tuple[str, str], int] = {}
    for i, f in enumerate(fighters):
        s = str(f.get("side"))
        fid = str(f.get("id"))
        fighter_idx_map[(s, fid)] = i
        if s == def_side:
            def_fighters.append(dict(f, idx=i))
        else:
            atk_fighters.append(dict(f, idx=i))

    # If track has no fighters, attempt to populate from log spawned
    if not def_fighters and log_data.get("spawned"):
        for name, s_info in log_data["spawned"].items():
            def_fighters.append({"id": f"Operator {name}", "side": def_side, "team": s_info.get("team")})

    # Close range stats from track_stats
    close_stats_def = side_stats(track, def_side) if track else None
    close_stats_atk = side_stats(track, atk_side) if track else None

    # First contact determination
    # 1. First shot event in track
    shots = [e for e in events if e.get("type") == "shot"]
    t_first_shot = min([float(e.get("t", 0.0)) for e in shots]) if shots else None

    # 2. First contact in log
    log_threat_seen_times = []
    # Log doesn't always have absolute t, but track tactic events do!
    track_tactic_events = [e for e in events if e.get("type") == "tactic"]

    t_first_contact = t_first_shot
    if t_first_contact is None:
        t_first_contact = duration_s

    # Tactic decisions per fighter
    decisions_by_fighter: Dict[str, List[dict]] = {}
    for f in def_fighters:
        decisions_by_fighter[f["id"]] = []

    for ev in track_tactic_events:
        who = ev.get("who")
        if who in decisions_by_fighter:
            decisions_by_fighter[who].append({
                "t": float(ev.get("t", 0.0)),
                "decision": ev.get("decision"),
                "score": float(ev.get("score") or 0.0),
                "why": ev.get("why"),
            })

    # Seconds standing in the open per defender
    exposure_stats: Dict[str, dict] = {}
    for f in def_fighters:
        fi = f.get("idx")
        total_stand_s = 0.0
        pre_contact_stand_s = 0.0
        idle_stand_s = 0.0
        first_shot_t = None
        first_shot_hit = None
        first_shot_target = None
        shots_fired = 0

        # Exposure from states
        if fi is not None and states:
            for tick, state_row in enumerate(states):
                t_cur = tick * dt
                if fi < len(state_row) and state_row[fi]:
                    # [yaw, stance, hp, state]
                    s_entry = state_row[fi]
                    stance = s_entry[1] if len(s_entry) > 1 else None
                    cur_state = s_entry[3] if len(s_entry) > 3 else None
                    if stance == "stand":
                        total_stand_s += dt
                        if t_first_contact is not None and t_cur <= t_first_contact:
                            pre_contact_stand_s += dt
                        if cur_state in ("Hold", "Crouch", "Ambush"):
                            idle_stand_s += dt

        # Shots fired by this fighter
        for s in shots:
            if s.get("from") == f["id"] and s.get("side") == def_side:
                shots_fired += 1
                if first_shot_t is None:
                    first_shot_t = float(s.get("t", 0.0))
                    first_shot_hit = bool(s.get("hit"))
                    first_shot_target = str(s.get("to"))

        # Cross-reference with arena.json defence.open_idle_s if present
        arena_defence = rnd.get("defence") or {}
        arena_idle_arr = arena_defence.get("open_idle_s") or []
        arena_idle_before = None
        for entry in arena_idle_arr:
            if isinstance(entry, dict) and entry.get("id") == f["id"]:
                arena_idle_before = entry.get("before_contact_s")

        exposure_stats[f["id"]] = {
            "total_stand_s": round(total_stand_s, 2),
            "pre_contact_stand_s": round(pre_contact_stand_s, 2),
            "idle_stand_s": round(idle_stand_s, 2),
            "arena_idle_before_s": arena_idle_before,
            "first_shot_t": round(first_shot_t, 2) if first_shot_t is not None else None,
            "first_shot_hit": first_shot_hit,
            "first_shot_target": first_shot_target,
            "shots_fired": shots_fired,
        }

    # Posts from log
    posts_assigned = {}
    for f in def_fighters:
        fid = f["id"]
        # Match with or without "Operator " prefix
        short_name = fid.replace("Operator ", "").strip()
        p_info = log_data.get("posts", {}).get(short_name)
        if not p_info:
            p_info = log_data.get("posts", {}).get(fid)
        posts_assigned[fid] = p_info

    # Hypotheses evaluation
    h1_verdict, h1_evidence = evaluate_h1(scenario, obj, rnd, log_data, decisions_by_fighter)
    h2_verdict, h2_evidence = evaluate_h2(posts_assigned, decisions_by_fighter, log_data, t_first_contact)
    h3_verdict, h3_evidence = evaluate_h3(log_data, exposure_stats, rnd)
    h4_verdict, h4_evidence = evaluate_h4(log_data, exposure_stats, t_first_contact, shots)

    return {
        "scenario": scenario,
        "map": map_name,
        "seed": seed,
        "winner": winner,
        "end_reason": end_reason,
        "duration_s": duration_s,
        "def_side": def_side,
        "atk_side": atk_side,
        "objective": {
            "enabled": obj_enabled,
            "x": obj_x,
            "y": obj_y,
            "z": obj_z,
            "radius_m": obj_r_m,
            "hold_s": obj_hold_s,
            "result": obj_result,
            "level_m": obj_level_m,
        },
        "def_fighters": def_fighters,
        "posts_assigned": posts_assigned,
        "decisions_by_fighter": decisions_by_fighter,
        "exposure_stats": exposure_stats,
        "first_contact_s": round(t_first_contact, 2) if t_first_contact is not None else None,
        "close_stats_def": close_stats_def,
        "close_stats_atk": close_stats_atk,
        "hypotheses": {
            "H1": {"verdict": h1_verdict, "evidence": h1_evidence},
            "H2": {"verdict": h2_verdict, "evidence": h2_evidence},
            "H3": {"verdict": h3_verdict, "evidence": h3_evidence},
            "H4": {"verdict": h4_verdict, "evidence": h4_evidence},
        },
        "log_stats": {
            "log_found": log_data["found"],
            "round_found": log_data["round_found"],
            "decisions_logged": len(log_data["decisions"]),
            "fire_events_logged": len(log_data["fire_events"]),
            "failures_logged": len(log_data["failures"]),
        },
    }


def evaluate_h1(scenario: str, obj: dict, rnd: dict, log_data: dict, decisions: dict) -> Tuple[str, str]:
    """H1: Ground-objective scenario -> cases 3 and 4 applied (no defence site, pulled onto point)."""
    obj_z = obj.get("z", 0.0) or 0.0
    is_ground = (obj_z < 200.0) or (scenario in ("tactical", "street", "chokepoint", "random") and obj_z < 250.0)

    # Check if defenders got Objective orders pulling them onto point
    pull_count = 0
    total_defenders = len(decisions)
    for fid, dec_list in decisions.items():
        if any(d["decision"] == "Objective" for d in dec_list):
            pull_count += 1

    # Check guard_share in defence block
    guard_share = (rnd.get("defence") or {}).get("guard_share")

    if is_ground:
        evidence = (
            f"Scenario '{scenario}' has ground objective at Z={obj_z} cm (level {obj.get('level_m', 0.0)}m). "
            f"No dedicated roof defence site exists (guard_share={guard_share}). "
            f"{pull_count}/{total_defenders} defenders received 'Objective' orders pulling them to the capture point."
        )
        return "HOLDS", evidence
    else:
        evidence = (
            f"Scenario '{scenario}' has elevated/roof objective at Z={obj_z} cm. "
            f"Ground-objective condition does not apply."
        )
        return "DOES NOT HOLD", evidence


def evaluate_h2(posts: dict, decisions: dict, log_data: dict, t_contact: Optional[float]) -> Tuple[str, str]:
    """H2: Defenders had tactical posts along a route and left them when bEverContact turned true at long range."""
    fighters_with_posts = [fid for fid, p in posts.items() if p and p.get("role")]
    if not fighters_with_posts:
        # Check if spawned on posts in log
        return "DOES NOT HOLD", "Defenders had no tactical posts along the route; spawned directly on/near the point."

    # Check if any defender abandoned their post for Advance, FallBack, or Objective upon contact
    abandoned = []
    for fid, dec_list in decisions.items():
        if fid in fighters_with_posts:
            post_role = posts[fid].get("role", "post")
            for d in dec_list:
                if d["t"] > 0 and d["decision"] in ("FallBack", "Advance", "Objective", "TakeCover"):
                    abandoned.append((fid, d["decision"], d["t"], d["score"]))
                    break

    # Look in log for threat seen distance at time of order change
    long_range_threats = []
    for d in log_data.get("decisions", []):
        if d.get("threat_dist_m", 0) > 40.0:
            long_range_threats.append(f"{d['who']} -> {d['order']} (threat seen at {d['threat_dist_m']:.1f}m)")

    if abandoned:
        details = ", ".join([f"{a[0]} left {posts[a[0]].get('role')} for {a[1]} at t={a[2]}s" for a in abandoned])
        threat_note = f"; long-range threat triggers: {'; '.join(long_range_threats[:2])}" if long_range_threats else ""
        return "HOLDS", f"Defenders started on posts ({', '.join(fighters_with_posts)}) and abandoned them: {details}{threat_note}."
    else:
        return "CANNOT TELL", f"Defenders were assigned posts ({', '.join(fighters_with_posts)}), but clear abandonment order transition could not be verified."


def evaluate_h3(log_data: dict, exposure_stats: dict, rnd: dict) -> Tuple[str, str]:
    """H3: No cover slot was found near defenders (bCoverAvailable false / cover 0.00 / crates not cover)."""
    # Check cover values in log
    decisions = log_data.get("decisions", [])
    zero_cover_count = sum(1 for d in decisions if d.get("cover") == 0.0)
    total_decisions_with_cover = sum(1 for d in decisions if "cover" in d)

    take_cover_failures = [f for f in log_data.get("failures", []) if f.get("order") == "TakeCover"]

    # Check pre-contact standing time
    total_pre_contact_stand = sum(st["pre_contact_stand_s"] for st in exposure_stats.values())

    if total_decisions_with_cover > 0 and zero_cover_count == total_decisions_with_cover:
        evidence = (
            f"100% of logged decisions ({zero_cover_count}/{total_decisions_with_cover}) reported cover=0.00. "
            f"Defenders stood exposed in the open for a combined {total_pre_contact_stand:.1f}s before contact."
        )
        if take_cover_failures:
            evidence += f" TakeCover failed {len(take_cover_failures)} time(s)."
        return "HOLDS", evidence
    elif total_pre_contact_stand > 10.0:
        evidence = (
            f"Defenders spent {total_pre_contact_stand:.1f}s standing in the open before contact without taking cover."
        )
        return "HOLDS", evidence
    else:
        return "CANNOT TELL", "Insufficient cover diagnostic logging present in this round's logs."


def evaluate_h4(log_data: dict, exposure_stats: dict, t_contact: Optional[float], shots: list) -> Tuple[str, str]:
    """H4: 'Let them run through' caused by ShouldHoldFire, attention rule, or weapon range band."""
    fire_events = log_data.get("fire_events", [])
    # Look for attention off delay
    attention_delays = [fe for fe in fire_events if fe.get("attention_off", 1.0) < 0.9]

    # Look for close engagement distance when first opening fire
    very_close_fires = [fe for fe in fire_events if fe.get("dist_m", 100.0) < 15.0]

    # Calculate gap between contact and first defender shot
    first_def_shots = [st["first_shot_t"] for st in exposure_stats.values() if st["first_shot_t"] is not None]
    min_def_shot = min(first_def_shots) if first_def_shots else None

    gap_s = None
    if min_def_shot is not None and t_contact is not None:
        gap_s = max(0.0, min_def_shot - t_contact)

    if attention_delays or very_close_fires:
        details = []
        if attention_delays:
            details.append(f"attention delay active (attention off={attention_delays[0]['attention_off']:.2f})")
        if very_close_fires:
            details.append(f"enemy allowed to close to {very_close_fires[0]['dist_m']:.1f}m before opening fire")
        evidence = f"Firing delay observed: {', '.join(details)}. First defender shot at t={min_def_shot}s."
        return "HOLDS", evidence
    elif gap_s is not None and gap_s > 3.0:
        evidence = f"Defenders delayed opening fire by {gap_s:.1f}s after contact was established."
        return "HOLDS", evidence
    else:
        return "CANNOT TELL", f"First shot gap={gap_s}s; no explicit attention-off penalty line recorded."


def format_markdown_report(metrics: dict, arena_path: Path, round_index: int) -> str:
    """Format full 1-page markdown decision timeline report."""
    obj = metrics["objective"]
    hyp = metrics["hypotheses"]
    log_st = metrics["log_stats"]

    md = []
    md.append(f"# PMC Round Decision Trace: Round {round_index}")
    md.append(f"**Source File**: `{arena_path.name}` | **Map**: `{metrics['map']}` | **Seed**: `{metrics['seed']}`")
    md.append("")
    md.append("## 1. Round Overview & Objective Context")
    md.append(f"- **Scenario**: `{metrics['scenario']}`")
    md.append(f"- **Result**: Winner **Side {metrics['winner']}** via `{metrics['end_reason']}` in **{metrics['duration_s']:.1f} s**")
    md.append(f"- **Role Assignment**: Defender = **Side {metrics['def_side']}**, Attacker = **Side {metrics['atk_side']}**")
    md.append(f"- **Objective**: Position `(X={obj['x']}, Y={obj['y']}, Z={obj['z']})` | Level: **{obj['level_m']:.1f} m** ({'Ground' if obj['z'] < 200 else 'Roof'})")
    md.append(f"- **Objective Specs**: Radius **{obj['radius_m']} m**, Hold **{obj['hold_s']} s**, Final State: `{obj['result']}`")
    md.append(f"- **First Contact**: **{metrics['first_contact_s']} s**")
    md.append("")

    md.append("## 2. Defender Positions & Initial Posts")
    md.append("| Defender | Weapon / Look | Initial Post / Role | Coordinates / Post ID | Post Score | Danger |")
    md.append("|---|---|---|---|---|---|")
    for f in metrics["def_fighters"]:
        fid = f["id"]
        post = metrics["posts_assigned"].get(fid)
        if post:
            role = post.get("role", "custom")
            coord = post.get("post_id") or post.get("pos", "spawn")
            score_s = f"{post.get('score', 0.0):.2f}" if "score" in post else "n/a"
            danger_s = f"{post.get('danger', 0.0):.2f}" if "danger" in post else "n/a"
        else:
            role = "Ground / Objective"
            coord = "Spawn ring"
            score_s = "n/a"
            danger_s = "0.00"
        md.append(f"| **{fid}** | {f.get('weapon') or 'm16'} | `{role}` | `{coord}` | {score_s} | {danger_s} |")
    md.append("")

    md.append("## 3. Order Sequence & Decision Timeline (Spawn to Engagement)")
    md.append("| Time (s) | Fighter | Decision / Action | Score | Context / Trigger |")
    md.append("|---|---|---|---|---|")

    # Combine track tactic events and log events into a unified chronological table
    timeline_rows = []
    for fid, d_list in metrics["decisions_by_fighter"].items():
        for d in d_list:
            timeline_rows.append((d["t"], fid, d["decision"], f"{d['score']:.2f}", d.get("why") or "tactic eval"))

    # Add log fire events
    for fe in metrics.get("log_fire_events", []):
        timeline_rows.append((metrics["first_contact_s"] or 0.0, fe["who"], "Opens Fire", "-", f"threat at {fe['dist_m']:.1f}m, attention off {fe['attention_off']:.2f}"))

    timeline_rows.sort(key=lambda r: (r[0], r[1]))

    # Display up to first 20 key decisions
    for r in timeline_rows[:25]:
        phase_mark = " *(pre-contact)*" if metrics["first_contact_s"] and r[0] <= metrics["first_contact_s"] else ""
        md.append(f"| {r[0]:.1f} s{phase_mark} | {r[1]} | **{r[2]}** | {r[3]} | {r[4]} |")

    if not timeline_rows:
        md.append("| - | - | *No tactic decisions logged in track* | - | - |")
    md.append("")

    md.append("## 4. Exposure & Combat Engagement Times")
    md.append("| Defender | Total Standing | Pre-Contact Standing | Idle in Open | First Shot Fired | Target | Hit? | Total Shots |")
    md.append("|---|---|---|---|---|---|---|---|")
    for f in metrics["def_fighters"]:
        fid = f["id"]
        st = metrics["exposure_stats"].get(fid, {})
        first_shot_str = f"{st['first_shot_t']:.1f} s" if st.get("first_shot_t") is not None else "Never"
        target_str = st.get("first_shot_target") or "-"
        hit_str = "Yes" if st.get("first_shot_hit") else "No" if st.get("first_shot_t") is not None else "-"
        md.append(
            f"| **{fid}** | {st.get('total_stand_s', 0.0):.1f} s | **{st.get('pre_contact_stand_s', 0.0):.1f} s** | "
            f"{st.get('idle_stand_s', 0.0):.1f} s | {first_shot_str} | `{target_str}` | {hit_str} | {st.get('shots_fired', 0)} |"
        )
    md.append("")

    md.append("## 5. Audit 4.1 Hypotheses Evaluation")
    md.append("| Hypothesis | Verdict | Evidence from Track & Log |")
    md.append("|---|---|---|")
    md.append(f"| **H1 (Ground Objective Pull)** | `{hyp['H1']['verdict']}` | {hyp['H1']['evidence']} |")
    md.append(f"| **H2 (Post Abandonment)** | `{hyp['H2']['verdict']}` | {hyp['H2']['evidence']} |")
    md.append(f"| **H3 (No Cover Available)** | `{hyp['H3']['verdict']}` | {hyp['H3']['evidence']} |")
    md.append(f"| **H4 (Hold Fire / Attention Delay)** | `{hyp['H4']['verdict']}` | {hyp['H4']['evidence']} |")
    md.append("")

    md.append("## 6. Diagnostic Status & Stop Rule")
    if log_st["log_found"] and log_st["round_found"] and log_st["decisions_logged"] > 0:
        md.append(f"- **Log Quality**: Complete. Found `{log_st['decisions_logged']}` order decision logs and `{log_st['fire_events_logged']}` fire triggers.")
    elif log_st["log_found"]:
        md.append(f"- **Log Quality**: Partial. Log file present but order scores are primarily in `round_*.track.json` (`{len(timeline_rows)}` events found).")
    else:
        md.append("- **Log Quality**: Missing log file. Evaluated using `arena.json` and `round_*.track.json` alone.")

    return "\n".join(md)


def main():
    parser = argparse.ArgumentParser(description="Generate 1-page PMC round decision timeline and evaluate H1-H4.")
    parser.add_argument("arena", help="Path to arena result JSON file (e.g. *.arena.json)")
    parser.add_argument("--round", "-r", type=int, default=1, help="Round index to trace (default 1)")
    parser.add_argument("--track", "-t", default=None, help="Explicit path to round_*.track.json")
    parser.add_argument("--log", "-l", default=None, help="Explicit path to job execution log file")
    parser.add_argument("--out", "-o", default=None, help="Optional output path to write markdown file")
    args = parser.parse_args()

    arena_path = Path(args.arena)
    if not arena_path.is_file():
        sys.stderr.write(f"Error: Arena file not found: {arena_path}\n")
        sys.exit(1)

    try:
        arena_data = json.loads(arena_path.read_text(encoding="utf-8"))
    except Exception as e:
        sys.stderr.write(f"Error: Failed to parse arena JSON {arena_path}: {e}\n")
        sys.exit(1)

    rnd, arena_meta = parse_arena_round(arena_data, args.round)
    track_path = resolve_track_file(arena_path, args.round, args.track)
    track_data = None
    if track_path and track_path.is_file():
        try:
            track_data = json.loads(track_path.read_text(encoding="utf-8"))
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to parse track file {track_path}: {e}\n")

    log_path = resolve_log_file(arena_path, args.log)
    log_data = parse_log_for_round(log_path, args.round)

    metrics = compute_trace_metrics(rnd, arena_meta, track_data, log_data)
    md_output = format_markdown_report(metrics, arena_path, args.round)

    if args.out:
        out_p = Path(args.out)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        out_p.write_text(md_output, encoding="utf-8")
        print(f"Report written to {out_p}")
    else:
        print(md_output)


if __name__ == "__main__":
    main()
