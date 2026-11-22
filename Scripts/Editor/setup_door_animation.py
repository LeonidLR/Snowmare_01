"""Door clips for the openable doors R2 (designer 2026-11-22): the designer's pack /Game/Free_Interaction_Animation/Animations/Door.

  anim_OpenDoor_L / _R           an unlocked door opened by hand
  anim_OpenDoor_Kick_L / _R      a kick (the game alternates the legs between attempts)
  anim_OpenDoor_Push_L / _R      the forced push-open of a door that WAS locked (after the key or a shot lock)

Skeleton check: the clips sit on the pack's own UE5 mannequin skeleton (/Game/Free_Interaction_Animation/Demo/Characters/Mannequins/Meshes/
SK_Mannequin; strings of the .uasset, confirmed here by the loaded clips). Our operatives are on the UE4 mannequin hierarchy
(UE4_Mannequin_Skeleton: SK_Post_Apo_Survivor_a; the Female_Soldier body plays the same copies in the game) -> IK retarget like the cold
idles / storm walk (setup_operative_storm_animation.py): the M4 pack's RTG_UE5Manny_UE4Manny with its "Blend to Source" op off IN MEMORY
(never saved). The PMC operator (SK_PmcOperator_Skeleton) has the pack's UE5 hierarchy (+ root_inner) -> its montages use the pack clips
by bone name (as the shotgun / pistol pack clips, docs/systems/animation-setup.md; Montage_Play does not check compatible skeletons).

Writes (nothing in the pack folder, no skeleton / ABP / engine asset saved):
  /Game/Animations/Door/UE4/anim_OpenDoor_*         retargeted copies (root motion off: the clips play in place)
  /Game/Animations/Door/UE4/AM_Door_{Open,Kick,Push}_{L,R}   montages, slot FullBody (ABP_Operative's full-body slot)
  /Game/Animations/Door/Pmc/AM_Door_{Open,Kick,Push}_{L,R}   montages of the pack clips themselves (DEPEND on the untracked pack)
/Game/Animations is untracked (designer packs): these assets are derived from the untracked pack and are not committed.
Measures per clip the CONTACT time (hand / foot furthest forward = on the leaf / the kick lands) -> doors.json montage_contact_s
(the averages are printed; the game uses them to time the leaf), the clip length, root motion and the pelvis travel.

Run with every editor of this project closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
CODEX_DOOR_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_DOOR_CLIPS=1: redo existing copies / montages.
Result: Saved/Logs/setup_door_animation.txt (+ setup_door_animation.json with the measured contact times).
"""
import json
import os

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DRY = os.environ.get("CODEX_DOOR_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_DOOR_CLIPS") == "1"
PACK = "/Game/Free_Interaction_Animation/Animations/Door"
PACK_MESH = "/Game/Free_Interaction_Animation/Demo/Characters/Mannequins/Meshes/SKM_Manny_Simple"
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
TARGET_MESH = "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a"
PMC_MESH = "/Game/Characters/Enemies/PMC/Operator/SKM_PmcOperator"
OUT_UE4 = "/Game/Animations/Door/UE4"
OUT_PMC = "/Game/Animations/Door/Pmc"
FULL_BODY_SLOT = "FullBody"
BLEND_IN, BLEND_OUT = 0.2, 0.25
# clip key -> (pack clip, montage name, the bone that touches the leaf)
CLIPS = {
    "open_l": ("anim_OpenDoor_L", "AM_Door_Open_L", "hand_l"),
    "open_r": ("anim_OpenDoor_R", "AM_Door_Open_R", "hand_r"),
    "kick_l": ("anim_OpenDoor_Kick_L", "AM_Door_Kick_L", "foot_l"),
    "kick_r": ("anim_OpenDoor_Kick_R", "AM_Door_Kick_R", "foot_r"),
    "push_l": ("anim_OpenDoor_Push_L", "AM_Door_Push_L", "hand_l"),
    "push_r": ("anim_OpenDoor_Push_R", "AM_Door_Push_R", "hand_r"),
}
WORLD = unreal.AnimPoseSpaces.WORLD
log = []


