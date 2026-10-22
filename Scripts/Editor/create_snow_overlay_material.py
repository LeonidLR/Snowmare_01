# Creates the gradual snow cover overlay material (environment phase 4c, docs/systems/environment-weather.md §6 / §12):
#   /Game/Environment/Weather/Snow/Materials/M_SnowOverlay   translucent (Surface ForwardShading), default lit, world-space normal,
#                                                            a tiny camera-ward WPO nudge (no z-fighting with its own base mesh)
#   /Game/Environment/Weather/Snow/Materials/MI_SnowOverlay  its instance (the component's C++ default; the presenter currently
#                                                            assigns M_SnowOverlay itself - either works, tune the MI)
# UWeatherSnowOverlayComponent puts it as the UMeshComponent OVERLAY material on outdoor static meshes - no existing material is changed.
# Look: snow mask = saturate((world up - threshold(SnowAmount) + noise) x sharpness) x a dusting patch mask that fills in with SnowAmount,
# so at SnowAmount ~0.35 only flat tops are white and at 1 most upward faces are; a short creep band near the object top; Megascans Fresh
# Snow albedo / normal / roughness projected from above (world XY; steep faces fade to a flat snow colour); FrostAmount = a light bluish-white rime in noisy patches on all
# faces. Driven by MPC_Environment (SnowAmount, FrostAmount; written by AWeatherPresenterActor).
# MF_SnowyWeatherBlend is NOT used: it reads the old MPC_WeatherSettings and calls MF_ComputeLandscapeUV, which fails to load in 5.8.
# Only the two assets above are created / replaced (they are this script's own). CODEX_SNOW_DEBUG_FORCE=1 bakes DebugForce 1 (snow on
# every face) to check that the overlay draws at all.
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_snow_overlay_material.txt
import os
import unreal

PATH = "/Game/Environment/Weather/Snow/Materials"
NAME = "M_SnowOverlay"
INSTANCE = "MI_SnowOverlay"
MPC_PATH = "/Game/Environment/Weather/MPC/MPC_Environment"
SNOW_D = "/Game/Environment/Weather/Snow/Textures/T_Fresh_Snow_ugkieimdy_2K_D"
SNOW_N = "/Game/Environment/Weather/Snow/Textures/T_Fresh_Snow_ugkieimdy_2K_N"
SNOW_ORD = "/Game/Environment/Weather/Snow/Textures/T_FreshSnow_ugkieimdy_2K_ORDp"
NOISE = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_snow_overlay_material.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


# --- HLSL -------------------------------------------------------------------------------------------------------------
# Masks: x = snow, y = frost, z = opacity (max of both), w = 0.
MASK_HLSL = r"""
float amount = saturate(SnowAmount);
float frostAmount = saturate(FrostAmount);
float up = N.z;
// Threshold on the world-up normal: high (flat tops only) at a light cover, low (most upward faces) at a full one.
float shaped = pow(amount, ThresholdCurve);
float threshold = lerp(ThresholdLight, ThresholdFull, shaped);
float wobble = (NoiseA.r - 0.5) * NoiseStrength + (NoiseB.g - 0.5) * NoiseStrength * 0.5;
float top = saturate((up - threshold + wobble) * Sharpness);
// Creep: a band near the object's top grows down the sides with the cover (not on undersides); its lower edge is broken by a
// side-projected noise (world XY along the face, Z up) so it hangs in uneven tongues, not stripes.
float height = max(BoundsZ, 1.0);
float fromTop = saturate((height * 0.5 - RelZ) / height);
float band = amount * CreepShare;
float creep = saturate((band - fromTop + (NoiseSide.r - 0.5) * band * 0.9) / max(band * 0.25, 0.001)) * saturate(up * 4.0 + 0.6) * step(0.001, band);
creep *= saturate((amount - 0.25) * 4.0);
float snow = max(top, creep);
// Dusting -> cover: a light cover is broken into fine flecks and thin, it closes up and thickens with the amount.
float grow = saturate(amount * PatchGrowth);
// Top-projected noise on flat faces, side-projected on steep ones (a top projection smears into vertical streaks on a wall).
float steepBlend = saturate(up * 2.5 - 0.5);
float fine = lerp(NoiseSide.r * 0.6 + NoiseSide.b * 0.4, NoiseB.r * 0.6 + NoiseA.g * 0.4, steepBlend);
float coverage = saturate((fine + grow - 0.75) * 2.2);
snow *= coverage * (0.6 + 0.4 * grow) * step(0.005, amount);
// Frost: a thin bluish rime in soft patches on every face, a little more on top.
float frostNoise = NoiseA.a * 0.5 + NoiseB.b * 0.3 + NoiseSide.g * 0.2;
float frost = saturate((frostNoise - (1.0 - frostAmount * FrostCoverage)) * 2.5) * FrostStrength * saturate(up * 0.4 + 0.8);
// Debug: 1 = full snow on every face (checks the overlay draws at all).
snow = max(snow, DebugForce);
return float4(snow, frost, saturate(max(snow, frost)), 0.0);
"""

