"""Creates /Game/Vehicles/BP_APC_BTR82 (APC P0, docs/vehicles/APC_01_TECHNICAL.md): a Blueprint child of the C++
AVehiclePawn with the vendor model SK_East_APC_BTR82 + ABP_East_APC_BTR82 on its Mesh component. The vendor assets in
VigilanteContent are only referenced, never modified. An existing Blueprint keeps the user's edits (only an empty mesh /
anim class is filled in).

Run headless ONLY when no Unreal Editor has this project open:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_apc_vehicle.txt
"""
import os
import unreal

FOLDER = "/Game/Vehicles"
NAME = "BP_APC_BTR82"
ASSET = f"{FOLDER}/{NAME}"
MESH = "/Game/VigilanteContent/Vehicles/East_APC_BTR82/SK_East_APC_BTR82.SK_East_APC_BTR82"
ANIM = "/Game/VigilanteContent/Vehicles/East_APC_BTR82/ABP_East_APC_BTR82.ABP_East_APC_BTR82_C"

library = unreal.EditorAssetLibrary
log = []

if library.does_asset_exist(ASSET):
    blueprint = library.load_asset(ASSET)
    log.append(f"{ASSET} exists: kept")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.VehiclePawn)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, None, factory)
    if not blueprint:
        raise RuntimeError(f"Failed to create {ASSET}")
    log.append(f"created {ASSET}")

mesh = unreal.load_object(None, MESH)
anim_class = unreal.load_object(None, ANIM)
cdo = unreal.get_default_object(unreal.load_object(None, f"{ASSET}.{NAME}_C"))
body = cdo.get_editor_property("mesh")
if body.get_editor_property("skeletal_mesh_asset") is None and mesh:
    body.set_editor_property("skeletal_mesh_asset", mesh)
    log.append(f"mesh = {mesh.get_name()}")
if body.get_editor_property("anim_class") is None and anim_class:
    body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
    body.set_editor_property("anim_class", anim_class)
    log.append(f"anim class = {anim_class.get_name()}")
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
library.save_loaded_asset(blueprint)
log.append(f"saved {ASSET} (mesh {body.get_editor_property('skeletal_mesh_asset')}, anim {body.get_editor_property('anim_class')})")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_apc_vehicle.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
