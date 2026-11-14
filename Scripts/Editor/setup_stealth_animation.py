"""Stealth pack inspection / scoped retarget for designer task 003.
Default: inspect source clips only, saving no assets. CODEX_STEALTH_WRITE=1 generates only
/Game/Characters/Animations/Stealth/{Male,Female}; the source pack is read-only.
Run as an Unreal Python commandlet in CodexTactics-interim, with the designer editor closed.
Retarget and rebake follow setup_injured_animation.py; no Blueprint or material assets are generated.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import unreal

lib = unreal.EditorAssetLibrary
WORLD = unreal.AnimPoseSpaces.WORLD
WRITE = os.environ.get("CODEX_STEALTH_WRITE") == "1"
PROJECT = Path(unreal.Paths.project_dir()).resolve()
PACK = "/Game/StealthLocomotion"
SOURCE_MESH = PACK + "/Characters/Mannequin/Mesh/SK_Mannequin"
IK = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"Male": "/Game/Characters/Animations/Stealth/Male", "Female": "/Game/Characters/Animations/Stealth/Female"}
MESH = {"Male": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "Female": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}

assert PROJECT.name == "CodexTactics-interim", "Run only in the authorized interim worktree"
branch = subprocess.check_output(["git", "branch", "--show-current"], cwd=str(PROJECT), text=True).strip()
assert branch == "interim/2026-11-12", "Wrong integration branch"
for output in OUT.values():
    disk = (PROJECT / "Content" / output.removeprefix("/Game/")).resolve()
    assert str(disk).lower().startswith(str(PROJECT / "Content" / "Characters" / "Animations" / "Stealth").lower() + os.sep), "Output junction escapes the authorized worktree"
if WRITE:
    assert not unreal.SourceControl.is_enabled(), "Run with -SCCProvider=None: the temporary retargeter must never be auto-saved"
    editors = subprocess.check_output(["powershell", "-NoProfile", "-Command",
        "@(Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { $_.CommandLine -like '*CodexTactics*' }).Count"], text=True).strip()
    assert editors == "0", "Close the designer's Unreal editor before generating retargeted copies"


def load(path):
    asset = lib.load_asset(path)
    assert asset is not None, "Missing asset: " + path
    return asset


def sample_pose(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("extract_root_motion", False)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, t, options)


def measure(clip, mesh=None):
    length = float(clip.get_editor_property("sequence_length"))
    out = {"length": round(length, 4), "frames": unreal.AnimationLibrary.get_num_frames(clip),
           "skeleton": clip.get_editor_property("skeleton").get_path_name()}
    name = clip.get_name()
    if not re.fullmatch(r"Stealth_(?:Slow|Fast)_(?:F|FR|R|BR|B|BL|L|FL)_IP|Stealth_(?:Left|Right)Wall_(?:Slow|Fast)_IP", name):
        return out
    n = max(3, int(length * 60))
    names = ("foot_l", "foot_r", "ball_l", "ball_r")
    frames = []
    for i in range(n):
        pose = sample_pose(clip, i * length / n, mesh)
        frames.append({b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, WORLD).translation for b in names})
    low = {b: min(f[b].z for f in frames) for b in names}
    speeds = []
    dx = dy = 0.0
    for i in range(1, n):
        contacts = []
        for b in names:
            a, c = frames[i - 1][b], frames[i][b]
            if a.z <= low[b] + 2.0 and c.z <= low[b] + 2.0:
                contacts.append((math.hypot(c.x - a.x, c.y - a.y) * n / length, c.x - a.x, c.y - a.y))
        if contacts:
            speed, sx, sy = min(contacts)
            speeds.append(speed)
            dx += sx
            dy += sy
    strikes = [i / n for i in range(n) if frames[i]["foot_l"].z <= low["foot_l"] + 2.0
               and frames[i - 1]["foot_l"].z > low["foot_l"] + 2.0]
    out.update(contact_speed=round(statistics.median(speeds), 2) if speeds else 0.0,
               phase_offset=round(strikes[0], 4) if strikes else 0.0,
               heading_deg=round(math.degrees(math.atan2(dx, -dy)), 1) if speeds else None)
    return out


# Exact rebake routine reused from setup_injured_animation.py: retargeted keys must not retain source tracks.
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


source_files = sorted((PROJECT / "Content" / "StealthLocomotion").rglob("*.uasset"))
source_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
report = {"write": WRITE, "source_mesh": SOURCE_MESH, "clips": {}, "bodies": {}}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([PACK], True)
paths = sorted(p.split(".")[0] for p in lib.list_assets(PACK + "/Animations/InPlace", recursive=False, include_folder=False))
source = {}
for path in paths:
    clip = load(path)
    if isinstance(clip, unreal.AnimSequence):
        source[path.rsplit("/", 1)[1]] = (path, clip)
        report["clips"][clip.get_name()] = measure(clip)
assert len(source) >= 40, "The complete stealth pack was not found"

if WRITE:
    assert len(source) == 62, "Authorization covers exactly 62 source clips per body; source list changed"
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_Stealth_TMP", OUT["Male"] + "/_Tmp", load(TEMPLATE))
    assert rtg is not None, "Cannot create the in-memory retargeter"
    try:
        ctrl = unreal.IKRetargeterController.get_controller(rtg)
        rig = load(IK)
        ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
        ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, rig)
        ctrl.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, rig)
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
        index = ctrl.get_index_of_op_by_name("Blend to Source")
        if index >= 0:
            ctrl.set_retarget_op_enabled(index, False)
        source_mesh = load(SOURCE_MESH)
        for gender, folder in OUT.items():
            target_mesh = load(MESH[gender])
            todo = []
            for name, (path, _) in source.items():
                if not lib.does_asset_exist(folder + "/" + name):
                    data = registry.get_asset_by_object_path(path + "." + name)
                    assert data.is_valid(), path
                    todo.append(data)
            if todo:
                copies = unreal.IKRetargetBatchOperation.duplicate_and_retarget(todo, source_mesh, target_mesh, rtg,
                    target_path=folder, include_referenced_assets=False, overwrite_existing_files=False)
                for data in copies:
                    asset = data.get_asset()
                    assert asset.get_path_name().startswith(folder + "/"), "Retarget output escaped the allowed folder"
                    assert isinstance(asset, unreal.AnimSequence)
                    asset.set_editor_property("enable_root_motion", False)
                    error = rebake(asset)
                    assert error <= 0.5, f"{asset.get_name()}: rebake error {error} cm"
                    assert lib.save_loaded_asset(asset, False), asset.get_path_name()
            body = {"mesh": MESH[gender], "clips": {}}
            ratios = []
            for name, (_, original) in source.items():
                copy = load(folder + "/" + name)
                assert copy.get_editor_property("skeleton") == target_mesh.get_editor_property("skeleton"), name
                row = measure(copy, target_mesh)
                body["clips"][name] = row
                speed = report["clips"][name].get("contact_speed", 0)
                if speed > 1 and row.get("contact_speed", 0) > 1:
                    ratios.append(row["contact_speed"] / speed)
            body["stride_scale"] = round(statistics.median(ratios), 4) if ratios else 1.0
            report["bodies"][gender] = body
    finally:
        # This retargeter was never saved. Only the new destination animation copies are saved above.
        lib.delete_loaded_asset(rtg)

assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest() == value for p, value in source_hashes.items()), "Source pack changed"
report["source_pack_unchanged"] = True
out = PROJECT / "Saved" / "Logs" / "setup_stealth_animation.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log(f"Stealth animation {'retarget' if WRITE else 'source-only inspection'} complete: {len(source)} clips; report {out}")