def say(text):
    log.append(text)
    unreal.log("setup_door_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), options)


def contact(clip, bone, mesh=None, forward="y", fps=30.0):
    """The first time the bone reaches 90 % of its furthest forward reach (vs its start, component space), the pelvis travel, the reach."""
    length = clip.get_editor_property("sequence_length")
    steps = max(2, int(length * fps))
    g = lambda p, b: unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation  # noqa: E731
    f = lambda v: getattr(v, forward)  # noqa: E731
    poses = [(length * i / steps, pose_at(clip, length * i / steps, mesh)) for i in range(steps + 1)]
    start = f(g(poses[0][1], bone))
    reach = [(t, f(g(p, bone)) - start) for t, p in poses]
    best = max(r for _, r in reach)
    first = next((t for t, r in reach if r >= 0.9 * best), 0.0) if best > 1.0 else length * 0.5
    pelvis = g(poses[-1][1], "pelvis") - g(poses[0][1], "pelvis")
    return first, best, pelvis, length


def make_montage(dest, name, clip):
    path = f"{dest}/{name}"
    if library.does_asset_exist(path) and RESET and not DRY:
        library.delete_asset(path)
    if library.does_asset_exist(path):
        montage = library.load_asset(path)
        state = "kept"
    else:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", clip)
        montage = tools.create_asset(name, dest, unreal.AnimMontage, factory)
        state = "created"
    unreal.OperativeAnimGraphLibrary.set_montage_slot(montage, FULL_BODY_SLOT)
    montage.set_editor_property("enable_auto_blend_out", True)
    blend_in = montage.get_editor_property("blend_in")
    blend_in.set_editor_property("blend_time", BLEND_IN)
    montage.set_editor_property("blend_in", blend_in)
    blend_out = montage.get_editor_property("blend_out")
    blend_out.set_editor_property("blend_time", BLEND_OUT)
    montage.set_editor_property("blend_out", blend_out)
    if not DRY:
        library.save_loaded_asset(montage, False)
    say(f"  {path}: {state}, slot {FULL_BODY_SLOT}, {montage.get_editor_property('sequence_length'):.2f} s, "
        f"skeleton {montage.get_editor_property('skeleton').get_name()}")
    return montage


pack = {}
for key, (clip_name, _, _) in CLIPS.items():
    clip = load(f"{PACK}/{clip_name}")
    if clip:
        pack[key] = clip
        say(f"pack {clip_name}: skeleton {clip.get_editor_property('skeleton').get_path_name()}, "
            f"{clip.get_editor_property('sequence_length'):.2f} s, root motion {clip.get_editor_property('enable_root_motion')}")
source_mesh, target_mesh, pmc_mesh = load(PACK_MESH), load(TARGET_MESH), load(PMC_MESH)
if pmc_mesh:
    say(f"PMC body skeleton {pmc_mesh.get_editor_property('skeleton').get_name()}")
if target_mesh:
    say(f"operative body skeleton {target_mesh.get_editor_property('skeleton').get_name()}")

# 1. UE4 copies (IK retarget, Blend to Source off in memory).
retargeter = load(RETARGETER)
copies = {}
todo = []
for key, (clip_name, _, _) in CLIPS.items():
    dest = f"{OUT_UE4}/{clip_name}"
    if library.does_asset_exist(dest) and not RESET:
        copies[key] = library.load_asset(dest)
    elif key in pack:
        todo.append(key)
if todo and retargeter and source_mesh and target_mesh:
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    index = controller.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        controller.set_retarget_op_enabled(index, False)
        say("RTG_UE5Manny_UE4Manny: 'Blend to Source' off in memory for this batch (asset not saved)")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    by_pack_name = {str(d.asset_name): d for d in registry.get_assets_by_path(PACK, recursive=False)}
    datas = [by_pack_name[CLIPS[k][0]] for k in todo if CLIPS[k][0] in by_pack_name]
    result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(datas, source_mesh, target_mesh, retargeter, target_path=OUT_UE4,
                                                                    include_referenced_assets=False, overwrite_existing_files=True)
    by_name = {str(d.asset_name): d.get_asset() for d in result}
    for key in todo:
        asset = by_name.get(CLIPS[key][0])
        if isinstance(asset, unreal.AnimSequence):
            asset.set_editor_property("enable_root_motion", False)
            asset.set_editor_property("force_root_lock", True)
            copies[key] = asset
            if not DRY:
                library.save_loaded_asset(asset, False)
    say(f"retargeted {len(todo)} clip(s) onto {target_mesh.get_name()} -> {OUT_UE4}{' (in memory)' if DRY else ''}")

# 2. Measurements + montages.
contacts = {"open": [], "kick": [], "push": []}
report = {"clips": {}}
for key, (clip_name, montage_name, bone) in CLIPS.items():
    kind = key.split("_")[0]
    if key in pack:
        t, reach, pelvis, length = contact(pack[key], bone)
        contacts[kind].append(t)
        report["clips"][key] = {"contact_s": round(t, 3), "reach_cm": round(reach, 1), "length_s": round(length, 3),
                                "pelvis_travel_cm": [round(pelvis.x, 1), round(pelvis.y, 1), round(pelvis.z, 1)]}
        say(f"{clip_name}: {bone} on the leaf at {t:.2f} s (reach {reach:.0f} cm) of {length:.2f} s; pelvis travel "
            f"({pelvis.x:.0f}, {pelvis.y:.0f}, {pelvis.z:.0f}) cm")
        make_montage(OUT_PMC, montage_name, pack[key])
    if key in copies:
        if target_mesh:
            t2, reach2, _, _ = contact(copies[key], bone, target_mesh)
            say(f"  retargeted copy: {bone} on the leaf at {t2:.2f} s (reach {reach2:.0f} cm)")
        make_montage(OUT_UE4, montage_name, copies[key])
report["montage_contact_s"] = {k: round(sum(v) / len(v), 2) for k, v in contacts.items() if v}
say(f"doors.json montage_contact_s (measured): {json.dumps(report['montage_contact_s'])}")
say("DRY RUN: nothing saved" if DRY else "done")
logs = os.path.join(unreal.Paths.project_saved_dir(), "Logs")
os.makedirs(logs, exist_ok=True)
with open(os.path.join(logs, "setup_door_animation.txt"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(log) + "\n")
with open(os.path.join(logs, "setup_door_animation.json"), "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
