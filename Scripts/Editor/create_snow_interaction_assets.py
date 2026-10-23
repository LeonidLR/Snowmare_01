# Creates the assets of the interactive snow, phase 1 (docs/systems/environment-weather.md section 14, docs/design/INTERACTIVE_SNOW.md),
# all under /Game/Environment/Weather/Snow/Tracks/:
#   RT_SnowInteraction         render target RG16F (RGBA16F fallback), 2048^2, clamp, cleared black. R = compaction depth 0..~4,
#                              G = berm 0..~4. Readers: height (cm) = G * (1 - saturate(R)) * MaxBermCm - saturate(R) * MaxDepthCm,
#                              UV = (WorldXY - MPC_Environment.SnowTrackWindow.xy) * SnowTrackWindow.z (enabled = .w).
#                              KEPT when it exists (other materials reference it): only its format / size / addressing are fixed.
#   M_SnowStamp                unlit ADDITIVE canvas material: procedural stamp shapes in local UV, chosen by the vertex colour
#                              (R = shape id * 16 + 8 in 8 bits, G = depth / 4, B = berm / 4; SnowInteractionRules::EncodeStampColor).
#                              Shapes: 0 boot, 1 paw, 2 body capsule, 3 crater with a rim, 4 dent, 5 segment cross-section.
#   M_SnowInteraction_Fade     unlit MODULATE full-target pass: RT *= (FadeR, FadeG) (the refill by snowfall / time).
#   M_SnowTracks_Shift         unlit OPAQUE full-target copy: out = clamp(Source(uv + Offset).rg, 0, ClampMax), 0 outside [0, 1]
#                              (window scroll RT -> scratch -> RT, and the periodic clamp pass).
#   DA_SnowStamps              USnowStampSet (C++ class: run after the build); created once with the C++ defaults, KEPT when it exists
#                              (hand tuning survives).
# The three materials are this script's own: they are deleted and rebuilt on every run. Nothing outside the Tracks folder is touched.
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_snow_interaction_assets.txt
import os
import unreal

PATH = "/Game/Environment/Weather/Snow/Tracks"
RT_NAME = "RT_SnowInteraction"
STAMP = "M_SnowStamp"
FADE = "M_SnowInteraction_Fade"
SHIFT = "M_SnowTracks_Shift"
STAMPS_DA = "DA_SnowStamps"
RT_SIZE = 2048

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_snow_interaction_assets.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# --- Render target ----------------------------------------------------------------------------------------------------
rt_full = PATH + "/" + RT_NAME
if eal.does_asset_exist(rt_full):
    rt = eal.load_asset(rt_full)
    log("kept (exists): " + rt_full)
else:
    rt = tools.create_asset(RT_NAME, PATH, unreal.TextureRenderTarget2D, unreal.TextureRenderTargetFactoryNew())
    log("created " + rt_full)
fmt = None
for candidate in ("RTF_RG16F", "RTF_RG16f"):
    fmt = getattr(unreal.TextureRenderTargetFormat, candidate, None)
    if fmt is not None:
        break
if fmt is None:
    fmt = unreal.TextureRenderTargetFormat.RTF_RGBA16F
    log("RG16F not available from Python: RGBA16F fallback")
