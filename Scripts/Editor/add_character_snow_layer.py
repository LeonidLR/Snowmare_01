# Adds the SNOW LAYER to the character master materials (environment section 15, directional snow on characters, user decisions
# 2026-10-23; docs/systems/environment-weather.md section 15, the indices in docs/systems/animation-setup.md):
#   /Game/Characters/Materials/M_CharacterMaster, M_CharacterMaster_Masked_TwoSided, M_CharacterMaster_TwoSided, M_CharacterMaster_Subsurface
# Driven by UCharacterSnowSubsystem through 5 Custom Primitive Data floats on the body mesh (CharacterSnowRules::FirstCustomDataIndex):
#   8 = SnowTop, 9 = SnowPX, 10 = SnowNX, 11 = SnowPY, 12 = SnowNY (pockets in the body mesh's LOCAL axes).
# Mask = Top x max(0, Nl.z) + SideScale x sum side_i x max(0, Nl . dir_i), Nl = the vertex normal in LOCAL space (world -> local
# Transform; the two-sided masters multiply the world normal by TwoSidedSign first, so a back face takes its own side), broken by a
# UV-space noise (T_TilingNoisePacked on UV0 x SnowNoiseTiling, the shared wrap sampler: no extra sampler slot), then
#   BaseColor = lerp(original, SnowColour, mask)          (slight whitening: SnowMaxCover 0.65 caps it, faces / weapons stay readable)
#   Roughness = lerp(original, SnowRoughness, mask)
#   Normal    = lerp(original, (0, 0, 1), mask x SnowNormalFlatten)   (tangent space: the snow smooths the detail a little)
# No WPO; the overlay slot, the ABPs and the Blueprints are not touched. IDENTICAL AT 0: with every pocket 0 the mask is exactly 0
# (guarded), every lerp returns its first input bit for bit.
# Idempotent: a master that already has the layer (custom node "CharacterSnowLayer:mask") only gets the HLSL and the parameter defaults
# below re-applied (tuning), nothing is re-wired. The very first insertion logs the instruction counts before / after.
# Also verifies that no expression of the masters already reads Custom Primitive Data (the indices must be free).
# Run (holding the build lock, $env:CODEX_AGENT = "claude"):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<abs path to this file> -AllowCommandletRendering
#   (-AllowCommandletRendering: the SM6 shaders are really compiled, so the compile errors and the instruction counts are real)
# Result: Saved/Logs/add_character_snow_layer.txt
import os
import unreal

MASTERS = ["M_CharacterMaster", "M_CharacterMaster_Masked_TwoSided", "M_CharacterMaster_TwoSided", "M_CharacterMaster_Subsurface"]
DIR = "/Game/Characters/Materials"
NOISE = "/Game/Environment/Weather/Common/Textures/T_TilingNoisePacked"
FIRST_INDEX = 8
POCKETS = ["SnowTop", "SnowPX", "SnowNX", "SnowPY", "SnowNY"]
MARK = "CharacterSnowLayer:"
# Tuning (master defaults; an MI may override them).
SCALARS = {
    "SnowGain": 0.8,
    "SnowNoiseAmp": 0.9,
    "SnowBias": 0.5,
    "SnowSharpness": 2.0,
    "SnowMaxCover": 0.65,
    "SnowSideScale": 1.0,
    "SnowNoiseTiling": 7.0,
    "SnowRoughness": 0.75,
    "SnowNormalFlatten": 0.6,
}
SNOW_COLOUR = unreal.LinearColor(0.78, 0.82, 0.87, 1.0)

MASK_HLSL = r"""
// Directional snow on the body (environment section 15): pockets in the body mesh's LOCAL axes (Custom Primitive Data 8..12).
float3 n = normalize(N);
float side = PX * saturate(n.x) + NX * saturate(-n.x) + PY * saturate(n.y) + NY * saturate(-n.y);
float cover = Top * saturate(n.z) + side * SideScale;
// T_TilingNoisePacked R / G normalised to ~N(0.5, 0.2) (measured: R 0.31 +- 0.175, G 0.50 +- 0.19).
float a = (Noise.r - 0.31) / 0.175 * 0.2 + 0.5;
float b = (Noise.g - 0.50) / 0.19 * 0.2 + 0.5;
float pattern = a * 0.6 + b * 0.4;
float m = saturate((cover * Gain + (pattern - 0.5) * NoiseAmp - Bias) * Sharpness) * MaxCover;
// Exactly 0 without snow: the layer must render bit for bit like the material without it.
return cover > 0.0001 ? m : 0.0;
"""

log_lines = []


def log(text):
    log_lines.append(str(text))
    unreal.log(str(text))


