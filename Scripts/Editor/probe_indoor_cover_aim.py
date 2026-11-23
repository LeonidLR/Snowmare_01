"""Read-only probe of the Riffle cover pack aim poses (head / hands / rifle axis) -> Saved/Logs/probe_riffle_aim2.txt.
Used for docs/design/INDOOR_COVER_ANIMATIONS.md section 2. Run like inspect_indoor_cover_pack.py (no editor of this checkout open)."""
import math, os, unreal
LOG = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "probe_riffle_aim2.txt")
B = "/Game/Riffle_Cover_Animation/Animations/"
clips = ["Crouch/Left/AS_Cover_CrouchL_Idle", "Crouch/Left/Aim/2/AS_Cover_CrouchL_Aim2_Idle", "Crouch/Left/Aim/2/Offset/AS_Cover_CrouchL_Aim2_UM",
 "Crouch/Left/Aim/2/Offset/AS_Cover_CrouchL_Aim2_DM", "Crouch/Left/Aim/1/AS_Cover_CrouchL_Aim1_Idle", "Crouch/Left/Aim/1/Offset/AS_Cover_CrouchL_Aim1_LM",
 "Crouch/Left/Aim/1/Offset/AS_Cover_CrouchL_Aim1_RM", "Stand/Left/Aim/AS_Cover_StandL_Aim_Idle", "Stand/Left/Aim/Offset/AS_Cover_StandL_Aim_LM", "Stand/Left/Aim/Offset/AS_Cover_StandL_Aim_RM",
 "Stand/Left/Aim/Offset/AS_Cover_StandL_Aim_LU", "Stand/Left/Aim/Offset/AS_Cover_StandL_Aim_LD", "Stand/Left/AS_Cover_StandL_Idle", "Stand/Right/AS_Cover_StandR_Idle"]
out = []
W = unreal.AnimPoseSpaces.WORLD
for c in clips:
    s = unreal.EditorAssetLibrary.load_asset(B + c)
    p = unreal.AnimPoseExtensions.get_anim_pose_at_time(s, 0.0, unreal.AnimPoseEvaluationOptions())
    g = lambda n: unreal.AnimPoseExtensions.get_bone_pose(p, n, W)
    h, hr, hl, sp = g("head"), g("hand_r"), g("hand_l"), g("spine_05")
    # rifle axis ~ from right hand to left hand
    d = hl.translation - hr.translation
    L = max(d.length(), 0.01)
    yaw = math.degrees(math.atan2(d.y, d.x)); pitch = math.degrees(math.asin(max(-1, min(1, d.z / L))))
    hf = h.rotation.rotate_vector(unreal.Vector(0, 1, 0))  # UE5 manny head: +Y local ~ forward? report both
    hx = h.rotation.rotate_vector(unreal.Vector(1, 0, 0))
    out.append("%s: head z %.0f, hand_r z %.0f (dz to head %+.0f), hand_l z %.0f, rifle(hr->hl) yaw %.0f pitch %.0f, headY->(%.2f,%.2f,%.2f) headX->(%.2f,%.2f,%.2f), spine5 %s" % (
        c.split('/')[-1], h.translation.z, hr.translation.z, hr.translation.z - h.translation.z, hl.translation.z, yaw, pitch, hf.x, hf.y, hf.z, hx.x, hx.y, hx.z,
        "(%.0f,%.0f,%.0f)" % (sp.translation.x, sp.translation.y, sp.translation.z)))
open(LOG, "w").write("\n".join(out) + "\n")
