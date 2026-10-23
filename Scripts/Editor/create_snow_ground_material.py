# Creates the snow GROUND overlay material (environment phase 4d, docs/systems/environment-weather.md section 12.d):
#   /Game/Environment/Weather/Snow/Materials/M_SnowGroundOverlay   translucent (Surface ForwardShading), default lit, world-space normal,
#                                                                  the same tiny camera-ward WPO nudge as M_SnowOverlay (no z-fighting)
#   /Game/Environment/Weather/Snow/Materials/MI_SnowGroundOverlay  its instance (UWeatherSnowOverlayComponent's default ground material;
#                                                                  the component makes one dynamic instance of it per world for the
#                                                                  interior-zone rectangles)
# UWeatherSnowOverlayComponent puts it as the UMeshComponent OVERLAY material on the ground slabs (SM_Template_Map_Floor, the big flat
# cube floors) that M_SnowOverlay skips - no existing material or asset is changed.
# Look: a Fresh Snow layer projected from above (world XY) over the grey ground on upward faces only. Coverage = MPC_Environment.SnowAmount
# against a two-scale noise field (T_TilingNoisePacked: wind-swept macro patches + a finer breakup), so a light cover is a patchy, thin
# dusting and a full cover closes up but keeps thinner wind-swept spots - never a flat white sheet: the albedo's dark grain lifted
# (gamma 0.6) and flattened with distance (no far moire) so the snow reads brighter than the grey floor, the snow normal
# map, a subtle specular sparkle that fades out with distance. FreshSnow (MPC; IsSnowing when the MPC has no FreshSnow yet) = brighter and
# rougher; FrostAmount = a smoother crust with more glints. Interior zones (up to 6 rectangles from the component, closed / shelter
# camera zones) stay bare.
# Interactive snow (docs/design/INTERACTIVE_SNOW.md contract): when /Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction AND the
# MPC vector SnowTrackWindow exist, the material samples the RT at UV = (WorldXY - origin) x invSize (3 samples, one shared sampler:
# height + finite-difference normal), only where SnowTrackWindow.w > 0.5 and inside the window (2 m edge fade): compacted (R) = darker,
# smoother, a slight occlusion and a thin dusting shows the ground; berm (G) = brighter, rougher; height = G(1-R) MaxBermCm - R MaxDepthCm
# drives a cheap bump-offset parallax on the snow UVs. Without them the track part is left out (log: "track read pending") - re-run the
# script once the RT / MPC parameter exist.
# CODEX_GROUND_TRACK_DRYRUN=1 compiles the track part against stand-ins (a vector parameter for the window, an engine texture for the RT)
# to check the HLSL, then rebuilds the material normally. CODEX_SNOW_DEBUG_FORCE=1 bakes DebugForce 1 (full snow on every up face).
# Only the two assets above are created / replaced (they are this script's own).
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_snow_ground_material.txt
import os
import unreal

PATH = "/Game/Environment/Weather/Snow/Materials"
NAME = "M_SnowGroundOverlay"
INSTANCE = "MI_SnowGroundOverlay"
MPC_PATH = "/Game/Environment/Weather/MPC/MPC_Environment"
RT_PATH = "/Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction"
SNOW_D = "/Game/Environment/Weather/Snow/Textures/T_Fresh_Snow_ugkieimdy_2K_D"
SNOW_N = "/Game/Environment/Weather/Snow/Textures/T_Fresh_Snow_ugkieimdy_2K_N"
SNOW_ORD = "/Game/Environment/Weather/Snow/Textures/T_FreshSnow_ugkieimdy_2K_ORDp"
NOISE = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"
DRYRUN_RT = "/Engine/EngineResources/Black"
ZONE_COUNT = 6

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_snow_ground_material.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


# --- HLSL -------------------------------------------------------------------------------------------------------------
# Interior zones: 1 inside any of the rectangles (A = centre XY, half extent XY; B = cos yaw, sin yaw, top Z, bottom Z), soft edge.
ZONES_HLSL = r"""
float4 A[6] = { Z0A, Z1A, Z2A, Z3A, Z4A, Z5A };
float4 B[6] = { Z0B, Z1B, Z2B, Z3B, Z4B, Z5B };
float inside = 0.0;
[unroll] for (int i = 0; i < 6; ++i)
{
    float4 a = A[i];
    float4 b = B[i];
    if (a.z > 0.0)
    {
        float2 d = WP.xy - a.xy;
        float2 l = float2(d.x * b.x + d.y * b.y, -d.x * b.y + d.y * b.x);
        float2 e = a.zw - abs(l);
        float m = saturate(min(e.x, e.y) / max(SoftCm, 1.0) + 0.5);
        m *= step(WP.z, b.z) * step(b.w - 100.0, WP.z);
        inside = max(inside, m);
    }
}
return inside;
"""

