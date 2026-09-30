# Creates /Game/VFX/Materials/M_WeaponMatrixDots: the red-orange dot matrix of the cells the active weapon can reach in
# the turn-based attack mode (Godot Shaders/tactical_weapon_matrix_dots.gdshader: 8 dots per cell of radius 0.24, a
# thin 0.035 cell frame, dot_color (1, 0.28, 0.06, 0.95), border_color (0.85, 0.2, 0.05, 0.35)).
# The distance falloff (Godot vertex COLOR.a: 1 at the first cell down to 0.25 at the weapon's range) comes from the
# instanced mesh's per-instance custom data 0 (ATurnGridOverlayActor::SetAttackCells).
# The Godot math runs in a Custom node returning scalars (x = alpha, y = frame mix); colours are nodes.
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_weapon_matrix_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_WeaponMatrixDots"

HLSL = r"""
float bx = min(CellUV.x, 1.0 - CellUV.x);
float by = min(CellUV.y, 1.0 - CellUV.y);
float isBorder = 1.0 - step(0.035, min(bx, by));
float2 dots = frac(CellUV * 8.0) - 0.5;
float dotMask = 1.0 - smoothstep(0.24 * 0.65, 0.24, length(dots));
float alpha = max(dotMask * 0.95, isBorder * 0.35) * Falloff;
if (alpha < 0.015)
{
    alpha = 0.0;
}
return float4(saturate(alpha), isBorder * (1.0 - dotMask), 0, 0);
"""

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)
material.set_editor_property("used_with_instanced_static_meshes", True)

uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, -100)
falloff = mel.create_material_expression(material, unreal.MaterialExpressionPerInstanceCustomData, -1000, 50)
falloff.set_editor_property("data_index", 0)
falloff.set_editor_property("const_default_value", 1.0)
custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -700, 0)
custom.set_editor_property("code", HLSL)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
custom.set_editor_property("description", "Godot tactical_weapon_matrix_dots")
inputs = []
for input_name in ("CellUV", "Falloff"):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", input_name)
    inputs.append(item)
custom.set_editor_property("inputs", inputs)
mel.connect_material_expressions(uv, "", custom, "CellUV")
mel.connect_material_expressions(falloff, "", custom, "Falloff")

alpha = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, 100)
alpha.set_editor_property("r", True)
mel.connect_material_expressions(custom, "", alpha, "")
mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
frame = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, -50)
frame.set_editor_property("g", True)
mel.connect_material_expressions(custom, "", frame, "")
# sRGB (1, 0.28, 0.06) and (0.85, 0.2, 0.05) as linear constants.
dot = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -250)
dot.set_editor_property("constant", unreal.LinearColor(1.0, 0.0636, 0.0060, 1.0))
border = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -170)
border.set_editor_property("constant", unreal.LinearColor(0.6939, 0.0331, 0.0040, 1.0))
mix = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -200, -200)
mel.connect_material_expressions(dot, "", mix, "A")
mel.connect_material_expressions(border, "", mix, "B")
mel.connect_material_expressions(frame, "", mix, "Alpha")
mel.connect_material_property(mix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_weapon_matrix_material.txt"), "w", encoding="utf-8") as f:
    f.write("created %s saved=%s\n" % (full, saved))
