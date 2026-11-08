"""Read-only: the shotgun reload clip's left-hand path, to pick the shell-insertion segment (weapon_anim_sets.json reload_shell_start_s /
_end_s). Samples the retargeted male copy (/Game/Characters/Animations/Shotgun/Male/anim_shotgun_aim_reload) on SK_Post_Apo_Survivor_a every
1/30 s: hand_l relative to hand_r (the gun) and to spine_03, component space, cm; plus the hand_l speed. A shell insertion = hand_l travelling
from the gun's loading port (under the receiver) and back.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/measure_shotgun_reload.txt
"""
import os

import unreal

assets = unreal.EditorAssetLibrary
WORLD = unreal.AnimPoseSpaces.WORLD
CLIPS = {"male copy": ("/Game/Characters/Animations/Shotgun/Male/anim_shotgun_aim_reload", "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a"),
         "pack": ("/Game/ShotgunLocomotionPack/Animations/Aim/anim_shotgun_aim_reload", None)}
log = []


def say(text):
    log.append(text)
    unreal.log("measure_shotgun_reload: " + text)


for name, (path, mesh_path) in CLIPS.items():
    clip = assets.load_asset(path)
    mesh = assets.load_asset(mesh_path) if mesh_path else None
    if not clip:
        say(f"{name}: missing {path}")
        continue
    length = clip.get_editor_property("sequence_length")
    say(f"{name}: {path} {length:.3f} s")
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    prev = None
    steps = int(length * 30)
    for i in range(steps + 1):
        t = min(i / 30.0, length - 0.001)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, t, options)
        g = lambda b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, WORLD).translation  # noqa: E731
        hl, hr, sp = g("hand_l"), g("hand_r"), g("spine_03")
        rel = hl - hr
        speed = (hl - prev).length() * 30.0 if prev is not None else 0.0
        prev = hl
        say(f"  t={t:5.2f} hand_l-hand_r ({rel.x:6.1f},{rel.y:6.1f},{rel.z:6.1f}) |{rel.length():5.1f}| hand_r-spine ({(hr - sp).x:5.1f},{(hr - sp).y:5.1f},{(hr - sp).z:5.1f}) "
            f"hand_l speed {speed:5.0f} cm/s")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "measure_shotgun_reload.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
