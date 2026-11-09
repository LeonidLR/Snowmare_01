"""Read-only: the RPG-7's attach transform on the operative's weapon socket (hand_r), computed from the poses (2026-11-09, feat/rpg7-launcher-pack).

In the launcher pack the tube rides on SK_Mannequin's 'Soket_Launcher' (hand_r). The IK-retargeted copies hold the hands where the pack's do
relative to the body, but the operatives' hand_r bone frames differ from the pack mannequin's by ~10 deg (setup_launcher_animation.json "aim_deg"),
so the pack's socket alone would tilt the tube. Here, per body, over the retargeted shouldered idle (Anim_Set_02_Idle_01): the tube's
component-space ORIENTATION is taken from the pack clip at the same time (level, along the body's forward) and its POSITION keeps the pack's
right-hand grip (the hand_r origin in the launcher's frame); attach = inverse(hand_r) * that launcher transform, averaged over the clip.
The model (SM_RPG7_*: the pack's weapon frame, muzzle +Y, up +Z) mounts with it. Prints attach_location / attach_rotation_pyr for
weapons_tuning.json "rpg7.visual" (the Commander's body = the male), the female's own values and the grip / shoulder residuals for both.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/calibrate_launcher_grip.txt
"""
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
WORLD = unreal.AnimPoseSpaces.WORLD
PACK_MESH = "/Game/Launcher_Animations/Mesh/SK_Mannequin"
PACK_IDLE = "/Game/Launcher_Animations/Animations/Set_02/Anim_Set_02_Idle_01"
BODIES = {
    "male (Commander / Engineer)": ("/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/SK_Post_Apo_Survivor_a_1slot",
                                    "/Game/Characters/Animations/Launcher/Male/Anim_Set_02_Idle_01"),
    "female (Medic-Sapper)": ("/Game/Characters/Operatives/Optimized/Female_Soldier/SK_Female_Soldier_1slot",
                              "/Game/Characters/Animations/Launcher/Female/Anim_Set_02_Idle_01"),
}
CHECK_CLIPS = ("Anim_Set_02_Walk", "Anim_Set_02_Run", "Anim_Set_02_Sit_Idle", "Anim_Set_02_Shot", "Anim_Set_02_Reload")
log = []
mesh = lib.load_asset(PACK_MESH)
sock = mesh.find_socket("Soket_Launcher")
SOCKET = unreal.Transform(sock.get_editor_property("relative_location"), sock.get_editor_property("relative_rotation"), sock.get_editor_property("relative_scale"))
pack_idle = lib.load_asset(PACK_IDLE)


def pose(clip, t, body=None):
    o = unreal.AnimPoseEvaluationOptions()
    if body:
        o.set_editor_property("optional_skeletal_mesh", body)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), o)


def bone(p, name):
    return unreal.AnimPoseExtensions.get_bone_pose(p, name, WORLD)


def launcher(p, attach):
    return unreal.MathLibrary.compose_transforms(attach, bone(p, "hand_r"))


def residuals(pack_pose, body_pose, attach):
    a, b = launcher(pack_pose, SOCKET), launcher(body_pose, attach)
    grip = (unreal.MathLibrary.inverse_transform_location(a, bone(pack_pose, "hand_l").translation)
            - unreal.MathLibrary.inverse_transform_location(b, bone(body_pose, "hand_l").translation)).length()
    shoulder_a = unreal.MathLibrary.inverse_transform_location(a, bone(pack_pose, "upperarm_r").translation)
    shoulder_b = unreal.MathLibrary.inverse_transform_location(b, bone(body_pose, "upperarm_r").translation)
    # the shoulder's distance from the tube's axis (the launcher frame's Y axis): on the shoulder = ~5-8 cm
    off_a = math.hypot(shoulder_a.x, shoulder_a.z)
    off_b = math.hypot(shoulder_b.x, shoulder_b.z)
    ax = unreal.MathLibrary.transform_direction(b, unreal.Vector(0, 1, 0))
    ax_a = unreal.MathLibrary.transform_direction(a, unreal.Vector(0, 1, 0))
    dot = max(-1.0, min(1.0, ax.x * ax_a.x + ax.y * ax_a.y + ax.z * ax_a.z))
    return grip, off_a, off_b, math.degrees(math.acos(dot))


for label, (mesh_path, clip_path) in BODIES.items():
    body = lib.load_asset(mesh_path)
    clip = lib.load_asset(clip_path)
    if not body or not clip:
        log.append(f"{label}: missing {mesh_path} / {clip_path}")
        continue
    length = clip.get_editor_property("sequence_length")
    loc_sum = unreal.Vector(0, 0, 0)
    quats = []
    samples = 9
    for i in range(samples):
        t = length * i / (samples - 1)
        pp, bp = pose(pack_idle, t), pose(clip, t, body)
        la = launcher(pp, SOCKET)
        grip_in_launcher = unreal.MathLibrary.inverse_transform_location(la, bone(pp, "hand_r").translation)  # the right hand on the pistol grip
        rot = la.rotation
        hand = bone(bp, "hand_r")
        want_loc = hand.translation - rot.rotate_vector(grip_in_launcher)
        want = unreal.Transform(want_loc, rot.rotator(), unreal.Vector(1, 1, 1))
        attach = unreal.MathLibrary.compose_transforms(want, unreal.MathLibrary.invert_transform(hand))
        loc_sum += attach.translation
        quats.append(attach.rotation)
    loc = loc_sum / samples
    q = quats[len(quats) // 2]
    attach = unreal.Transform(loc, q.rotator(), unreal.Vector(1, 1, 1))
    r = q.rotator()
    log.append(f"{label}: attach_location [{loc.x:.2f}, {loc.y:.2f}, {loc.z:.2f}]  attach_rotation_pyr [{r.pitch:.2f}, {r.yaw:.2f}, {r.roll:.2f}]")
    sr = sock.get_editor_property("relative_rotation")
    log.append(f"   (the pack's socket: [{SOCKET.translation.x:.2f}, {SOCKET.translation.y:.2f}, {SOCKET.translation.z:.2f}] / [{sr.pitch:.2f}, {sr.yaw:.2f}, {sr.roll:.2f}])")
    folder = clip_path.rsplit("/", 1)[0]
    for name in ("Anim_Set_02_Idle_01",) + CHECK_CLIPS:
        c = lib.load_asset(f"{folder}/{name}")
        pc = lib.load_asset(f"/Game/Launcher_Animations/Animations/Set_02/{name}")
        if not c or not pc:
            continue
        worst = [0.0, 0.0, 0.0, 0.0]
        n = c.get_editor_property("sequence_length")
        for i in range(5):
            t = n * i / 4
            g, sa, sb, ang = residuals(pose(pc, t), pose(c, t, body), attach)
            worst = [max(worst[0], g), sa, sb, max(worst[3], ang)]
        log.append(f"   {name}: left hand off its grip <= {worst[0]:.1f} cm, shoulder off the tube axis {worst[2]:.1f} cm (pack {worst[1]:.1f}), "
                   f"tube vs the pack's direction <= {worst[3]:.1f} deg")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "calibrate_launcher_grip.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
