# Landscape material fix + snow functions (environment §13, docs/systems/environment-weather.md).
# Modes (env CODEX_LANDSNOW_MODE):
#   dump   (default) read-only: M_Landscape_Master / MI_Landscape_Master / M_SnowTracks_Fade graphs, MPC_Environment, the snow-track
#          referencers and material stats -> Saved/Logs/landscape_snow_dump.txt. Saves nothing.
#   patch  idempotent: (1) the FIX - slope mask from PixelNormalWS.z, ice mask from WorldPosition.z, the old track sample as a scalar
#          (R) mask (the float2 x float3 multiply that broke the compile); (2) (re)builds the snow functions MF_SnowPowder /
#          MF_SnowSurface / MF_SnowInteraction in /Game/Environment/Weather/Snow/Materials and wires them into M_Landscape_Master
#          (own nodes carry the description "landsnow:<key>"; an already patched graph is detected by them and its own nodes are
#          rebuilt, the user's nodes are never deleted); disconnects the landscape WPO (track depth path); points MI_Landscape_Master's
#          RT_SnowTracks override at RT_SnowInteraction when that RT exists. Saves only M_Landscape_Master, MI_Landscape_Master and
#          the three MFs. Log: Saved/Logs/landscape_snow_patch.txt (the first run on an unpatched graph also writes the "after the
#          fix" stats there; landscape_snow_patch_first.txt keeps that first log).
#   stats  read-only: compile errors + instruction / sampler counts of M_Landscape_Master and MI_Landscape_Master for the editor's
#          max shader platform (PCD3D_SM6 with D3D12) -> Saved/Logs/landscape_snow_stats.txt.
# Run (holding the build lock, $env:CODEX_AGENT = "claude"; -AllowCommandletRendering for the SM6 compile / stats):
#   $env:CODEX_LANDSNOW_MODE = "patch"
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -AllowCommandletRendering
# Backups of the user's assets before the first patch: Saved/Backups/landscape_snow_20261022_233432/ (manifest.txt with sha1).
import os
import unreal

LANDSCAPE = "/Game/VFX/Materials/M_Landscape_Master"
LANDSCAPE_MI = "/Game/VFX/Materials/MI_Landscape_Master"
FADE = "/Game/VFX/SnowTracks/M_SnowTracks_Fade"
MPC_PATH = "/Game/Environment/Weather/MPC/MPC_Environment"
TRACK_MANAGER = "/Game/VFX/SnowTracks/BP_SnowTrackManager"

MODE = os.environ.get("CODEX_LANDSNOW_MODE", "dump").lower()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log(name):
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", name)
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


PROPS = ("texture", "parameter_name", "r", "g", "b", "a", "constant", "default_value", "const_a", "const_b", "const_alpha",
         "sampler_type", "sampler_source", "coordinate_index", "u_tiling", "v_tiling", "const_exponent", "material_function",
         "default_value_a", "default_value_b", "collection", "group", "sort_priority", "r", "g", "b", "a", "text", "name",
         "code", "output_type", "description", "mapping_scale", "world_position_shader_offset", "world_position_origin_type",
         "min", "max", "const_min", "const_max", "input_name", "parameter_name", "layer_name", "const_coordinate",
         "mask_r", "mask_g", "mask_b", "mask_a", "mip_value_mode", "const_b", "use_previous_frame", "mapping_type",
         "custom_uv", "screen_alignment", "normalize", "name", "spline_param")


def describe(expr):
    d = []
    seen = set()
    for prop in PROPS:
        if prop in seen:
            continue
        seen.add(prop)
        try:
            v = expr.get_editor_property(prop)
        except Exception:
            continue
        if v is None:
            continue
        if isinstance(v, unreal.Object):
            v = v.get_path_name()
        elif isinstance(v, unreal.LinearColor):
            v = "(%.4g,%.4g,%.4g,%.4g)" % (v.r, v.g, v.b, v.a)
        elif isinstance(v, str) and len(v) > 400:
            v = v.replace("\n", "\\n")
        else:
            v = str(v).replace("\n", "\\n")
        d.append("%s=%s" % (prop, v))
    return ", ".join(d)


# Only the plain outputs (asking for an attribute-style output such as MP_MATERIAL_ATTRIBUTES crashes the 5.8 library).
OUTPUT_PROPS = ("MP_BASE_COLOR", "MP_METALLIC", "MP_SPECULAR", "MP_ROUGHNESS", "MP_ANISOTROPY", "MP_NORMAL", "MP_TANGENT",
                "MP_EMISSIVE_COLOR", "MP_OPACITY", "MP_OPACITY_MASK", "MP_WORLD_POSITION_OFFSET", "MP_AMBIENT_OCCLUSION",
                "MP_REFRACTION", "MP_PIXEL_DEPTH_OFFSET", "MP_SUBSURFACE_COLOR", "MP_DISPLACEMENT")


def dump_graph(material):
    """Walk every material output; print each expression once with an id and its inputs by id."""
    ids = {}
    order = []

    def key(expr):
        return expr.get_path_name()

    def visit(expr):
        k = key(expr)
        if k in ids:
            return ids[k]
        ids[k] = "n%d" % len(ids)
        try:
            inputs = mel.get_inputs_for_material_expression(material, expr)
            names = mel.get_material_expression_input_names(expr)
        except Exception:
            inputs, names = [], []
        links = []
        for i, child in enumerate(inputs):
            if child is None:
                continue
            cid = visit(child)
            try:
                out = str(mel.get_input_node_output_name_for_material_expression(expr, child))
            except Exception:
                out = ""
            links.append("%s<-%s%s" % (names[i] if i < len(names) else i, cid, ("." + out) if out else ""))
        pos = ""
        try:
            pos = " @(%d,%d)" % (expr.get_editor_property("material_expression_editor_x"), expr.get_editor_property("material_expression_editor_y"))
        except Exception:
            pass
        order.append("%s %s%s {%s} [%s]" % (ids[k], expr.get_class().get_name().replace("MaterialExpression", ""), pos,
                                           describe(expr), "; ".join(links)))
        return ids[k]

    for prop in OUTPUT_PROPS:
        p = getattr(unreal.MaterialProperty, prop, None)
        if p is None:
            continue
        try:
            node = mel.get_material_property_input_node(material, p)
        except Exception:
            node = None
        if node is None:
            continue
        try:
            out = str(mel.get_material_property_input_node_output_name(material, p))
        except Exception:
            out = ""
        nid = visit(node)
        log("OUTPUT %s <- %s%s" % (prop, nid, ("." + out) if out else ""))
    for line in order:
        log(line)
    log("connected expressions: %d, total expressions: %d" % (len(ids), mel.get_num_material_expressions(material)))


