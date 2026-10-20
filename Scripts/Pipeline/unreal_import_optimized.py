"""Character single-slot pipeline, stage 7: import the optimized character into Unreal (headless editor Python).

PREPARED, NOT YET RUN: needs the user's editor closed (the commandlet saves new assets).

    UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript -script="<this file>" -unattended
        -AllowCommandletRendering -RenderOffscreen -nosplash
    with CODEX_PIPELINE_ARGS='["--asset-dir", "<ArtPipeline asset folder>", "--dest", "/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a"]'

Creates ONLY new assets under --dest (never touches the source pack, Blueprints or maps):
  SK_<Name>_1slot          on the SAME skeleton as the source (inventory.json), source physics asset reused
  T_<Name>_BaseColor       sRGB, TC_Default (BC1), TEXTUREGROUP_Character
  T_<Name>_Normal          TC_Normalmap (BC5), DirectX (no flip), TEXTUREGROUP_CharacterNormalMap
  T_<Name>_ORM             linear, TC_Masks, TEXTUREGROUP_CharacterSpecular (R=AO G=Roughness B=Metallic, final values)
  T_<Name>_Sheen           linear, TC_Alpha (BC4)
  M_CharacterMaster        (once, in --master-dir) Opaque: BaseColor * Tint + Fresnel * Sheen, ORM unpacked, Normal
  MI_<Name>                instance of the master with the four textures
"""
import argparse
import json
import os
import sys

import unreal


def parse():
    argv = sys.argv[1:]
    if not argv and os.environ.get("CODEX_PIPELINE_ARGS"):
        argv = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--dest", required=True)
    p.add_argument("--master-dir", default="/Game/Characters/Materials")
    p.add_argument("--master-name", default="M_CharacterMaster")
    return p.parse_args(argv)


def import_task(filename, dest, name, options=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = False
    if options:
        t.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    paths = list(t.imported_object_paths)
    if not paths:
        raise RuntimeError("import failed: %s" % filename)
    return unreal.load_asset(paths[0])


def texture(path, dest, name, srgb, compression, group):
    tex = import_task(path, dest, name)
    tex.set_editor_property("srgb", srgb)
    tex.set_editor_property("compression_settings", compression)
    tex.set_editor_property("lod_group", group)
    tex.set_editor_property("flip_green_channel", False)
    return tex


def master_material(directory, name):
    path = "%s/%s" % (directory, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, unreal.Material,
                                                                  unreal.MaterialFactoryNew())

    def tex_param(pname, sampler, x, y):
        n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        n.set_editor_property("parameter_name", pname)
        n.set_editor_property("sampler_type", sampler)
        return n

    bc = tex_param("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -300)
    nrm = tex_param("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -900, 300)
    orm = tex_param("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -900, 0)
    sheen = tex_param("Sheen", unreal.MaterialSamplerType.SAMPLERTYPE_ALPHA, -900, -600)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, -800)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    mul_tint = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, -400)
    mel.connect_material_expressions(bc, "RGB", mul_tint, "A")
    mel.connect_material_expressions(tint, "", mul_tint, "B")
    fres = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -600, -700)
    mul_sheen = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, -650)
    mel.connect_material_expressions(fres, "", mul_sheen, "A")
    mel.connect_material_expressions(sheen, "R", mul_sheen, "B")
    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -200, -450)
    mel.connect_material_expressions(mul_tint, "", add, "A")
    mel.connect_material_expressions(mul_sheen, "", add, "B")
    mel.connect_material_property(add, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(orm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(orm, "B", unreal.MaterialProperty.MP_METALLIC)
    mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    mel.recompile_material(mat)
    return mat


def main():
    args = parse()
    src = os.path.join(args.asset_dir, "01_Source")
    inv = json.load(open(os.path.join(src, "inventory.json"), encoding="utf-8"))
    short = inv["name"].replace("SK_", "", 1)
    baked = os.path.join(args.asset_dir, "04_Textures_Baked")
    fbx = os.path.join(args.asset_dir, "05_Export", inv["name"] + "_1slot.fbx")
    skeleton = unreal.load_asset(inv["skeleton"])
    physics = unreal.load_asset(inv["physics_asset"]) if inv.get("physics_asset") else None

    tg = unreal.TextureGroup
    tc = unreal.TextureCompressionSettings
    tex = {
        "BaseColor": texture(os.path.join(baked, "T_%s_BaseColor.png" % short), args.dest, "T_%s_BaseColor" % short,
                             True, tc.TC_DEFAULT, tg.TEXTUREGROUP_CHARACTER),
        "Normal": texture(os.path.join(baked, "T_%s_Normal.png" % short), args.dest, "T_%s_Normal" % short,
                          False, tc.TC_NORMALMAP, tg.TEXTUREGROUP_CHARACTER_NORMAL_MAP),
        "ORM": texture(os.path.join(baked, "T_%s_ORM.png" % short), args.dest, "T_%s_ORM" % short,
                       False, tc.TC_MASKS, tg.TEXTUREGROUP_CHARACTER_SPECULAR),
        "Sheen": texture(os.path.join(baked, "T_%s_Sheen.png" % short), args.dest, "T_%s_Sheen" % short,
                         False, tc.TC_ALPHA, tg.TEXTUREGROUP_CHARACTER),
    }

    master = master_material(args.master_dir, args.master_name)
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_%s" % short, args.dest, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, master)
    for k, t in tex.items():
        mel.set_material_instance_texture_parameter_value(mi, k, t)

    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", physics is None)
    if physics:
        ui.set_editor_property("physics_asset", physics)
    sk = ui.get_editor_property("skeletal_mesh_import_data")
    sk.set_editor_property("import_morph_targets", False)
    sk.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    sk.set_editor_property("normal_generation_method", unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
    sk.set_editor_property("update_skeleton_reference_pose", False)
    sk.set_editor_property("use_t0_as_ref_pose", False)
    sk.set_editor_property("convert_scene", True)
    mesh = import_task(fbx, args.dest, "SK_%s_1slot" % short, ui)
    mats = mesh.materials
    if len(mats) != 1:
        unreal.log_warning("expected one material slot, got %d" % len(mats))
    m0 = mats[0]
    m0.set_editor_property("material_interface", mi)
    mesh.set_editor_property("materials", [m0])
    if physics:
        mesh.set_editor_property("physics_asset", physics)

    saved = []
    for a in [mesh, mi, master] + list(tex.values()):
        p = a.get_path_name().split(".")[0]
        if p.startswith(args.dest) or p.startswith(args.master_dir):
            unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False)
            saved.append(p)
    unreal.log("PIPELINE_IMPORT_DONE %s" % json.dumps({"saved": saved, "slots": len(mesh.materials)}))


main()
