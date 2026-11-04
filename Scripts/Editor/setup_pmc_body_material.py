"""Setup PMC Operator Body Material (Task T5).

Creates /Game/Characters/Enemies/PMC/Operator/Materials/M_PmcOperator_Body:
- Master material taking pack textures from Content/Cyberpunk_Explorer_A by reference
- Parameters:
  - Tint (vector): multiplies base color on cloth via cloth mask (non-metallic)
  - BaseColorFactor (vector): secondary tint alias for runtime subsystem
  - SnowAmount (scalar 0..1): world-up snow accumulation layer on shoulders/coat
  - Grime (scalar 0..1): dirt/crevice darkening and roughness modifier
  - basecolor (texture2d)
  - Normal (texture2d)
  - ORM (texture2d, Occlusion/Roughness/Metallic)
- Re-parents MI_PmcOperator_Grey, MI_PmcOperator_Dark, MI_PmcOperator_Snowed to M_PmcOperator_Body
  with distinct visible settings.
"""

import os
import unreal

log = []

def say(text):
    log.append(str(text))
    unreal.log("setup_pmc_body_material: " + str(text))

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

MASTER_DIR = "/Game/Characters/Enemies/PMC/Operator/Materials"
MASTER_NAME = "M_PmcOperator_Body"
MASTER_PATH = MASTER_DIR + "/" + MASTER_NAME

OPERATOR_DIR = "/Game/Characters/Enemies/PMC/Operator"

BASE_TEX_PATH = "/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_4th_BaseColor"
NORM_TEX_PATH = "/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_Normal"
ORM_TEX_PATH = "/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_OcclusionRoughnessMetallic"

SHADING_HLSL = r"""
float ao = ORM.r;
float roughness = ORM.g;
float metallic = ORM.b;

// 1. Cloth mask (non-metals take the cloth dye/tint)
float clothMask = saturate(1.0 - metallic * 2.5);

// 2. Tint
float3 combinedTint = Tint * BaseColorFactor;
float3 tintedBase = lerp(BaseColor, BaseColor * combinedTint, clothMask);

// 3. Grime: darkens crevices and increases surface roughness
float grimeFactor = saturate(Grime);
float grimeDarken = 1.0 - grimeFactor * 0.45 * (1.0 - ao * 0.4);
float3 grimeBase = tintedBase * grimeDarken;
float grimeRoughness = lerp(roughness, min(roughness + 0.2, 0.95), grimeFactor);

// 4. World-up Snow layer
float up = saturate(dot(normalize(WorldNormal), float3(0.0, 0.0, 1.0)));
float snowAmt = saturate(SnowAmount);
float snowThreshold = 1.0 - snowAmt * 0.85;
float snowMask = saturate((up - snowThreshold) / 0.25) * saturate(snowAmt * 1.5);

// Snow appearance: soft cold white, high roughness
float3 snowColor = float3(0.86, 0.89, 0.93);
float3 finalBase = lerp(grimeBase, snowColor, snowMask);
float finalRoughness = lerp(grimeRoughness, 0.9, snowMask);

return float4(finalBase, finalRoughness);
"""

METALLIC_HLSL = r"""
float metallic = ORM.b;
float up = saturate(dot(normalize(WorldNormal), float3(0.0, 0.0, 1.0)));
float snowAmt = saturate(SnowAmount);
float snowThreshold = 1.0 - snowAmt * 0.85;
float snowMask = saturate((up - snowThreshold) / 0.25) * saturate(snowAmt * 1.5);
return lerp(metallic, 0.0, snowMask);
"""

