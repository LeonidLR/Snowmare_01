"""Read-only UE 5.8 animation source-motion measurement (no asset saves).

Commandlet (put script arguments INSIDE the -script value):
  UnrealEditor-Cmd.exe Project.uproject -run=pythonscript \
    -script="Scripts/Editor/measure_anim_root_motion.py /Game/CaV_Anims/Animations/ThirdPerson_Anims"
Optional: --sample-hz 30. Output: Saved/Measure/anim_root_motion.csv.

Translations are source/component axes: +X forward, +Y right, +Z up, cm.
Deltas and sampled maximum rise are relative to time zero, not distance travelled.
Samples include 0, the exact endpoint, source frame boundaries and 1/sample_hz.
Peak time is the earliest sampled maximum; this is not a continuous-curve solve.
Pelvis is diagnostic ONLY, never substituted for a missing/static root.
Frames = AnimationLibrary.get_num_frames (sampled keys minus one, UE convention).

Verified in installed Engine/Source/Editor/AnimationBlueprintLibrary:
  Public/AnimPose.h: GetAnimPoseAtTime, GetBoneNames, GetBonePose,
    EvaluationOptions.bIncorporateRootMotionIntoPose (ignores root lock).
  Public/AnimationBlueprintLibrary.h: GetNumFrames, GetSequenceLength,
    GetBonePoseForTime (deprecated since 5.2, retained as a labelled fallback).
No extract-root-motion Python method is assumed. Sampling needs no saved asset edits.
"""

import argparse
import csv
import math
import os
import sys

import unreal

DEFAULT_FOLDER = "/Game/CaV_Anims/Animations/ThirdPerson_Anims"
FIELDS = (
    "asset_path", "duration_s", "frames", "root_motion_enabled",
    "dx_forward_cm", "dy_side_cm", "dz_up_cm", "max_root_rise_cm",
    "max_root_rise_time_s", "skeleton", "sample_hz", "samples",
    "method", "pelvis_space", "pelvis_dx_cm", "pelvis_dy_cm",
    "pelvis_dz_cm", "max_pelvis_rise_cm", "max_pelvis_rise_time_s",
    "status", "note",
)


def short_error(exc):
    return " ".join(str(exc).split())[:240] or type(exc).__name__


def xyz(transform):
    vector = transform.translation
    result = (float(vector.x), float(vector.y), float(vector.z))
    if not all(math.isfinite(value) for value in result):
        raise ValueError("non-finite bone translation")
    return result


def sample_times(duration, frames, hz):
    # Include endpoint exactly (do not shorten by epsilon or wrap a looping clip).
    times = {0.0, duration}
    times.update(i / hz for i in range(1, int(math.ceil(duration * hz)))
                 if i / hz < duration)
    if frames > 0:
        times.update(duration * i / frames for i in range(1, frames))
    return sorted(times)


def motion_stats(times, positions):
    origin, end = positions[0], positions[-1]
    peak_index = max(range(len(positions)), key=lambda i: positions[i][2])
    return tuple(end[i] - origin[i] for i in range(3)) + (
        positions[peak_index][2] - origin[2], times[peak_index])


def modern_samples(sequence, times):
    extensions = unreal.AnimPoseExtensions
    options = unreal.AnimPoseEvaluationOptions()
    # These are TEMPORARY evaluation-struct properties, never asset properties.
    options.set_editor_property("evaluation_type", unreal.AnimDataEvalType.SOURCE)
    options.set_editor_property("should_retarget", False)
    options.set_editor_property("extract_root_motion", False)
    options.set_editor_property("incorporate_root_motion_into_pose", True)
    roots, pelvis = [], []
    has_pelvis = False
    for index, time in enumerate(times):
        pose = extensions.get_anim_pose_at_time(sequence, time, options)
        if not extensions.is_valid(pose):
            raise RuntimeError("invalid evaluated pose at %.6fs" % time)
        if index == 0:
            names = {str(name) for name in extensions.get_bone_names(pose)}
            if "root" not in names:
                raise ValueError("pose has no root bone")
            has_pelvis = "pelvis" in names
        roots.append(xyz(extensions.get_bone_pose(
            pose, "root", unreal.AnimPoseSpaces.WORLD)))
        if has_pelvis:
            pelvis.append(xyz(extensions.get_bone_pose(
                pose, "pelvis", unreal.AnimPoseSpaces.WORLD)))
    return roots, pelvis, "AnimPose_source", "component", ""


