"""First aid by wound tier (the game designer 2026-11-16, feat/first-aid): survey the healing clip pack and retarget the clips first_aid.json names.

The pack /Game/HealingAnimations is the game designer's (an untracked folder through a junction): it is only READ - nothing in it is modified or
saved. Its clips (Animations/AS_*) sit on the pack's own UE5 mannequin (Demo/Mannequins/Meshes/SKM_Manny, SK_Mannequin - the UE5 hierarchy,
spine_01..05, a different asset from the operatives'), so the clips are IK-retargeted UE5 -> UE4 with a TRANSIENT copy of the M4 pack's
RTG_UE5Manny_UE4Manny (source = the healing pack's IK_Mannequin, target = the M4 pack's IK_UE4_Mannequin; never saved; "Blend to Source" off) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/FirstAid/Male/<pack clip name>   (the operatives' UE4_Mannequin_Skeleton)
  female: SK_Female_soldier      -> /Game/Characters/Animations/FirstAid/Female/<pack clip name> (Female_Soldier_Skeleton)
Which clips: every clip name in Content/Data/Survival/first_aid.json ("items" -> "<item>" -> "clips" -> any list) - add a name there and run the
script again to get its copies. Each copy is re-keyed (the retarget leaves the source's key data behind) and has root motion OFF (the clips are
played in place on the FullBody slot; the capsule stays where the treatment put it).

SURVEY (always, every pack clip): skeleton, length, frames, the root-motion flag, the root's travel over the clip (cm), and the pose: the pelvis
height above the lower foot / the root at the first, middle and last frame, the head height, the hands' mean forward reach from the pelvis
(+Y = the body's forward) - a pose class from the mid-frame pelvis height (standing > 75 cm, kneeling / squatting 40..75, sitting 18..40,
lying < 18) and a role from the name (Friend = a helper clip, Wounded* = the patient).

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_FIRST_AID_DRYRUN=1: the survey only (nothing retargeted or saved). CODEX_RESET_FIRST_AID_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_first_aid_animation.json (+ .txt summary)
"""
import json
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_FIRST_AID_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_FIRST_AID_CLIPS") == "1"
PACK = "/Game/HealingAnimations/Animations"
PACK_MESH = "/Game/HealingAnimations/Demo/Mannequins/Meshes/SKM_Manny"
PACK_IK = "/Game/HealingAnimations/Demo/Mannequins/Rigs/IK_Mannequin"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT_ROOT = "/Game/Characters/Animations/FirstAid"
OUT = {"He": OUT_ROOT + "/Male", "She": OUT_ROOT + "/Female"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
CONFIG = os.path.join(unreal.Paths.project_content_dir(), "Data", "Survival", "first_aid.json")
WORLD = unreal.AnimPoseSpaces.WORLD
SAMPLES = 24
log = []
report = {"pack": PACK, "clips": {}, "bodies": {}, "wanted": [], "missing": []}


def say(text):
    log.append(text)
    unreal.log("setup_first_aid_animation: " + text)


def load(path, quiet=False):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None and not quiet:
        say(f"MISSING {path}")
    return asset


def wanted_clips():
    """Every clip name first_aid.json lists under items.<item>.clips.<set> (lists of names)."""
    names = []
    try:
        with open(CONFIG, encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:  # noqa: BLE001
        say(f"no config {CONFIG}: {e}")
        return names
    for item in (data.get("items") or {}).values():
        for clips in ((item or {}).get("clips") or {}).values():
            for name in clips if isinstance(clips, list) else []:
                if isinstance(name, str) and name not in names:
                    names.append(name)
    return names


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        options.set_editor_property("extract_root_motion", False)
    except Exception:  # noqa: BLE001
        pass
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    try:
        return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD).translation
    except Exception:  # noqa: BLE001
        return None


def pose_class(pelvis_z):
    if pelvis_z is None:
        return "?"
    if pelvis_z > 75.0:
        return "standing"
    if pelvis_z > 40.0:
        return "kneeling/squat"
    if pelvis_z > 18.0:
        return "sitting"
    return "lying"


def role(name):
    low = name.lower()
    if "friend" in low:
        return "helper"
    if low.startswith("as_wounded"):
        return "patient"
    return "self"


def measure(clip, mesh=None):
    length = clip.get_editor_property("sequence_length")
    times = [i * length / SAMPLES for i in range(SAMPLES)] + [max(0.0, length - 0.001)]
    frames = []
    for t in times:
        p = pose_at(clip, t, mesh)
        frames.append({b: bone(p, b) for b in ("root", "pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r")})
    r0 = frames[0]["root"]

    def height(frame, b):
        v = frame.get(b)
        feet = [frame[f].z for f in ("foot_l", "foot_r") if frame.get(f) is not None]
        ground = min(feet + [frame["root"].z]) if feet else frame["root"].z
        return None if v is None else round(v.z - ground, 1)

    mid = frames[len(frames) // 2]
    out = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip),
           "root_travel_cm": round(((frames[-1]["root"] - r0).length()), 1) if r0 is not None else None,
           "pelvis_z": [height(frames[0], "pelvis"), height(mid, "pelvis"), height(frames[-1], "pelvis")],
           "head_z_mid": height(mid, "head"),
           "pelvis_drift_cm": round((frames[-1]["pelvis"] - frames[0]["pelvis"]).length(), 1) if frames[0]["pelvis"] else None}
    reach = []
    for f in frames:
        if f["pelvis"] is not None:
            for h in ("hand_l", "hand_r"):
                if f[h] is not None:
                    reach.append(f[h].y - f["pelvis"].y)
    out["hands_forward_cm"] = round(sum(reach) / len(reach), 1) if reach else None
    out["pose"] = pose_class(out["pelvis_z"][1])
    return out