def dump_material(path):
    material = eal.load_asset(path)
    log("==== %s (%s)" % (path, material.get_class().get_name()))
    for prop in ("material_domain", "blend_mode", "shading_model", "used_with_landscape", "two_sided", "tangent_space_normal",
                 "use_material_attributes", "num_customized_u_vs"):
        try:
            log("  %s = %s" % (prop, material.get_editor_property(prop)))
        except Exception:
            pass
    log("  scalars: %s" % [(str(n), mel.get_material_default_scalar_parameter_value(material, n)) for n in mel.get_scalar_parameter_names(material)])
    log("  vectors: %s" % [(str(n), str(mel.get_material_default_vector_parameter_value(material, n))) for n in mel.get_vector_parameter_names(material)])
    log("  textures: %s" % [(str(n), mel.get_material_default_texture_parameter_value(material, n).get_path_name() if mel.get_material_default_texture_parameter_value(material, n) else None) for n in mel.get_texture_parameter_names(material)])
    try:
        log("  static switches: %s" % [str(n) for n in mel.get_static_switch_parameter_names(material)])
    except Exception:
        pass
    try:
        log("  used textures: %s" % [t.get_path_name() for t in mel.get_used_textures(material)])
    except Exception as error:  # noqa: BLE001
        log("  used textures: n/a %s" % error)
    dump_graph(material)


def dump_instance(path):
    mi = eal.load_asset(path)
    log("==== %s parent %s" % (path, mi.get_editor_property("parent").get_path_name()))
    for v in mi.get_editor_property("scalar_parameter_values"):
        log("  scalar %s = %s" % (v.get_editor_property("parameter_info").get_editor_property("name"), v.get_editor_property("parameter_value")))
    for v in mi.get_editor_property("vector_parameter_values"):
        log("  vector %s = %s" % (v.get_editor_property("parameter_info").get_editor_property("name"), v.get_editor_property("parameter_value")))
    for v in mi.get_editor_property("texture_parameter_values"):
        t = v.get_editor_property("parameter_value")
        log("  texture %s = %s" % (v.get_editor_property("parameter_info").get_editor_property("name"), t.get_path_name() if t else None))


def stats_of(path):
    material = eal.load_asset(path)
    try:
        s = mel.get_statistics(material)
        return "vs %d ps %d samplers %d vs-tex %d ps-tex %d vt %d uv %d interp %d (shader platform %s)" % (
            s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers, s.num_vertex_texture_samples,
            s.num_pixel_texture_samples, s.num_virtual_texture_samples, s.num_uv_scalars, s.num_interpolator_scalars,
            unreal.SystemLibrary.get_rendering_detail_mode() if False else "max-rhi")
    except Exception as error:  # noqa: BLE001
        return "stats unavailable: %s" % error


# =====================================================================================================================
# Patch / stats (environment §13)
# =====================================================================================================================
MF_DIR = "/Game/Environment/Weather/Snow/Materials"
MF_POWDER = MF_DIR + "/MF_SnowPowder"
MF_SURFACE = MF_DIR + "/MF_SnowSurface"
MF_INTERACTION = MF_DIR + "/MF_SnowInteraction"
NOISE_TEX = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"
RT_NEW = "/Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction"
RT_OLD = "/Game/VFX/SnowTracks/RT_SnowTracks"
TAG = "landsnow:"
# MPC_Environment.SnowTrackWindow = (origin X, origin Y, 1 / size, enabled), written by the interactive-snow subsystem. The origin is the
# window's world-space MIN corner (RT texel (0, 0) = origin, U along +X, V along +Y) - see WINDOW_HLSL; change both here if that changes.
WINDOW_PARAM = "SnowTrackWindow"
FIRST_LOG = "landscape_snow_patch_first.txt"

SCALAR = unreal.FunctionInputType.FUNCTION_INPUT_SCALAR
VEC3 = unreal.FunctionInputType.FUNCTION_INPUT_VECTOR3
VEC4 = unreal.FunctionInputType.FUNCTION_INPUT_VECTOR4
TEX2D = unreal.FunctionInputType.FUNCTION_INPUT_TEXTURE2D
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
SHARED_WRAP = unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS
SHARED_CLAMP = unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS


class PatchError(Exception):
    pass


def new_expr(owner, cls, x, y):
    if isinstance(owner, unreal.MaterialFunction):
        return mel.create_material_expression_in_function(owner, cls, x, y)
    return mel.create_material_expression(owner, cls, x, y)


def link(src, src_out, dst, dst_in):
    if not mel.connect_material_expressions(src, src_out, dst, dst_in):
        raise PatchError("connect failed: %s.%s -> %s.%s" % (src.get_class().get_name(), src_out, dst.get_class().get_name(), dst_in))


def custom(owner, code, title, inputs, out_type, x, y):
    """Custom HLSL node; inputs = [(name, source expression, source output name)]."""
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


def sampler_type_of(texture):
    if isinstance(texture, unreal.TextureRenderTarget2D):
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
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


PREVIEW_NOTES = set()


def set_preview(node, values):
    """FunctionInput.PreviewValue is an FVector4f; its Python binding has no x / y / z / w attributes in 5.8, so several routes are tried.
    The preview only matters for an input left unconnected (M_Landscape_Master connects every input)."""
    value = node.get_editor_property("preview_value")
    numbers = [float(v) for v in values]
    attempts = (
        ("constructor", lambda: type(value)(*numbers)),
        ("editor properties", lambda: [value.set_editor_property(f, n) for f, n in zip(("x", "y", "z", "w"), numbers)] and value),
        ("import_text", lambda: value.import_text("(X=%f,Y=%f,Z=%f,W=%f)" % tuple(numbers)) or value),
    )
    for name, attempt in attempts:
        try:
            node.set_editor_property("preview_value", attempt())
            PREVIEW_NOTES.add("preview values set via %s" % name)
            return
        except Exception as error:  # noqa: BLE001 - try the next route
            PREVIEW_NOTES.add("preview via %s failed: %s" % (name, str(error).splitlines()[0]))
    PREVIEW_NOTES.add("preview values left at 0")


def fn_input(mf, name, kind, priority, preview, x, y, tooltip=""):
    node = new_expr(mf, unreal.MaterialExpressionFunctionInput, x, y)
    node.set_editor_property("input_name", name)
    node.set_editor_property("input_type", kind)
    node.set_editor_property("sort_priority", priority)
    if tooltip:
        node.set_editor_property("description", tooltip)
    if preview is not None:
        set_preview(node, preview)
    node.set_editor_property("use_preview_value_as_default", True)
    return node


def fn_tex_input(mf, name, priority, texture, x, y, tooltip=""):
    node = fn_input(mf, name, TEX2D, priority, None, x, y, tooltip)
    preview = new_expr(mf, unreal.MaterialExpressionTextureObject, x - 300, y)
    preview.set_editor_property("texture", texture)
    preview.set_editor_property("sampler_type", sampler_type_of(texture))
    link(preview, "", node, "Preview")
    return node


def fn_output(mf, name, priority, source, source_out, x, y):
    node = new_expr(mf, unreal.MaterialExpressionFunctionOutput, x, y)
    node.set_editor_property("output_name", name)
    node.set_editor_property("sort_priority", priority)
    link(source, source_out, node, "")
    return node


