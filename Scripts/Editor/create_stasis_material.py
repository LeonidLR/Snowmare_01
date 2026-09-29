# Creates /Game/VFX/Materials/M_TacticalStasis: the dark translucent look of enemies left outside a turn-based fight
# (Godot Shaders/tactical_stasis_enemy.gdshader: albedo = mix(dark (0.06, 0.08, 0.12) * 0.65, edge (0.20, 0.35, 0.55),
# fresnel * 0.45), alpha = clamp(0.40 + fresnel * 0.25, 0.20, 0.60), fresnel power 2.2, roughness 0.85, metallic 0.1).
# Used by UTurnBasedCombatSubsystem (C++) as an override material on every mesh of those enemies.
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_stasis_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_TacticalStasis"
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
# Enemies are skeletal meshes (Blueprints) or the static placeholder body.
material.set_editor_property("used_with_skeletal_mesh", True)

fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -900, 0)
fresnel.set_editor_property("exponent", 2.2)
fresnel.set_editor_property("base_reflect_fraction", 0.0)

dark = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -900, -250)
dark.set_editor_property("constant", unreal.LinearColor(0.06 * 0.65, 0.08 * 0.65, 0.12 * 0.65, 1.0))
edge = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -900, -150)
edge.set_editor_property("constant", unreal.LinearColor(0.20, 0.35, 0.55, 1.0))
fresnel_045 = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, 0)
fresnel_045.set_editor_property("const_b", 0.45)
mel.connect_material_expressions(fresnel, "", fresnel_045, "A")
albedo = mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -400, -200)
mel.connect_material_expressions(dark, "", albedo, "A")
mel.connect_material_expressions(edge, "", albedo, "B")
mel.connect_material_expressions(fresnel_045, "", albedo, "Alpha")
mel.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)

fresnel_025 = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, 200)
fresnel_025.set_editor_property("const_b", 0.25)
mel.connect_material_expressions(fresnel, "", fresnel_025, "A")
add = mel.create_material_expression(material, unreal.MaterialExpressionAdd, -450, 200)
add.set_editor_property("const_b", 0.40)
mel.connect_material_expressions(fresnel_025, "", add, "A")
clamp = mel.create_material_expression(material, unreal.MaterialExpressionClamp, -250, 200)
clamp.set_editor_property("min_default", 0.20)
clamp.set_editor_property("max_default", 0.60)
mel.connect_material_expressions(add, "", clamp, "Input")
mel.connect_material_property(clamp, "", unreal.MaterialProperty.MP_OPACITY)

roughness = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -250, 350)
roughness.set_editor_property("r", 0.85)
mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
metallic = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -250, 450)
metallic.set_editor_property("r", 0.1)
mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
log_lines.append("created %s saved=%s" % (full, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_stasis_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