# Track read: x = height cm, y = compaction R, z = berm G, w = window fade (all x fade); Slope = (dh/dx, dh/dy) x fade.
TRACK_HLSL = r"""
Slope = float2(0.0, 0.0);
if (Window.w < 0.5 || Window.z <= 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
float2 uv = (WP.xy - Window.xy) * Window.z;
float sizeCm = 1.0 / Window.z;
float2 edgeCm = min(uv, 1.0 - uv) * sizeCm;
float fade = saturate(min(edgeCm.x, edgeCm.y) / max(EdgeFadeCm, 1.0));
if (fade <= 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
float texel = 1.0 / max(Resolution, 1.0);
float2 s0 = Tex.SampleLevel(TexSampler, uv, 0).rg;
float2 sx = Tex.SampleLevel(TexSampler, uv + float2(texel, 0.0), 0).rg;
float2 sy = Tex.SampleLevel(TexSampler, uv + float2(0.0, texel), 0).rg;
float3 r = saturate(float3(s0.x, sx.x, sy.x));
float3 g = saturate(float3(s0.y, sx.y, sy.y));
float3 h = g * (1.0 - r) * MaxBermCm - r * MaxDepthCm;
float texelCm = sizeCm * texel;
Slope = float2(h.y - h.x, h.z - h.x) / max(texelCm, 0.01) * fade;
return float4(h.x * fade, r.x * fade, g.x * (1.0 - r.x) * fade, fade);
"""

# Snow UVs with a bump-offset parallax (world XY / tiling; a raised berm shifts toward the camera, a print away from it).
PARALLAX_HLSL = r"""
float2 shift = CamVec.xy / max(CamVec.z, 0.35) * Track.x * ParallaxScale;
return (WP.xy + shift) / Tiling;
"""

# Masks: x = snow share of the pixel (for the colour / normal), y = opacity, z = macro field (0..1), w = fresh (0..1).
MASK_HLSL = r"""
float amount = saturate(SnowAmount);
float fresh = saturate(Fresh);
// Upward faces only (the slab sides stay bare).
float up = saturate((N.z - UpMin) / max(UpSoft, 0.001));
// Two noise scales (wind-swept macro patches, a finer breakup), each channel normalised by the measured statistics of
// T_TilingNoisePacked (R 0.31 +- 0.175, G 0.50 +- 0.19, B 0.42 +- 0.21), so the field is ~ N(0.5, 0.2) and the cover fraction follows
// SnowAmount (0.35 ~ 40 %, 0.7 ~ 90 %, 1 ~ 99 %).
float3 zA = (NoiseA.rgb - float3(0.31, 0.50, 0.42)) / float3(0.175, 0.19, 0.21);
float3 zB = (NoiseB.rgb - float3(0.31, 0.50, 0.42)) / float3(0.175, 0.19, 0.21);
float macroZ = (zA.r * 0.65 + zA.g * 0.35) / 0.738;
float midZ = (zB.b * 0.6 + zB.r * 0.4) / 0.721;
float macro = saturate(0.5 + 0.2 * macroZ);
float field = 0.5 + 0.2 * (macroZ * MacroShare + midZ * (1.0 - MacroShare)) / max(length(float2(MacroShare, 1.0 - MacroShare)), 0.001);
// The snow front moves across the field with the cover: none at 0, a patchy dusting at 0.35, closed at 1.
float t = lerp(ThresholdBare, ThresholdFull, pow(amount, CoverCurve));
float cover = smoothstep(t - EdgeSoft, t + EdgeSoft, field) * step(0.005, amount);
// Thickness: thin at the patch edge and at a light cover, thick inside a patch at a full cover.
float inner = saturate((field - t) / 0.25);
float thick = lerp(ThinOpacity, FullOpacity, saturate(amount * 1.15)) * lerp(0.6, 1.0, inner);
// Wind-swept thin spots stay even at a full cover (never a white sheet).
thick *= 1.0 - BareSpots * saturate((0.3 - macro) / 0.3);
// Fine grain (37 cm noise): a light cover is a speckled dusting, not a painted blob; it evens out as the cover thickens.
thick *= lerp(1.0, saturate(0.2 + 1.6 * (NoiseFine.g - 0.25)), GrainStrength * (1.0 - saturate(amount * 1.25)));
float opacity = cover * thick;
// A heavier cover leaves a thin film over the bare spots too (no black holes at 0.7 - 1).
float film = FilmOpacity * saturate((amount - 0.4) / 0.6) * (0.6 + 0.4 * saturate(0.5 + 0.2 * midZ));
opacity = max(opacity, film * step(0.005, amount));
// A print through a thin dusting shows the ground.
opacity *= 1.0 - TrackR * saturate(0.85 - amount) * 0.8;
opacity *= up * (1.0 - Interior);
opacity = max(opacity, DebugForce * up);
return float4(saturate(max(cover, DebugForce)), saturate(opacity), macro, fresh);
"""

