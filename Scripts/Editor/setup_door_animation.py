"""Door clips for the openable doors R2 (designer 2026-11-22): the designer's pack /Game/Free_Interaction_Animation/Animations/Door.

  anim_OpenDoor_L / _R           an unlocked door opened by hand
  anim_OpenDoor_Kick_L / _R      a kick (the game alternates the legs between attempts)
  anim_OpenDoor_Push_L / _R      the forced push-open of a door that WAS locked (after the key or a shot lock)

Skeleton check: the clips sit on the pack's own UE5 mannequin skeleton (/Game/Free_Interaction_Animation/Demo/Characters/Mannequins/Meshes/
SK_Mannequin; strings of the .uasset, confirmed here by the loaded clips). Our operatives are on the UE4 mannequin hierarchy
(UE4_Mannequin_Skeleton: SK_Post_Apo_Survivor_a; the Female_Soldier body plays the same copies in the game) -> IK retarget like the cold
idles / storm walk (setup_operative_storm_animation.py): the M4 pack's RTG_UE5Manny_UE4Manny with its "Blend to Source" op off IN MEMORY
(never saved). The PMC operator (SK_PmcOperator_Skeleton) has the pack's UE5 hierarchy (+ root_inner, identity between root and pelvis).
Played by bone name (the first R2 set: copies of the pack clips on the pack skeleton) the clips lost their root motion - the PMC capsule did
not move (0 cm). Designer 2026-11-22 "door animations for the PMC": the PMC set is IK-RETARGETED onto SKM_PmcOperator like the launcher set
(setup_launcher_animation.py body "Pmc"): a transient retargeter (the M4 pack's RTG_UE4Manny_UE5Manny op stack, IK_Mannequin on BOTH sides -
the pack and the PMC body are UE5 Manny hierarchies; auto-mapped chains, "Blend to Source" off; never saved, deleted at the end), then the
same root motion bake as the operatives' copies. Old PMC copies still on the pack skeleton are replaced (with their montages) by a re-run.

Writes (nothing in the pack folder, no skeleton / ABP / engine asset saved):
  /Game/Animations/Door/UE4/anim_OpenDoor_*         retargeted copies (root motion off: the clips play in place)
  /Game/Animations/Door/UE4/AM_Door_{Open,Kick,Push}_{L,R}   montages, slot FullBody (ABP_Operative's full-body slot)
  /Game/Animations/Door/Pmc/anim_OpenDoor_*         copies retargeted onto SKM_PmcOperator (skeleton SK_PmcOperator_Skeleton)
  /Game/Animations/Door/Pmc/AM_Door_{Open,Kick,Push}_{L,R}   montages of those copies, slot FullBody
  /Game/Animations/Door/_Tmp/RTG_DoorPack_Pmc_TMP    the transient retargeter (deleted again at the end of the run)
/Game/Animations is untracked (designer packs): these assets are derived from the untracked pack and are not committed.
ROOT MOTION (designer 2026-11-22, option a): the pack clips walk the pelvis forward with no root motion (open 74, kick 104, push 195 cm) -
in place they slid and snapped back. The kick / push clips carry it on their root bone already (only root motion was off); the open clips
move the pelvis alone. Every copy whose root stands still gets the pelvis's smoothed horizontal travel baked into the root track (the pelvis keeps
its height and its sway - the residual about the smoothed path), root motion on (root lock: first frame): the montage drives the capsule.
The PMC montages use the baked retargeted copies (/Game/Animations/Door/Pmc/anim_OpenDoor_*; the pack itself is never written); the
PMC body's root_inner track (identity) is counted into the pelvis travel, its rotation is checked (logged when it turns).
Measures per clip the CONTACT time (hand / foot furthest forward = on the leaf / the kick lands) -> doors.json montage_contact_s
(the averages are printed; the game uses them to time the leaf), the clip length, root motion and the pelvis travel.

Run with every editor of this project closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
CODEX_DOOR_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_DOOR_CLIPS=1: redo existing copies / montages.
CODEX_DOOR_BODIES=UE4,Pmc (default both) limits the sets made / measured.
Result: Saved/Logs/setup_door_animation.txt (+ setup_door_animation.json with the measured contact times).
"""
import json
import math
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
PMC_SKELETON = "SK_PmcOperator_Skeleton"
IK_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Rigs/IK_Mannequin"
TEMPLATE_RTG_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE4Manny_UE5Manny"
TMP = "/Game/Animations/Door/_Tmp"
BODIES = [b.strip() for b in os.environ.get("CODEX_DOOR_BODIES", "UE4,Pmc").split(",") if b.strip() in ("UE4", "Pmc")]
OUT_UE4 = "/Game/Animations/Door/UE4"
OUT_PMC = "/Game/Animations/Door/Pmc"
FULL_BODY_SLOT = "FullBody"
BLEND_IN, BLEND_OUT = 0.1, 0.1  # short: a montage blend scales its root motion (0.2 / 0.25 lost ~30 % of the travel)
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
    options.set_editor_property("extract_root_motion", False)  # the baked root travel stays in the pose (contact / reach unchanged)
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