def create_master_material():
    if not library.does_directory_exist(MASTER_DIR):
        library.make_directory(MASTER_DIR)
        say("Created directory " + MASTER_DIR)

    base_tex = library.load_asset(BASE_TEX_PATH)
    norm_tex = library.load_asset(NORM_TEX_PATH)
    orm_tex = library.load_asset(ORM_TEX_PATH)

    if not base_tex or not norm_tex or not orm_tex:
        say("ERROR: Missing one or more pack textures!")
        return None

    if library.does_asset_exist(MASTER_PATH):
        say("Master material %s already exists, loading..." % MASTER_PATH)
        material = library.load_asset(MASTER_PATH)
    else:
        say("Creating new master material %s..." % MASTER_PATH)
        material = tools.create_asset(MASTER_NAME, MASTER_DIR, unreal.Material, unreal.MaterialFactoryNew())

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("two_sided", False)

    # Clear existing expressions if any
    expressions = list(mel.get_material_expressions(material))
    for exp in expressions:
        mel.delete_material_expression(material, exp)

    # 1. Texture parameters
    tex_base = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, -200)
    tex_base.set_editor_property("parameter_name", "basecolor")
    tex_base.set_editor_property("texture", base_tex)

    tex_orm = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 100)
    tex_orm.set_editor_property("parameter_name", "ORM")
    tex_orm.set_editor_property("texture", orm_tex)
    tex_orm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    tex_norm = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -900, 400)
    tex_norm.set_editor_property("parameter_name", "Normal")
    tex_norm.set_editor_property("texture", norm_tex)
    tex_norm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)

    # 2. Parameters
    param_tint = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -450)
    param_tint.set_editor_property("parameter_name", "Tint")
    param_tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    param_bcf = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -900, -320)
    param_bcf.set_editor_property("parameter_name", "BaseColorFactor")
    param_bcf.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    param_snow = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 650)
    param_snow.set_editor_property("parameter_name", "SnowAmount")
    param_snow.set_editor_property("default_value", 0.0)
    param_snow.set_editor_property("slider_min", 0.0)
    param_snow.set_editor_property("slider_max", 1.0)

    param_grime = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -900, 750)
    param_grime.set_editor_property("parameter_name", "Grime")
    param_grime.set_editor_property("default_value", 0.0)
    param_grime.set_editor_property("slider_min", 0.0)
    param_grime.set_editor_property("slider_max", 1.0)

    node_wn = mel.create_material_expression(material, unreal.MaterialExpressionPixelNormalWS, -900, 850)

    # 3. Custom shading node (BaseColor RGB + Roughness A)
    custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -500, 0)
    custom.set_editor_property("description", "PMC Shading & Snow")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("code", SHADING_HLSL)

    shading_inputs = ["BaseColor", "ORM", "WorldNormal", "Tint", "BaseColorFactor", "SnowAmount", "Grime"]
    custom_inputs = []
    for inp_name in shading_inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", inp_name)
        custom_inputs.append(item)
    custom.set_editor_property("inputs", custom_inputs)

    mel.connect_material_expressions(tex_base, "RGB", custom, "BaseColor")
    mel.connect_material_expressions(tex_orm, "RGB", custom, "ORM")
    mel.connect_material_expressions(node_wn, "", custom, "WorldNormal")
    mel.connect_material_expressions(param_tint, "RGB", custom, "Tint")
    mel.connect_material_expressions(param_bcf, "RGB", custom, "BaseColorFactor")
    mel.connect_material_expressions(param_snow, "", custom, "SnowAmount")
    mel.connect_material_expressions(param_grime, "", custom, "Grime")

    # Connect BaseColor
    mask_base = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -200, -100)
    mask_base.set_editor_property("r", True)
    mask_base.set_editor_property("g", True)
    mask_base.set_editor_property("b", True)
    mask_base.set_editor_property("a", False)
    mel.connect_material_expressions(custom, "", mask_base, "")
    mel.connect_material_property(mask_base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Connect Roughness
    mask_rough = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -200, 100)
    mask_rough.set_editor_property("r", False)
    mask_rough.set_editor_property("g", False)
    mask_rough.set_editor_property("b", False)
    mask_rough.set_editor_property("a", True)
    mel.connect_material_expressions(custom, "", mask_rough, "")
    mel.connect_material_property(mask_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # 4. Metallic node
    custom_metal = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -500, 300)
    custom_metal.set_editor_property("description", "PMC Metallic")
    custom_metal.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom_metal.set_editor_property("code", METALLIC_HLSL)

    metal_inputs = ["ORM", "WorldNormal", "SnowAmount"]
    metal_custom_inputs = []
    for inp_name in metal_inputs:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", inp_name)
        metal_custom_inputs.append(item)
    custom_metal.set_editor_property("inputs", metal_custom_inputs)

    mel.connect_material_expressions(tex_orm, "RGB", custom_metal, "ORM")
    mel.connect_material_expressions(node_wn, "", custom_metal, "WorldNormal")
    mel.connect_material_expressions(param_snow, "", custom_metal, "SnowAmount")
    mel.connect_material_property(custom_metal, "", unreal.MaterialProperty.MP_METALLIC)

    # 5. Connect Normal and AO
    mel.connect_material_property(tex_norm, "", unreal.MaterialProperty.MP_NORMAL)

    mask_ao = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -200, 250)
    mask_ao.set_editor_property("r", True)
    mask_ao.set_editor_property("g", False)
    mask_ao.set_editor_property("b", False)
    mask_ao.set_editor_property("a", False)
    mel.connect_material_expressions(tex_orm, "", mask_ao, "")
    mel.connect_material_property(mask_ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    mel.recompile_material(material)
    library.save_loaded_asset(material)
    say("Master material %s created, recompiled and saved successfully." % MASTER_PATH)
    return material

def configure_material_instances(master):
    settings = {
        "MI_PmcOperator_Grey": {
            "Tint": unreal.LinearColor(154 / 255.0, 158 / 255.0, 163 / 255.0, 1.0),
            "BaseColorFactor": unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
            "SnowAmount": 0.0,
            "Grime": 0.15,
        },
        "MI_PmcOperator_Dark": {
            "Tint": unreal.LinearColor(51 / 255.0, 55 / 255.0, 59 / 255.0, 1.0),
            "BaseColorFactor": unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
            "SnowAmount": 0.0,
            "Grime": 0.05,
        },
        "MI_PmcOperator_Snowed": {
            "Tint": unreal.LinearColor(216 / 255.0, 220 / 255.0, 224 / 255.0, 1.0),
            "BaseColorFactor": unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
            "SnowAmount": 0.85,
            "Grime": 0.20,
        },
    }

    for name, params in settings.items():
        mi_path = OPERATOR_DIR + "/" + name
        mi = library.load_asset(mi_path)
        if not mi:
            say("Creating missing MI: %s" % mi_path)
            factory = unreal.MaterialInstanceConstantFactoryNew()
            mi = tools.create_asset(name, OPERATOR_DIR, unreal.MaterialInstanceConstant, factory)

        # Reparent to M_PmcOperator_Body
        mel.set_material_instance_parent(mi, master)
        say("Reparented %s -> %s" % (name, master.get_name()))

        # Set parameters
        mel.set_material_instance_vector_parameter_value(mi, "Tint", params["Tint"])
        mel.set_material_instance_vector_parameter_value(mi, "BaseColorFactor", params["BaseColorFactor"])
        mel.set_material_instance_scalar_parameter_value(mi, "SnowAmount", params["SnowAmount"])
        mel.set_material_instance_scalar_parameter_value(mi, "Grime", params["Grime"])

        mel.update_material_instance(mi)
        library.save_loaded_asset(mi)
        say("Configured and saved %s (Tint=%s, Snow=%s, Grime=%s)" % (name, params["Tint"], params["SnowAmount"], params["Grime"]))

def main():
    say("Starting setup_pmc_body_material...")
    master = create_master_material()
    if not master:
        say("Failed to create master material!")
        return False
    configure_material_instances(master)
    say("setup_pmc_body_material completed successfully.")
    return True

try:
    main()
finally:
    out_dir = os.path.join(unreal.Paths.project_saved_dir(), "Logs")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "setup_pmc_body_material.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(log) + "\n")
