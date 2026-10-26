# Section 18 (docs/systems/environment-weather.md): the RAIN visuals of temperate levels for the top-down tactical camera, and DA_RainFX,
# their editable look. Builds (all under /Game/Environment/Weather/Rain/, own assets; the migrated originals stay UNTOUCHED):
#   Niagara/M_Weather_Raindrop + MI_Weather_Raindrop   lit translucent streak (volumetric non-directional lighting: dark at night, pale
#                                                     by day), soft core across, tapered ends, near-camera fade, per-drop random opacity
#   Niagara/NS_Weather_Rain                           retuned copy of NS_Raindrops: MainRaindropsGPU = the rain (a cylinder on the
#                                                     SYSTEM, which UWeatherRainFxComponent keeps on the view focus; velocity-aligned
#                                                     streaks; wind slant via a spawn velocity; depth-buffer collision kills the drops
#                                                     on the ground / roofs; 4 kill boxes indoors); RainSplashes = small ground splashes
#                                                     spawned on a disc around the view (no CPU traces); MainRaindropsCPU idle (rate 0)
#   Niagara/NS_Weather_PuddleSplashes                 copy of NS_PuddleSplashes: a spawn-rate disc of splash crowns (was one burst)
#   PostProcess/PPM_Weather_RainScreenDrops + PPMI_    copy of PPM_RainDropsOnScreen with the old MPC_WeatherSettings switches replaced
#                                                     by a scalar Strength (the component sets it: DA_RainFX x MPC ScreenDrops x RainIntensity)
#   DA_RainFX                                         URainFxSettings with the C++ defaults (the user tunes it)
#
# NEVER WIPES HAND EDITS: an existing asset is kept and only reported. CODEX_RAINFX_FORCE=1 deletes and rebuilds them all (back up hand
# edits first). CODEX_RAINFX_ONLY=<comma list of keys: material, rain, puddles, screen, settings> limits a forced rebuild.
# Run (editor commandlet, holding the build lock, CODEX_AGENT=claude):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -unattended -nullrhi
# CODEX_RAINFX_MODE: create (default) | dump (writes the stacks to Saved/Logs/weather_rain_fx_dump.txt). Result: Saved/Logs/create_weather_rain_fx.txt
import os

import unreal

ROOT = "/Game/Environment/Weather/Rain"
SRC_RAIN = ROOT + "/Niagara/NS_Raindrops"
SRC_PUDDLE = ROOT + "/Niagara/NS_PuddleSplashes"
SRC_SCREEN = ROOT + "/PostProcess/PPM_RainDropsOnScreen"
RAIN = ROOT + "/Niagara/NS_Weather_Rain"
PUDDLE = ROOT + "/Niagara/NS_Weather_PuddleSplashes"
DROP_MATERIAL = ROOT + "/Niagara/M_Weather_Raindrop"
DROP_INSTANCE = ROOT + "/Niagara/MI_Weather_Raindrop"
SCREEN = ROOT + "/PostProcess/PPM_Weather_RainScreenDrops"
SCREEN_INSTANCE = ROOT + "/PostProcess/PPMI_Weather_RainScreenDrops"
SETTINGS = ROOT + "/DA_RainFX"

MULT_V2_F = "/Niagara/DynamicInputs/Multiply/Multiply_Vector2D_ByFloat.Multiply_Vector2D_ByFloat"
RANDOM_FLOAT = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"
KILL_MODULE = "/Niagara/Modules/Update/Lifetime/KillParticlesInVolume.KillParticlesInVolume"
SPAWN_RATE_MODULE = "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"
SHAPE_MODULE = "/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation"
ADD_VELOCITY_MODULE = "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity"

GPU = "MainRaindropsGPU"
CPU = "MainRaindropsCPU"
SPLASH = "RainSplashes"
BURST = "SimpleSpriteBurst"
KILL_BOXES = 4                                         # = WeatherRainFxRules::KillBoxCount
KILL_SHAPE_SWITCH = ("Kill Shape", "NewEnumerator1")  # Sphere, Box, Plane, Slab, Cone
CYLINDER = ("Shape Primitive", "NewEnumerator1")      # ShapeLocation V2: Box/Plane, Cylinder, Cone, ... (NS_Raindrops' cylinder)

