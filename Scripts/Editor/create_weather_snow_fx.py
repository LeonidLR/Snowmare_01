# Environment phase 4b: NS_Weather_Snow, the falling snow for the top-down tactical camera
# (docs/systems/environment-weather.md section 12).
#
# Duplicates the migrated NS_SnowStorm (left untouched) into /Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow and retunes the
# copy through UNiagaraAuthoringLibrary (CodexTacticsEditor, C++: UE 5.8 has no Python API for Niagara stacks). Re-runnable: an existing
# NS_Weather_Snow is deleted and rebuilt from NS_SnowStorm, so hand edits to NS_Weather_Snow are lost - tune by hand OR here, not both.
#
# Run (editor commandlet; take the build lock first: Scripts/agent_lock.ps1, CODEX_AGENT=claude):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# Environment variable CODEX_SNOWFX_MODE:
#   dump    - writes the stacks of NS_SnowStorm and NS_Weather_Snow to Saved/Logs/weather_snow_fx_dump.txt; saves nothing
#   create  - (default) (re)creates NS_Weather_Snow, applies the retune below, compiles, saves only NS_Weather_Snow, then dumps
# Result: Saved/Logs/create_weather_snow_fx.txt
#
# What the retune does (old third-person system -> tactical camera 15-45 m above the ground):
# - spawn volume: the old system spawned in an 8 m cylinder around the CAMERA (module NMS_CameraProperties, now disabled); the copy
#   spawns in a cylinder standing on the SYSTEM (Engine.Owner.Position), which UWeatherSnowFxComponent keeps on the ground point the
#   view looks at: radius 30 m, from the ground up to 28 m (above the highest camera at max zoom);
# - flakes: 6-14 cm sprites (were 3-8 cm for a camera 2 m away), 6-9 s lifetime (they fall through the whole volume), faster speed limit;
# - contract User parameters (docs section 5): Snow_SpawnRate -> SpawnRate, Max_Snow_Opacity -> Opacity, Max_SnowSmoke_Opacity ->
#   DriftOpacity, SnowSmoke_SpriteSize -> DriftSpriteSize (renamed everywhere they are read), new WeatherIntensity, WindStrength,
#   WindDirection; the old Min_Snow_Opacity / Min_SnowSmoke_Opacity stay as the low end of the per-particle random opacity;
# - wind: both emitters' WindForce.Wind Speed = User.WindDirection, Wind Speed Scale = WindStrength^2 x K (flakes K = FLAKE_WIND,
#   drift K = DRIFT_WIND): Snowfall (wind 0.45) drifts slowly, Blizzard (1) drives sideways sheets; the module's turbulence stays;
# - ground drift (SnowSmoke): a 2.5 m high band on the ground around the view, more puffs, longer life;
# - flakes render with the new MI_Weather_Snowflake (M_Weather_Snowflake: soft disk, fades out within NEAR_FADE of the camera) instead of
#   the engine's DefaultSpriteMaterial; flakes may stay off screen for OFFSCREEN_SECONDS (the wind carries them into the view).
import os

import unreal

SOURCE = "/Game/Environment/Weather/Snow/Niagara/NS_SnowStorm"
TARGET = "/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow"
FLAKE_MATERIAL = "/Game/Environment/Weather/Snow/Niagara/M_Weather_Snowflake"
FLAKE_INSTANCE = "/Game/Environment/Weather/Snow/Niagara/MI_Weather_Snowflake"
MULTIPLY = "/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float"

FLAKES = "SnowFlakes"
DRIFT = "SnowSmoke"

