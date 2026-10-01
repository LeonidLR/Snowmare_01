"""Wires the operatives to the user's Post_Apo_Survivor mesh and the RifleAnims pack:

  - The survivor skeleton and the RifleAnims skeleton (both UE4 mannequin rigs) are made compatible.
  - /Game/Characters/Operatives/ABP_Operative: parent UOperativeAnimInstance, survivor skeleton, AnimGraph built by
    UOperativeAnimGraphLibrary (stand / crouch / aim blend spaces, upper-body montage slot); fire and reload clips.
  - BP_Operative: ABP_Operative, outfit per squad member (M_Outfit_Hoodie / _Pants, _Inst_2nd, _Inst_3rd).
  - Prone from the user's Crawl_MocapAnimPack (same UE4 mannequin rig, so its skeletons are made compatible, no
    retarget): BS_Rifle_Prone / BS_Rifle_Prone_Aim (rifle idle + in-place crawl, created once), stance transitions
    stand / crouch <-> prone, prone fire / hit / death clips.

Re-running keeps hand edits (graph built only while empty; CODEX_REBUILD_ANIM_GRAPHS=1 forces it). Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_rifle_animation.txt
"""
import os
import unreal

ROOT = "/Game/Characters/Operatives"
RIFLE = "/Game/RifleAnims/Animations"
CRAWL = "/Game/Crawl_MocapAnimPack/Animations"
CRAWL_SKELETONS = ("/Game/Crawl_MocapAnimPack/Demo/Models/SK_Mannequin_A_Skeleton",
                   "/Game/Crawl_MocapAnimPack/Demo/Models/Rifle/Rifle_Mannequin_A_Skeleton",
                   "/Game/Crawl_MocapAnimPack/Demo/Models/Pistol/Pistol_Mannequin_A_Skeleton")
# Crawl speed of the pack's in-place clips (Loco_Linear_root/Crawl_Walk_F moves 69 cm in 3.33 s), cm/s.
CRAWL_SPEED = 21.0
OUTFITS = "/Game/Post_Apo_Survivor/Materials"
library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []

bp_class = unreal.load_object(None, f"{ROOT}/BP_Operative.BP_Operative_C")
bp = library.load_asset(f"{ROOT}/BP_Operative")
cdo = unreal.get_default_object(bp_class)
mesh_component = cdo.get_editor_property("mesh")
mesh = mesh_component.get_editor_property("skeletal_mesh_asset")
skeleton = mesh.get_editor_property("skeleton")
stand = library.load_asset(f"{RIFLE}/BlendSpaces/Standing_IdleWalkJogRun/BS_Rifle")
rifle_skeleton = stand.get_editor_property("skeleton")
log.append(f"mesh {mesh.get_path_name()} skeleton {skeleton.get_path_name()}; anims skeleton {rifle_skeleton.get_path_name()}")

# Same rig, several skeleton assets: let each use the other's animations.
pairs = [(skeleton, rifle_skeleton), (rifle_skeleton, skeleton)]
crawl_ok = library.does_asset_exist(CRAWL_SKELETONS[0])
if crawl_ok:
    for path in CRAWL_SKELETONS:
        if library.does_asset_exist(path):
            other = library.load_asset(path)
            pairs += [(skeleton, other), (other, skeleton)]
for a, b in pairs:
    if a == b:
        continue
    compatible = list(a.get_editor_property("compatible_skeletons"))
    if not any(c.get_path_name() == b.get_path_name() for c in compatible if c):
        compatible.append(b)
        a.set_editor_property("compatible_skeletons", compatible)
        library.save_loaded_asset(a)
        log.append(f"{a.get_name()} ({a.get_path_name()}) now compatible with {b.get_path_name()}")

abp_path = f"{ROOT}/ABP_Operative"
if library.does_asset_exist(abp_path):
    abp = library.load_asset(abp_path)
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("parent_class", unreal.OperativeAnimInstance)
    abp = tools.create_asset("ABP_Operative", ROOT, unreal.AnimBlueprint, factory)
    log.append(f"created {abp_path}")

# Prone blend spaces (created once; hand edits kept).
prone_bs, prone_aim_bs = None, None
if crawl_ok:
    loco = f"{CRAWL}/Locomotion_Set"
    moves = [library.load_asset(f"{loco}/Crawl_Walk_{d}_IPC") for d in ("F", "45R", "R", "135R", "B", "135L", "L", "45L")]
    for name, idle in (("BS_Rifle_Prone", f"{CRAWL}/Rifle_Set/Idle/Crawl_Rifle_Idle01"),
                       ("BS_Rifle_Prone_Aim", f"{CRAWL}/Rifle_Set/Idle/Crawl_Rifle_Aim_Idle")):
        path = f"{ROOT}/Anims/{name}"
        if library.does_asset_exist(path):
            bs = library.load_asset(path)
        else:
            factory = unreal.BlendSpaceFactoryNew()
            factory.set_editor_property("target_skeleton", skeleton)
            bs = tools.create_asset(name, f"{ROOT}/Anims", unreal.BlendSpace, factory)
            result = unreal.OperativeAnimGraphLibrary.fill_directional_blend_space(bs, library.load_asset(idle), moves, CRAWL_SPEED)
            log.append(f"created {path}: {result}")
            library.save_loaded_asset(bs, False)
        if name.endswith("_Aim"):
            prone_aim_bs = bs
        else:
            prone_bs = bs

