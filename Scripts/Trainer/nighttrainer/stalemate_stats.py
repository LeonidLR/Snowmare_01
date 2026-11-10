"""Stalemate / fight-flow stats of arena round tracks (codex.pmc_arena_track/1; fix/pmc-arena-stalemate 2026-11-10).

Per round: shots per minute, the first contact, the longest no-contact spell (gaps between shots after the first one,
plus the quiet tail to the round's end), the attackers' mean distance to the objective point over time (and per 30 s),
how close they got, the commander's order events per fighter per minute, quick re-decisions (the same fighter's next
order within `flip_s`), and the outcome. Works on old tracks too (the before / after comparison of the fix).

  python Scripts/Trainer/nighttrainer/stalemate_stats.py <arena result .json | track .json> [...] [--json out.json]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

UNIT_TO_M = {"m": 1.0, "cm": 0.01, "mm": 0.001}


def round_stats(track: dict, result_round: dict | None = None, flip_s: float = 2.0, bucket_s: float = 30.0) -> dict:
    """The fight-flow stats of one track (result_round: the arena result's round entry, for the outcome)."""
    rr = result_round or {}
    dt = float(track.get("dt") or 0.2) or 0.2
    scale = UNIT_TO_M.get(str(track.get("units", "m")).lower(), 1.0)
    frames = track.get("frames") or []
    duration = float(rr.get("duration_s") or track.get("duration_s") or len(frames) * dt) or dt
    fighters = track.get("fighters") or []
    events = track.get("events") or []
    shots = sorted(float(e.get("t", 0.0)) for e in events if e.get("type") == "shot")
    tactics = [e for e in events if e.get("type") == "tactic"]
    out = {
        "round": track.get("round"), "seed": track.get("round_seed"), "duration_s": round(duration, 1),
        "winner": rr.get("winner", track.get("winner")), "end_reason": rr.get("end_reason"),
        "objective_result": (rr.get("objective") or {}).get("result") if isinstance(rr.get("objective"), dict) else None,
        "shots": len(shots), "shots_per_min": round(len(shots) / (duration / 60.0), 2),
        "contact": bool(shots),
        "first_contact_s": round(shots[0], 1) if shots else None,
    }
    # Quiet spells: from the first shot on (the approach before it is first_contact_s), plus the tail to the end.
    gaps = [b - a for a, b in zip(shots, shots[1:])]
    tail = duration - shots[-1] if shots else duration
    out["longest_quiet_s"] = round(max(gaps + [tail]) if shots else duration, 1)
    out["longest_quiet_inner_s"] = round(max(gaps), 1) if gaps else 0.0
    out["quiet_tail_s"] = round(tail, 1)
    # Orders: per fighter per minute, and quick re-decisions (the same fighter again within flip_s).
    n_f = max(1, len(fighters))
    out["orders"] = len(tactics)
    out["orders_per_fighter_min"] = round(len(tactics) / n_f / (duration / 60.0), 2)
    last, flips, same = {}, 0, 0
    for e in sorted(tactics, key=lambda e: float(e.get("t", 0.0))):
        key = (str(e.get("side")), str(e.get("who")))
        t = float(e.get("t", 0.0))
        if key in last and t - last[key][0] < flip_s:
            flips += 1
            same += last[key][1] == e.get("decision")
        last[key] = (t, e.get("decision"))
    out["quick_redecisions"] = flips
    out["quick_redecisions_same_order"] = same
    # Attackers' distance to the point over time.
    obj = track.get("objective") if isinstance(track.get("objective"), dict) else None
    if obj and frames:
        atk = str(obj.get("attacker"))
        idx = [i for i, f in enumerate(fighters) if str(f.get("side")) == atk]
        px, py = float(obj.get("x", 0.0)), float(obj.get("y", 0.0))
        radius_m = float(obj.get("radius", 600.0)) * scale
        series, closest = [], None
        for row in frames:
            d = [math.hypot(row[i][0] - px, row[i][1] - py) * scale for i in idx if i < len(row) and row[i]]
            if d:
                series.append(sum(d) / len(d))
                closest = min(d) if closest is None else min(closest, min(d))
            else:
                series.append(None)
        valid = [v for v in series if v is not None]
        out["attacker_mean_dist_m"] = round(sum(valid) / len(valid), 1) if valid else None
        out["attacker_closest_m"] = round(closest, 1) if closest is not None else None
        out["attacker_reached_point"] = bool(closest is not None and closest <= radius_m)
        per = max(1, int(round(bucket_s / dt)))
        buckets = []
        for k in range(0, len(series), per):
            chunk = [v for v in series[k:k + per] if v is not None]
            buckets.append(round(sum(chunk) / len(chunk), 1) if chunk else None)
        out["attacker_dist_by_30s_m"] = buckets
    return out


def _load(path: str):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def stats_for_path(path: str, **kw) -> list:
    """Rows for an arena result (each round's track next to it) or a single track file."""
    doc = _load(path)
    if doc.get("schema") == "codex.pmc_arena_track/1":
        return [round_stats(doc, None, **kw)]
    rows = []
    base = os.path.dirname(os.path.abspath(path))
    for rnd in doc.get("rounds") or []:
        track = rnd.get("track")
        if not track:
            continue
        tp = track if os.path.isabs(track) else os.path.join(base, track)
        try:
            rows.append(round_stats(_load(tp), rnd, **kw))
        except (OSError, ValueError) as exc:
            rows.append({"round": rnd.get("index"), "error": str(exc)})
    return rows


def summary(rows: list) -> dict:
    ok = [r for r in rows if "error" not in r]
    if not ok:
        return {}

    def mean(key):
        vals = [r[key] for r in ok if isinstance(r.get(key), (int, float))]
        return round(sum(vals) / len(vals), 2) if vals else None

    outcomes = {}
    for r in ok:
        k = f"{r.get('winner')}/{r.get('end_reason')}"
        outcomes[k] = outcomes.get(k, 0) + 1
    return {"rounds": len(ok), "shots_per_min": mean("shots_per_min"), "first_contact_s": mean("first_contact_s"),
            "longest_quiet_s": mean("longest_quiet_s"), "longest_quiet_max_s": max(r["longest_quiet_s"] for r in ok),
            "attacker_mean_dist_m": mean("attacker_mean_dist_m"), "attacker_closest_m": mean("attacker_closest_m"),
            "orders_per_fighter_min": mean("orders_per_fighter_min"), "quick_redecisions": mean("quick_redecisions"),
            "no_contact_rounds": sum(1 for r in ok if not r.get("contact")), "outcomes": outcomes}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--json", help="write rows + summary here")
    ap.add_argument("--flip-s", type=float, default=2.0)
    args = ap.parse_args(argv)
    rows = []
    for p in args.paths:
        rows += stats_for_path(p, flip_s=args.flip_s)
    cols = ("round", "seed", "duration_s", "winner", "end_reason", "shots_per_min", "first_contact_s", "longest_quiet_s",
            "attacker_mean_dist_m", "attacker_closest_m", "orders_per_fighter_min", "quick_redecisions")
    print("\t".join(cols))
    for r in rows:
        print("\t".join(str(r.get(c)) for c in cols))
    s = summary(rows)
    print("summary:", json.dumps(s))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"rows": rows, "summary": s}, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
