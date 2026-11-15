"""Propose obstacle height targets from source-root measurements; dry run by default.

python Scripts/Tools/apply_measured_heights.py --csv measurements.csv --heights heights.json
Add --write --output exact-target.json to write explicitly; no implicit output path.
Root displacement/rise is a measurement proxy, not proof of hand/foot contact geometry.
The preview/contact review in MANTLING section 11 still applies.
"""

import argparse
import copy
import csv
import json
import math
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parents[2]
CAV_PREFIX = "/Game/CaV_Anims/Animations/ThirdPerson_Anims/"
# MANTLING.md section 11.2: upward clips only, grouped by provisional height class.
# High-fence OVER actions share the high class, but retain their own action at runtime.
# Step/blocked walls intentionally have no authored climb clip; never mark them measured.
# Land/roll/drop, Wall_Run, ambiguous JumpFenceClimb and split hang phases are excluded.
CLASS_CLIPS = {
    "step": (),
    "low_vault": ("LowVault", "RunLowVault"),
    "low_mantle": ("Low_Climb", "RunLowClimb"),
    "high_mantle": (
        "HighClimb", "RunHighClimb", "JumpClimb", "RunJumpClimb",
        "Standing_JumpClimbUpNEW", "RunJumpClimbNEW", "TallLedge_ClimbUpNEW",
        "Standing_Tall_Wall_Climb", "Sprint_Footplant_TallWallClimb",
        "HighFenceVault", "JumpFenceVault",
    ),
    "wall_no_traverse": (),
}
OVER_CLIPS = {"LowVault", "RunLowVault", "HighFenceVault", "JumpFenceVault"}


def nearest_five(value):
    """Positive half-way values round upward, independent of Python's ties-to-even."""
    return int(math.floor(value / 5 + 0.5) * 5)


def read_measurements(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"asset_path", "dz_up_cm", "max_root_rise_cm", "status", "root_motion_enabled"}
        missing = required - set(reader.fieldnames or ())
        if missing:
            raise ValueError("CSV missing columns: " + ", ".join(sorted(missing)))
        return list(reader)


def clip_name(asset_path):
    if not asset_path.startswith(CAV_PREFIX):
        return None
    return asset_path.rsplit("/", 1)[-1].split(".", 1)[0]


def measured_rise(row, name, metric):
    if row["status"].strip().lower() not in {"ok", "fallback"}:
        raise ValueError("measurement status=" + row["status"])
    if row["root_motion_enabled"].strip().lower() not in {"true", "1"}:
        raise ValueError("root motion disabled or unknown")
    keys = {"dz": "dz_up_cm", "max-rise": "max_root_rise_cm"}
    if metric == "auto":
        # OVER returns to the far-side ground: its endpoint dz can legitimately be zero.
        # ON TOP prefers net positive dz; use peak rise when that endpoint is not usable.
        selected = "max_root_rise_cm" if name in OVER_CLIPS else "dz_up_cm"
        if selected == "dz_up_cm":
            try:
                net = float(row[selected])
            except (TypeError, ValueError):
                net = math.nan
            if not math.isfinite(net) or net <= 0:
                selected = "max_root_rise_cm"
    else:
        selected = keys[metric]
    value = float(row[selected])
    if not math.isfinite(value) or value <= 0:
        raise ValueError(f"{selected} must be finite and positive")
    return value, selected