def write_log():
    out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "add_character_snow_layer.txt")
    with open(out, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")


mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def stats(material):
    try:
        s = mel.get_statistics(material)
        return "vs %d ps %d samplers %d" % (s.num_vertex_shader_instructions, s.num_pixel_shader_instructions, s.num_samplers)
    except Exception as error:  # noqa: BLE001
        return "stats unavailable (%s)" % error


def desc_of(expression):
    try:
        return str(expression.get_editor_property("desc"))
    except Exception:  # noqa: BLE001
        return ""


def find_marked(material, role):
    for expression in mel.get_material_expressions(material):
        if desc_of(expression) == MARK + role:
            return expression
        if isinstance(expression, unreal.MaterialExpressionCustom) and str(expression.get_editor_property("description")) == MARK + role:
            return expression
    return None


def reads_custom_primitive_data(material):
    found = []
    for expression in mel.get_material_expressions(material):
        if isinstance(expression, (unreal.MaterialExpressionScalarParameter, unreal.MaterialExpressionVectorParameter)):
            try:
                if expression.get_editor_property("use_custom_primitive_data"):
                    found.append("%s@%d" % (expression.get_editor_property("parameter_name"), expression.get_editor_property("primitive_data_index")))
            except Exception:  # noqa: BLE001
                pass
        if type(expression).__name__ in ("MaterialExpressionPerInstanceCustomData", "MaterialExpressionPerInstanceCustomData3Vector"):
            found.append(type(expression).__name__)
    return found


def mark(expression, role):
    expression.set_editor_property("desc", MARK + role)
    return expression


def input_of(material, prop):
    node = mel.get_material_property_input_node(material, prop)
    name = mel.get_material_property_input_node_output_name(material, prop) if node else ""
    if name in ("None",):
        name = ""
    return node, name


def scalar_param(material, name, value, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", "Snow")
    return mark(node, "param")


def noise_sampler_type(texture):
    # T_TilingNoisePacked is TC_MASKS (linear): a mismatched sampler type is a material translation error.
    if texture.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_MASKS:
        return unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def apply_defaults(material):
    for expression in mel.get_material_expressions(material):
        if not desc_of(expression).startswith(MARK):
            continue
        if isinstance(expression, unreal.MaterialExpressionTextureSample) and expression.get_editor_property("texture"):
            expression.set_editor_property("sampler_type", noise_sampler_type(expression.get_editor_property("texture")))
        if isinstance(expression, unreal.MaterialExpressionScalarParameter):
            name = str(expression.get_editor_property("parameter_name"))
            if name in SCALARS:
                expression.set_editor_property("default_value", SCALARS[name])
        if isinstance(expression, unreal.MaterialExpressionVectorParameter) and str(expression.get_editor_property("parameter_name")) == "SnowColour":
            expression.set_editor_property("default_value", SNOW_COLOUR)


def insert_layer(material, two_sided):
    base_node, base_out = input_of(material, unreal.MaterialProperty.MP_BASE_COLOR)
    rough_node, rough_out = input_of(material, unreal.MaterialProperty.MP_ROUGHNESS)
    normal_node, normal_out = input_of(material, unreal.MaterialProperty.MP_NORMAL)
    log("  inputs: base %s.%s, roughness %s.%s, normal %s.%s" % (base_node.get_name() if base_node else None, base_out,
        rough_node.get_name() if rough_node else None, rough_out, normal_node.get_name() if normal_node else None, normal_out))
    if not base_node or not rough_node or not normal_node:
        log("  SKIPPED: a base colour / roughness / normal input is missing")
        return False
    x0 = -2600
    # Pockets (Custom Primitive Data 8..12).
    pockets = []
    for index, name in enumerate(POCKETS):
        node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x0, 1400 + index * 90)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", 0.0)
        node.set_editor_property("group", "Snow (Custom Primitive Data)")
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", FIRST_INDEX + index)
        pockets.append(mark(node, "pocket"))
    params = {}
    for i, (name, value) in enumerate(SCALARS.items()):
        params[name] = scalar_param(material, name, value, x0, 1900 + i * 80)
    colour = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, x0, 2700)
    colour.set_editor_property("parameter_name", "SnowColour")
    colour.set_editor_property("default_value", SNOW_COLOUR)
    colour.set_editor_property("group", "Snow")
    mark(colour, "param")
    # Local-space normal (two-sided: the back face's own normal).
    normal_ws = mark(mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, x0, 1100), "normal")
    world_normal = normal_ws
    if two_sided:
        sign = mark(mel.create_material_expression(material, unreal.MaterialExpressionTwoSidedSign, x0, 1200), "normal")
        flipped = mark(mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x0 + 250, 1150), "normal")
        mel.connect_material_expressions(normal_ws, "", flipped, "A")
        mel.connect_material_expressions(sign, "", flipped, "B")
        world_normal = flipped
    to_local = mark(mel.create_material_expression(material, unreal.MaterialExpressionTransform, x0 + 500, 1150), "normal")
    to_local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    to_local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    mel.connect_material_expressions(world_normal, "", to_local, "")
    # Noise on UV0 (sticks to the body, no swimming with the animation).
    uv = mark(mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, x0, 2900), "noise")
    uv_scaled = mark(mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x0 + 250, 2900), "noise")
    mel.connect_material_expressions(uv, "", uv_scaled, "A")
    mel.connect_material_expressions(params["SnowNoiseTiling"], "", uv_scaled, "B")
    noise_texture = eal.load_asset(NOISE)
    noise = mark(mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, x0 + 500, 2900), "noise")
    noise.set_editor_property("texture", noise_texture)
    noise.set_editor_property("sampler_type", noise_sampler_type(noise_texture))
    noise.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    mel.connect_material_expressions(uv_scaled, "", noise, "UVs")
    # Mask.
    mask = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x0 + 900, 1600)
    mask.set_editor_property("code", MASK_HLSL)
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    mask.set_editor_property("description", MARK + "mask")
    names = ["N", "Top", "PX", "NX", "PY", "NY", "Noise", "Gain", "NoiseAmp", "Bias", "Sharpness", "MaxCover", "SideScale"]
    inputs = []
    for input_name in names:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    mask.set_editor_property("inputs", inputs)
    sources = [to_local] + pockets + [noise, params["SnowGain"], params["SnowNoiseAmp"], params["SnowBias"], params["SnowSharpness"],
                                      params["SnowMaxCover"], params["SnowSideScale"]]
    for input_name, node in zip(names, sources):
        mel.connect_material_expressions(node, "RGBA" if node is noise else "", mask, input_name)
    # Base colour.
    base = mark(mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x0 + 1300, 1300), "basecolor")
    mel.connect_material_expressions(base_node, base_out, base, "A")
    colour_rgb = mark(mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x0 + 1100, 1350), "basecolor")
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        colour_rgb.set_editor_property(channel, on)
    mel.connect_material_expressions(colour, "", colour_rgb, "")
    mel.connect_material_expressions(colour_rgb, "", base, "B")
    mel.connect_material_expressions(mask, "", base, "Alpha")
    mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Roughness.
    rough = mark(mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x0 + 1300, 1500), "roughness")
    mel.connect_material_expressions(rough_node, rough_out, rough, "A")
    mel.connect_material_expressions(params["SnowRoughness"], "", rough, "B")
    mel.connect_material_expressions(mask, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    # Normal (tangent space): flattened a little under the snow.
    flat = mark(mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x0 + 1100, 1750), "normal")
    flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    flatten = mark(mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x0 + 1100, 1850), "normal")
    mel.connect_material_expressions(mask, "", flatten, "A")
    mel.connect_material_expressions(params["SnowNormalFlatten"], "", flatten, "B")
    normal = mark(mel.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, x0 + 1300, 1700), "normal")
    mel.connect_material_expressions(normal_node, normal_out, normal, "A")
    mel.connect_material_expressions(flat, "", normal, "B")
    mel.connect_material_expressions(flatten, "", normal, "Alpha")
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
    return True


