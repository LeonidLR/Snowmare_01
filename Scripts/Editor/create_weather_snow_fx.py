# Environment phase 4b: NS_Weather_Snow, the falling snow for the top-down tactical camera, and DA_SnowFX, its editable look
# (docs/systems/environment-weather.md section 12.b).
#
# Duplicates the migrated NS_SnowStorm (the user's original system from WeatherSystem_01; it stays UNTOUCHED) into
# /Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow and retunes the copy through UNiagaraAuthoringLibrary (CodexTacticsEditor, C++:
# UE 5.8 has no Python API for Niagara stacks).
#
# NEVER WIPES HAND EDITS: an existing NS_Weather_Snow / MI_Weather_Snowflake / M_Weather_Snowflake / DA_SnowFX is kept as it is and the
# script only reports it. Set CODEX_SNOWFX_FORCE=1 to delete and rebuild them (hand edits in those assets are then lost - back them up).
#
# Run (editor commandlet; take the build lock first: Scripts/agent_lock.ps1, CODEX_AGENT=claude):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# Environment variable CODEX_SNOWFX_MODE:
#   dump    - writes the stacks of NS_SnowStorm and NS_Weather_Snow to Saved/Logs/weather_snow_fx_dump.txt; saves nothing
#   create  - (default) creates what is missing (all four assets with CODEX_SNOWFX_FORCE=1), compiles, saves only those, then dumps
# Result: Saved/Logs/create_weather_snow_fx.txt
#
# What the retune does (old third-person system -> tactical camera 15-45 m above the ground):
# - spawn volume: the old system spawned in an 8 m cylinder around the CAMERA (module NMS_CameraProperties, now disabled); the copy
#   spawns in a cylinder standing on the SYSTEM (Engine.Owner.Position), which UWeatherSnowFxComponent keeps on the ground point the
#   view looks at;
# - USER PARAMETERS for everything the user tunes in DA_SnowFX (the component writes them every refresh; their defaults here = the C++
#   defaults of FWeatherSnowFxTuning, used when the system runs without the component):
#     flakes: FlakeSizeMin / FlakeSizeMax (InitializeParticle sprite size), FlakeLifetimeMin / Max, Min_Snow_Opacity, FlakeGravity
#     (GravityForce), SpeedLimit (both emitters), FlakeWindSpeed (wind speed at WindStrength 1), SpawnRadius / SpawnHeight (ShapeLocation),
#     FlakeMaterial (the sprite material, a dynamic instance of MI_Weather_Snowflake: NearFadeStart / NearFadeLength / Softness / Brightness);
#     drift: DriftRate (SpawnRate), DriftOpacity, Min_SnowSmoke_Opacity, DriftSpriteSize, DriftWindSpeed, DriftRadius / DriftHeight;
#     contract (docs section 5): SpawnRate, Opacity, WeatherIntensity, WindStrength, WindDirection;
# - NO SNOW INDOORS: KILL_BOXES x KillParticlesInVolume (box) at the end of both emitters' particle update, Volume Origin <-
#   User.KillBox<i>Origin, Box Size <- User.KillBox<i>Size (world axis-aligned, full size; the component writes the nearest interior camera
#   zones / roofs, unused slots far below the world); flakes also die on any surface they hit (depth-buffer collision, the Collision module's
#   KillOnCollision switch on: a roof in view stops them);
# - wind: both emitters' WindForce.Wind Speed = User.WindDirection, Wind Speed Scale = WindStrength^2 x <emitter wind speed>;
# - ground drift (SnowSmoke): a band on the ground around the view;
# - flakes render with MI_Weather_Snowflake (M_Weather_Snowflake: soft disk, fades out within NearFadeStart of the camera); flakes may stay
#   off screen for OFFSCREEN_SECONDS (the wind carries them into the view).
import os

import unreal

SOURCE = "/Game/Environment/Weather/Snow/Niagara/NS_SnowStorm"
TARGET = "/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow"
FLAKE_MATERIAL = "/Game/Environment/Weather/Snow/Niagara/M_Weather_Snowflake"
FLAKE_INSTANCE = "/Game/Environment/Weather/Snow/Niagara/MI_Weather_Snowflake"
SETTINGS = "/Game/Environment/Weather/Snow/DA_SnowFX"
MULTIPLY = "/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float"
KILL_MODULE = "/Niagara/Modules/Update/Lifetime/KillParticlesInVolume.KillParticlesInVolume"