rt.set_editor_property("render_target_format", fmt)
rt.set_editor_property("size_x", RT_SIZE)
rt.set_editor_property("size_y", RT_SIZE)
rt.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
rt.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
rt.set_editor_property("clear_color", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
rt.set_editor_property("auto_generate_mips", False)
log("RT format %s size %dx%d saved=%s" % (rt.get_editor_property("render_target_format"), rt.get_editor_property("size_x"),
    rt.get_editor_property("size_y"), eal.save_loaded_asset(rt, only_if_is_dirty=False)))

# --- Materials (own: rebuilt) -------------------------------------------------------------------------------------------
for name in (STAMP, FADE, SHIFT):
    full = PATH + "/" + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
        log("replaced (own asset): " + full)


def new_material(name, blend):
    material = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", blend)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    return material


def custom(material, code, description, input_names, output_type, x, y):
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


def scalar(material, name, value, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def finish(material, name):
    mel.layout_material_expressions(material)
    errors = mel.recompile_material(material)
    log("%s compile errors: %s" % (name, list(errors) if errors else "none"))
    log("%s saved=%s" % (name, eal.save_asset(PATH + "/" + name, only_if_is_dirty=False)))


# Stamp: every shape is a signed distance in the print's own space q (the quad spans q = +-1/0.72, so a rim fits outside the
# print); compaction ramps in from the print edge, the berm is a bump just outside it.
STAMP_HLSL = r"""
float id = floor(VC.r * 255.0 / 16.0 + 0.01);
float depth = VC.g * 4.0;
float berm = VC.b * 4.0;
float2 q = (UV * 2.0 - 1.0) / 0.72;
float sdf = 1.0;
float bowl = 0.0;
if (id < 0.5)
{
    // Boot: toe and heel ellipses (x = along the walk).
    float toe = (length((q - float2(0.28, 0.0)) / float2(0.70, 0.95)) - 1.0) * 0.70;
    float heel = (length((q - float2(-0.55, 0.0)) / float2(0.45, 0.80)) - 1.0) * 0.45;
    sdf = min(toe, heel);
}
else if (id < 1.5)
{
    // Paw: a main pad and four toes.
    float pad = (length((q - float2(-0.30, 0.0)) / float2(0.48, 0.55)) - 1.0) * 0.48;
    float t1 = length(q - float2(0.42, 0.52)) - 0.24;
    float t2 = length(q - float2(0.42, -0.52)) - 0.24;
    float t3 = length(q - float2(0.66, 0.17)) - 0.24;
    float t4 = length(q - float2(0.66, -0.17)) - 0.24;
    sdf = min(min(pad, min(t1, t2)), min(t3, t4));
}
else if (id < 2.5)
{
    // Body: a rounded box (capsule-like) filling the print.
    float r = 0.45;
    float2 b = abs(q) - (1.0 - r);
    sdf = length(max(b, 0.0)) + min(max(b.x, b.y), 0.0) - r;
}
else if (id < 4.5)
{
    // Crater (3) / dent (4): a bowl, deepest in the middle.
    sdf = length(q) - 1.0;
    bowl = saturate(1.0 - dot(q, q));
}
else
{
    // Segment cross-section (trails / ruts / furrows): a rounded U across, constant along.
    sdf = abs(q.y) - 1.0;
    bowl = saturate(1.0 - q.y * q.y);
}
float compact = (id >= 2.5) ? bowl : saturate(-sdf / 0.22);
float rimWidth = (id > 2.5 && id < 3.5) ? 0.22 : 0.14;
float rim = saturate(1.0 - abs(sdf - rimWidth) / rimWidth);
rim = rim * rim * (3.0 - 2.0 * rim);
return float3(depth * compact, berm * rim, 0.0);
"""

stamp = new_material(STAMP, unreal.BlendMode.BLEND_ADDITIVE)
uv = mel.create_material_expression(stamp, unreal.MaterialExpressionTextureCoordinate, -900, 0)
vc = mel.create_material_expression(stamp, unreal.MaterialExpressionVertexColor, -900, 200)
shape = custom(stamp, STAMP_HLSL, "Stamp shapes (R depth, G berm)", ["UV", "VC"], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -500, 0)
mel.connect_material_expressions(uv, "", shape, "UV")
mel.connect_material_expressions(vc, "", shape, "VC")
mel.connect_material_property(shape, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(stamp, STAMP)

# Fade: modulate in place.
fade = new_material(FADE, unreal.BlendMode.BLEND_MODULATE)
fade_r = scalar(fade, "FadeR", 1.0, -700, 0)
fade_g = scalar(fade, "FadeG", 1.0, -700, 150)
fade_out = custom(fade, "return float3(saturate(R), saturate(G), 1.0);", "Fade factors", ["R", "G"], unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                  -400, 0)
mel.connect_material_expressions(fade_r, "", fade_out, "R")
mel.connect_material_expressions(fade_g, "", fade_out, "G")
mel.connect_material_property(fade_out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(fade, FADE)

# Shift: opaque copy with an offset; zero outside, clamped.
shift = new_material(SHIFT, unreal.BlendMode.BLEND_OPAQUE)
shift_uv = mel.create_material_expression(shift, unreal.MaterialExpressionTextureCoordinate, -1200, 0)
offset = mel.create_material_expression(shift, unreal.MaterialExpressionVectorParameter, -1200, 150)
offset.set_editor_property("parameter_name", "Offset")
offset.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
offset_rg = mel.create_material_expression(shift, unreal.MaterialExpressionComponentMask, -1000, 150)
offset_rg.set_editor_property("r", True)
offset_rg.set_editor_property("g", True)
offset_rg.set_editor_property("b", False)
offset_rg.set_editor_property("a", False)
mel.connect_material_expressions(offset, "", offset_rg, "")
shifted = mel.create_material_expression(shift, unreal.MaterialExpressionAdd, -850, 50)
mel.connect_material_expressions(shift_uv, "", shifted, "A")
mel.connect_material_expressions(offset_rg, "", shifted, "B")
source = mel.create_material_expression(shift, unreal.MaterialExpressionTextureSampleParameter2D, -650, 0)
source.set_editor_property("parameter_name", "Source")
source.set_editor_property("texture", rt)
source.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
source.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
mel.connect_material_expressions(shifted, "", source, "UVs")
clamp_max = scalar(shift, "ClampMax", 4.0, -650, 250)
shift_out = custom(shift, r"""
float inside = step(0.0, UV.x) * step(0.0, UV.y) * step(UV.x, 1.0) * step(UV.y, 1.0);
return float3(clamp(S.r, 0.0, ClampMax) * inside, clamp(S.g, 0.0, ClampMax) * inside, 0.0);
""", "Outside = 0, clamp", ["S", "UV", "ClampMax"], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -350, 0)
mel.connect_material_expressions(source, "RGBA", shift_out, "S")
mel.connect_material_expressions(shifted, "", shift_out, "UV")
mel.connect_material_expressions(clamp_max, "", shift_out, "ClampMax")
mel.connect_material_property(shift_out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(shift, SHIFT)

# --- Stamp set (needs the C++ class) -----------------------------------------------------------------------------------
da_full = PATH + "/" + STAMPS_DA
stamp_set_class = getattr(unreal, "SnowStampSet", None)
if stamp_set_class is None:
    log("USnowStampSet is not compiled in yet: %s skipped (run again after the build)" % da_full)
elif eal.does_asset_exist(da_full):
    log("kept (exists, hand tuning survives): " + da_full)
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", stamp_set_class)
    stamp_set = tools.create_asset(STAMPS_DA, PATH, stamp_set_class, factory)
    log("created %s saved=%s" % (da_full, eal.save_asset(da_full, only_if_is_dirty=False)))

write_log()
