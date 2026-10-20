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
  T_<Name>_Sheen           linear, TC_Grayscale (G8, 1K)
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
    p.add_argument("--lods", type=int, default=3, help="LOD count generated in Unreal (LOD0 = the imported mesh)")
    p.add_argument("--allow-existing", action="store_true", help="re-import over assets already in --dest")
    p.add_argument("--variants-only", default="",
                   help="comma separated look variants to ADD to an imported asset: their textures + MI only; the mesh, "
                        "its slot and every existing asset stay untouched")
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


def set_master_defaults(mat, defaults):
    """Texture parameters need a default texture of the matching sampler type or the material does not compile."""
    mel = unreal.MaterialEditingLibrary
    for e in mel.get_material_expressions(mat):
        if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D):
            name = str(e.get_editor_property("parameter_name"))
            if name in defaults:
                e.set_editor_property("texture", defaults[name])
    mel.recompile_material(mat)
    try:
        st = mel.get_statistics(mat)
        unreal.log("PIPELINE_IMPORT master stats: vs %s ps %s samplers %s" % (
            st.num_vertex_shader_instructions, st.num_pixel_shader_instructions, st.num_samplers))
    except Exception as exc:
        unreal.log_warning("master stats unavailable: %s" % exc)


CHANGED_MASTERS = set()  # masters created / upgraded in this run: the only ones saved


