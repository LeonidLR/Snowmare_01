"""Shotgun weapon layer clips (designer decision 21, 2026-11-03; implemented 2026-11-08): retarget the user's ShotgunLocomotionPack.

The pack is on its own UE5 Manny skeleton (SK_Mannequin, 161 bones, spine_01..05, clavicles on spine_05). The PMC operator skeleton has the
same hierarchy (+ root_inner), so PMC fighters play the pack clips by bone name directly. The operatives' UE4 skeletons
(UE4_Mannequin_Skeleton 71 bones, Female_Soldier_Skeleton 68 bones: spine_01..03, clavicles on spine_03) do not: bone-name playback bends
the chest / clavicles so the aim pose points the gun sideways (render 2026-11-08). So the clips are IK-retargeted exactly like the cold idles /
storm walk (Scripts/Editor/setup_operative_weather_animation.py): the M4 pack's RTG_UE5Manny_UE4Manny with its "Blend to Source" op switched
off IN MEMORY (never saved) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/Shotgun/Male/<pack clip name>
  female: SK_Female_soldier      -> /Game/Characters/Animations/Shotgun/Female/<pack clip name>
The clip list is every path in Content/Data/Weapons/weapon_anim_sets.json "shotgun"; that file's "retargeted" map (skeleton name -> folder)
tells UOperativeAnimInstance to load the copy for its skeleton (pack clip otherwise). No skeleton, ABP or pack asset is modified or saved.

Run with no editor / build of this checkout running:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
CODEX_SHOTGUN_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_SHOTGUN_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_shotgun_animation.txt
"""
import json
import os

import unreal

library = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_SHOTGUN_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_SHOTGUN_CLIPS") == "1"
PACK_MESHES = "/Game/ShotgunLocomotionPack/Demo/Characters/Mannequins/Meshes/"
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Animations/Shotgun/Male", "She": "/Game/Characters/Animations/Shotgun/Female"}
SOURCE_MESH = {"He": PACK_MESHES + "SKM_Manny", "She": PACK_MESHES + "SKM_Quinn"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
MEASURE = ("anim_shotgun_stand_idle", "anim_shotgun_aim_idle", "anim_shotgun_aim_fire_01", "anim_shotgun_aim_reload",
           "anim_shotgun_walk_fwd_loop", "anim_shotgun_aim_walk_fwd_loop", "anim_shotgun_crouch_idle")
WORLD = unreal.AnimPoseSpaces.WORLD
log = []


def say(text):
    log.append(text)
    unreal.log("setup_shotgun_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def pack_paths():
    """Every clip path of the "shotgun" block (object paths 'Pkg.Name' -> package paths)."""
    with open(os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapon_anim_sets.json"), encoding="utf-8") as f:
        block = json.load(f)["shotgun"]
    out = []
    for value in block.values():
        values = value.values() if isinstance(value, dict) else [value]
        for v in values:
            if isinstance(v, str) and v.startswith("/Game/ShotgunLocomotionPack/"):
                out.append(v.split(".")[0])
    return sorted(set(out))


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), options)


def sample(clip, mesh=None):
    """Mid-clip component-space pelvis / head / hands / ik_hand_gun-free gun line: right hand -> left hand direction (the gun axis)."""
    length = clip.get_editor_property("sequence_length")
    p = pose_at(clip, length * 0.5, mesh)
    g = lambda b: unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation  # noqa: E731
    return {"pelvis": g("pelvis"), "head": g("head"), "hand_l": g("hand_l"), "hand_r": g("hand_r")}


def fmt(v):
    return f"({v.x:.0f},{v.y:.0f},{v.z:.0f})"


def gun_dir(s):
    d = s["hand_l"] - s["hand_r"]
    n = d.length()
    return unreal.Vector(d.x / n, d.y / n, d.z / n) if n > 0.1 else unreal.Vector(0, 0, 0)


paths = pack_paths()
say(f"{len(paths)} pack clips listed in weapon_anim_sets.json")
retargeter = load(RETARGETER)
if retargeter:
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    index = controller.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        controller.set_retarget_op_enabled(index, False)
        say("RTG_UE5Manny_UE4Manny: op 'Blend to Source' disabled in memory for this batch (asset not saved)")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
worst = 0.0
for gender in ("He", "She"):
    source_mesh, target_mesh = load(SOURCE_MESH[gender]), load(TARGET_MESH[gender])
    retargeted, todo = {}, []
    for path in paths:
        name = path.rsplit("/", 1)[1]
        dest = f"{OUT[gender]}/{name}"
        if library.does_asset_exist(dest) and not RESET:
            retargeted[name] = library.load_asset(dest)
            continue
        data = registry.get_asset_by_object_path(f"{path}.{name}")
        if not data.is_valid():
            say(f"  {name}: not in the pack")
            continue
        todo.append(data)
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
    missing = [p.rsplit("/", 1)[1] for p in paths if p.rsplit("/", 1)[1] not in retargeted]
    say(f"{gender}: {len(retargeted)} / {len(paths)} copies; missing: {missing if missing else 'none'}")
    for name in MEASURE:
        new = retargeted.get(name)
        src = next((p for p in paths if p.endswith("/" + name)), None)
        if not new or not src:
            continue
        a, b = sample(load(src)), sample(new, target_mesh)
        err = max((a[k] - a["pelvis"] - (b[k] - b["pelvis"])).length() for k in ("head", "hand_l", "hand_r"))
        worst = max(worst, err)
        ga, gb = gun_dir(a), gun_dir(b)
        dot = ga.x * gb.x + ga.y * gb.y + ga.z * gb.z
        say(f"  {name}: {new.get_editor_property('sequence_length'):.2f} s, root motion {new.get_editor_property('enable_root_motion')}; "
            f"pack head {fmt(a['head'] - a['pelvis'])} hands {fmt(a['hand_l'] - a['pelvis'])} {fmt(a['hand_r'] - a['pelvis'])} | "
            f"retargeted head {fmt(b['head'] - b['pelvis'])} hands {fmt(b['hand_l'] - b['pelvis'])} {fmt(b['hand_r'] - b['pelvis'])} | "
            f"posture error {err:.0f} cm, gun-axis cos {dot:.3f}")
say(f"worst posture error (head / hands vs pelvis, pack vs retargeted): {worst:.0f} cm")
say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_shotgun_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