def legacy_samples(sequence, times):
    library = unreal.AnimationLibrary
    # Deprecated API returns local transforms; verify track names to avoid its
    # identity/ref-pose fallback being reported as measured zero root motion.
    names = {str(name) for name in library.get_animation_track_names(sequence)}
    if "root" not in names:
        raise ValueError("fallback: no animated root track")
    roots, pelvis = [], []
    for time in times:
        roots.append(xyz(library.get_bone_pose_for_time(sequence, "root", time, False)))
        if "pelvis" in names:
            pelvis.append(xyz(library.get_bone_pose_for_time(sequence, "pelvis", time, False)))
    return (roots, pelvis, "AnimationLibrary_legacy", "bone_local",
            "Legacy evaluation uses engine defaults; pelvis is LOCAL, not component space.")


def measure(sequence, hz):
    row = dict.fromkeys(FIELDS, "")
    row.update(asset_path=sequence.get_path_name(), sample_hz=hz, status="ok")
    try:
        library = unreal.AnimationLibrary
        duration = float(library.get_sequence_length(sequence))
        frames = int(library.get_num_frames(sequence))
        if not math.isfinite(duration) or duration < 0 or frames < 0:
            raise ValueError("invalid duration/frame count")
        row.update(duration_s=duration, frames=frames,
                   root_motion_enabled=bool(sequence.get_editor_property("enable_root_motion")))
        skeleton = sequence.get_editor_property("skeleton")
        row["skeleton"] = skeleton.get_path_name() if skeleton else ""
        times = sample_times(duration, frames, hz)
        try:
            roots, pelvis, method, pelvis_space, note = modern_samples(sequence, times)
        except Exception as modern_error:
            roots, pelvis, method, pelvis_space, note = legacy_samples(sequence, times)
            row["status"] = "fallback"
            note = "AnimPose: %s; %s" % (short_error(modern_error), note)
        row.update(method=method, pelvis_space=pelvis_space, samples=len(times), note=note)
        root_keys = ("dx_forward_cm", "dy_side_cm", "dz_up_cm",
                     "max_root_rise_cm", "max_root_rise_time_s")
        row.update(zip(root_keys, motion_stats(times, roots)))
        if pelvis:
            pelvis_keys = ("pelvis_dx_cm", "pelvis_dy_cm", "pelvis_dz_cm",
                           "max_pelvis_rise_cm", "max_pelvis_rise_time_s")
            row.update(zip(pelvis_keys, motion_stats(times, pelvis)))
        else:
            row["note"] = (note + " Pelvis unavailable; root measurements retained.").strip()
    except Exception as exc:
        row.update(status="error", note=short_error(exc))
    return row


def arguments(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", nargs="?", default=DEFAULT_FOLDER,
                        help="Recursive Unreal package folder, e.g. /Game/HealingAnimations/Animations")
    parser.add_argument("--sample-hz", type=float, default=30.0)
    args = parser.parse_args(argv)
    args.folder = args.folder.rstrip("/")
    if not args.folder.startswith("/Game/") or "." in args.folder or "\\" in args.folder:
        parser.error("folder must be an Unreal /Game/... package folder")
    if not math.isfinite(args.sample_hz) or not 1 <= args.sample_hz <= 240:
        parser.error("--sample-hz must be finite and between 1 and 240")
    return args


def main(argv=None):
    args = arguments(sys.argv[1:] if argv is None else argv)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    # Registry discovery only: this does not resave/import any package.
    registry.scan_paths_synchronous([args.folder])
    assets = sorted(registry.get_assets_by_path(args.folder, recursive=True),
                    key=lambda data: str(data.package_name))
    output = os.path.abspath(os.path.join(
        unreal.Paths.project_saved_dir(), "Measure", "anim_root_motion.csv"))
    os.makedirs(os.path.dirname(output), exist_ok=True)
    count = failures = fallbacks = 0
    with open(output, "w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        for data in assets:
            # Avoid loading non-animation assets; UE5 AssetClassPath is metadata.
            class_path = getattr(data, "asset_class_path", None)
            if class_path is not None and str(class_path.asset_name) != "AnimSequence":
                continue
            path = "%s.%s" % (data.package_name, data.asset_name)
            try:
                sequence = data.get_asset()
                if sequence is None:
                    raise RuntimeError("asset load returned None")
                if not isinstance(sequence, unreal.AnimSequence):
                    continue
                row = measure(sequence, args.sample_hz)
            except Exception as exc:
                row = dict.fromkeys(FIELDS, "")
                row.update(asset_path=path, status="error", note=short_error(exc))
            writer.writerow(row)
            count += 1
            failures += row["status"] == "error"
            fallbacks += row["status"] == "fallback"
            if row["status"] != "ok":
                print("[MeasureRoot] %s %s: %s" % (row["status"], path, row["note"]))
    print("[MeasureRoot] %d sequences, %d errors, %d fallbacks -> %s" %
          (count, failures, fallbacks, output))
    if count == 0:
        print("[MeasureRoot] WARNING: no AnimSequence assets found in %s" % args.folder)
    return failures


if __name__ == "__main__":
    # Per-asset failures are CSV rows; do not terminate the batch on one bad clip.
    main()
