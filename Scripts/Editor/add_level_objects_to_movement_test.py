"""Adds level objects to /Game/Maps/L_MovementTest without regenerating the map (keeps manual edits):
a fuel barrel, two abandoned barricades, two hidden mines and two supply crates (Godot movements_demo.tscn:
BarrelObject, AbandonedBarricadeEast / Generator, AbandonedMinePath / Alley, LootCrateObject, TrappedSupplyCrate),
the «Начать бой» squad spot behind the gate (TargetPoint tagged CombatStart; Godot main.gd _on_start_combat_pressed)
and four enemy spawn points in the yard beyond the gate (Godot EnemySpawnPoints: north gate, west flank, east flank,
far perimeter). Objects whose label already exists are skipped. They stand away from the routes of the automated checks.
Locations are design coordinates (layout at the origin, yaw 0); they follow the "Floor" actor when the user moves or
rotates the layout (same mapping as SmokeUtils::LevelPoint).

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import math

import unreal

MAP_PATH = "/Game/Maps/L_MovementTest"


def checkpoint_crate(crate):
    """Godot LootCrateObject: medkits 2, food 2, no bread / matches / shotgun / fuel / cryo / plasma."""
    crate.set_editor_property("crate_name", "📦 Армейский ящик снабжения (КПП)")
    contents = crate.get_editor_property("contents")
    for field, value in (("medkits", 2), ("canned_food", 2), ("bread", 0), ("matches", 0), ("shotgun_ammo", 0),
                         ("flame_fuel", 0), ("cryo_ammo", 0), ("plasma_ammo", 0)):
        contents.set_editor_property(field, value)
    crate.set_editor_property("contents", contents)


def trapped_crate(crate):
    """Godot TrappedSupplyCrate: trapped, turret 1, barricades 2, mines 2 (other items at defaults)."""
    crate.set_editor_property("crate_name", "📦 Заминированный ящик аванпоста")
    crate.set_editor_property("trapped", True)
    contents = crate.get_editor_property("contents")
    for field, value in (("turrets", 1), ("barricades", 2), ("mines", 2)):
        contents.set_editor_property(field, value)
    crate.set_editor_property("contents", contents)


def spawn_lane(lane):
    def setup(point):
        point.set_editor_property("spawn_lane", lane)
    return setup


def combat_start(point):
    """UMissionSubsystem::CombatStartTag: the squad is placed here by «Начать бой»."""
    point.set_editor_property("tags", ["CombatStart"])


OBJECTS = [
    # label, class, location (actor centre), yaw, setup
    ("Barrel_Fuel_01", unreal.BarrelActor, unreal.Vector(-400, 1200, 70), 0, None),
    ("Barricade_Abandoned_East", unreal.BarricadeActor, unreal.Vector(2000, 1500, 50), 0, None),
    ("Barricade_Abandoned_West", unreal.BarricadeActor, unreal.Vector(-2300, 700, 50), 90, None),
    ("Mine_Abandoned_Path", unreal.ProximityMineActor, unreal.Vector(2300, 2400, 30), 0, None),
    ("Mine_Abandoned_Alley", unreal.ProximityMineActor, unreal.Vector(2400, -1400, 30), 0, None),
    ("Crate_Supply_Checkpoint", unreal.LootCrateActor, unreal.Vector(1200, 1200, 40), 0, checkpoint_crate),
    ("Crate_Supply_Trapped", unreal.LootCrateActor, unreal.Vector(1200, 900, 40), 0, trapped_crate),
    ("CombatStart", unreal.TargetPoint, unreal.Vector(0, -2150, 0), -90, combat_start),
    ("EnemySpawn_NorthGate", unreal.EnemySpawnPoint, unreal.Vector(0, -2600, 100), 90, spawn_lane("NORTH_GATE")),
    ("EnemySpawn_WestFlank", unreal.EnemySpawnPoint, unreal.Vector(-1400, -2400, 100), 90, spawn_lane("WEST_FLANK")),
    ("EnemySpawn_EastFlank", unreal.EnemySpawnPoint, unreal.Vector(1400, -2400, 100), 90, spawn_lane("EAST_FLANK")),
    ("EnemySpawn_FarPerimeter", unreal.EnemySpawnPoint, unreal.Vector(0, -2850, 100), 90, spawn_lane("FAR_PERIMETER")),
]

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
level_editor.load_level(MAP_PATH)

level_actors = actors.get_all_level_actors()
existing = {a.get_actor_label() for a in level_actors}
floor = next((a for a in level_actors if a.get_actor_label() == "Floor"), None)
floor_loc = floor.get_actor_location() if floor else unreal.Vector(0, 0, 0)
floor_yaw = floor.get_actor_rotation().yaw if floor else 0.0


def to_level(point):
    """Design point -> current layout (Floor location + yaw), like SmokeUtils::LevelPoint."""
    c, s = math.cos(math.radians(floor_yaw)), math.sin(math.radians(floor_yaw))
    return unreal.Vector(point.x * c - point.y * s + floor_loc.x, point.x * s + point.y * c + floor_loc.y, point.z)

added = []
for label, cls, location, yaw, setup in OBJECTS:
    if label in existing:
        continue
    actor = actors.spawn_actor_from_class(cls, to_level(location), unreal.Rotator(0, 0, yaw + floor_yaw))
    actor.set_actor_label(label)
    if setup:
        setup(actor)
    added.append(label)
if added:
    # Loading the map in a commandlet re-creates RecastNavMesh-Default without tiles; saved like that it stays empty at
    # runtime (Dynamic generation keeps the loaded data). The map ships without it: it is created and built at load.
    for nav in [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]:
        actors.destroy_actor(nav)
saved = level_editor.save_current_level() if added else False
# Commandlet stdout drops Python prints: leave the result next to the logs.
with open(unreal.Paths.project_saved_dir() + "Logs/AddBarrels.txt", "w", encoding="utf-8") as f:
    f.write(f"added {added}, saved={saved}")
