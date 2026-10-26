# Section 18 (docs/systems/environment-weather.md): wet ground + puddles on the landscape (M_Landscape_Master), driven by MPC_Environment
# Wetness / PuddleAmount / IsRaining / RainIntensity - EXACTLY neutral at 0 (every blend is lerp(input, wet, mask) with mask 0 = the input,
# no renormalisation), so an arctic level (the presenter never writes the rain scalars there: they stay 0) renders as before.
# Builds /Game/Environment/Weather/Rain/Materials/MF_RainWetSurface (own asset, emptied and rebuilt on every run) and inserts ONE call
# of it between the snow functions and the material outputs: BaseColor / Roughness / Normal from MF_SnowInteraction and Specular from
# MF_SnowSurface go through it (AO and Emissive are untouched). The function:
#   - wet ground: albedo darker (WetDarken) and a little more saturated, roughness x WetRoughnessScale, specular up to 0.5;
#   - puddles in flat spots (vertex up > FlatMin) by a two-scale noise field (T_TilingNoisePacked, the master's SnowNoiseTexture - the
#     shared wrap sampler: +0 samplers), coverage growing with PuddleAmount: near-black albedo, mirror roughness, flat water normal with
#     procedural ripple rings while it rains (no texture);
# Own nodes in the master carry the description "landrain:<key>"; a re-run deletes and re-inserts them. Re-run THIS after
# landscape_snow_patch.py (that script rebuilds its own nodes and rewires the outputs).
# Modes (CODEX_LANDRAIN_MODE): patch (default; saves only M_Landscape_Master + MF_RainWetSurface; MI_Landscape_Master untouched),
#   stats (read-only). Logs: Saved/Logs/landscape_rain_patch.txt / landscape_rain_stats.txt.
# Backups before the first patch: Saved/Backups/rain_20261026_115538/manifest.txt (sha1 of M_Landscape_Master fff01c88...).
# Run (holding the build lock, CODEX_AGENT=claude): UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path> -AllowCommandletRendering
import importlib.util
import os
import traceback

import unreal

LANDSCAPE = "/Game/VFX/Materials/M_Landscape_Master"
LANDSCAPE_MI = "/Game/VFX/Materials/MI_Landscape_Master"
MF_PATH = "/Game/Environment/Weather/Rain/Materials/MF_RainWetSurface"
MF_INTERACTION = "/Game/Environment/Weather/Snow/Materials/MF_SnowInteraction"
MF_SURFACE = "/Game/Environment/Weather/Snow/Materials/MF_SnowSurface"
MPC_PATH = "/Game/Environment/Weather/MPC/MPC_Environment"
NOISE_TEX = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"
TAG = "landrain:"
GROUP = "11_RainWet"

MODE = os.environ.get("CODEX_LANDRAIN_MODE", "patch").lower()
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
_spec = importlib.util.spec_from_file_location("rain_hlsl", os.path.join(PROJECT_DIR, "Scripts", "Editor", "rain_hlsl.py"))
HL = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(HL)

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
SCALAR = unreal.FunctionInputType.FUNCTION_INPUT_SCALAR
VEC3 = unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3
TEX2D = unreal.FunctionInputType.FUNCTION_INPUT_TEXTURE2D
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
SHARED_WRAP = unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS
log_lines = []


class PatchError(Exception):
    pass


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log(name):
    with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", name), "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


# x = wet, y = puddle. Exactly (0, 0) at Wetness 0 and PuddleAmount 0.
MASK_HLSL = r"""
float wet = saturate(Wetness) * WetStrength;
float p = saturate(PuddleAmount);
float th = lerp(PuddleBare, PuddleFull, sqrt(p));
float puddle = smoothstep(th - PuddleEdge, th + PuddleEdge, Field);
puddle *= saturate((Up - FlatMin) / max(1.0 - FlatMin, 0.001));
puddle *= saturate(wet * 2.0);
puddle *= step(0.0001, p);
return float2(wet, puddle);
"""

