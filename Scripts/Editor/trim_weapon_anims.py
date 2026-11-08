"""Trims the weapon layer's long loops to one clean cycle (2026-11-09, the coordinator: the pistol set weighed ~105 MB). The Fab GASP loops run
2-4 cycles (walk 4 s, run 2.5 s) and the idles 9-10 s; the native layer plays every loop on one normalized phase, so one cycle is all it needs.

Per clip: the cycle = the frame P inside the one-cycle window (one gait cycle for the loops, a breath for the idles) whose pose matches frame 0
best - head / hands / feet relative to the pelvis - then the clip is cut to frames [0, P]; the same P is applied to every
copy of the clip (Source + the Male / Female retargets have the same frames). Clips already one cycle (best match at the end) are left. Only
Claude-owned copies are cut (/Game/Characters/Animations/...); the packs and the Lyra originals stay as they are. Compression stays the
project's ACL (checked).

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
CODEX_TRIM_DRYRUN=1: report only. Report: Saved/Logs/trim_weapon_anims.txt
"""
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_TRIM_DRYRUN") == "1"
WORLD = unreal.AnimPoseSpaces.WORLD
SETS = {
    "pistol": ("/Game/Characters/Animations/Pistol/Source", "/Game/Characters/Animations/Pistol/Male", "/Game/Characters/Animations/Pistol/Female"),  # Lyra copies: Male / Female only
    "shotgun": ("/Game/Characters/Animations/Shotgun/Male", "/Game/Characters/Animations/Shotgun/Female"),
}
BONES = ("head", "hand_l", "hand_r", "foot_l", "foot_r")
log = []


def say(text):
    log.append(text)
    unreal.log("trim_weapon_anims: " + text)


def folder_size(path):
    disk = path.replace("/Game/", unreal.Paths.project_content_dir())
    total = 0
    for root, _, files in os.walk(disk):
        total += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    return total / 1048576.0


def pose_points(clip, frame):
    p = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, unreal.AnimationLibrary.get_time_at_frame(clip, frame), unreal.AnimPoseEvaluationOptions())
    pelvis = unreal.AnimPoseExtensions.get_bone_pose(p, "pelvis", WORLD).translation
    return [unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation - pelvis for b in BONES]


def window(name, length):
    """Where ONE cycle can end: one gait cycle (two steps) for the locomotion loops - every direction of a set must hold the same number of
    cycles, the layer blends them on one normalized phase - a breath cycle for the idles."""
    if "Idle" in name or "idle" in name:
        return 1.5, min(6.0, length)
    if "Run" in name or "Sprint" in name or "jog" in name:
        return 0.45, min(0.9, length)
    return 0.75, min(1.4, length)


def find_cycle(clip, name):
    frames = unreal.AnimationLibrary.get_num_frames(clip)
    length = clip.get_editor_property("sequence_length")
    if frames < 4 or length <= 0.0:
        return None, 0.0
    fps = frames / length
    lo, hi = window(name, length)
    start = max(2, int(lo * fps))
    stop = min(frames, int(hi * fps))
    ref = pose_points(clip, 0)
    errors = {}
    for f in range(start, stop + 1):
        pts = pose_points(clip, f)
        errors[f] = max((a - b).length() for a, b in zip(ref, pts))
    if not errors:
        return None, 0.0
    best = min(errors, key=errors.get)
    return best, errors[best]


