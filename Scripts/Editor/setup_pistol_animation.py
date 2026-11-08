"""Pistol weapon layer clips (the whole pistol, 2026-11-08): the user's Fab pack "Pistol and Rifle Locomotion Animations 1700" (GASP-style
motion-matching set on the UEFN mannequin; in the project only its mesh was imported, the animations sit as FBX in the Epic launcher's vault cache,
read only) for the relaxed carry - stand / crouch idle, walk / run / crouch / sprint loops - and the Lyra mannequin pistol set already in the
project (/Game/Characters/Mannequins/Anims/Pistol: aim idle, aim walk / jog 8-way, fire, reload, equip) for the aimed pistol. The Fab pack has no
aim / fire / reload / equip clips.

1. Imports the chosen Fab FBX clips onto the project's UE5 mannequin skeleton /Game/Characters/Mannequins/Meshes/SK_Mannequin (the UEFN mannequin's
   bones are a superset by name) -> /Game/Characters/Animations/Pistol/Source/ (root motion off: the layer plays them in place).
2. Measures each loop's travel direction (root bone, first -> last frame) and maps the loops onto the layer's 8 directions.
3. IK-retargets every clip the layer uses onto the operatives' UE4 skeletons (the M4 pack's RTG_UE5Manny_UE4Manny, "Blend to Source" off in memory,
   as the shotgun / cold idles) -> /Game/Characters/Animations/Pistol/{Male,Female}/; PMC fighters (Manny hierarchy) play the Source / Lyra clips.
4. Writes the "pistol" block for Content/Data/Weapons/weapon_anim_sets.json into Saved/Logs/setup_pistol_animation.json.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
CODEX_PISTOL_RESET=1 re-imports / re-retargets existing copies. Report: Saved/Logs/setup_pistol_animation.txt
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
RESET = os.environ.get("CODEX_PISTOL_RESET") == "1"
VAULT = r"C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary/Pistol_and_Rifle_Locomotion_Animations_1700-509f8c0a/fbx/gaspfix_extracted/GaspFix/_FixedPistol"
SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
SOURCE_MESH = {"He": "/Game/ShotgunLocomotionPack/Demo/Characters/Mannequins/Meshes/SKM_Manny",
               "She": "/Game/ShotgunLocomotionPack/Demo/Characters/Mannequins/Meshes/SKM_Quinn"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
OUT_SRC = "/Game/Characters/Animations/Pistol/Source"
OUT = {"He": "/Game/Characters/Animations/Pistol/Male", "She": "/Game/Characters/Animations/Pistol/Female"}
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
LYRA = "/Game/Characters/Mannequins/Anims/Pistol/"
PMC_SKELETON = "/Game/Characters/Enemies/PMC/Operator/SK_PmcOperator_Skeleton"
FAB = {  # the relaxed carry (FBX stem -> folder)
    "M_Neutral_Stand_Idle_Loop_Pistol": "Idle", "M_Neutral_Crouch_Idle_Loop_Pistol": "Idle", "M_Neutral_Sprint_Loop_F_Pistol": "Sprint",
}
for kind, folder in (("Walk", "Walk"), ("Run", "Run"), ("Crouch", "Crouch")):
    for d in ("F", "FL", "FR", "B", "BL", "BR", "LL", "RL"):  # LL / RL: the strafes (LR / RR are the same strafes off the other foot)
        FAB[f"M_Neutral_{kind}_Loop_{d}_Pistol"] = folder
LYRA_CLIPS = ["MF_Pistol_Idle_ADS", "MM_Pistol_Fire", "MM_Pistol_Reload", "MM_Pistol_Equip"] + [
    f"{folder}/MF_Pistol_{folder}_{d}" for folder in ("Walk", "Jog") for d in ("Fwd", "Fwd_Right", "Right", "Bwd_Right", "Bwd", "Bwd_Left", "Left", "Fwd_Left")]
DIRS = ("fwd", "fwd_right", "right", "bwd_right", "bwd", "bwd_left", "left", "fwd_left")
log = []


def say(text):
    log.append(text)
    unreal.log("setup_pistol_animation: " + text)


def load(path):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


skeleton = load(SKELETON)
pmc = load(PMC_SKELETON)
if pmc and skeleton:
    try:
        compat = [str(s.get_path_name()) for s in pmc.get_editor_property("compatible_skeletons")]
    except Exception as e:  # noqa: BLE001
        compat = [f"? {e}"]
    say(f"PMC skeleton compatible skeletons: {compat}")

# 1. Import the Fab clips.
sources = {}
for stem, folder in sorted(FAB.items()):
    fbx = os.path.join(VAULT, folder, stem + ".FBX")
    dest = f"{OUT_SRC}/{stem}"
    if lib.does_asset_exist(dest) and not RESET:
        sources[stem] = lib.load_asset(dest)
        continue
    if not os.path.exists(fbx):
        say(f"  {stem}: no FBX")
        continue
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_as_skeletal = True
    ui.import_materials = False
    ui.import_textures = False
    ui.skeleton = skeleton
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    task = unreal.AssetImportTask()
    task.filename = fbx
    task.destination_path = OUT_SRC
    task.destination_name = stem
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.options = ui
    try:
        tools.import_asset_tasks([task])
    except RuntimeError as e:  # the UEFN mannequin's extra bones (e.g. "attach") are not on SK_Mannequin: their tracks are dropped
        say(f"  {stem}: {str(e).split(':')[-1].strip()} (track dropped)")
    anims = [lib.load_asset(p) for p in task.imported_object_paths]
    if not anims and lib.does_asset_exist(dest):
        anims = [lib.load_asset(dest)]
    anims = [a for a in anims if isinstance(a, unreal.AnimSequence)]
    if not anims:
        say(f"  {stem}: import FAILED")
        continue
    clip = anims[0]
    clip.set_editor_property("enable_root_motion", False)
    lib.save_loaded_asset(clip, False)
    sources[stem] = clip
say(f"{len(sources)} / {len(FAB)} Fab clips on {SKELETON} in {OUT_SRC}")

# 2. Directions from the root travel.
WORLD = unreal.AnimPoseSpaces.WORLD


def travel(clip):
    length = clip.get_editor_property("sequence_length")
    opt = unreal.AnimPoseEvaluationOptions()
    opt.set_editor_property("extract_root_motion", True) if hasattr(opt, "extract_root_motion") else None
    a = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, 0.0, opt)
    b = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, length - 0.001), opt)
    pa = unreal.AnimPoseExtensions.get_bone_pose(a, "root", WORLD).translation
    pb = unreal.AnimPoseExtensions.get_bone_pose(b, "root", WORLD).translation
    d = pb - pa
    if d.length() < 1.0:  # in place: the pelvis instead
        pa = unreal.AnimPoseExtensions.get_bone_pose(a, "pelvis", WORLD).translation
        pb = unreal.AnimPoseExtensions.get_bone_pose(b, "pelvis", WORLD).translation
        d = pb - pa
    return d, length


chosen = {}
for kind in ("Walk", "Run", "Crouch"):
    best = {}
    for d in ("F", "FL", "FR", "B", "BL", "BR", "LL", "RL"):
        stem = f"M_Neutral_{kind}_Loop_{d}_Pistol"
        clip = sources.get(stem)
        if not clip:
            continue
        v, length = travel(clip)
        speed = math.hypot(v.x, v.y) / max(length, 0.01)
        # UE mannequin component space: forward = +Y, his LEFT = +X (the pack's "LL" / "LR" left strafes travel +X); yaw clockwise from forward.
        yaw = math.degrees(math.atan2(-v.x, v.y)) % 360.0
        index = int(round(yaw / 45.0)) % 8
        err = abs(((yaw - index * 45.0) + 180.0) % 360.0 - 180.0)
        say(f"  {stem}: travel ({v.x:.0f}, {v.y:.0f}) over {length:.2f} s = {speed:.0f} cm/s, yaw {yaw:.0f} -> {DIRS[index]} (off {err:.0f})")
        if index not in best or err < best[index][1]:
            best[index] = (stem, err, speed)
    chosen[kind] = {DIRS[i]: best[i] for i in best}
    say(f"{kind}: {sorted((k, v[0], round(v[2])) for k, v in chosen[kind].items())}")

# 3. Retarget everything the layer uses.
retargeter = load(RETARGETER)
if retargeter:
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    index = controller.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        controller.set_retarget_op_enabled(index, False)
used_src = {"M_Neutral_Stand_Idle_Loop_Pistol", "M_Neutral_Crouch_Idle_Loop_Pistol", "M_Neutral_Sprint_Loop_F_Pistol"}
for kind in chosen.values():
    used_src.update(v[0] for v in kind.values())
clip_paths = {stem: f"{OUT_SRC}/{stem}" for stem in used_src if stem in sources}
for rel in LYRA_CLIPS:
    path = LYRA + rel
    clip_paths[rel.split("/")[-1]] = path
registry = unreal.AssetRegistryHelpers.get_asset_registry()
for gender in ("He", "She"):
    src_mesh, dst_mesh = load(SOURCE_MESH[gender]), load(TARGET_MESH[gender])
    todo = []
    for name, path in sorted(clip_paths.items()):
        if lib.does_asset_exist(f"{OUT[gender]}/{name}") and not RESET:
            continue
        data = registry.get_asset_by_object_path(f"{path}.{name}")
        if data.is_valid():
            todo.append(data)
        else:
            say(f"  {gender}: no {path}")
    if todo and retargeter and src_mesh and dst_mesh:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(todo, src_mesh, dst_mesh, retargeter, target_path=OUT[gender],
                                                                         include_referenced_assets=False, overwrite_existing_files=True)
        for data in result:
            asset = data.get_asset()
            if isinstance(asset, unreal.AnimSequence):
                asset.set_editor_property("enable_root_motion", False)
                lib.save_loaded_asset(asset, False)
    have = [n for n in clip_paths if lib.does_asset_exist(f"{OUT[gender]}/{n}")]
    say(f"{gender}: {len(have)} / {len(clip_paths)} copies in {OUT[gender]}")


# 4. The "pistol" block.
def obj(path):
    name = path.rsplit("/", 1)[-1]
    return f"{path}.{name}"


def src(stem):
    return obj(f"{OUT_SRC}/{stem}")


block = {
    "retargeted": {"UE4_Mannequin_Skeleton": OUT["He"], "Female_Soldier_Skeleton": OUT["She"]},
    "stand_idle": src("M_Neutral_Stand_Idle_Loop_Pistol"),
    "aim_idle": obj(LYRA + "MF_Pistol_Idle_ADS"),
    "crouch_idle": src("M_Neutral_Crouch_Idle_Loop_Pistol"),
    "sprint": src("M_Neutral_Sprint_Loop_F_Pistol"),
    "walk": {k: src(v[0]) for k, v in chosen.get("Walk", {}).items()},
    "jog": {k: src(v[0]) for k, v in chosen.get("Run", {}).items()},
    "crouch": {k: src(v[0]) for k, v in chosen.get("Crouch", {}).items()},
    "aim_walk": {d: obj(LYRA + "Walk/MF_Pistol_Walk_" + n) for d, n in zip(DIRS, ("Fwd", "Fwd_Right", "Right", "Bwd_Right", "Bwd", "Bwd_Left", "Left", "Fwd_Left"))},
    "fire_aim": obj(LYRA + "MM_Pistol_Fire"),
    "fire_hip": obj(LYRA + "MM_Pistol_Fire"),
    "reload": obj(LYRA + "MM_Pistol_Reload"),
    "equip": obj(LYRA + "MM_Pistol_Equip"),
    "walk_speed": round(sum(v[2] for v in chosen.get("Walk", {}).values()) / max(1, len(chosen.get("Walk", {})))) or 150,
    "jog_speed": round(sum(v[2] for v in chosen.get("Run", {}).values()) / max(1, len(chosen.get("Run", {})))) or 350,
    "crouch_speed": round(sum(v[2] for v in chosen.get("Crouch", {}).values()) / max(1, len(chosen.get("Crouch", {})))) or 120,
}
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_pistol_animation.json"), "w", encoding="utf-8") as f:
    json.dump(block, f, indent=2)
say("done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_pistol_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
