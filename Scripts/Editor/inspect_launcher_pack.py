"""Read-only inspection of the game designer's RPG launcher pack (2026-11-09, feat/rpg7-launcher-pack): /Game/Launcher_Animations.

The pack is the user's (an untracked folder reached through a junction): NOTHING is saved or modified here. Reports
  - every AnimSequence / other asset under Animations/ (length, frames, rate, root motion, root travel, notifies, skeleton, additive type),
  - the pack skeleton (bone list + parents) next to the operatives' skeletons (UE4_Mannequin_Skeleton, Female_Soldier_Skeleton, the PMC operator),
  - the sockets of the pack's SK_Mannequin and of every weapon mesh (where B_Mannequin holds the launcher, where the rocket sits),
  - the Blueprint components of B_Launcher / B_Mannequin (mesh, attach parent / socket, relative transform),
  - the weapon meshes (LOD0 triangles, sections, bounds, bones) and the textures (size, compression),
  - per clip: the hands / head relative to the pelvis at mid-clip; for the Shot / Reload clips the per-frame hand speeds (the fire moment, the
    rocket insertion moment).
Output: Saved/Logs/inspect_launcher_pack.json

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi -nosplash
"""
import json
import os

import unreal

PACK = "/Game/Launcher_Animations"
out = {"errors": []}
lib = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
WORLD = unreal.AnimPoseSpaces.WORLD


def safe(fn, default=None):
    try:
        return fn()
    except Exception as exc:  # noqa: BLE001 - inspection: keep going
        out["errors"].append(str(exc))
        return default


def vec(v):
    return [round(v.x, 2), round(v.y, 2), round(v.z, 2)]


def rot(r):
    return [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)]


def assets_under(path):
    return list(reg.get_assets_by_path(path, recursive=True))


def pose_at(clip, t):
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), unreal.AnimPoseEvaluationOptions())


def bone_ws(pose, bone):
    return safe(lambda: unreal.AnimPoseExtensions.get_bone_pose(pose, bone, WORLD), None)


