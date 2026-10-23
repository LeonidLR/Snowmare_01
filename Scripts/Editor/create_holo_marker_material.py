# Creates /Game/UI/Materials/M_HoloMarker + MI_HoloMarker_Orange: the holographic ORANGE look of every tutorial marker (the
# "disc with a pole" of ATutorialMarkerActor: placed step / beat / role-spot markers and the runtime guide beacon of the current
# navigation target). Game designer play-test 2026-10-23: "all the markers - those discs with a pole stuck in them - must be
# HOLOGRAPHIC, ORANGE".
#
# Look: unlit, translucent, no fog, exposure-invariant emissive (EyeAdaptationInverse: the same on screen in the Clear noon and the
# snowy night) - orange core, a bright rim (the disc's outer edge, a fresnel rim on the pole), thin scanlines rising up the pole and
# rings running out over the disc, a gentle pulse and a soft flicker, depth fade where the disc meets the ground. The disc / pole
# switch is custom primitive data 0 ("IsDisc", set by ATutorialMarkerActor::ApplyMaterial). Translucent never casts shadows.
#
# Run (editor CLOSED for this checkout, under the build lock):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_holo_marker_material.txt (with the shader instruction counts)
import os
import unreal

PATH = "/Game/UI/Materials"
NAME = "M_HoloMarker"
INSTANCE = "MI_HoloMarker_Orange"
# sRGB (255, 122, 16) - a clear orange (the old placeholder amber was sRGB (255, 194, 79), close to yellow on snow).
ORANGE = unreal.LinearColor(1.0, 0.195, 0.005, 1.0)
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
if not eal.does_directory_exist(PATH):
    eal.make_directory(PATH)
for asset in (PATH + "/" + INSTANCE, PATH + "/" + NAME):
    if eal.does_asset_exist(asset):
        eal.delete_asset(asset)

material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", False)
material.set_editor_property("used_with_static_lighting", False)
material.set_editor_property("use_translucency_vertex_fog", False)  # "Apply Fogging" off: the snowfall fog never swallows a marker
try:
    material.set_editor_property("enable_responsive_aa", True)  # TSR / TAA: no smearing of the thin lines
except Exception as exc:  # property name differs between versions: not essential
    log_lines.append("note: enable_responsive_aa not set (%s)" % exc)


def link(src, out, dst, inp):
    ok = mel.connect_material_expressions(src, out, dst, inp)
    if not ok:
        log_lines.append("LINK FAILED: %s.%s -> %s.%s" % (src.get_class().get_name(), out, dst.get_class().get_name(), inp))
    return ok


def link_any(src, dst, names):
    """The pin names of some expressions are their property names: try each spelling, quietly, then report."""
    for name in names:
        if mel.connect_material_expressions(src, "", dst, name):
            log_lines.append("linked %s.%s" % (dst.get_class().get_name(), name))
            return True
    log_lines.append("LINK FAILED: -> %s (%s)" % (dst.get_class().get_name(), ", ".join(names)))
    return False


def scalar(name, value, x, y, group="Holo"):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


# --- inputs ---------------------------------------------------------------------------------------------------------------
color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -400)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", ORANGE)
color.set_editor_property("group", "Holo")

is_disc = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1500, 100)
is_disc.set_editor_property("parameter_name", "IsDisc")
is_disc.set_editor_property("default_value", 0.0)
is_disc.set_editor_property("use_custom_primitive_data", True)
is_disc.set_editor_property("primitive_data_index", 0)

world_pos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1900, -200)
local_pos = mel.create_material_expression(material, unreal.MaterialExpressionTransformPosition, -1650, -200)
local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
link(world_pos, "", local_pos, "")

time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -1900, 300)

# Pole scanlines in WORLD units (the same line spacing on every pole height): frac(z / spacing - t * speed) - Frac takes the large
# world coordinate down to a float before the Custom node.
z_mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1650, 0)
for channel, on in (("r", False), ("g", False), ("b", True), ("a", False)):
    z_mask.set_editor_property(channel, on)