def scalar_param(owner, name, value, group, priority, x, y):
    node = new_expr(owner, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    node.set_editor_property("sort_priority", priority)
    return node


def tex_sample(owner, texture_input, uv, uv_out, sampler_type, source, x, y):
    node = new_expr(owner, unreal.MaterialExpressionTextureSample, x, y)
    node.set_editor_property("sampler_type", sampler_type)
    node.set_editor_property("sampler_source", source)
    link(uv, uv_out, node, "UVs")
    link(texture_input, "", node, "Tex")
    return node


def transform_world_to_tangent(owner, source, x, y):
    node = new_expr(owner, unreal.MaterialExpressionTransform, x, y)
    node.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    node.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_TANGENT)
    link(source, "", node, "")
    return node


def open_function(path, description):
    """Own asset: created once, emptied and rebuilt on every patch run (deterministic)."""
    folder, name = path.rsplit("/", 1)
    if eal.does_asset_exist(path):
        mf = eal.load_asset(path)
        # DeleteAllMaterialExpressionsInFunction leaves some expressions behind in 5.8 (stale FunctionOutputs with the same names then
        # shadow the new ones): delete one by one until the function is empty.
        for _attempt in range(8):
            leftovers = list(mel.get_material_function_expressions(mf))
            if not leftovers:
                break
            for expr in leftovers:
                mel.delete_material_expression_in_function(mf, expr)
        remaining = len(list(mel.get_material_function_expressions(mf)))
        if remaining:
            raise PatchError("%s: %d expressions could not be deleted" % (path, remaining))
        log("rebuilding %s (emptied)" % path)
    else:
        mf = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
        log("created %s" % path)
    mf.set_editor_property("description", description)
    mf.set_editor_property("expose_to_library", True)
    try:
        mf.set_editor_property("library_categories_text", [unreal.Text("CodexTactics|Snow")])
    except Exception:  # noqa: BLE001 - cosmetic
        pass
    return mf


def finish_function(mf, path):
    mel.layout_material_function_expressions(mf)
    mel.update_material_function(mf, None)
    log("%s: %d expressions, saved=%s" % (path, mel.get_num_material_expressions_in_function(mf), eal.save_asset(path, only_if_is_dirty=False)))


# --- HLSL -------------------------------------------------------------------------------------------------------------

UV_XY_HLSL = "return W.xy / max(Tiling, 1.0);"

POWDER_HLSL = r"""
// Powder: snow settling on the bare layers (rock, ice) as the cover grows. Neutral at SnowAmount 0.
float amount = saturate(SnowAmount);
float bare = saturate(RockMask + IceMask);
// Flat faces first: the up threshold sinks from 0.97 (a dusting) to PowderUpMin (full cover).
float upMask = saturate((Up - lerp(0.97, PowderUpMin, amount)) * 8.0);
// Patches: the packed noise fills in as the cover grows.
float patches = Noise.r * 0.6 + Noise.g * 0.4;
float cover = saturate((patches - 1.0 + amount * 1.05) * PowderContrast);
float powder = saturate(amount * upMask * cover * PowderStrength) * bare;
float snowWeight = saturate((1.0 - RockMask) * (1.0 - IceMask) + powder);
return float2(powder, snowWeight);
"""

POWDER_COLOUR_HLSL = r"""
return float4(lerp(BaseColor, SnowColor, Mask.x), lerp(Roughness, SnowRoughness, Mask.x));
"""

SURFACE_WEIGHTS_HLSL = r"""
// x fresh, y frost, z glint. Neutral when FreshSnow and FrostAmount are 0.
float w = saturate(SnowWeight);
float fresh = saturate(FreshSnow) * w;
// Fresh powder buries most of the rime (the frost reads in clear weather).
float frost = saturate(FrostAmount) * lerp(FrostOnBare, 1.0, w) * (1.0 - 0.6 * fresh);
// Glint: two decorrelated lookups of the packed noise (one shifted by the view direction) multiplied are high only in small spots (~2 %)
// that twinkle when the view moves. T_TilingNoisePacked: G mean 0.50, B mean 0.42 (p95 ~0.78); A is a sparse cell-edge channel.
float sparkle = Micro.b * Glint.g;
float glint = saturate((sparkle - GlintThreshold) * 16.0) * frost * GlintStrength;
return float4(fresh, frost, glint, 0.0);
"""

SURFACE_GLINT_UV_HLSL = r"""
return W.xy / max(GlintTiling, 1.0) * 1.37 + V.xy * GlintParallax;
"""

SURFACE_COLOUR_HLSL = r"""
float3 colour = BaseColor * (1.0 + FreshBrightness * Weights.x);
float rough = lerp(Roughness, FreshRoughness, Weights.x);
rough = lerp(rough, FrostRoughness, Weights.y);
rough = lerp(rough, 0.05, Weights.z);
return float4(colour, saturate(rough));
"""

SURFACE_EMISSIVE_HLSL = r"""
return BaseColor * (Weights.z * GlintEmissive);
"""

# Normal path: only vertex-normal / position based inputs (PixelNormalWS may not feed the Normal output).
SURFACE_BEND_HLSL = r"""
float fresh = saturate(FreshSnow) * saturate(SnowWeight);
// Centred on the channel means of T_TilingNoisePacked (G 0.50, B 0.42; R is skewed to 0.31) so the bend has no constant tilt.
return float3((Micro.g - 0.5) * 2.0, (Micro.b - 0.42) * 2.0, 0.0) * (FreshNormalStrength * fresh);
"""

NORMAL_COMBINE_HLSL = r"""
return normalize(float3(NormalIn.xy + Bend.xy, NormalIn.z));
"""

# x, y = RT UV; z = UV step of the normal samples; w = fade (edge fade 2 m x enabled). Window = (origin X, origin Y, 1 / size, enabled),
# origin = the window's min corner. A disabled window (w 0) or no window (z 0) gives fade 0 = no effect.
WINDOW_HLSL = r"""
float2 uv = (W.xy - Window.xy) * Window.z;
float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) / max(Window.z, 1e-7);
float fade = saturate(edge / max(EdgeFadeCm, 1.0)) * saturate(Window.w) * step(1e-9, Window.z);
return float4(uv, NormalSampleCm * Window.z, fade);
"""

UV_OFFSET_X_HLSL = "return UVW.xy + float2(UVW.z, 0.0);"
UV_OFFSET_Y_HLSL = "return UVW.xy + float2(0.0, UVW.z);"
UV_CENTRE_HLSL = "return UVW.xy;"

# x compaction, y berm (both faded, 0..1), z / w = height slope dH/dX, dH/dY (cm per cm, faded). The RT holds 0..4 per channel
# (USnowInteractionSubsystem clamps); height (cm) = G (1 - saturate(R)) MaxBermCm - saturate(R) MaxDepthCm (docs/design/INTERACTIVE_SNOW.md §3).
DECODE_HLSL = r"""
float fade = UVW.w * saturate(SnowWeight);
float r0 = saturate(S0.r), rx = saturate(Sx.r), ry = saturate(Sy.r);
float g0 = max(S0.g, 0.0) * (1.0 - r0), gx = max(Sx.g, 0.0) * (1.0 - rx), gy = max(Sy.g, 0.0) * (1.0 - ry);
float h0 = g0 * MaxBermCm - r0 * MaxDepthCm;
float hx = gx * MaxBermCm - rx * MaxDepthCm;
float hy = gy * MaxBermCm - ry * MaxDepthCm;
float step = max(NormalSampleCm, 0.5);
return float4(r0 * fade, saturate(g0) * fade, (hx - h0) / step * fade, (hy - h0) / step * fade);
"""

