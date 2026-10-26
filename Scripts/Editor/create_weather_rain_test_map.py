# Creates the test map of the TEMPERATE climate profile (environment section 17, docs/systems/environment-weather.md):
#   /Game/Environment/Weather/Test/L_WeatherRainTest   (an agents' test map - no user map is touched)
# with its own simple surface material /Game/Environment/Weather/Test/M_RainTest_Surface (vector Color, scalar Roughness) and three
# instances MI_RainTest_Ground (grass-green earth), MI_RainTest_Wood (the hut), MI_RainTest_Stone (props). Layout (world cm):
#   - a 60 x 60 m floor (engine cube, top at z 0) with MI_RainTest_Ground;
#   - a 6 x 5 m hut 3 m high at (+900, +300) with a door gap in its south wall and a flat roof (MI_RainTest_Wood);
#   - props: crates, barrels (cylinders), a low stone wall, two pillars, a cone (MI_RainTest_Stone / _Wood);
#   - a PlayerStart at the centre (the project's game mode spawns the squad there), a NavMeshBoundsVolume over the floor;
#   - NO lighting actors: the weather presenter's own rig lights the level (section 16); a placed AWeatherPresenterActor (label
#     WeatherPresenter, folder Lighting) is the editor preview (PreviewClimate Temperate, 12:00 SunnyDay) and the runtime presenter.
# The level data is Content/Data/LevelJson/L_WeatherRainTest.json ("environment": {"climate": "temperate"}): the game mode reads it by map name.
# Re-running replaces the map and the material assets (they are this script's own assets).
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_weather_rain_test_map.txt
import os
import unreal

MAP_PATH = "/Game/Environment/Weather/Test/L_WeatherRainTest"
ASSET_DIR = "/Game/Environment/Weather/Test"
MATERIAL = ASSET_DIR + "/M_RainTest_Surface"
INSTANCES = {
    "MI_RainTest_Ground": (unreal.LinearColor(0.16, 0.22, 0.09, 1.0), 0.85),
    "MI_RainTest_Wood": (unreal.LinearColor(0.32, 0.2, 0.11, 1.0), 0.7),
    "MI_RainTest_Stone": (unreal.LinearColor(0.38, 0.38, 0.4, 1.0), 0.6),
}
CUBE = "/Engine/BasicShapes/Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder"
CONE = "/Engine/BasicShapes/Cone"

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_weather_rain_test_map.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


