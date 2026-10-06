"""Creates /Game/Maps/L_PatrolTest: an open 100x100 m sandbox for planning outpost patrols (Sprint 11).

The user places cover, objects, APatrolRouteActor splines and enemies by hand; the script only lays the base:
  - floor with the world grid material (1 m grid helps read distances: escort 2-3.5 m, alert 15 / 20 m)
  - low perimeter walls (1 m) so units cannot walk off the edge
  - PlayerStart in the south (X -4000) facing north (+X)
  - a cover "palette" next to the start: copy / move these instead of building cover from scratch
      * Barricade_60  - 60 cm low cover (SightRules::CoverHeightCm): prone hides, crouch / stand see over it
      * Barricade_100 - 1 m cover: crouched operatives hide too
      * Wall_300      - 3 m wall: blocks sight for every stance
      * Pillar_300    - 1x1 m column, 3 m
  - sun, sky light, sky atmosphere, exponential height fog
  - NavMeshBoundsVolume over the whole area (the NavMesh builds at load, RuntimeGeneration=Dynamic)

Never overwrites an existing map: delete /Game/Maps/L_PatrolTest by hand first if a fresh one is wanted.

Run headless (editor closed):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

MAP_PATH = "/Game/Maps/L_PatrolTest"
HALF = 5000  # cm: 100 x 100 m
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
GRID_MATERIAL = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None, folder=None):
    actor = actors.spawn_actor_from_class(cls, location, rotation)
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def block(label, center, size, material=None, folder=None):
    """Axis-aligned box resting on the floor. center/size in cm; the engine cube is 100 cm."""
    actor = spawn(unreal.StaticMeshActor, unreal.Vector(*center), label=label, folder=folder)
    component = actor.static_mesh_component
    component.set_static_mesh(CUBE)
    if material:
        component.set_material(0, material)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    return actor


if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    unreal.log_error("CodexTactics: " + MAP_PATH + " already exists - not overwriting it")
else:
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)

    block("Floor", (0, 0, -10), (2 * HALF, 2 * HALF, 20), GRID_MATERIAL, "Base")
    for label, center, size in [
        ("Edge_North", (HALF, 0, 50), (50, 2 * HALF, 100)),
        ("Edge_South", (-HALF, 0, 50), (50, 2 * HALF, 100)),
        ("Edge_East", (0, HALF, 50), (2 * HALF, 50, 100)),
        ("Edge_West", (0, -HALF, 50), (2 * HALF, 50, 100)),
    ]:
        block(label, center, size, folder="Base")

    # Cover palette west of the start.
    for label, center, size in [
        ("Barricade_60", (-4000, -800, 30), (300, 40, 60)),
        ("Barricade_100", (-4000, -1300, 50), (300, 40, 100)),
        ("Wall_300", (-4000, -1900, 150), (500, 50, 300)),
        ("Pillar_300", (-4000, -2500, 150), (100, 100, 300)),
    ]:
        block(label, center, size, folder="CoverPalette")

    spawn(unreal.PlayerStart, unreal.Vector(-4000, 0, 100), label="PlayerStart", folder="Base")
    spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(0, -50, -30), label="Sun", folder="Lighting")
    sky_light = spawn(unreal.SkyLight, unreal.Vector(0, 0, 800), label="SkyLight", folder="Lighting")
    sky_light.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), label="SkyAtmosphere", folder="Lighting")
    spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), label="HeightFog", folder="Lighting")

    # The 2 m box brush scales to 104 x 104 x 10 m.
    nav_bounds = spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 200), label="NavMeshBounds", folder="Base")
    nav_bounds.set_actor_scale3d(unreal.Vector(52, 52, 5))

    if unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        unreal.log("CodexTactics: saved " + MAP_PATH)
    else:
        unreal.log_error("CodexTactics: failed to save " + MAP_PATH)