# Bump offset: the crushed-snow detail inside a print is looked up where the view ray meets the lowered surface.
CRUSH_UV_HLSL = r"""
float h = D.y * MaxBermCm - D.x * MaxDepthCm;
float2 shift = V.xy / max(V.z, 0.25) * h * BumpOffsetScale;
return (W.xy + shift) / max(CrushTiling, 1.0);
"""

INTERACTION_COLOUR_HLSL = r"""
float r = D.x, b = D.y;
float crush = (Crush.g - 0.5) * CrushStrength * r;
float3 colour = BaseColor * (1.0 - CompactDarken * r + crush) * (1.0 + BermBrighten * b);
float rough = lerp(lerp(Roughness, CompactRoughness, r), BermRoughness, b);
return float4(colour, saturate(rough));
"""

INTERACTION_AO_HLSL = "return 1.0 - CompactAO * D.x;"

INTERACTION_BEND_HLSL = "return float3(-D.z, -D.w, 0.0) * TrackNormalStrength;"

ROCK_VERTEX_HLSL = r"""
// The slope mask from the vertex normal (same angles as the base layer's PixelNormalWS mask): the snow functions feed the Normal output,
// which PixelNormalWS may not.
float angle = acos(clamp(N.z, -1.0, 1.0)) * 57.2957795;
return smoothstep(MinAngle, MaxAngle, angle);
"""


def build_powder(noise):
    mf = open_function(MF_POWDER, "Powder snow settling on rock and ice as the cover grows (SnowAmount x world up x (rock + ice) x noise). "
                                  "Neutral at SnowAmount 0. Environment §13.")
    x0 = -1600
    base = fn_input(mf, "BaseColor", VEC3, 0, (0.5, 0.5, 0.5, 0), x0, -600)
    rough = fn_input(mf, "Roughness", SCALAR, 1, (0.5, 0, 0, 0), x0, -500)
    amount = fn_input(mf, "SnowAmount", SCALAR, 2, (0, 0, 0, 0), x0, -400, "MPC_Environment.SnowAmount (0..1)")
    up = fn_input(mf, "Up", SCALAR, 3, (1, 0, 0, 0), x0, -300, "VertexNormalWS.z")
    rock = fn_input(mf, "RockMask", SCALAR, 4, (0, 0, 0, 0), x0, -200)
    ice = fn_input(mf, "IceMask", SCALAR, 5, (0, 0, 0, 0), x0, -100)
    snow_colour = fn_input(mf, "SnowColor", VEC3, 6, (0.85, 0.92, 0.98, 0), x0, 0)
    snow_rough = fn_input(mf, "SnowRoughness", SCALAR, 7, (0.65, 0, 0, 0), x0, 100)
    world = fn_input(mf, "WorldPosition", VEC3, 8, (0, 0, 0, 0), x0, 200)
    noise_tex = fn_tex_input(mf, "NoiseTex", 9, noise, x0, 300, "T_TilingNoisePacked (shared wrap sampler)")
    group = "08_SnowPowder"
    tiling = scalar_param(mf, "PowderTiling", 900.0, group, 0, x0, 500)
    up_min = scalar_param(mf, "PowderUpMin", 0.6, group, 1, x0, 600)
    contrast = scalar_param(mf, "PowderContrast", 3.0, group, 2, x0, 700)
    strength = scalar_param(mf, "PowderStrength", 1.0, group, 3, x0, 800)
    uv = custom(mf, UV_XY_HLSL, "Powder UV", [("W", world, ""), ("Tiling", tiling, "")], F2, x0 + 400, 300)
    sample = tex_sample(mf, noise_tex, uv, "", sampler_type_of(noise), SHARED_WRAP, x0 + 700, 300)
    mask = custom(mf, POWDER_HLSL, "Powder mask (x powder, y snow weight)",
                  [("SnowAmount", amount, ""), ("Up", up, ""), ("RockMask", rock, ""), ("IceMask", ice, ""), ("Noise", sample, "RGBA"),
                   ("PowderUpMin", up_min, ""), ("PowderContrast", contrast, ""), ("PowderStrength", strength, "")], F2, x0 + 1000, 0)
    colour = custom(mf, POWDER_COLOUR_HLSL, "Powder colour + roughness",
                    [("BaseColor", base, ""), ("Roughness", rough, ""), ("SnowColor", snow_colour, ""), ("SnowRoughness", snow_rough, ""),
                     ("Mask", mask, "")], F4, x0 + 1300, -400)
    rgb = mask_rgb(mf, colour, x0 + 1600, -450)
    alpha = mask_channel(mf, colour, "a", x0 + 1600, -350)
    powder = mask_channel(mf, mask, "r", x0 + 1600, 0)
    weight = mask_channel(mf, mask, "g", x0 + 1600, 100)
    fn_output(mf, "BaseColor", 0, rgb, "", x0 + 1900, -450)
    fn_output(mf, "Roughness", 1, alpha, "", x0 + 1900, -350)
    fn_output(mf, "PowderMask", 2, powder, "", x0 + 1900, 0)
    fn_output(mf, "SnowWeight", 3, weight, "", x0 + 1900, 100)
    finish_function(mf, MF_POWDER)
    return mf


def mask_rgb(owner, source, x, y):
    node = new_expr(owner, unreal.MaterialExpressionComponentMask, x, y)
    for c, v in (("r", True), ("g", True), ("b", True), ("a", False)):
        node.set_editor_property(c, v)
    link(source, "", node, "")
    return node


def mask_channel(owner, source, channel, x, y):
    node = new_expr(owner, unreal.MaterialExpressionComponentMask, x, y)
    for c in ("r", "g", "b", "a"):
        node.set_editor_property(c, c == channel)
    link(source, "", node, "")
    return node