# Drop material defaults (DA_RainFX overrides them through a dynamic instance).
DROP_HLSL = r"""
// UV.x across the streak, UV.y along it (a velocity-aligned sprite).
float across = abs(UV.x - 0.5) * 2.0;
float core = saturate(1.0 - across);
core = core * sqrt(core);
// Tapered ends (a motion-blurred drop), brightest in the middle.
float along = saturate(sin(UV.y * 3.14159265));
float nearFade = saturate((Depth - NearFadeStart) / max(NearFadeLength, 1.0));
float randomOpacity = lerp(saturate(OpacityRandomMin), 1.0, Random);
return core * along * nearFade * randomOpacity;
"""

USER_RAIN = [
    ("WeatherIntensity", "float", "0"),
    ("SpawnRate", "float", "8000"),
    ("DropColor", "color", "0.72,0.8,0.9,0.5"),
    ("DropSize", "vector2", "3.5,110"),
    ("DropSizeRandomMin", "float", "0.5"),
    ("DropGravity", "vector", "0,0,-4000"),
    ("FallSpeed", "float", "1100"),
    ("DropLifetime", "float", "3"),
    ("DropWindVelocity", "vector", "0,0,0"),
    ("SpawnRadius", "float", "2600"),
    ("SpawnHeight", "float", "2600"),
    ("SplashRate", "float", "1500"),
    ("SplashSizeMin", "float", "14"),
    ("SplashSizeMax", "float", "28"),
    ("SplashRadius", "float", "2200"),
    ("SplashTint", "vector", "0.55,0.55,0.55"),
    ("CpuDropRate", "float", "0"),
] + [(f"KillBox{i}Origin", "position", "0,0,-10000000") for i in range(KILL_BOXES)] \
  + [(f"KillBox{i}Size", "vector", "0,0,0") for i in range(KILL_BOXES)]
USER_PUDDLE = [
    ("SpawnRate", "float", "300"),
    ("SizeMin", "float", "30"),
    ("SizeMax", "float", "60"),
    ("Radius", "float", "2000"),
    ("Tint", "vector", "0.5,0.5,0.5"),
]

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "create_weather_rain_fx.txt")
DUMP_PATH = os.path.join(PROJECT_DIR, "Saved", "Logs", "weather_rain_fx_dump.txt")
FORCE = os.environ.get("CODEX_RAINFX_FORCE", "0") == "1"
ONLY = set(k.strip() for k in os.environ.get("CODEX_RAINFX_ONLY", "").split(",") if k.strip())

LIB = unreal.NiagaraAuthoringLibrary
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
_lines = []
_failures = []


def log(text):
    _lines.append(text)
    unreal.log("[weather_rain_fx] " + text)


def flush():
    os.makedirs(os.path.dirname(LOG_PATH), exist_ok=True)
    with open(LOG_PATH, "w", encoding="utf-8") as handle:
        handle.write("\n".join(_lines) + "\n")


def call(what, report):
    ok = not str(report).startswith("ERROR")
    log(("ok    " if ok else "FAIL  ") + what + ": " + str(report))
    if not ok:
        _failures.append(what)
    return report if ok else ""


def asset_file(path):
    return os.path.join(PROJECT_DIR, "Content", path[len("/Game/"):] + ".uasset")


def exists(path):
    return os.path.exists(asset_file(path))


def wanted(key):
    return not ONLY or key in ONLY


def remove_previous(paths):
    """Previous outputs of this script are removed from disk and the registry rescanned (an editor delete keeps the package in memory)."""
    removed = False
    for path in paths:
        if os.path.exists(asset_file(path)):
            log("removing the previous " + asset_file(path))
            os.remove(asset_file(path))
            removed = True
    if removed:
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        registry.scan_paths_synchronous([ROOT + "/Niagara", ROOT + "/PostProcess", ROOT], True)


