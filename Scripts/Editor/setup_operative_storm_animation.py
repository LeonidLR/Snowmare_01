"""Blizzard walk upper body (game designer 2026-10-22): /Game/90+_Weather_Animations NEW_version/Storm, set Storm_1.

The designer: in a blizzard the operatives walk with the arms stretched out against the wind - the storm clips of the same pack
the cold idles came from, on the UPPER BODY. Measured (Scripts/Editor/measure_storm_clips.py, 2026-10-22): the storm clips stand
in place (no root motion, the loops' feet do not move: 0 cm fore-aft swing; 1Start / 6End take one step into / out of the
stance), the loops hold both hands in front of the face - Storm_1 He 2Loop / 4Loop: hands 54 / 47 cm in front of spine_03 and
45 / 27 cm above it; She 42 / 58 cm, 46 / 44 cm up; the transits keep them there (55 / 56 cm). Storm_2 crouches lower (pelvis He 85,
She 76 cm) and one hand hangs back in its 2Loop, so Storm_1 is the walk overlay: its loops + transits only (Start / End are not
needed: the layer blends in / out over 0.3 s while walking).

Retarget exactly like the cold idles (Scripts/Editor/setup_operative_weather_animation.py: the pack is on its own UE5 Manny
skeleton; bone-name playback on the UE4 Post_Apo skeleton bends the clavicles / spine wrongly): the M4 pack's RTG_UE5Manny_UE4Manny
with its "Blend to Source" op switched off IN MEMORY (never saved) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Operatives/Anims/Weather/Storm/Male/AS_He_Storm_1_{2Loop,3Tranzit,4Loop,5Tranzit}
  female: SK_Female_soldier      -> /Game/Characters/Operatives/Anims/Weather/Storm/Female/AS_She_Storm_1_{...}
No skeleton, ABP or pack asset is modified or saved: UOperativeAnimInstance loads the copies by path (BlizzardWalk* class defaults
empty). Only the new copies are saved.

Run with the editor of this checkout closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
CODEX_STORM_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_STORM_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_operative_storm_animation.txt
"""
import os

import unreal

library = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_STORM_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_STORM_CLIPS") == "1"
PACK = "/Game/90+_Weather_Animations/Animations/NEW_version/Storm/"
PACK_MESHES = "/Game/90+_Weather_Animations/Demo/Characters/Mannequins/Meshes/"
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Operatives/Anims/Weather/Storm/Male", "She": "/Game/Characters/Operatives/Anims/Weather/Storm/Female"}
SOURCE_MESH = {"He": PACK_MESHES + "SKM_Manny", "She": PACK_MESHES + "SKM_Quinn"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
SET = "Storm_1"
TAGS = ("2Loop", "3Tranzit", "4Loop", "5Tranzit")
WALK_REFERENCE = "/Game/RifleAnims"  # the operative's rifle walk on the same mesh: how far forward the hands are normally
WORLD = unreal.AnimPoseSpaces.WORLD
log = []


def say(text):
    log.append(text)
    unreal.log("setup_operative_storm_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), options)


def sample(clip, mesh=None, forward_axis="y"):
    """Mid-clip pelvis / spine_03 / head / hands (component space, cm) and the hands' mean forward / up offset from spine_03."""
    length = clip.get_editor_property("sequence_length")
    g = lambda p, b: unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation  # noqa: E731
    fwd = lambda v: getattr(v, forward_axis)  # noqa: E731
    poses = [pose_at(clip, length * i / 8.0, mesh) for i in range(9)]
    hands_fwd = sum(fwd(g(p, h)) - fwd(g(p, "spine_03")) for p in poses for h in ("hand_l", "hand_r")) / 18.0
    hands_up = sum(g(p, h).z - g(p, "spine_03").z for p in poses for h in ("hand_l", "hand_r")) / 18.0
    mid = poses[4]
    lowest = min(min(g(p, "foot_l").z, g(p, "foot_r").z) for p in poses)
    return {"pelvis": g(mid, "pelvis"), "head": g(mid, "head"), "hand_l": g(mid, "hand_l"), "hand_r": g(mid, "hand_r"),
            "spine_03": g(mid, "spine_03"), "hands_fwd": hands_fwd, "hands_up": hands_up, "foot": lowest}


def fmt(v):
    return f"({v.x:.0f},{v.y:.0f},{v.z:.0f})"


registry = unreal.AssetRegistryHelpers.get_asset_registry()
clips = {str(d.asset_name): d for d in registry.get_assets_by_path(PACK.rstrip("/"), recursive=False)}
say(f"{len(clips)} clips in {PACK}")
retargeter = load(RETARGETER)
if retargeter:
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    index = controller.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        controller.set_retarget_op_enabled(index, False)
        say("RTG_UE5Manny_UE4Manny: op 'Blend to Source' disabled in memory for this batch (asset not saved)")

worst = 0.0
for gender in ("He", "She"):
    source_mesh, target_mesh = load(SOURCE_MESH[gender]), load(TARGET_MESH[gender])
    wanted = [f"AS_{gender}_{SET}_{tag}" for tag in TAGS]
    retargeted, todo = {}, []
    for name in wanted:
        if name not in clips:
            say(f"  {name}: not in the pack")
            continue
        dest = f"{OUT[gender]}/{name}"
        if library.does_asset_exist(dest) and not RESET:
            retargeted[name] = library.load_asset(dest)
        else:
            todo.append(clips[name])
    if todo and retargeter and source_mesh and target_mesh:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False,
            overwrite_existing_files=True)
        for data in result:
            asset = data.get_asset()
            if isinstance(asset, unreal.AnimSequence):
                retargeted[str(data.asset_name)] = asset
                if not DRY:
                    library.save_loaded_asset(asset, False)
        say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}{' (in memory)' if DRY else ''}")
    for name in wanted:
        new = retargeted.get(name)
        if not new:
            say(f"  {name}: NOT retargeted")
            continue
        a, b = sample(clips[name].get_asset()), sample(new, target_mesh)
        err = max((a[k] - a["pelvis"] - (b[k] - b["pelvis"])).length() for k in ("head", "hand_l", "hand_r"))
        worst = max(worst, err)
        say(f"  {name}: {new.get_editor_property('sequence_length'):.2f} s, root motion {new.get_editor_property('enable_root_motion')}; "
            f"pack hands fwd {a['hands_fwd']:.0f} up {a['hands_up']:.0f} | retargeted hands fwd {b['hands_fwd']:.0f} up {b['hands_up']:.0f}, "
            f"pelvis {fmt(b['pelvis'])} head {fmt(b['head'])} hands {fmt(b['hand_l'])} {fmt(b['hand_r'])} | posture error {err:.0f} cm")
    # Reference: the operative's normal rifle walk on the same mesh (the smoke compares the hands live).
    walk = [d for d in registry.get_assets_by_path(WALK_REFERENCE, recursive=True) if "Walk_Fwd" in str(d.asset_name)]
    if walk and target_mesh:
        w = sample(walk[0].get_asset(), target_mesh)
        say(f"  reference {walk[0].asset_name} on {target_mesh.get_name()}: hands fwd {w['hands_fwd']:.0f} up {w['hands_up']:.0f}")
say(f"worst posture error (head / hands vs pelvis, pack vs retargeted): {worst:.0f} cm")
say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_storm_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
