"""Read-only: the ShotgunLocomotionPack's root-motion loops for the full-body shotgun set (game designer 2026-11-09 b: "the shotgun locomotion from
/Game/ShotgunLocomotionPack/Animations/Loop_Root"). Per clip of Loop_Root / TurnInPlace / AimOffset (the pack, read only) and per retargeted copy
in /Game/Characters/Animations/Shotgun/{Male,Female} when present:
  length / frames / additive type / root motion flag; the root's travel (cm, the clip's natural ground speed = travel / length) and yaw;
  the pelvis's travel in component space (on the copies: where the retarget put the root motion); the loop seam (first vs last frame, cm);
  the stride (a foot's range along the travel) - the copies' stride against the pack's = their speed scale;
  for the aim-offset poses: the gun line's (hand_r -> hand_l) yaw / pitch against the front_C pose.
Nothing is saved. Report: Saved/Logs/measure_shotgun_loop_root.json

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
W = unreal.AnimPoseSpaces.WORLD
PACK = "/Game/ShotgunLocomotionPack/Animations/"
COPIES = {"male": ("/Game/Characters/Animations/Shotgun/Male", "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a"),
          "female": ("/Game/Characters/Animations/Shotgun/Female", "/Game/Female_Soldier/Mesh/SK_Female_soldier")}
out = {"pack": {}, "copies": {}}


def pose(clip, t, mesh=None):
    o = unreal.AnimPoseEvaluationOptions()
    if mesh:
        o.set_editor_property("optional_skeletal_mesh", mesh)
    L = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, L - 0.0005)), o)


def bone(p, name):
    return unreal.AnimPoseExtensions.get_bone_pose(p, name, W)


def v(x):
    return [round(x.x, 2), round(x.y, 2), round(x.z, 2)]


def measure(clip, mesh=None):
    L = clip.get_editor_property("sequence_length")
    n = unreal.AnimationLibrary.get_num_frames(clip)
    e = {"length": round(L, 3), "frames": n, "additive": str(clip.get_editor_property("additive_anim_type")).split(".")[-1].rstrip(">"),
         "root_motion": clip.get_editor_property("enable_root_motion")}
    a, b = pose(clip, 0.0, mesh), pose(clip, L, mesh)
    r0, r1 = bone(a, "root"), bone(b, "root")
    d = r1.translation - r0.translation
    e["root_travel"] = v(d)
    e["root_speed"] = round(math.hypot(d.x, d.y) / max(L, 0.001), 1)
    e["root_yaw"] = round(r1.rotation.rotator().yaw - r0.rotation.rotator().yaw, 1)
    p0, p1 = bone(a, "pelvis").translation, bone(b, "pelvis").translation
    e["pelvis_travel"] = v(p1 - p0)
    e["pelvis_rel_root_travel"] = v((p1 - r1.translation) - (p0 - r0.translation))
    # loop seam: head / hands / feet relative to the root, first vs last frame
    seam = 0.0
    for name in ("head", "hand_l", "hand_r", "foot_l", "foot_r"):
        seam = max(seam, ((bone(a, name).translation - r0.translation) - (bone(b, name).translation - r1.translation)).length())
    e["seam_cm"] = round(seam, 2)
    # stride: foot_l range along the root travel direction, relative to the root
    if math.hypot(d.x, d.y) > 1.0:
        dx, dy = d.x / math.hypot(d.x, d.y), d.y / math.hypot(d.x, d.y)
        vals = []
        for f in range(n + 1):
            p = pose(clip, f * L / max(n, 1), mesh)
            rel = bone(p, "foot_l").translation - bone(p, "root").translation
            vals.append(rel.x * dx + rel.y * dy)
        e["stride_cm"] = round(max(vals) - min(vals), 2)
        # the left foot's heel strike (its most forward point along the travel) as a share of the loop: the loops' phase offsets, so a
        # direction blend lines up the same foot (sync markers)
        e["left_strike_phase"] = round(vals.index(max(vals)) / max(n, 1), 4)
    # the plant speed: a foot on the ground (within 3 cm of its lowest) moves back under the body at the clip's real ground speed
    feet = {"foot_l": [], "foot_r": []}
    for f in range(n + 1):
        p = pose(clip, f * L / max(n, 1), mesh)
        root = bone(p, "root").translation
        for name in feet:
            feet[name].append(bone(p, name).translation - root)
    speeds = []
    dt = L / max(n, 1)
    for name, pts in feet.items():
        low = min(q.z for q in pts)
        for i in range(1, len(pts)):
            if pts[i].z < low + 3.0 and pts[i - 1].z < low + 3.0:
                speeds.append(math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y) / dt)
    speeds.sort()
    if speeds:
        e["plant_speed"] = round(speeds[len(speeds) // 2], 1)
        e["plant_frames"] = len(speeds)
    if "aimoffset" in clip.get_name():
        hr, hl = bone(b, "hand_r").translation, bone(b, "hand_l").translation
        g = hl - hr
        e["gun_yaw_pitch"] = [round(math.degrees(math.atan2(g.y, g.x)), 1), round(math.degrees(math.atan2(g.z, math.hypot(g.x, g.y))), 1)]
    return e


for folder in ("Loop_Root", "TurnInPlace", "AimOffset", "Idle"):
    for data in reg.get_assets_by_path(PACK + folder, recursive=False):
        if str(data.asset_class_path.asset_name) != "AnimSequence":
            continue
        clip = data.get_asset()
        out["pack"][f"{folder}/{data.asset_name}"] = measure(clip)
        for key, (copy_folder, mesh_path) in COPIES.items():
            path = f"{copy_folder}/{data.asset_name}"
            if lib.does_asset_exist(path):
                out["copies"].setdefault(key, {})[str(data.asset_name)] = measure(lib.load_asset(path), lib.load_asset(mesh_path))
path = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "measure_shotgun_loop_root.json")
with open(path, "w", encoding="utf-8") as f:
    json.dump(out, f, indent=1)
unreal.log("measure_shotgun_loop_root -> " + path)