# Base colour (rgb) + roughness (a); Spec = specular with the sparkle.
SURFACE_HLSL = r"""
float macro = Masks.z;
float fresh = Masks.w;
float frost = saturate(FrostAmount);
// The Megascans albedo has dark grain: lifted (gamma) and flattened with distance (no far moire), so the snow reads brighter than the grey floor.
float dist = length(CameraPos - WP);
float detail = 1.0 - saturate((dist - DetailFadeStartCm) / max(DetailFadeLengthCm, 1.0)) * 0.75;
float3 lifted = pow(saturate(SnowD.rgb), SnowAlbedoGamma);
float3 snow = lerp(float3(0.86, 0.87, 0.88), lifted, detail) * SnowTint.rgb * SnowBrightness;
// Macro shading: older, wind-packed patches a bit darker, drifts a bit brighter.
snow *= lerp(1.0 - MacroShade, 1.0 + MacroShade * 0.4, macro);
// Fresh snow: brighter, flatter (less of the texture's crevices).
snow = lerp(snow, SnowTint.rgb * SnowBrightness * 1.06, fresh * 0.4);
float rough = saturate(ORD.g * RoughnessScale);
rough = lerp(rough, saturate(rough + 0.12), fresh);
// Frost: a smoother crust.
rough = lerp(rough, rough * 0.72, frost * 0.6 * (1.0 - fresh));
// Interactive snow: compacted = darker, smoother, a slight occlusion in the print; berm = brighter, rougher.
float r = Track.y;
float g = Track.z;
snow *= lerp(1.0, CompactDarken, r) * (1.0 - CompactOcclusion * saturate(-Track.x / 4.0));
rough = lerp(rough, saturate(rough - 0.25), r);
snow *= lerp(1.0, BermBrighten, g);
rough = saturate(rough + 0.08 * g);
// Sparkle: rare fine-noise points get a sharp highlight (only where the sun reflects), fading out with the distance (no shimmer far away).
float sparkleFade = 1.0 - saturate((dist - SparkleNearCm) / max(SparkleFarCm - SparkleNearCm, 1.0));
float density = SparkleDensity * (1.0 + frost) * (1.0 - fresh * 0.5);
float sparkle = pow(saturate((NoiseFine.a - (1.0 - density)) / max(density, 0.001)), 3.0) * sparkleFade * (1.0 - r);
rough = lerp(rough, 0.3, sparkle);
Spec = 0.5 + sparkle * SparkleStrength;
return float4(snow, rough);
"""

# World-space normal: the geometric normal bent by the snow normal map (projected from above) and the track slope.
NORMAL_HLSL = r"""
float share = Masks.x;
float2 bend = SnowN.xy * NormalStrength * share * saturate(N.z * 2.0) - Slope * TrackNormalStrength;
return normalize(N + float3(bend, 0.0));
"""

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

if not eal.does_asset_exist(MPC_PATH):
    log("MPC %s does not exist yet - nothing done" % MPC_PATH)
    write_log()
    raise SystemExit(0)
mpc = eal.load_asset(MPC_PATH)
scalar_names = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
vector_names = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")]
log("MPC scalars: %s; vectors: %s" % (", ".join(scalar_names), ", ".join(vector_names)))
for needed in ("SnowAmount", "FrostAmount"):
    if needed not in scalar_names:
        log("MPC has no scalar %s - nothing done" % needed)
        write_log()
        raise SystemExit(0)
