"""Read-only: the pistol's attach transform on the operative's weapon socket, computed from the pose instead of guessed (the whole pistol, 2026-11-08).
In the retargeted aim idle (MF_Pistol_Idle_ADS, the male copy on SK_Post_Apo_Survivor_a) the barrel must point along the body's forward (+Y, mesh
space) and the slide up (+Z); the grip sits between the wrist (hand_r) and the middle knuckle. The optimized SM_Pistol9mm_Pistol (the art package,
2026-11-09): muzzle = +X, up = +Z, pivot = the grip centre. Prints attach_location / attach_rotation_pyr for weapons_tuning.json "pistol.visual".

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/calibrate_pistol_grip.txt
"""
import os

import unreal

lib = unreal.EditorAssetLibrary
WORLD = unreal.AnimPoseSpaces.WORLD
MESH = "/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/SK_Post_Apo_Survivor_a_1slot"
CLIP = "/Game/Characters/Animations/Pistol/Male/MF_Pistol_Idle_ADS"
GRIP = unreal.Vector(0.0, 0.0, 0.0)  # the pivot is the grip centre
log = []

cdo = unreal.get_default_object(unreal.load_object(None, "/Game/Characters/Operatives/BP_Operative.BP_Operative_C"))
socket = str(cdo.get_editor_property("weapon_socket")) if cdo else "hand_r"
mesh = lib.load_asset(MESH)
clip = lib.load_asset(CLIP)
opt = unreal.AnimPoseEvaluationOptions()
opt.set_editor_property("optional_skeletal_mesh", mesh)
pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, 1.0, opt)
bone = socket
rel = unreal.Transform()
sock = mesh.find_socket(socket) if mesh else None
if sock:
    bone = str(sock.get_editor_property("bone_name"))
    rel = unreal.Transform(sock.get_editor_property("relative_location"), sock.get_editor_property("relative_rotation"), sock.get_editor_property("relative_scale"))
hand = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, WORLD)
S = unreal.MathLibrary.compose_transforms(rel, hand)
knuckle = unreal.AnimPoseExtensions.get_bone_pose(pose, "middle_01_r", WORLD).translation
wrist = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_r", WORLD).translation
forward = unreal.MathLibrary.inverse_transform_direction(S, unreal.Vector(0.0, 1.0, 0.0))
up = unreal.MathLibrary.inverse_transform_direction(S, unreal.Vector(0.0, 0.0, 1.0))
rot = unreal.MathLibrary.make_rot_from_xz(forward, up)  # the barrel is the mesh's +X
grip_cs = unreal.Vector((wrist.x + knuckle.x) / 2, (wrist.y + knuckle.y) / 2, (wrist.z + knuckle.z) / 2)
grip_local = unreal.MathLibrary.inverse_transform_location(S, grip_cs)
offset = unreal.MathLibrary.greater_greater_vector_rotator(GRIP, rot) if hasattr(unreal.MathLibrary, "greater_greater_vector_rotator") else rot.rotate_vector(GRIP)
loc = unreal.Vector(grip_local.x - offset.x, grip_local.y - offset.y, grip_local.z - offset.z)
log.append(f"socket {socket} on bone {bone}; forward in socket space {forward}, up {up}")
log.append(f"attach_location [{loc.x:.2f}, {loc.y:.2f}, {loc.z:.2f}]")
log.append(f"attach_rotation_pyr [{rot.pitch:.2f}, {rot.yaw:.2f}, {rot.roll:.2f}]")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "calibrate_pistol_grip.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