def duplicate(source, target):
    asset = eal.duplicate_asset(source, target)
    if not asset:
        folder, name = target.rsplit("/", 1)
        asset = tools.duplicate_asset(name, folder, unreal.load_asset(source))
    if not asset:
        raise RuntimeError("duplicate failed: " + target)
    log("duplicated " + source + " -> " + target + " (the source stays untouched)")
    return unreal.load_asset(target)


def custom_node(material, code, description, input_names, output_type, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("description", description)
    items = []
    for input_name in input_names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        items.append(item)
    node.set_editor_property("inputs", items)
    return node


# ---------------------------------------------------------------------------------------------------------------------
# Drop material
# ---------------------------------------------------------------------------------------------------------------------
def create_drop_material():
    folder, name = DROP_MATERIAL.rsplit("/", 1)
    material = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    # Lit by the translucency lighting volume (sky + sun, cheap, no per-pixel shadows): the rain is pale by day and dark at night.
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    material.set_editor_property("used_with_niagara_sprites", True)
    # Thin fast streaks: TSR / TAA would smear them away without the responsive flag.
    material.set_editor_property("enable_responsive_aa", True)

    def scalar(param, value, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, y)
        node.set_editor_property("parameter_name", param)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "Rain")
        return node

    color = mel.create_material_expression(material, unreal.MaterialExpressionParticleColor, -900, -300)
    uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, -100)
    depth = mel.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -900, 0)
    rnd = mel.create_material_expression(material, unreal.MaterialExpressionParticleRandom, -900, 50)
    near_start = scalar("NearFadeStart", 700.0, 100)
    near_length = scalar("NearFadeLength", 1100.0, 200)
    random_min = scalar("OpacityRandomMin", 0.5, 300)
    brightness = scalar("Brightness", 1.0, -400)
    glow = scalar("Glow", 0.0, -500)
    names = ("UV", "Depth", "Random", "NearFadeStart", "NearFadeLength", "OpacityRandomMin")
    custom = custom_node(material, DROP_HLSL, "Rain streak x near fade", names, unreal.CustomMaterialOutputType.CMOT_FLOAT1, -550, 50)
    for input_name, node in zip(names, (uv, depth, rnd, near_start, near_length, random_min)):
        mel.connect_material_expressions(node, "", custom, input_name)
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, 0)
    mel.connect_material_expressions(color, "A", opacity, "A")
    mel.connect_material_expressions(custom, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    base = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -250, -300)
    mel.connect_material_expressions(color, "", base, "A")
    mel.connect_material_expressions(brightness, "", base, "B")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, -450)
    mel.connect_material_expressions(base, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -250, 300)
    rough.set_editor_property("r", 0.3)
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(material)
    eal.save_asset(DROP_MATERIAL, only_if_is_dirty=False)

    folder, name = DROP_INSTANCE.rsplit("/", 1)
    instance = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, material)
    for param, value in (("NearFadeStart", 700.0), ("NearFadeLength", 1100.0), ("OpacityRandomMin", 0.5), ("Brightness", 1.1), ("Glow", 0.0)):
        mel.set_material_instance_scalar_parameter_value(instance, param, value)
    eal.save_asset(DROP_INSTANCE, only_if_is_dirty=False)
    try:
        s = mel.get_statistics(material)
        log("drop material stats: vs %d ps %d samplers %d" % (s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers))
    except Exception as error:  # noqa: BLE001
        log("drop material stats n/a: %s" % error)
    log("created " + DROP_MATERIAL + " + " + DROP_INSTANCE)
    return instance


