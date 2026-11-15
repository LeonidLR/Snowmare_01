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
# Refined by team-chat #452 after the 31-clip measurement (2026-11-15).
# ON TOP uses endpoint dz; OVER uses peak root rise. Asset enable_root_motion=False
# does not remove the source root track: retain the measurement and emit a warning.
# Step/blocked walls have no authored upward clip and remain unmeasured.
CLASS_CLIPS = {
    "step": (),
    "low_vault": ("LowVault", "RunLowVault"),
    "low_mantle": ("Low_Climb", "RunLowClimb"),
    "high_mantle": ("HighClimb", "RunHighClimb", "HI_Climb_Climb_Up"),
    "tall_ledge": ("Standing_JumpClimbUpNEW", "TallLedge_ClimbUpNEW", "RunJumpClimbNEW"),
    "very_tall": ("JumpClimb", "RunJumpClimb"),
    "high_fence_over": ("HighFenceVault",),
    "wall_no_traverse": (),
}
NEW_CLASSES = ("tall_ledge", "very_tall", "high_fence_over")
CLASS_METRICS = {key: "max-rise" if key in {"low_vault", "high_fence_over"} else "dz"
                 for key in CLASS_CLIPS}
CLIP_METRICS = {name: CLASS_METRICS[key] for key, names in CLASS_CLIPS.items() for name in names}
# These actions return to their starting level, have no upward root track, or need
# phase/contact inspection. Show raw dz AND peak, but never fold them into a class.
REVIEW_CLIPS = ("JumpFenceVault", "JumpFenceClimb", "Standing_Tall_Wall_Climb", "Sprint_Footplant_TallWallClimb")


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
    keys = {"dz": "dz_up_cm", "max-rise": "max_root_rise_cm"}
    selected = keys[CLIP_METRICS[name] if metric == "auto" else metric]
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
    keys = list(document["classes"]) + [k for k in NEW_CLASSES if k not in document["classes"]]
    order = {key: index for index, key in enumerate(CLASS_CLIPS)}
    keys.sort(key=lambda key: order.get(key, len(order)))
    for key in keys:
        is_new = key not in document["classes"]
        original = document["classes"].get(key, {})
        nominal, tolerance = original.get("nominal_cm"), original.get("tolerance_cm")
        if not is_new and (not isinstance(nominal, (int, float)) or isinstance(nominal, bool) or
                not isinstance(tolerance, (int, float)) or isinstance(tolerance, bool) or
                not math.isfinite(nominal) or not math.isfinite(tolerance) or nominal <= 0 or tolerance < 0):
            raise ValueError(f"Invalid nominal/tolerance: {key}")
        names = CLASS_CLIPS.get(key)
        proposal = {"class": key, "old_nominal_cm": nominal, "old_tolerance_cm": tolerance,
                    "nominal_cm": None, "tolerance_cm": None, "spread_cm": None,
                    "measurements": [], "flags": ["NEW_CLASS"] if is_new else [], "blocking": False}
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
            rm_enabled = matches[0]["root_motion_enabled"].strip().lower() in {"true", "1"}
            warnings = [] if rm_enabled else ["WARN RM_FLAG_OFF"]
            if warnings and "WARN RM_FLAG_OFF" not in proposal["flags"]:
                proposal["flags"].append("WARN RM_FLAG_OFF")
            proposal["measurements"].append({"clip": matches[0]["asset_path"], "rise_cm": value,
                                           "metric": selected, "status": matches[0]["status"],
                                           "root_motion_enabled": rm_enabled, "warnings": warnings})
        values = [m["rise_cm"] for m in proposal["measurements"]]
        if not values:
            proposal["flags"].append("NO_CLIP_MEASUREMENT")
            proposal["blocking"] = True
        else:
            target = nearest_five(median(values))
            proposed_tolerance = max(5, int(math.ceil(max(abs(v - target) for v in values) / 5) * 5))
            spread = max(values) - min(values)
            proposal.update(nominal_cm=target, tolerance_cm=proposed_tolerance, spread_cm=spread)
            # Existing classes keep their approved conflict threshold. A new class has
            # no previous tolerance; compare its spread with the proposed tolerance.
            if spread > (proposed_tolerance if tolerance is None else tolerance):
                proposal["flags"].append("SPREAD_EXCEEDS_TOLERANCE")
                proposal["blocking"] = True
            lo, hi = original.get("min_cm"), original.get("max_cm")
            if target <= 0 or (lo is not None and min([target] + values) < lo) or (hi is not None and max([target] + values) > hi):
                proposal["flags"].append("OUTSIDE_CLASS_RANGE")
                proposal["blocking"] = True
        proposals.append(proposal)
    return proposals


