#!/usr/bin/env python3
"""pmc_round_trace.py - Decision timeline and hypothesis evaluator for PMC arena rounds.

Task T-01 (smart enemies roadmap / audit 4.1):
Reads arena result JSON (codex.pmc_arena/1) + track JSON (codex.pmc_arena_track/1)
+ [PmcAI] log lines to produce a 1-page markdown decision timeline for designers:
- Scenario, map, seed, duration, winner, end reason.
- Objective coordinates, storey level, height (m), radius, hold time.
- Each defender's spawn, initial post, order sequence with scores from spawn to first contact.
- Seconds standing in the open per defender (arena.json open_idle_s ground truth).
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
from typing import Any, Dict, List, Optional, Tuple, Union

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


def parse_round_identifier(
    round_arg: Union[int, str],
    num_rounds: int,
    zero_based: bool = False,
) -> Tuple[int, int]:
    """Parse round identifier into (round_1based_index, round_0based_index).

    Accepts:
      - 'r0', 'r1', 'r2'... (strictly 0-based clip notation)
      - integers: if zero_based=True or round_arg=='0', treated as 0-based index;
                  otherwise treated as 1-based round index.
    Raises ValueError on invalid or out-of-range round.
    """
    s = str(round_arg).strip().lower()
    if s.startswith("r"):
        try:
            idx_0 = int(s[1:])
        except ValueError:
            raise ValueError(f"Invalid clip round format: '{round_arg}'. Expected 'r0', 'r1', etc.")
        idx_1 = idx_0 + 1
    elif zero_based or s == "0":
        try:
            idx_0 = int(s)
        except ValueError:
            raise ValueError(f"Invalid round index: '{round_arg}'.")
        idx_1 = idx_0 + 1
    else:
        try:
            idx_1 = int(s)
        except ValueError:
            raise ValueError(f"Invalid round number: '{round_arg}'.")
        idx_0 = idx_1 - 1

    if num_rounds > 0 and (idx_0 < 0 or idx_0 >= num_rounds):
        raise ValueError(
            f"Round '{round_arg}' is out of range for arena with {num_rounds} round(s). "
            f"Valid options: 1..{num_rounds} (1-based) or r0..r{num_rounds - 1} (0-based clip notation)."
        )
    return idx_1, idx_0


def parse_arena_round(
    arena_data: dict,
    round_arg: Union[int, str],
    zero_based: bool = False,
) -> Tuple[dict, dict, int, int]:
    """Extract round data and arena-level metadata for requested round.

    Returns (target_round, arena_data, round_1based, round_0based).
    Raises ValueError if round cannot be found.
    """
    rounds = arena_data.get("rounds") or []
    if not rounds:
        raise ValueError("Arena data contains no rounds.")

    idx_1, idx_0 = parse_round_identifier(round_arg, len(rounds), zero_based)

    target_round = None
    if 0 <= idx_0 < len(rounds):
        candidate = rounds[idx_0]
        if candidate.get("index") in (idx_1, idx_0):
            target_round = candidate

    if not target_round:
        for r in rounds:
            if r.get("index") == idx_1:
                target_round = r
                break

    if not target_round and 0 <= idx_0 < len(rounds):
        target_round = rounds[idx_0]

    if not target_round:
        available = [f"r{i} (index {r.get('index', i+1)})" for i, r in enumerate(rounds)]
        raise ValueError(f"Round '{round_arg}' not found in arena. Available: {', '.join(available)}.")

    return target_round, arena_data, idx_1, idx_0


def resolve_track_file(
    arena_path: Path,
    round_1based: int,
    round_0based: int,
    explicit_track: Optional[str] = None,
    round_obj: Optional[dict] = None,
) -> Optional[Path]:
    """Auto-discover track JSON for a given round if not provided explicitly."""
    if explicit_track:
        p = Path(explicit_track)
        if p.is_file():
            return p

    arena_dir = arena_path.parent
    if round_obj and round_obj.get("track"):
        track_ref = Path(round_obj["track"])
        if track_ref.is_file():
            return track_ref
        if (arena_dir / track_ref).is_file():
            return arena_dir / track_ref

    base_name = arena_path.name
    if base_name.endswith(".arena.json"):
        stem = base_name[:-len(".arena.json")]
    elif base_name.endswith(".json"):
        stem = arena_path.stem
    else:
        stem = base_name

    candidates = [
        arena_dir / f"round_{round_1based}.track.json",
        arena_dir / f"round_{round_0based}.track.json",
        arena_dir / f"{stem}.arena" / f"round_{round_1based}.track.json",
        arena_dir / f"{stem}.arena" / f"round_{round_0based}.track.json",
        arena_dir / stem / f"round_{round_1based}.track.json",
        arena_dir / stem / f"round_{round_0based}.track.json",
        arena_dir / f"{arena_path.stem}" / f"round_{round_1based}.track.json",
        arena_dir / f"{arena_path.stem}" / f"round_{round_0based}.track.json",
        arena_dir.parent / f"{stem}.arena" / f"round_{round_1based}.track.json",
        arena_dir.parent / f"{stem}.arena" / f"round_{round_0based}.track.json",
        arena_dir.parent / stem / f"round_{round_1based}.track.json",
        arena_dir.parent / stem / f"round_{round_0based}.track.json",
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


def partition_log_for_rounds(lines: List[str]) -> Dict[int, List[str]]:
    """Partition log lines into per-round buckets (1-based round index).

    Spawn and post placement lines that occur prior to '[PmcArena] round N:'
    belong to round N's setup.
    """
    round_headers = []
    round_re = re.compile(r"\[PmcArena\]\s+round\s+(\d+):")
    for idx, line in enumerate(lines):
        m = round_re.search(line)
        if m:
            round_headers.append((idx, int(m.group(1))))

    if not round_headers:
        # Single-round log or no round headers; everything belongs to round 1
        return {1: [l for l in lines if "[PmcAI]" in l or "[PmcArena" in l]}

    buckets: Dict[int, List[str]] = {}
    for _, r_num in round_headers:
        buckets[r_num] = []

    for i, (hdr_idx, r_num) in enumerate(round_headers):
        # Setup lines preceding this header (spawns and post placements)
        start_idx = 0 if i == 0 else round_headers[i - 1][0] + 1
        for j in range(start_idx, hdr_idx):
            line = lines[j]
            if "[PmcAI]" in line and ("spawned" in line or "placed on a" in line):
                buckets[r_num].append(line)

        # Execution lines from hdr_idx onwards (up to next header)
        end_idx = round_headers[i + 1][0] if i + 1 < len(round_headers) else len(lines)
        for j in range(hdr_idx, end_idx):
            line = lines[j]
            if "[PmcAI]" in line or "[PmcArena" in line:
                # If this line is a spawn/post right before the next header, it belongs to the next round
                if i + 1 < len(round_headers) and ("spawned" in line or "placed on a" in line):
                    continue
                buckets[r_num].append(line)

    return buckets


def parse_log_for_round(
    log_path: Optional[Path],
    round_1based: int,
    round_0based: int,
    lines_override: Optional[List[str]] = None,
) -> Dict[str, Any]:
    """Parse [PmcAI] lines from log file partitioned for round_1based."""
    res: Dict[str, Any] = {
        "found": False,
        "round_found": False,
        "spawned": {},        # name -> {team, look, weapon, pos}
        "posts": {},          # name -> {role, pos, yaw, score, danger, post_id}
        "decisions": [],      # [{t_log, team, who, order, score, threat_state, threat_dist_cm, hp, cover}]
        "failures": [],       # [{t_log, team, who, order, reason, delay_s}]
        "fire_events": [],    # [{t_log, team, who, action, dist_m, attention_off}]
        "completions": [],    # [{t_log, team, who, order, result_state}]
        "raw_lines": [],
    }
    if lines_override is not None:
        lines = lines_override
        res["found"] = True
    elif log_path and log_path.is_file():
        res["found"] = True
        try:
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception:
            return res
    else:
        return res

    buckets = partition_log_for_rounds(lines)
    round_lines = buckets.get(round_1based)
    if not round_lines and round_0based in buckets:
        round_lines = buckets[round_0based]

    if round_lines:
        res["round_found"] = True
        res["raw_lines"] = round_lines
    else:
        return res

    # Regexes for PmcAI log entries
    spawn_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+spawned\s+Operator\s+([A-Za-z0-9_-]+)\s+\(([^)]*)\)\s+at\s+V\(([^)]*)\)"
    )
    post_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+(?:Operator\s+)?([A-Za-z0-9_-]+)\s+placed\s+on\s+a\s+(?:([A-Za-z0-9_-]+)\s+)?post\s+V?\(([^)]*)\)\s+\(([^)]*)\)"
    )
    order_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)\s+->\s+([A-Za-z0-9_-]+)\s+\(score\s+([0-9.]+);\s*([^)]*)\)"
    )
    fail_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+Operator\s+([A-Za-z0-9_-]+)'s\s+([A-Za-z0-9_-]+)\s+failed\s+\(([^)]*)\)\s*-\s*not\s+again\s+for\s+([0-9.]+)\s*s"
    )
    fire_re = re.compile(
        r"\[PmcAI\]\s+team\s+(\d+):\s+(?:Operator\s+)?([A-Za-z0-9_-]+)\s+(opens\s+fire|holds\s+his\s+fire)\s+\(threat\s+at\s+([0-9.]+)\s*cm,\s*attention\s+off\s+([0-9.]+)\)"
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
            role = role or "post"
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
            team, name, action, dist_cm, att_off = m_fi.groups()
            res["fire_events"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "action": "holds his fire" if "holds" in action else "opens fire",
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
    obj_z = obj.get("z", 0.0) or 0.0
    obj_r_m = obj.get("radius_m", 0.0)
    obj_hold_s = obj.get("hold_s", 0.0)
    obj_result = obj.get("result", "none")
    obj_height_m = obj_z / 100.0
    obj_level = obj.get("level")
    if obj_level is None:
        obj_level = 0 if obj_height_m < 2.0 else round(obj_height_m / 3.0)

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
        def_side = "B" if "B" in sides else "A"
    if not atk_side:
        atk_side = "A" if def_side == "B" else "B"

    # Fighters from track or arena/log
    fighters = track.get("fighters") or [] if track else []
    frames = track.get("frames") or [] if track else []
    events = track.get("events") or [] if track else []
    states = track.get("states") or [] if track else []
    dt = float(track.get("dt", 0.2)) if track else 0.2

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

    # If track has no fighters, populate from log spawned
    if not def_fighters and log_data.get("spawned"):
        for name, s_info in log_data["spawned"].items():
            def_fighters.append({
                "id": f"Operator {name}",
                "name": name,
                "side": def_side,
                "team": s_info.get("team"),
            })

    # Close range stats from track_stats
    close_stats_def = side_stats(track, def_side) if track else None
    close_stats_atk = side_stats(track, atk_side) if track else None

    # First contact determination
    shots = [e for e in events if e.get("type") == "shot"]
    t_first_shot = min([float(e.get("t", 0.0)) for e in shots]) if shots else None

    t_first_contact = t_first_shot
    if t_first_contact is None:
        t_first_contact = duration_s

    # Tactic decisions per fighter - strictly filter by side & team
    decisions_by_fighter: Dict[str, List[dict]] = {}
    for f in def_fighters:
        decisions_by_fighter[f["id"]] = []

    track_tactic_events = [e for e in events if e.get("type") == "tactic"]
    for ev in track_tactic_events:
        ev_side = ev.get("side")
        if ev_side is not None and str(ev_side) != str(def_side):
            continue

        ev_who = str(ev.get("who", ""))
        for f in def_fighters:
            fid = f["id"]
            fname = f.get("name") or (fid[len("Operator "):] if fid.startswith("Operator ") else fid)
            if "team" in ev and "team" in f and ev["team"] != f["team"]:
                continue
            if ev_who in (fid, fname, f"Operator {fname}"):
                decisions_by_fighter[fid].append({
                    "t": float(ev.get("t", 0.0)),
                    "decision": ev.get("decision"),
                    "score": float(ev.get("score") or 0.0),
                    "why": ev.get("why"),
                })
                break

    # Seconds standing in the open per defender
    arena_defence = rnd.get("defence") or {}
    arena_idle_arr = arena_defence.get("open_idle_s") or []
    idle_by_fighter_id: Dict[str, dict] = {}
    for entry in arena_idle_arr:
        if isinstance(entry, dict):
            ef_id = entry.get("id")
            if ef_id:
                idle_by_fighter_id[ef_id] = entry
                if ef_id.startswith("Operator "):
                    idle_by_fighter_id[ef_id[len("Operator "):]] = entry
                else:
                    idle_by_fighter_id[f"Operator {ef_id}"] = entry

    exposure_stats: Dict[str, dict] = {}
    for f in def_fighters:
        fid = f["id"]
        fname = f.get("name") or (fid[len("Operator "):] if fid.startswith("Operator ") else fid)
        fi = f.get("idx")
        total_stand_s = 0.0
        pre_contact_stand_s = 0.0
        idle_stand_s = 0.0
        first_shot_t = None
        first_shot_hit = None
        first_shot_target = None
        shots_fired = 0

        # Ground truth from arena.json if available
        matched_idle = idle_by_fighter_id.get(fid) or idle_by_fighter_id.get(fname)
        if matched_idle is not None:
            total_stand_s = float(matched_idle.get("total_s", 0.0))
            pre_contact_stand_s = float(matched_idle.get("before_contact_s", 0.0))
            idle_stand_s = pre_contact_stand_s
        elif fi is not None and states:
            for tick, state_row in enumerate(states):
                t_cur = tick * dt
                if fi < len(state_row) and state_row[fi]:
                    s_entry = state_row[fi]
                    stance = s_entry[1] if len(s_entry) > 1 else None
                    cur_state = s_entry[3] if len(s_entry) > 3 else None
                    if stance == "stand" and cur_state not in ("Cover", "InCover", "TakeCover"):
                        total_stand_s += dt
                        if t_first_contact is not None and t_cur <= t_first_contact:
                            pre_contact_stand_s += dt
                        if cur_state in ("Hold", "Idle", "Ambush"):
                            idle_stand_s += dt

        # Shots fired by this fighter
        for s in shots:
            s_from = str(s.get("from", ""))
            if (s_from in (fid, fname, f"Operator {fname}")) and (
                s.get("side") is None or str(s.get("side")) == str(def_side)
            ):
                shots_fired += 1
                if first_shot_t is None:
                    first_shot_t = float(s.get("t", 0.0))
                    first_shot_hit = bool(s.get("hit"))
                    first_shot_target = str(s.get("to"))

        exposure_stats[fid] = {
            "total_stand_s": round(total_stand_s, 2),
            "pre_contact_stand_s": round(pre_contact_stand_s, 2),
            "idle_stand_s": round(idle_stand_s, 2),
            "first_shot_t": round(first_shot_t, 2) if first_shot_t is not None else None,
            "first_shot_hit": first_shot_hit,
            "first_shot_target": first_shot_target,
            "shots_fired": shots_fired,
        }

    # Initial posts assigned to defenders
    posts_assigned: Dict[str, dict] = {}
    for f in def_fighters:
        fid = f["id"]
        fname = f.get("name") or (fid[len("Operator "):] if fid.startswith("Operator ") else fid)
        if fname in log_data.get("posts", {}):
            posts_assigned[fid] = log_data["posts"][fname]
        elif fid in log_data.get("posts", {}):
            posts_assigned[fid] = log_data["posts"][fid]

    # Evaluate Hypotheses H1 - H4
    h1_verdict, h1_evidence = evaluate_h1(scenario, obj, rnd, log_data, decisions_by_fighter)
    h2_verdict, h2_evidence = evaluate_h2(posts_assigned, decisions_by_fighter, log_data, def_fighters)
    h3_verdict, h3_evidence = evaluate_h3(log_data, exposure_stats, rnd, def_fighters)
    h4_verdict, h4_evidence = evaluate_h4(log_data, exposure_stats, t_first_contact, shots, def_fighters)

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
            "height_m": round(obj_height_m, 2),
            "level": int(obj_level),
        },
        "def_fighters": def_fighters,
        "posts_assigned": posts_assigned,
        "decisions_by_fighter": decisions_by_fighter,
        "exposure_stats": exposure_stats,
        "first_contact_s": round(t_first_contact, 2) if t_first_contact is not None else None,
        "close_stats_def": close_stats_def,
        "close_stats_atk": close_stats_atk,
        "log_fire_events": log_data.get("fire_events", []),
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

    all_decisions = []
    for d_list in decisions.values():
        all_decisions.extend(d_list)

    objective_orders = [d for d in all_decisions if d.get("decision") == "Objective"]
    obj_order_ratio = len(objective_orders) / len(all_decisions) if all_decisions else 0.0

    if not is_ground:
        return (
            "DOES NOT HOLD",
            f"Scenario is '{scenario}' with objective at Z={obj_z:.0f} cm (roof/upper elevation). H1 assumes a ground-objective scenario.",
        )

    if objective_orders or obj_order_ratio > 0.2:
        evidence = (
            f"Ground objective at Z={obj_z:.0f} cm. Defenders received {len(objective_orders)} Objective orders "
            f"({obj_order_ratio * 100:.1f}% of decisions), pulling them onto the objective point."
        )
        return "HOLDS", evidence
    else:
        return "CANNOT TELL", f"Ground objective at Z={obj_z:.0f} cm, but defenders did not exhibit excessive Objective orders."


def evaluate_h2(
    posts: dict,
    decisions: dict,
    log_data: dict,
    def_fighters: list,
) -> Tuple[str, str]:
    """H2: Defenders had tactical posts along route and left them on contact at long range."""
    fighters_with_posts = [fid for fid, p in posts.items() if p.get("role") and p.get("role") != "custom"]
    if not fighters_with_posts:
        return "DOES NOT HOLD", "Defenders had no tactical posts along the route; spawned directly on/near the point."

    def_names = {f["id"] for f in def_fighters} | {f.get("name") for f in def_fighters if f.get("name")}
    def_teams = {f.get("team") for f in def_fighters if f.get("team") is not None}

    abandoned = []
    for fid, dec_list in decisions.items():
        if fid in fighters_with_posts:
            post_role = posts[fid].get("role", "post")
            for d in dec_list:
                if d["t"] > 0 and d["decision"] in ("FallBack", "Advance", "Objective", "TakeCover"):
                    abandoned.append((fid, d["decision"], d["t"], d["score"]))
                    break

    long_range_threats = []
    for d in log_data.get("decisions", []):
        if def_teams and d.get("team") not in def_teams:
            continue
        if def_names and d.get("who") not in def_names and f"Operator {d.get('who')}" not in def_names:
            continue
        if d.get("threat_dist_m", 0) > 40.0:
            long_range_threats.append(f"{d['who']} -> {d['order']} (threat seen at {d['threat_dist_m']:.1f}m)")

    if abandoned:
        details = []
        for a in abandoned:
            p_desc = posts[a[0]].get("post_id") or posts[a[0]].get("role") or "post"
            details.append(f"{a[0]} left {p_desc} for {a[1]} at t={a[2]}s")
        threat_note = f"; long-range threat triggers: {'; '.join(long_range_threats[:2])}" if long_range_threats else ""
        return "HOLDS", f"Defenders started on posts ({', '.join(fighters_with_posts)}) and abandoned them: {', '.join(details)}{threat_note}."
    else:
        return "CANNOT TELL", f"Defenders were assigned posts ({', '.join(fighters_with_posts)}), but clear abandonment order transition could not be verified."


def evaluate_h3(
    log_data: dict,
    exposure_stats: dict,
    rnd: dict,
    def_fighters: list,
) -> Tuple[str, str]:
    """H3: No cover slot was found near defenders (bCoverAvailable false / cover 0.00 / crates not cover)."""
    def_names = {f["id"] for f in def_fighters} | {f.get("name") for f in def_fighters if f.get("name")}
    def_teams = {f.get("team") for f in def_fighters if f.get("team") is not None}

    decisions = [
        d for d in log_data.get("decisions", [])
        if (not def_teams or d.get("team") in def_teams) and
           (not def_names or d.get("who") in def_names or f"Operator {d.get('who')}" in def_names)
    ]
    zero_cover_count = sum(1 for d in decisions if d.get("cover") == 0.0)
    total_decisions_with_cover = sum(1 for d in decisions if "cover" in d)

    take_cover_failures = [
        f for f in log_data.get("failures", [])
        if f.get("order") == "TakeCover" and
           (not def_teams or f.get("team") in def_teams) and
           (not def_names or f.get("who") in def_names or f"Operator {f.get('who')}" in def_names)
    ]

    total_pre_contact_stand = sum(st["pre_contact_stand_s"] for st in exposure_stats.values())

    if total_decisions_with_cover > 0 and zero_cover_count == total_decisions_with_cover:
        evidence = (
            f"100% of logged defender decisions ({zero_cover_count}/{total_decisions_with_cover}) reported cover=0.00. "
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


def evaluate_h4(
    log_data: dict,
    exposure_stats: dict,
    t_contact: Optional[float],
    shots: list,
    def_fighters: list,
) -> Tuple[str, str]:
    """H4: 'Let them run through' caused by ShouldHoldFire, attention rule, or weapon range band."""
    def_names = {f["id"] for f in def_fighters} | {f.get("name") for f in def_fighters if f.get("name")}
    def_teams = {f.get("team") for f in def_fighters if f.get("team") is not None}

    fire_events = [
        fe for fe in log_data.get("fire_events", [])
        if (not def_teams or fe.get("team") in def_teams) and
           (not def_names or fe.get("who") in def_names or f"Operator {fe.get('who')}" in def_names)
    ]

    hold_events = [fe for fe in fire_events if fe.get("action") == "holds his fire"]
    attention_delays = [fe for fe in fire_events if fe.get("attention_off", 1.0) < 0.9]
    very_close_fires = [fe for fe in fire_events if fe.get("action") == "opens fire" and fe.get("dist_m", 100.0) < 15.0]

    first_def_shots = [st["first_shot_t"] for st in exposure_stats.values() if st["first_shot_t"] is not None]
    min_def_shot = min(first_def_shots) if first_def_shots else None

    gap_s = None
    if min_def_shot is not None and t_contact is not None:
        gap_s = max(0.0, min_def_shot - t_contact)

    if hold_events:
        he = hold_events[0]
        evidence = (
            f"Defenders explicitly held fire via ShouldHoldFire: {he['who']} holds his fire "
            f"(threat at {he['dist_m']:.1f}m, attention off {he['attention_off']:.2f})."
        )
        return "HOLDS", evidence
    elif attention_delays or very_close_fires:
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


def format_markdown_report(metrics: dict, arena_path: Path, round_arg: Union[int, str]) -> str:
    """Format full 1-page markdown decision timeline report."""
    obj = metrics["objective"]
    hyp = metrics["hypotheses"]
    log_st = metrics["log_stats"]

    md = []
    md.append(f"# PMC Round Decision Trace: Round {round_arg}")
    md.append(f"**Source File**: `{arena_path.name}` | **Map**: `{metrics['map']}` | **Seed**: `{metrics['seed']}`")
    md.append("")
    md.append("## 1. Round Overview & Objective Context")
    md.append(f"- **Scenario**: `{metrics['scenario']}`")
    md.append(f"- **Result**: Winner **Side {metrics['winner']}** via `{metrics['end_reason']}` in **{metrics['duration_s']:.1f} s**")
    md.append(f"- **Role Assignment**: Defender = **Side {metrics['def_side']}**, Attacker = **Side {metrics['atk_side']}**")
    md.append(
        f"- **Objective**: Position `(X={obj['x']}, Y={obj['y']}, Z={obj['z']})` | "
        f"Storey Level: **{obj['level']}** | Height: **{obj['height_m']:.1f} m** ({'Ground' if obj['height_m'] < 2.0 else 'Roof'})"
    )
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

    timeline_rows = []
    for fid, d_list in metrics["decisions_by_fighter"].items():
        for d in d_list:
            timeline_rows.append((d["t"], fid, d["decision"], f"{d['score']:.2f}", d.get("why") or "tactic eval"))

    for fe in metrics.get("log_fire_events", []):
        act_label = "Holds Fire" if fe.get("action") == "holds his fire" else "Opens Fire"
        timeline_rows.append((
            metrics["first_contact_s"] or 0.0,
            fe["who"],
            act_label,
            "-",
            f"threat at {fe['dist_m']:.1f}m, attention off {fe['attention_off']:.2f}",
        ))

    timeline_rows.sort(key=lambda r: (r[0], r[1]))

    for r in timeline_rows[:25]:
        phase_mark = " *(pre-contact)*" if metrics["first_contact_s"] and r[0] <= metrics["first_contact_s"] else ""
        md.append(f"| {r[0]:.1f} s{phase_mark} | {r[1]} | **{r[2]}** | {r[3]} | {r[4]} |")

    if not timeline_rows:
        md.append("| 0.0 s | Defenders | Spawn / Hold | - | Initial scenario deployment |")
    md.append("")

    md.append("## 4. Exposure & Engagement Metrics")
    md.append("| Defender | Total Standing in Open (s) | Pre-Contact Standing (s) | First Shot Time (s) | Hits / Targets |")
    md.append("|---|---|---|---|---|")
    for f in metrics["def_fighters"]:
        fid = f["id"]
        st = metrics["exposure_stats"].get(fid, {})
        first_shot_s = f"{st['first_shot_t']:.1f} s" if st.get("first_shot_t") is not None else "None"
        hits_str = f"{st.get('shots_fired', 0)} shots"
        if st.get("first_shot_target"):
            hits_str += f" (first -> {st['first_shot_target']})"
        md.append(
            f"| **{fid}** | {st.get('total_stand_s', 0.0):.1f} s | {st.get('pre_contact_stand_s', 0.0):.1f} s | "
            f"{first_shot_s} | {hits_str} |"
        )
    md.append("")

    md.append("## 5. Audit 4.1 Hypotheses Verdict (H1 - H4)")
    md.append("| Hypothesis | Verdict | Evidence / Log Citation |")
    md.append("|---|---|---|")
    md.append(f"| **H1 (Ground Objective Pull)** | `{hyp['H1']['verdict']}` | {hyp['H1']['evidence']} |")
    md.append(f"| **H2 (Abandoned Tactical Posts)** | `{hyp['H2']['verdict']}` | {hyp['H2']['evidence']} |")
    md.append(f"| **H3 (No Cover Available)** | `{hyp['H3']['verdict']}` | {hyp['H3']['evidence']} |")
    md.append(f"| **H4 (Hold Fire / Attention Delay)** | `{hyp['H4']['verdict']}` | {hyp['H4']['evidence']} |")
    md.append("")

    md.append("## 6. Diagnostic Status & Stop Rule")
    if log_st["log_found"] and log_st["round_found"] and log_st["decisions_logged"] > 0:
        md.append(
            f"- **Log Quality**: Complete. Found `{log_st['decisions_logged']}` order decision logs and `{log_st['fire_events_logged']}` fire triggers."
        )
    elif log_st["log_found"]:
        md.append(
            f"- **Log Quality**: Partial. Log file present but order scores are primarily in `round_*.track.json` (`{len(timeline_rows)}` events found)."
        )
    else:
        md.append("- **Log Quality**: Missing log file. Evaluated using `arena.json` and `round_*.track.json` alone.")

    return "\n".join(md)


def main():
    parser = argparse.ArgumentParser(description="Generate 1-page PMC round decision timeline and evaluate H1-H4.")
    parser.add_argument("arena", help="Path to arena result JSON file (e.g. *.arena.json)")
    parser.add_argument(
        "--round",
        "-r",
        default="1",
        help="Round to trace: 1-based round index (e.g. 1, 2) or 0-based clip notation (e.g. r0, r1, r2, or 0, 1 with --zero-based).",
    )
    parser.add_argument(
        "--zero-based",
        "-0",
        action="store_true",
        help="Interpret integer --round as 0-based index matching clip suffix _rK.",
    )
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

    try:
        rnd, arena_meta, round_1based, round_0based = parse_arena_round(arena_data, args.round, args.zero_based)
    except ValueError as e:
        sys.stderr.write(f"Error: {e}\n")
        sys.exit(1)

    track_path = resolve_track_file(arena_path, round_1based, round_0based, args.track, rnd)
    track_data = None
    if track_path and track_path.is_file():
        try:
            track_data = json.loads(track_path.read_text(encoding="utf-8"))
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to parse track file {track_path}: {e}\n")

    log_path = resolve_log_file(arena_path, args.log)
    log_data = parse_log_for_round(log_path, round_1based, round_0based)

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
