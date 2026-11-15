"""Mantling slice 1 (docs/design/MANTLING.md 11.6, the game designer's L_Gen_Depot_1 obstacle rework 2026-11-15): rewrites the ACCEPTED Depot
layout Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json onto the obstacle height classes of Content/Data/LevelGen/obstacle_heights.json.

  sandbags 100 -> 95, jersey barriers 90 -> 95          (low_vault: the existing vault)
  barrels 90 stay, NOT traversable                       (explosive: VaultRules no longer vaults them)
  wrecks 150 -> 140, boilers 160 -> 140                  (high_mantle: climbed on top)
  crates 110 -> 85 / 140 alternately (by id order)       (low_mantle / high_mantle: about half each)
  cut parapet walls 220 -> 230                           (tall_ledge)
  fences 250 -> 135 for LOWERED_FENCES (about a third: interior fences at least 12 m from every ambush point, never the map's perimeter),
                the rest 330                             (high_fence_over / wall_no_traverse)
  one-storey roofs (300): unchanged in this slice (marked mantle edges are slice 2)

Idempotent (the target heights are absolute). Run: python Scripts/Tools/apply_depot_height_classes.py [--check] - then rebuild the map with
Scripts/levelgen_build_map.ps1 -Layout gen_ref_01_02_depot_1 -Target /Game/Maps/Generated/L_Gen_Depot_1. --check: exit 1 when the file is not
reworked yet (nothing written).
"""
import argparse
import json
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LAYOUT = os.path.join(ROOT, "Content", "Data", "LevelGen", "accepted", "gen_ref_01_02_depot_1.json")
HEIGHTS = os.path.join(ROOT, "Content", "Data", "LevelGen", "obstacle_heights.json")

# The fences lowered to high_fence_over (the designer: "about a third, only where it does not break routes or devalue ambush fields"): the
# interior fences whose whole length lies at least 12 m from every ambush point of the layout, none of the map's perimeter fences.
LOWERED_FENCES = (
    "F_compound_N_1", "F_compound_E_0", "F_boilers_S_0", "F_boilers_S_1", "F_boilers_E_0", "F_lot_N_0", "F_lot_S_0", "F_lot_S_1", "F_lot_W_0",
    "F_junkyard_S_2", "F_east_N_0",
)
AMBUSH_CLEARANCE_CM = 1200.0


def nominal(classes, name):
    return float(classes[name]["nominal_cm"])


def segment_distance(p, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / length2))
    return math.hypot(p[0] - ax - t * dx, p[1] - ay - t * dy)


def rework(layout, classes):
    """The reworked layout and a list of report lines."""
    low_vault = nominal(classes, "low_vault")
    low_mantle = nominal(classes, "low_mantle")
    high_mantle = nominal(classes, "high_mantle")
    tall_ledge = nominal(classes, "tall_ledge")
    fence_over = nominal(classes, "high_fence_over")
    wall = nominal(classes, "wall_no_traverse")
    report = []
    counts = {}
    crates = sorted((c for c in layout["covers"] if c.get("kind") == "crate"), key=lambda c: c["id"])
    crate_height = {c["id"]: (low_mantle if n % 2 == 0 else high_mantle) for n, c in enumerate(crates)}
    for c in layout["covers"]:
        kind = c.get("kind")
        target = {"sandbag": low_vault, "jersey": low_vault, "wreck": high_mantle, "boiler": high_mantle}.get(kind)
        if kind == "crate":
            target = crate_height[c["id"]]
        if target is None:
            continue
        old = c["size"][2]
        c["size"][2] = int(target)
        counts.setdefault((kind, old, int(target)), 0)
        counts[(kind, old, int(target))] += 1
    ambush = [a["pos"] for a in layout.get("ambush_points", [])]
    for w in layout["walls"]:
        kind = w.get("kind")
        old = w["height"]
        if kind == "cut_wall":
            w["height"] = int(tall_ledge)
        elif kind == "fence":
            lowered = w["id"] in LOWERED_FENCES
            w["height"] = int(fence_over if lowered else wall)
            if lowered:
                near = min((segment_distance(p, w["a"], w["b"]) for p in ambush), default=1e9)
                if near < AMBUSH_CLEARANCE_CM:
                    raise ValueError("%s lies %.0f cm from an ambush point (< %.0f)" % (w["id"], near, AMBUSH_CLEARANCE_CM))
        else:
            continue
        counts.setdefault((kind, old, w["height"]), 0)
        counts[(kind, old, w["height"])] += 1
    missing = [f for f in LOWERED_FENCES if not any(w["id"] == f for w in layout["walls"])]
    if missing:
        raise ValueError("unknown fences in LOWERED_FENCES: %s" % ", ".join(missing))
    for (kind, old, new), n in sorted(counts.items()):
        report.append("%-9s %3d x %4s -> %4d cm" % (kind, n, old, new))
    return layout, report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="exit 1 when the layout is not reworked yet (writes nothing)")
    args = parser.parse_args()
    with open(HEIGHTS, encoding="utf-8") as f:
        classes = json.load(f)["classes"]
    with open(LAYOUT, encoding="utf-8") as f:
        text = f.read()
    layout, report = rework(json.loads(text), classes)
    out = json.dumps(layout, indent=1) + "\n"
    for line in report:
        print(line)
    if args.check:
        print("reworked" if out == text else "NOT reworked")
        return 0 if out == text else 1
    if out != text:
        with open(LAYOUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        print("written %s" % LAYOUT)
    else:
        print("already reworked: %s" % LAYOUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