def review_measurements(rows):
    """Unassigned actions remain evidence for manual review, never measured classes."""
    result = []
    for row in rows:
        if clip_name(row["asset_path"]) not in REVIEW_CLIPS:
            continue
        flags = ["NEEDS_REVIEW"]
        if row["root_motion_enabled"].strip().lower() not in {"true", "1"}:
            flags.append("WARN RM_FLAG_OFF")
        result.append({"clip": row["asset_path"], "dz_up_cm": row["dz_up_cm"],
                       "max_root_rise_cm": row["max_root_rise_cm"], "status": row["status"], "flags": flags})
    return result


def apply_proposals(document, proposals, csv_path):
    conflicts = [p["class"] for p in proposals if p["blocking"]]
    if conflicts:
        raise ValueError("Conflicted classes; no JSON written: " + ", ".join(conflicts))
    result = copy.deepcopy(document)
    for proposal in proposals:
        if proposal["nominal_cm"] is None:
            continue  # Step/wall remain unchanged and unmeasured.
        entry = result["classes"].setdefault(proposal["class"], {})
        if proposal["old_nominal_cm"] is None:
            entry.update(min_cm=proposal["nominal_cm"] - proposal["tolerance_cm"],
                         max_cm=proposal["nominal_cm"] + proposal["tolerance_cm"])
        entry.update(nominal_cm=proposal["nominal_cm"], tolerance_cm=proposal["tolerance_cm"],
                     measured=True, source_clips=[m["clip"] for m in proposal["measurements"]],
                     measurement_source=str(csv_path), measurement_basis="source_root_motion_proxy",
                     source_measurements=proposal["measurements"])
    return result


def print_proposals(proposals):
    print("class | before cm/tol | proposed cm/tol | clips (rise cm) | spread cm | flags")
    for p in proposals:
        before = "new" if p["old_nominal_cm"] is None else f"{p['old_nominal_cm']:g} +/-{p['old_tolerance_cm']:g}"
        after = "unchanged" if p["nominal_cm"] is None else f"{p['nominal_cm']} +/-{p['tolerance_cm']}"
        clips = ", ".join(f"{clip_name(m['clip'])}:{m['rise_cm']:g}" for m in p["measurements"]) or "none"
        spread = "n/a" if p["spread_cm"] is None else f"{p['spread_cm']:g}"
        print(f"{p['class']} | {before} | {after} | {clips} | {spread} | {','.join(p['flags']) or 'OK'}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=ROOT / "Saved/Measure/anim_root_motion.csv")
    parser.add_argument("--heights", type=Path, default=ROOT / "Content/Data/LevelGen/obstacle_heights.json")
    parser.add_argument("--metric", choices=("auto", "dz", "max-rise"), default="auto",
                        help="auto: class metric; mantle uses endpoint dz, over uses peak rise (no dz-to-peak fallback)")
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--output", type=Path, help="exact JSON destination, required with --write")
    args = parser.parse_args(argv)
    if args.write and args.output is None:
        parser.error("--write requires an explicit --output JSON path")
    if args.output and (args.output.suffix.lower() != ".json" or args.output.resolve() == args.csv.resolve()):
        parser.error("--output must be a JSON path different from the CSV input")
    try:
        document = json.loads(args.heights.read_text(encoding="utf-8-sig"))
        rows = read_measurements(args.csv)
        proposals = propose_heights(document, rows, args.metric)
        print_proposals(proposals)
        reviews = review_measurements(rows)
        if reviews:
            print("review clip | endpoint dz cm | peak rise cm | status | flags")
            for review in reviews:
                print(f"{clip_name(review['clip'])} | {review['dz_up_cm']} | {review['max_root_rise_cm']} | {review['status']} | {','.join(review['flags'])}")
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