# ---------------------------------------------------------------------------------------------------------------------
# Niagara
# ---------------------------------------------------------------------------------------------------------------------
def add_kill_boxes(system, emitter):
    names = []
    for i in range(KILL_BOXES):
        name = call("%s kill box %d" % (emitter, i), LIB.add_module(system, emitter, "ParticleUpdate", KILL_MODULE, "KillIndoor%d" % i, ""))
        if name:
            names.append(name)
            call("%s %s shape" % (emitter, name), LIB.set_module_static_switch(system, emitter, name, KILL_SHAPE_SWITCH[0], KILL_SHAPE_SWITCH[1]))
    return names


def link_kill_boxes(system, emitter, names):
    for i, name in enumerate(names):
        call("%s %s origin" % (emitter, name), LIB.link_module_input(system, emitter, name, "Volume Origin", "User.KillBox%dOrigin" % i))
        call("%s %s size" % (emitter, name), LIB.link_module_input(system, emitter, name, "Box Size", "User.KillBox%dSize" % i))


def const(system, emitter, name, value):
    text = ",".join(str(v) for v in value) if isinstance(value, (tuple, list)) else str(value)
    call("%s %s" % (emitter or "system", name), LIB.set_rapid_iteration_value(system, emitter, name, text))


def create_rain(drop_instance):
    system = duplicate(SRC_RAIN, RAIN)
    for name, type_name, default in USER_RAIN:
        call("user " + name, LIB.add_user_parameter(system, name, type_name, default))
    call("camera module off", LIB.set_module_enabled(system, "", "NMS_CameraProperties", False))
    # The CPU emitter (per-drop line traces) stays idle: the splashes come from a ground disc instead (cheap).
    call("cpu rate", LIB.link_module_input(system, CPU, "SpawnRate", "SpawnRate", "User.CpuDropRate"))

    # --- the rain (GPU) ---
    call("gpu rate", LIB.link_module_input(system, GPU, "SpawnRate", "SpawnRate", "User.SpawnRate"))
    call("gpu origin", LIB.link_module_input(system, GPU, "ShapeLocation", "Shape Origin", "Engine.Owner.Position"))
    call("gpu radius", LIB.link_module_input(system, GPU, "ShapeLocation", "Cylinder Radius", "User.SpawnRadius"))
    call("gpu height", LIB.link_module_input(system, GPU, "ShapeLocation", "Cylinder Height", "User.SpawnHeight"))
    call("gpu colour", LIB.link_module_input(system, GPU, "InitializeParticle", "Color", "User.DropColor"))
    call("gpu life min", LIB.link_module_input(system, GPU, "InitializeParticle", "Lifetime Min", "User.DropLifetime"))
    call("gpu life max", LIB.link_module_input(system, GPU, "InitializeParticle", "Lifetime Max", "User.DropLifetime"))
    size = call("gpu size", LIB.set_module_input_dynamic_input(system, GPU, "InitializeParticle", "Sprite Size", MULT_V2_F))
    random_scale = ""
    if size:
        call("gpu size vector", LIB.link_module_input(system, GPU, size, "Vector2D", "User.DropSize"))
        random_scale = call("gpu size random", LIB.set_module_input_dynamic_input(system, GPU, size, "Float", RANDOM_FLOAT))
        if random_scale:
            call("gpu size random min", LIB.link_module_input(system, GPU, random_scale, "Minimum", "User.DropSizeRandomMin"))
    call("gpu gravity", LIB.link_module_input(system, GPU, "GravityForce", "Gravity", "User.DropGravity"))
    call("gpu speed limit", LIB.link_module_input(system, GPU, "SolveForcesAndVelocity", "Speed Limit", "User.FallSpeed"))
    call("gpu brightness by distance off", LIB.clear_module_input(system, GPU, "ScaleColor", "Scale RGB"))
    # The old size-over-life curve shrank the streaks to short dashes (stills 2026-10-26): the length is DA_RainFX's.
    call("gpu size over life off", LIB.set_module_enabled(system, GPU, "ScaleSpriteSize", False))
    wind = call("gpu wind velocity", LIB.add_module(system, GPU, "ParticleSpawn", ADD_VELOCITY_MODULE, "WindVelocity", ""))
    # Depth-buffer collision: the drops die where they meet the ground / a roof in view.
    call("gpu collision on", LIB.set_module_enabled(system, GPU, "Collision", True))
    call("gpu kill collided on", LIB.set_module_enabled(system, GPU, "KillParticles", True))
    call("gpu kill on collision", LIB.set_module_static_switch(system, GPU, "Collision", "KillOnCollision", "true"))
    # The old single kill volume (1 m box at the world origin) would cut a hole in the rain.
    call("gpu old kill volume off", LIB.set_module_enabled(system, GPU, "KillParticlesInVolume", False))
    kills = add_kill_boxes(system, GPU)
    call("gpu material", LIB.set_renderer_property(system, GPU, 0, "Material", drop_instance.get_path_name()))
    call("gpu material binding", LIB.bind_renderer_material_to_user_parameter(system, GPU, 0, "DropMaterial"))
    call("gpu sub image", LIB.set_renderer_property(system, GPU, 0, "SubImageSize", "(X=1.000000,Y=1.000000)"))
    # The system stands up to 15 m upwind of the view; slanted drops travel ~20 m sideways in their life.
    call("gpu bounds", LIB.set_emitter_fixed_bounds(system, GPU, unreal.Vector(6500, 6500, 3200)))

    # --- ground splashes (CPU, a disc around the view; the old event-driven spawn never fires: the CPU drops are idle) ---
    splash_rate = call("splash spawn rate", LIB.add_module(system, SPLASH, "EmitterUpdate", SPAWN_RATE_MODULE, "SplashSpawnRate", ""))
    splash_shape = call("splash shape", LIB.add_module(system, SPLASH, "ParticleSpawn", SHAPE_MODULE, "SplashDisc", ""))
    if splash_shape:
        call("splash cylinder", LIB.set_module_static_switch(system, SPLASH, splash_shape, CYLINDER[0], CYLINDER[1]))
    call("splash size min", LIB.link_module_input(system, SPLASH, "InitializeParticle", "Uniform Sprite Size Min", "User.SplashSizeMin"))
    call("splash size max", LIB.link_module_input(system, SPLASH, "InitializeParticle", "Uniform Sprite Size Max", "User.SplashSizeMax"))
    call("splash tint", LIB.link_module_input(system, SPLASH, "ScaleColor", "Scale RGB", "User.SplashTint"))
    call("splash bounds", LIB.set_emitter_fixed_bounds(system, SPLASH, unreal.Vector(3000, 3000, 400)))
    call("compile 1", LIB.compile_system(system))

    # --- inputs of the added modules, then constants ---
    link_kill_boxes(system, GPU, kills)
    if wind:
        call("gpu wind link", LIB.link_module_input(system, GPU, wind, "Velocity", "User.DropWindVelocity"))
    if splash_rate:
        call("splash rate link", LIB.link_module_input(system, SPLASH, splash_rate, "SpawnRate", "User.SplashRate"))
    if splash_shape:
        call("splash origin", LIB.link_module_input(system, SPLASH, splash_shape, "Shape Origin", "Engine.Owner.Position"))
        call("splash radius", LIB.link_module_input(system, SPLASH, splash_shape, "Cylinder Radius", "User.SplashRadius"))
    const(system, GPU, "ShapeLocation.Cylinder Height Midpoint", 0.0)
    const(system, GPU, "Collision.Maximum time offscreen", 30.0)
    const(system, GPU, "ScaleColor.Scale RGB", (1.0, 1.0, 1.0))
    if random_scale:
        const(system, GPU, random_scale + ".Maximum", 1.0)
    if splash_shape:
        const(system, SPLASH, splash_shape + ".Cylinder Height", 2.0)
        const(system, SPLASH, splash_shape + ".Cylinder Height Midpoint", 0.0)
    const(system, SPLASH, "InitializeParticle.Position Offset", (0.0, 0.0, 6.0))
    call("compile 2", LIB.compile_system(system))
    if not eal.save_asset(RAIN, only_if_is_dirty=False):
        raise RuntimeError("save failed: " + RAIN)
    log("saved " + RAIN)


