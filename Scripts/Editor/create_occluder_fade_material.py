# Creates /Game/UI/Materials/M_OccluderFade: the see-through look of a wall that hides the cursor's target (the game designer's play-test
# 2026-11-01 on L_FormationLab, approved fix c: "a wall that hides the cursor's target fades, like the roof cutaway"; UOccluderFadeSubsystem).
#
# Look: Masked (no translucency sorting, cheap: the depth / velocity passes as any masked mesh), Default Lit - a cool grey wall with a soft
# fresnel rim so its outline still reads, its opacity mask = DitherTemporalAA(Opacity): TSR / TAA blends the dither into a smooth fade.
# Parameters: Opacity (1 = solid; the subsystem eases it 1 -> 0.45 -> 1), Tint, RimColor, RimStrength. The proxy mesh carrying it casts no
# shadow (the hidden original keeps casting its own).
#
# Run (editor CLOSED for this checkout, under the build lock):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_occluder_fade_material.txt (with the shader instruction counts)
import os
import unreal

PATH = "/Game/UI/Materials"
NAME = "M_OccluderFade"
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
if not eal.does_directory_exist(PATH):
    eal.make_directory(PATH)
if eal.does_asset_exist(PATH + "/" + NAME):
    eal.delete_asset(PATH + "/" + NAME)

material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
material.set_editor_property("two_sided", False)
material.set_editor_property("opacity_mask_clip_value", 0.5)


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


def scalar(name, value, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", "OccluderFade")
    return node


def vector(name, value, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", "OccluderFade")
    return node


# Base colour: the tint (a cool light grey: reads as "the wall, see-through" on snow and on the dark lab floor).
tint = vector("Tint", unreal.LinearColor(0.38, 0.42, 0.48, 1.0), -700, -300)
mel.connect_material_property(tint, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
roughness = scalar("Roughness", 0.8, -700, -150)
mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)

# A soft fresnel rim (emissive) keeps the wall's outline readable while it is mostly dithered away.
fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -900, 0)
fresnel.set_editor_property("exponent", 3.0)
fresnel.set_editor_property("base_reflect_fraction", 0.0)
rim_color = vector("RimColor", unreal.LinearColor(0.55, 0.75, 1.0, 1.0), -900, 150)
rim_strength = scalar("RimStrength", 0.35, -900, 300)
rim = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, 50)
link(fresnel, "", rim, "A")
link(rim_color, "RGB", rim, "B")
rim_scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 100)
link(rim, "", rim_scaled, "A")
link(rim_strength, "", rim_scaled, "B")
mel.connect_material_property(rim_scaled, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# Opacity mask: the dithered opacity (TSR / TAA resolve it into a smooth fade; no sorting, no overdraw of translucency).
opacity = scalar("Opacity", 1.0, -700, 450)
# DitherTemporalAA is an engine material function in UE 5 (no expression class): a function call node.
dither_function = unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA")
if dither_function:
    dither = mel.create_material_expression(material, unreal.MaterialExpressionMaterialFunctionCall, -450, 450)
    dither.set_editor_property("material_function", dither_function)
    link_any(opacity, dither, ("Alpha Threshold", "AlphaThreshold", "Alpha"))
    mel.connect_material_property(dither, "", unreal.MaterialProperty.MP_OPACITY_MASK)
else:
    log_lines.append("WARNING: DitherTemporalAA not found - the opacity drives the mask directly (a hard cut)")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)

mel.recompile_material(material)
saved = eal.save_asset(PATH + "/" + NAME)
log_lines.append("created %s/%s saved=%s" % (PATH, NAME, saved))
try:
    stats = mel.get_statistics(material)
    log_lines.append("stats: pixel shader instructions %s, vertex shader instructions %s, samplers %s" % (
        stats.num_pixel_shader_instructions, stats.num_vertex_shader_instructions, stats.num_samplers))
except Exception as exc:
    log_lines.append("stats unavailable: %s" % exc)

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_occluder_fade_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