def build_surface(noise):
    mf = open_function(MF_SURFACE, "Snow surface response: FreshSnow (MPC FreshSnow) brighter / rougher with a micro normal; FrostAmount lower "
                                   "roughness and a view-dependent glint from the same noise. Neutral at FreshSnow 0 and FrostAmount 0. Environment §13.")
    x0 = -1800
    base = fn_input(mf, "BaseColor", VEC3, 0, (0.5, 0.5, 0.5, 0), x0, -700)
    rough = fn_input(mf, "Roughness", SCALAR, 1, (0.5, 0, 0, 0), x0, -600)
    normal_in = fn_input(mf, "Normal", VEC3, 2, (0, 0, 1, 0), x0, -500, "Tangent-space normal in")
    weight = fn_input(mf, "SnowWeight", SCALAR, 3, (1, 0, 0, 0), x0, -400, "Snow coverage (vertex-normal based)")
    fresh = fn_input(mf, "FreshSnow", SCALAR, 4, (0, 0, 0, 0), x0, -300, "MPC_Environment.FreshSnow (0..1; IsSnowing when the MPC has no FreshSnow)")
    frost = fn_input(mf, "FrostAmount", SCALAR, 5, (0, 0, 0, 0), x0, -200, "MPC_Environment.FrostAmount (0..1)")
    world = fn_input(mf, "WorldPosition", VEC3, 6, (0, 0, 0, 0), x0, -100)
    camera = fn_input(mf, "CameraVector", VEC3, 7, (0, 0, 1, 0), x0, 0, "CameraVectorWS")
    noise_tex = fn_tex_input(mf, "NoiseTex", 8, noise, x0, 100, "T_TilingNoisePacked (shared wrap sampler)")
    group = "09_SnowSurface"
    params = {}
    for index, (name, value) in enumerate((("FreshBrightness", 0.06), ("FreshRoughness", 0.9), ("FreshNormalStrength", 0.1), ("MicroTiling", 140.0),
                                           ("FrostRoughness", 0.3), ("FrostOnBare", 0.35), ("GlintTiling", 25.0), ("GlintThreshold", 0.46),
                                           ("GlintParallax", 0.6), ("GlintStrength", 1.0), ("GlintEmissive", 1.5))):
        params[name] = scalar_param(mf, name, value, group, index, x0, 300 + index * 100)
    micro_uv = custom(mf, UV_XY_HLSL, "Micro UV", [("W", world, ""), ("Tiling", params["MicroTiling"], "")], F2, x0 + 400, 100)
    micro = tex_sample(mf, noise_tex, micro_uv, "", sampler_type_of(noise), SHARED_WRAP, x0 + 700, 100)
    glint_uv = custom(mf, SURFACE_GLINT_UV_HLSL, "Glint UV (view shifted)",
                      [("W", world, ""), ("V", camera, ""), ("GlintTiling", params["GlintTiling"], ""), ("GlintParallax", params["GlintParallax"], "")],
                      F2, x0 + 400, 300)
    glint = tex_sample(mf, noise_tex, glint_uv, "", sampler_type_of(noise), SHARED_WRAP, x0 + 700, 300)
    weights = custom(mf, SURFACE_WEIGHTS_HLSL, "Fresh / frost / glint weights",
                     [("SnowWeight", weight, ""), ("FreshSnow", fresh, ""), ("FrostAmount", frost, ""), ("Micro", micro, "RGBA"), ("Glint", glint, "RGBA"),
                      ("FrostOnBare", params["FrostOnBare"], ""), ("GlintThreshold", params["GlintThreshold"], ""),
                      ("GlintStrength", params["GlintStrength"], "")], F4, x0 + 1000, -200)
    colour = custom(mf, SURFACE_COLOUR_HLSL, "Surface colour + roughness",
                    [("BaseColor", base, ""), ("Roughness", rough, ""), ("Weights", weights, ""), ("FreshBrightness", params["FreshBrightness"], ""),
                     ("FreshRoughness", params["FreshRoughness"], ""), ("FrostRoughness", params["FrostRoughness"], "")], F4, x0 + 1300, -600)
    specular = new_expr(mf, unreal.MaterialExpressionLinearInterpolate, x0 + 1300, -350)
    specular.set_editor_property("const_a", 0.5)
    specular.set_editor_property("const_b", 1.0)
    link(mask_channel(mf, weights, "b", x0 + 1150, -350), "", specular, "Alpha")
    emissive = custom(mf, SURFACE_EMISSIVE_HLSL, "Glint emissive", [("BaseColor", base, ""), ("Weights", weights, ""), ("GlintEmissive", params["GlintEmissive"], "")],
                      F3, x0 + 1300, -250)
    bend = custom(mf, SURFACE_BEND_HLSL, "Fresh micro normal (world)",
                  [("SnowWeight", weight, ""), ("FreshSnow", fresh, ""), ("Micro", micro, "RGBA"), ("FreshNormalStrength", params["FreshNormalStrength"], "")],
                  F3, x0 + 1000, 200)
    bend_ts = transform_world_to_tangent(mf, bend, x0 + 1300, 200)
    normal = custom(mf, NORMAL_COMBINE_HLSL, "Normal combine", [("NormalIn", normal_in, ""), ("Bend", bend_ts, "")], F3, x0 + 1600, 100)
    fn_output(mf, "BaseColor", 0, mask_rgb(mf, colour, x0 + 1600, -650), "", x0 + 1900, -650)
    fn_output(mf, "Roughness", 1, mask_channel(mf, colour, "a", x0 + 1600, -550), "", x0 + 1900, -550)
    fn_output(mf, "Specular", 2, specular, "", x0 + 1900, -350)
    fn_output(mf, "Emissive", 3, emissive, "", x0 + 1900, -250)
    fn_output(mf, "Normal", 4, normal, "", x0 + 1900, 100)
    finish_function(mf, MF_SURFACE)
    return mf


