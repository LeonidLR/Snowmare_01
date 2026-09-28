"""Adds level objects to /Game/Maps/L_MovementTest without regenerating the map (keeps manual edits):
a fuel barrel, two abandoned barricades and two hidden mines (Godot movements_demo.tscn: BarrelObject,
AbandonedBarricadeEast / Generator, AbandonedMinePath / Alley). Objects whose label already exists are skipped.
They stand away from the routes of the automated checks.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

MAP_PATH = "/Game/Maps/L_MovementTest"
OBJECTS = [
    # label, class, location (actor centre), yaw
    ("Barrel_Fuel_01", unreal.BarrelActor, unreal.Vector(-400, 1200, 70), 0),
    ("Barricade_Abandoned_East", unreal.BarricadeActor, unreal.Vector(2000, 1500, 50), 0),
    ("Barricade_Abandoned_West", unreal.BarricadeActor, unreal.Vector(-2300, 700, 50), 90),
    ("Mine_Abandoned_Path", unreal.ProximityMineActor, unreal.Vector(2300, 2400, 30), 0),
    ("Mine_Abandoned_Alley", unreal.ProximityMineActor, unreal.Vector(2400, -1400, 30), 0),
]

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor.load_level(MAP_PATH)

existing = {a.get_actor_label() for a in actors.get_all_level_actors()}
added = []
for label, cls, location, yaw in OBJECTS:
    if label in existing:
        continue
    actor = actors.spawn_actor_from_class(cls, location, unreal.Rotator(0, 0, yaw))
    actor.set_actor_label(label)
    added.append(label)
saved = level_editor.save_current_level() if added else False
# Commandlet stdout drops Python prints: leave the result next to the logs.
with open(unreal.Paths.project_saved_dir() + "Logs/AddBarrels.txt", "w", encoding="utf-8") as f:
    f.write(f"added {added}, saved={saved}")