def root_of(clip, frame):
    return unreal.AnimationLibrary.get_bone_poses_for_frame(clip, ["root"], frame, False)[0].translation


def bake_root_motion(clip, window_s=0.3):
    """The pelvis's smoothed horizontal travel (root space X / Y, a moving average over window_s) moves onto the root track; the pelvis
    keeps its height and the sway about the smoothed path. Root motion on, root lock = first frame. Returns (end travel, frames, skipped)."""
    model = clip.get_editor_property("data_model_interface")
    keys = model.get_number_of_keys()
    first, last = root_of(clip, 0), root_of(clip, keys - 1)
    moved = ((last.x - first.x) ** 2 + (last.y - first.y) ** 2) ** 0.5
    if moved > 5.0:
        if not DRY:
            clip.set_editor_property("enable_root_motion", True)
            clip.set_editor_property("force_root_lock", False)
            clip.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
        return moved, keys, True  # the root moves already (baked before / by the retarget): a re-run must not zero it
    bones = [str(b) for b in model.get_bone_track_names()]
    if "root" not in bones:
        bones = ["root"] + bones
    tracks = {b: ([], [], []) for b in bones}
    for f in range(keys):
        for b, t in zip(bones, unreal.AnimationLibrary.get_bone_poses_for_frame(clip, bones, f, False)):
            tracks[b][0].append(unreal.Vector(t.translation.x, t.translation.y, t.translation.z))
            tracks[b][1].append(unreal.Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w))
            tracks[b][2].append(unreal.Vector(t.scale3d.x, t.scale3d.y, t.scale3d.z))
    length = clip.get_editor_property("sequence_length")
    half = max(1, int(round(window_s * 0.5 * (keys - 1) / max(length, 0.001))))
    pel = list(tracks["pelvis"][0])
    # The PMC body: root -> root_inner -> pelvis. root_inner is identity in its reference pose; its translation (if the retarget keys any)
    # counts into the travel, and the pelvis offset below stays valid while root_inner does not turn (checked, logged).
    inner = tracks["root_inner"][0] if "root_inner" in tracks else [unreal.Vector(0.0, 0.0, 0.0)] * keys
    if "root_inner" in tracks:
        turn = max(2.0 * math.degrees(math.acos(min(1.0, abs(q.w)))) for q in tracks["root_inner"][1])
        if turn > 1.0:
            say(f"  {clip.get_name()}: root_inner turns {turn:.1f} deg - the pelvis offset is taken in its parent's axes")
    raw = [(pel[f].x + inner[f].x - pel[0].x - inner[0].x, pel[f].y + inner[f].y - pel[0].y - inner[0].y) for f in range(keys)]
    smooth = []
    for f in range(keys):
        lo, hi = max(0, f - half), min(keys - 1, f + half)
        n = hi - lo + 1
        smooth.append((sum(raw[i][0] for i in range(lo, hi + 1)) / n, sum(raw[i][1] for i in range(lo, hi + 1)) / n))
    # the ends: the root starts at 0 and ends at the full travel (no offset left in the pelvis at the first / last frame)
    s0, s1 = smooth[0], smooth[-1]
    for f in range(keys):
        a = f / float(max(1, keys - 1))
        smooth[f] = (smooth[f][0] - s0[0] * (1 - a) + (raw[-1][0] - s1[0]) * a, smooth[f][1] - s0[1] * (1 - a) + (raw[-1][1] - s1[1]) * a)
    root0 = tracks["root"][0][0]
    for f in range(keys):
        dx, dy = smooth[f]
        tracks["root"][0][f] = unreal.Vector(root0.x + dx, root0.y + dy, root0.z)
        p = pel[f]
        tracks["pelvis"][0][f] = unreal.Vector(p.x - dx, p.y - dy, p.z)
    if not DRY:
        ctrl = clip.controller
        ctrl.open_bracket(unreal.Text("door root motion"), False)
        ctrl.remove_all_bone_tracks(False)
        for b in bones:
            ctrl.add_bone_track(b, False)
            ctrl.set_bone_track_keys(b, tracks[b][0], tracks[b][1], tracks[b][2], False)
        ctrl.close_bracket(False)
        clip.set_editor_property("enable_root_motion", True)
        clip.set_editor_property("force_root_lock", False)
        try:
            clip.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
        except Exception as error:  # noqa: BLE001
            say(f"root_motion_root_lock: {error}")
    return (raw[-1][0] ** 2 + raw[-1][1] ** 2) ** 0.5, keys, False


