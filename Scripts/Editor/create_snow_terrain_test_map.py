# Creates the landscape test map for M_Landscape_Master / MI_Landscape_Master (environment §13, docs/systems/environment-weather.md):
#   /Game/Environment/Weather/Test/L_SnowTerrainTest   (an agents' test map - no user map is touched)
# A 101 x 101 m landscape (4 x 4 components of 63 quads, 40 cm spacing) with MI_Landscape_Master: a flat plateau in the middle (snow),
# a steep ridge to the north (+X: rock on the 35-56 degree flank, snow on top), a bowl to the east (+Y: its floor 4.2 m deep, below
# IceMinHeight -250 cm = ice), a gentle 19 degree slope to the west (-Y: snow); a sun, sky light, sky atmosphere and height fog
# (the weather presenter adopts them), a PlayerStart on the plateau (the project's game mode spawns the squad and the environment runs)
# and a NavMeshBoundsVolume (runtime navmesh: the operatives can walk).
# The landscape comes from ULandscapeAuthoringLibrary::CreateLandscapeFromHeights (CodexTacticsEditor; UE 5.8 has no Python API for it).
# Re-running replaces the map (it is this script's own asset).
# Run (holding the build lock, $env:CODEX_AGENT = "claude"; the landscape edit layers need a renderer):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -AllowCommandletRendering
# Result: Saved/Logs/create_snow_terrain_test_map.txt
import math
import os
import unreal

MAP_PATH = "/Game/Environment/Weather/Test/L_SnowTerrainTest"
MATERIAL = "/Game/VFX/Materials/MI_Landscape_Master"
COMPONENTS = 4
QUADS = 63
SPACING = 40.0
SIZE = COMPONENTS * QUADS + 1  # vertices per side
HALF = (SIZE - 1) * SPACING * 0.5

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_snow_terrain_test_map.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


def smoothstep(edge0, edge1, x):
    t = min(max((x - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def height_cm(x, y):
    """World height (cm) at (x, y) relative to the map centre."""
    h = 0.0
    # North ridge: 6 m over 6 m of run, steepest (56 degrees) in the middle of the flank, flat on top.
    h += 600.0 * smoothstep(900.0, 1500.0, x)
    # West slope: 19 degrees up to 4.5 m.
    h += min(max(-y - 1000.0, 0.0) * 0.35, 450.0)
    # East bowl: 4.2 m deep, 10 m radius (walls up to ~33 degrees: snow; the floor below IceMinHeight: ice).
    r = math.hypot(x + 200.0, y - 1500.0) / 1000.0
    if r < 1.0:
        h -= 420.0 * (1.0 - r * r) ** 2
    # Gentle undulation (keeps the plateau from reading as a plane).
    h += 12.0 * math.sin(x / 310.0) * math.cos(y / 270.0) + 6.0 * math.sin((x + y) / 130.0)
    return h


def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(cls, location, rotation)
    if label:
        actor.set_actor_label(label)
    return actor


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.EditorAssetLibrary.delete_asset(MAP_PATH)
        log("replaced (own asset): " + MAP_PATH)
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)

    heights = []
    low, high = 1e9, -1e9
    for j in range(SIZE):
        y = -HALF + j * SPACING
        for i in range(SIZE):
            x = -HALF + i * SPACING
            h = height_cm(x, y)
            low, high = min(low, h), max(high, h)
            heights.append(h)
    landscape = unreal.LandscapeAuthoringLibrary.create_landscape_from_heights(
        world, unreal.Vector(-HALF, -HALF, 0.0), unreal.Vector(SPACING, SPACING, 100.0), COMPONENTS, COMPONENTS, QUADS, heights, material)
    if not landscape:
        log("ERROR: the landscape was not created")
        return
    landscape.set_actor_label("SnowTerrain")
    log("landscape %s: %d x %d vertices, %.0f m, heights %.0f .. %.0f cm, material %s" % (landscape.get_name(), SIZE, SIZE, (SIZE - 1) * SPACING / 100.0,
                                                                                          low, high, MATERIAL))

    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(0, -40, 35), "Sun")
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    sky_light = spawn(unreal.SkyLight, unreal.Vector(0, 0, 1500), label="SkyLight")
    sky_light.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky_light.light_component.set_editor_property("real_time_capture", True)
    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), label="SkyAtmosphere")
    spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), label="HeightFog")
    spawn(unreal.PlayerStart, unreal.Vector(-300.0, -200.0, height_cm(-300.0, -200.0) + 120.0), label="PlayerStart")
    # The 2 m box brush scales to 100 x 100 x 20 m.
    nav = spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 0), label="NavMeshBounds")
    nav.set_actor_scale3d(unreal.Vector(HALF / 100.0, HALF / 100.0, 10.0))

    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
    log("saved %s: %s" % (MAP_PATH, saved))


main()
write_log()
