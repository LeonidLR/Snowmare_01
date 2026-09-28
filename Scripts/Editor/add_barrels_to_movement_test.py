"""Adds a fuel barrel to /Game/Maps/L_MovementTest without regenerating the map (keeps manual edits).

Skips if the map already has a BarrelActor.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

MAP_PATH = "/Game/Maps/L_MovementTest"
BARRELS = [("Barrel_Fuel_01", unreal.Vector(-400, 1200, 70))]

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor.load_level(MAP_PATH)

existing = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.BarrelActor)]
if existing:
    result = f"already present: {[a.get_actor_label() for a in existing]}"
else:
    for label, location in BARRELS:
        barrel = actors.spawn_actor_from_class(unreal.BarrelActor, location, unreal.Rotator(0, 0, 0))
        barrel.set_actor_label(label)
    saved = level_editor.save_current_level()
    result = f"added {[label for label, _ in BARRELS]}, saved={saved}"
# Commandlet stdout drops Python prints: leave the result next to the logs.
with open(unreal.Paths.project_saved_dir() + "Logs/AddBarrels.txt", "w", encoding="utf-8") as f:
    f.write(result)