# Tuning (cm, s). Change here and re-run, or tune NS_Weather_Snow by hand afterwards (see the docs).
SPAWN_RADIUS = 3000.0        # cylinder radius around the view focus (covers the view at max zoom)
SPAWN_HEIGHT = 2800.0        # from the ground up past the highest camera (40 m x sin 35 deg = 23 m)
FLAKE_SIZE = (8.0, 18.0)     # uniform sprite size min / max
FLAKE_LIFETIME = (6.0, 9.0)
FLAKE_SPEED_LIMIT = 2500.0
FLAKE_WIND = 1100.0          # wind speed (cm/s) at WindStrength 1
DRIFT_RADIUS = 2600.0
DRIFT_HEIGHT = 250.0
DRIFT_LIFETIME = (8.0, 12.0)
DRIFT_RATE = (14.0, 24.0)    # puffs per second (random range)
DRIFT_WIND = 1200.0
OFFSCREEN_SECONDS = 30.0     # depth-buffer collision kills a flake after this long off screen (was 1 s: the wind then emptied the view)
# MI_Weather_Snowflake (soft round flake, fades out near the camera so no flake blots the view at close zoom)
FLAKE_NEAR_FADE_START = 300.0
FLAKE_NEAR_FADE_LENGTH = 900.0
FLAKE_SOFTNESS = 0.3          # the flake's soft edge starts at this fraction of its radius
FLAKE_BRIGHTNESS = 1.0

FLAKE_HLSL = r"""
float d = length(UV - 0.5) * 2.0;
float disk = 1.0 - smoothstep(Softness, 1.0, d);
float nearFade = saturate((Depth - NearFadeStart) / max(NearFadeLength, 1.0));
return disk * nearFade;
"""

USER_RENAMES = [
    ("Snow_SpawnRate", "SpawnRate"),
    ("Max_Snow_Opacity", "Opacity"),
    ("Max_SnowSmoke_Opacity", "DriftOpacity"),
    ("SnowSmoke_SpriteSize", "DriftSpriteSize"),
]
USER_NEW = [
    ("WeatherIntensity", "float", "0"),
    ("WindStrength", "float", "0.3"),
    ("WindDirection", "vector", "1,0,0"),
]
USER_DEFAULTS = [
    ("SpawnRate", "float", "5000"),
    ("Opacity", "float", "0.85"),
    ("Min_Snow_Opacity", "float", "0.5"),
    ("DriftOpacity", "float", "0.2"),
    ("Min_SnowSmoke_Opacity", "float", "0"),
    ("DriftSpriteSize", "float", "700"),
]

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "create_weather_snow_fx.txt")
DUMP_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "weather_snow_fx_dump.txt")

LIB = unreal.NiagaraAuthoringLibrary
_lines = []
_failures = []


def log(text):
    _lines.append(text)
    unreal.log("[weather_snow_fx] " + text)


def flush():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")


def call(what, report):
    """Every library call returns its report line ("ERROR: ..." on failure); the dynamic-input call returns the new node name."""
    ok = not str(report).startswith("ERROR")
    log(("ok    " if ok else "FAIL  ") + what + ": " + str(report))
    if not ok:
        _failures.append(what)
    return report if ok else ""


def dump():
    with open(DUMP_PATH, "w", encoding="utf-8") as handle:
        for path in (SOURCE, TARGET):
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                handle.write(LIB.dump_niagara_system(unreal.load_asset(path)) + "\n\n")
    log("dump written to " + DUMP_PATH)


def wind_squared(system, emitter, k):
    """WindForce.Wind Speed = User.WindDirection; Wind Speed Scale = WindStrength x (WindStrength x k). Returns the inner node name."""
    call(emitter + " wind direction", LIB.link_module_input(system, emitter, "WindForce", "Wind Speed", "User.WindDirection"))
    outer = call(emitter + " wind scale", LIB.set_module_input_dynamic_input(system, emitter, "WindForce", "Wind Speed Scale", MULTIPLY))
    call(emitter + " wind a", LIB.link_module_input(system, emitter, outer, "A", "User.WindStrength"))
    inner = call(emitter + " wind b", LIB.set_module_input_dynamic_input(system, emitter, outer, "B", MULTIPLY))
    call(emitter + " wind b.a", LIB.link_module_input(system, emitter, inner, "A", "User.WindStrength"))
    return inner