link(world_pos, "", z_mask, "")
spacing = scalar("ScanSpacingCm", 18.0, -1650, 120)
z_div = mel.create_material_expression(material, unreal.MaterialExpressionDivide, -1450, 0)
link(z_mask, "", z_div, "A")
link(spacing, "", z_div, "B")
scan_speed = scalar("ScanSpeed", 0.6, -1650, 380)
t_scan = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -1450, 300)
link(time, "", t_scan, "A")
link(scan_speed, "", t_scan, "B")
z_sub = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -1250, 0)
link(z_div, "", z_sub, "A")
link(t_scan, "", z_sub, "B")
pole_scan = mel.create_material_expression(material, unreal.MaterialExpressionFrac, -1100, 0)
link(z_sub, "", pole_scan, "")

fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -1250, 200)
fresnel.set_editor_property("exponent", 2.0)
fresnel.set_editor_property("base_reflect_fraction", 0.0)

# --- the hologram mask (one Custom node: rim, scan rings / lines, pulse, flicker) -> (emissive weight, opacity) ----------------
custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -800, 0)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
custom.set_editor_property("description", "HoloMarkerMask")
code = """
float r = saturate(length(LocalPos.xy) / 50.0);
// Disc: a bright outer rim, rings running outwards, a faint fill.
float discRim = smoothstep(0.80, 0.94, r) * (1.0 - smoothstep(0.975, 1.0, r));
float rings = smoothstep(0.82, 1.0, frac(r * RingCount - Time * ScanSpeed)) * (1.0 - smoothstep(0.80, 0.92, r));
float disc = DiscFill + discRim * RimBoost + rings * ScanBoost;
// Pole: a soft core, the fresnel rim, scanlines rising, fading towards the top.
float h = saturate(LocalPos.z / 100.0 + 0.5);
float lines = smoothstep(0.72, 1.0, PoleScan);
float pole = (PoleFill + Fres * RimBoost + lines * ScanBoost) * (1.0 - 0.55 * h);
float m = lerp(pole, disc, IsDisc);
float pulse = 1.0 - PulseAmount * (0.5 + 0.5 * sin(Time * PulseSpeed));
float flick = 1.0 - FlickerAmount * saturate(sin(Time * 23.0) * sin(Time * 7.3 + 1.7) * 2.5 - 1.2);
float e = m * pulse * flick;
return float2(e, saturate(e * OpacityScale));
"""
custom.set_editor_property("code", code)
input_names = ["LocalPos", "PoleScan", "Fres", "IsDisc", "Time", "RingCount", "ScanSpeed", "DiscFill", "PoleFill", "RimBoost", "ScanBoost",
               "PulseAmount", "PulseSpeed", "FlickerAmount", "OpacityScale"]
inputs = []
for input_name in input_names:
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", input_name)
    inputs.append(custom_input)
custom.set_editor_property("inputs", inputs)

params = {
    "RingCount": scalar("RingCount", 3.0, -1100, 420),
    "DiscFill": scalar("DiscFill", 0.3, -1100, 520),
    "PoleFill": scalar("PoleFill", 0.6, -1100, 620),
    "RimBoost": scalar("RimBoost", 1.0, -1100, 720),
    "ScanBoost": scalar("ScanBoost", 0.75, -1100, 820),
    "PulseAmount": scalar("PulseAmount", 0.15, -1100, 920),
    "PulseSpeed": scalar("PulseSpeed", 2.6, -1100, 1020),
    "FlickerAmount": scalar("FlickerAmount", 0.2, -1100, 1120),
    "OpacityScale": scalar("OpacityScale", 1.6, -1100, 1220),
}
link(local_pos, "", custom, "LocalPos")
link(pole_scan, "", custom, "PoleScan")
link(fresnel, "", custom, "Fres")
link(is_disc, "", custom, "IsDisc")
link(time, "", custom, "Time")
link(scan_speed, "", custom, "ScanSpeed")
for name, node in params.items():
    link(node, "", custom, name)

