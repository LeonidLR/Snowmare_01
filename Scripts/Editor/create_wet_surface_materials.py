# Section 18 (docs/systems/environment-weather.md): the WET look of temperate levels - new overlay materials, no existing material changed:
#   /Game/Environment/Weather/Rain/Materials/M_WetOverlay + MI_WetOverlay
#       objects (UWeatherSnowOverlayComponent in wet mode puts it where the snow overlay would go): a translucent, forward-lit dark glossy
#       layer - wet darkening + gloss on upward faces, less on the sides with drip streaks, none on undersides; rain dimples on the tops
#       while it rains. Opacity = MPC Wetness x face mask x WetOpacity: exactly 0 on a dry level (and the component removes it then).
#   /Game/Environment/Weather/Rain/Materials/M_WetGroundOverlay + MI_WetGroundOverlay
#       ground slabs: a wet film (Wetness) + puddles (PuddleAmount over a two-scale noise field, flat faces only) with procedural ripple
#       rings while it rains (IsRaining x RainIntensity); interior camera zones stay dry (Zone<i>A / B, the snow ground overlay's rule).
# Both: translucent, Surface ForwardShading (per-pixel lit, the sun's shadows), world-space normal, the camera-ward WPO nudge of the snow
# overlays (no z-fighting with the base mesh). Masked overlays are invisible in UE 5.8 (drawn under the own mesh).
# Own assets: an existing one is KEPT (hand edits are safe); CODEX_WET_FORCE=1 rebuilds them.
# Run (holding the build lock, CODEX_AGENT=claude; -AllowCommandletRendering for the stats):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -AllowCommandletRendering
# Result: Saved/Logs/create_wet_surface_materials.txt
import importlib.util
import os

import unreal

PATH = "/Game/Environment/Weather/Rain/Materials"
OBJECT = "M_WetOverlay"
OBJECT_MI = "MI_WetOverlay"
GROUND = "M_WetGroundOverlay"
GROUND_MI = "MI_WetGroundOverlay"
MPC_PATH = "/Game/Environment/Weather/MPC/MPC_Environment"
NOISE = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"
ZONE_COUNT = 6

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
FORCE = os.environ.get("CODEX_WET_FORCE", "0") == "1"
_spec = importlib.util.spec_from_file_location("rain_hlsl", os.path.join(PROJECT_DIR, "Scripts", "Editor", "rain_hlsl.py"))
HL = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(HL)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_wet_surface_materials.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


OBJECT_MASK_HLSL = r"""
// x wet, y top share, z opacity, w drip. Exactly 0 at Wetness 0.
float up = N.z;
float top = saturate((up - TopMin) / max(1.0 - TopMin, 0.01));
float side = saturate(up * 2.0 + 1.0) * SideShare;
float drip = smoothstep(DripThreshold, DripThreshold + 0.15, NoiseSide.r) * (1.0 - top) * saturate(up + 1.0) * DripStrength;
float face = max(top, saturate(side + drip));
float w = saturate(Wetness) * face;
return float4(w, top, max(w * WetOpacity, DebugForce), drip);
"""

OBJECT_SURFACE_HLSL = r"""
float rough = lerp(SideRoughness, TopRoughness, Masks.y);
return float4(WetColor.rgb, rough);
"""

OBJECT_NORMAL_HLSL = r"""
float rain = saturate(IsRaining) * saturate(RainIntensity * 1.5);
float2 s = Slope * (RippleStrength * rain * Masks.y);
return normalize(N + float3(-s, 0.0));
"""

GROUND_MASK_HLSL = r"""
// x film, y puddle, z opacity, w unused. Exactly 0 at Wetness 0 and PuddleAmount 0.
float upMask = saturate((N.z - 0.5) * 4.0);
float dry = 1.0 - saturate(Zone);
float film = saturate(Wetness) * upMask * dry * FilmOpacity;
float puddle = saturate(Puddle) * dry;
return float4(film, puddle, max(max(film, puddle * PuddleOpacity), DebugForce), 0.0);
"""