def remove_previous(paths):
    """Previous outputs of this script (never loaded in this session) are removed from disk and the registry rescanned: deleting them
    through the editor keeps the packages in memory and the new assets could not take their names."""
    removed = False
    for path in paths:
        asset_file = os.path.join(PROJECT_DIR, "Content", path[len("/Game/"):] + ".uasset")
        if os.path.exists(asset_file):
            log("removing the previous " + asset_file)
            os.remove(asset_file)
            removed = True
    if removed:
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([os.path.dirname(TARGET)], True)


def create_flake_material():
    """M_Weather_Snowflake (translucent unlit Niagara sprite: particle colour x soft disk x near-camera fade) + MI_Weather_Snowflake."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    folder, name = FLAKE_MATERIAL.rsplit("/", 1)
    material = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_niagara_sprites", True)

    def scalar(param, value, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, y)
        node.set_editor_property("parameter_name", param)
        node.set_editor_property("default_value", value)
        return node

    color = mel.create_material_expression(material, unreal.MaterialExpressionParticleColor, -900, -300)
    uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, -100)
    depth = mel.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -900, 0)
    near_start = scalar("NearFadeStart", FLAKE_NEAR_FADE_START, 100)
    near_length = scalar("NearFadeLength", FLAKE_NEAR_FADE_LENGTH, 200)
    softness = scalar("Softness", FLAKE_SOFTNESS, 300)
    brightness = scalar("Brightness", FLAKE_BRIGHTNESS, -400)
    custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -550, 50)
    custom.set_editor_property("code", FLAKE_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("description", "Snowflake disk x near fade")
    inputs = []
    for input_name in ("UV", "Depth", "NearFadeStart", "NearFadeLength", "Softness"):
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    mel.connect_material_expressions(uv, "", custom, "UV")
    mel.connect_material_expressions(depth, "", custom, "Depth")
    mel.connect_material_expressions(near_start, "", custom, "NearFadeStart")
    mel.connect_material_expressions(near_length, "", custom, "NearFadeLength")
    mel.connect_material_expressions(softness, "", custom, "Softness")
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
    mel.connect_material_expressions(color, "A", opacity, "A")
    mel.connect_material_expressions(custom, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, -300)
    mel.connect_material_expressions(color, "", emissive, "A")
    mel.connect_material_expressions(brightness, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(FLAKE_MATERIAL, only_if_is_dirty=False)

    folder, name = FLAKE_INSTANCE.rsplit("/", 1)
    instance = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, material)
    for param, value in (("NearFadeStart", FLAKE_NEAR_FADE_START), ("NearFadeLength", FLAKE_NEAR_FADE_LENGTH), ("Softness", FLAKE_SOFTNESS),
                         ("Brightness", FLAKE_BRIGHTNESS)):
        mel.set_material_instance_scalar_parameter_value(instance, param, value)
    unreal.EditorAssetLibrary.save_asset(FLAKE_INSTANCE, only_if_is_dirty=False)
    log("created " + FLAKE_MATERIAL + " + " + FLAKE_INSTANCE)
    return instance


def create():
    remove_previous([TARGET, FLAKE_INSTANCE, FLAKE_MATERIAL])
    instance = create_flake_material()
    if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET):
        raise RuntimeError("duplicate failed")
    system = unreal.load_asset(TARGET)
    log("duplicated " + SOURCE + " -> " + TARGET)

    # 1. Graph structure (user parameters, links, dynamic inputs, cleared overrides).
    for old, new in USER_RENAMES:
        call("rename " + old, LIB.rename_user_parameter(system, old, new))
    for name, type_name, default in USER_NEW + USER_DEFAULTS:
        call("user " + name, LIB.add_user_parameter(system, name, type_name, default))
    for emitter in (FLAKES, DRIFT):
        call(emitter + " origin", LIB.link_module_input(system, emitter, "ShapeLocation", "Shape Origin", "Engine.Owner.Position"))
        call(emitter + " radius", LIB.clear_module_input(system, emitter, "ShapeLocation", "Cylinder Radius"))
    call("camera module off", LIB.set_module_enabled(system, "", "NMS_CameraProperties", False))
    flake_wind = wind_squared(system, FLAKES, FLAKE_WIND)
    drift_wind = wind_squared(system, DRIFT, DRIFT_WIND)
    call("flake material", LIB.set_renderer_property(system, FLAKES, 0, "Material", instance.get_path_name()))
    # The flakes blow up to ~80 m downwind of the system (UWeatherSnowFxComponent moves it upwind of the view in a blizzard).
    call("flake bounds", LIB.set_emitter_fixed_bounds(system, FLAKES, unreal.Vector(9000, 9000, SPAWN_HEIGHT + 600)))
    call("compile 1", LIB.compile_system(system))

    # 2. Constants (rapid iteration values exist once the structure compiled).
    def const(emitter, name, value):
        text = ",".join(str(v) for v in value) if isinstance(value, (tuple, list)) else str(value)
        call("%s %s" % (emitter, name), LIB.set_rapid_iteration_value(system, emitter, name, text))

    const(FLAKES, "ShapeLocation.Cylinder Radius", SPAWN_RADIUS)
    const(FLAKES, "ShapeLocation.Cylinder Height", SPAWN_HEIGHT)
    const(FLAKES, "ShapeLocation.Cylinder Height Midpoint", 0.0)
    const(FLAKES, "InitializeParticle.Uniform Sprite Size Min", FLAKE_SIZE[0])
    const(FLAKES, "InitializeParticle.Uniform Sprite Size Max", FLAKE_SIZE[1])
    const(FLAKES, "InitializeParticle.Lifetime Min", FLAKE_LIFETIME[0])
    const(FLAKES, "InitializeParticle.Lifetime Max", FLAKE_LIFETIME[1])
    const(FLAKES, "SolveForcesAndVelocity.Speed Limit", FLAKE_SPEED_LIMIT)
    const(FLAKES, flake_wind + ".B", FLAKE_WIND)
    const(FLAKES, "Collision.Maximum time offscreen", OFFSCREEN_SECONDS)
    const(DRIFT, "ShapeLocation.Cylinder Radius", DRIFT_RADIUS)
    const(DRIFT, "ShapeLocation.Cylinder Height", DRIFT_HEIGHT)
    const(DRIFT, "ShapeLocation.Cylinder Height Midpoint", 0.3)
    const(DRIFT, "InitializeParticle.Lifetime Min", DRIFT_LIFETIME[0])
    const(DRIFT, "InitializeParticle.Lifetime Max", DRIFT_LIFETIME[1])
    const(DRIFT, "RandomRangeFloat.Minimum", DRIFT_RATE[0])
    const(DRIFT, "RandomRangeFloat.Maximum", DRIFT_RATE[1])
    const(DRIFT, "SolveForcesAndVelocity.Speed Limit", FLAKE_SPEED_LIMIT)
    const(DRIFT, drift_wind + ".B", DRIFT_WIND)
    call("compile 2", LIB.compile_system(system))

    if not unreal.EditorAssetLibrary.save_asset(TARGET, only_if_is_dirty=False):
        raise RuntimeError("save failed")
    log("saved " + TARGET)


def main():
    mode = os.environ.get("CODEX_SNOWFX_MODE", "create")
    log("mode " + mode)
    if mode == "create":
        create()
    dump()
    log("failures: %d %s" % (len(_failures), _failures))


try:
    main()
except Exception as error:  # noqa: BLE001 - report every failure in the log file
    log("ERROR " + repr(error))
    raise
finally:
    flush()