def build_interaction(noise, rt):
    mf = open_function(MF_INTERACTION, "Interactive snow: RT_SnowInteraction (R compaction, G berm) in the MPC SnowTrackWindow (origin XY = min "
                                       "corner, 1/size, enabled): normals from 3 RT samples (shared clamp sampler), bump-offset crushed detail, "
                                       "compacted = darker / smoother / AO, berm = brighter / rougher, 2 m edge fade; a disabled window = no "
                                       "effect. Environment §13, docs/design/INTERACTIVE_SNOW.md §3.")
    x0 = -2000
    base = fn_input(mf, "BaseColor", VEC3, 0, (0.5, 0.5, 0.5, 0), x0, -700)
    rough = fn_input(mf, "Roughness", SCALAR, 1, (0.5, 0, 0, 0), x0, -600)
    normal_in = fn_input(mf, "Normal", VEC3, 2, (0, 0, 1, 0), x0, -500, "Tangent-space normal in")
    weight = fn_input(mf, "SnowWeight", SCALAR, 3, (1, 0, 0, 0), x0, -400, "Snow coverage (vertex-normal based)")
    world = fn_input(mf, "WorldPosition", VEC3, 4, (0, 0, 0, 0), x0, -300)
    camera = fn_input(mf, "CameraVector", VEC3, 5, (0, 0, 1, 0), x0, -200, "CameraVectorWS")
    window = fn_input(mf, "Window", VEC4, 6, (0, 0, 0, 0), x0, -100, "MPC_Environment.SnowTrackWindow (origin X, origin Y, 1/size, enabled)")
    rt_tex = fn_tex_input(mf, "InteractionRT", 7, rt, x0, 0, "RT_SnowInteraction (R compaction, G berm; shared clamp sampler)")
    noise_tex = fn_tex_input(mf, "NoiseTex", 8, noise, x0, 100, "T_TilingNoisePacked (shared wrap sampler)")
    group = "07_SnowInteraction"
    params = {}
    for index, (name, value) in enumerate((("MaxDepthCm", 20.0), ("MaxBermCm", 6.0), ("EdgeFadeCm", 200.0), ("NormalSampleCm", 8.0),
                                           ("TrackNormalStrength", 0.25), ("CompactDarken", 0.12), ("CompactRoughness", 0.45), ("CompactAO", 0.25),
                                           ("BermBrighten", 0.06), ("BermRoughness", 0.9), ("BumpOffsetScale", 1.0), ("CrushTiling", 18.0),
                                           ("CrushStrength", 0.1))):
        params[name] = scalar_param(mf, name, value, group, index, x0, 300 + index * 100)
    uvw = custom(mf, WINDOW_HLSL, "Window UV + fade", [("W", world, ""), ("Window", window, ""), ("EdgeFadeCm", params["EdgeFadeCm"], ""),
                                                       ("NormalSampleCm", params["NormalSampleCm"], "")], F4, x0 + 400, -100)
    rt_type = sampler_type_of(rt)
    uv0 = custom(mf, UV_CENTRE_HLSL, "UV centre", [("UVW", uvw, "")], F2, x0 + 700, -250)
    uvx = custom(mf, UV_OFFSET_X_HLSL, "UV +x", [("UVW", uvw, "")], F2, x0 + 700, -100)
    uvy = custom(mf, UV_OFFSET_Y_HLSL, "UV +y", [("UVW", uvw, "")], F2, x0 + 700, 50)
    s0 = tex_sample(mf, rt_tex, uv0, "", rt_type, SHARED_CLAMP, x0 + 1000, -250)
    sx = tex_sample(mf, rt_tex, uvx, "", rt_type, SHARED_CLAMP, x0 + 1000, -100)
    sy = tex_sample(mf, rt_tex, uvy, "", rt_type, SHARED_CLAMP, x0 + 1000, 50)
    decode = custom(mf, DECODE_HLSL, "Decode (compaction, berm, dH/dX, dH/dY)",
                    [("UVW", uvw, ""), ("SnowWeight", weight, ""), ("S0", s0, "RGBA"), ("Sx", sx, "RGBA"), ("Sy", sy, "RGBA"),
                     ("MaxDepthCm", params["MaxDepthCm"], ""), ("MaxBermCm", params["MaxBermCm"], ""), ("NormalSampleCm", params["NormalSampleCm"], "")],
                    F4, x0 + 1300, -100)
    crush_uv = custom(mf, CRUSH_UV_HLSL, "Bump offset (crushed detail UV)",
                      [("D", decode, ""), ("W", world, ""), ("V", camera, ""), ("MaxDepthCm", params["MaxDepthCm"], ""), ("MaxBermCm", params["MaxBermCm"], ""),
                       ("BumpOffsetScale", params["BumpOffsetScale"], ""), ("CrushTiling", params["CrushTiling"], "")], F2, x0 + 1600, 150)
    crush = tex_sample(mf, noise_tex, crush_uv, "", sampler_type_of(noise), SHARED_WRAP, x0 + 1900, 150)
    colour = custom(mf, INTERACTION_COLOUR_HLSL, "Compacted / berm colour + roughness",
                    [("BaseColor", base, ""), ("Roughness", rough, ""), ("D", decode, ""), ("Crush", crush, "RGBA"),
                     ("CompactDarken", params["CompactDarken"], ""), ("CompactRoughness", params["CompactRoughness"], ""),
                     ("BermBrighten", params["BermBrighten"], ""), ("BermRoughness", params["BermRoughness"], ""), ("CrushStrength", params["CrushStrength"], "")],
                    F4, x0 + 2200, -600)
    ao = custom(mf, INTERACTION_AO_HLSL, "Compacted AO", [("D", decode, ""), ("CompactAO", params["CompactAO"], "")], F1, x0 + 2200, -400)
    bend = custom(mf, INTERACTION_BEND_HLSL, "Track normal (world)", [("D", decode, ""), ("TrackNormalStrength", params["TrackNormalStrength"], "")],
                  F3, x0 + 1600, -300)
    bend_ts = transform_world_to_tangent(mf, bend, x0 + 1900, -300)
    normal = custom(mf, NORMAL_COMBINE_HLSL, "Normal combine", [("NormalIn", normal_in, ""), ("Bend", bend_ts, "")], F3, x0 + 2200, -250)
    fn_output(mf, "BaseColor", 0, mask_rgb(mf, colour, x0 + 2500, -650), "", x0 + 2800, -650)
    fn_output(mf, "Roughness", 1, mask_channel(mf, colour, "a", x0 + 2500, -550), "", x0 + 2800, -550)
    fn_output(mf, "Normal", 2, normal, "", x0 + 2800, -250)
    fn_output(mf, "AmbientOcclusion", 3, ao, "", x0 + 2800, -400)
    fn_output(mf, "TrackMask", 4, mask_channel(mf, decode, "r", x0 + 2500, 0), "", x0 + 2800, 0)
    finish_function(mf, MF_INTERACTION)
    return mf


# --- master material ----------------------------------------------------------------------------------------------------

def platform_name():
    try:
        return unreal.LandscapeAuthoringLibrary.get_max_shader_platform_name()
    except Exception:  # noqa: BLE001 - the editor module is older than this script
        return "max RHI platform"


def compile_report(material, label):
    """Recompiles for the max RHI shader platform; returns (errors, stats line)."""
    errors = list(mel.recompile_material(material)) if isinstance(material, unreal.Material) else []
    stats = mel.get_statistics(material)
    line = "%s [%s]: errors %d | VS %d PS %d instructions | samplers %d | texture samples VS %d PS %d | VT %d | UV scalars %d interp %d" % (
        label, platform_name(), len(errors), stats.num_vertex_shader_instructions, stats.num_pixel_shader_instructions, stats.num_samplers,
        stats.num_vertex_texture_samples, stats.num_pixel_texture_samples, stats.num_virtual_texture_samples, stats.num_uv_scalars,
        stats.num_interpolator_scalars)
    log("STATS " + line)
    for error in errors:
        log("  COMPILE ERROR: %s" % error)
    return errors, line


def expressions_of(material):
    return list(mel.get_material_expressions(material))


def desc_of(expr):
    try:
        return str(expr.get_editor_property("desc") or "")
    except Exception:  # noqa: BLE001
        return ""


def param_name_of(expr):
    try:
        return str(expr.get_editor_property("parameter_name"))
    except Exception:  # noqa: BLE001
        return ""


def find_param(material, name):
    for expr in expressions_of(material):
        if param_name_of(expr) == name and not isinstance(expr, unreal.MaterialExpressionCollectionParameter):
            return expr
    raise PatchError("parameter %s not found in %s" % (name, material.get_path_name()))


def inputs_of(material, expr):
    return list(mel.get_inputs_for_material_expression(material, expr))


def find_consumer(material, cls, input_index, source):
    """The expression of class cls whose input #input_index is source."""
    for expr in expressions_of(material):
        if isinstance(expr, cls):
            ins = inputs_of(material, expr)
            if input_index < len(ins) and ins[input_index] == source:
                return expr
    return None


def set_mask(expr, channels):
    for c in ("r", "g", "b", "a"):
        expr.set_editor_property(c, c in channels)


