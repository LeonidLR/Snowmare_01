"""Builds the Rifle_2 locomotion of the operatives from the user's pack Content/RifleAnims/Rifle_2 (Idle, Walk):

  - Skeletons: the Rifle_2 clips' skeleton and the operative mesh's skeleton (Post_Apo_Survivor, a UE mannequin rig)
    are made compatible both ways (and with the RifleAnims / SK_Mannequin skeletons when they differ).
  - /Game/Characters/Operatives/Anims/BS_Rifle2_Walk: 8-way Direction (-180..180) x Speed blend space — idle on the
    zero row, the walk loops F, FR, RR, BR, B, BL, LL, FL at the measured walk speed (root motion of the F loop).
  - /Game/Characters/Operatives/ABP_Operative_Rifle2: parent UOperativeAnimInstance, bUseRifle2Locomotion, the
    Rifle_2 clip sets (idle loop + breaks, stand turns 45 / 90 / 135 / 180 L / R, 16 starts, 16 stops), the fire /
    reload / hit / death clips copied from ABP_Operative, and the AnimGraph built by
    UOperativeAnimGraphLibrary.BuildRifle2LocomotionGraph (State Machine Idle / IdleBreak / TurnInPlace / WalkStart /
    Walk / WalkStop on the bLoco* flags, Rotate Root Bone, upper-body + full-body slots).
  - BP_Operative is NOT switched to it unless CODEX_ASSIGN_RIFLE2=1 (ABP_Operative stays the game's AnimBP).

Re-running keeps hand edits of the graph (built only while empty or as generated; CODEX_REBUILD_ANIM_GRAPHS=1 forces).
Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_rifle2_animation.txt
"""
import os
import unreal

ROOT = "/Game/Characters/Operatives"
PACK = "/Game/RifleAnims/Rifle_2"
IDLE = f"{PACK}/Idle"
WALK = f"{PACK}/Walk"
library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        log.append(f"MISSING {path}")
    return asset


def make_compatible(a, b):
    if a is None or b is None or a == b:
        return
    compatible = list(a.get_editor_property("compatible_skeletons"))
    if not any(c and c.get_path_name() == b.get_path_name() for c in compatible):
        compatible.append(b)
        a.set_editor_property("compatible_skeletons", compatible)
        library.save_loaded_asset(a)
        log.append(f"{a.get_path_name()} now compatible with {b.get_path_name()}")


def root_speed(anim):
    """Ground speed of a clip's root motion (cm/s); 0 for an in-place clip."""
    try:
        length = anim.get_editor_property("sequence_length") if hasattr(anim, "get_editor_property") else 0.0
    except Exception:
        length = 0.0
    if not length:
        length = unreal.AnimationLibrary.get_sequence_length(anim)
    try:
        start = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False)
        end = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False)
        a, b = start.translation, end.translation
        distance = ((b.x - a.x) ** 2 + (b.y - a.y) ** 2) ** 0.5
        return distance / length if length > 0 else 0.0, length
    except Exception as error:  # API differences: fall back to the default speed
        log.append(f"root motion of {anim.get_name()} not measured: {error}")
        return 0.0, length


# --- skeletons ---
bp_class = unreal.load_object(None, f"{ROOT}/BP_Operative.BP_Operative_C")
bp = library.load_asset(f"{ROOT}/BP_Operative")
cdo = unreal.get_default_object(bp_class)
mesh_component = cdo.get_editor_property("mesh")
skeleton = mesh_component.get_editor_property("skeletal_mesh_asset").get_editor_property("skeleton")
idle_loop = load(f"{IDLE}/M_Neutral_Stand_Idle_Loop_Rifle")
pack_skeleton = idle_loop.get_editor_property("skeleton") if idle_loop else None
log.append(f"operative skeleton {skeleton.get_path_name()}; Rifle_2 skeleton {pack_skeleton.get_path_name() if pack_skeleton else '-'}")
make_compatible(skeleton, pack_skeleton)
make_compatible(pack_skeleton, skeleton)
old_rifle = load("/Game/RifleAnims/Animations/BlendSpaces/Standing_IdleWalkJogRun/BS_Rifle")
if old_rifle:
    rifle_skeleton = old_rifle.get_editor_property("skeleton")
    make_compatible(pack_skeleton, rifle_skeleton)
    make_compatible(rifle_skeleton, pack_skeleton)

# --- clip sets (sector order F, FR, RR, BR, B, BL, LL, FL; feet Lfoot, Rfoot) ---
SECTORS = ("F", "FR", "RR", "BR", "B", "BL", "LL", "FL")
loops = [load(f"{WALK}/M_Neutral_Walk_Loop_{s}_Rifle") for s in SECTORS]
starts = [load(f"{WALK}/M_Neutral_Walk_Start_{s}_{foot}_Rifle") for s in SECTORS for foot in ("Lfoot", "Rfoot")]
stops = [load(f"{WALK}/M_Neutral_Walk_Stop_{s}_{foot}_Rifle") for s in SECTORS for foot in ("Lfoot", "Rfoot")]
turns_left = [load(f"{IDLE}/M_Neutral_Stand_Turn_{deg}_L_Rifle") for deg in ("045", "090", "135", "180")]
turns_right = [load(f"{IDLE}/M_Neutral_Stand_Turn_{deg}_R_Rifle") for deg in ("045", "090", "135", "180")]
breaks = [b for b in (load(f"{IDLE}/M_Neutral_Stand_Idle_Break_v0{i}_Rifle") for i in range(1, 7)) if b]

