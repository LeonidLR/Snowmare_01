"""Read-only inspection for the D-1 shotgun work (2026-11-08): the ShotgunLocomotionPack clips (lengths, root speed of the *_root
loops, notifies), the skeletons involved (pack SK_Mannequin, the operative / Female Soldier / PMC Operator skeletons: bones, parents,
translation retargeting modes), the D-1 model (/Game/Fab/_Model_D-1__Shotgun) and the pack's demo shotgun (bounds, bones, sockets).
Nothing is saved. Output: Saved/Logs/inspect_shotgun_pack.json.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path to this file>
"""
import json
import os

import unreal

out = {"errors": []}
lib = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()


def safe(fn, default=None):
    try:
        return fn()
    except Exception as exc:  # noqa: BLE001 - inspection: keep going
        out["errors"].append(str(exc))
        return default


def vec(v):
    return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]


def assets_under(path):
    return [a for a in reg.get_assets_by_path(path, recursive=True)]


# --- Pack clips ---------------------------------------------------------------------------------------------------------
clips = {}
for data in assets_under("/Game/ShotgunLocomotionPack/Animations"):
    if str(data.asset_class_path.asset_name) != "AnimSequence":
        continue
    anim = data.get_asset()
    name = str(data.asset_name)
    entry = {"path": str(data.package_name), "length": round(anim.get_play_length(), 3)}
    entry["frames"] = safe(lambda: unreal.AnimationLibrary.get_num_frames(anim))
    entry["root_motion"] = safe(lambda: anim.get_editor_property("enable_root_motion"))
    skel = anim.get_editor_property("skeleton")
    entry["skeleton"] = skel.get_path_name() if skel else None
    if name.endswith("_root") or "start" in name or "stop" in name or "turn" in name:
        start = safe(lambda: unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False))
        end = safe(lambda: unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", anim.get_play_length(), False))
        if start and end:
            d = end.translation - start.translation
            entry["root_delta"] = vec(d)
            entry["root_speed"] = round((d.x ** 2 + d.y ** 2) ** 0.5 / max(anim.get_play_length(), 0.001), 1)
    notifies = safe(lambda: unreal.AnimationLibrary.get_animation_notify_events(anim), [])
    entry["notifies"] = [str(n.get_editor_property("notify_name")) for n in notifies] if notifies else []
    clips[name] = entry
out["clips"] = clips

# --- Skeletons ----------------------------------------------------------------------------------------------------------
def describe_skeleton(path):
    skel = lib.load_asset(path)
    if not skel:
        return None
    return unreal.OperativeAnimGraphLibrary.describe_skeleton(skel).splitlines()


skeleton_paths = {
    "pack_SK_Mannequin": "/Game/ShotgunLocomotionPack/Demo/Characters/Mannequins/Meshes/SK_Mannequin",
    "pmc_operator": "/Game/Characters/Enemies/PMC/Operator/SK_PmcOperator_Skeleton",
}
# The operative's skeleton through BP_Operative's mesh, the Female Soldier's through her member Blueprint.
for key, bp_path in (("operative", "/Game/Characters/Operatives/BP_Operative"),
                     ("female_soldier", "/Game/Characters/Operatives/BP_Operative_FemaleSoldier")):
    cls = safe(lambda: unreal.EditorAssetLibrary.load_blueprint_class(bp_path))
    cdo = unreal.get_default_object(cls) if cls else None
    mesh_comp = cdo.get_editor_property("mesh") if cdo else None
    mesh = mesh_comp.get_editor_property("skeletal_mesh_asset") if mesh_comp else None
    if mesh:
        skel = mesh.get_editor_property("skeleton")
        out.setdefault("meshes", {})[key] = {"mesh": mesh.get_path_name(), "skeleton": skel.get_path_name() if skel else None,
                                             "anim_class": str(mesh_comp.get_editor_property("anim_class"))}
        if skel:
            skeleton_paths[key] = skel.get_path_name().split(".")[0]
out["skeletons"] = {k: describe_skeleton(v) for k, v in skeleton_paths.items()}

# --- Sockets on the pack's mannequin (where the demo holds the shotgun) ------------------------------------------------
pack_skel = lib.load_asset(skeleton_paths["pack_SK_Mannequin"])
sockets = []
for mesh_path in ("/Game/ShotgunLocomotionPack/Demo/Characters/Mannequins/Meshes/SKM_Manny",):
    mesh = lib.load_asset(mesh_path)
    if mesh:
        for i in range(safe(lambda: mesh.num_sockets(), 0) or 0):
            s = mesh.get_socket_by_index(i)
            sockets.append({"owner": mesh_path, "name": str(s.socket_name), "bone": str(s.bone_name),
                            "loc": vec(s.relative_location), "rot": [round(s.relative_rotation.pitch, 2), round(s.relative_rotation.yaw, 2),
                                                                     round(s.relative_rotation.roll, 2)], "scale": vec(s.relative_scale)})
out["manny_sockets"] = sockets

# --- Meshes: D-1 and the demo shotgun -----------------------------------------------------------------------------------
def mesh_info(mesh):
    info = {"path": mesh.get_path_name(), "class": mesh.get_class().get_name()}
    b = safe(lambda: mesh.get_bounds())
    if b:
        info["bounds_origin"] = vec(b.origin)
        info["bounds_extent"] = vec(b.box_extent)
    if isinstance(mesh, unreal.SkeletalMesh):
        skel = mesh.get_editor_property("skeleton")
        info["skeleton"] = skel.get_path_name() if skel else None
        if skel:
            info["bones"] = unreal.OperativeAnimGraphLibrary.describe_skeleton(skel).splitlines()[:40]
        info["sockets"] = [str(mesh.get_socket_by_index(i).socket_name) for i in range(safe(lambda: mesh.num_sockets(), 0) or 0)]
    return info


meshes = []
for root in ("/Game/Fab/_Model_D-1__Shotgun", "/Game/ShotgunLocomotionPack/Demo/Asset"):
    for data in assets_under(root):
        cls = str(data.asset_class_path.asset_name)
        if cls in ("SkeletalMesh", "StaticMesh"):
            meshes.append(mesh_info(data.get_asset()))
        elif cls == "AnimSequence":
            anim = data.get_asset()
            meshes.append({"path": str(data.package_name), "class": cls, "length": round(anim.get_play_length(), 3)})
        else:
            meshes.append({"path": str(data.package_name), "class": cls})
out["weapon_assets"] = meshes

path = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "inspect_shotgun_pack.json")
with open(path, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1)
unreal.log("inspect_shotgun_pack -> " + path)