def create_puddles():
    system = duplicate(SRC_PUDDLE, PUDDLE)
    for name, type_name, default in USER_PUDDLE:
        call("user " + name, LIB.add_user_parameter(system, name, type_name, default))
    call("camera module off", LIB.set_module_enabled(system, "", "NMS_CameraProperties", False))
    call("burst off", LIB.set_module_enabled(system, BURST, "SpawnBurst_Instantaneous", False))
    rate = call("spawn rate", LIB.add_module(system, BURST, "EmitterUpdate", SPAWN_RATE_MODULE, "PuddleSpawnRate", ""))
    shape = call("shape", LIB.add_module(system, BURST, "ParticleSpawn", SHAPE_MODULE, "PuddleDisc", ""))
    if shape:
        call("cylinder", LIB.set_module_static_switch(system, BURST, shape, CYLINDER[0], CYLINDER[1]))
    call("life cycle by the system", LIB.set_module_static_switch(system, BURST, "EmitterState", "Life Cycle Mode", "NewEnumerator0"))
    call("size min", LIB.link_module_input(system, BURST, "InitializeParticle", "Uniform Sprite Size Min", "User.SizeMin"))
    call("size max", LIB.link_module_input(system, BURST, "InitializeParticle", "Uniform Sprite Size Max", "User.SizeMax"))
    call("tint", LIB.link_module_input(system, BURST, "ScaleColor", "Scale RGB", "User.Tint"))
    call("bounds", LIB.set_emitter_fixed_bounds(system, BURST, unreal.Vector(2800, 2800, 400)))
    call("compile 1", LIB.compile_system(system))
    if rate:
        call("rate link", LIB.link_module_input(system, BURST, rate, "SpawnRate", "User.SpawnRate"))
    if shape:
        call("origin", LIB.link_module_input(system, BURST, shape, "Shape Origin", "Engine.Owner.Position"))
        call("radius", LIB.link_module_input(system, BURST, shape, "Cylinder Radius", "User.Radius"))
        const(system, BURST, shape + ".Cylinder Height", 2.0)
        const(system, BURST, shape + ".Cylinder Height Midpoint", 0.0)
    const(system, BURST, "InitializeParticle.Position Offset", (0.0, 0.0, 4.0))
    call("compile 2", LIB.compile_system(system))
    if not eal.save_asset(PUDDLE, only_if_is_dirty=False):
        raise RuntimeError("save failed: " + PUDDLE)
    log("saved " + PUDDLE)