# Base colour (rgb) + roughness (a).
SURFACE_HLSL = r"""
float snow = Masks.x;
float opacity = max(Masks.z, 0.0001);
float snowShare = saturate(snow / opacity);
// The texture is projected from above: on steep faces (the creep band) it would smear into vertical streaks, so steep faces fade to
// the texture's average snow colour.
float3 flatSnow = SnowTint.rgb * SnowBrightness * 0.88;
float3 snowColour = lerp(flatSnow, SnowD.rgb * SnowTint.rgb * SnowBrightness, saturate(N.z * 2.5 - 0.5));
// Fresh, thick snow is brighter (less of the dark texture crevices) as the cover closes up.
snowColour = lerp(snowColour, SnowTint.rgb * SnowBrightness * 0.92, saturate(snow - 0.6) * 0.5);
float3 colour = lerp(FrostColour.rgb, snowColour, snowShare);
float roughness = lerp(FrostRoughness, saturate(ORD.g * SnowRoughnessScale), snowShare);
return float4(colour, roughness);
"""

# World-space normal: the geometric normal bent by the snow normal map (projected from above).
NORMAL_HLSL = r"""
float snowShare = saturate(Masks.x / max(Masks.z, 0.0001));
float2 bend = SnowN.xy * NormalStrength * snowShare * saturate(N.z * 2.0);
return normalize(N + float3(bend, 0.0));
"""

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

if not eal.does_asset_exist(MPC_PATH):
    log("MPC %s does not exist yet (created by Scripts/Editor/create_environment_mpc.py) - nothing done" % MPC_PATH)
    write_log()
    raise SystemExit(0)
mpc = eal.load_asset(MPC_PATH)
scalar_names = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
log("MPC scalars: %s" % ", ".join(scalar_names))
for needed in ("SnowAmount", "FrostAmount"):
    if needed not in scalar_names:
        log("MPC has no scalar %s - nothing done" % needed)
        write_log()
        raise SystemExit(0)

for name in (INSTANCE, NAME):
    full = PATH + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
        log("replaced (own asset): " + full)

material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
# TRANSLUCENT, not masked: UE sorts an overlay after its base mesh only in the translucency pass (MeshIdInPrimitive); in the opaque
# base pass the draw order follows the pipeline state, so a masked overlay is drawn under its own base mesh and never shows.
# Surface ForwardShading: per-pixel lighting with the directional light's shadows (snow in the shade of a wall stays darker).
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
material.set_editor_property("tangent_space_normal", False)
material.set_editor_property("two_sided", False)
for usage in ("used_with_static_lighting", "used_with_nanite", "used_with_instanced_static_meshes", "used_with_spline_meshes"):
    try:
        material.set_editor_property(usage, True)
    except Exception as error:  # noqa: BLE001 - a usage flag missing in this engine version is not fatal
        log("usage %s not set: %s" % (usage, error))

X0 = -2200


def scalar(name, value, y, group="Snow"):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, X0 + 600, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


def vector(name, value, y, group="Snow"):
    node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, X0 + 600, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


def mpc_scalar(name, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, X0 + 600, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", name)
    return node


def sampler_type_of(texture):
    compression = texture.get_editor_property("compression_settings")
    srgb = texture.get_editor_property("srgb")
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if compression == unreal.TextureCompressionSettings.TC_MASKS:
        return unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    if compression == unreal.TextureCompressionSettings.TC_GRAYSCALE:
        return unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE
    if compression == unreal.TextureCompressionSettings.TC_ALPHA:
        return unreal.MaterialSamplerType.SAMPLERTYPE_ALPHA
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def world_xy_uv(tiling_param, y):
    """World XY / tiling (cm per tile): a top-down projection."""
    world = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, X0, y)
    mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, X0 + 250, y)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", False)
    mask.set_editor_property("a", False)
    mel.connect_material_expressions(world, "", mask, "")
    divide = mel.create_material_expression(material, unreal.MaterialExpressionDivide, X0 + 900, y)
    mel.connect_material_expressions(mask, "", divide, "A")
    mel.connect_material_expressions(tiling_param, "", divide, "B")
    return divide


def texture(path, param_name, uv, y):
    asset = eal.load_asset(path)
    node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, X0 + 1200, y)
    node.set_editor_property("parameter_name", param_name)
    node.set_editor_property("texture", asset)
    node.set_editor_property("sampler_type", sampler_type_of(asset))
    node.set_editor_property("group", "Textures")
    mel.connect_material_expressions(uv, "", node, "UVs")
    log("texture %s: compression %s, srgb %s" % (path, asset.get_editor_property("compression_settings"), asset.get_editor_property("srgb")))
    return node