def make_retargeter():
    """UE5 Manny (the healing pack's IK_Mannequin) -> UE4 rig (IK_UE4_Mannequin), transient copy of the M4 pack's template (never saved)."""
    template = load(TEMPLATE_RTG)
    ik_src = load(PACK_IK)
    ik_dst = load(IK_UE4)
    if not template or not ik_src or not ik_dst:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_FirstAidPack_TMP", OUT_ROOT + "/_Tmp", template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik_src)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik_dst)
    for side, ik in ((unreal.RetargetSourceOrTarget.SOURCE, ik_src), (unreal.RetargetSourceOrTarget.TARGET, ik_dst)):
        try:
            ctrl.assign_ik_rig_to_all_ops(side, ik)
        except Exception as e:  # noqa: BLE001 - older signature / not needed for the source
            say(f"assign_ik_rig_to_all_ops({side}): {e}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    except Exception as e:  # noqa: BLE001
        say(f"auto_map_chains: {e}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE5 -> UE4 (IK_Mannequin -> IK_UE4_Mannequin, ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off)")
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


# --- Survey: every pack clip ------------------------------------------------------------------------------------------------------------
pack_paths = {}
for p in lib.list_assets(PACK, recursive=True, include_folder=False):
    pkg = p.split(".")[0]
    pack_paths[pkg.rsplit("/", 1)[1]] = pkg
source_mesh = load(PACK_MESH)
pack = {}
for name, path in sorted(pack_paths.items()):
    clip = load(path)
    if not isinstance(clip, unreal.AnimSequence):
        continue
    pack[name] = clip
    entry = measure(clip)
    skeleton = clip.get_editor_property("skeleton")
    entry["skeleton"] = skeleton.get_path_name() if skeleton else None
    entry["root_motion_flag"] = bool(clip.get_editor_property("enable_root_motion"))
    entry["role"] = role(name)
    report["clips"][name] = entry
    say(f"  {name}: {entry['length']} s ({entry['frames']} fr), {entry['pose']} pelvis {entry['pelvis_z']} head {entry['head_z_mid']}, "
        f"hands fwd {entry['hands_forward_cm']}, root travel {entry['root_travel_cm']} (RM {entry['root_motion_flag']}), {entry['role']}, "
        f"skeleton {entry['skeleton']}")
say(f"{len(pack)} pack clips surveyed")
if source_mesh:
    say(f"source mesh {source_mesh.get_name()}, skeleton {source_mesh.get_editor_property('skeleton').get_path_name()}")

# --- Retarget the configured clips -------------------------------------------------------------------------------------------------------
wanted = wanted_clips()
report["wanted"] = wanted
report["missing"] = [n for n in wanted if n not in pack]
say(f"first_aid.json names {len(wanted)} clips; not in the pack: {report['missing'] or 'none'}")
if not DRY and wanted:
    retargeter = make_retargeter()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for gender in ("He", "She"):
        target_mesh = load(TARGET_MESH[gender])
        copies, todo = {}, []
        for name in wanted:
            if name not in pack:
                continue
            dest = f"{OUT[gender]}/{name}"
            if lib.does_asset_exist(dest) and not RESET:
                copies[name] = lib.load_asset(dest)
                continue
            data = registry.get_asset_by_object_path(f"{pack_paths[name]}.{name}")
            if data.is_valid():
                todo.append(data)
        if todo and retargeter and source_mesh and target_mesh:
            result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
                todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False, overwrite_existing_files=True)
            for data in result:
                asset = data.get_asset()
                if isinstance(asset, unreal.AnimSequence):
                    err = rebake(asset)
                    asset.set_editor_property("enable_root_motion", False)
                    if err > 0.5:
                        say(f"  {data.asset_name}: re-key error {err:.2f} cm")
                    copies[str(data.asset_name)] = asset
                    lib.save_loaded_asset(asset, False)
            say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}")
        body = {"mesh": TARGET_MESH[gender], "clips": {}}
        for name in wanted:
            new = copies.get(name)
            if not new:
                body["clips"][name] = {"missing": True}
                continue
            row = measure(new, target_mesh)
            row["root_motion_enabled"] = bool(new.get_editor_property("enable_root_motion"))
            row["skeleton"] = new.get_editor_property("skeleton").get_path_name()
            body["clips"][name] = row
            say(f"    {gender} {name}: {row['length']} s, {row['pose']} pelvis {row['pelvis_z']} (pack {report['clips'][name]['pelvis_z']}), "
                f"hands fwd {row['hands_forward_cm']}, root motion {row['root_motion_enabled']}")
        report["bodies"][gender] = body
    if retargeter:
        lib.delete_loaded_asset(retargeter)
    if lib.does_directory_exist(OUT_ROOT + "/_Tmp"):
        lib.delete_directory(OUT_ROOT + "/_Tmp")
say("DRY RUN: survey only, nothing saved" if DRY else "done")
os.makedirs(os.path.join(unreal.Paths.project_saved_dir(), "Logs"), exist_ok=True)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_first_aid_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_first_aid_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
