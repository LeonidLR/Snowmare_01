"""The mantle / vault set (the game designer 2026-11-15: obstacle heights standardised to the clip heights): retarget, check and measure the
root-motion clips of the purchased "Climbing and Vaulting Animations" pack (DeadPixelLabs) that the runtime plays with root motion + Motion Warping.

The pack /Game/CaV_Anims is the game designer's (an untracked folder): it is only READ - nothing in it is modified or saved. Its third-person
clips (Animations/ThirdPerson_Anims/Climbing, Extras, Landing) sit on the pack's own UE4 mannequin (Demo/CharacterMesh/ThirdPersonMesh/SK_Mannequin,
UE4_Mannequin_Skeleton - the UE4 hierarchy, a different asset from the operatives'), so - exactly like the injured set
(Scripts/Editor/setup_injured_animation.py) and the prone crawl (setup_crawl_animation.py) - the clips are IK-retargeted with the M4 pack's
IK_UE4_Mannequin on BOTH sides (a transient retargeter, never saved; "Blend to Source" off) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/Mantle/Male/<pack clip name>
  female: SK_Female_soldier      -> /Game/Characters/Animations/Mantle/Female/<pack clip name>
Clip list (obstacle class in cm): low_vault 95 LowVault, RunLowVault, LowVaultLand; low_mantle 85 Low_Climb, RunLowClimb; high_mantle 140
HighClimb, RunHighClimb; tall_ledge 230 Standing_JumpClimbUpNEW, TallLedge_ClimbUpNEW, RunJumpClimbNEW, TallLand; very_tall 310 JumpClimb,
RunJumpClimb; high_fence_over 135 HighFenceVault, TallFence_Land. Each copy is re-keyed (the retarget leaves the source's key data behind).
Unlike the injured set these are ROOT-MOTION clips: the copies have EnableRootMotion on, root lock = the animation's first frame (the clip
starts at the origin of the motion-warped window; RefPose would add the mannequin's reference-pose offset), so the root's travel and rise are
what the runtime extracts. Measured per clip (component space, +Y = the body's forward, +X = its LEFT, root bone):
  length      seconds
  root        end displacement of the root (dz, forward) and the peak rise of the root over its first frame (cm) - source vs male vs female
              (flagged when a copy's rise differs from the source's by > 3 cm; the male / female scale difference is reported as a ratio)
  hands       hand_l / hand_r: time of the lowest height over the clip, and the first time a hand reaches the obstacle class height - 10 cm
              above the lower foot of the first frame (approximate contact on the ledge)
  on top      the first time both feet are above the class height - 5 cm
              (above the lower foot of the first frame; approximate "on top" time); none for landing clips
  armed       the pack clips are authored unarmed ("unarmed clip"): the hands hold nothing

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_MANTLE_DRYRUN=1: retarget in memory and measure only (nothing saved). CODEX_RESET_MANTLE_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_mantle_animation.json (+ .txt summary)
"""
import json
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_MANTLE_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_MANTLE_CLIPS") == "1"
PACK = "/Game/CaV_Anims/Animations/ThirdPerson_Anims"
PACK_MESH = "/Game/CaV_Anims/Demo/CharacterMesh/ThirdPersonMesh/SK_Mannequin"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Animations/Mantle/Male", "She": "/Game/Characters/Animations/Mantle/Female"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
WORLD = unreal.AnimPoseSpaces.WORLD
RATE = 30.0  # samples per second
RISE_TOLERANCE = 3.0
# clip -> obstacle class height in cm (None = a landing clip, no ledge)
CLIPS = {
    "LowVault": 95, "RunLowVault": 95, "LowVaultLand": None,
    "Low_Climb": 85, "RunLowClimb": 85,
    "HighClimb": 140, "RunHighClimb": 140,
    "Standing_JumpClimbUpNEW": 230, "TallLedge_ClimbUpNEW": 230, "RunJumpClimbNEW": 230, "TallLand": None,
    "JumpClimb": 310, "RunJumpClimb": 310,
    "HighFenceVault": 135, "TallFence_Land": None,
}
log = []
report = {"clips": {}, "bodies": {}, "missing": [], "root_motion": {"enable_root_motion": True, "root_lock": "AnimFirstFrame"}}


