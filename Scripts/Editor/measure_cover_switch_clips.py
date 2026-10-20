"""Measures the M4 Cover Pack's run-in-to-cover clips for the wall-edge cover switch (game designer 2026-10-20).

Read-only: loads the pack's clips (user content, NOT modified) and writes, per clip, its length, root-motion flags and the
root / pelvis tracks (component space, every 0.1 s: position + yaw of the pelvis' forward axis) to
Saved/Logs/measure_cover_switch_clips.txt. The numbers decide the clip lead (where the clip's first frame stands relative to its
last one, i.e. to the cover slot) and the approach / facing semantics of the _L / _R variants
(AOperativeCharacter::CoverSwitch* tunables, Tactics/CoverSwitchRules).

Run with no editor of THIS checkout open:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import math
import os
import unreal

LOG = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "measure_cover_switch_clips.txt")
BASE = "/Game/M4_Cover_Pack/Animations/"
CLIPS = [
    "stand/anim_M4_std_idle_fwd_to_cvr_std_idle_L",
    "stand/anim_M4_std_idle_fwd_to_cvr_std_idle_R",
    "stand/anim_M4_std_jog_fwd_to_cvr_std_idle_L",
    "stand/anim_M4_std_jog_fwd_to_cvr_std_idle_R",
    "stand/anim_M4_std_jog_fwd_L_to_cvr_std_idle_L",
    "stand/anim_M4_std_jog_fwd_L_to_cvr_std_idle_R",
    "stand/anim_M4_std_jog_fwd_R_to_cvr_std_idle_L",
    "stand/anim_M4_std_jog_fwd_R_to_cvr_std_idle_R",
    "stand/anim_M4_std_sprint_fwd_L_to_cvr_std_idle_L",
    "stand/anim_M4_std_sprint_fwd_R_to_cvr_std_idle_R",
    "stand/anim_M4_cvr_std_idle_L",
    "stand/anim_M4_cvr_std_idle_R",
    "crouch/anim_M4_std_jog_fwd_to_cvr_crch_idle_L",
    "crouch/anim_M4_std_jog_fwd_to_cvr_crch_idle_R",
    "crouch/anim_M4_std_jog_fwd_L_to_cvr_crch_idle_L",
    "crouch/anim_M4_std_jog_fwd_L_to_cvr_crch_idle_R",
    "crouch/anim_M4_std_jog_fwd_R_to_cvr_crch_idle_L",
    "crouch/anim_M4_std_jog_fwd_R_to_cvr_crch_idle_R",
    "crouch/anim_M4_crch_idle_fwd_to_cvr_crch_idle_L",
    "crouch/anim_M4_cvr_crch_idle_L",
]
out = []


def pose(seq, bone, t):
    return unreal.AnimationLibrary.get_bone_pose_for_time(seq, bone, t, False)


def compose(parent, child):
    """Component-space child = parent * child (UE order: child first)."""
    return child.multiply(parent)


def yaw_of(rot_quat, axis):
    v = rot_quat.rotate_vector(axis)
    return math.degrees(math.atan2(v.y, v.x)), v


for rel in CLIPS:
    path = BASE + rel
    name = rel.split("/")[-1]
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        out.append(f"MISSING {path}")
        continue
    seq = unreal.EditorAssetLibrary.load_asset(path)
    length = seq.get_play_length()
    rm = seq.get_editor_property("enable_root_motion")
    lock = seq.get_editor_property("root_motion_root_lock")
    force = seq.get_editor_property("force_root_lock")
    out.append(f"== {name}: length {length:.2f} s, enable_root_motion {rm}, lock {lock}, force_root_lock {force}")
    rows = []
    t = 0.0
    steps = max(1, int(round(length / 0.1)))
    for i in range(steps + 1):
        t = min(length, i * 0.1)
        root = pose(seq, "root", t)
        pelvis_local = pose(seq, "pelvis", t)
        pelvis = compose(root, pelvis_local)
        r = root.translation
        p = pelvis.translation
        # The mannequin-style skeleton faces +Y in component space; report the pelvis yaw of its +Y-ish axis and the root yaw.
        root_yaw = root.rotation.rotator().yaw
        pel_fwd_yaw, _ = yaw_of(pelvis.rotation, unreal.Vector(0.0, 1.0, 0.0))
        pel_x_yaw, _ = yaw_of(pelvis.rotation, unreal.Vector(1.0, 0.0, 0.0))
        rows.append((t, r.x, r.y, r.z, root_yaw, p.x, p.y, p.z, pel_fwd_yaw, pel_x_yaw))
    for row in rows:
        out.append("  t=%.2f root (%.1f, %.1f, %.1f) yaw %.1f | pelvis (%.1f, %.1f, %.1f) axisY-yaw %.1f axisX-yaw %.1f" % row)
    first, last = rows[0], rows[-1]
    out.append("  root travel (%.1f, %.1f) = %.1f cm, root yaw %.1f -> %.1f; pelvis start-minus-end (%.1f, %.1f) = %.1f cm" % (
        last[1] - first[1], last[2] - first[2], math.hypot(last[1] - first[1], last[2] - first[2]), first[4], last[4],
        first[5] - last[5], first[6] - last[6], math.hypot(first[5] - last[5], first[6] - last[6])))
    # Pelvis speed over the first 0.3 s (the jog's pace the clip starts at).
    if len(rows) >= 4:
        d = math.hypot(rows[3][5] - rows[0][5], rows[3][6] - rows[0][6]) + math.hypot(rows[3][1] - rows[0][1], rows[3][2] - rows[0][2])
        out.append("  pelvis+root speed first 0.3 s ~ %.0f cm/s" % (d / max(rows[3][0] - rows[0][0], 0.01)))

os.makedirs(os.path.dirname(LOG), exist_ok=True)
with open(LOG, "w", encoding="utf-8") as handle:
    handle.write("\n".join(out) + "\n")