# ---------------------------------------------------------------------------------------------------------------------
# Screen drops
# ---------------------------------------------------------------------------------------------------------------------
def walk(material, start):
    """Every expression reachable from start: (expression, [(input index, input name, child)])."""
    seen = {}
    stack = [start]
    while stack:
        expr = stack.pop()
        key = expr.get_path_name()
        if key in seen:
            continue
        inputs = list(mel.get_inputs_for_material_expression(material, expr))
        names = list(mel.get_material_expression_input_names(expr))
        seen[key] = (expr, [(i, names[i] if i < len(names) else str(i), c) for i, c in enumerate(inputs) if c is not None])
        stack.extend(c for c in inputs if c is not None)
    return list(seen.values())


def create_screen():
    material = duplicate(SRC_SCREEN, SCREEN)
    root = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    graph = walk(material, root)
    collection_nodes = [e for e, _ in graph if isinstance(e, unreal.MaterialExpressionCollectionParameter)]
    gate = None
    consumer = None
    for expr, inputs in graph:
        children = [c for _, _, c in inputs]
        if isinstance(expr, unreal.MaterialExpressionMultiply) and children and all(isinstance(c, unreal.MaterialExpressionCollectionParameter) for c in children):
            gate = expr
    if gate:
        for expr, inputs in graph:
            for _, name, child in inputs:
                if child == gate:
                    consumer = (expr, name)
    if not gate or not consumer:
        raise RuntimeError("PPM graph: the MPC_WeatherSettings gate was not found (%d collection nodes)" % len(collection_nodes))
    strength = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 600)
    strength.set_editor_property("parameter_name", "Strength")
    strength.set_editor_property("default_value", 0.0)
    strength.set_editor_property("group", "Rain")
    if not mel.connect_material_expressions(strength, "", consumer[0], consumer[1]):
        raise RuntimeError("PPM graph: Strength could not be connected")
    mel.delete_material_expression(material, gate)
    for node in collection_nodes:
        mel.delete_material_expression(material, node)
    errors = mel.recompile_material(material)
    log("screen drops: replaced the MPC_WeatherSettings gate (%d nodes) by Strength -> %s; compile errors: %s" % (
        len(collection_nodes) + 1, consumer[1], list(errors) if errors else "none"))
    eal.save_asset(SCREEN, only_if_is_dirty=False)
    folder, name = SCREEN_INSTANCE.rsplit("/", 1)
    instance = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, material)
    # The old instance's values (PPMI_RainDropsOnScreen) + a subtle default strength; the component overrides Strength / Tiling / Speed.
    for param, value in (("AnimationSpeed", 0.3), ("RaindDropsNormalStrength", 0.03), ("RainDropsTiling", 0.9), ("Strength", 0.3)):
        mel.set_material_instance_scalar_parameter_value(instance, param, value)
    eal.save_asset(SCREEN_INSTANCE, only_if_is_dirty=False)
    try:
        s = mel.get_statistics(material)
        log("screen drops stats: vs %d ps %d samplers %d" % (s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers))
    except Exception as error:  # noqa: BLE001
        log("screen drops stats n/a: %s" % error)
    log("created " + SCREEN + " + " + SCREEN_INSTANCE)


