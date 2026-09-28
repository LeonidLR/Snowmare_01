"""Creates /Game/Characters/Operatives/BP_Operative (child of AOperativeCharacter) if it does not exist.

The Blueprint owns the operative's look: skeletal mesh, AnimBP, materials, capsule and stance shapes.
Without a skeletal mesh it shows the C++ placeholder cylinder. Existing assets are never overwritten,
so the user's edits are safe to keep when this script runs again.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

FOLDER = "/Game/Characters/Operatives"
NAME = "BP_Operative"
ASSET = f"{FOLDER}/{NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.log(f"{ASSET} already exists, left untouched")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.OperativeCharacter)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, None, factory)
    if not blueprint:
        raise RuntimeError(f"Failed to create {ASSET}")
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    unreal.log(f"Created {ASSET}")
