"""Wires the operatives to the user's Post_Apo_Survivor mesh and the RifleAnims pack:

  - The survivor skeleton and the RifleAnims skeleton (both UE4 mannequin rigs) are made compatible.
  - /Game/Characters/Operatives/ABP_Operative: parent UOperativeAnimInstance, survivor skeleton, AnimGraph built by
    UOperativeAnimGraphLibrary (stand / crouch / aim blend spaces, upper-body montage slot); fire and reload clips.
  - BP_Operative: ABP_Operative, outfit per squad member (M_Outfit_Hoodie / _Pants, _Inst_2nd, _Inst_3rd).

Re-running keeps hand edits (graph built only while empty; CODEX_REBUILD_ANIM_GRAPHS=1 forces it). Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_rifle_animation.txt
"""
import os
import unreal

ROOT = "/Game/Characters/Operatives"
RIFLE = "/Game/RifleAnims/Animations"
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

# Same rig, two skeleton assets: let each use the other's animations.
for a, b in ((skeleton, rifle_skeleton), (rifle_skeleton, skeleton)):
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

# The graph is built only while empty — the user polishes it by hand (CODEX_REBUILD_ANIM_GRAPHS=1 forces a rebuild).
if os.environ.get("CODEX_REBUILD_ANIM_GRAPHS") == "1" or unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp) == 0:
    result = unreal.OperativeAnimGraphLibrary.build_operative_locomotion_graph(
        abp, stand,
        library.load_asset(f"{RIFLE}/BlendSpaces/Standing_IdleWalk_Aim/BS_Rifle_Aim"),
        library.load_asset(f"{RIFLE}/BlendSpaces/Crouch_IdleWalk/BS_Rifle_Crouch"),
        library.load_asset(f"{RIFLE}/BlendSpaces/Crouch_IdleWalk_Aim/BS_Rifle_Crouch_Aim"),
        None,  # no prone clips in the pack yet: the crouch blend space stands in
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
