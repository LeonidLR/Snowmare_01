"""#51 measurement (read-only, nothing saved): how far the torso / head / gun hand travel during the rifle fire clips on the PMC Operator body
vs the operative body, with the PMC skeleton's current translation retargeting and with the operative-style modes (applied IN MEMORY only).

For each fire clip of ABP_Operative's FireMontage / FireAimMontage (+ the crouch / idle reference clips if found): sample 30 poses, component
space, each bone relative to the pelvis; report the travel range (max distance from the clip's first pose) of spine_03 / head / hand_r and
the pelvis itself.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/measure_pmc_fire_recoil.txt
"""
import os

import unreal

lib = unreal.OperativeAnimGraphLibrary
assets = unreal.EditorAssetLibrary
WORLD = unreal.AnimPoseSpaces.WORLD
PMC_SKELETON = "/Game/Characters/Enemies/PMC/Operator/SK_PmcOperator_Skeleton"
PMC_MESH = "/Game/Characters/Enemies/PMC/Operator/SKM_PmcOperator"
OPERATIVE_MESH = "/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/SK_Post_Apo_Survivor_a_1slot"
BONES = ("spine_03", "head", "hand_r")
log = []


def say(text):
    log.append(text)
    unreal.log("measure_pmc_fire_recoil: " + text)


def clips_of(montage):
    out = []
    if not montage:
        return out
    for track in montage.get_editor_property("slot_anim_tracks"):
        for seg in track.get_editor_property("anim_track").get_editor_property("anim_segments"):
            ref = seg.get_editor_property("anim_reference")
            if ref and ref not in out:
                out.append(ref)
    return out


def travel(clip, mesh):
    length = clip.get_editor_property("sequence_length")
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    first, worst = None, {b: 0.0 for b in BONES + ("pelvis",)}
    worst["spine_len"] = 0.0
    for i in range(31):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(length * i / 30.0, length - 0.001), options)
        g = lambda b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, WORLD).translation  # noqa: E731
        pelvis = g("pelvis")
        cur = {b: g(b) - pelvis for b in BONES}
        cur["pelvis"] = pelvis
        if first is None:
            first = cur
            worst["spine_len"] = cur["spine_03"].length()
            continue
        for b in cur:
            worst[b] = max(worst[b], (cur[b] - first[b]).length())
    return worst


abp = unreal.load_object(None, "/Game/Characters/Operatives/ABP_Operative.ABP_Operative_C")
cdo = unreal.get_default_object(abp) if abp else None
clips = []
for prop in ("fire_montage", "fire_aim_montage"):
    try:
        m = cdo.get_editor_property(prop) if cdo else None
    except Exception as e:  # noqa: BLE001
        m = None
        say(f"{prop}: {e}")
    say(f"{prop}: {m.get_path_name() if m else None}")
    clips += [c for c in clips_of(m) if c not in clips]
for prop in ("hit_stand_animation", "hit_crouch_animation", "hit_back_animation", "reload_animation"):
    try:
        c = cdo.get_editor_property(prop) if cdo else None
    except Exception as e:  # noqa: BLE001
        c = None
        say(f"{prop}: {e}")
    say(f"{prop}: {c.get_path_name() if c else None}")
    if c and c not in clips:
        clips.append(c)
pmc_mesh, op_mesh, skel = assets.load_asset(PMC_MESH), assets.load_asset(OPERATIVE_MESH), assets.load_asset(PMC_SKELETON)
say(f"meshes: pmc {pmc_mesh is not None}, operative {op_mesh is not None}; clips {[c.get_name() for c in clips]}")


def row(tag):
    for c in clips:
        for name, mesh in (("operative", op_mesh), ("pmc", pmc_mesh)):
            if tag != "current" and name == "operative":
                continue
            w = travel(c, mesh)
            say(f"  [{tag}] {c.get_name()} on {name}: travel vs pelvis spine_03 {w['spine_03']:.1f} head {w['head']:.1f} hand_r {w['hand_r']:.1f} cm, "
                f"pelvis {w['pelvis']:.1f} cm; spine_03 above pelvis at t0 {w['spine_len']:.1f} cm")


row("current")
if skel:
    lines = lib.describe_skeleton(skel).splitlines()
    names = [l.split(" ")[1] for l in lines if l]
    modes = {}
    for l in lines:
        modes.setdefault(l.split(" ")[3], []).append(l.split(" ")[1])
    say("PMC skeleton modes now: " + ", ".join(f"{k}: {len(v)}" for k, v in modes.items()))
    keep = [n for n in names if n in ("root", "root_inner") or n.startswith("ik_")]
    lib.set_bone_translation_retargeting(skel, [n for n in names if n not in keep and n != "pelvis"], 1, False)
    lib.set_bone_translation_retargeting(skel, ["pelvis"], 2, False)
    lib.set_bone_translation_retargeting(skel, keep, 0, False)
    row("operative-style modes, in memory")
say("nothing saved")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "measure_pmc_fire_recoil.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