# --- Animations -----------------------------------------------------------------------------------------------------------
clips = {}
others = []
for data in assets_under(PACK + "/Animations"):
    cls = str(data.asset_class_path.asset_name)
    name = str(data.asset_name)
    if cls != "AnimSequence":
        entry = {"path": str(data.package_name), "class": cls}
        asset = data.get_asset()
        if isinstance(asset, unreal.BlendSpace):
            entry["samples"] = safe(lambda: [str(s.get_editor_property("animation").get_name()) for s in asset.get_editor_property("sample_data")], [])
        others.append(entry)
        continue
    anim = data.get_asset()
    length = anim.get_play_length()
    entry = {"path": str(data.package_name), "length": round(length, 3)}
    entry["frames"] = safe(lambda: unreal.AnimationLibrary.get_num_frames(anim))
    entry["fps"] = round(entry["frames"] / length, 2) if entry["frames"] and length > 0 else None
    entry["root_motion"] = safe(lambda: anim.get_editor_property("enable_root_motion"))
    entry["additive"] = str(safe(lambda: anim.get_editor_property("additive_anim_type")))
    skel = anim.get_editor_property("skeleton")
    entry["skeleton"] = skel.get_path_name() if skel else None
    start = safe(lambda: unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False))
    end = safe(lambda: unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False))
    if start and end:
        d = end.translation - start.translation
        entry["root_delta"] = vec(d)
        entry["root_speed"] = round((d.x ** 2 + d.y ** 2) ** 0.5 / max(length, 0.001), 1)
        entry["root_yaw_delta"] = round(end.rotation.rotator().yaw - start.rotation.rotator().yaw, 1)
    # pelvis travel (in-place clips: the hips should not drift; root-motion clips carry it in the root)
    p0, p1 = pose_at(anim, 0.0), pose_at(anim, length)
    a, b = bone_ws(p0, "pelvis"), bone_ws(p1, "pelvis")
    if a and b:
        entry["pelvis_delta"] = vec(b.translation - a.translation)
    notifies = safe(lambda: unreal.AnimationLibrary.get_animation_notify_events(anim), [])
    entry["notifies"] = [[str(n.get_editor_property("notify_name")), round(n.get_editor_property("trigger_time_offset"), 3)] for n in notifies] if notifies else []
    mid = pose_at(anim, length * 0.5)
    pelvis = bone_ws(mid, "pelvis")
    if pelvis:
        entry["mid"] = {b: vec(bone_ws(mid, b).translation - pelvis.translation) for b in ("head", "hand_l", "hand_r") if bone_ws(mid, b)}
        entry["mid_pelvis_z"] = round(pelvis.translation.z, 1)
    # per-frame hand / head speeds for the one-shots (the fire kick, the rocket insertion)
    if any(k in name for k in ("Shot", "Reload", "Attack", "Get_Hit", "Dead", "Turn", "Rotation")):
        frames = entry["frames"] or 0
        track = []
        prev = None
        for f in range(0, frames + 1):
            t = min(length, f / max(entry["fps"] or 30.0, 1.0))
            p = pose_at(anim, t)
            pts = {b: bone_ws(p, b) for b in ("pelvis", "head", "hand_l", "hand_r", "spine_03", "lowerarm_r")}
            if not pts["pelvis"]:
                break
            rel = {b: pts[b].translation - pts["pelvis"].translation for b in pts if pts[b] and b != "pelvis"}
            row = {"t": round(t, 3), "pelvis_z": round(pts["pelvis"].translation.z, 1)}
            for b, v in rel.items():
                row[b] = vec(v)
            if prev:
                row["v_hand_l"] = round((rel["hand_l"] - prev["hand_l"]).length(), 2)
                row["v_hand_r"] = round((rel["hand_r"] - prev["hand_r"]).length(), 2)
                row["v_head"] = round((rel["head"] - prev["head"]).length(), 2)
            prev = rel
            track.append(row)
        entry["track"] = track
    clips[name] = entry
out["clips"] = clips
out["other_anim_assets"] = others


# --- Skeletons ------------------------------------------------------------------------------------------------------------
def describe_skeleton(path):
    skel = lib.load_asset(path)
    if not skel:
        return None
    return unreal.OperativeAnimGraphLibrary.describe_skeleton(skel).splitlines()


skeleton_paths = {"pack": PACK + "/Mesh/SK_Mannequin_Skeleton", "pmc_operator": "/Game/Characters/Enemies/PMC/Operator/SK_PmcOperator_Skeleton"}
for key, bp_path in (("operative", "/Game/Characters/Operatives/BP_Operative"),
                     ("female_soldier", "/Game/Characters/Operatives/BP_Operative_FemaleSoldier")):
    cls = safe(lambda: unreal.EditorAssetLibrary.load_blueprint_class(bp_path))
    cdo = unreal.get_default_object(cls) if cls else None
    mesh_comp = cdo.get_editor_property("mesh") if cdo else None
    mesh = mesh_comp.get_editor_property("skeletal_mesh_asset") if mesh_comp else None
    if mesh:
        skel = mesh.get_editor_property("skeleton")
        out.setdefault("operative_meshes", {})[key] = {
            "mesh": mesh.get_path_name(), "skeleton": skel.get_path_name() if skel else None,
            "anim_class": str(mesh_comp.get_editor_property("anim_class")),
            "weapon_socket": str(safe(lambda: cdo.get_editor_property("weapon_socket")))}
        if skel:
            skeleton_paths[key] = skel.get_path_name().split(".")[0]
        sock_name = safe(lambda: cdo.get_editor_property("weapon_socket"))
        sock = safe(lambda: mesh.find_socket(sock_name)) if sock_name else None
        if sock:
            out["operative_meshes"][key]["weapon_socket_def"] = {
                "bone": str(sock.get_editor_property("bone_name")), "loc": vec(sock.get_editor_property("relative_location")),
                "rot": rot(sock.get_editor_property("relative_rotation")), "scale": vec(sock.get_editor_property("relative_scale"))}
