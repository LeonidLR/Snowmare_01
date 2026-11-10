"""The RPG-7 launcher set (the game designer's launcher pack, 2026-11-09, feat/rpg7-launcher-pack): retarget, check and compress every clip the
weapon layer uses.

The pack /Game/Launcher_Animations is the user's (an untracked folder through a junction): it is only READ - nothing in it is modified or saved.
Its skeleton (SK_Mannequin_Skeleton, 69 bones) is the UE4 mannequin hierarchy, so the clips are IK-retargeted with the M4 pack's
IK_UE4_Mannequin on BOTH sides (a transient retargeter built here, never saved; the shotgun / pistol pipeline's "Blend to Source" op off) from
the pack's SK_Mannequin onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/Launcher/Male/<pack clip name>   (the Commander, the Engineer: UE4_Mannequin_Skeleton)
  female: SK_Female_soldier      -> /Game/Characters/Animations/Launcher/Female/<pack clip name> (the Medic-Sapper: Female_Soldier_Skeleton)
  pmc:    SKM_PmcOperator        -> /Game/Characters/Animations/Launcher/Pmc/<pack clip name>    (the hostile PMC fighters: SK_PmcOperator_Skeleton,
          a UE5 Manny hierarchy - retargeted UE4 -> UE5 with IK_UE4_Mannequin -> IK_Mannequin, the M4 pack's RTG_UE4Manny_UE5Manny op stack;
          the game designer 2026-11-09: "PMC fighters must be able to use the RPG", feat/rpg-ai-pmc)
CODEX_LAUNCHER_BODIES=He,She,Pmc (default all three) limits the bodies retargeted / measured.
The clip list = every pack path in Content/Data/Weapons/weapon_anim_sets.json "launcher" (strings, directional blocks, aim_poses, hits, deaths).
Then each copy is re-keyed (the retarget leaves the source's key data behind - trim_weapon_anims.py's rebake) and keeps the project's ACL
compression. Per clip and body the script measures against the pack clip (component space, the launcher on the pack's 'Soket_Launcher' of hand_r):
  posture   head / hands relative to the pelvis (cm)                      grip    the left hand in the launcher's frame (cm: hands on the grips)
  shoulder  the right shoulder (upperarm_r) in the launcher's frame (cm)  aim     the tube's direction (deg) in the aim clips' last frame
  drift     the pelvis between the loops' first and last frame (cm)       length  seconds / frames vs the pack
and for the pack clips: the aim poses' yaw / pitch (the aim offset's ranges), the reload clips' insertion moment (the left hand nearest the
tube's front), the deaths' final pelvis height. No skeleton, ABP or pack asset is saved.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_LAUNCHER_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_LAUNCHER_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_launcher_animation.json (+ .txt summary)
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_LAUNCHER_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_LAUNCHER_CLIPS") == "1"
PACK = "/Game/Launcher_Animations/"
PACK_MESH = PACK + "Mesh/SK_Mannequin"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
IK_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Rigs/IK_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
TEMPLATE_RTG_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE4Manny_UE5Manny"
OUT = {"He": "/Game/Characters/Animations/Launcher/Male", "She": "/Game/Characters/Animations/Launcher/Female",
       "Pmc": "/Game/Characters/Animations/Launcher/Pmc"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier",
               "Pmc": "/Game/Characters/Enemies/PMC/Operator/SKM_PmcOperator"}
# The target side's IK rig per body (the UE4 operatives: the pack's own rig; the PMC body: the UE5 Manny rig).
TARGET_IK = {"He": IK_UE4, "She": IK_UE4, "Pmc": IK_UE5}
BODIES = [b.strip() for b in os.environ.get("CODEX_LAUNCHER_BODIES", "He,She,Pmc").split(",") if b.strip() in OUT]
WORLD = unreal.AnimPoseSpaces.WORLD
TUBE_FRONT = unreal.Vector(0.0, 35.6, 0.0)  # the launcher's muzzle in its own frame (cm; the rocket goes in here)
log = []
report = {"clips": {}, "bodies": {}}


def say(text):
    log.append(text)
    unreal.log("setup_launcher_animation: " + text)


def load(path):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def pack_paths():
    with open(os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapon_anim_sets.json"), encoding="utf-8") as f:
        block = json.load(f)["launcher"]
    out = []

    def walk(v):
        if isinstance(v, str) and v.startswith(PACK):
            out.append(v.split(".")[0])
        elif isinstance(v, dict):
            for x in v.values():
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
    walk(block)
    return sorted(set(out))


def socket_transform():
    mesh = load(PACK_MESH)
    sock = mesh.find_socket("Soket_Launcher") if mesh else None
    if not sock:
        return unreal.Transform()
    return unreal.Transform(sock.get_editor_property("relative_location"), sock.get_editor_property("relative_rotation"),
                            sock.get_editor_property("relative_scale"))


SOCKET = socket_transform()


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD)


def launcher_frame(pose):
    """The launcher's component-space transform: the pack's socket on hand_r."""
    return unreal.MathLibrary.compose_transforms(SOCKET, bone(pose, "hand_r"))


def measure(clip, t, mesh=None):
    p = pose_at(clip, t, mesh)
    pelvis = bone(p, "pelvis").translation
    lf = launcher_frame(p)
    rel = {b: bone(p, b).translation - pelvis for b in ("head", "hand_l", "hand_r")}
    grip = unreal.MathLibrary.inverse_transform_location(lf, bone(p, "hand_l").translation)
    shoulder = unreal.MathLibrary.inverse_transform_location(lf, bone(p, "upperarm_r").translation)
    axis = unreal.MathLibrary.transform_direction(lf, unreal.Vector(0.0, 1.0, 0.0))
    return {"rel": rel, "grip": grip, "shoulder": shoulder, "axis": axis, "pelvis": pelvis}


def yaw_pitch(axis):
    """Yaw (+ = the body's right; component +Y is forward, +X the body's LEFT on the UE4 mannequin) / pitch (+ = up) of a direction, deg."""
    yaw = math.degrees(math.atan2(-axis.x, axis.y))
    pitch = math.degrees(math.atan2(axis.z, math.hypot(axis.x, axis.y)))
    return round(yaw, 1), round(pitch, 1)


def dist(a, b):
    return (a - b).length()


def make_retargeter(target_ik_path=IK_UE4):
    """UE4 rig -> the body's rig, transient (never saved): the template's op stack, the source on IK_UE4_Mannequin, the target on
    target_ik_path (IK_UE4_Mannequin for the operatives; IK_Mannequin - the UE5 Manny - for the PMC body, from RTG_UE4Manny_UE5Manny)."""
    ue5 = target_ik_path != IK_UE4
    template = load(TEMPLATE_RTG_UE5 if ue5 else TEMPLATE_RTG)
    ik = load(IK_UE4)
    target_ik = load(target_ik_path)
    if not template or not ik or not target_ik:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_LauncherPack_UE5_TMP" if ue5 else "RTG_LauncherPack_UE4_TMP",
                                                                      "/Game/Characters/Animations/Launcher/_Tmp", template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_ik)
    try:
        ctrl.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_ik)
    except Exception as e:  # noqa: BLE001 - older signature
        say(f"assign_ik_rig_to_all_ops: {e}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    except Exception as e:  # noqa: BLE001
        say(f"auto_map_chains: {e}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE4 -> {'UE5 (IK_Mannequin)' if ue5 else 'UE4 (IK_UE4_Mannequin both sides)'}, ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off")
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


def folder_mb(path):
    disk = path.replace("/Game/", unreal.Paths.project_content_dir())
    total = 0
    for root, _, files in os.walk(disk):
        total += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    return total / 1048576.0


# --- The pack clips: aim angles, the insertion moment, the deaths -------------------------------------------------------------------
paths = pack_paths()
say(f"{len(paths)} pack clips listed in weapon_anim_sets.json \"launcher\"")
pack = {p.rsplit("/", 1)[1]: load(p) for p in paths}
idle = pack.get("Anim_Set_02_Idle_01")
base_axis = measure(idle, 1.0)["axis"] if idle else unreal.Vector(0, 1, 0)
report["pack_idle_tube"] = yaw_pitch(base_axis)
for name, clip in pack.items():
    if not clip:
        continue
    length = clip.get_editor_property("sequence_length")
    entry = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip)}
    first, last = measure(clip, 0.0), measure(clip, length)
    entry["drift_cm"] = round(dist(first["pelvis"], last["pelvis"]), 2)
    if "Rotation" in name:
        entry["aim_yaw_pitch"] = yaw_pitch(last["axis"])
    if "Reload" in name:
        best = (1e9, 0.0)
        frames = entry["frames"]
        for f in range(frames + 1):
            t = min(length, f * length / max(frames, 1))
            p = pose_at(clip, t)
            front = unreal.MathLibrary.transform_location(launcher_frame(p), TUBE_FRONT)
            d = dist(bone(p, "hand_l").translation, front)
            if t > 0.4 and d < best[0]:
                best = (d, t)
        entry["insert_s"] = round(best[1], 3)
        entry["insert_hand_to_muzzle_cm"] = round(best[0], 1)
    if "Dead" in name:
        entry["final_pelvis_z"] = round(last["pelvis"].z, 1)
    if "Shot" in name:
        # the kick: the frame the hands move fastest back
        prev, peak = None, (0.0, 0.0)
        for f in range(entry["frames"] + 1):
            t = f * length / max(entry["frames"], 1)
            m = measure(clip, t)
            if prev is not None and dist(m["rel"]["hand_r"], prev) > peak[0]:
                peak = (dist(m["rel"]["hand_r"], prev), t)
            prev = m["rel"]["hand_r"]
        entry["kick_s"] = round(peak[1], 3)
    report["clips"][name] = entry
for k in ("Anim_Set_02_Rotation_90_Left", "Anim_Set_02_Rotation_90_Right", "Anim_Set_02_Rotation_90_UP", "Anim_Set_02_Rotation_90_Down"):
    if k in report["clips"]:
        say(f"  aim pose {k}: tube yaw / pitch {report['clips'][k].get('aim_yaw_pitch')} (idle {report['pack_idle_tube']})")

# --- Retarget ------------------------------------------------------------------------------------------------------------------------
retargeters = {}
source_mesh = load(PACK_MESH)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
mb_before = sum(folder_mb(o) for o in OUT.values())
for gender in BODIES:
    target_mesh = load(TARGET_MESH[gender])
    if TARGET_IK[gender] not in retargeters:
        retargeters[TARGET_IK[gender]] = make_retargeter(TARGET_IK[gender])
    retargeter = retargeters[TARGET_IK[gender]]
    copies, todo = {}, []
    for path in paths:
        name = path.rsplit("/", 1)[1]
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
                asset.set_editor_property("enable_root_motion", False)
                err = rebake(asset)
                if err > 0.5:
                    say(f"  {data.asset_name}: re-key error {err:.2f} cm (kept the un-keyed copy)")
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
        length = clip.get_editor_property("sequence_length")
        row = {"length": round(new.get_editor_property("sequence_length"), 3), "frames": unreal.AnimationLibrary.get_num_frames(new),
               "compression": str(unreal.AnimationLibrary.get_bone_compression_settings(new).get_name()) if unreal.AnimationLibrary.get_bone_compression_settings(new) else None}
        worst_posture = worst_grip = worst_shoulder = worst_aim = 0.0
        for t in (0.0, length * 0.25, length * 0.5, length * 0.75, length):
            a, b = measure(clip, t), measure(new, t, target_mesh)
            worst_posture = max(worst_posture, max(dist(a["rel"][k], b["rel"][k]) for k in ("head", "hand_l", "hand_r")))
            worst_grip = max(worst_grip, dist(a["grip"], b["grip"]))
            worst_shoulder = max(worst_shoulder, dist(a["shoulder"], b["shoulder"]))
            dot = max(-1.0, min(1.0, a["axis"].x * b["axis"].x + a["axis"].y * b["axis"].y + a["axis"].z * b["axis"].z))
            worst_aim = max(worst_aim, math.degrees(math.acos(dot)))
        f0, f1 = measure(new, 0.0, target_mesh), measure(new, length, target_mesh)
        row.update({"posture_cm": round(worst_posture, 1), "grip_cm": round(worst_grip, 1), "shoulder_cm": round(worst_shoulder, 1),
                    "aim_deg": round(worst_aim, 1), "drift_cm": round(dist(f0["pelvis"], f1["pelvis"]), 2),
                    "grip_in_launcher": [round(v, 1) for v in (f0["grip"].x, f0["grip"].y, f0["grip"].z)]})
        body["clips"][name] = row
    report["bodies"][gender] = body
    worst = max((r.get("grip_cm", 0) for r in body["clips"].values()), default=0)
    say(f"{gender}: {len(copies)} / {len(paths)} copies; worst grip error {worst:.1f} cm")
mb_after = sum(folder_mb(o) for o in OUT.values())
report["size_mb"] = {"before": round(mb_before, 2), "after": round(mb_after, 2),
                     "pack_animations": round(folder_mb(PACK + "Animations"), 2)}
for rtg in retargeters.values():
    if rtg:
        lib.delete_loaded_asset(rtg)
say(f"copies {mb_after:.1f} MB (pack Animations folder {report['size_mb']['pack_animations']:.1f} MB)")
say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_launcher_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_launcher_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
