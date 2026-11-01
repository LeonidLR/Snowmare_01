# Creates /Game/UI/Materials/M_FormationGhost: the hologram of the custom formation's placement ghosts (AFormationGhostActor; the game
# designer's PIE 2026-11-01, bug_05: "the ghosts are pale" - they wore the see-through silhouette M_Silhouette, whose core is 0.2 opacity,
# because M_HoloMarker has no skeletal-mesh usage). A dedicated material instead of changing the markers' M_HoloMarker (its disc / pole mask
# is made for the marker mesh).
#
# Look: unlit, translucent, depth-tested (the see-through outline behind a wall is the overlay M_Silhouette), no fog, exposure-invariant
# emissive (EyeAdaptationInverse, as M_HoloMarker: the same on screen by day and at night) - a solid hologram core, a bright fresnel rim (the
# body's outline), thin scanlines rising up the body, a soft flicker. Parameters set by the actor: "Color" (orange / selected yellow /
# refused red), "EmissiveIntensity". Used with skeletal meshes.
#
# Run (editor CLOSED for this checkout, under the build lock):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_formation_ghost_material.txt (with the shader instruction counts)
import os
import unreal

PATH = "/Game/UI/Materials"
NAME = "M_FormationGhost"
# The tutorial markers' hologram orange (M_HoloMarker ORANGE).
ORANGE = unreal.LinearColor(1.0, 0.38, 0.04, 1.0)
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
if not eal.does_directory_exist(PATH):
    eal.make_directory(PATH)
full = PATH + "/" + NAME
if eal.does_asset_exist(full):
    eal.delete_asset(full)

material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", False)
material.set_editor_property("used_with_skeletal_mesh", True)
material.set_editor_property("used_with_static_lighting", False)
material.set_editor_property("use_translucency_vertex_fog", False)  # "Apply Fogging" off: the snowfall fog never swallows a ghost
try:
    material.set_editor_property("enable_responsive_aa", True)  # TSR / TAA: no smearing of the scanlines
except Exception as exc:
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


def scalar(name, value, x, y, group="Ghost"):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


# --- inputs ---------------------------------------------------------------------------------------------------------------
color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -400)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", ORANGE)
color.set_editor_property("group", "Ghost")

world_pos = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1900, -100)
time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -1900, 300)

# Scanlines in WORLD units rising up the body: frac(z / spacing - t * speed) (Frac takes the large world coordinate down first).
z_mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1650, 0)
for channel, on in (("r", False), ("g", False), ("b", True), ("a", False)):
    z_mask.set_editor_property(channel, on)
link(world_pos, "", z_mask, "")
spacing = scalar("ScanSpacingCm", 7.0, -1650, 120)
z_div = mel.create_material_expression(material, unreal.MaterialExpressionDivide, -1450, 0)
link(z_mask, "", z_div, "A")
link(spacing, "", z_div, "B")
scan_speed = scalar("ScanSpeed", 0.8, -1650, 380)
t_scan = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -1450, 300)
link(time, "", t_scan, "A")
link(scan_speed, "", t_scan, "B")
z_sub = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -1250, 0)
link(z_div, "", z_sub, "A")
link(t_scan, "", z_sub, "B")
scan = mel.create_material_expression(material, unreal.MaterialExpressionFrac, -1100, 0)
link(z_sub, "", scan, "")

fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -1250, 200)
fresnel.set_editor_property("exponent", 2.2)
fresnel.set_editor_property("base_reflect_fraction", 0.0)

# --- the hologram mask (one Custom node) -> (emissive weight, opacity) ----------------------------------------------------
custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -800, 0)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
custom.set_editor_property("description", "FormationGhostMask")
code = """
float lines = smoothstep(0.78, 1.0, Scan);
float rim = saturate(Fres);
float flick = 1.0 - FlickerAmount * saturate(sin(Time * 19.0) * sin(Time * 6.1 + 1.3) * 2.5 - 1.2);
float e = (Fill + rim * RimBoost + lines * ScanBoost) * flick;
float o = saturate(OpacityFill + rim * RimOpacity + lines * ScanOpacity);
return float2(e, o);
"""
custom.set_editor_property("code", code)
input_names = ["Scan", "Fres", "Time", "Fill", "RimBoost", "ScanBoost", "FlickerAmount", "OpacityFill", "RimOpacity", "ScanOpacity"]
inputs = []
for input_name in input_names:
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property("input_name", input_name)
    inputs.append(custom_input)
custom.set_editor_property("inputs", inputs)

params = {
    "Fill": scalar("Fill", 0.55, -1100, 420),
    "RimBoost": scalar("RimBoost", 2.4, -1100, 520),
    "ScanBoost": scalar("ScanBoost", 0.45, -1100, 620),
    "FlickerAmount": scalar("FlickerAmount", 0.12, -1100, 720),
    "OpacityFill": scalar("OpacityFill", 0.5, -1100, 820),
    "RimOpacity": scalar("RimOpacity", 0.9, -1100, 920),
    "ScanOpacity": scalar("ScanOpacity", 0.2, -1100, 1020),
}
link(scan, "", custom, "Scan")
link(fresnel, "", custom, "Fres")
link(time, "", custom, "Time")
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

# --- emissive = Color x EmissiveIntensity x mask, exposure-invariant -------------------------------------------------------
intensity = scalar("EmissiveIntensity", 1.6, -900, -250)
tint = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, -350)
link(color, "RGB", tint, "A")
link(intensity, "", tint, "B")
emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -250)
link(tint, "", emissive, "A")
link(mask_e, "", emissive, "B")
invariance = scalar("ExposureInvariance", 1.0, -400, -100)
eye = mel.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse, -200, -250)
link_any(emissive, eye, ("LightValue", "LightValueInput", "Light Value Input", "Light Value"))
link_any(invariance, eye, ("Alpha", "AlphaInput", "Alpha Input"))
mel.connect_material_property(eye, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(mask_o, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(material)
saved = eal.save_asset(full)
log_lines.append("created %s saved=%s used_with_skeletal_mesh=%s" % (full, saved, material.get_editor_property("used_with_skeletal_mesh")))
try:
    stats = mel.get_statistics(material)
    log_lines.append("stats: pixel shader instructions %s, vertex shader instructions %s, samplers %s" % (
        stats.num_pixel_shader_instructions, stats.num_vertex_shader_instructions, stats.num_samplers))
except Exception as exc:
    log_lines.append("stats unavailable: %s" % exc)

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_formation_ghost_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
