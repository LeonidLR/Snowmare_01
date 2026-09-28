"""Creates /Game/Maps/L_MovementTest: a 60x60 m arena for squad movement checks.

Layout (UE cm, X forward from the player start):
  - floor with the world grid material, outer walls
  - narrow corridor (1.8 m wide) at X 1200..2200 to trigger column mode
  - pillars and a low barricade as obstacles
  - bunker room with a CameraZoneVolume + fixed CameraActor (camera zone check)
  - PlayerStart at the origin facing +X, lights
  - NavMeshBoundsVolume over the arena; the NavMesh is built at load (RuntimeGeneration=Dynamic)

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

MAP_PATH = "/Game/Maps/L_MovementTest"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
GRID_MATERIAL = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None):
    actor = actors.spawn_actor_from_class(cls, location, rotation)
    if label:
        actor.set_actor_label(label)
    return actor


def block(label, center, size, material=None):
    """Axis-aligned box. center/size in cm; the engine cube is 100 cm."""
    actor = spawn(unreal.StaticMeshActor, unreal.Vector(*center), label=label)
    component = actor.static_mesh_component
    component.set_static_mesh(CUBE)
    if material:
        component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)

block("Floor", (0, 0, -10), (6000, 6000, 20), GRID_MATERIAL)
for label, center, size in [
    ("Wall_North", (3000, 0, 150), (50, 6000, 300)),
    ("Wall_South", (-3000, 0, 150), (50, 6000, 300)),
    ("Wall_East", (0, 3000, 150), (6000, 50, 300)),
    ("Wall_West", (0, -3000, 150), (6000, 50, 300)),
    ("Corridor_Left", (1700, -115, 150), (1000, 50, 300)),
    ("Corridor_Right", (1700, 115, 150), (1000, 50, 300)),
    ("Pillar_A", (800, -800, 150), (100, 100, 300)),
    ("Pillar_B", (800, 900, 150), (100, 100, 300)),
    ("Pillar_C", (-900, 600, 150), (100, 100, 300)),
    ("Barricade_Low", (-1000, -1000, 50), (300, 40, 100)),
]:
    block(label, center, size)

# Bunker room (camera zone check): open towards the player start, centre (-1800, 1800).
for label, center, size in [
    ("Bunker_Back", (-2200, 1800, 150), (50, 850, 300)),
    ("Bunker_SideA", (-1800, 2200, 150), (850, 50, 300)),
    ("Bunker_SideB", (-1800, 1400, 150), (850, 50, 300)),
]:
    block(label, center, size)

bunker_center = unreal.Vector(-1800, 1800, 90)
zone_camera_location = unreal.Vector(-1250, 1250, 750)
zone_camera = spawn(unreal.CameraActor, zone_camera_location,
                    unreal.MathLibrary.find_look_at_rotation(zone_camera_location, bunker_center),
                    label="Camera_Bunker")
zone = spawn(unreal.CameraZoneVolume, bunker_center, label="CameraZone_Bunker")
zone.set_actor_scale3d(unreal.Vector(0.95, 0.95, 1.0))
zone.set_editor_property("target_camera", zone_camera)
zone.set_editor_property("zone_name", "Сектор наблюдения 01")

spawn(unreal.PlayerStart, unreal.Vector(0, 0, 100), label="PlayerStart")
spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(0, -50, -30), label="Sun")
sky_light = spawn(unreal.SkyLight, unreal.Vector(0, 0, 800), label="SkyLight")
sky_light.light_component.set_editor_property("real_time_capture", True)
spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), label="SkyAtmosphere")

# NavMesh bounds over the whole arena; the 2 m box brush scales to 64 x 64 x 10 m.
nav_bounds = spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 200), label="NavMeshBounds")
nav_bounds.set_actor_scale3d(unreal.Vector(32, 32, 5))

if unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
    unreal.log("CodexTactics: saved " + MAP_PATH)
else:
    unreal.log_error("CodexTactics: failed to save " + MAP_PATH)
