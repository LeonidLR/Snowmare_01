"""Fixes the STRETCHED Marksman (user bug 2026-10-11: elongated torso / limbs, walking and lying dead).

Root cause: the Marksman mesh (BP_Enemy_Marksman -> SKM_base_mesh_2_ue5) sits on the pack's UE5 skeleton
/Game/Biochemical_Monster_1/baze_mech/ue5/SK_base_mesh_ue5_1_Skeleton, whose 161 bones all kept the FBX-import default
translation retargeting mode "Animation". With that mode a clip's own bone translations (its bone LENGTHS) replace the
mesh's. The Marksman plays clips authored on other proportions - the monster pack's UE4 mannequin clips (walk, run,
idles, attacks), RifleAnims / Crawl packs (UE4), and since 4c75003 the Sniper pack's Manny death clips - so the body
takes the clip's bone lengths: the UE4 clips carry spine_02 / spine_03 of ~12 cm while this mesh has them at 0 cm (its
spine is spine_01 11 + spine_04 11 + spine_05 11 = 33 cm; the clip makes it 62 cm), the limbs come out 8-18 % long, and the
Sniper death clips (Manny proportions) shrink the lower arm by 24 %.

Fix (skeleton asset only, no BP / ABP / clip / mesh touched): the project's own UE4 mannequin skeletons already work this way
(pelvis + ik_* Animation, every other bone Skeleton), so the BM1 UE5 skeleton gets the same setup:
  root, ik_*, interaction, center_of_mass, *_end  -> Animation       (animated goals / root motion keep the clip's values)
  pelvis                                          -> AnimationScaled (the clip's hip height / motion, scaled to this mesh)
  every other bone (spine, limbs, fingers, ...)   -> Skeleton        (the mesh's own bone lengths; the clip only supplies rotations)
Measured read-only (dry run, 25 Marksman clips): torso length x1.56 / legs x1.18 / arms x1.18 of the mesh's before, x1.00 after.
CODEX_RETARGET_STYLE=manny copies Epic's mannequin setup instead (AnimationScaled almost everywhere): the Manny / Crawl / Rifle
clips come out right but the monster pack's UE4 clips stay x1.12 / x1.11 / x1.27 off (measured), so it is not the default.

Other characters are not touched: the operatives, Brute, Frostbitten, Cutter (UE4 mannequin skeletons: Skeleton mode on 59
bones, pelvis + ik_* Animation) and the Hound (own clips) are not affected - see docs/port/skeleton_audit.md section 6.

Run (a) with the MAIN project's editor CLOSED (commandlet; backs the skeleton up first, then saves):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
or (b) inside the open editor: Tools > Execute Python Script (the editor owns the asset, no file lock problem), then save all.
CODEX_RETARGET_DRYRUN=1 changes the asset IN MEMORY only (no backup, no save) - used to measure the effect read-only.
Result: Saved/Logs/fix_marksman_retargeting.txt (modes before / after, proportion check per clip).
Backup: Saved/Backups/marksman_retarget_<timestamp>/Content/... (restore = copy the .uasset back with the editor closed).
"""
import os
import shutil
import time

import unreal

DRY = os.environ.get("CODEX_RETARGET_DRYRUN") == "1"
STYLE = os.environ.get("CODEX_RETARGET_STYLE", "skeleton")  # "skeleton" (default) | "manny" (Epic mannequin, see the docstring)
TARGETS = ["/Game/Biochemical_Monster_1/baze_mech/ue5/SK_base_mesh_ue5_1_Skeleton"]
# Same fix is needed (and identical) the day these Manny-exact skeletons get a character on them:
#   /Game/Biochemical_Monster_2/baze_mech/ue5/SK_baze_mesh_ue5_Skeleton (unused Cutter-pack UE5 export)
#   /Game/Characters/Operatives/Explorer/Explorer/SkeletalMeshes/Explorer_Skeleton (no BP yet)
REFERENCE = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
MARKSMAN_MESH = "/Game/Biochemical_Monster_1/baze_mech/ue5/SKM_base_mesh_2_ue5"
ABP_CLASS = "/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman.ABP_Enemy_Marksman_C"
MODE = unreal.BoneTranslationRetargetingMode
LOCAL = unreal.AnimPoseSpaces.LOCAL
log = []


def say(text):
    log.append(text)
    unreal.log("fix_marksman_retargeting: " + text)


def bone_names(skeleton):
    return [str(n) for n in skeleton.get_reference_pose().get_bone_names()]


def read_modes(skeleton):
    tree = skeleton.get_editor_property("bone_tree")
    return {n: tree[i].get_editor_property("translation_retargeting_mode") for i, n in enumerate(bone_names(skeleton))}


def histogram(modes):
    hist = {}
    for m in modes.values():
        hist[m.name] = hist.get(m.name, 0) + 1
    return hist


def fallback_mode(name):
    return MODE.ANIMATION if (name.startswith("ik_") or name.endswith("_end")) else MODE.SKELETON


def target_modes(skeleton, reference_modes):
    if STYLE == "skeleton":
        return {n: (MODE.ANIMATION if reference_modes.get(n, fallback_mode(n)) == MODE.ANIMATION
                    else MODE.ANIMATION_SCALED if n == "pelvis" else MODE.SKELETON) for n in bone_names(skeleton)}
    return {n: reference_modes.get(n, fallback_mode(n)) for n in bone_names(skeleton)}