FLAKES = "SnowFlakes"
DRIFT = "SnowSmoke"
KILL_BOXES = 4                # = WeatherSnowFxRules::KillBoxCount
# KillParticlesInVolume's shape static switch (ENiagaraKillVolumeOptions; the dump lists the module's switches).
KILL_SHAPE_SWITCH = ("Kill Shape", "NewEnumerator1")  # Sphere, Box, Plane, Slab, Cone

# Fixed tuning (cm, s): the rest is a User parameter (DA_SnowFX).
FLAKE_SPEED_LIMIT = 2500.0
OFFSCREEN_SECONDS = 30.0     # depth-buffer collision kills a flake after this long off screen (was 1 s: the wind then emptied the view)
# MI_Weather_Snowflake defaults (DA_SnowFX overrides them through a dynamic instance)
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
# (name, type, default) - defaults = FWeatherSnowFxTuning's C++ defaults at intensity 1.
USER_NEW = [
    ("WeatherIntensity", "float", "0"),
    ("WindStrength", "float", "0.3"),
    ("WindDirection", "vector", "1,0,0"),
    ("FlakeSizeMin", "float", "5"),
    ("FlakeSizeMax", "float", "11"),
    ("FlakeLifetimeMin", "float", "6"),
    ("FlakeLifetimeMax", "float", "9"),
    ("FlakeGravity", "vector", "0,0,-980"),
    ("SpeedLimit", "float", str(FLAKE_SPEED_LIMIT)),
    ("FlakeWindSpeed", "float", "1100"),
    ("SpawnRadius", "float", "3000"),
    ("SpawnHeight", "float", "2800"),
    ("DriftRate", "float", "24"),
    ("DriftWindSpeed", "float", "1200"),
    ("DriftRadius", "float", "2600"),
    ("DriftHeight", "float", "250"),
] + [(f"KillBox{i}Origin", "position", "0,0,-10000000") for i in range(KILL_BOXES)] \
  + [(f"KillBox{i}Size", "vector", "0,0,0") for i in range(KILL_BOXES)]
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
FORCE = os.environ.get("CODEX_SNOWFX_FORCE", "0") == "1"

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
    """Every library call returns its report line ("ERROR: ..." on failure); the dynamic-input / add-module calls return the new node name."""
    ok = not str(report).startswith("ERROR")
    log(("ok    " if ok else "FAIL  ") + what + ": " + str(report))
    if not ok:
        _failures.append(what)
    return report if ok else ""


def asset_file(path):
    return os.path.join(PROJECT_DIR, "Content", path[len("/Game/"):] + ".uasset")


def exists(path):
    """On disk (the asset registry still lists a package this script just deleted)."""
    return os.path.exists(asset_file(path))


def dump():
    with open(DUMP_PATH, "w", encoding="utf-8") as handle:
        for path in (SOURCE, TARGET):
            if exists(path):
                handle.write(LIB.dump_niagara_system(unreal.load_asset(path)) + "\n\n")
    log("dump written to " + DUMP_PATH)


def wind_squared(system, emitter, speed_param):
    """WindForce.Wind Speed = User.WindDirection; Wind Speed Scale = WindStrength x (WindStrength x User.<speed_param>)."""
    call(emitter + " wind direction", LIB.link_module_input(system, emitter, "WindForce", "Wind Speed", "User.WindDirection"))
    outer = call(emitter + " wind scale", LIB.set_module_input_dynamic_input(system, emitter, "WindForce", "Wind Speed Scale", MULTIPLY))
    call(emitter + " wind a", LIB.link_module_input(system, emitter, outer, "A", "User.WindStrength"))
    inner = call(emitter + " wind b", LIB.set_module_input_dynamic_input(system, emitter, outer, "B", MULTIPLY))
    call(emitter + " wind b.a", LIB.link_module_input(system, emitter, inner, "A", "User.WindStrength"))
    call(emitter + " wind b.b", LIB.link_module_input(system, emitter, inner, "B", "User." + speed_param))
    return inner


