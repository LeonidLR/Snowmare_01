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
    elif zero_based:
        try:
            idx_0 = int(s)
        except ValueError:
            raise ValueError(f"Invalid round index: '{round_arg}'.")
        idx_1 = idx_0 + 1
    elif s == "0":
        raise ValueError(
            "Round 0 is ambiguous: rounds are 1-based by default. "
            "Use '--round 1' for the first round, or pass '--zero-based' / '-0' (or use 'r0') for 0-based clip index."
        )
    else:
        try:
            idx_1 = int(s)
        except ValueError:
            raise ValueError(f"Invalid round number: '{round_arg}'.")
        if idx_1 < 1:
            raise ValueError(f"Round number must be >= 1 (got {idx_1}). Use --zero-based or 'r0' for 0-based clip indexing.")
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

    # Arena exports are normally 1-based; legacy files with index 0 are 0-based.
    rounds_by_index = {r["index"]: r for r in rounds if r.get("index") is not None}
    target_round = rounds_by_index.get(idx_0 if 0 in rounds_by_index else idx_1)
    if target_round is None and rounds[idx_0].get("index") is None:
        target_round = rounds[idx_0]

    if target_round is None:
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

    arena_dir = arena_path if arena_path.is_dir() else arena_path.parent
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

    arena_dir = arena_path if arena_path.is_dir() else arena_path.parent
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
        "spawned": {},        # (team, short name) -> {team, look, weapon, pos}
        "posts": {},          # (team, short name) -> {role, pos, yaw, score, danger, post_id}
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

    # Time regex for UE log lines
    time_re = re.compile(r"\[\d{4}\.\d{2}\.\d{2}-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})\]")

    def extract_time_s(line_text: str) -> Optional[float]:
        tm = time_re.search(line_text)
        if tm:
            h, m, s, ms = tm.groups()
            return int(h) * 3600 + int(m) * 60 + int(s) + int(ms) / 1000.0
        return None

    round_hdr_re = re.compile(r"\[PmcArena\]\s+round\s+\d+:")
    t_round_start: Optional[float] = None
    for line in round_lines:
        if round_hdr_re.search(line):
            t_round_start = extract_time_s(line)
            break

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
            look_parts = [p.strip() for p in look.split(",")]
            weapon = next((p for p in look_parts if not p.startswith("look ")), "n/a")
            spawn_entry = {"team": int(team), "look": look, "pos": pos_str, "weapon": weapon}
            res["spawned"][(int(team), name)] = spawn_entry
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
                elif part.startswith("range"):
                    try:
                        r_toks = part.split()
                        post_info["range_cm"] = float(r_toks[1])
                    except Exception:
                        pass
                else:
                    post_info["post_id"] = part
            res["posts"][(int(team), name)] = post_info
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
            t_line = extract_time_s(line)
            rel_t: Optional[float] = None
            if t_line is not None and t_round_start is not None and t_line >= t_round_start:
                rel_t = round(t_line - t_round_start, 1)
            res["fire_events"].append({
                "team": int(team),
                "who": f"Operator {name}",
                "action": "holds his fire" if "holds" in action else "opens fire",
                "dist_m": float(dist_cm) / 100.0,
                "attention_off": float(att_off),
                "t": rel_t,
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


def synthesize_round_from_track(track_data: dict, round_1based: int) -> Tuple[dict, dict]:
    """Synthesize rnd and arena_meta dicts directly from a track.json structure."""
    scenario = track_data.get("scenario", "unknown")
    map_name = track_data.get("map", "unknown")
    seed = track_data.get("seed")
    winner = track_data.get("winner", "None")
    duration_s = float(track_data.get("duration_s", 0.0) or 0.0)
    obj = track_data.get("objective") or {}
    attacker = obj.get("attacker")
    def_side = "A" if attacker == "B" else "B" if attacker == "A" else None

    rnd = {
        "round": round_1based,
        "scenario": scenario,
        "map": map_name,
        "seed": seed,
        "winner": winner,
        "end_reason": track_data.get("end_reason", "elimination" if winner != "None" else "timeout"),
        "duration_s": duration_s,
        "objective": obj,
        "attacker": attacker,
        "sides": {
            "A": {"role": "defender" if def_side == "A" else "attacker"},
            "B": {"role": "defender" if def_side == "B" else "attacker"},
        } if def_side else {},
    }
    arena_meta = {
        "scenario": scenario,
        "map": map_name,
        "seed": seed,
        "num_rounds": 1,
    }
    return rnd, arena_meta


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
    track_obj = (track.get("objective") or {}) if track else {}
    obj_r_m = obj.get("radius_m")
    if obj_r_m is None:
        obj_r_m = track_obj.get("radius_m")
    if obj_r_m is None and track_obj.get("radius") is not None:
        scale = UNIT_TO_M.get(str(track.get("units", "m")).lower(), 1.0)
        obj_r_m = float(track_obj["radius"]) * scale
    if obj_r_m is None and obj_enabled:
        obj_r_m = (arena.get("config") or {}).get("objective_radius_m")
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
    if not def_side or not atk_side:
        attacker_val = rnd.get("attacker")
        if not attacker_val and track and isinstance(track.get("objective"), dict):
            attacker_val = track["objective"].get("attacker")
        if attacker_val:
            atk_side = str(attacker_val)
            def_side = "A" if atk_side == "B" else "B"
    if not def_side:
        def_side = "B" if "B" in sides else "A"
    if not atk_side:
        atk_side = "A" if def_side == "B" else "B"

    # Fighters from track or arena/log
    fighters = track.get("fighters") or [] if track else []
    frames = track.get("frames") or [] if track else []
    events = sorted(track.get("events") or [], key=lambda event: float(event.get("t", 0.0))) if track else []
    states = track.get("states") or [] if track else []
    dt = float(track.get("dt", 0.2)) if track else 0.2

    def_fighters: List[dict] = []
    # Side A = team 1, Side B = team 2 in arena logs.
    teams = {"A": 1, "B": 2}
    roster = [dict(f, idx=i) for i, f in enumerate(fighters)]
    track_sides = {f["side"] for f in roster}
    roster.extend(
        {"id": f"Operator {name}", "name": name, "side": side, "team": team}
        for side, side_team in teams.items() if side not in track_sides
        for team, name in log_data.get("spawned", {})
        if team == side_team
    )

    fighters_by_key = {}
    keys_by_name = {}
    for f in roster:
        key = (f["side"], f["id"])
        f["team"] = f.get("team") if f.get("team") is not None else teams.get(f["side"])
        fighters_by_key[key] = f
        for name in {f["id"].removeprefix("Operator "), f.get("name")} - {None, ""}:
            keys_by_name.setdefault(name.removeprefix("Operator "), set()).add(key)
        if f["side"] == def_side:
            def_fighters.append(f)

    def fighter_key(name, side=None, team=None):
        if name is None:
            return None
        exact_key = (side, str(name))
        if exact_key in fighters_by_key and (team is None or fighters_by_key[exact_key]["team"] == team):
            return exact_key
        candidates = [
            key for key in keys_by_name.get(str(name).removeprefix("Operator "), ())
            if (side is None or key[0] == side)
            and (team is None or fighters_by_key[key]["team"] == team)
        ]
        return candidates[0] if len(candidates) == 1 else None

    # Close range stats from track_stats
    close_stats_def = side_stats(track, def_side) if track else None
    close_stats_atk = side_stats(track, atk_side) if track else None

    # First contact includes shots with an unknown shooter.
    t_first_contact = next((float(e.get("t", 0.0)) for e in events if e.get("type") == "shot"), duration_s)

    # Tactic decisions per fighter - strictly filter by side & team
    decisions_by_fighter = {(f["side"], f["id"]): [] for f in def_fighters}
    shots_by_fighter = {}
    for ev in events:
        if ev.get("type") == "tactic":
            key = fighter_key(ev.get("who"), ev.get("side"), ev.get("team"))
            if key in decisions_by_fighter:
                decisions_by_fighter[key].append({
                    "t": float(ev.get("t", 0.0)),
                    "decision": ev.get("decision"),
                    "score": float(ev.get("score") or 0.0),
                    "why": ev.get("why"),
                })
        elif ev.get("type") == "shot":
            key = fighter_key(ev.get("from"), ev.get("side"))
            if key is not None:
                shots_by_fighter.setdefault(key, []).append(ev)

    # Seconds standing in the open per defender (C++ open_idle_s with fighter_index and side)
    arena_defence = rnd.get("defence") or {}
    arena_idle_arr = arena_defence.get("open_idle_s") or []
    idle_by_idx: Dict[int, dict] = {}
    idle_by_side_id: Dict[Tuple[str, str], dict] = {}
    for entry in arena_idle_arr:
        if isinstance(entry, dict):
            fi_idx = entry.get("fighter_index")
            if fi_idx is not None:
                idle_by_idx[int(fi_idx)] = entry
            key = fighter_key(entry.get("id"), entry.get("side"))
            if key is not None:
                idle_by_side_id[key] = entry

    exposure_stats: Dict[Tuple[str, str], dict] = {}
    initial_positions = {}
    for f in def_fighters:
        key = (f["side"], f["id"])
        fi = f.get("idx")
        total_stand_s = 0.0
        pre_contact_stand_s = 0.0
        idle_stand_s = 0.0
        first_shot_t = None
        first_shot_hit = None
        first_shot_target = None
        shots_fired = 0
        open_idle_source = "none"
        pre_contact_cover_samples = 0
        if fi is not None and frames and frames[0] and fi < len(frames[0]) and frames[0][fi]:
            initial_positions[key] = frames[0][fi]

        # Ground truth from arena.json if available
        matched_idle = idle_by_idx.get(fi)
        if matched_idle is not None and matched_idle.get("side") not in (None, key[0]):
            matched_idle = None
        if matched_idle is None:
            matched_idle = idle_by_side_id.get(key)

        if matched_idle is not None:
            total_stand_s = float(matched_idle.get("total_s", 0.0))
            pre_contact_stand_s = float(matched_idle.get("before_contact_s", 0.0))
            idle_stand_s = pre_contact_stand_s
            open_idle_source = "arena.defence.open_idle_s"
        if fi is not None and states:
            if matched_idle is None:
                open_idle_source = "track states"
            for tick, state_row in enumerate(states):
                if not state_row or fi >= len(state_row) or not state_row[fi]:
                    continue
                s_entry = state_row[fi]
                stance = s_entry[1] if len(s_entry) > 1 else None
                cur_state = s_entry[3] if len(s_entry) > 3 else None
                in_cover = isinstance(cur_state, str) and (
                    cur_state.endswith("/cover") or cur_state in ("Cover", "InCover")
                )
                before_contact = tick * dt <= t_first_contact
                if in_cover and before_contact:
                    pre_contact_cover_samples += 1
                if matched_idle is None and stance == "stand" and not in_cover and cur_state != "TakeCover":
                    total_stand_s += dt
                    if before_contact:
                        pre_contact_stand_s += dt
                    if cur_state in ("Hold", "Idle", "Ambush"):
                        idle_stand_s += dt

        # Shots fired by this fighter
        fighter_shots = shots_by_fighter.get(key, [])
        shots_fired = len(fighter_shots)
        if fighter_shots:
            first = fighter_shots[0]
            first_shot_t = float(first.get("t", 0.0))
            first_shot_hit = bool(first.get("hit"))
            first_shot_target = str(first.get("to"))
            target_key = fighter_key(first_shot_target, atk_side)
            if target_key is not None:
                first_shot_target = fighter_label(target_key)

        exposure_stats[key] = {
            "total_stand_s": round(total_stand_s, 2),
            "pre_contact_stand_s": round(pre_contact_stand_s, 2),
            "idle_stand_s": round(idle_stand_s, 2),
            "first_shot_t": round(first_shot_t, 2) if first_shot_t is not None else None,
            "first_shot_hit": first_shot_hit,
            "first_shot_target": first_shot_target,
            "shots_fired": shots_fired,
            "source": open_idle_source,
            "pre_contact_cover_samples": pre_contact_cover_samples,
        }

    # Initial posts assigned to defenders
    posts_assigned = {}
    for f in def_fighters:
        post = log_data.get("posts", {}).get(log_fighter_key(f))
        if post:
            posts_assigned[(f["side"], f["id"])] = post

    defender_log_keys = {log_fighter_key(f) for f in def_fighters}
    def_log = {
        field: [
            entry for entry in log_data.get(field, [])
            if (entry["team"], entry["who"].removeprefix("Operator ")) in defender_log_keys
            or (not defender_log_keys and entry["team"] == teams.get(def_side))
        ]
        for field in ("decisions", "failures", "fire_events")
    }
    def_log.update(found=log_data["found"], round_found=log_data["round_found"])
    sides_by_team = {team: side for side, team in teams.items()}
    sides_by_team.update((f["team"], f["side"]) for f in roster if f["team"] is not None)

    # Evaluate Hypotheses H1 - H4
    h1_verdict, h1_evidence = evaluate_h1(scenario, obj, rnd, log_data, decisions_by_fighter, t_first_contact)
    h2_verdict, h2_evidence = evaluate_h2(posts_assigned, decisions_by_fighter, def_log, def_side)
    h3_verdict, h3_evidence = evaluate_h3(def_log, exposure_stats)
    h4_verdict, h4_evidence = evaluate_h4(def_log, exposure_stats, t_first_contact, def_side)

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
        "initial_positions": initial_positions,
        "track_units": track.get("units", "m") if track else "m",
        "decisions_by_fighter": decisions_by_fighter,
        "exposure_stats": exposure_stats,
        "first_contact_s": round(t_first_contact, 2) if t_first_contact is not None else None,
        "close_stats_def": close_stats_def,
        "close_stats_atk": close_stats_atk,
        "log_fire_events": log_data.get("fire_events", []),
        "log_spawned": log_data.get("spawned", {}),
        "sides_by_team": sides_by_team,
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


def evaluate_h1(
    scenario: str,
    obj: dict,
    rnd: dict,
    log_data: dict,
    decisions: dict,
    t_contact: Optional[float] = None,
) -> Tuple[str, str]:
    """H1: Ground-objective scenario -> cases 3 and 4 applied (no defence site, pulled onto point)."""
    obj_z = obj.get("z", 0.0) or 0.0
    is_ground = (obj_z < 200.0) or (scenario in ("tactical", "street", "chokepoint", "random") and obj_z < 250.0)

    all_decisions = []
    for d_list in decisions.values():
        all_decisions.extend(d_list)

    objective_orders = [d for d in all_decisions if d.get("decision") == "Objective"]
    obj_order_ratio = len(objective_orders) / len(all_decisions) if all_decisions else 0.0
    pre_contact_obj_orders = [
        d for d in objective_orders
        if t_contact is not None and d.get("t", 0.0) <= t_contact
    ]

    if not is_ground:
        return (
            "DOES NOT HOLD",
            f"Scenario is '{scenario}' with objective at Z={obj_z:.0f} cm (roof/upper elevation). H1 assumes a ground-objective scenario.",
        )

    if not all_decisions:
        return (
            "CANNOT TELL",
            f"Ground objective at Z={obj_z:.0f} cm, but no defender tactical decisions were recorded.",
        )

    if obj_order_ratio > 0.2 or pre_contact_obj_orders:
        evidence = (
            f"Ground objective at Z={obj_z:.0f} cm. Defenders received {len(objective_orders)} Objective orders "
            f"({obj_order_ratio * 100:.1f}% of decisions"
        )
        if pre_contact_obj_orders:
            evidence += f", {len(pre_contact_obj_orders)} before contact at t={t_contact}s"
        evidence += "), pulling them onto the objective point."
        return "HOLDS", evidence
    else:
        return "DOES NOT HOLD", (
            f"Ground objective at Z={obj_z:.0f} cm, but defenders were not pulled onto the point "
            f"({len(objective_orders)} Objective orders, ratio {obj_order_ratio * 100:.1f}% <= 20%, none before contact)."
        )


def log_fighter_key(fighter: dict) -> Tuple[Optional[int], str]:
    name = fighter.get("name") or fighter["id"]
    return fighter.get("team"), name.removeprefix("Operator ")


def fighter_label(key: Tuple[str, str]) -> str:
    return f"{key[0]}:{key[1]}"


def evaluate_h2(
    posts: dict,
    decisions: dict,
    log_data: dict,
    def_side: str,
) -> Tuple[str, str]:
    """H2: Defenders had tactical posts along route and left them on contact at long range."""
    if not log_data.get("found") or not log_data.get("round_found"):
        return "CANNOT TELL", "No execution log available; defender tactical post assignments are unknown."

    fighters_with_posts = [fid for fid, p in posts.items() if p.get("role") and p.get("role") != "custom"]
    if not fighters_with_posts:
        return "DOES NOT HOLD", "Defenders had no tactical posts along the route; spawned directly on/near the point."

    post_fighters = ", ".join(fighter_label(key) for key in fighters_with_posts)
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
        if d.get("threat_dist_m", 0) > 40.0:
            long_range_threats.append(
                f"{fighter_label((def_side, d['who']))} -> {d['order']} (threat seen at {d['threat_dist_m']:.1f}m)"
            )

    if abandoned:
        details = []
        for a in abandoned:
            p_desc = posts[a[0]].get("post_id") or posts[a[0]].get("role") or "post"
            details.append(f"{fighter_label(a[0])} left {p_desc} for {a[1]} at t={a[2]}s")
        threat_note = f"; long-range threat triggers: {'; '.join(long_range_threats[:2])}" if long_range_threats else ""
        return "HOLDS", f"Defenders started on posts ({post_fighters}) and abandoned them: {', '.join(details)}{threat_note}."
    else:
        return "CANNOT TELL", f"Defenders were assigned posts ({post_fighters}), but clear abandonment order transition could not be verified."


def evaluate_h3(
    log_data: dict,
    exposure_stats: dict,
) -> Tuple[str, str]:
    """H3: No cover slot was found near defenders (bCoverAvailable false / cover 0.00 / crates not cover)."""
    cover_samples = sum(st["pre_contact_cover_samples"] for st in exposure_stats.values())
    if cover_samples:
        return "DOES NOT HOLD", f"Track states show defenders in cover in {cover_samples} fighter samples before first contact."

    decisions = log_data.get("decisions", [])
    zero_cover_count = sum(1 for d in decisions if d.get("cover") == 0.0)
    total_decisions_with_cover = sum(1 for d in decisions if "cover" in d)

    take_cover_failures = [
        f for f in log_data.get("failures", [])
        if f.get("order") == "TakeCover"
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
            f"Defenders spent {total_pre_contact_stand:.1f}s standing in the open before contact "
            "(arena exposure metrics or the defender stance/state samples in the track)."
        )
        return "HOLDS", evidence
    else:
        return "CANNOT TELL", "Insufficient cover diagnostic logging present in this round's logs."


def evaluate_h4(
    log_data: dict,
    exposure_stats: dict,
    t_contact: Optional[float],
    def_side: str,
) -> Tuple[str, str]:
    """H4: 'Let them run through' caused by ShouldHoldFire, attention rule, or weapon range band."""
    hold_events = [fe for fe in log_data.get("fire_events", []) if fe.get("action") == "holds his fire"]

    first_def_shots = [st["first_shot_t"] for st in exposure_stats.values() if st["first_shot_t"] is not None]
    min_def_shot = min(first_def_shots) if first_def_shots else None

    gap_s = None
    if min_def_shot is not None and t_contact is not None:
        gap_s = max(0.0, min_def_shot - t_contact)

    if hold_events:
        he = hold_events[0]
        evidence = (
            f"Defenders explicitly held fire via ShouldHoldFire: {fighter_label((def_side, he['who']))} holds his fire "
            f"(threat at {he['dist_m']:.1f}m, attention off {he['attention_off']:.2f})."
        )
        return "HOLDS", evidence
    elif gap_s is not None and gap_s > 3.0:
        evidence = (
            f"Defenders delayed opening fire by {gap_s:.1f}s after contact was established "
            f"(first shot at t={min_def_shot:.1f}s vs contact at t={t_contact:.1f}s)."
        )
        return "HOLDS", evidence
    elif gap_s is not None:
        return "DOES NOT HOLD", f"Defenders opened fire promptly ({gap_s:.1f}s after contact at t={t_contact:.1f}s); no ShouldHoldFire hold lines recorded."
    else:
        return "CANNOT TELL", "No defender shots or ShouldHoldFire hold lines recorded."


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
    radius = f"Radius **{obj['radius_m']} m**, " if obj["radius_m"] is not None else ""
    md.append(f"- **Objective Specs**: {radius}Hold **{obj['hold_s']} s**, Final State: `{obj['result']}`")
    md.append(f"- **First Contact**: **{metrics['first_contact_s']} s**")
    md.append("")

    position_rows = []
    for f in metrics["def_fighters"]:
        key = (f["side"], f["id"])
        post = metrics["posts_assigned"].get(key, {})
        spawn = metrics["log_spawned"].get(log_fighter_key(f), {})
        position = metrics["initial_positions"].get(key)
        location = f"Spawn: {spawn['pos']} (cm)" if spawn.get("pos") else ""
        if not location and position:
            coords = ", ".join(f"{coord:g}" for coord in position)
            location = f"First frame: ({coords}) {metrics['track_units']}"
        weapon = spawn.get("weapon") or f.get("weapon") or ""
        if weapon == "n/a":
            weapon = f.get("weapon") or ""
        post_label = " · ".join(post[field] for field in ("role", "post_id", "pos") if post.get(field))
        score_s = f"{post['score']:.2f}" if "score" in post else ""
        danger_s = f"{post['danger']:.2f}" if "danger" in post else ""
        if location or weapon or post_label:
            position_rows.append(f"| **{fighter_label(key)}** | {weapon} | {location} | {post_label} | {score_s} | {danger_s} |")
    if position_rows:
        md.append("## 2. Defender Positions & Initial Posts")
        md.append("| Defender | Weapon | Location | Initial Post | Post Score | Danger |")
        md.append("|---|---|---|---|---|---|")
        md.extend(position_rows)
        md.append("")

    md.append("## 3. Order Sequence & Decision Timeline (Spawn to Engagement)")
    md.append("| Time (s) | Fighter | Decision / Action | Score | Context / Trigger |")
    md.append("|---|---|---|---|---|")

    timeline_rows = []
    for fid, d_list in metrics["decisions_by_fighter"].items():
        for d in d_list:
            t_val = d["t"]
            timeline_rows.append((t_val, fighter_label(fid), d["decision"], f"{d['score']:.2f}", d.get("why") or "tactic eval", f"{t_val:.1f} s"))

    for fe in metrics.get("log_fire_events", []):
        act_label = "Holds Fire" if fe.get("action") == "holds his fire" else "Opens Fire"
        t_val = fe.get("t")
        t_sort = t_val if t_val is not None else (metrics["first_contact_s"] or 0.0)
        t_display = f"{t_val:.1f} s" if t_val is not None else "t n/a"
        timeline_rows.append((
            t_sort,
            fighter_label((metrics["sides_by_team"].get(fe["team"], "?"), fe["who"])),
            act_label,
            "-",
            f"threat at {fe['dist_m']:.1f}m, attention off {fe['attention_off']:.2f}",
            t_display,
        ))

    timeline_rows.sort(key=lambda r: (r[0], r[1]))

    for r in timeline_rows[:25]:
        t_disp = r[5]
        phase_mark = " *(pre-contact)*" if (metrics["first_contact_s"] and r[0] <= metrics["first_contact_s"] and t_disp != "t n/a") else ""
        md.append(f"| {t_disp}{phase_mark} | {r[1]} | **{r[2]}** | {r[3]} | {r[4]} |")

    if not timeline_rows:
        md.append("| 0.0 s | Defenders | Spawn / Hold | - | Initial scenario deployment |")
    md.append("")

    md.append("## 4. Exposure & Engagement Metrics")
    md.append("| Defender | Total Standing in Open (s) | Pre-Contact Standing (s) | Source | First Shot Time (s) | Hits / Targets |")
    md.append("|---|---|---|---|---|---|")
    for f in metrics["def_fighters"]:
        key = (f["side"], f["id"])
        st = metrics["exposure_stats"].get(key, {})
        first_shot_s = f"{st['first_shot_t']:.1f} s" if st.get("first_shot_t") is not None else "None"
        hits_str = f"{st.get('shots_fired', 0)} shots"
        if st.get("first_shot_target"):
            hits_str += f" (first -> {st['first_shot_target']})"
        src_str = f"`{st.get('source', 'n/a')}`"
        md.append(
            f"| **{fighter_label(key)}** | {st.get('total_stand_s', 0.0):.1f} s | {st.get('pre_contact_stand_s', 0.0):.1f} s | "
            f"{src_str} | {first_shot_s} | {hits_str} |"
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
    parser.add_argument("arena", help="Path to arena result JSON file, track file, or .arena directory")
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
    if not arena_path.exists():
        sys.stderr.write(f"Error: Path not found: {arena_path}\n")
        sys.exit(1)

    track_data = None
    arena_meta = {}
    rnd = {}
    round_1based = 1
    round_0based = 0

    if arena_path.is_dir():
        arena_json_path = arena_path / "arena.json"
        if arena_json_path.is_file():
            try:
                arena_data = json.loads(arena_json_path.read_text(encoding="utf-8"))
                rnd, arena_meta, round_1based, round_0based = parse_arena_round(arena_data, args.round, args.zero_based)
                track_path = resolve_track_file(arena_json_path, round_1based, round_0based, args.track, rnd)
            except ValueError as e:
                sys.stderr.write(f"Error: {e}\n")
                sys.exit(1)
        else:
            try:
                round_1based, round_0based = parse_round_identifier(args.round, num_rounds=0, zero_based=args.zero_based)
            except ValueError as e:
                sys.stderr.write(f"Error: {e}\n")
                sys.exit(1)

            if args.track:
                track_path = Path(args.track)
            else:
                track_candidates = [
                    arena_path / f"round_{round_1based}.track.json",
                    arena_path / f"round_{round_0based}.track.json",
                ]
                track_path = next((p for p in track_candidates if p.is_file()), None)
                if not track_path:
                    all_tracks = sorted(arena_path.glob("round_*.track.json"))
                    if all_tracks:
                        track_path = all_tracks[0]

            if not track_path or not track_path.is_file():
                sys.stderr.write(f"Error: No round track files found in directory {arena_path}\n")
                sys.exit(1)

            track_data = json.loads(track_path.read_text(encoding="utf-8"))
            rnd, arena_meta = synthesize_round_from_track(track_data, round_1based)

    elif arena_path.is_file():
        if arena_path.name.endswith(".track.json"):
            track_path = arena_path
            track_data = json.loads(track_path.read_text(encoding="utf-8"))
            trk_round = track_data.get("round", 1)
            round_1based = int(trk_round)
            round_0based = round_1based - 1
            rnd, arena_meta = synthesize_round_from_track(track_data, round_1based)
        else:
            try:
                arena_data = json.loads(arena_path.read_text(encoding="utf-8"))
            except Exception as e:
                sys.stderr.write(f"Error: Failed to parse arena JSON {arena_path}: {e}\n")
                sys.exit(1)

            if arena_data.get("schema") == "codex.pmc_arena_track/1" or ("fighters" in arena_data and "rounds" not in arena_data):
                track_path = arena_path
                track_data = arena_data
                trk_round = track_data.get("round", 1)
                round_1based = int(trk_round)
                round_0based = round_1based - 1
                rnd, arena_meta = synthesize_round_from_track(track_data, round_1based)
            else:
                try:
                    rnd, arena_meta, round_1based, round_0based = parse_arena_round(arena_data, args.round, args.zero_based)
                except ValueError as e:
                    sys.stderr.write(f"Error: {e}\n")
                    sys.exit(1)
                track_path = resolve_track_file(arena_path, round_1based, round_0based, args.track, rnd)

    if track_data is None and track_path and track_path.is_file():
        try:
            track_data = json.loads(track_path.read_text(encoding="utf-8"))
        except Exception as e:
            sys.stderr.write(f"Warning: Failed to parse track file {track_path}: {e}\n")

    log_path = resolve_log_file(arena_path if arena_path.is_file() else (arena_path / "arena.json"), args.log)
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