def apply_modes(skeleton, wanted):
    # BoneTree is read-only as a whole in Python and tree[i] hands out a copy: edit the copy, assign it back by index.
    names = bone_names(skeleton)
    skeleton.modify()
    tree = skeleton.get_editor_property("bone_tree")
    for i, n in enumerate(names):
        node = tree[i]
        node.set_editor_property("translation_retargeting_mode", wanted[n])
        tree[i] = node


def backup(skeleton):
    package = skeleton.get_path_name().split(".")[0]  # /Game/...
    relative = package.replace("/Game/", "Content/", 1) + ".uasset"
    source = os.path.join(unreal.SystemLibrary.get_project_directory(), relative)
    stamp = BACKUP_STAMP
    destination = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "Backups", "marksman_retarget_" + stamp, relative)
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copy2(source, destination)
    say("backup %s -> %s" % (source, destination))


def marksman_clips():
    """AnimSequences on the class defaults of ABP_Enemy_Marksman (walk / run / idle / aim / attack / crouch / prone / death)."""
    cdo = unreal.get_default_object(unreal.load_object(None, ABP_CLASS))
    clips = {}
    for prop in dir(cdo):
        if prop.startswith("_"):
            continue
        try:
            value = getattr(cdo, prop)
        except Exception:
            continue
        if callable(value):
            continue
        items = [value] if isinstance(value, unreal.AnimSequence) else (
            [x for x in value if isinstance(x, unreal.AnimSequence)]
            if hasattr(value, "__len__") and not isinstance(value, (str, bytes)) else [])
        for clip in items:
            clips[clip.get_name()] = (prop, clip)
    return clips


def pose_of(clip, mesh, time_s):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("should_retarget", True)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, time_s, options)


def chain(pose, bones):
    return sum(pose.get_bone_pose(b, LOCAL).translation.length() for b in bones)


# Bone chains whose summed local length is the body proportion (ratio 1.0 = the mesh's own length).
CHAINS = {
    "torso": ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02"],
    "leg": ["calf_l", "foot_l", "calf_r", "foot_r"],
    "arm": ["lowerarm_l", "hand_l", "lowerarm_r", "hand_r"],
}


def proportion_report(tag):
    """Per clip: the worst ratio (evaluated chain length / mesh reference chain length) over a few clip times."""
    mesh = unreal.load_asset(MARKSMAN_MESH)
    clips = marksman_clips()
    first = next(iter(clips.values()))[1]
    reference = pose_of(first, mesh, 0.0)
    ref_len = {k: sum(reference.get_ref_bone_pose(b, LOCAL).translation.length() for b in bones) for k, bones in CHAINS.items()}
    say("[%s] mesh reference chain lengths: %s" % (tag, {k: round(v, 1) for k, v in ref_len.items()}))
    worst = {}
    for name, (prop, clip) in sorted(clips.items()):
        length = clip.get_play_length()
        row = {}
        for k, bones in CHAINS.items():
            ratios = [chain(pose_of(clip, mesh, length * f), bones) / ref_len[k] for f in (0.0, 0.37, 0.8)]
            row[k] = max(ratios, key=lambda r: abs(r - 1.0))
            worst[k] = max(worst.get(k, 1.0), row[k], key=lambda r: abs(r - 1.0))
        say("[%s] %-28s %-26s torso x%.2f  leg x%.2f  arm x%.2f" % (tag, name, prop, row["torso"], row["leg"], row["arm"]))
    say("[%s] WORST ratios over all %d clips: torso x%.2f  leg x%.2f  arm x%.2f" % (
        tag, len(clips), worst.get("torso", 0), worst.get("leg", 0), worst.get("arm", 0)))
    return worst


BACKUP_STAMP = time.strftime("%Y%m%d_%H%M%S")
reference_skeleton = unreal.load_asset(REFERENCE)
reference_modes = read_modes(reference_skeleton)
say("reference %s modes %s" % (REFERENCE, histogram(reference_modes)))

before = proportion_report("before")
for path in TARGETS:
    skeleton = unreal.load_asset(path)
    old = read_modes(skeleton)
    wanted = target_modes(skeleton, reference_modes)
    say("%s: %d bones, modes %s -> %s" % (path, len(old), histogram(old), histogram(wanted)))
    if all(old[n] == wanted[n] for n in wanted):
        say("  already set, nothing to do")
        continue
    if not DRY:
        backup(skeleton)
    apply_modes(skeleton, wanted)
    check = read_modes(skeleton)
    if any(check[n] != wanted[n] for n in wanted):
        say("  ERROR: the bone tree did not take the new modes")
    elif DRY:
        say("  DRY RUN: modes applied in memory only (not saved)")
    elif unreal.EditorAssetLibrary.save_loaded_asset(skeleton, False):
        say("  saved")
    else:
        say("  ERROR: save failed (is the asset checked out / is another editor holding it?)")
after = proportion_report("after")

ok = all(abs(v - 1.0) <= 0.06 for v in after.values())
say("RESULT: %s (worst body-proportion error after the fix: %s, before: %s)" % (
    "PASS" if ok else "CHECK", {k: round(v, 2) for k, v in after.items()}, {k: round(v, 2) for k, v in before.items()}))
with open(os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "Logs", "fix_marksman_retargeting.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