def add_kill_boxes(system, emitter):
    """KILL_BOXES KillParticlesInVolume modules (box) at the end of the particle update, each bound to User.KillBox<i>Origin / Size."""
    names = []
    for i in range(KILL_BOXES):
        name = call("%s kill box %d" % (emitter, i), LIB.add_module(system, emitter, "ParticleUpdate", KILL_MODULE, "KillIndoor%d" % i, ""))
        if not name:
            continue
        names.append(name)
        call("%s %s shape" % (emitter, name), LIB.set_module_static_switch(system, emitter, name, KILL_SHAPE_SWITCH[0], KILL_SHAPE_SWITCH[1]))
    return names


def link_kill_boxes(system, emitter, names):
    for i, name in enumerate(names):
        call("%s %s origin" % (emitter, name), LIB.link_module_input(system, emitter, name, "Volume Origin", "User.KillBox%dOrigin" % i))
        call("%s %s size" % (emitter, name), LIB.link_module_input(system, emitter, name, "Box Size", "User.KillBox%dSize" % i))


def remove_previous(paths):
    """Previous outputs of this script (never loaded in this session) are removed from disk and the registry rescanned: deleting them
    through the editor keeps the packages in memory and the new assets could not take their names."""
    removed = False
    for path in paths:
        file_path = asset_file(path)
        if os.path.exists(file_path):
            log("removing the previous " + file_path)
            os.remove(file_path)
            removed = True
    if removed:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([os.path.dirname(TARGET), os.path.dirname(SETTINGS)], True)


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


def create_system(instance):
    system = unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET)
    if not system:
        # Seen after a forced delete in the same session: the asset tools path still works.
        folder, name = TARGET.rsplit("/", 1)
        system = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, folder, unreal.load_asset(SOURCE))
    if not system:
        raise RuntimeError("duplicate failed (" + TARGET + " is now missing: run the script again without CODEX_SNOWFX_FORCE)")
    system = unreal.load_asset(TARGET)
    log("duplicated " + SOURCE + " -> " + TARGET + " (the source stays untouched)")

    # 1. Graph structure (user parameters, links, dynamic inputs, added modules).
    for old, new in USER_RENAMES:
        call("rename " + old, LIB.rename_user_parameter(system, old, new))
    for name, type_name, default in USER_NEW + USER_DEFAULTS:
        call("user " + name, LIB.add_user_parameter(system, name, type_name, default))
    for emitter in (FLAKES, DRIFT):
        call(emitter + " origin", LIB.link_module_input(system, emitter, "ShapeLocation", "Shape Origin", "Engine.Owner.Position"))
        call(emitter + " speed limit", LIB.link_module_input(system, emitter, "SolveForcesAndVelocity", "Speed Limit", "User.SpeedLimit"))
    call("camera module off", LIB.set_module_enabled(system, "", "NMS_CameraProperties", False))
    # Flakes die where they hit the depth buffer (roofs, the ground) instead of sliding on (NS_SnowStorm had the switch off).
    call("flake kill on collision", LIB.set_module_static_switch(system, FLAKES, "Collision", "KillOnCollision", "true"))
    # Flakes: the look from DA_SnowFX.
    call("flake radius", LIB.link_module_input(system, FLAKES, "ShapeLocation", "Cylinder Radius", "User.SpawnRadius"))
    call("flake height", LIB.link_module_input(system, FLAKES, "ShapeLocation", "Cylinder Height", "User.SpawnHeight"))
    call("flake size min", LIB.link_module_input(system, FLAKES, "InitializeParticle", "Uniform Sprite Size Min", "User.FlakeSizeMin"))
    call("flake size max", LIB.link_module_input(system, FLAKES, "InitializeParticle", "Uniform Sprite Size Max", "User.FlakeSizeMax"))
    call("flake life min", LIB.link_module_input(system, FLAKES, "InitializeParticle", "Lifetime Min", "User.FlakeLifetimeMin"))
    call("flake life max", LIB.link_module_input(system, FLAKES, "InitializeParticle", "Lifetime Max", "User.FlakeLifetimeMax"))
    call("flake gravity", LIB.link_module_input(system, FLAKES, "GravityForce", "Gravity", "User.FlakeGravity"))
    wind_squared(system, FLAKES, "FlakeWindSpeed")
    # Drift.
    call("drift radius", LIB.link_module_input(system, DRIFT, "ShapeLocation", "Cylinder Radius", "User.DriftRadius"))
    call("drift height", LIB.link_module_input(system, DRIFT, "ShapeLocation", "Cylinder Height", "User.DriftHeight"))
    call("drift rate", LIB.link_module_input(system, DRIFT, "SpawnRate", "SpawnRate", "User.DriftRate"))
    wind_squared(system, DRIFT, "DriftWindSpeed")
    # No snow indoors.
    kills = {emitter: add_kill_boxes(system, emitter) for emitter in (FLAKES, DRIFT)}
    # Material: MI_Weather_Snowflake, bound to User.FlakeMaterial (the component swaps in a dynamic instance).
    call("flake material", LIB.set_renderer_property(system, FLAKES, 0, "Material", instance.get_path_name()))
    call("flake material binding", LIB.bind_renderer_material_to_user_parameter(system, FLAKES, 0, "FlakeMaterial"))
    # The flakes blow up to ~80 m downwind of the system (UWeatherSnowFxComponent moves it upwind of the view in a blizzard).
    call("flake bounds", LIB.set_emitter_fixed_bounds(system, FLAKES, unreal.Vector(9000, 9000, 2800 + 600)))
    call("compile 1", LIB.compile_system(system))

    # 2. Inputs of the added modules (they exist once the structure compiled), then the constants.
    for emitter, names in kills.items():
        link_kill_boxes(system, emitter, names)

    def const(emitter, name, value):
        text = ",".join(str(v) for v in value) if isinstance(value, (tuple, list)) else str(value)
        call("%s %s" % (emitter, name), LIB.set_rapid_iteration_value(system, emitter, name, text))

    const(FLAKES, "ShapeLocation.Cylinder Height Midpoint", 0.0)
    const(FLAKES, "Collision.Maximum time offscreen", OFFSCREEN_SECONDS)
    const(DRIFT, "ShapeLocation.Cylinder Height Midpoint", 0.3)
    const(DRIFT, "InitializeParticle.Lifetime Min", 8.0)
    const(DRIFT, "InitializeParticle.Lifetime Max", 12.0)
    call("compile 2", LIB.compile_system(system))

    if not unreal.EditorAssetLibrary.save_asset(TARGET, only_if_is_dirty=False):
        raise RuntimeError("save failed")
    log("saved " + TARGET)


