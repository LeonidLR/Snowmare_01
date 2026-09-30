# Creates /Game/VFX/Materials/M_FrostVignette: the full-screen frost vignette that grows with the squad's cold
# (Godot Shaders/frost_vignette.gdshader + resources/frost_vignette_material.tres, drawn over the whole UI by the
# UI/FrostOverlay ColorRect). The Godot fragment code is kept as one Custom (HLSL) node; the HUD draws the material
# over the screen and sets the scalar parameter "ColdPct" (0..100, the coldest operative).
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_frost_vignette_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_FrostVignette"
log_lines = []

HLSL = r"""
float2 p = ScreenUV * 2.0 - 1.0;
float dist = length(p);
float coldFactor = saturate((ColdPct - 35.0) / 65.0);
if (coldFactor <= 0.001)
{
    return float4(0, 0, 0, 0);
}
float angle = atan2(p.y, p.x);
float2 hp = float2(angle * 12.0, floor(dist * 20.0));
float3 p3 = frac(float3(hp.x, hp.y, hp.x) * 0.1031);
p3 += dot(p3, p3.yzx + 33.33);
float jagged = frac((p3.x + p3.y) * p3.z);
float edge = pow(dist, VignettePower) + (jagged - 0.5) * 0.15 * coldFactor;
float alpha = smoothstep(0.65 - coldFactor * 0.3, 1.2, edge) * coldFactor;
if (ColdPct > 80.0)
{
    alpha += sin(Seconds * 4.0) * 0.08 * ((ColdPct - 80.0) / 20.0);
}
// x = edge (colour mix), y = alpha; the colour and the opacity clamp are material nodes below.
return float4(edge, alpha, 0, 0);
"""

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)


def scalar(name, value, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1100, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def color(name, value, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1100, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


uv = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1100, -300)
cold = scalar("ColdPct", 0.0, -150)
power = scalar("VignettePower", 2.8, 0)
time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -1100, 150)

custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -700, 0)
custom.set_editor_property("code", HLSL)
custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
custom.set_editor_property("description", "Godot frost_vignette")
inputs = []
for input_name in ("ScreenUV", "ColdPct", "VignettePower", "Seconds"):
    item = unreal.CustomInput()
    item.set_editor_property("input_name", input_name)
    inputs.append(item)
custom.set_editor_property("inputs", inputs)
mel.connect_material_expressions(uv, "", custom, "ScreenUV")
mel.connect_material_expressions(cold, "", custom, "ColdPct")
mel.connect_material_expressions(power, "", custom, "VignettePower")
mel.connect_material_expressions(time, "", custom, "Seconds")

edge = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, -100)
edge.set_editor_property("r", True)
mel.connect_material_expressions(custom, "", edge, "")
edge_mix = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, -100)
edge_mix.set_editor_property("const_b", 0.6)
mel.connect_material_expressions(edge, "", edge_mix, "A")
# Godot frost_color (0.55, 0.82, 1.0) / deep_freeze_color (0.1, 0.35, 0.65) are sRGB: linear constants here.
frost = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -300)
frost.set_editor_property("constant", unreal.LinearColor(0.2623, 0.6376, 1.0, 1.0))
deep = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -450, -220)
deep.set_editor_property("constant", unreal.LinearColor(0.0100, 0.1005, 0.3736, 1.0))
mix = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -150, -200)
mel.connect_material_expressions(frost, "", mix, "A")
mel.connect_material_expressions(deep, "", mix, "B")
mel.connect_material_expressions(edge_mix, "", mix, "Alpha")
mel.connect_material_property(mix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
alpha = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -450, 100)
alpha.set_editor_property("g", True)
mel.connect_material_expressions(custom, "", alpha, "")
alpha_scaled = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 100)
alpha_scaled.set_editor_property("const_b", 0.85)
mel.connect_material_expressions(alpha, "", alpha_scaled, "A")
opacity = mel.create_material_expression(material, unreal.MaterialExpressionClamp, -150, 100)
opacity.set_editor_property("min_default", 0.0)
opacity.set_editor_property("max_default", 0.95)
mel.connect_material_expressions(alpha_scaled, "", opacity, "")
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
log_lines.append("created %s saved=%s" % (full, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_frost_vignette_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