# Colour + roughness. lerp(x, y, 0) = x: neutral at mask 0.
COLOUR_HLSL = r"""
float wet = Mask.x;
float puddle = Mask.y;
float lum = dot(BaseColor, float3(0.3, 0.59, 0.11));
float3 wetColour = lerp(float3(lum, lum, lum), BaseColor, 1.0 + WetSaturation) * WetDarken;
float3 colour = lerp(BaseColor, wetColour, wet);
colour = lerp(colour, colour * PuddleDarken, puddle);
float rough = lerp(Roughness, Roughness * WetRoughnessScale, wet);
rough = lerp(rough, PuddleRoughness, puddle);
return float4(colour, rough);
"""

SPECULAR_HLSL = r"""
return lerp(Specular, max(Specular, 0.5), max(Mask.x * 0.5, Mask.y));
"""

# Tangent-space normal (the landscape's; ~world XY on the flat spots that hold puddles): the flat water + ripples inside the puddles.
NORMAL_HLSL = r"""
float rain = saturate(IsRaining) * saturate(RainIntensity * 1.5);
float2 s = Slope * (RippleStrength * rain);
float3 water = normalize(float3(-s, 1.0));
return lerp(Normal, water, Mask.y);
"""


def new_expr(owner, cls, x, y):
    if isinstance(owner, unreal.MaterialFunction):
        return mel.create_material_expression_in_function(owner, cls, x, y)
    return mel.create_material_expression(owner, cls, x, y)


def link(src, src_out, dst, dst_in):
    if not mel.connect_material_expressions(src, src_out, dst, dst_in):
        raise PatchError("connect failed: %s.%s -> %s.%s" % (src.get_class().get_name(), src_out, dst.get_class().get_name(), dst_in))


def custom(owner, code, title, inputs, out_type, x, y):
    node = new_expr(owner, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code.strip())
    node.set_editor_property("output_type", out_type)
    node.set_editor_property("description", title)
    items = []
    for name, _src, _out in inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", name)
        items.append(item)
    node.set_editor_property("inputs", items)
    for name, src, out in inputs:
        link(src, out, node, name)
    return node


def fn_input(mf, name, kind, priority, x, y, tooltip=""):
    node = new_expr(mf, unreal.MaterialExpressionFunctionInput, x, y)
    node.set_editor_property("input_name", name)
    node.set_editor_property("input_type", kind)
    node.set_editor_property("sort_priority", priority)
    if tooltip:
        node.set_editor_property("description", tooltip)
    return node


def fn_output(mf, name, priority, source, source_out, x, y):
    node = new_expr(mf, unreal.MaterialExpressionFunctionOutput, x, y)
    node.set_editor_property("output_name", name)
    node.set_editor_property("sort_priority", priority)
    link(source, source_out, node, "")
    return node