def custom(code, description, input_names, output_type, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("description", description)
    inputs = []
    for input_name in input_names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    node.set_editor_property("inputs", inputs)
    return node


# Inputs -------------------------------------------------------------------------------------------------------------
snow_amount = mpc_scalar("SnowAmount", -1400)
frost_amount = mpc_scalar("FrostAmount", -1300)
threshold_light = scalar("ThresholdLight", 0.97, -1200)
threshold_full = scalar("ThresholdFull", 0.22, -1100)
threshold_curve = scalar("ThresholdCurve", 1.25, -1000)
sharpness = scalar("Sharpness", 7.0, -900)
noise_strength = scalar("NoiseStrength", 0.22, -800)
creep_share = scalar("CreepShare", 0.12, -700)
patch_growth = scalar("PatchGrowth", 2.0, -600)
frost_coverage = scalar("FrostCoverage", 0.6, -500, "Frost")
frost_strength = scalar("FrostStrength", 0.35, -400, "Frost")
debug_force = scalar("DebugForce", float(os.environ.get("CODEX_SNOW_DEBUG_FORCE", "0")), -1500, "Debug")
noise_tiling_a = scalar("NoiseTilingA", 380.0, -300, "Noise")
noise_tiling_b = scalar("NoiseTilingB", 70.0, -200, "Noise")
snow_tiling = scalar("SnowTiling", 260.0, -100, "Textures")

normal_ws = mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, X0 + 600, 0)
world_pos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, X0, 100)
object_pos = mel.create_material_expression(material, unreal.MaterialExpressionObjectPositionWS, X0, 200)
relative = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, X0 + 300, 150)
mel.connect_material_expressions(world_pos, "", relative, "A")
mel.connect_material_expressions(object_pos, "", relative, "B")
rel_z = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, X0 + 600, 150)
rel_z.set_editor_property("r", False)
rel_z.set_editor_property("g", False)
rel_z.set_editor_property("b", True)
mel.connect_material_expressions(relative, "", rel_z, "")
bounds = mel.create_material_expression(material, unreal.MaterialExpressionObjectBounds, X0 + 300, 300)
bounds_z = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, X0 + 600, 300)
bounds_z.set_editor_property("r", False)
bounds_z.set_editor_property("g", False)
bounds_z.set_editor_property("b", True)
mel.connect_material_expressions(bounds, "", bounds_z, "")

noise_a = texture(NOISE, "NoiseTexA", world_xy_uv(noise_tiling_a, 400), 400)
noise_b = texture(NOISE, "NoiseTexB", world_xy_uv(noise_tiling_b, 600), 600)


def world_side_uv(tiling_param, y):
    """(world X + Y, world Z) / tiling: a projection that varies along a vertical face and downward."""
    world = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, X0, y)
    split = custom("return float2(W.x + W.y, W.z);", "Side UV", ["W"], unreal.CustomMaterialOutputType.CMOT_FLOAT2, X0 + 250, y)
    mel.connect_material_expressions(world, "", split, "W")
    divide = mel.create_material_expression(material, unreal.MaterialExpressionDivide, X0 + 900, y)
    mel.connect_material_expressions(split, "", divide, "A")
    mel.connect_material_expressions(tiling_param, "", divide, "B")
    return divide


noise_side = texture(NOISE, "NoiseTexSide", world_side_uv(noise_tiling_b, 700), 700)
snow_uv = world_xy_uv(snow_tiling, 800)
snow_d = texture(SNOW_D, "SnowAlbedo", snow_uv, 800)
snow_n = texture(SNOW_N, "SnowNormal", snow_uv, 1000)
snow_ord = texture(SNOW_ORD, "SnowORD", snow_uv, 1200)

# Masks --------------------------------------------------------------------------------------------------------------
mask_inputs = ["SnowAmount", "FrostAmount", "N", "NoiseA", "NoiseB", "NoiseSide", "RelZ", "BoundsZ", "ThresholdLight", "ThresholdFull", "ThresholdCurve",
               "Sharpness", "NoiseStrength", "CreepShare", "PatchGrowth", "FrostCoverage", "FrostStrength", "DebugForce"]
masks = custom(MASK_HLSL, "Snow + frost masks", mask_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -700, -600)
for input_name, node in zip(mask_inputs, [snow_amount, frost_amount, normal_ws, noise_a, noise_b, noise_side, rel_z, bounds_z, threshold_light, threshold_full,
                                          threshold_curve, sharpness, noise_strength, creep_share, patch_growth, frost_coverage, frost_strength,
                                          debug_force]):
    # The noise textures pack four noises: all four channels (RGBA) go in.
    mel.connect_material_expressions(node, "RGBA" if node in (noise_a, noise_b, noise_side) else "", masks, input_name)