mask_e = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -550, -100)
mask_e.set_editor_property("r", True)
mask_e.set_editor_property("g", False)
link(custom, "", mask_e, "")
mask_o = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -550, 150)
mask_o.set_editor_property("r", False)
mask_o.set_editor_property("g", True)
link(custom, "", mask_o, "")

# --- emissive = Color x Intensity x mask, exposure-invariant --------------------------------------------------------------
intensity = scalar("EmissiveIntensity", 1.1, -900, -250)
# The weather presenter grades the night / snowfall down to 45-70 % saturation, which turns the orange peach (more saturation
# cannot be put back - the grade pulls blue up to the luma, a resaturated orange came out PINK). Under a desaturating grade the
# colour loses its blue (R, 0.28 R, 0) and gets dimmer (no tonemapper highlight wash-out): after the grade it reads ORANGE again. "SaturationCompensation" =
# 1 / the grade's saturation (1 by day, up to 2.5), set by ATutorialMarkerActor while it is shown.
sat_comp = scalar("SaturationCompensation", 1.0, -1100, -500)
resat = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -800, -450)
resat.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
resat.set_editor_property("description", "HoloMarkerResaturate")
resat.set_editor_property("code", "float t = saturate((S - 1.0) / 1.2); return lerp(C, float3(C.r, C.r * 0.28, 0.0), t) * (1.0 - 0.28 * t);")
resat_inputs = []
for input_name in ("C", "S"):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", input_name)
    resat_inputs.append(custom_input)
resat.set_editor_property("inputs", resat_inputs)
link(color, "RGB", resat, "C")
link(sat_comp, "", resat, "S")
tint = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, -350)
link(resat, "", tint, "A")
link(intensity, "", tint, "B")
emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -250)
link(tint, "", emissive, "A")
link(mask_e, "", emissive, "B")
invariance = scalar("ExposureInvariance", 1.0, -400, -100)
eye = mel.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse, -200, -250)
link_any(emissive, eye, ("LightValue", "LightValueInput", "Light Value Input", "Light Value"))
link_any(invariance, eye, ("Alpha", "AlphaInput", "Alpha Input"))
mel.connect_material_property(eye, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# --- opacity, faded where the marker meets the ground (no hard intersection line in the snow) -----------------------------
# The disc lies 2-5 cm over the ground: a short fade only softens where snow bumps poke through it (a long one would erase it);
# the pole fades out over its foot.
pole_fade = scalar("PoleDepthFadeCm", 25.0, -650, 300)
disc_fade = scalar("DiscDepthFadeCm", 3.0, -650, 400)
fade = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -400, 300)
link(pole_fade, "", fade, "A")
link(disc_fade, "", fade, "B")
link(is_disc, "", fade, "Alpha")
depth_fade = mel.create_material_expression(material, unreal.MaterialExpressionDepthFade, -200, 150)
link(mask_o, "", depth_fade, "Opacity")
link(fade, "", depth_fade, "FadeDistance")
mel.connect_material_property(depth_fade, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(material)
saved = eal.save_asset(PATH + "/" + NAME)
log_lines.append("created %s/%s saved=%s" % (PATH, NAME, saved))
try:
    stats = mel.get_statistics(material)
    log_lines.append("stats: pixel shader instructions %s, vertex shader instructions %s, samplers %s" % (
        stats.num_pixel_shader_instructions, stats.num_vertex_shader_instructions, stats.num_samplers))
except Exception as exc:
    log_lines.append("stats unavailable: %s" % exc)

instance = tools.create_asset(INSTANCE, PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
instance.set_editor_property("parent", material)
mel.set_material_instance_vector_parameter_value(instance, "Color", ORANGE)
mel.update_material_instance(instance)
saved = eal.save_asset(PATH + "/" + INSTANCE)
log_lines.append("created %s/%s saved=%s" % (PATH, INSTANCE, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_holo_marker_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