speed, loop_length = root_speed(loops[0]) if loops[0] else (0.0, 1.1)
# The loop holds several strides: one cycle (two steps) is about 1.1 s at a walk.
cycle = loop_length / max(1, round(loop_length / 1.1)) if loop_length else 1.1
if speed < 30.0:
    log.append(f"walk loop speed {speed:.1f} cm/s looks in-place: using 150")
    speed = 150.0
log.append(f"walk speed {speed:.1f} cm/s, loop {loop_length:.2f} s, cycle {cycle:.2f} s")

# --- the blend space ---
bs_path = f"{ROOT}/Anims/BS_Rifle2_Walk"
if library.does_asset_exist(bs_path):
    blend_space = library.load_asset(bs_path)
else:
    factory = unreal.BlendSpaceFactoryNew()
    factory.set_editor_property("target_skeleton", skeleton)
    blend_space = tools.create_asset("BS_Rifle2_Walk", f"{ROOT}/Anims", unreal.BlendSpace, factory)
result = unreal.OperativeAnimGraphLibrary.fill_directional_blend_space(blend_space, idle_loop, loops, speed)
log.append(f"BS_Rifle2_Walk: {result}")
library.save_loaded_asset(blend_space, False)

# --- the AnimBP ---
abp_path = f"{ROOT}/ABP_Operative_Rifle2"
if library.does_asset_exist(abp_path):
    abp = library.load_asset(abp_path)
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("parent_class", unreal.OperativeAnimInstance)
    abp = tools.create_asset("ABP_Operative_Rifle2", ROOT, unreal.AnimBlueprint, factory)
    log.append(f"created {abp_path}")

node_count = unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp)
GENERATED_NODE_COUNTS = (9,)  # the generated layout (state machine, rotate root, cache / slots, getters)
if os.environ.get("CODEX_REBUILD_ANIM_GRAPHS") == "1" or node_count == 0 or node_count in GENERATED_NODE_COUNTS:
    result = unreal.OperativeAnimGraphLibrary.build_rifle2_locomotion_graph(abp, blend_space, "FullBody", "Fire", "spine_01")
    ok, report = result if isinstance(result, tuple) else ("errors 0" in result, result)
    log.append(f"graph ok={ok}\n{report}")
    log.append(f"graph nodes now {unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp)}")
else:
    log.append(f"graph kept (edited by hand, {node_count} nodes)")

abp_class = unreal.load_object(None, f"{abp_path}.ABP_Operative_Rifle2_C")
anim_cdo = unreal.get_default_object(abp_class)
anim_cdo.set_editor_property("use_native_locomotion", False)
anim_cdo.set_editor_property("use_rifle2_locomotion", True)
anim_cdo.set_editor_property("rifle2_idle_loop", idle_loop)
anim_cdo.set_editor_property("rifle2_idle_breaks", breaks)
anim_cdo.set_editor_property("rifle2_turn_left", turns_left)
anim_cdo.set_editor_property("rifle2_turn_right", turns_right)
anim_cdo.set_editor_property("rifle2_starts", starts)
anim_cdo.set_editor_property("rifle2_stops", stops)
anim_cdo.set_editor_property("rifle2_walk_clip_speed", speed)
anim_cdo.set_editor_property("rifle2_walk_cycle_seconds", max(0.3, cycle))
# Fire / reload / hits / deaths / stance transitions: the same as ABP_Operative (only while unset here).
main_class = unreal.load_object(None, f"{ROOT}/ABP_Operative.ABP_Operative_C")
if main_class:
    main_cdo = unreal.get_default_object(main_class)
    for prop in ("upper_body_slot", "fire_montage", "fire_aim_montage", "reload_animation", "hit_stand_animation", "hit_crouch_animation",
                 "hit_prone_animation", "death_stand_animations", "death_crouch_animation", "death_prone_animation",
                 "stand_to_prone_animation", "prone_to_stand_animation", "crouch_to_prone_animation", "prone_to_crouch_animation",
                 "stand_to_crouch_animation", "crouch_to_stand_animation", "fire_prone_animation", "grenade_throw_walk_animation",
                 "grenade_throw_run_animation", "grenade_throw_crouch_animation", "grenade_throw_prone_animation"):
        try:
            value = anim_cdo.get_editor_property(prop)
            if value is None or value == [] or (prop == "upper_body_slot" and str(value) == "DefaultSlot"):
                anim_cdo.set_editor_property(prop, main_cdo.get_editor_property(prop))
        except Exception as error:
            log.append(f"{prop} not copied: {error}")
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
library.save_loaded_asset(abp)
missing = [n for n, clips in (("starts", starts), ("stops", stops), ("turns L", turns_left), ("turns R", turns_right), ("loops", loops))
           if any(c is None for c in clips)]
log.append(f"clips: loops {sum(c is not None for c in loops)}/8, starts {sum(c is not None for c in starts)}/16, "
           f"stops {sum(c is not None for c in stops)}/16, turns {sum(c is not None for c in turns_left + turns_right)}/8, "
           f"breaks {len(breaks)}{'; missing: ' + ', '.join(missing) if missing else ''}")

if os.environ.get("CODEX_ASSIGN_RIFLE2") == "1":
    mesh_component.set_editor_property("anim_class", abp_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    library.save_loaded_asset(bp)
    log.append("BP_Operative now uses ABP_Operative_Rifle2")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_rifle2_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