def scalar(owner, name, value, priority, x, y):
    node = new_expr(owner, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", GROUP)
    node.set_editor_property("sort_priority", priority)
    return node


def mpc_param(owner, mpc, name, x, y):
    node = new_expr(owner, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", mpc)
    node.set_editor_property("parameter_name", name)
    return node


def mask(owner, source, channels, x, y):
    node = new_expr(owner, unreal.MaterialExpressionComponentMask, x, y)
    for c in ("r", "g", "b", "a"):
        node.set_editor_property(c, c in channels)
    link(source, "", node, "")
    return node


def open_function():
    folder, name = MF_PATH.rsplit("/", 1)
    if eal.does_asset_exist(MF_PATH):
        mf = eal.load_asset(MF_PATH)
        # DeleteAllMaterialExpressionsInFunction leaves expressions behind in 5.8: delete one by one.
        for _attempt in range(8):
            leftovers = list(mel.get_material_function_expressions(mf))
            if not leftovers:
                break
            for expr in leftovers:
                mel.delete_material_expression_in_function(mf, expr)
        if list(mel.get_material_function_expressions(mf)):
            raise PatchError("%s could not be emptied" % MF_PATH)
        log("rebuilding %s (emptied)" % MF_PATH)
    else:
        mf = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
        log("created %s" % MF_PATH)
    mf.set_editor_property("description", "Wet ground + puddles + rain ripples (MPC Wetness / PuddleAmount / IsRaining / RainIntensity). "
                                          "Exactly neutral at 0. Environment section 18.")
    mf.set_editor_property("expose_to_library", True)
    return mf


def build_function(mpc, noise):
    mf = open_function()
    x0 = -1800
    base = fn_input(mf, "BaseColor", VEC3, 0, x0, -700)
    rough = fn_input(mf, "Roughness", SCALAR, 1, x0, -600)
    specular = fn_input(mf, "Specular", SCALAR, 2, x0, -500)
    normal = fn_input(mf, "Normal", VEC3, 3, x0, -400, "Tangent-space normal (the landscape's)")
    world = fn_input(mf, "WorldPosition", VEC3, 4, x0, -300)
    up = fn_input(mf, "Up", SCALAR, 5, x0, -200, "VertexNormalWS.z")
    noise_tex = fn_input(mf, "NoiseTex", TEX2D, 6, x0, -100, "T_TilingNoisePacked (shared wrap sampler)")
    preview = new_expr(mf, unreal.MaterialExpressionTextureObject, x0 - 300, -100)
    preview.set_editor_property("texture", noise)
    preview.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    link(preview, "", noise_tex, "Preview")
    wetness = mpc_param(mf, mpc, "Wetness", x0, 100)
    puddles = mpc_param(mf, mpc, "PuddleAmount", x0, 150)
    raining = mpc_param(mf, mpc, "IsRaining", x0, 200)
    rain = mpc_param(mf, mpc, "RainIntensity", x0, 250)
    p = {}
    for index, (name, value) in enumerate((("WetStrength", 1.0), ("WetDarken", 0.55), ("WetSaturation", 0.15), ("WetRoughnessScale", 0.45),
                                           ("PuddleBare", 1.15), ("PuddleFull", 0.74), ("PuddleEdge", 0.06), ("FlatMin", 0.97),
                                           ("PuddleDarken", 0.5), ("PuddleRoughness", 0.04), ("RippleStrength", 0.45),
                                           ("RippleCellCm", 30.0), ("RippleSpeed", 1.4), ("RippleRing", 0.045),
                                           ("PuddleMacroTilingCm", 900.0), ("PuddleMidTilingCm", 310.0))):
        p[name] = scalar(mf, name, value, index, x0, 400 + index * 60)
    uv_macro = custom(mf, "return W.xy / max(T, 1.0);", "Puddle UV macro", [("W", world, ""), ("T", p["PuddleMacroTilingCm"], "")], F2, x0 + 400, -50)
    uv_mid = custom(mf, "return W.xy / max(T, 1.0);", "Puddle UV mid", [("W", world, ""), ("T", p["PuddleMidTilingCm"], "")], F2, x0 + 400, 50)
    samples = []
    for uv, y in ((uv_macro, -50), (uv_mid, 50)):
        node = new_expr(mf, unreal.MaterialExpressionTextureSample, x0 + 700, y)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        node.set_editor_property("sampler_source", SHARED_WRAP)
        link(uv, "", node, "UVs")
        link(noise_tex, "", node, "Tex")
        samples.append(node)
    field = custom(mf, HL.PUDDLE_FIELD, "Puddle field", [("NoiseMacro", samples[0], "RGBA"), ("NoiseMid", samples[1], "RGBA")], F1, x0 + 1000, 0)
    masks = custom(mf, MASK_HLSL, "Wet + puddle masks",
                   [("Wetness", wetness, ""), ("PuddleAmount", puddles, ""), ("Field", field, ""), ("Up", up, ""),
                    ("WetStrength", p["WetStrength"], ""), ("PuddleBare", p["PuddleBare"], ""), ("PuddleFull", p["PuddleFull"], ""),
                    ("PuddleEdge", p["PuddleEdge"], ""), ("FlatMin", p["FlatMin"], "")], F2, x0 + 1300, 0)
    colour = custom(mf, COLOUR_HLSL, "Wet colour + roughness",
                    [("BaseColor", base, ""), ("Roughness", rough, ""), ("Mask", masks, ""), ("WetDarken", p["WetDarken"], ""),
                     ("WetSaturation", p["WetSaturation"], ""), ("WetRoughnessScale", p["WetRoughnessScale"], ""),
                     ("PuddleDarken", p["PuddleDarken"], ""), ("PuddleRoughness", p["PuddleRoughness"], "")], F4, x0 + 1600, -600)
    spec = custom(mf, SPECULAR_HLSL, "Wet specular", [("Specular", specular, ""), ("Mask", masks, "")], F1, x0 + 1600, -450)
    time = new_expr(mf, unreal.MaterialExpressionTime, x0 + 1000, 300)
    slope = custom(mf, HL.RIPPLES, "Rain ripples (procedural)",
                   [("WP", world, ""), ("Time", time, ""), ("CellCm", p["RippleCellCm"], ""), ("Speed", p["RippleSpeed"], ""),
                    ("Ring", p["RippleRing"], "")], F2, x0 + 1300, 300)
    normal_out = custom(mf, NORMAL_HLSL, "Puddle normal (tangent)",
                        [("Normal", normal, ""), ("Mask", masks, ""), ("Slope", slope, ""), ("IsRaining", raining, ""),
                         ("RainIntensity", rain, ""), ("RippleStrength", p["RippleStrength"], "")], F3, x0 + 1600, -300)
    fn_output(mf, "BaseColor", 0, mask(mf, colour, ("r", "g", "b"), x0 + 1900, -650), "", x0 + 2200, -650)
    fn_output(mf, "Roughness", 1, mask(mf, colour, ("a",), x0 + 1900, -550), "", x0 + 2200, -550)
    fn_output(mf, "Specular", 2, spec, "", x0 + 2200, -450)
    fn_output(mf, "Normal", 3, normal_out, "", x0 + 2200, -300)
    fn_output(mf, "PuddleMask", 4, mask(mf, masks, ("g",), x0 + 1900, 0), "", x0 + 2200, 0)
    mel.layout_material_function_expressions(mf)
    mel.update_material_function(mf, None)
    log("%s: %d expressions, saved=%s" % (MF_PATH, mel.get_num_material_expressions_in_function(mf), eal.save_asset(MF_PATH, only_if_is_dirty=False)))
    return mf


def desc_of(expr):
    try:
        return str(expr.get_editor_property("desc") or "")
    except Exception:  # noqa: BLE001
        return ""


def function_path(expr):
    if not isinstance(expr, unreal.MaterialExpressionMaterialFunctionCall):
        return ""
    mf = expr.get_editor_property("material_function")
    return mf.get_path_name().split(".")[0] if mf else ""


def find_param(material, name):
    for expr in mel.get_material_expressions(material):
        try:
            if str(expr.get_editor_property("parameter_name")) == name and not isinstance(expr, unreal.MaterialExpressionCollectionParameter):
                return expr
        except Exception:  # noqa: BLE001
            continue
    raise PatchError("parameter %s not found" % name)


def platform_name():
    try:
        return unreal.LandscapeAuthoringLibrary.get_max_shader_platform_name()
    except Exception:  # noqa: BLE001
        return "max RHI platform"


def compile_report(material, label):
    errors = list(mel.recompile_material(material)) if isinstance(material, unreal.Material) else []
    s = mel.get_statistics(material)
    line = "%s [%s]: errors %d | VS %d PS %d instructions | samplers %d | texture samples VS %d PS %d | UV scalars %d interp %d" % (
        label, platform_name(), len(errors), s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers,
        s.num_vertex_texture_samples, s.num_pixel_texture_samples, s.num_uv_scalars, s.num_interpolator_scalars)
    log("STATS " + line)
    for error in errors:
        log("  COMPILE ERROR: %s" % error)
    return errors, line


def tagged(expr, key):
    expr.set_editor_property("desc", TAG + key)
    return expr


def patch_master(mf):
    material = eal.load_asset(LANDSCAPE)
    owned = [e for e in mel.get_material_expressions(material) if desc_of(e).startswith(TAG)]
    log("M_Landscape_Master: %s" % ("already patched - %d own nodes rebuilt" % len(owned) if owned else "unpatched"))
    before = None
    if not owned:
        _errors, before = compile_report(material, "before the rain function")
    for expr in owned:
        mel.delete_material_expression(material, expr)
    # The snow functions feeding the outputs: AO comes from MF_SnowInteraction, Emissive from MF_SnowSurface (never rerouted here).
    interaction = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    surface = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if function_path(interaction) != MF_INTERACTION or function_path(surface) != MF_SURFACE:
        raise PatchError("the snow functions were not found on AO / Emissive (%s, %s): run landscape_snow_patch.py first" % (
            function_path(interaction), function_path(surface)))
    noise = find_param(material, "SnowNoiseTexture")
    x, y = 1400, 0
    call = tagged(new_expr(material, unreal.MaterialExpressionMaterialFunctionCall, x, y), "wet")
    call.set_editor_property("material_function", mf)
    world = tagged(new_expr(material, unreal.MaterialExpressionWorldPosition, x - 400, y + 300), "world")
    normal_ws = tagged(new_expr(material, unreal.MaterialExpressionVertexNormalWS, x - 600, y + 400), "vertex-normal")
    up = tagged(mask(material, normal_ws, ("b",), x - 400, y + 400), "up")
    link(interaction, "BaseColor", call, "BaseColor")
    link(interaction, "Roughness", call, "Roughness")
    link(surface, "Specular", call, "Specular")
    link(interaction, "Normal", call, "Normal")
    link(world, "", call, "WorldPosition")
    link(up, "", call, "Up")
    link(noise, "", call, "NoiseTex")
    for prop, out in (("MP_BASE_COLOR", "BaseColor"), ("MP_ROUGHNESS", "Roughness"), ("MP_SPECULAR", "Specular"), ("MP_NORMAL", "Normal")):
        if not mel.connect_material_property(call, out, getattr(unreal.MaterialProperty, prop)):
            raise PatchError("output %s could not be connected" % prop)
    _errors, after = compile_report(material, "after the rain function")
    log("saved %s: %s" % (LANDSCAPE, eal.save_asset(LANDSCAPE, only_if_is_dirty=False)))
    return before, after


def run_patch():
    mpc = eal.load_asset(MPC_PATH)
    scalars = [str(q.get_editor_property("parameter_name")) for q in mpc.get_editor_property("scalar_parameters")]
    for needed in ("Wetness", "PuddleAmount", "IsRaining", "RainIntensity"):
        if needed not in scalars:
            raise PatchError("MPC has no scalar %s" % needed)
    mf = build_function(mpc, eal.load_asset(NOISE_TEX))
    before, after = patch_master(mf)
    _errors, mi = compile_report(eal.load_asset(LANDSCAPE_MI), "MI_Landscape_Master (not modified)")
    log("SUMMARY before: %s" % (before or "(was already patched)"))
    log("SUMMARY after: %s" % after)
    log("SUMMARY MI: %s" % mi)


def run_stats():
    for path in (LANDSCAPE, LANDSCAPE_MI):
        compile_report(eal.load_asset(path), path)
    material = eal.load_asset(LANDSCAPE)
    log("own nodes: %d" % len([e for e in mel.get_material_expressions(material) if desc_of(e).startswith(TAG)]))
    for prop in ("MP_BASE_COLOR", "MP_ROUGHNESS", "MP_NORMAL", "MP_AMBIENT_OCCLUSION", "MP_SPECULAR", "MP_EMISSIVE_COLOR"):
        node = mel.get_material_property_input_node(material, getattr(unreal.MaterialProperty, prop))
        log("output %s <- %s %s" % (prop, function_path(node) or (node.get_class().get_name() if node else "none"), desc_of(node) if node else ""))


if MODE == "patch":
    try:
        run_patch()
    except Exception as error:  # noqa: BLE001
        log("PATCH FAILED: %s" % error)
        log(traceback.format_exc())
    write_log("landscape_rain_patch.txt")
elif MODE == "stats":
    run_stats()
    write_log("landscape_rain_stats.txt")