def ensure_emissive(mat):
    """Adds the optional glow to the master (once): UseEmissive static switch (default off = no extra sampler for the bodies
    that do not glow) -> Emissive texture * EmissiveStrength."""
    mel = unreal.MaterialEditingLibrary
    names = [str(e.get_editor_property("parameter_name")) for e in mel.get_material_expressions(mat)
             if hasattr(e, "get_editor_property") and isinstance(e, (unreal.MaterialExpressionParameter,
                                                                     unreal.MaterialExpressionTextureSampleParameter))]
    if "Emissive" in names:
        return False
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 600)
    tex.set_editor_property("parameter_name", "Emissive")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tex.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black.Black"))
    strength = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 850)
    strength.set_editor_property("parameter_name", "EmissiveStrength")
    strength.set_editor_property("default_value", 1.0)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 700)
    mel.connect_material_expressions(tex, "RGB", mul, "A")
    mel.connect_material_expressions(strength, "", mul, "B")
    switch = mel.create_material_expression(mat, unreal.MaterialExpressionStaticSwitchParameter, -350, 700)
    switch.set_editor_property("parameter_name", "UseEmissive")
    switch.set_editor_property("default_value", False)
    zero = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -600, 900)
    mel.connect_material_expressions(mul, "", switch, "True")
    mel.connect_material_expressions(zero, "", switch, "False")
    mel.connect_material_property(switch, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    CHANGED_MASTERS.add(mat.get_path_name())
    return True


def master_material(directory, name, defaults, masked=False, two_sided=False):
    """M_CharacterMaster[_Masked][_TwoSided]: the same graph; masked = opacity mask from the BaseColor alpha (clip 0.3333),
    two-sided when the original body had two-sided materials (single-layer cloth)."""
    name = name + ("_Masked" if masked else "") + ("_TwoSided" if two_sided else "")
    path = "%s/%s" % (directory, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)  # shared by every body: never re-defaulted to this asset's textures
    mel = unreal.MaterialEditingLibrary
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, unreal.Material,
                                                                  unreal.MaterialFactoryNew())

    def tex_param(pname, sampler, x, y):
        n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        n.set_editor_property("parameter_name", pname)
        n.set_editor_property("sampler_type", sampler)
        return n

    if masked:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        mat.set_editor_property("opacity_mask_clip_value", 0.3333)
    if two_sided:
        mat.set_editor_property("two_sided", True)
    bc = tex_param("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, -900, -300)
    if masked:
        mel.connect_material_property(bc, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    nrm = tex_param("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, -900, 300)
    orm = tex_param("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS, -900, 0)
    sheen = tex_param("Sheen", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, -900, -600)
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
    try:
        mel.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    except Exception:
        mel.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    set_master_defaults(mat, defaults)
    CHANGED_MASTERS.add(mat.get_path_name())
    return mat


def main():
    args = parse()
    src = os.path.join(args.asset_dir, "01_Source")
    inv = json.load(open(os.path.join(src, "inventory.json"), encoding="utf-8"))
    short = os.path.basename(os.path.normpath(args.asset_dir))  # asset = folder name (pipeline_common.Paths.short)
    baked = os.path.join(args.asset_dir, "04_Textures_Baked")
    fbx = os.path.join(args.asset_dir, "05_Export", "SK_%s_1slot.fbx" % short)
    # do no harm: refuse to run over existing assets unless asked to
    existing = [str(a) for a in unreal.EditorAssetLibrary.list_assets(args.dest, recursive=True)]         if unreal.EditorAssetLibrary.does_directory_exist(args.dest) else []
    master_path = "%s/%s" % (args.master_dir, args.master_name)
    only = [v.strip() for v in args.variants_only.split(",") if v.strip()]
    if only:
        clash = [e for e in existing if any(("_%s_" % v) in e.split("/")[-1] or e.split(".")[-1].endswith("_" + v)
                                            for v in only)]
        if clash and not args.allow_existing:  # --allow-existing: re-import these variants' textures + MIs
            raise RuntimeError("variant assets already exist: %s (use --allow-existing to re-import)" % clash)
    elif existing and not args.allow_existing:
        raise RuntimeError("assets already in %s: %s (use --allow-existing to re-import)" % (args.dest, existing))
    unreal.log("PIPELINE_IMPORT existing in dest: %s; master exists: %s" % (
        existing, unreal.EditorAssetLibrary.does_asset_exist(master_path)))
    skeleton = unreal.load_asset(inv["skeleton"])
    physics = unreal.load_asset(inv["physics_asset"]) if inv.get("physics_asset") else None

    tg = unreal.TextureGroup
    tc = unreal.TextureCompressionSettings
    # Look variants (texture sets, e.g. the Military Zombie): one texture set + one MI per variant on the same mesh;
    # the mesh's slot gets the first variant's MI.
    variant_names = [v["name"] for v in inv.get("variants") or []] or [None]
    if only:
        variant_names = [v for v in variant_names if v in only]
        if len(variant_names) != len(only):
            raise RuntimeError("variants %s not in the inventory" % only)
    mel = unreal.MaterialEditingLibrary
    master = None
    masters, instances, all_textures = [], [], []
    for vname in variant_names:
        tag = short if not vname else "%s_%s" % (short, vname)
        tex = {
            "BaseColor": texture(os.path.join(baked, "T_%s_BaseColor.png" % tag), args.dest, "T_%s_BaseColor" % tag,
                                 True, tc.TC_DEFAULT, tg.TEXTUREGROUP_CHARACTER),
            "Normal": texture(os.path.join(baked, "T_%s_Normal.png" % tag), args.dest, "T_%s_Normal" % tag,
                              False, tc.TC_NORMALMAP, tg.TEXTUREGROUP_CHARACTER_NORMAL_MAP),
            "ORM": texture(os.path.join(baked, "T_%s_ORM.png" % tag), args.dest, "T_%s_ORM" % tag,
                           False, tc.TC_MASKS, tg.TEXTUREGROUP_CHARACTER_SPECULAR),
            "Sheen": texture(os.path.join(baked, "T_%s_Sheen.png" % tag), args.dest, "T_%s_Sheen" % tag,
                             False, tc.TC_GRAYSCALE, tg.TEXTUREGROUP_CHARACTER),
        }
        all_textures += list(tex.values())
        summary = json.load(open(os.path.join(args.asset_dir, "06_Review", "summary.json"), encoding="utf-8"))
        flags = summary.get("bake", {}).get("variants", {}).get(vname or "default", {})
        master = master_material(args.master_dir, args.master_name, tex, bool(flags.get("masked")), bool(flags.get("two_sided")))
        masters.append(master)
        mi_path = "%s/MI_%s" % (args.dest, tag)
        if unreal.EditorAssetLibrary.does_asset_exist(mi_path):
            mi = unreal.load_asset(mi_path)
        else:
            mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                "MI_%s" % tag, args.dest, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, master)
        for k, t in tex.items():
            mel.set_material_instance_texture_parameter_value(mi, k, t)
        em_png = os.path.join(baked, "T_%s_Emissive.png" % tag)
        if os.path.exists(em_png):
            if ensure_emissive(master):
                unreal.log("PIPELINE_IMPORT master: optional emissive added")
            em_tex = texture(em_png, args.dest, "T_%s_Emissive" % tag, True, tc.TC_DEFAULT, tg.TEXTUREGROUP_CHARACTER)
            all_textures.append(em_tex)
            strength = flags.get("emissive_strength", 1.0)
            mel.set_material_instance_texture_parameter_value(mi, "Emissive", em_tex)
            mel.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", float(strength))
            mel.set_material_instance_static_switch_parameter_value(mi, "UseEmissive", True)
            mel.update_material_instance(mi)
        instances.append(mi)
    mi = instances[0]
    if only:
        saved = []
        changed = [m for m in {m.get_path_name(): m for m in masters}.values() if m.get_path_name() in CHANGED_MASTERS]
        for a in changed + instances + all_textures:
            p = a.get_path_name().split(".")[0]
            if p.startswith(args.dest) or p.startswith(args.master_dir):
                unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False)
                saved.append(p)
        unreal.log("PIPELINE_IMPORT_DONE %s" % json.dumps({"saved": saved, "variants": variant_names, "mesh": "untouched"}))
        return

    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    ui.set_editor_property("skeleton", skeleton)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)  # never create assets we would not save
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
    sms = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    if args.lods > 1:
        sms.regenerate_lod(mesh, new_lod_count=args.lods, regenerate_even_if_imported=False, generate_base_lod=False)
    lods = [{"lod": i, "verts": sms.get_num_verts(mesh, i), "sections": sms.get_num_sections(mesh, i)}
            for i in range(sms.get_lod_count(mesh))]
    unreal.log("PIPELINE_IMPORT lods %s skeleton %s" % (json.dumps(lods), mesh.skeleton.get_path_name()))

    saved = []
    changed = [m for m in {m.get_path_name(): m for m in masters}.values() if m.get_path_name() in CHANGED_MASTERS]
    for a in [mesh] + changed + instances + all_textures:
        p = a.get_path_name().split(".")[0]
        if p.startswith(args.dest) or p.startswith(args.master_dir):
            unreal.EditorAssetLibrary.save_asset(p, only_if_is_dirty=False)
            saved.append(p)
    unreal.log("PIPELINE_IMPORT_DONE %s" % json.dumps({"saved": saved, "slots": len(mesh.materials),
                                                       "variants": [v for v in variant_names if v]}))


main()