def apply_fix(material):
    """The compile fix: scalar slope / ice / track masks. Idempotent (sets the mask bits)."""
    changes = []
    for expr in expressions_of(material):
        if not isinstance(expr, unreal.MaterialExpressionComponentMask) or desc_of(expr).startswith(TAG):
            continue
        ins = inputs_of(material, expr)
        source = ins[0] if ins else None
        before = "".join(c for c in "rgba" if expr.get_editor_property(c))
        if isinstance(source, unreal.MaterialExpressionPixelNormalWS):
            set_mask(expr, "b")
            changes.append("slope mask: PixelNormalWS.%s -> .b (normal.z; acos -> degrees -> SmoothStep RockSlopeMin/MaxAngle)" % before)
        elif isinstance(source, unreal.MaterialExpressionWorldPosition) and before in ("rgb", "b"):
            set_mask(expr, "b")
            changes.append("ice mask: WorldPosition.%s -> .b (world Z; SmoothStep IceMaxHeight -> IceMinHeight)" % before)
        elif isinstance(source, unreal.MaterialExpressionTextureSampleParameter2D) and param_name_of(source) == "RT_SnowTracks":
            set_mask(expr, "r")
            changes.append("old track sample: RT_SnowTracks.%s -> .r (scalar mask; was the float2 x float3 multiply)" % before)
    # The second compile error: the WPO track depth read the PixelNormalWS slope mask (invalid in the vertex shader). The landscape WPO
    # is removed from use (useless at landscape vertex density, invalidates VSM); the old 04_SnowTracks nodes (TrackCenterLocation /
    # TrackAreaSize / TrackIndentColor / TrackRoughnessDrop / TrackMaxWPODepth / RT_SnowTracks) stay in the graph.
    if mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        mel.disconnect_material_property(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        changes.append("World Position Offset disconnected (the TrackMaxWPODepth path read PixelNormalWS in the vertex shader)")
    return changes


def tag(expr, key):
    expr.set_editor_property("desc", TAG + key)
    return expr


def own(material, cls, key, x, y):
    return tag(new_expr(material, cls, x, y), key)


def function_call(material, mf, key, x, y):
    call = own(material, unreal.MaterialExpressionMaterialFunctionCall, key, x, y)
    call.set_editor_property("material_function", mf)
    return call


def mpc_names(mpc):
    scalars = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")]
    vectors = [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")]
    return scalars, vectors


def patch_master(powder_mf, surface_mf, interaction_mf, noise, rt, mpc):
    material = eal.load_asset(LANDSCAPE)
    owned = [e for e in expressions_of(material) if desc_of(e).startswith(TAG)]
    already = bool(owned)
    log("M_Landscape_Master: %s (%d own nodes)" % ("ALREADY PATCHED - rebuilding the own nodes" if already else "unpatched", len(owned)))
    for change in apply_fix(material):
        log("FIX " + change)
    result = {"fix": None}
    if not already:
        _errors, result["fix"] = compile_report(material, "after the fix (scalar masks, old track path on colour / roughness, WPO off)")
    for expr in owned:
        mel.delete_material_expression(material, expr)

    # The user's nodes the snow functions read (found by their parameters, not by position).
    snow_colour = find_param(material, "SnowBaseColor")
    snow_rough = find_param(material, "SnowRoughness")
    rock_min = find_param(material, "RockSlopeMinAngle")
    rock_max = find_param(material, "RockSlopeMaxAngle")
    layer_colour = find_consumer(material, unreal.MaterialExpressionLinearInterpolate, 1, find_param(material, "RockBaseColor"))
    layer_rough = find_consumer(material, unreal.MaterialExpressionLinearInterpolate, 1, find_param(material, "RockRoughness"))
    ice_mask = find_consumer(material, unreal.MaterialExpressionSmoothStep, 0, find_param(material, "IceMaxHeight"))
    if not (layer_colour and layer_rough and ice_mask):
        raise PatchError("base layer nodes not found (colour %s, roughness %s, ice %s)" % (layer_colour, layer_rough, ice_mask))

    scalars, vectors = mpc_names(mpc)
    x = 1000

    def mpc_param(name, y):
        node = own(material, unreal.MaterialExpressionCollectionParameter, "mpc_" + name, x, y)
        node.set_editor_property("collection", mpc)
        node.set_editor_property("parameter_name", name)
        return node

    amount = mpc_param("SnowAmount", -900)
    frost = mpc_param("FrostAmount", -800)
    # FreshSnow (rises while it snows, fades after; written by USnowInteractionSubsystem) when the MPC has it, else IsSnowing.
    fresh_name = "FreshSnow" if "FreshSnow" in scalars else "IsSnowing"
    snowing = mpc_param(fresh_name, -700)
    log("fresh snow input: MPC_Environment.%s" % fresh_name)
    if WINDOW_PARAM in vectors:
        window = mpc_param(WINDOW_PARAM, -600)
        window_state = "MPC_Environment.%s" % WINDOW_PARAM
    else:
        window = own(material, unreal.MaterialExpressionConstant4Vector, "window_pending", x, -600)
        window.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        window_state = "PENDING: MPC_Environment has no %s yet - a zero constant (window disabled) stands in; re-run the patch" % WINDOW_PARAM
    log("interaction window: " + window_state)
    world = own(material, unreal.MaterialExpressionWorldPosition, "world", x, -500)
    camera = own(material, unreal.MaterialExpressionCameraVectorWS, "camera", x, -400)
    vertex_normal = own(material, unreal.MaterialExpressionVertexNormalWS, "vertex_normal", x, -300)
    up = own(material, unreal.MaterialExpressionComponentMask, "up", x + 200, -300)
    set_mask(up, "b")
    link(vertex_normal, "", up, "")
    rock_vertex = tag(custom(material, ROCK_VERTEX_HLSL, "Rock mask (vertex normal)", [("N", vertex_normal, ""), ("MinAngle", rock_min, ""),
                                                                                       ("MaxAngle", rock_max, "")], F1, x + 200, -200), "rock_vertex")
    noise_param = own(material, unreal.MaterialExpressionTextureObjectParameter, "noise", x, 0)
    noise_param.set_editor_property("parameter_name", "SnowNoiseTexture")
    noise_param.set_editor_property("texture", noise)
    noise_param.set_editor_property("sampler_type", sampler_type_of(noise))
    noise_param.set_editor_property("group", "10_SnowTextures")
    rt_param = own(material, unreal.MaterialExpressionTextureObjectParameter, "rt", x, 150)
    rt_param.set_editor_property("parameter_name", "SnowInteractionRT")
    rt_param.set_editor_property("texture", rt)
    rt_param.set_editor_property("sampler_type", sampler_type_of(rt))
    rt_param.set_editor_property("group", "07_SnowInteraction")

    powder = function_call(material, powder_mf, "powder", x + 500, -700)
    for name, src, out in (("BaseColor", layer_colour, ""), ("Roughness", layer_rough, ""), ("SnowAmount", amount, ""), ("Up", up, ""),
                           ("RockMask", rock_vertex, ""), ("IceMask", ice_mask, ""), ("SnowColor", snow_colour, "RGB"), ("SnowRoughness", snow_rough, ""),
                           ("WorldPosition", world, ""), ("NoiseTex", noise_param, "")):
        link(src, out, powder, name)
    surface = function_call(material, surface_mf, "surface", x + 900, -600)
    for name, src, out in (("BaseColor", powder, "BaseColor"), ("Roughness", powder, "Roughness"), ("SnowWeight", powder, "SnowWeight"),
                           ("FreshSnow", snowing, ""), ("FrostAmount", frost, ""), ("WorldPosition", world, ""), ("CameraVector", camera, ""),
                           ("NoiseTex", noise_param, "")):
        link(src, out, surface, name)
    interaction = function_call(material, interaction_mf, "interaction", x + 1300, -500)
    for name, src, out in (("BaseColor", surface, "BaseColor"), ("Roughness", surface, "Roughness"), ("Normal", surface, "Normal"),
                           ("SnowWeight", powder, "SnowWeight"), ("WorldPosition", world, ""), ("CameraVector", camera, ""), ("Window", window, ""),
                           ("InteractionRT", rt_param, ""), ("NoiseTex", noise_param, "")):
        link(src, out, interaction, name)

    def to_output(src, name, prop):
        if not mel.connect_material_property(src, name, prop):
            raise PatchError("connect to %s failed" % prop)

    to_output(interaction, "BaseColor", unreal.MaterialProperty.MP_BASE_COLOR)
    to_output(interaction, "Roughness", unreal.MaterialProperty.MP_ROUGHNESS)
    to_output(interaction, "Normal", unreal.MaterialProperty.MP_NORMAL)
    to_output(interaction, "AmbientOcclusion", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    to_output(surface, "Specular", unreal.MaterialProperty.MP_SPECULAR)
    to_output(surface, "Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    log("outputs: BaseColor / Roughness / Normal / AO <- MF_SnowInteraction, Specular / Emissive <- MF_SnowSurface, WPO disconnected")
    errors, result["final"] = compile_report(material, "after the snow functions")
    if errors:
        raise PatchError("M_Landscape_Master does not compile after the patch: %s" % errors)
    log("saved %s: %s" % (LANDSCAPE, eal.save_asset(LANDSCAPE, only_if_is_dirty=False)))
    result["window"] = window_state
    return result


def patch_instance(rt_new_exists):
    mi = eal.load_asset(LANDSCAPE_MI)
    if rt_new_exists:
        rt_new = eal.load_asset(RT_NEW)
        mel.set_material_instance_texture_parameter_value(mi, "RT_SnowTracks", rt_new)
        log("MI: RT_SnowTracks override -> %s (the old RT_SnowTracks render target is no longer loaded through the MI)" % RT_NEW)
    else:
        log("MI: RT_SnowTracks override left on %s (PENDING: %s does not exist yet)" % (RT_OLD, RT_NEW))
    mel.update_material_instance(mi)
    _errors, line = compile_report(mi, "MI_Landscape_Master")
    log("saved %s: %s" % (LANDSCAPE_MI, eal.save_asset(LANDSCAPE_MI, only_if_is_dirty=False)))
    return line


def run_patch():
    noise = eal.load_asset(NOISE_TEX)
    rt_new_exists = eal.does_asset_exist(RT_NEW)
    rt = eal.load_asset(RT_NEW if rt_new_exists else RT_OLD)
    log("noise %s (sampler %s); interaction RT %s%s" % (NOISE_TEX, sampler_type_of(noise), rt.get_path_name(),
                                                        "" if rt_new_exists else " (PENDING: RT_SnowInteraction does not exist yet)"))
    mpc = eal.load_asset(MPC_PATH)
    scalars, vectors = mpc_names(mpc)
    log("MPC %s: scalars %s vectors %s" % (MPC_PATH, scalars, vectors))
    original = compile_report(eal.load_asset(LANDSCAPE), "before (as found)")
    powder_mf = build_powder(noise)
    surface_mf = build_surface(noise)
    interaction_mf = build_interaction(noise, rt)
    result = patch_master(powder_mf, surface_mf, interaction_mf, noise, rt, mpc)
    mi_line = patch_instance(rt_new_exists)
    log("SUMMARY before: %s" % original[1])
    log("SUMMARY after fix: %s" % (result["fix"] or "(graph was already patched - see %s)" % FIRST_LOG))
    log("SUMMARY after snow functions: %s" % result["final"])
    log("SUMMARY MI: %s" % mi_line)
    log("SUMMARY window: %s" % result["window"])
    log("function input previews: %s" % sorted(PREVIEW_NOTES))


def run_stats():
    for path in (LANDSCAPE, LANDSCAPE_MI):
        compile_report(eal.load_asset(path), path)
    material = eal.load_asset(LANDSCAPE)
    log("own nodes: %d; functions: %s" % (len([e for e in expressions_of(material) if desc_of(e).startswith(TAG)]),
                                          [p for p in (MF_POWDER, MF_SURFACE, MF_INTERACTION) if eal.does_asset_exist(p)]))
    log("scalars: %s" % [(str(n), round(mel.get_material_default_scalar_parameter_value(material, n), 4)) for n in mel.get_scalar_parameter_names(material)])
    log("vectors: %s" % [str(n) for n in mel.get_vector_parameter_names(material)])
    log("textures: %s" % [str(n) for n in mel.get_texture_parameter_names(material)])
    for prop in ("MP_BASE_COLOR", "MP_ROUGHNESS", "MP_NORMAL", "MP_AMBIENT_OCCLUSION", "MP_SPECULAR", "MP_EMISSIVE_COLOR", "MP_WORLD_POSITION_OFFSET"):
        node = mel.get_material_property_input_node(material, getattr(unreal.MaterialProperty, prop))
        log("output %s <- %s" % (prop, (node.get_class().get_name() + " " + desc_of(node)) if node else "none"))


if MODE == "dump":
    dump_material(LANDSCAPE)
    dump_instance(LANDSCAPE_MI)
    dump_material(FADE)
    mpc = eal.load_asset(MPC_PATH)
    log("==== %s scalars %s vectors %s" % (MPC_PATH, [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")],
                                          [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")]))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for path in (TRACK_MANAGER, FADE, LANDSCAPE_MI, LANDSCAPE, "/Game/VFX/SnowTracks/RT_SnowTracks"):
        refs = registry.get_referencers(path.rsplit(".", 1)[0], unreal.AssetRegistryDependencyOptions())
        log("referencers of %s: %s" % (path, [str(r) for r in refs] if refs else []))
    bp = eal.load_asset(TRACK_MANAGER)
    log("track manager: %s" % bp)
    try:
        gen = bp.generated_class()
        cdo = unreal.get_default_object(gen)
        log("  cdo components: %s" % [c.get_name() for c in cdo.get_components_by_class(unreal.ActorComponent)])
    except Exception as error:  # noqa: BLE001
        log("  cdo n/a: %s" % error)
    log("STATS landscape: %s" % stats_of(LANDSCAPE))
    log("STATS landscape MI: %s" % stats_of(LANDSCAPE_MI))
    log("STATS fade: %s" % stats_of(FADE))
    write_log("landscape_snow_dump.txt")

elif MODE == "patch":
    try:
        run_patch()
    except Exception as error:  # noqa: BLE001 - the log must say why
        import traceback
        log("PATCH FAILED: %s" % error)
        log(traceback.format_exc())
    write_log("landscape_snow_patch.txt")
    if not os.path.exists(os.path.join(unreal.Paths.project_saved_dir(), "Logs", FIRST_LOG)) and \
            any(line.startswith("SUMMARY after fix: after the fix") for line in log_lines):
        write_log(FIRST_LOG)
elif MODE == "stats":
    run_stats()
    write_log("landscape_snow_stats.txt")