out["skeletons"] = {k: describe_skeleton(v) for k, v in skeleton_paths.items()}


# --- Meshes, sockets ------------------------------------------------------------------------------------------------------
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
            info["bones"] = unreal.OperativeAnimGraphLibrary.describe_skeleton(skel).splitlines()[:12]
        socks = []
        for i in range(safe(lambda: mesh.num_sockets(), 0) or 0):
            s = mesh.get_socket_by_index(i)
            socks.append({"name": str(s.socket_name), "bone": str(s.bone_name), "loc": vec(s.relative_location), "rot": rot(s.relative_rotation)})
        info["sockets"] = socks
        info["materials"] = [str(m.get_editor_property("material_slot_name")) + " = " + (m.get_editor_property("material_interface").get_path_name()
                             if m.get_editor_property("material_interface") else "None") for m in mesh.get_editor_property("materials")]
        sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem) if hasattr(unreal, "SkeletalMeshEditorSubsystem") else None
        info["lod0_verts"] = safe(lambda: sub.get_num_verts(mesh, 0), None) if sub else None
        info["lod_count"] = safe(lambda: mesh.get_lod_num(), None)
    return info


meshes = []
for data in assets_under(PACK + "/Mesh") + assets_under(PACK + "/Materials") + assets_under(PACK + "/Textures"):
    cls = str(data.asset_class_path.asset_name)
    asset = data.get_asset()
    if cls in ("SkeletalMesh", "StaticMesh"):
        meshes.append(mesh_info(asset))
    elif cls == "Texture2D":
        meshes.append({"path": str(data.package_name), "class": cls, "size": [asset.blueprint_get_size_x(), asset.blueprint_get_size_y()],
                       "srgb": asset.get_editor_property("srgb"), "compression": str(asset.get_editor_property("compression_settings"))})
    else:
        meshes.append({"path": str(data.package_name), "class": cls})
out["pack_assets"] = meshes


# --- Blueprint components (where the demo holds the launcher) -------------------------------------------------------------
def bp_components(path):
    bp = lib.load_asset(path)
    if not bp:
        return None
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = sub.k2_gather_subobject_data_for_blueprint(bp)
    rows = []
    for h in handles:
        data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if obj is None:
            continue
        row = {"name": obj.get_name(), "class": obj.get_class().get_name()}
        if isinstance(obj, unreal.SceneComponent):
            row["loc"] = vec(obj.get_editor_property("relative_location"))
            row["rot"] = rot(obj.get_editor_property("relative_rotation"))
            row["scale"] = vec(obj.get_editor_property("relative_scale3d"))
            row["attach_socket"] = str(obj.get_editor_property("attach_socket_name"))
            parent = obj.get_attach_parent()
            row["parent"] = parent.get_name() if parent else None
        if isinstance(obj, unreal.SkeletalMeshComponent):
            m = obj.get_editor_property("skeletal_mesh_asset")
            row["mesh"] = m.get_path_name() if m else None
            row["anim_class"] = str(obj.get_editor_property("anim_class"))
            row["anim"] = str(safe(lambda: obj.get_editor_property("animation_data").get_editor_property("anim_to_play")))
        if isinstance(obj, unreal.StaticMeshComponent):
            m = obj.get_editor_property("static_mesh")
            row["mesh"] = m.get_path_name() if m else None
        if isinstance(obj, unreal.ChildActorComponent):
            row["child_class"] = str(obj.get_editor_property("child_actor_class"))
        rows.append(row)
    return rows


out["blueprints"] = {name: safe(lambda: bp_components(f"{PACK}/Blueprint/{name}")) for name in ("B_Launcher", "B_Mannequin")}

path = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "inspect_launcher_pack.json")
with open(path, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1)
unreal.log("inspect_launcher_pack -> " + path)