def build_material():
    """M_RainTest_Surface: BaseColor = vector Color, Roughness = scalar Roughness (own asset, rebuilt)."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    lib = unreal.MaterialEditingLibrary
    # The instances first (they reference the material), then the material.
    for name in INSTANCES:
        if unreal.EditorAssetLibrary.does_asset_exist(ASSET_DIR + "/" + name):
            unreal.EditorAssetLibrary.delete_asset(ASSET_DIR + "/" + name)
    if unreal.EditorAssetLibrary.does_asset_exist(MATERIAL):
        unreal.EditorAssetLibrary.delete_asset(MATERIAL)
    material = tools.create_asset("M_RainTest_Surface", ASSET_DIR, unreal.Material, unreal.MaterialFactoryNew())
    color = lib.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    rough = lib.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, 200)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.7)
    lib.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    lib.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    lib.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    log("material %s" % MATERIAL)
    result = {}
    for name, (value, roughness) in INSTANCES.items():
        path = ASSET_DIR + "/" + name
        instance = tools.create_asset(name, ASSET_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        lib.set_material_instance_parent(instance, material)
        lib.set_material_instance_vector_parameter_value(instance, "Color", value)
        lib.set_material_instance_scalar_parameter_value(instance, "Roughness", roughness)
        lib.update_material_instance(instance)
        unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False)
        result[name] = instance
        log("instance %s colour %s roughness %.2f" % (path, value, roughness))
    return result


def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None, folder=None):
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(cls, location, rotation)
    if label:
        actor.set_actor_label(label)
    if folder:
        actor.set_folder_path(folder)
    return actor


def mesh(path, location, scale, material, label, rotation=unreal.Rotator(0, 0, 0), folder="Props"):
    """A StaticMeshActor of an engine basic shape (100 cm unit; the cube / cylinder are centred, so z = the centre)."""
    actor = spawn(unreal.StaticMeshActor, location, rotation, label, folder)
    component = actor.static_mesh_component
    component.set_static_mesh(unreal.EditorAssetLibrary.load_asset(path))
    component.set_material(0, material)
    actor.set_actor_scale3d(scale)
    return actor


def main():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.EditorAssetLibrary.delete_asset(MAP_PATH)
        log("replaced (own asset): " + MAP_PATH)
    materials = build_material()
    ground, wood, stone = materials["MI_RainTest_Ground"], materials["MI_RainTest_Wood"], materials["MI_RainTest_Stone"]
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)

    # Floor: 60 x 60 m, 20 cm thick, top at z 0.
    mesh(CUBE, unreal.Vector(0, 0, -10), unreal.Vector(60, 60, 0.2), ground, "Floor", folder="Ground")

    # The hut: 6 m (x) x 5 m (y), 3 m walls 20 cm thick, a 1.4 m door gap in the south (-y) wall, a flat roof.
    hx, hy, h = 900.0, 300.0, 300.0
    mesh(CUBE, unreal.Vector(hx, hy + 250, h / 2), unreal.Vector(6.0, 0.2, 3.0), wood, "Hut_WallN", folder="Hut")
    mesh(CUBE, unreal.Vector(hx - 300, hy, h / 2), unreal.Vector(0.2, 5.0, 3.0), wood, "Hut_WallW", folder="Hut")
    mesh(CUBE, unreal.Vector(hx + 300, hy, h / 2), unreal.Vector(0.2, 5.0, 3.0), wood, "Hut_WallE", folder="Hut")
    mesh(CUBE, unreal.Vector(hx - 185, hy - 250, h / 2), unreal.Vector(2.3, 0.2, 3.0), wood, "Hut_WallS_L", folder="Hut")
    mesh(CUBE, unreal.Vector(hx + 185, hy - 250, h / 2), unreal.Vector(2.3, 0.2, 3.0), wood, "Hut_WallS_R", folder="Hut")
    mesh(CUBE, unreal.Vector(hx, hy - 250, h - 40), unreal.Vector(1.4, 0.2, 0.8), wood, "Hut_Lintel", folder="Hut")
    mesh(CUBE, unreal.Vector(hx, hy, h + 10), unreal.Vector(6.6, 5.6, 0.2), wood, "Hut_Roof", folder="Hut")

    # Props.
    for index, (x, y) in enumerate([(-600, 400), (-520, 520), (-640, 560), (300, -700), (420, -720)]):
        mesh(CUBE, unreal.Vector(x, y, 50), unreal.Vector(1.0, 1.0, 1.0), wood, "Crate_%d" % index, unreal.Rotator(0, 0, index * 17.0))
    for index, (x, y) in enumerate([(-300, -500), (-220, -560), (-260, -440)]):
        mesh(CYLINDER, unreal.Vector(x, y, 60), unreal.Vector(0.6, 0.6, 1.2), stone, "Barrel_%d" % index)
    mesh(CUBE, unreal.Vector(-900, -200, 50), unreal.Vector(0.4, 8.0, 1.0), stone, "LowWall")
    mesh(CYLINDER, unreal.Vector(500, 900, 200), unreal.Vector(0.5, 0.5, 4.0), stone, "Pillar_0")
    mesh(CYLINDER, unreal.Vector(-500, 900, 200), unreal.Vector(0.5, 0.5, 4.0), stone, "Pillar_1")
    mesh(CONE, unreal.Vector(0, 1300, 75), unreal.Vector(1.5, 1.5, 1.5), stone, "Cone")

    spawn(unreal.PlayerStart, unreal.Vector(0, 0, 120), label="PlayerStart", folder="Gameplay")
    nav = spawn(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 0), label="NavMeshBounds", folder="Gameplay")
    nav.set_actor_scale3d(unreal.Vector(30.0, 30.0, 5.0))  # the 2 m box brush -> 60 x 60 x 10 m

    presenter = spawn(unreal.WeatherPresenterActor, unreal.Vector(0, 0, 0), label="WeatherPresenter", folder="Lighting")
    presenter.set_editor_property("preview_climate", unreal.ClimateProfile.TEMPERATE)
    presenter.set_editor_property("preview_weather", unreal.WeatherState.CLEAR)
    presenter.set_editor_property("preview_hour", 12.0)
    log("placed %s (editor preview: temperate 12:00 SunnyDay)" % presenter.get_name())

    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
    log("saved %s: %s" % (MAP_PATH, saved))


main()
write_log()
