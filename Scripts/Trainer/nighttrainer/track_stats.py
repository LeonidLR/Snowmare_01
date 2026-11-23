"""Close-range engagement stats from a round track (codex.pmc_arena_track/1), for the close_range fitness penalty.

Per side: shots fired, shots fired at a target closer than close_range_m, and hits taken at that range.
Shooter = (`side`, `from`); target = `to` if it is an unambiguous enemy fighter id, else the nearest living enemy
fighter at the shot's tick (real tracks name the target as an actor, e.g. BP_Operative_C_6). Distance from the frames at the tick.
Units: track["units"] ("m" default, "cm" -> /100). Weapon (for shotgun_exempt) is read from the shot event
("weapon") or the fighter entry ("weapon"); when the track carries none the exemption cannot apply and is skipped.
"""
from __future__ import annotations

import json
import math
import os

UNIT_TO_M = {"m": 1.0, "cm": 0.01, "mm": 0.001}


def _pos(frames: list, idx: int, fi: int):
    """Position of fighter fi at tick idx, else the last known earlier one (None if never seen)."""
    for k in range(min(idx, len(frames) - 1), -1, -1):
        row = frames[k]
        if fi < len(row) and row[fi]:
            return row[fi]
    return None


def side_stats(track: dict, side: str, close_range_m: float = 10.0, shotgun_exempt: bool = True) -> dict | None:
    """{"shots", "close_shots", "close_shot_ratio", "close_hits_taken"} for `side`, or None if not derivable."""
    fighters, frames = track.get("fighters"), track.get("frames")
    if not isinstance(fighters, list) or not isinstance(frames, list) or not frames:
        return None
    dt = float(track.get("dt") or 0.2) or 0.2
    scale = UNIT_TO_M.get(str(track.get("units", "m")).lower(), 1.0)
    # Fighter ids are NOT unique across sides in real tracks (both squads have an "Operator Kessler"), so a fighter is
    # keyed by (side, id). A legacy shot without a side can identify only a unique shooter.
    fighters = [dict(f, id=str(f.get("id")), side=str(f.get("side"))) for f in fighters]
    by_key = {(f["side"], f["id"]): i for i, f in enumerate(fighters)}
    keys_by_id = {}
    for key in by_key:
        keys_by_id.setdefault(key[1], []).append(key)
    shots = close = taken = 0
    for ev in track.get("events") or []:
        if ev.get("type") != "shot":
            continue
        src = str(ev.get("from"))
        sside = str(ev["side"]) if ev.get("side") is not None else None
        source_keys = [(sside, src)] if sside is not None else keys_by_id.get(src, [])
        if len(source_keys) != 1 or source_keys[0] not in by_key:
            continue
        source_key = source_keys[0]
        si = by_key[source_key]
        sside = source_key[0]
        tick = int(round(float(ev.get("t", 0.0)) / dt))
        a = _pos(frames, tick, si)
        if a is None:
            continue
        # Target: `to` when it names an enemy fighter unambiguously; otherwise (real tracks give actor names such as
        # "BP_Operative_C_6" or "?") the NEAREST living enemy fighter at that tick - the shooter's engagement distance.
        target_keys = [key for key in keys_by_id.get(str(ev.get("to")), []) if key[0] != sside]
        if len(target_keys) != 1:
            target_keys = [key for key in by_key if key[0] != sside]
        cand = [by_key[key] for key in target_keys]
        dists = []
        for i in cand:
            pos = frames[min(tick, len(frames) - 1)][i] if i < len(frames[min(tick, len(frames) - 1)]) else None
            if pos:  # alive at this tick only (no last-known position for the nearest-enemy fallback)
                dists.append(math.hypot(a[0] - pos[0], a[1] - pos[1]) * scale)
        if not dists:
            continue
        is_close = min(dists) < close_range_m
        if sside == side:
            weapon = str(ev.get("weapon") or fighters[si].get("weapon", "")).lower()
            if shotgun_exempt and "shotgun" in weapon:
                continue  # a shotgun is meant for close range: not counted at all
            shots += 1
            close += is_close
        elif is_close and ev.get("hit"):
            taken += 1  # an enemy shot that hit from close range (credited to every side but the shooter's)
    return {"shots": shots, "close_shots": close,
            "close_shot_ratio": (close / shots) if shots else 0.0, "close_hits_taken": taken}


def load_side_stats(path: str, side: str, close_range_m: float = 10.0, shotgun_exempt: bool = True) -> dict | None:
    """Stats from a track file; a missing / unreadable / unsupported track gives None (= no penalty)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            track = json.load(f)
        return side_stats(track, side, close_range_m, shotgun_exempt)
    except (OSError, ValueError, TypeError, KeyError):
        return None
