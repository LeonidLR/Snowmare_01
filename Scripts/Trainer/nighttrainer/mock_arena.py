"""Synthetic arena for --mock: a deterministic toy fitness over the genome + noise, written as codex.pmc_arena/1.

Strength of a weights file = -mean(((v - f_p * b_p) / (|b_p| + 1))^2) over its genes, where b_p is the reference
(baseline) value and f_p in [0.6, 1.4] a hidden per-gene factor derived from the gene's pointer (so the optimum is
NOT the baseline and the trainer has something to find). P(A wins a round) = sigmoid(k * (sA - sB)); the bot squad
has a fixed strength. Behaviour metrics are drawn around the outcome. Same args -> same JSON (seeded per round).

CLI (used by the adapter in mock mode, so the subprocess / timeout / retry path is exercised too):
  python mock_arena.py --weights-a A.json --opponent bot|champion [--weights-b B.json] --rounds 4 --seed 7
                       --out result.json --reference baseline.json [--crash-rate 0.0] [--bot-strength -0.03]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import sys

try:
    from .genome import flatten_numeric, load_json
except ImportError:  # run as a script
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from nighttrainer.genome import flatten_numeric, load_json

SQUAD = 4
HP_PER_UNIT = 100.0


def hidden_factor(pointer: str) -> float:
    h = int(hashlib.sha1(pointer.encode("utf-8")).hexdigest()[:8], 16) / 0xFFFFFFFF
    return 0.6 + 0.8 * h


def strength(doc, reference) -> float:
    genes, ref = flatten_numeric(doc), flatten_numeric(reference)
    if not ref:
        return 0.0
    total = 0.0
    for p, b in ref.items():
        v = float(genes.get(p, b))
        total += ((v - hidden_factor(p) * b) / (abs(b) + 1.0)) ** 2
    return -total / len(ref)


def simulate(weights_a, opponent: str, weights_b, reference, rounds: int, seed: int, bot_strength: float = -0.03,
             time_limit_s: float = 300.0, k: float = 60.0, track_prefix: str | None = None) -> dict:
    """track_prefix: when set, every round also writes <track_prefix>.round_<n>.track.json (codex.pmc_arena_track/1)
    and the round's "track" field names that file (relative to the result JSON's folder)."""
    sa = strength(weights_a, reference)
    sb = strength(weights_b, reference) if (opponent != "bot" and weights_b is not None) else bot_strength
    p_win = 1.0 / (1.0 + math.exp(-k * (sa - sb)))
    out_rounds = []
    for i in range(rounds):
        rng = random.Random(f"mock:{seed}:{i}")
        roll = rng.random()
        if abs(roll - p_win) < 0.03:
            winner = "draw"
        else:
            winner = "A" if roll < p_win else "B"
        quality = max(0.0, min(1.0, p_win + rng.gauss(0.0, 0.15)))

        def side(q, won):
            surv = max(0, min(SQUAD, int(round(SQUAD * (0.25 + 0.6 * q) + rng.gauss(0, 0.7)))))
            if won and surv == 0:
                surv = 1
            if not won and winner != "draw":
                surv = 0 if rng.random() < 0.6 else min(surv, 1)
            hp_lost = max(0.0, min(SQUAD * HP_PER_UNIT, (1.0 - q) * 280.0 + rng.gauss(0, 30)))
            flanks = rng.randint(0, 3)
            return {"survivors": surv, "squad_size": SQUAD, "hp_lost": round(hp_lost, 1),
                    "hp_max": SQUAD * HP_PER_UNIT, "kills": SQUAD - rng.randint(0, 2) if won else rng.randint(0, 3),
                    "flanks_attempted": flanks, "flanks_succeeded": sum(rng.random() < q for _ in range(flanks)),
                    "suppression_s": round(max(0.0, rng.gauss(18 * q, 6)), 1),
                    "ambushes": int(rng.random() < 0.3 * q), "retreats": int(rng.random() < 0.3),
                    "retreats_survived": 0, "heals": rng.randint(0, 2),
                    "friendly_fire": int(rng.random() < 0.15 * (1 - q)), "stuck_events": int(rng.random() < 0.05)}

        a, b = side(quality, winner == "A"), side(1.0 - quality, winner == "B")
        for s in (a, b):
            s["retreats_survived"] = s["retreats"] if s["survivors"] > 0 else 0
        duration = max(20.0, min(time_limit_s, 160.0 + rng.gauss(0, 50) - 40.0 * abs(quality - 0.5)))
        highlight = (a["kills"] + b["kills"] + 2.0 * (a["flanks_succeeded"] + b["flanks_succeeded"])
                     + 3.0 * (a["ambushes"] + b["ambushes"]) + 3.0 * (1.0 - abs(a["survivors"] - b["survivors"]) / SQUAD)
                     + rng.random())
        rnd = {"index": i, "seed": seed + i, "winner": winner, "duration_s": round(duration, 1),
               "end_reason": "elimination" if winner != "draw" else "time_limit", "highlight_score": round(highlight, 2),
               "sides": {"A": dict(a, team="pmc"), "B": dict(b, team="pmc" if opponent != "bot" else "player_bot")}}
        if track_prefix:
            path = f"{track_prefix}.round_{i}.track.json"
            with open(path, "w", encoding="utf-8") as f:
                json.dump(fake_track(rnd, random.Random(f"track:{seed}:{i}")), f, separators=(",", ":"))
            rnd["track"] = os.path.basename(path)
        out_rounds.append(rnd)
    return {"schema": "codex.pmc_arena/1", "map": "mock", "seed": seed, "rounds_requested": rounds,
            "time_limit_s": time_limit_s, "opponent": opponent, "mock": True, "rounds": out_rounds}


def fake_track(rnd: dict, rng: random.Random, dt: float = 0.2) -> dict:
    """A plausible codex.pmc_arena_track/1 for the replay viewer: two squads close in across a 60 x 40 m yard,
    shots / downs / kills consistent with the round's survivors, tactic decisions and heals as events."""
    duration = float(rnd["duration_s"])
    ids = [f"{s}{n + 1}" for s in "AB" for n in range(SQUAD)]
    pos = {f: [5.0 if f[0] == "A" else 55.0, 8.0 + 8.0 * (int(f[1:]) - 1) + rng.uniform(-2, 2)] for f in ids}
    death = {}
    for side in "AB":
        dead = SQUAD - int(rnd["sides"][side]["survivors"])
        for f in rng.sample([x for x in ids if x[0] == side], dead):
            death[f] = round(rng.uniform(0.3, 0.95) * duration, 1)
    flankers = {"A1"} if rnd["sides"]["A"]["flanks_attempted"] else set()
    flankers |= {"B4"} if rnd["sides"]["B"]["flanks_attempted"] else set()
    events = [{"t": 0.0, "type": "tactic", "side": s, "decision": "advance"} for s in "AB"]
    for f in sorted(flankers):
        events.append({"t": 20.0, "type": "tactic", "side": f[0], "decision": "flank", "by": f})
    for s in "AB":
        if rnd["sides"][s]["suppression_s"] > 10:
            events.append({"t": 30.0, "type": "tactic", "side": s, "decision": "suppress"})
    frames, t, next_shot = [], 0.0, {f: rng.uniform(12, 18) for f in ids}
    while t <= duration + 1e-6:
        alive = [f for f in ids if death.get(f, 1e9) > t]
        frame = []
        for f in ids:
            if f not in alive:
                frame.append(None)
                continue
            foes = [g for g in alive if g[0] != f[0]] or [g for g in ids if g[0] != f[0]]
            cx = sum(pos[g][0] for g in foes) / len(foes)
            cy = sum(pos[g][1] for g in foes) / len(foes)
            dx, dy = cx - pos[f][0], cy - pos[f][1]
            dist = max(0.1, math.hypot(dx, dy))
            if f in flankers and t > 20.0:
                dx, dy = dx - dy * 1.5, dy + dx * 0.4  # arc around
            hold = 14.0 + 3.0 * (int(f[1:]) % 3)  # each fighter keeps his own firing distance
            speed = 0.6 if (dist > hold or (f in flankers and t < 45.0)) else (-0.2 if dist < hold - 4 else 0.0)
            pos[f][0] = min(60.0, max(0.0, pos[f][0] + speed * dx / dist + rng.uniform(-0.15, 0.15)))
            pos[f][1] = min(40.0, max(0.0, pos[f][1] + speed * dy / dist + rng.uniform(-0.15, 0.15)))
            frame.append([round(pos[f][0], 1), round(pos[f][1], 1)])
            if t >= next_shot[f]:
                target = rng.choice(foes)
                events.append({"t": round(t, 1), "type": "shot", "from": f, "to": target, "hit": rng.random() < 0.3})
                next_shot[f] = t + rng.uniform(1.0, 2.5)
        frames.append(frame)
        t += dt
    for f, td in death.items():
        killer = rng.choice([g for g in ids if g[0] != f[0]])
        events.append({"t": round(max(0.0, td - 2.0), 1), "type": "down", "target": f, "by": killer})
        events.append({"t": td, "type": "kill", "target": f, "by": killer})
    for s in "AB":
        for _ in range(int(rnd["sides"][s]["heals"])):
            medic = f"{s}{rng.randint(1, SQUAD)}"
            events.append({"t": round(rng.uniform(25, max(26, duration - 5)), 1), "type": "heal", "by": medic,
                           "target": f"{s}{rng.randint(1, SQUAD)}"})
    events.sort(key=lambda e: e["t"])
    return {"schema": "codex.pmc_arena_track/1", "round": rnd["index"], "seed": rnd["seed"], "dt": dt, "units": "m",
            "fighters": [{"id": f, "side": f[0], "team": rnd["sides"][f[0]]["team"]} for f in ids],
            "frames": frames, "events": events, "winner": rnd["winner"], "mock": True}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--weights-a", required=True)
    ap.add_argument("--weights-b")
    ap.add_argument("--opponent", default="bot")
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", required=True)
    ap.add_argument("--reference", required=True)
    ap.add_argument("--crash-rate", type=float, default=0.0)
    ap.add_argument("--bot-strength", type=float, default=-0.03)
    ap.add_argument("--tracks", action="store_true", help="also write per-round track files")
    a = ap.parse_args(argv)
    if a.crash_rate > 0 and random.Random(f"crash:{a.seed}:{a.weights_a}").random() < a.crash_rate:
        print("mock arena: simulated crash", file=sys.stderr)
        return 3
    doc = simulate(load_json(a.weights_a), a.opponent, load_json(a.weights_b) if a.weights_b else None,
                   load_json(a.reference), a.rounds, a.seed, a.bot_strength,
                   track_prefix=os.path.splitext(a.out)[0] if a.tracks else None)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