GROUND_SURFACE_HLSL = r"""
float share = saturate(Masks.y * PuddleOpacity / max(Masks.z, 0.0001));
float3 colour = lerp(FilmColor.rgb, PuddleColor.rgb, share);
float rough = lerp(FilmRoughness, PuddleRoughness, share);
return float4(colour, rough);
"""

# The sky reflection direction (CameraVectorWS points from the surface to the eye).
REFLECT_HLSL = r"""
return reflect(-V, N);
"""

# A faked water reflection: the sky light cubemap (the presenter's real-time capture: follows the time of day and the weather) x Schlick
# Fresnel x a strength, mostly in the puddles (film: a weak sheen). Exactly 0 where the masks are 0.
GROUND_SHEEN_HLSL = r"""
float share = saturate(Masks.y * PuddleOpacity / max(Masks.z, 0.0001));
float nv = saturate(dot(N, V));
float f = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
float strength = lerp(FilmReflection, PuddleReflection, share) * saturate(Masks.z * 4.0);
return Sky * (f * strength);
"""

GROUND_NORMAL_HLSL = r"""
// The ripples fade out with the distance (no sparkle from sub-pixel rings at 40 m).
float rain = saturate(IsRaining) * saturate(RainIntensity * 1.5) * saturate(1.0 - (Distance - RippleFadeStart) / max(RippleFadeLength, 1.0));
float share = saturate(Masks.y * PuddleOpacity / max(Masks.z, 0.0001));
float2 s = Slope * (RippleStrength * rain * lerp(FilmRipple, 1.0, share));
return normalize(float3(-s, 1.0));
"""


