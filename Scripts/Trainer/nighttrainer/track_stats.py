"""Close-range engagement stats from a round track (codex.pmc_arena_track/1), for the close_range fitness penalty.

Per side: shots fired, shots fired at a target closer than close_range_m, and hits taken at that range.
Shot distance = |shooter - target| from the frames at the shot's tick (last known position if dead at that tick).
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
    index = {str(f.get("id")): i for i, f in enumerate(fighters)}
    fside = {str(f.get("id")): str(f.get("side")) for f in fighters}
    fweapon = {str(f.get("id")): str(f.get("weapon", "")) for f in fighters}
    shots = close = taken = 0
    for ev in track.get("events") or []:
        if ev.get("type") != "shot":
            continue
        src, dst = str(ev.get("from")), str(ev.get("to"))
        if src not in index or dst not in index:
            continue
        tick = int(round(float(ev.get("t", 0.0)) / dt))
        a, b = _pos(frames, tick, index[src]), _pos(frames, tick, index[dst])
        if a is None or b is None:
            continue
        is_close = math.hypot(a[0] - b[0], a[1] - b[1]) * scale < close_range_m
        if fside.get(src) == side:
            weapon = str(ev.get("weapon") or fweapon.get(src, "")).lower()
            if shotgun_exempt and "shotgun" in weapon:
                continue  # a shotgun is meant for close range: not counted at all
            shots += 1
            close += is_close
        elif fside.get(dst) == side and is_close and ev.get("hit"):
            taken += 1
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