# Surface ------------------------------------------------------------------------------------------------------------
snow_tint = vector("SnowTint", unreal.LinearColor(0.93, 0.96, 1.0, 1.0), 1400)
snow_brightness = scalar("SnowBrightness", 1.05, 1500)
frost_colour = vector("FrostColour", unreal.LinearColor(0.72, 0.82, 0.95, 1.0), 1600, "Frost")
frost_roughness = scalar("FrostRoughness", 0.35, 1700, "Frost")
snow_roughness_scale = scalar("SnowRoughnessScale", 1.0, 1800)
normal_strength = scalar("NormalStrength", 0.6, 1900)

surface_inputs = ["Masks", "SnowD", "ORD", "SnowTint", "SnowBrightness", "FrostColour", "FrostRoughness", "SnowRoughnessScale", "N"]
surface = custom(SURFACE_HLSL, "Snow / frost colour + roughness", surface_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -700, 600)
for input_name, node in zip(surface_inputs, [masks, snow_d, snow_ord, snow_tint, snow_brightness, frost_colour, frost_roughness, snow_roughness_scale,
                                             normal_ws]):
    mel.connect_material_expressions(node, "", surface, input_name)

normal_inputs = ["Masks", "SnowN", "N", "NormalStrength"]
normal = custom(NORMAL_HLSL, "Snow world normal", normal_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -700, 1000)
for input_name, node in zip(normal_inputs, [masks, snow_n, normal_ws, normal_strength]):
    mel.connect_material_expressions(node, "", normal, input_name)

# Outputs ------------------------------------------------------------------------------------------------------------
colour = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -350, 500)
colour.set_editor_property("r", True)
colour.set_editor_property("g", True)
colour.set_editor_property("b", True)
mel.connect_material_expressions(surface, "", colour, "")
mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)

roughness = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -350, 650)
roughness.set_editor_property("a", True)
mel.connect_material_expressions(surface, "", roughness, "")
mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

specular = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -350, 750)
specular.set_editor_property("r", 0.5)
mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)

mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)

opacity = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -500, -300)
opacity.set_editor_property("b", True)
mel.connect_material_expressions(masks, "", opacity, "")
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

# The overlay re-draws the same triangles: without a nudge toward the camera the translucent pass z-fights with the opaque depth
# (ring moire). WPO = camera vector x (base + distance x per-cm), i.e. ~0.3 cm close up, ~1.5 cm at 30 m - invisible, no silhouette growth.
offset_base = scalar("DepthNudgeCm", 0.3, 2100, "Depth")
offset_per_cm = scalar("DepthNudgePerCm", 0.0004, 2200, "Depth")
camera_position = mel.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1300, 2300)
vertex_position = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1300, 2400)
to_camera = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -1100, 2350)
mel.connect_material_expressions(camera_position, "", to_camera, "A")
mel.connect_material_expressions(vertex_position, "", to_camera, "B")
distance = mel.create_material_expression(material, unreal.MaterialExpressionLength, -950, 2350)
mel.connect_material_expressions(to_camera, "", distance, "")
scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -800, 2300)
mel.connect_material_expressions(distance, "", scaled, "A")
mel.connect_material_expressions(offset_per_cm, "", scaled, "B")
amount = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -650, 2250)
mel.connect_material_expressions(scaled, "", amount, "A")
mel.connect_material_expressions(offset_base, "", amount, "B")
# The direction to the camera from the vertex (CameraVector is a pixel-shader input, not usable for WPO).
camera_vector = mel.create_material_expression(material, unreal.MaterialExpressionNormalize, -900, 2000)
mel.connect_material_expressions(to_camera, "", camera_vector, "")
nudge = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -500, 2100)
mel.connect_material_expressions(camera_vector, "", nudge, "A")
mel.connect_material_expressions(amount, "", nudge, "B")
mel.connect_material_property(nudge, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

mel.layout_material_expressions(material)
compile_errors = mel.recompile_material(material)
log("compile errors: %s" % (list(compile_errors) if compile_errors else "none"))
saved = eal.save_asset(PATH + "/" + NAME, only_if_is_dirty=False)
log("created %s/%s saved=%s (DebugForce %.1f)" % (PATH, NAME, saved, debug_force.get_editor_property("default_value")))
try:
    stats = mel.get_statistics(material)
    log("stats: vs %d ps %d samplers %d" % (stats.num_vertex_shader_instructions, stats.num_pixel_shader_instructions, stats.num_samplers))
except Exception as error:  # noqa: BLE001
    log("stats unavailable: %s" % error)

instance = tools.create_asset(INSTANCE, PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(instance, material)
saved = eal.save_asset(PATH + "/" + INSTANCE, only_if_is_dirty=False)
log("created %s/%s saved=%s" % (PATH, INSTANCE, saved))
write_log()
