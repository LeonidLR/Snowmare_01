"""Smart-enemies day loop digest (roadmap 5a): a short summary of one scenario's N seeded arena / smoke results.

Reads every seed*.json / run*.json (schema codex.pmc_arena/1, written by Scripts/Tools/worker_seed_batch.ps1 runs) in a folder
and prints: the pass rate (from summary.json when present), the median of the T-04 defence metrics, and the worst 3 seeds.
Never prints raw logs (token economy). Usage:
    python Scripts/Tools/dayloop_digest.py Saved/Logs/Worker/dayloop-crossroads-1 [--json out.json]
    python Scripts/Tools/dayloop_digest.py folderA --compare folderB [--tune-seeds 1-10]
"""
from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

# GDD A-00A / A-00B thresholds as used by PmcCrossroadsSmoke / PmcAlleySmoke (T-04 constexprs; JSON at T-08).
IDLE_MAX_S = 2.0
CROSSFIRE_MIN_DEG = 45.0
SHOT_GAP_MAX_S = 1.5


def defender_side(rnd: dict) -> str | None:
    for key, side in (rnd.get("sides") or {}).items():
        if isinstance(side, dict) and side.get("role") == "defender":
            return key
    return None


def row(path: Path, states: dict[str, str]) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    rnd = (data.get("rounds") or [{}])[0]
    d = rnd.get("defence") or {}
    side = defender_side(rnd)
    idle = [f.get("before_contact_s", 0.0) for f in d.get("open_idle_s") or [] if side is None or f.get("side") == side]

    # 093 paired A/B fields
    seed = rnd.get("seed")
    if seed is None:
        m = re.search(r"(?:seed|run)?(\d+)", path.stem)
        seed = int(m.group(1)) if m else path.stem
    scenario = rnd.get("scenario") or "default"
    def_side = side or "A"
    atk_side = None
    for k, s in (rnd.get("sides") or {}).items():
        if isinstance(s, dict) and s.get("role") == "attacker":
            atk_side = k
            break
    if atk_side is None:
        atk_side = "B" if def_side == "A" else "A"

    def_win = (rnd.get("winner") == def_side)
    end_reason = rnd.get("end_reason")
    atk_capture = (end_reason == "objective" and rnd.get("winner") != def_side)

    sides = rnd.get("sides") or {}
    def_info = sides.get(def_side) or {}
    atk_info = sides.get(atk_side) or {}
    def_kills = def_info.get("kills")
    squad_size = atk_info.get("squad_size", 3)
    survivors = atk_info.get("survivors")

    if survivors is not None:
        att_down = max(0, squad_size - survivors)
    elif def_kills is not None:
        att_down = def_kills
    else:
        att_down = squad_size if def_win else 0

    if def_kills is not None:
        att_killed = def_kills
    else:
        att_killed = att_down

    r = {
        "run": path.stem,
        "seed": seed,
        "scenario": scenario,
        "state": states.get(path.stem, "?"),
        "winner": rnd.get("winner"),
        "end": end_reason,
        "def_side": def_side,
        "atk_side": atk_side,
        "def_win": bool(def_win),
        "atk_capture": bool(atk_capture),
        "att_killed": att_killed,
        "att_down": att_down,
        "att_down_ge2": bool(att_down >= 2),
        "on_pos": d.get("on_position_before_contact"),
        "idle_max": max(idle) if idle else None,
        "crossfire": d.get("crossfire_deg"),
        "gap": d.get("first_shot_gap_s"),
        "exposed_m": d.get("max_exposed_run_m"),
        "spread": d.get("spread_m"),
    }
    misses = 0
    misses += r["on_pos"] is not None and r["on_pos"] < 1.0
    misses += r["idle_max"] is not None and r["idle_max"] > IDLE_MAX_S
    misses += r["crossfire"] is not None and r["crossfire"] < CROSSFIRE_MIN_DEG
    misses += r["gap"] is not None and r["gap"] > SHOT_GAP_MAX_S
    r["misses"] = int(misses)
    return r


def med(rows: list[dict], key: str):
    vals = [x[key] for x in rows if isinstance(x.get(key), (int, float))]
    return round(statistics.median(vals), 2) if vals else None


def load_folder(folder: Path | str) -> list[dict]:
    f = Path(folder)
    states: dict[str, str] = {}
    summary = f / "summary.json"
    if summary.exists():
        try:
            s = json.loads(summary.read_text(encoding="utf-8-sig"))
            for x in s.get("runs", s if isinstance(s, list) else []):
                if isinstance(x, dict):
                    if x.get("run"):
                        states[str(x["run"])] = str(x.get("state", "?"))
                    if x.get("tag"):
                        states[str(x["tag"])] = str(x.get("state", "?"))
        except (ValueError, AttributeError):
            pass
    files = sorted(p for p in f.glob("*.json") if p.name != "summary.json")
    if not files:
        files = sorted(p for p in f.rglob("*.json") if p.name != "summary.json")
    return [row(p, states) for p in files]


def main() -> int:
    ap = argparse.ArgumentParser(description="Smart-enemies day loop digest")
    ap.add_argument("folder", help="Path to result folder")
    ap.add_argument("--compare", help="Path to second result folder to run paired A/B comparison against")
    ap.add_argument("--tune-seeds", help="Seed spec for tuning set (e.g. '1-10'); reports tune vs held-out")
    ap.add_argument("--rng-seed", type=int, default=42, help="RNG seed for bootstrap resampling (default: 42)")
    ap.add_argument("--resamples", type=int, default=2000, help="Number of bootstrap resamples (default: 2000)")
    ap.add_argument("--json", help="Path to output JSON")
    a = ap.parse_args()

    if a.compare:
        try:
            from paired_ab import compare_folders_cli
        except ImportError:
            from Scripts.Tools.paired_ab import compare_folders_cli
        return compare_folders_cli(
            a.folder,
            a.compare,
            tune_seeds=a.tune_seeds,
            rng_seed=a.rng_seed,
            n_resamples=a.resamples,
            json_out=a.json,
        )

    folder = Path(a.folder)
    rows = load_folder(folder)
    if not rows:
        print(f"no result JSON in {folder}")
        return 1
    passed = sum(1 for x in rows if x["state"] == "PASS")
    known = sum(1 for x in rows if x["state"] != "?")
    print(f"day loop digest: {folder.name}, {len(rows)} seeds" + (f", PASS {passed}/{known}" if known else ""))
    print("median: " + ", ".join(f"{k} {med(rows, k)}" for k in ("on_pos", "idle_max", "crossfire", "gap", "exposed_m", "spread")))
    print(f"criteria misses per seed (on_pos<1, idle>{IDLE_MAX_S}s, crossfire<{CROSSFIRE_MIN_DEG}, gap>{SHOT_GAP_MAX_S}s): "
          + ", ".join(f"{n}:{sum(1 for x in rows if x['misses'] == n)}" for n in range(5)))
    worst = sorted(rows, key=lambda x: (-x["misses"], x["crossfire"] or 0))[:3]
    for x in worst:
        print(f"  worst {x['run']}: {x['state']} misses {x['misses']} on_pos {x['on_pos']} idle {x['idle_max']} "
              f"crossfire {x['crossfire']} gap {x['gap']} exposed {x['exposed_m']} m, {x['winner']} {x['end']}")
    if a.json:
        Path(a.json).write_text(json.dumps(rows, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