def create_settings():
    """DA_SnowFX: a USnowFxSettings with the C++ defaults (the user tunes it in the editor)."""
    folder, name = SETTINGS.rsplit("/", 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.SnowFxSettings)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.SnowFxSettings, factory)
    if not asset or not unreal.EditorAssetLibrary.save_asset(SETTINGS, only_if_is_dirty=False):
        raise RuntimeError("DA_SnowFX create / save failed")
    tuning = asset.get_editor_property("tuning")
    log("created %s (flakes %.1f-%.1f cm, %.0f / s at full)" % (SETTINGS, tuning.get_editor_property("flake_size_min"),
                                                                tuning.get_editor_property("flake_size_max"),
                                                                tuning.get_editor_property("spawn_rate_at_full")))


def create():
    log("force %s" % FORCE)
    if FORCE:
        remove_previous([TARGET, FLAKE_INSTANCE, FLAKE_MATERIAL, SETTINGS])
    if exists(FLAKE_INSTANCE) and exists(FLAKE_MATERIAL):
        log("kept the existing " + FLAKE_INSTANCE + " (CODEX_SNOWFX_FORCE=1 rebuilds it)")
        instance = unreal.load_asset(FLAKE_INSTANCE)
    else:
        instance = create_flake_material()
    if exists(TARGET):
        log("kept the existing " + TARGET + " (hand edits are safe; CODEX_SNOWFX_FORCE=1 rebuilds it)")
    else:
        create_system(instance)
    if exists(SETTINGS):
        log("kept the existing " + SETTINGS + " (CODEX_SNOWFX_FORCE=1 recreates it with the C++ defaults)")
    else:
        create_settings()


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