changed = 0
for name in MASTERS:
    path = DIR + "/" + name
    if not eal.does_asset_exist(path):
        log("%s: MISSING" % path)
        continue
    material = eal.load_asset(path)
    two_sided = bool(material.get_editor_property("two_sided"))
    existing = find_marked(material, "mask")
    cpd = [f for f in reads_custom_primitive_data(material) if not (existing and f.split("@")[0] in POCKETS)]
    log("%s: two-sided %s, blend %s, shading %s, existing layer %s, other CPD readers %s, before: %s" % (name, two_sided,
        material.get_editor_property("blend_mode"), material.get_editor_property("shading_model"), existing is not None, cpd or "none", stats(material)))
    if cpd:
        log("  ABORTED: Custom Primitive Data already read by %s" % cpd)
        continue
    if existing:
        existing.set_editor_property("code", MASK_HLSL)
        apply_defaults(material)
        log("  layer present: HLSL + defaults re-applied")
    elif not insert_layer(material, two_sided):
        continue
    errors = mel.recompile_material(material)
    log("  compile errors: %s" % (list(errors) if errors else "none"))
    after = stats(material)
    log("  after: %s" % after)
    if errors or after.startswith("vs 0 ps 0"):
        log("  NOT SAVED: the material does not compile")
        continue
    saved = eal.save_asset(path, only_if_is_dirty=False)
    log("  saved %s" % saved)
    changed += 1
log("done: %d master(s) updated" % changed)
write_log()