def say(text):
    log.append(text)
    unreal.log("setup_mantle_animation: " + text)


def load(path, quiet=False):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None and not quiet:
        say(f"MISSING {path}")
    return asset


def find_pack_paths():
    """clip name -> package path, from the third-person folders only (the first-person arms set shares the names with an FP_ prefix)."""
    found = {}
    for p in lib.list_assets(PACK, recursive=True, include_folder=False):
        pkg = p.split(".")[0]
        name = pkg.rsplit("/", 1)[1]
        if name in CLIPS:
            found[name] = pkg
    return found


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        options.set_editor_property("extract_root_motion", False)
    except Exception:  # noqa: BLE001 - not every version exposes it
        pass
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD)


def measure(clip, name, mesh=None):
    """Length, root displacement / rise, hand contact and on-top times of one clip."""
    height = CLIPS[name]
    length = clip.get_editor_property("sequence_length")
    n = max(2, int(length * RATE))
    times = [i * length / n for i in range(n)] + [length]
    frames = []
    for t in times:
        p = pose_at(clip, t, mesh)
        frames.append({b: bone(p, b).translation for b in ("root", "hand_l", "hand_r", "foot_l", "foot_r")})
    r0 = frames[0]["root"]
    # Some clips start mid-wall (HighClimb's root starts 151 cm above the clip origin), so heights are taken above the lower foot of frame 0.
    ground = min(frames[0]["foot_l"].z, frames[0]["foot_r"].z)
    out = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip), "class_height": height,
           "root_dz": round(frames[-1]["root"].z - r0.z, 1), "root_forward": round(frames[-1]["root"].y - r0.y, 1),
           "root_side": round(frames[-1]["root"].x - r0.x, 1),
           "root_rise": round(max(f["root"].z for f in frames) - r0.z, 1)}
    out["root_rise_t"] = round(times[max(range(len(frames)), key=lambda i: frames[i]["root"].z)], 2)
    for h in ("hand_l", "hand_r"):
        out[h + "_lowest_t"] = round(times[min(range(len(frames)), key=lambda i: frames[i][h].z)], 2)
        if height:
            hit = next((times[i] for i in range(len(frames)) if frames[i][h].z - ground >= height - 10.0), None)
            out[h + "_ledge_t"] = round(hit, 2) if hit is not None else None
    if height:
        top = next((times[i] for i in range(len(frames)) if min(frames[i]["foot_l"].z, frames[i]["foot_r"].z) - ground >= height - 5.0), None)
        out["on_top_t"] = round(top, 2) if top is not None else None
    out["armed"] = "unarmed clip"
    return out


def make_retargeter():
    """UE4 rig -> UE4 rig, transient (never saved): the template's op stack with both sides on IK_UE4_Mannequin."""
    template = load(TEMPLATE_RTG)
    ik = load(IK_UE4)
    if not template or not ik:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_MantlePack_UE4_TMP", "/Game/Characters/Animations/Mantle/_Tmp", template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik)
    try:
        ctrl.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, ik)
    except Exception as e:  # noqa: BLE001 - older signature
        say(f"assign_ik_rig_to_all_ops: {e}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    except Exception as e:  # noqa: BLE001
        say(f"auto_map_chains: {e}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE4 -> UE4 (IK_UE4_Mannequin both sides, ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off)")
    return rtg


def rebake(clip):
    """Re-key the clip from its own frames (drops the key data the retarget left behind); returns the largest pose error (cm)."""
    model = clip.get_editor_property("data_model_interface")
    bones = [str(b) for b in model.get_bone_track_names()]
    keys = model.get_number_of_keys()
    length = clip.get_editor_property("sequence_length")
    times = [0.0, length * 0.37, max(0.0, length - 0.02)]
    probe = ("head", "hand_l", "hand_r", "foot_l", "foot_r")

    def sample():
        out = []
        for t in times:
            p = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, t, unreal.AnimPoseEvaluationOptions())
            out += [unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation for b in probe]
        return out
    before = sample()
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
    return max((a - b).length() for a, b in zip(before, sample()))


def set_root_motion(asset):
    asset.set_editor_property("enable_root_motion", True)
    try:
        asset.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
    except Exception as e:  # noqa: BLE001
        say(f"root_motion_root_lock: {e}")