def rebake(clip):
    """Rewrites the clip's bone tracks from its own frames. The cut (and the IK retarget) leave the sequencer data model holding every old key
    outside the new range - the file does not shrink; re-keying keeps only the frames in use (walk F copy: 1.24 MB -> 0.41 MB, pose error
    < 0.001 cm). Returns the largest pose error (cm) at three sample times, measured on head / hands / feet."""
    model = clip.get_editor_property("data_model_interface")
    bones = [str(b) for b in model.get_bone_track_names()]
    keys = model.get_number_of_keys()
    length = clip.get_editor_property("sequence_length")
    times = [0.0, length * 0.37, max(0.0, length - 0.02)]

    def sample():
        out = []
        for t in times:
            p = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, t, unreal.AnimPoseEvaluationOptions())
            out += [unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation for b in BONES]
        return out
    before_pts = sample()
    tracks = {b: ([], [], []) for b in bones}
    for f in range(keys):
        for b, t in zip(bones, unreal.AnimationLibrary.get_bone_poses_for_frame(clip, bones, f, False)):
            tracks[b][0].append(unreal.Vector(t.translation.x, t.translation.y, t.translation.z))
            tracks[b][1].append(unreal.Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w))
            tracks[b][2].append(unreal.Vector(t.scale3d.x, t.scale3d.y, t.scale3d.z))
    ctrl = clip.controller
    ctrl.open_bracket(unreal.Text("rebake"), False)
    ctrl.remove_all_bone_tracks(False)
    for b in bones:
        ctrl.add_bone_track(b, False)
        ctrl.set_bone_track_keys(b, tracks[b][0], tracks[b][1], tracks[b][2], False)
    ctrl.close_bracket(False)
    return max((a - b).length() for a, b in zip(before_pts, sample()))


before = {k: sum(folder_size(p) for p in v) for k, v in SETS.items()}
for key, folders in SETS.items():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    names = sorted({str(d.asset_name) for folder in folders for d in registry.get_assets_by_path(folder, recursive=False)})
    say(f"== {key}: {len(names)} clips in {', '.join(folders)}")
    for name in names:
        home = next((f for f in folders if lib.does_asset_exist(f"{f}/{name}")), folders[0])  # the source clip, else the first copy
        ref = lib.load_asset(f"{home}/{name}")
        if not isinstance(ref, unreal.AnimSequence):
            continue
        frames = unreal.AnimationLibrary.get_num_frames(ref)
        length = ref.get_editor_property("sequence_length")
        if length < 1.6 and "Idle" not in name and "idle" not in name:
            continue  # short clips (Lyra walks, shotgun loops, fire / reload / equip): already one cycle or a one-shot
        if any(t in name for t in ("Fire", "Reload", "Equip", "fire", "reload")):
            continue
        cycle, err = find_cycle(ref, name)
        if cycle is None or cycle >= frames - 2:
            say(f"  {name}: {frames} frames ({length:.2f} s) - already one cycle")
            continue
        fps = frames / length
        say(f"  {name}: {frames} frames ({length:.2f} s) -> {cycle} frames ({cycle / fps:.2f} s), seam {err:.1f} cm")
        if DRY:
            continue
        for folder in folders:
            path = f"{folder}/{name}"
            clip = lib.load_asset(path) if lib.does_asset_exist(path) else None
            if not isinstance(clip, unreal.AnimSequence):
                continue
            n = unreal.AnimationLibrary.get_num_frames(clip)
            controller = clip.controller
            controller.resize_number_of_frames(unreal.FrameNumber(cycle), unreal.FrameNumber(cycle), unreal.FrameNumber(n), False)
            if unreal.AnimationLibrary.get_bone_compression_settings(clip) is None:
                say(f"    {path}: no bone compression settings")
            lib.save_loaded_asset(clip, False)
            say(f"    {path}: {n} -> {unreal.AnimationLibrary.get_num_frames(clip)} frames")
# Re-key every copy (the cut ones and the untouched ones: the retarget leaves the old keys behind too).
if not DRY:
    for key, folders in SETS.items():
        for folder in folders:
            for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(folder, recursive=False):
                clip = data.get_asset()
                if not isinstance(clip, unreal.AnimSequence) or clip.get_editor_property("additive_anim_type") != unreal.AdditiveAnimationType.AAT_NONE:
                    continue  # additive clips (the pistol's recoil) keep their data as it is
                err = rebake(clip)
                if err > 0.5:
                    say(f"  {folder}/{data.asset_name}: re-key error {err:.2f} cm - NOT saved")
                    continue
                lib.save_loaded_asset(clip, False)
after = {k: sum(folder_size(p) for p in v) for k, v in SETS.items()}
for key in SETS:
    say(f"{key}: {before[key]:.1f} MB -> {after[key]:.1f} MB")
say("DRY RUN: nothing changed" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "trim_weapon_anims.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