has_fresh = "FreshSnow" in scalar_names
has_window = "SnowTrackWindow" in vector_names
has_rt = eal.does_asset_exist(RT_PATH)
dryrun = os.environ.get("CODEX_GROUND_TRACK_DRYRUN", "0") == "1"
log("FreshSnow in MPC: %s (else IsSnowing); SnowTrackWindow in MPC: %s; RT %s exists: %s; track dry run: %s"
    % (has_fresh, has_window, RT_PATH, has_rt, dryrun))


def build(with_track, track_dry):
    """Builds M_SnowGroundOverlay (replacing the own asset); returns (material, compile errors)."""
    full = PATH + "/" + NAME
    if eal.does_asset_exist(full):
        # Rebuilt in place (own asset): the instance and any reference stay valid.
        material = eal.load_asset(full)
        mel.delete_all_material_expressions(material)
        log("rebuilt in place (own asset): " + full)
    else:
        material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    # TRANSLUCENT as M_SnowOverlay: a masked overlay is drawn under its own base mesh in the opaque pass and never shows (UE 5.8).
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", False)
    for usage in ("used_with_static_lighting", "used_with_nanite", "used_with_instanced_static_meshes"):
        try:
            material.set_editor_property(usage, True)
        except Exception as error:  # noqa: BLE001 - a usage flag missing in this engine version is not fatal
            log("usage %s not set: %s" % (usage, error))

    x0 = -2600

    def scalar(name, value, y, group="Snow"):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x0 + 600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", group)
        return node

    def vector(name, value, y, group="Snow"):
        node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x0 + 600, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", group)
        return node

    def mpc_param(name, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, x0 + 600, y)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        return node

    def constant(value, y):
        node = mel.create_material_expression(material, unreal.MaterialExpressionConstant, x0 + 600, y)
        node.set_editor_property("r", value)
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

    def custom(code, description, input_names, output_type, x, y, extra_outputs=()):
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
        outputs = []
        for output_name, extra_type in extra_outputs:
            item = unreal.CustomOutput()
            item.set_editor_property("output_name", output_name)
            item.set_editor_property("output_type", extra_type)
            outputs.append(item)
        if outputs:
            node.set_editor_property("additional_outputs", outputs)
        return node

    def wire(node, names, sources, rgba=()):
        for input_name, source in zip(names, sources):
            # Vector parameters' default pin is RGB: the custom code reads all four components.
            four = source in rgba or isinstance(source, unreal.MaterialExpressionVectorParameter)
            ok = mel.connect_material_expressions(source, "RGBA" if four else "", node, input_name)
            if not ok:
                log("connect FAILED: %s -> %s" % (input_name, node.get_editor_property("description")))

    def texture(path, param_name, uv, y, shared=True):
        asset = eal.load_asset(path)
        node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, x0 + 1400, y)
        node.set_editor_property("parameter_name", param_name)
        node.set_editor_property("texture", asset)
        node.set_editor_property("sampler_type", sampler_type_of(asset))
        node.set_editor_property("group", "Textures")
        if shared:
            # One shared wrap sampler for all tiling textures (sampler budget).
            node.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        mel.connect_material_expressions(uv, "", node, "UVs")
        return node

    world_pos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, x0, 0)
    normal_ws = mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, x0, 100)
    camera_vec = mel.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, x0, 200)
    camera_pos = mel.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, x0, 300)

    def world_xy(tiling, y):
        mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x0 + 300, y)
        for channel, on in (("r", True), ("g", True), ("b", False), ("a", False)):
            mask.set_editor_property(channel, on)
        mel.connect_material_expressions(world_pos, "", mask, "")
        divide = mel.create_material_expression(material, unreal.MaterialExpressionDivide, x0 + 1000, y)
        mel.connect_material_expressions(mask, "", divide, "A")
        mel.connect_material_expressions(tiling, "", divide, "B")
        return divide

    # Inputs ---------------------------------------------------------------------------------------------------------
    snow_amount = mpc_param("SnowAmount", -1800)
    frost_amount = mpc_param("FrostAmount", -1700)
    fresh = mpc_param("FreshSnow" if has_fresh else "IsSnowing", -1600)
    debug_force = scalar("DebugForce", float(os.environ.get("CODEX_SNOW_DEBUG_FORCE", "0")), -1900, "Debug")
    threshold_bare = scalar("ThresholdBare", 1.05, -1500, "Cover")
    threshold_full = scalar("ThresholdFull", 0.0, -1450, "Cover")
    cover_curve = scalar("CoverCurve", 0.7, -1400, "Cover")
    edge_soft = scalar("EdgeSoft", 0.11, -1350, "Cover")
    thin_opacity = scalar("ThinOpacity", 0.4, -1300, "Cover")
    full_opacity = scalar("FullOpacity", 0.96, -1250, "Cover")
    bare_spots = scalar("BareSpots", 0.3, -1200, "Cover")
    macro_share = scalar("MacroShare", 0.55, -1150, "Cover")
    up_min = scalar("UpMin", 0.55, -1100, "Cover")
    up_soft = scalar("UpSoft", 0.3, -1050, "Cover")
    macro_tiling = scalar("MacroTiling", 2400.0, -1000, "Noise")
    mid_tiling = scalar("MidTiling", 520.0, -950, "Noise")
    fine_tiling = scalar("SparkleTiling", 37.0, -900, "Noise")
    snow_tiling = scalar("SnowTiling", 300.0, -850, "Textures")

    noise_a = texture(NOISE, "NoiseTexMacro", world_xy(macro_tiling, -1000), -1000)
    noise_b = texture(NOISE, "NoiseTexMid", world_xy(mid_tiling, -800), -800)
    noise_fine = texture(NOISE, "NoiseTexSparkle", world_xy(fine_tiling, -600), -600)

    # Interior zones (the component's dynamic instance fills them) ----------------------------------------------------------
    zone_names = []
    zone_nodes = []
    for index in range(ZONE_COUNT):
        for suffix in ("A", "B"):
            name = "Z%d%s" % (index, suffix)
            zone_names.append(name)
            zone_nodes.append(vector("Zone%d%s" % (index, suffix), unreal.LinearColor(0.0, 0.0, 0.0, 0.0), 2600 + len(zone_nodes) * 60, "Zones"))
    zone_soft = scalar("ZoneSoftCm", 40.0, 3400, "Zones")
    zones = custom(ZONES_HLSL, "Interior zones", ["WP"] + zone_names + ["SoftCm"], unreal.CustomMaterialOutputType.CMOT_FLOAT1, -1300, 2600)
    wire(zones, ["WP"] + zone_names + ["SoftCm"], [world_pos] + zone_nodes + [zone_soft])

    # Track read (interactive snow) ---------------------------------------------------------------------------------------
    if with_track:
        if track_dry:
            window = vector("SnowTrackWindowDryRun", unreal.LinearColor(0.0, 0.0, 1.0 / 6400.0, 1.0), -2400, "Tracks")
            rt_asset = eal.load_asset(DRYRUN_RT)
        else:
            window = mpc_param("SnowTrackWindow", -2400)
            rt_asset = eal.load_asset(RT_PATH)
        rt = mel.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, x0 + 600, -2300)
        rt.set_editor_property("parameter_name", "TrackRT")
        rt.set_editor_property("texture", rt_asset)
        try:
            rt.set_editor_property("sampler_type", sampler_type_of(rt_asset))
        except Exception:  # noqa: BLE001 - a render target has no compression settings
            rt.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rt.set_editor_property("group", "Tracks")
        track_inputs = ["Tex", "WP", "Window", "Resolution", "MaxDepthCm", "MaxBermCm", "EdgeFadeCm"]
        track = custom(TRACK_HLSL, "Track read (RT_SnowInteraction)", track_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -1900, -2300,
                       extra_outputs=[("Slope", unreal.CustomMaterialOutputType.CMOT_FLOAT2)])
        wire(track, track_inputs, [rt, world_pos, window, scalar("TrackResolution", 2048.0, -2200, "Tracks"),
                                   scalar("MaxDepthCm", 8.0, -2150, "Tracks"), scalar("MaxBermCm", 3.0, -2100, "Tracks"),
                                   scalar("EdgeFadeCm", 200.0, -2050, "Tracks")])
        track_out = track
        slope_out = track
        slope_pin = "Slope"
        parallax_inputs = ["WP", "CamVec", "Track", "ParallaxScale", "Tiling"]
        snow_uv = custom(PARALLAX_HLSL, "Snow UV + bump offset", parallax_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1900, -1900)
        wire(snow_uv, parallax_inputs, [world_pos, camera_vec, track, scalar("ParallaxScale", 0.6, -1950, "Tracks"), snow_tiling])
    else:
        track_out = mel.create_material_expression(material, unreal.MaterialExpressionConstant4Vector, x0 + 600, -2300)
        slope_out = mel.create_material_expression(material, unreal.MaterialExpressionConstant2Vector, x0 + 600, -2200)
        slope_pin = ""
        snow_uv = world_xy(snow_tiling, -400)
    track_r = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1500, -2000)
    for channel, on in (("r", False), ("g", True), ("b", False), ("a", False)):
        track_r.set_editor_property(channel, on)
    mel.connect_material_expressions(track_out, "", track_r, "")

    snow_d = texture(SNOW_D, "SnowAlbedo", snow_uv, -200)
    snow_n = texture(SNOW_N, "SnowNormal", snow_uv, 0)
    snow_ord = texture(SNOW_ORD, "SnowORD", snow_uv, 200)

    # Masks ------------------------------------------------------------------------------------------------------------
    mask_inputs = ["SnowAmount", "Fresh", "N", "NoiseA", "NoiseB", "NoiseFine", "Interior", "TrackR", "UpMin", "UpSoft", "MacroShare", "ThresholdBare",
                   "ThresholdFull", "CoverCurve", "EdgeSoft", "ThinOpacity", "FullOpacity", "BareSpots", "GrainStrength", "FilmOpacity", "DebugForce"]
    masks = custom(MASK_HLSL, "Ground snow masks", mask_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -900, -900)
    wire(masks, mask_inputs, [snow_amount, fresh, normal_ws, noise_a, noise_b, noise_fine, zones, track_r, up_min, up_soft, macro_share, threshold_bare,
                              threshold_full, cover_curve, edge_soft, thin_opacity, full_opacity, bare_spots, scalar("GrainStrength", 0.7, -1020, "Cover"),
                              scalar("FilmOpacity", 0.4, -1010, "Cover"), debug_force], rgba=(noise_a, noise_b, noise_fine))

    # Surface ----------------------------------------------------------------------------------------------------------
    surface_inputs = ["Masks", "SnowD", "ORD", "NoiseFine", "Track", "FrostAmount", "WP", "CameraPos", "SnowTint", "SnowBrightness", "MacroShade",
                      "SnowAlbedoGamma", "DetailFadeStartCm", "DetailFadeLengthCm", "RoughnessScale", "CompactDarken", "CompactOcclusion", "BermBrighten", "SparkleDensity", "SparkleStrength", "SparkleNearCm",
                      "SparkleFarCm"]
    surface = custom(SURFACE_HLSL, "Ground snow colour + roughness", surface_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT4, -900, 600,
                     extra_outputs=[("Spec", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])
    wire(surface, surface_inputs, [masks, snow_d, snow_ord, noise_fine, track_out, frost_amount, world_pos, camera_pos,
                                   vector("SnowTint", unreal.LinearColor(0.92, 0.95, 1.0, 1.0), 1400),
                                   scalar("SnowBrightness", 1.0, 1450), scalar("MacroShade", 0.08, 1500),
                                   scalar("SnowAlbedoGamma", 0.6, 1510), scalar("DetailFadeStartCm", 2200.0, 1520),
                                   scalar("DetailFadeLengthCm", 2500.0, 1530), scalar("RoughnessScale", 1.0, 1550),
                                   scalar("CompactDarken", 0.8, 1600, "Tracks"), scalar("CompactOcclusion", 0.12, 1650, "Tracks"),
                                   scalar("BermBrighten", 1.06, 1700, "Tracks"),
                                   scalar("SparkleDensity", 0.035, 1750, "Sparkle"), scalar("SparkleStrength", 0.5, 1800, "Sparkle"),
                                   scalar("SparkleNearCm", 1500.0, 1850, "Sparkle"), scalar("SparkleFarCm", 4500.0, 1900, "Sparkle")],
         rgba=(noise_fine,))

    normal_inputs = ["Masks", "SnowN", "N", "Slope", "NormalStrength", "TrackNormalStrength"]
    normal = custom(NORMAL_HLSL, "Ground snow world normal", normal_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -900, 1100)
    for input_name, node in zip(normal_inputs, [masks, snow_n, normal_ws, slope_out, scalar("NormalStrength", 0.55, 2000),
                                                scalar("TrackNormalStrength", 1.0, 2050, "Tracks")]):
        pin = slope_pin if input_name == "Slope" else ""
        if not mel.connect_material_expressions(node, pin, normal, input_name):
            log("connect FAILED: %s -> normal" % input_name)

    # Outputs ----------------------------------------------------------------------------------------------------------
    colour = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, 500)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        colour.set_editor_property(channel, on)
    mel.connect_material_expressions(surface, "", colour, "")
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, 650)
    for channel, on in (("r", False), ("g", False), ("b", False), ("a", True)):
        roughness.set_editor_property(channel, on)
    mel.connect_material_expressions(surface, "", roughness, "")
    mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    if not mel.connect_material_property(surface, "Spec", unreal.MaterialProperty.MP_SPECULAR):
        log("connect FAILED: Spec -> Specular")
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    opacity = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, -900)
    for channel, on in (("r", False), ("g", True), ("b", False), ("a", False)):
        opacity.set_editor_property(channel, on)
    mel.connect_material_expressions(masks, "", opacity, "")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # The camera-ward WPO nudge of M_SnowOverlay (no z-fighting with the base mesh): ~0.5 cm close up, ~2 cm at 30 m.
    offset_base = scalar("DepthNudgeCm", 0.5, 3600, "Depth")
    offset_per_cm = scalar("DepthNudgePerCm", 0.0005, 3650, "Depth")
    vertex_position = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1500, 3700)
    cam_pos_vs = mel.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -1500, 3600)
    to_camera = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -1300, 3650)
    mel.connect_material_expressions(cam_pos_vs, "", to_camera, "A")
    mel.connect_material_expressions(vertex_position, "", to_camera, "B")
    distance = mel.create_material_expression(material, unreal.MaterialExpressionLength, -1150, 3650)
    mel.connect_material_expressions(to_camera, "", distance, "")
    scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -1000, 3600)
    mel.connect_material_expressions(distance, "", scaled, "A")
    mel.connect_material_expressions(offset_per_cm, "", scaled, "B")
    amount = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -850, 3600)
    mel.connect_material_expressions(scaled, "", amount, "A")
    mel.connect_material_expressions(offset_base, "", amount, "B")
    direction = mel.create_material_expression(material, unreal.MaterialExpressionNormalize, -1000, 3750)
    mel.connect_material_expressions(to_camera, "", direction, "")
    nudge = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, 3650)
    mel.connect_material_expressions(direction, "", nudge, "A")
    mel.connect_material_expressions(amount, "", nudge, "B")
    mel.connect_material_property(nudge, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    mel.layout_material_expressions(material)
    errors = mel.recompile_material(material)
    return material, (list(errors) if errors else [])


def stats_of(material, label):
    try:
        stats = mel.get_statistics(material)
        log("%s stats: vs %d ps %d instructions, samplers %d, texture samples vs %d ps %d, interpolators %d"
            % (label, stats.num_vertex_shader_instructions, stats.num_pixel_shader_instructions, stats.num_samplers,
               stats.num_vertex_texture_samples, stats.num_pixel_texture_samples, stats.num_interpolator_scalars))
    except Exception as error:  # noqa: BLE001
        log("%s stats unavailable: %s" % (label, error))


if dryrun:
    material, errors = build(True, True)
    log("dry run (track part with stand-ins) compile errors: %s" % (errors or "none"))
    stats_of(material, "dry run")

with_track = has_window and has_rt
material, errors = build(with_track, False)
log("compile errors: %s" % (errors or "none"))
saved = eal.save_asset(PATH + "/" + NAME, only_if_is_dirty=False)
log("created %s/%s saved=%s track read: %s" % (PATH, NAME, saved, "WIRED" if with_track else
                                                  "PENDING (missing: %s)" % ", ".join(
                                                      ([] if has_window else ["MPC SnowTrackWindow"]) + ([] if has_rt else [RT_PATH]))))
stats_of(material, "final")

full_instance = PATH + "/" + INSTANCE
if eal.does_asset_exist(full_instance):
    instance = eal.load_asset(full_instance)
    log("kept the instance (exists, re-parented): " + full_instance)
else:
    instance = tools.create_asset(INSTANCE, PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mel.set_material_instance_parent(instance, material)
saved = eal.save_asset(full_instance, only_if_is_dirty=False)
log("instance %s saved=%s" % (full_instance, saved))
write_log()
