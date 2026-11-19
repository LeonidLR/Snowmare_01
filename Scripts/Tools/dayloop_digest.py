"""Smart-enemies day loop digest (roadmap 5a): a short summary of one scenario's N seeded arena / smoke results.

Reads every seed*.json / run*.json (schema codex.pmc_arena/1, written by Scripts/Tools/worker_seed_batch.ps1 runs) in a folder
and prints: the pass rate (from summary.json when present), the median of the T-04 defence metrics, and the worst 3 seeds.
Never prints raw logs (token economy). Usage:
    python Scripts/Tools/dayloop_digest.py Saved/Logs/Worker/dayloop-crossroads-1 [--json out.json]
"""
from __future__ import annotations

import argparse
import json
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
    r = {
        "run": path.stem,
        "state": states.get(path.stem, "?"),
        "winner": rnd.get("winner"),
        "end": rnd.get("end_reason"),
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--json")
    a = ap.parse_args()
    folder = Path(a.folder)
    states: dict[str, str] = {}
    summary = folder / "summary.json"
    if summary.exists():
        try:
            s = json.loads(summary.read_text(encoding="utf-8-sig"))
            for x in s.get("runs", s if isinstance(s, list) else []):
                if isinstance(x, dict) and x.get("run"):
                    states[str(x["run"])] = str(x.get("state", "?"))
        except (ValueError, AttributeError):
            pass
    files = sorted(p for p in folder.glob("*.json") if p.name != "summary.json")
    rows = [row(p, states) for p in files]
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
