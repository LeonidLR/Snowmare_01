"""Creates /Game/Blueprints/BP_CodexTacticsGameMode (child of ACodexTacticsGameMode) if it does not exist.

The designer edits the squad there: open the Blueprint -> Class Defaults -> CodexTactics|Squad -> Squad Roster
(row order = selection keys 1..N; to give a slot the regular operative clear its Member Blueprint AND Body Mesh).
The roster and every other default are inherited from the C++ constructor until the designer overrides them.
Config/DefaultEngine.ini GlobalDefaultGameMode points at this Blueprint. Existing assets are never overwritten,
so the designer's edits are safe to keep when this script runs again.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

FOLDER = "/Game/Blueprints"
NAME = "BP_CodexTacticsGameMode"
ASSET = f"{FOLDER}/{NAME}"

if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.log(f"{ASSET} already exists, left untouched")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.CodexTacticsGameMode)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, None, factory)
    if not blueprint:
        raise RuntimeError(f"Failed to create {ASSET}")
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint)
    unreal.log(f"Created {ASSET}")

# Verify the roster is inherited from the C++ defaults (not emptied).
generated = unreal.load_object(None, f"{ASSET}.{NAME}_C")
cdo = unreal.get_default_object(generated)
roster = cdo.get_editor_property("squad_roster")
unreal.log(f"{NAME} squad roster rows: {len(roster)}")
for index, row in enumerate(roster):
    member_bp = row.get_editor_property("member_blueprint")
    body = row.get_editor_property("body_mesh")
    unreal.log(f"  [{index}] {row.get_editor_property('display_name')} role={row.get_editor_property('role')} "
               f"member_blueprint={member_bp.get_path_name() if member_bp else 'none'} "
               f"body_mesh={body.get_path_name() if body else 'none'} "
               f"extra_weapons={len(row.get_editor_property('extra_weapons'))}")
if len(roster) != 3:
    raise RuntimeError(f"{NAME}: expected 3 inherited roster rows, got {len(roster)}")
