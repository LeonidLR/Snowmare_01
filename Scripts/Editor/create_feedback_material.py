# Creates /Game/VFX/Materials/M_CombatFeedback: unlit additive glow for tracers, plan markers, target flashes and
# the turn-based grid overlay (instanced static meshes).
# Parameters: Color (vector), Intensity (scalar; fade to 0). Used by UCombatFeedbackSubsystem (C++).
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_feedback_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_CombatFeedback"
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
material.set_editor_property("two_sided", True)
# Turn-based grid overlay tiles are instanced static meshes.
material.set_editor_property("used_with_instanced_static_meshes", True)

color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(0.2, 1.0, 0.4, 1.0))
intensity = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 200)
intensity.set_editor_property("parameter_name", "Intensity")
intensity.set_editor_property("default_value", 5.0)
multiply = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 100)
mel.connect_material_expressions(color, "", multiply, "A")
mel.connect_material_expressions(intensity, "", multiply, "B")
mel.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
log_lines.append("created %s saved=%s" % (full, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_feedback_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