def rekey(clip):
    """Re-key the clip from its own frames (drops the key data an IK retarget leaves behind, as setup_launcher_animation.py's rebake)."""
    model = clip.get_editor_property("data_model_interface")
    bones = [str(b) for b in model.get_bone_track_names()]
    keys = model.get_number_of_keys()
    tracks = {b: ([], [], []) for b in bones}
    for f in range(keys):
        for b, t in zip(bones, unreal.AnimationLibrary.get_bone_poses_for_frame(clip, bones, f, False)):
            tracks[b][0].append(unreal.Vector(t.translation.x, t.translation.y, t.translation.z))
            tracks[b][1].append(unreal.Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w))
            tracks[b][2].append(unreal.Vector(t.scale3d.x, t.scale3d.y, t.scale3d.z))
    ctrl = clip.controller
    ctrl.open_bracket(unreal.Text("door rekey"), False)
    ctrl.remove_all_bone_tracks(False)
    for b in bones:
        ctrl.add_bone_track(b, False)
        ctrl.set_bone_track_keys(b, tracks[b][0], tracks[b][1], tracks[b][2], False)
    ctrl.close_bracket(False)


def make_pmc_retargeter():
    """UE5 Manny (the pack) -> UE5 Manny (the PMC body), transient: RTG_UE4Manny_UE5Manny's op stack, IK_Mannequin on both sides."""
    template, ik = load(TEMPLATE_RTG_UE5), load(IK_UE5)
    if not template or not ik:
        return None
    if library.does_asset_exist(f"{TMP}/RTG_DoorPack_Pmc_TMP"):
        library.delete_asset(f"{TMP}/RTG_DoorPack_Pmc_TMP")
    rtg = tools.duplicate_asset("RTG_DoorPack_Pmc_TMP", TMP, template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik)
    for side in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            ctrl.assign_ik_rig_to_all_ops(side, ik)
        except Exception as error:  # noqa: BLE001 - older signature / source not per op
            say(f"assign_ik_rig_to_all_ops({side}): {error}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    except Exception as error:  # noqa: BLE001
        say(f"auto_map_chains: {error}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE5 -> UE5 (IK_Mannequin both sides), ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off")
    return rtg


def make_montage(dest, name, clip, force=False):
    path = f"{dest}/{name}"
    if library.does_asset_exist(path) and (RESET or force) and not DRY:
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
    if "UE4" not in BODIES:
        break
    if library.does_asset_exist(dest) and not RESET:
        copies[key] = library.load_asset(dest)
    elif key in pack:
        todo.append(key)
if todo and retargeter and source_mesh and target_mesh and "UE4" in BODIES:
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

# 1b. PMC copies: IK-retargeted onto SKM_PmcOperator (a copy still on the pack skeleton - the first R2 set - is redone).
pmc_copies = {}
pmc_todo = []
for key, (clip_name, montage_name, _) in CLIPS.items():
    if "Pmc" not in BODIES:
        break
    dest = f"{OUT_PMC}/{clip_name}"
    existing = library.load_asset(dest) if library.does_asset_exist(dest) else None
    on_body = existing is not None and existing.get_editor_property("skeleton").get_name() == PMC_SKELETON
    if existing is not None and on_body and not RESET:
        pmc_copies[key] = existing
    elif key in pack:
        if existing is not None:
            say(f"PMC {clip_name}: on {existing.get_editor_property('skeleton').get_name()} - redone by the retarget")
        pmc_todo.append(key)
pmc_retargeter = make_pmc_retargeter() if pmc_todo and source_mesh and pmc_mesh else None
if pmc_todo and pmc_retargeter:
    if not DRY:
        for key in pmc_todo:
            # the montage first (it references the old copy), then the copy itself
            for path in (f"{OUT_PMC}/{CLIPS[key][1]}", f"{OUT_PMC}/{CLIPS[key][0]}"):
                if library.does_asset_exist(path):
                    library.delete_asset(path)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    by_pack_name = {str(d.asset_name): d for d in registry.get_assets_by_path(PACK, recursive=False)}
    datas = [by_pack_name[CLIPS[k][0]] for k in pmc_todo if CLIPS[k][0] in by_pack_name]
    result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(datas, source_mesh, pmc_mesh, pmc_retargeter, target_path=OUT_PMC,
                                                                    include_referenced_assets=False, overwrite_existing_files=True)
    by_name = {str(d.asset_name): d.get_asset() for d in result}
    for key in pmc_todo:
        asset = by_name.get(CLIPS[key][0])
        if isinstance(asset, unreal.AnimSequence):
            asset.set_editor_property("enable_root_motion", False)
            asset.set_editor_property("force_root_lock", True)
            rekey(asset)
            pmc_copies[key] = asset
            say(f"PMC {CLIPS[key][0]}: retargeted onto {pmc_mesh.get_name()} (skeleton {asset.get_editor_property('skeleton').get_name()})")
            if not DRY:
                library.save_loaded_asset(asset, False)
        else:
            say(f"PMC {CLIPS[key][0]}: the retarget gave no clip")
    say(f"retargeted {len(pmc_todo)} clip(s) onto {pmc_mesh.get_name()} -> {OUT_PMC}{' (in memory)' if DRY else ''}")

# 2. Root motion, measurements, montages.
contacts = {"open": [], "kick": [], "push": []}
roots = {"open": [], "kick": [], "push": []}
at_contact = {"open": [], "kick": [], "push": []}
pmc_roots = {"open": [], "kick": [], "push": []}
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
    for label, dest, clip, mesh in (("UE4", OUT_UE4, copies.get(key), target_mesh), ("Pmc", OUT_PMC, pmc_copies.get(key), pmc_mesh)):
        if not clip:
            continue
        travel, frames, skipped = bake_root_motion(clip)
        if not DRY:
            library.save_loaded_asset(clip, False)
        keys = clip.get_editor_property("data_model_interface").get_number_of_keys()
        length = clip.get_editor_property("sequence_length")
        tc, reach2, _, _ = contact(clip, bone, mesh)
        fc = min(keys - 1, int(round(tc / max(length, 0.001) * (keys - 1))))
        r0, rc, r1 = root_of(clip, 0), root_of(clip, fc), root_of(clip, keys - 1)
        end_cm = ((r1.x - r0.x) ** 2 + (r1.y - r0.y) ** 2) ** 0.5
        contact_cm = ((rc.x - r0.x) ** 2 + (rc.y - r0.y) ** 2) ** 0.5
        if label == "UE4":
            roots[kind].append(end_cm)
            at_contact[kind].append(contact_cm)
        else:
            pmc_roots[kind].append(end_cm)
        report["clips"].setdefault(key, {})[label] = {"root_end_cm": round(end_cm, 1), "root_at_contact_cm": round(contact_cm, 1),
                                                      "root_end_xy": [round(r1.x - r0.x, 1), round(r1.y - r0.y, 1)], "contact_s": round(tc, 3),
                                                      "baked_before": skipped, "frames": frames}
        say(f"  {label} {clip_name}: root motion {'kept (baked before)' if skipped else 'baked'} - root travel {end_cm:.0f} cm "
            f"(x {r1.x - r0.x:.0f}, y {r1.y - r0.y:.0f}), {contact_cm:.0f} cm at the contact {tc:.2f} s, {frames} frames, root motion "
            f"{clip.get_editor_property('enable_root_motion')}")
        make_montage(dest, montage_name, clip, force=True)  # the blend times change with the script: always remade
report["montage_contact_s"] = {k: round(sum(v) / len(v), 2) for k, v in contacts.items() if v}
report["montage_root_cm"] = {k: round(sum(v) / len(v)) for k, v in roots.items() if v}
report["montage_root_at_contact_cm"] = {k: round(sum(v) / len(v)) for k, v in at_contact.items() if v}
report["pmc_root_cm"] = {k: round(sum(v) / len(v)) for k, v in pmc_roots.items() if v}
say(f"doors.json montage_contact_s (measured): {json.dumps(report['montage_contact_s'])}")
say(f"doors.json montage_root_cm (UE4 copies): {json.dumps(report['montage_root_cm'])}; root travel at the contact: "
    f"{json.dumps(report['montage_root_at_contact_cm'])}")
say(f"PMC copies root travel (vs the UE4 copies' montage_root_cm): {json.dumps(report['pmc_root_cm'])}")
if pmc_retargeter:
    library.delete_loaded_asset(pmc_retargeter)
    if library.does_directory_exist(TMP):
        library.delete_directory(TMP)
say("DRY RUN: nothing saved" if DRY else "done")
logs = os.path.join(unreal.Paths.project_saved_dir(), "Logs")
os.makedirs(logs, exist_ok=True)
with open(os.path.join(logs, "setup_door_animation.txt"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(log) + "\n")
with open(os.path.join(logs, "setup_door_animation.json"), "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
