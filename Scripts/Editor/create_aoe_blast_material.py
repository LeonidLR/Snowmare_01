# Creates /Game/VFX/Materials/M_AoeBlast: the grenade blast zone at the aim point — a pulsing translucent fill and the
# epicentre crosshair (Godot Scenes/movements/aoe_blast_ring.gdshader, ring_color (1, 0.3, 0.15, 0.9),
# fill_color (1, 0.18, 0.05, 0.22)). The outline ring itself stays the glowing segments of AGrenadeAimActor.
# Drawn on the 100 x 100 cm engine plane scaled so UV 0.5 = the Godot plane edge (blast radius x 1.025).
# The Godot math runs in a Custom node that returns scalars only (x = alpha, y = crosshair); colours are nodes.
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_aoe_blast_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_AoeBlast"

HLSL = r"""
float2 d = PlaneUV - 0.5;
float dist = length(d);
if (dist > 0.5)
{
    return float4(0, 0, 0, 0);
}
float pulse = 0.90 + 0.10 * sin(Seconds * 5.0);
float inside = 1.0 - smoothstep(0.475 - 0.015, 0.475, dist);
float centerDot = 1.0 - smoothstep(0.015, 0.03, dist);
float crossX = (1.0 - smoothstep(0.005, 0.01, abs(d.x))) * (1.0 - smoothstep(0.06, 0.10, abs(d.y)));
float crossY = (1.0 - smoothstep(0.005, 0.01, abs(d.y))) * (1.0 - smoothstep(0.06, 0.10, abs(d.x)));
float crosshair = saturate(crossX + crossY + centerDot);
float alpha = inside * 0.22 * (1.0 - dist * 1.1) * pulse + crosshair * 0.9;
return float4(saturate(alpha), crosshair, 0, 0);
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

uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, -100)
time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -1000, 50)
custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -700, 0)
custom.set_editor_property("code", HLSL)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
custom.set_editor_property("description", "Godot aoe_blast_ring (fill + crosshair)")
inputs = []
for input_name in ("PlaneUV", "Seconds"):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", input_name)
    inputs.append(item)
custom.set_editor_property("inputs", inputs)
mel.connect_material_expressions(uv, "", custom, "PlaneUV")
mel.connect_material_expressions(time, "", custom, "Seconds")

alpha = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, 100)
alpha.set_editor_property("r", True)
mel.connect_material_expressions(custom, "", alpha, "")
mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

cross = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, -50)
cross.set_editor_property("g", True)
mel.connect_material_expressions(custom, "", cross, "")
# sRGB (1, 0.18, 0.05) and (1, 0.3, 0.15) as linear constants.
fill = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -250)
fill.set_editor_property("constant", unreal.LinearColor(1.0, 0.0273, 0.0040, 1.0))
ring = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -170)
ring.set_editor_property("constant", unreal.LinearColor(1.0, 0.0732, 0.0194, 1.0))
mix = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -200, -200)
mel.connect_material_expressions(fill, "", mix, "A")
mel.connect_material_expressions(ring, "", mix, "B")
mel.connect_material_expressions(cross, "", mix, "Alpha")
mel.connect_material_property(mix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_aoe_blast_material.txt"), "w", encoding="utf-8") as f:
    f.write("created %s saved=%s\n" % (full, saved))