def propose_heights(document, rows, metric="auto"):
    if metric not in {"auto", "dz", "max-rise"}:
        raise ValueError("Unknown metric: " + metric)
    if document.get("schema") != "codex.obstacle_heights/1" or not isinstance(document.get("classes"), dict):
        raise ValueError("Expected codex.obstacle_heights/1 classes object")
    indexed = {}
    for row in rows:
        name = clip_name(row["asset_path"])
        if name:
            indexed.setdefault(name, []).append(row)
    proposals = []
    for key, original in document["classes"].items():
        nominal, tolerance = original.get("nominal_cm"), original.get("tolerance_cm")
        if (not isinstance(nominal, (int, float)) or isinstance(nominal, bool) or
                not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool) or
                not math.isfinite(nominal) or not math.isfinite(tolerance) or nominal <= 0 or tolerance < 0):
            raise ValueError(f"Invalid nominal/tolerance: {key}")
        names = CLASS_CLIPS.get(key)
        proposal = {"class": key, "old_nominal_cm": nominal, "old_tolerance_cm": tolerance,
                    "nominal_cm": None, "tolerance_cm": None, "spread_cm": None,
                    "measurements": [], "flags": [], "blocking": False}
        if not names:
            proposal["flags"].append("NO_CLIP" if names is not None else "NO_MAPPING")
            proposal["blocking"] = names is None
            proposals.append(proposal)
            continue
        for name in names:
            matches = indexed.get(name, [])
            if len(matches) > 1:
                proposal["flags"].append(f"DUPLICATE_CLIP:{name}")
                proposal["blocking"] = True
                continue
            if not matches:
                continue
            try:
                value, selected = measured_rise(matches[0], name, metric)
            except (ValueError, TypeError) as error:
                proposal["flags"].append(f"INVALID_CLIP:{name}:{error}")
                proposal["blocking"] = True
                continue
            proposal["measurements"].append({"clip": matches[0]["asset_path"], "rise_cm": value,
                                               "metric": selected, "status": matches[0]["status"]})
        values = [m["rise_cm"] for m in proposal["measurements"]]
        if not values:
            proposal["flags"].append("NO_CLIP_MEASUREMENT")
            proposal["blocking"] = True
        else:
            target = nearest_five(median(values))
            proposed_tolerance = max(5, int(math.ceil(max(abs(v - target) for v in values) / 5) * 5))
            spread = max(values) - min(values)
            proposal.update(nominal_cm=target, tolerance_cm=proposed_tolerance, spread_cm=spread)
            # Compare against the EXISTING allowed spread; expanding the suggested tolerance
            # must not silently hide inconsistent clip families in one class.
            if spread > tolerance:
                proposal["flags"].append("SPREAD_EXCEEDS_TOLERANCE")
                proposal["blocking"] = True
            lo, hi = original.get("min_cm"), original.get("max_cm")
            if target <= 0 or (lo is not None and min([target] + values) < lo) or (hi is not None and max([target] + values) > hi):
                proposal["flags"].append("OUTSIDE_CLASS_RANGE")
                proposal["blocking"] = True
        proposals.append(proposal)
    return proposals


def apply_proposals(document, proposals, csv_path):
    conflicts = [p["class"] for p in proposals if p["blocking"]]
    if conflicts:
        raise ValueError("Conflicted classes; no JSON written: " + ", ".join(conflicts))
    result = copy.deepcopy(document)
    for proposal in proposals:
        if proposal["nominal_cm"] is None:
            continue  # Step/wall remain unchanged and unmeasured.
        entry = result["classes"][proposal["class"]]
        entry.update(nominal_cm=proposal["nominal_cm"], tolerance_cm=proposal["tolerance_cm"],
                     measured=True, source_clips=[m["clip"] for m in proposal["measurements"]],
                     measurement_source=str(csv_path), measurement_basis="source_root_motion_proxy",
                     source_measurements=proposal["measurements"])
    return result


def print_proposals(proposals):
    print("class | before cm/tol | proposed cm/tol | clips (rise cm) | spread cm | flags")
    for p in proposals:
        before = f"{p['old_nominal_cm']:g} +/-{p['old_tolerance_cm']:g}"
        after = "unchanged" if p["nominal_cm"] is None else f"{p['nominal_cm']} +/-{p['tolerance_cm']}"
        clips = ", ".join(f"{clip_name(m['clip'])}:{m['rise_cm']:g}" for m in p["measurements"]) or "none"
        spread = "n/a" if p["spread_cm"] is None else f"{p['spread_cm']:g}"
        print(f"{p['class']} | {before} | {after} | {clips} | {spread} | {','.join(p['flags']) or 'OK'}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=ROOT / "Saved/Measure/anim_root_motion.csv")
    parser.add_argument("--heights", type=Path, default=ROOT / "Content/Data/LevelGen/obstacle_heights.json")
    parser.add_argument("--metric", choices=("auto", "dz", "max-rise"), default="auto",
                        help="auto: OVER uses peak rise; ON TOP uses positive dz, otherwise peak")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--output", type=Path, help="exact JSON destination, required with --write")
    args = parser.parse_args(argv)
    if args.write and args.output is None:
        parser.error("--write requires an explicit --output JSON path")
    if args.output and (args.output.suffix.lower() != ".json" or args.output.resolve() == args.csv.resolve()):
        parser.error("--output must be a JSON path different from the CSV input")
    try:
        document = json.loads(args.heights.read_text(encoding="utf-8-sig"))
        proposals = propose_heights(document, read_measurements(args.csv), args.metric)
        print_proposals(proposals)
        print("Root-motion proposals require the contact/landing review in MANTLING section 11.")
        if args.write:
            result = apply_proposals(document, proposals, args.csv)
            # Only this explicitly supplied path is written. No input/default-path rewrite,
            # directory creation, generated maps, or secondary files.
            args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
            print("WROTE " + str(args.output))
        else:
            print("DRY RUN: no files written")
        return 2 if any(p["blocking"] for p in proposals) else 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("ERROR: " + str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
