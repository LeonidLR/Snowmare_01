# Creates /Game/VFX/Materials/M_TargetFresnel: the red edge flash of an enemy picked as a target, seen through walls
# (Godot Shaders/tactical_target_fresnel.gdshader: unshaded, no depth test, albedo = fresnel_color.rgb,
# alpha = clamp(fresnel_color.a * fresnel^2 * 3.2 * flash_alpha, 0, 0.95); main.gd _apply_enemy_target_fresnel fades
# flash_alpha 1 -> 0 in 0.4 s). Used by UCombatFeedbackSubsystem::HighlightTarget as the overlay of the enemy's meshes;
# parameters "Color" (rgb + a) and "Intensity" (= flash_alpha, faded by ACombatFeedbackActor).
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_target_fresnel_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_TargetFresnel"
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("disable_depth_test", True)
material.set_editor_property("used_with_skeletal_mesh", True)

color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -200)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.0194, 0.0100, 0.95))  # sRGB (1, 0.15, 0.1)
mel.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

fresnel = mel.create_material_expression(material, unreal.MaterialExpressionFresnel, -900, 100)
fresnel.set_editor_property("exponent", 2.0)
fresnel.set_editor_property("base_reflect_fraction", 0.0)
edge = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -650, 100)
edge.set_editor_property("const_b", 3.2)
mel.connect_material_expressions(fresnel, "", edge, "A")
flash = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 300)
flash.set_editor_property("parameter_name", "Intensity")
flash.set_editor_property("default_value", 1.0)
base = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -450, 100)
mel.connect_material_expressions(edge, "", base, "A")
mel.connect_material_expressions(flash, "", base, "B")
alpha = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 100)
mel.connect_material_expressions(color, "A", alpha, "A")
mel.connect_material_expressions(base, "", alpha, "B")
clamp = mel.create_material_expression(material, unreal.MaterialExpressionClamp, -150, 100)
clamp.set_editor_property("min_default", 0.0)
clamp.set_editor_property("max_default", 0.95)
mel.connect_material_expressions(alpha, "", clamp, "")
mel.connect_material_property(clamp, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
log_lines.append("created %s saved=%s" % (full, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_target_fresnel_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