class Builder:
    """Node helpers for one material."""

    def __init__(self, material, mpc):
        self.m = material
        self.mpc = mpc
        self.x0 = -2600

    def scalar(self, name, value, y, group):
        node = mel.create_material_expression(self.m, unreal.MaterialExpressionScalarParameter, self.x0 + 600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", group)
        return node

    def vector(self, name, value, y, group):
        node = mel.create_material_expression(self.m, unreal.MaterialExpressionVectorParameter, self.x0 + 600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", group)
        return node

    def mpc_param(self, name, y):
        node = mel.create_material_expression(self.m, unreal.MaterialExpressionCollectionParameter, self.x0 + 600, y)
        node.set_editor_property("collection", self.mpc)
        node.set_editor_property("parameter_name", name)
        return node

    def expr(self, cls, x, y):
        return mel.create_material_expression(self.m, cls, x, y)

    def custom(self, code, description, names, out_type, x, y):
        node = self.expr(unreal.MaterialExpressionCustom, x, y)
        node.set_editor_property("code", code.strip())
        node.set_editor_property("output_type", out_type)
        node.set_editor_property("description", description)
        items = []
        for name in names:
            item = unreal.CustomInput()
            item.set_editor_property("input_name", name)
            items.append(item)
        node.set_editor_property("inputs", items)
        return node

    def wire(self, node, names, sources, rgba=()):
        for name, source in zip(names, sources):
            four = source in rgba or isinstance(source, unreal.MaterialExpressionVectorParameter)
            if not mel.connect_material_expressions(source, "RGBA" if four else "", node, name):
                log("connect FAILED: %s -> %s" % (name, node.get_editor_property("description")))

    def texture(self, path, param, uv, y):
        asset = eal.load_asset(path)
        node = self.expr(unreal.MaterialExpressionTextureSampleParameter2D, self.x0 + 1400, y)
        node.set_editor_property("parameter_name", param)
        node.set_editor_property("texture", asset)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        node.set_editor_property("group", "Textures")
        node.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        mel.connect_material_expressions(uv, "", node, "UVs")
        return node

    def mask(self, source, channels, x, y):
        node = self.expr(unreal.MaterialExpressionComponentMask, x, y)
        for channel in ("r", "g", "b", "a"):
            node.set_editor_property(channel, channel in channels)
        mel.connect_material_expressions(source, "", node, "")
        return node

    def depth_nudge(self):
        """The overlays' camera-ward WPO nudge: 0.5 cm + 0.0005 x distance (no z-fighting with the base mesh)."""
        base = self.scalar("DepthNudgeCm", 0.5, 4000, "Depth")
        per_cm = self.scalar("DepthNudgePerCm", 0.0005, 4100, "Depth")
        camera = self.expr(unreal.MaterialExpressionCameraPositionWS, -1300, 4200)
        vertex = self.expr(unreal.MaterialExpressionWorldPosition, -1300, 4300)
        to_camera = self.expr(unreal.MaterialExpressionSubtract, -1100, 4250)
        mel.connect_material_expressions(camera, "", to_camera, "A")
        mel.connect_material_expressions(vertex, "", to_camera, "B")
        distance = self.expr(unreal.MaterialExpressionLength, -950, 4250)
        mel.connect_material_expressions(to_camera, "", distance, "")
        scaled = self.expr(unreal.MaterialExpressionMultiply, -800, 4200)
        mel.connect_material_expressions(distance, "", scaled, "A")
        mel.connect_material_expressions(per_cm, "", scaled, "B")
        amount = self.expr(unreal.MaterialExpressionAdd, -650, 4150)
        mel.connect_material_expressions(scaled, "", amount, "A")
        mel.connect_material_expressions(base, "", amount, "B")
        direction = self.expr(unreal.MaterialExpressionNormalize, -900, 3900)
        mel.connect_material_expressions(to_camera, "", direction, "")
        nudge = self.expr(unreal.MaterialExpressionMultiply, -500, 4000)
        mel.connect_material_expressions(direction, "", nudge, "A")
        mel.connect_material_expressions(amount, "", nudge, "B")
        mel.connect_material_property(nudge, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def new_material(name):
    full = PATH + "/" + name
    material = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", False)
    for usage in ("used_with_static_lighting", "used_with_nanite", "used_with_instanced_static_meshes", "used_with_spline_meshes"):
        try:
            material.set_editor_property(usage, True)
        except Exception as error:  # noqa: BLE001
            log("usage %s not set: %s" % (usage, error))
    log("building " + full)
    return material


def world_xy_div(b, world, tiling, y):
    xy = b.mask(world, ("r", "g"), b.x0 + 300, y)
    divide = b.expr(unreal.MaterialExpressionDivide, b.x0 + 1000, y)
    mel.connect_material_expressions(xy, "", divide, "A")
    mel.connect_material_expressions(tiling, "", divide, "B")
    return divide


def ripples(b, world, cell_cm, speed, ring, y):
    time = b.expr(unreal.MaterialExpressionTime, b.x0 + 600, y + 60)
    node = b.custom(HL.RIPPLES, "Rain ripples (procedural)", ["WP", "Time", "CellCm", "Speed", "Ring"], F2, -1500, y)
    b.wire(node, ["WP", "Time", "CellCm", "Speed", "Ring"], [world, time, cell_cm, speed, ring])
    return node


def finish(material, name, instance_name):
    mel.layout_material_expressions(material)
    errors = mel.recompile_material(material)
    log("%s compile errors: %s" % (name, list(errors) if errors else "none"))
    log("%s saved=%s" % (name, eal.save_asset(PATH + "/" + name, only_if_is_dirty=False)))
    try:
        s = mel.get_statistics(material)
        log("%s stats: vs %d ps %d samplers %d" % (name, s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers))
    except Exception as error:  # noqa: BLE001
        log("%s stats unavailable: %s" % (name, error))
    instance = tools.create_asset(instance_name, PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, material)
    log("%s saved=%s" % (instance_name, eal.save_asset(PATH + "/" + instance_name, only_if_is_dirty=False)))


def build_object(mpc):
    material = new_material(OBJECT)
    b = Builder(material, mpc)
    world = b.expr(unreal.MaterialExpressionWorldPosition, b.x0, 0)
    normal = b.expr(unreal.MaterialExpressionVertexNormalWS, b.x0, 100)
    wetness = b.mpc_param("Wetness", -1800)
    raining = b.mpc_param("IsRaining", -1700)
    rain = b.mpc_param("RainIntensity", -1600)
    top_min = b.scalar("TopMin", 0.35, -1500, "Wet")
    side_share = b.scalar("SideShare", 0.45, -1450, "Wet")
    wet_opacity = b.scalar("WetOpacity", 0.5, -1400, "Wet")
    drip_threshold = b.scalar("DripThreshold", 0.55, -1350, "Drips")
    drip_strength = b.scalar("DripStrength", 0.25, -1300, "Drips")
    debug = b.scalar("DebugForce", 0.0, -1250, "Debug")
    wet_color = b.vector("WetColor", unreal.LinearColor(0.004, 0.005, 0.006, 1.0), -1200, "Wet")
    top_rough = b.scalar("TopRoughness", 0.3, -1150, "Wet")
    side_rough = b.scalar("SideRoughness", 0.5, -1100, "Wet")
    ripple_strength = b.scalar("RippleStrength", 0.35, -1050, "Ripples")
    cell = b.scalar("RippleCellCm", 22.0, -1000, "Ripples")
    speed = b.scalar("RippleSpeed", 1.6, -950, "Ripples")
    ring = b.scalar("RippleRing", 0.05, -900, "Ripples")
    drip_tiling_x = b.scalar("DripTilingCm", 45.0, -850, "Drips")
    # Side-projected noise for vertical drip streaks: ((x + y) / 45 cm, z / 300 cm).
    side_uv = b.custom("return float2((W.x + W.y) / max(Tiling, 1.0), W.z / max(Tiling * 12.0, 1.0));", "Drip UV", ["W", "Tiling"], F2, b.x0 + 800, 700)
    b.wire(side_uv, ["W", "Tiling"], [world, drip_tiling_x])
    noise_side = b.texture(NOISE, "NoiseTexSide", side_uv, 700)
    names = ["Wetness", "N", "NoiseSide", "TopMin", "SideShare", "WetOpacity", "DripThreshold", "DripStrength", "DebugForce"]
    masks = b.custom(OBJECT_MASK_HLSL, "Wet masks", names, F4, -700, -600)
    b.wire(masks, names, [wetness, normal, noise_side, top_min, side_share, wet_opacity, drip_threshold, drip_strength, debug], rgba=(noise_side,))
    names = ["Masks", "WetColor", "TopRoughness", "SideRoughness"]
    surface = b.custom(OBJECT_SURFACE_HLSL, "Wet colour + roughness", names, F4, -700, 500)
    b.wire(surface, names, [masks, wet_color, top_rough, side_rough])
    slope = ripples(b, world, cell, speed, ring, 1200)
    names = ["Masks", "N", "Slope", "IsRaining", "RainIntensity", "RippleStrength"]
    normal_out = b.custom(OBJECT_NORMAL_HLSL, "Wet world normal", names, F3, -700, 1000)
    b.wire(normal_out, names, [masks, normal, slope, raining, rain, ripple_strength])
    mel.connect_material_property(b.mask(surface, ("r", "g", "b"), -350, 500), "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(b.mask(surface, ("a",), -350, 650), "", unreal.MaterialProperty.MP_ROUGHNESS)
    specular = b.scalar("Specular", 0.25, 750, "Wet")
    mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal_out, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(b.mask(masks, ("b",), -500, -300), "", unreal.MaterialProperty.MP_OPACITY)
    b.depth_nudge()
    finish(material, OBJECT, OBJECT_MI)


def build_ground(mpc):
    material = new_material(GROUND)
    b = Builder(material, mpc)
    world = b.expr(unreal.MaterialExpressionWorldPosition, b.x0, 0)
    normal = b.expr(unreal.MaterialExpressionVertexNormalWS, b.x0, 100)
    wetness = b.mpc_param("Wetness", -1900)
    puddles = b.mpc_param("PuddleAmount", -1850)
    raining = b.mpc_param("IsRaining", -1800)
    rain = b.mpc_param("RainIntensity", -1750)
    film_opacity = b.scalar("FilmOpacity", 0.4, -1700, "Film")
    film_color = b.vector("FilmColor", unreal.LinearColor(0.01, 0.011, 0.012, 1.0), -1650, "Film")
    film_rough = b.scalar("FilmRoughness", 0.35, -1600, "Film")
    film_ripple = b.scalar("FilmRipple", 0.25, -1550, "Ripples")
    puddle_opacity = b.scalar("PuddleOpacity", 0.8, -1500, "Puddles")
    puddle_color = b.vector("PuddleColor", unreal.LinearColor(0.02, 0.024, 0.028, 1.0), -1450, "Puddles")
    puddle_rough = b.scalar("PuddleRoughness", 0.06, -1400, "Puddles")
    puddle_bare = b.scalar("PuddleBare", 1.15, -1350, "Puddles")
    puddle_full = b.scalar("PuddleFull", 0.68, -1300, "Puddles")
    puddle_edge = b.scalar("PuddleEdge", 0.1, -1250, "Puddles")
    flat_min = b.scalar("FlatMin", 0.96, -1200, "Puddles")
    debug = b.scalar("DebugForce", 0.0, -1150, "Debug")
    ripple_strength = b.scalar("RippleStrength", 1.2, -1100, "Ripples")
    cell = b.scalar("RippleCellCm", 30.0, -1050, "Ripples")
    speed = b.scalar("RippleSpeed", 1.4, -1000, "Ripples")
    ring = b.scalar("RippleRing", 0.08, -950, "Ripples")
    macro_tiling = b.scalar("PuddleMacroTilingCm", 1400.0, -900, "Noise")
    mid_tiling = b.scalar("PuddleMidTilingCm", 420.0, -850, "Noise")
    puddle_reflection = b.scalar("PuddleReflection", 7.0, -800, "Puddles")
    film_reflection = b.scalar("FilmReflection", 1.2, -750, "Film")
    noise_macro = b.texture(NOISE, "NoiseTexMacro", world_xy_div(b, world, macro_tiling, -900), -900)
    noise_mid = b.texture(NOISE, "NoiseTexMid", world_xy_div(b, world, mid_tiling, -700), -700)
    field = b.custom(HL.PUDDLE_FIELD, "Puddle field", ["NoiseMacro", "NoiseMid"], F1, -1600, -800)
    b.wire(field, ["NoiseMacro", "NoiseMid"], [noise_macro, noise_mid], rgba=(noise_macro, noise_mid))
    up = b.mask(normal, ("b",), b.x0 + 300, 150)
    names = ["Field", "PuddleAmount", "Wetness", "Up", "PuddleBare", "PuddleFull", "PuddleEdge", "FlatMin"]
    puddle = b.custom(HL.PUDDLE_MASK, "Puddle mask", names, F1, -1300, -800)
    b.wire(puddle, names, [field, puddles, wetness, up, puddle_bare, puddle_full, puddle_edge, flat_min])
    # Interior zones (the component's dynamic instance fills them).
    zone_names, zone_nodes = [], []
    for index in range(ZONE_COUNT):
        for suffix in ("A", "B"):
            zone_names.append("Z%d%s" % (index, suffix))
            zone_nodes.append(b.vector("Zone%d%s" % (index, suffix), unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 2600 + len(zone_nodes) * 60, "Zones"))
    zone_soft = b.scalar("ZoneSoftCm", 40.0, 3400, "Zones")
    zones = b.custom(HL.ZONES, "Interior zones (dry)", ["WP"] + zone_names + ["SoftCm"], F1, -1300, 2600)
    b.wire(zones, ["WP"] + zone_names + ["SoftCm"], [world] + zone_nodes + [zone_soft], rgba=tuple(zone_nodes))
    names = ["Wetness", "Puddle", "N", "Zone", "FilmOpacity", "PuddleOpacity", "DebugForce"]
    masks = b.custom(GROUND_MASK_HLSL, "Wet ground masks", names, F4, -700, -600)
    b.wire(masks, names, [wetness, puddle, normal, zones, film_opacity, puddle_opacity, debug])
    names = ["Masks", "PuddleOpacity", "FilmColor", "PuddleColor", "FilmRoughness", "PuddleRoughness"]
    surface = b.custom(GROUND_SURFACE_HLSL, "Wet ground colour + roughness", names, F4, -700, 500)
    b.wire(surface, names, [masks, puddle_opacity, film_color, puddle_color, film_rough, puddle_rough])
    slope = ripples(b, world, cell, speed, ring, 1200)
    fade_start = b.scalar("RippleFadeStartCm", 1800.0, -700, "Ripples")
    fade_length = b.scalar("RippleFadeLengthCm", 2200.0, -650, "Ripples")
    distance = b.custom("return length(W - C);", "Camera distance", ["W", "C"], F1, -1000, 1100)
    b.wire(distance, ["W", "C"], [world, b.expr(unreal.MaterialExpressionCameraPositionWS, b.x0, 1150)])
    names = ["Masks", "PuddleOpacity", "Slope", "IsRaining", "RainIntensity", "RippleStrength", "FilmRipple", "Distance", "RippleFadeStart", "RippleFadeLength"]
    normal_out = b.custom(GROUND_NORMAL_HLSL, "Wet ground world normal", names, F3, -700, 1000)
    b.wire(normal_out, names, [masks, puddle_opacity, slope, raining, rain, ripple_strength, film_ripple, distance, fade_start, fade_length])
    # Faked water reflection (the sky light cubemap along the rippled reflection ray).
    camera = b.expr(unreal.MaterialExpressionCameraVectorWS, b.x0, 1500)
    reflect_dir = b.custom(REFLECT_HLSL, "Reflection ray", ["V", "N"], F3, -1000, 1500)
    b.wire(reflect_dir, ["V", "N"], [camera, normal_out])
    sky = b.expr(unreal.MaterialExpressionSkyLightEnvMapSample, -800, 1500)
    mel.connect_material_expressions(reflect_dir, "", sky, "Direction")
    mel.connect_material_expressions(b.mask(surface, ("a",), -1000, 1650), "", sky, "Roughness")
    names = ["Masks", "PuddleOpacity", "Sky", "N", "V", "PuddleReflection", "FilmReflection"]
    sheen = b.custom(GROUND_SHEEN_HLSL, "Water reflection (emissive)", names, F3, -550, 1500)
    b.wire(sheen, names, [masks, puddle_opacity, sky, normal_out, camera, puddle_reflection, film_reflection])
    mel.connect_material_property(sheen, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(b.mask(surface, ("r", "g", "b"), -350, 500), "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(b.mask(surface, ("a",), -350, 650), "", unreal.MaterialProperty.MP_ROUGHNESS)
    specular = b.scalar("Specular", 0.5, 750, "Puddles")
    mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(normal_out, "", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(b.mask(masks, ("b",), -500, -300), "", unreal.MaterialProperty.MP_OPACITY)
    b.depth_nudge()
    finish(material, GROUND, GROUND_MI)


def main():
    if not eal.does_asset_exist(MPC_PATH):
        log("MPC missing - nothing done")
        return
    mpc = eal.load_asset(MPC_PATH)
    scalars = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
    for needed in ("Wetness", "PuddleAmount", "IsRaining", "RainIntensity"):
        if needed not in scalars:
            log("MPC has no scalar %s (run create_environment_mpc.py) - nothing done" % needed)
            return
    for name, instance, build in ((OBJECT, OBJECT_MI, build_object), (GROUND, GROUND_MI, build_ground)):
        full, full_mi = PATH + "/" + name, PATH + "/" + instance
        if eal.does_asset_exist(full) and not FORCE:
            log("kept the existing %s (CODEX_WET_FORCE=1 rebuilds it)" % full)
            continue
        for path in (full_mi, full):
            if eal.does_asset_exist(path):
                eal.delete_asset(path)
                log("replaced (own asset): " + path)
        build(mpc)


try:
    main()
except Exception as error:  # noqa: BLE001
    log("ERROR " + repr(error))
    raise
finally:
    write_log()
