# Creates /Game/VFX/Materials/M_GhostHologram: the see-through placement / relocation ghost
# (Godot main.gd _set_ghost_material_valid: StandardMaterial3D alpha 0.65, albedo = emission colour, emission x2 —
# green (0.2, 0.95, 0.4) valid, red (1, 0.2, 0.2) invalid). Unlit translucent here: emissive = Color x EmissionScale,
# opacity = Opacity. Used by ARelocationGhostActor (parameter "Color" set per valid / invalid).
# Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file>
# Result: Saved/Logs/create_ghost_hologram_material.txt
import os
import unreal

PATH = "/Game/VFX/Materials"
NAME = "M_GhostHologram"
log_lines = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
full = PATH + "/" + NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    unreal.EditorAssetLibrary.delete_asset(full)
material = tools.create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
material.set_editor_property("two_sided", True)
material.set_editor_property("used_with_static_lighting", False)
material.set_editor_property("used_with_instanced_static_meshes", True)

color = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, -100)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(0.0331, 0.8879, 0.1329, 1.0))  # sRGB (0.2, 0.95, 0.4)
scale = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 150)
scale.set_editor_property("parameter_name", "EmissionScale")
scale.set_editor_property("default_value", 2.0)
emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, -50)
mel.connect_material_expressions(color, "RGB", emissive, "A")
mel.connect_material_expressions(scale, "", emissive, "B")
mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

opacity = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 300)
opacity.set_editor_property("parameter_name", "Opacity")
opacity.set_editor_property("default_value", 0.65)
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

mel.recompile_material(material)
saved = unreal.EditorAssetLibrary.save_asset(full)
log_lines.append("created %s saved=%s" % (full, saved))

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_ghost_hologram_material.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log_lines) + "\n")