# --- The pack clips -------------------------------------------------------------------------------------------------------------------
paths = find_pack_paths()
report["missing"] = [n for n in CLIPS if n not in paths]
say(f"{len(paths)} / {len(CLIPS)} pack clips found{' (DRY RUN)' if DRY else ''}; missing: {report['missing'] or 'none'}")
source_mesh = load(PACK_MESH)
pack = {n: load(p) for n, p in sorted(paths.items())}
for name, clip in pack.items():
    if not clip:
        continue
    entry = measure(clip, name)
    entry["skeleton"] = clip.get_editor_property("skeleton").get_path_name()
    entry["root_motion_flag"] = bool(clip.get_editor_property("enable_root_motion"))
    report["clips"][name] = entry
    say(f"  {name}: {entry['length']} s, root dz {entry['root_dz']} fwd {entry['root_forward']} rise {entry['root_rise']} @ {entry['root_rise_t']} s")
if source_mesh:
    say(f"source mesh {source_mesh.get_name()}, skeleton {source_mesh.get_editor_property('skeleton').get_path_name()}")

# --- Retarget -------------------------------------------------------------------------------------------------------------------------
retargeter = make_retargeter()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
for gender in ("He", "She"):
    target_mesh = load(TARGET_MESH[gender])
    copies, todo = {}, []
    for name, path in sorted(paths.items()):
        dest = f"{OUT[gender]}/{name}"
        if lib.does_asset_exist(dest) and not RESET:
            copies[name] = lib.load_asset(dest)
            continue
        data = registry.get_asset_by_object_path(f"{path}.{name}")
        if data.is_valid():
            todo.append(data)
        else:
            say(f"  {name}: not in the pack")
    if todo and retargeter and source_mesh and target_mesh:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False, overwrite_existing_files=True)
        for data in result:
            asset = data.get_asset()
            if isinstance(asset, unreal.AnimSequence):
                err = rebake(asset)
                set_root_motion(asset)
                if err > 0.5:
                    say(f"  {data.asset_name}: re-key error {err:.2f} cm")
                copies[str(data.asset_name)] = asset
                if not DRY:
                    lib.save_loaded_asset(asset, False)
        say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}{' (in memory)' if DRY else ''}")
    body = {"mesh": TARGET_MESH[gender], "clips": {}}
    for name, clip in pack.items():
        new = copies.get(name)
        if not clip or not new:
            body["clips"][name] = {"missing": True}
            continue
        row = measure(new, name, target_mesh)
        row["root_motion_enabled"] = bool(new.get_editor_property("enable_root_motion"))
        src = report["clips"][name]
        row["rise_diff_cm"] = round(row["root_rise"] - src["root_rise"], 1)
        row["rise_ratio"] = round(row["root_rise"] / src["root_rise"], 3) if src["root_rise"] > 1.0 else None
        row["rise_flag"] = abs(row["rise_diff_cm"]) > RISE_TOLERANCE
        body["clips"][name] = row
        say(f"    {gender} {name}: {row['length']} s, dz {row['root_dz']} fwd {row['root_forward']} rise {row['root_rise']} (src {src['root_rise']}, "
            f"diff {row['rise_diff_cm']}, ratio {row['rise_ratio']}{', FLAG' if row['rise_flag'] else ''}), hands lowest "
            f"{row['hand_l_lowest_t']}/{row['hand_r_lowest_t']} ledge {row.get('hand_l_ledge_t')}/{row.get('hand_r_ledge_t')}, on top {row.get('on_top_t')}, "
            f"{row['armed']}, root motion {row['root_motion_enabled']}")
    report["bodies"][gender] = body
    say(f"{gender}: {len(copies)} / {len(paths)} copies; flagged rise > {RISE_TOLERANCE} cm: "
        f"{[n for n, r in body['clips'].items() if r.get('rise_flag')]}")
if retargeter:
    lib.delete_loaded_asset(retargeter)
say("DRY RUN: nothing saved" if DRY else "done")
os.makedirs(os.path.join(unreal.Paths.project_saved_dir(), "Logs"), exist_ok=True)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_mantle_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_mantle_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