# The graph is built only while empty â€” the user polishes it by hand (CODEX_REBUILD_ANIM_GRAPHS=1 forces a rebuild).
# Node counts of graphs this script generated earlier (left as generated -> safe to regenerate with the new layout):
# CountAnimGraphNodes (without the output node): 41 = before the FullBody slot (commit 8a56896), 42 = with it,
# 48 = with the prone blend spaces and the prone aim switch, 52 = with the aim layered over the standing legs.
GENERATED_NODE_COUNTS = (41, 42, 48, 52)
node_count = unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp)
if os.environ.get("CODEX_REBUILD_ANIM_GRAPHS") == "1" or node_count == 0 or node_count in GENERATED_NODE_COUNTS:
    result = unreal.OperativeAnimGraphLibrary.build_operative_locomotion_graph(
        abp, stand,
        library.load_asset(f"{RIFLE}/BlendSpaces/Standing_IdleWalk_Aim/BS_Rifle_Aim"),
        library.load_asset(f"{RIFLE}/BlendSpaces/Crouch_IdleWalk/BS_Rifle_Crouch"),
        library.load_asset(f"{RIFLE}/BlendSpaces/Crouch_IdleWalk_Aim/BS_Rifle_Crouch_Aim"),
        prone_bs, prone_aim_bs,  # None without the crawl pack: the crouch blend spaces stand in
        "Fire", "spine_01", 0.25)  # the pack's fire montages use slot "Fire"
    # Python returns only the out string when the bool is a plain return value in some bindings.
    ok, report = result if isinstance(result, tuple) else ("errors 0" in result, result)
    log.append(f"graph ok={ok}\n{report}")
else:
    log.append("graph kept (edited by hand)")

abp_class = unreal.load_object(None, f"{abp_path}.ABP_Operative_C")
anim_cdo = unreal.get_default_object(abp_class)
anim_cdo.set_editor_property("use_native_locomotion", False)
# Clips only while unset (chosen by hand after that).
if anim_cdo.get_editor_property("fire_montage") is None:
    anim_cdo.set_editor_property("upper_body_slot", "Fire")
    anim_cdo.set_editor_property("fire_montage", library.load_asset(f"{RIFLE}/Montages/AM_Rifle_Fire"))
if anim_cdo.get_editor_property("fire_aim_montage") is None:
    anim_cdo.set_editor_property("fire_aim_montage", library.load_asset(f"{RIFLE}/Montages/AM_Rifle_Fire_Aim"))
if anim_cdo.get_editor_property("reload_animation") is None:
    anim_cdo.set_editor_property("reload_animation", library.load_asset(f"{RIFLE}/Fire_Reload_Equip_Jump/AS_Rifle_ReloadLoaded"))
# Prone clips from the crawl pack (only while unset; chosen by hand after that).
if crawl_ok:
    prone_clips = {
        "stand_to_prone_animation": "Transitions_Set/Crawl_from_Act",
        "prone_to_stand_animation": "Transitions_Set/Crawl_to_Act",
        "crouch_to_prone_animation": "Transitions_Set/Crawl_from_Cr",
        "prone_to_crouch_animation": "Transitions_Set/Crawl_to_Cr",
        "fire_prone_animation": "Rifle_Set/Shoots/Crawl_Rifle_Shoot_Light",
        "hit_prone_animation": "Hit_Death_Set/Crawl_Hit_F",
        "death_prone_animation": "Hit_Death_Set/Crawl_Death01",
    }
    for prop, clip in prone_clips.items():
        if anim_cdo.get_editor_property(prop) is None:
            anim_cdo.set_editor_property(prop, library.load_asset(f"{CRAWL}/{clip}"))
            log.append(f"{prop} = {clip}")
    anim_cdo.set_editor_property("prone_blend_space_max_speed", CRAWL_SPEED)
for montage in ("AM_Rifle_Fire", "AM_Rifle_Fire_Aim"):
    m = library.load_asset(f"{RIFLE}/Montages/{montage}")
    slots = [t.get_editor_property("slot_name") for t in m.get_editor_property("slot_anim_tracks")]
    log.append(f"{montage} slots {slots}")
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
library.save_loaded_asset(abp)

mesh_component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
mesh_component.set_editor_property("anim_class", abp_class)
if len(cdo.get_editor_property("squad_outfits")) == 0:  # set by hand after that
    # One outfit per squad member: commander, engineer, medic-sapper.
    outfits = []
    for suffix in ("", "_Inst_2nd", "_Inst_3rd"):
        outfit = unreal.OperativeOutfit()
        outfit.set_editor_property("slot_materials", {
            "M_Outfit_Hoodie": library.load_asset(f"{OUTFITS}/M_Outfit_Hoodie{suffix}"),
            "M_Outfit_Pants": library.load_asset(f"{OUTFITS}/M_Outfit_Pants{suffix}"),
        })
        outfits.append(outfit)
    cdo.set_editor_property("squad_outfits", outfits)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
library.save_loaded_asset(bp)
log.append(f"BP_Operative anim={mesh_component.get_editor_property('anim_class').get_name()} "
           f"outfits={[sorted(v.get_name() for v in o.get_editor_property('slot_materials').values()) for o in cdo.get_editor_property('squad_outfits')]}")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_rifle_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