def create_settings():
    folder, name = SETTINGS.rsplit("/", 1)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.RainFxSettings)
    asset = tools.create_asset(name, folder, unreal.RainFxSettings, factory)
    if not asset or not eal.save_asset(SETTINGS, only_if_is_dirty=False):
        raise RuntimeError("DA_RainFX create / save failed")
    tuning = asset.get_editor_property("tuning")
    log("created %s (drops %.0f / s at full, %.0f-%.0f cm long)" % (SETTINGS, tuning.get_editor_property("spawn_rate_at_full"),
                                                                 tuning.get_editor_property("drop_length_min"), tuning.get_editor_property("drop_length_max")))


def dump():
    with open(DUMP_PATH, "w", encoding="utf-8") as handle:
        for path in (RAIN, PUDDLE):
            if exists(path):
                handle.write(LIB.dump_niagara_system(unreal.load_asset(path)) + "\n\n")
    log("dump written to " + DUMP_PATH)


def create():
    log("force %s, only %s" % (FORCE, sorted(ONLY) if ONLY else "all"))
    groups = {
        "material": [DROP_INSTANCE, DROP_MATERIAL],
        "rain": [RAIN],
        "puddles": [PUDDLE],
        "screen": [SCREEN_INSTANCE, SCREEN],
        "settings": [SETTINGS],
    }
    if FORCE:
        remove_previous([p for key, paths in groups.items() if wanted(key) for p in paths])
    if exists(DROP_INSTANCE) and exists(DROP_MATERIAL):
        log("kept the existing " + DROP_INSTANCE)
        drop_instance = unreal.load_asset(DROP_INSTANCE)
    else:
        drop_instance = create_drop_material()
    for key, path, build in (("rain", RAIN, lambda: create_rain(drop_instance)), ("puddles", PUDDLE, create_puddles),
                             ("screen", SCREEN, create_screen), ("settings", SETTINGS, create_settings)):
        if exists(path):
            log("kept the existing " + path + " (hand edits are safe; CODEX_RAINFX_FORCE=1 rebuilds it)")
        else:
            build()


def main():
    mode = os.environ.get("CODEX_RAINFX_MODE", "create")
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
