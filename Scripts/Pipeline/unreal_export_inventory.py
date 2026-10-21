"""Character single-slot pipeline, stages 1-2 (Unreal side, READ-ONLY).

Writes an inventory of a skeletal mesh (slots -> materials -> texture parameters -> texture assets, LODs, skeleton,
referencing Blueprints, animations of the skeleton) and exports the mesh FBX, every referenced texture (PNG) and,
optionally, a set of animation sequences (FBX) used for the hidden-geometry pose test in Blender.

Never saves or modifies an asset. Run headless through the wrapper `Scripts/Pipeline/run_unreal_export.ps1` or:

    UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript
        -script="<this file>" -unattended -nullrhi -nosplash
    with CODEX_PIPELINE_ARGS='["--mesh", "/Game/Path/SK_Name", "--out", "<asset folder>", "--anim-regex", "<re>"]' 

Docs: docs/pipeline/CHARACTER_SINGLE_SLOT.md
"""
import argparse
import json
import os
import re
import shlex
import sys

import unreal


def parse_args():
    argv = sys.argv[1:]
    # Paths with spaces do not survive the commandlet's -script= parsing: the wrapper passes the arguments as a
    # JSON list in the CODEX_PIPELINE_ARGS environment variable instead.
    if not argv and os.environ.get("CODEX_PIPELINE_ARGS"):
        argv = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    elif len(argv) == 1 and " " in argv[0]:
        argv = shlex.split(argv[0], posix=False)
    p = argparse.ArgumentParser()
    p.add_argument("--mesh", required=True, help="/Game/... object path of the skeletal mesh")
    p.add_argument("--out", required=True, help="asset folder (01_Source / 02_Textures_Source are created inside)")
    p.add_argument("--anim-regex", default="", help="export AnimSequences of the skeleton whose name matches (pose test)")
    p.add_argument("--anim-max", type=int, default=12)
    p.add_argument("--anim-root", default="/Game", help="only consider animations under this path")
    p.add_argument("--skip-mesh", action="store_true", help="inventory + textures only (no mesh FBX)")
    p.add_argument("--skip-textures", action="store_true", help="do not re-export the textures")
    p.add_argument("--material-sets-file", default="",
                   help="JSON [{slot name: material path}, ...]: look variants (texture sets) baked on the same atlas")
    return p.parse_args([a.strip('"') for a in argv])


def obj_path(obj):
    return obj.get_path_name() if obj else None


def enum_name(value):
    s = str(value)
    return s.split(".")[-1].split(":")[0].strip("<> ")


def texture_info(tex):
    info = {"path": obj_path(tex), "class": tex.get_class().get_name()}
    for key, getter in (
        ("size_x", lambda: tex.blueprint_get_size_x()),
        ("size_y", lambda: tex.blueprint_get_size_y()),
        ("srgb", lambda: bool(tex.get_editor_property("srgb"))),
        ("compression", lambda: enum_name(tex.get_editor_property("compression_settings"))),
        ("lod_group", lambda: enum_name(tex.get_editor_property("lod_group"))),
        ("max_texture_size", lambda: tex.get_editor_property("max_texture_size")),
        ("memory_bytes", lambda: tex.blueprint_get_memory_size()),
    ):
        try:
            info[key] = getter()
        except Exception as exc:  # property not available for this texture class
            info[key] = None
            info.setdefault("warnings", []).append("%s: %s" % (key, exc))
    return info


def material_info(mat):
    mel = unreal.MaterialEditingLibrary
    info = {"path": obj_path(mat), "class": mat.get_class().get_name(), "texture_params": {}, "scalar_params": {},
            "vector_params": {}, "used_textures": []}
    is_instance = isinstance(mat, unreal.MaterialInstance)
    base = mat.get_base_material() if hasattr(mat, "get_base_material") else mat
    info["base_material"] = obj_path(base)
    try:
        info["blend_mode"] = enum_name(base.get_editor_property("blend_mode"))
        info["two_sided"] = bool(base.get_editor_property("two_sided"))
    except Exception:
        pass
    try:  # Default Lit / Subsurface / ... (the single-slot master has a Subsurface variant)
        info["shading_model"] = enum_name(base.get_editor_property("shading_model"))
    except Exception:
        pass
    for name in mel.get_texture_parameter_names(mat):
        tex = (mel.get_material_instance_texture_parameter_value(mat, name) if is_instance
               else mel.get_material_default_texture_parameter_value(mat, name))
        info["texture_params"][str(name)] = obj_path(tex)
    for name in mel.get_scalar_parameter_names(mat):
        info["scalar_params"][str(name)] = (mel.get_material_instance_scalar_parameter_value(mat, name) if is_instance
                                            else mel.get_material_default_scalar_parameter_value(mat, name))
    if is_instance:  # which values the instance itself overrides (the rest comes from its parent chain)
        try:
            info["scalar_overrides"] = {str(v.get_editor_property("parameter_info").get_editor_property("name")):
                                        float(v.get_editor_property("parameter_value"))
                                        for v in mat.get_editor_property("scalar_parameter_values")}
        except Exception as exc:
            info["scalar_overrides_error"] = str(exc)
    for name in mel.get_vector_parameter_names(mat):
        v = (mel.get_material_instance_vector_parameter_value(mat, name) if is_instance
             else mel.get_material_default_vector_parameter_value(mat, name))
        info["vector_params"][str(name)] = [v.r, v.g, v.b, v.a]
    # static switches (StaticBoolParameter / StaticSwitchParameter): the instance's value, else the default
    info["static_switch_params"] = {}
    try:
        for name in mel.get_static_switch_parameter_names(mat):
            info["static_switch_params"][str(name)] = bool(
                mel.get_material_instance_static_switch_parameter_value(mat, name) if is_instance
                else mel.get_material_default_static_switch_parameter_value(mat, name))
    except Exception as exc:
        info["static_switch_error"] = str(exc)
    try:
        info["used_textures"] = [obj_path(t) for t in mel.get_material_used_textures(mat)]
    except Exception:
        pass
    # Material graph: which texture (and which channel) feeds which material output. This is the ground truth
    # for texture roles; the names are only a fallback.
    info["graph"] = {}
    for prop_name, prop in GRAPH_PROPERTIES:
        try:
            node = mel.get_material_property_input_node(base, prop)
        except Exception:
            node = None
        if node is None:
            continue
        try:
            out_name = mel.get_material_property_input_node_output_name(base, prop)
        except Exception:
            out_name = ""
        info["graph"][prop_name] = walk_expression(base, node, str(out_name), info["texture_params"], 0)
    return info


GRAPH_PROPERTIES = [(n, getattr(unreal.MaterialProperty, "MP_" + n.upper(), None)) for n in (
    "base_color", "normal", "roughness", "metallic", "specular", "ambient_occlusion", "emissive_color", "opacity",
    "opacity_mask", "subsurface_color")]
GRAPH_PROPERTIES = [(n, p) for n, p in GRAPH_PROPERTIES if p is not None]


def walk_expression(material, expr, out_name, texture_overrides, depth):
    """Describe a material expression and its inputs (recursively, depth-limited)."""
    mel = unreal.MaterialEditingLibrary
    d = {"node": expr.get_class().get_name().replace("MaterialExpression", ""), "output": out_name}
    for prop in ("texture", "parameter_name", "r", "constant", "default_value", "const_a", "const_b", "const_alpha",
                 "sampler_type", "coordinate_index", "u_tiling", "v_tiling", "const_exponent", "material_function",
                 "default_value_a", "default_value_b"):
        try:
            v = expr.get_editor_property(prop)
        except Exception:
            continue
        if isinstance(v, unreal.Object):
            v = obj_path(v)
        elif isinstance(v, unreal.LinearColor):
            v = [v.r, v.g, v.b, v.a]
        elif not isinstance(v, (int, float, str, bool)):
            v = str(v)
        d[prop] = v
    if d.get("parameter_name") and str(d["parameter_name"]) in texture_overrides:
        d["texture"] = texture_overrides[str(d["parameter_name"])]
    if depth >= 8:
        return d
    try:
        inputs = mel.get_inputs_for_material_expression(material, expr)
        names = mel.get_material_expression_input_names(expr)
    except Exception:
        return d
    children = []
    for i, child in enumerate(inputs):
        if child is None:
            continue
        try:
            child_out = str(mel.get_input_node_output_name_for_material_expression(expr, child))
        except Exception:
            child_out = ""
        c = walk_expression(material, child, child_out, texture_overrides, depth + 1)
        c["input"] = str(names[i]) if i < len(names) else str(i)
        children.append(c)
    if children:
        d["inputs"] = children
    return d


def texture_file_stems(tex_paths):
    """Png stem per texture asset path: the asset name, or <name>__<folder> when two assets share a name (a pack with
    texture/set_1, texture/set_2, ... folders holding equally named textures)."""
    names = {}
    for tp in tex_paths:
        names.setdefault(tp.split(".")[-1], []).append(tp)
    stems = {}
    for name, group in names.items():
        for tp in group:
            folder = tp.rsplit("/", 2)[-2] if tp.count("/") >= 2 else ""
            stems[tp] = name if len(group) == 1 else "%s__%s" % (name, folder)
        taken = [stems[tp] for tp in group]
        if len(set(taken)) != len(taken):  # same name in equally named folders of different packs
            for index, tp in enumerate(sorted(group)):
                stems[tp] = "%s__%d" % (name, index + 1)
    return stems


def graph_textures(graph):
    found = set()

    def visit(n):
        if n.get("texture"):
            found.add(n["texture"])
        for c in n.get("inputs", []):
            visit(c)

    for n in graph.values():
        visit(n)
    return found


def export(obj, filename, exporter=None, options=None):
    task = unreal.AssetExportTask()
    task.object = obj
    task.filename = filename
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    if exporter:
        task.exporter = exporter
    if options:
        task.options = options
    ok = unreal.Exporter.run_asset_export_task(task)
    if not ok:
        unreal.log_warning("Export failed: %s -> %s %s" % (obj_path(obj), filename, list(task.errors)))
    return bool(ok)


def fbx_options():
    opt = unreal.FbxExportOption()
    opt.set_editor_property("collision", False)
    opt.set_editor_property("level_of_detail", False)  # LOD0 only
    opt.set_editor_property("export_morph_targets", True)
    opt.set_editor_property("vertex_color", True)
    opt.set_editor_property("export_preview_mesh", False)
    # Export the imported source geometry: the render-data path goes through a skinned mesh component and asserts
    # (MeshObject == nullptr) under -nullrhi.
    opt.set_editor_property("export_source_mesh", True)
    opt.set_editor_property("map_skeletal_motion_to_root", False)
    # Material baking renders the mesh (MeshMergeUtilities) and asserts under -nullrhi: never bake on export.
    try:
        opt.set_editor_property("bake_material_inputs", unreal.FbxMaterialBakeMode.DISABLED)
    except Exception as exc:
        unreal.log_warning("bake_material_inputs not settable: %s" % exc)
    return opt


def main():
    args = parse_args()
    out_src = os.path.join(args.out, "01_Source")
    out_tex = os.path.join(args.out, "02_Textures_Source")
    os.makedirs(out_src, exist_ok=True)
    os.makedirs(out_tex, exist_ok=True)

    mesh = unreal.load_asset(args.mesh)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("Not a skeletal mesh: %s" % args.mesh)
    sms = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    mesh_name = mesh.get_name()
    skeleton = mesh.skeleton

    inv = {
        "mesh": obj_path(mesh),
        "name": mesh_name,
        "skeleton": obj_path(skeleton),
        "physics_asset": obj_path(mesh.physics_asset),
        "bounds_extent": None,
        "lods": [],
        "slots": [],
        "materials": {},
        "textures": {},
        "morph_targets": [str(n) for n in mesh.get_all_morph_target_names()],
        "referencers": [],
        "skeleton_animations": [],
        "exports": {"mesh_fbx": None, "textures": {}, "animations": {}},
        "notes": "triangle counts per slot are filled in by blender_clean.py (FBX section counts)",
    }
    try:
        b = mesh.get_bounds()
        inv["bounds_extent"] = [b.box_extent.x, b.box_extent.y, b.box_extent.z]
    except Exception:
        pass

    lod_count = sms.get_lod_count(mesh)
    for lod in range(lod_count):
        inv["lods"].append({"lod": lod, "num_verts": sms.get_num_verts(mesh, lod),
                            "num_sections": sms.get_num_sections(mesh, lod)})
    # slot index used by each LOD0 section
    section_slots = []
    for s in range(sms.get_num_sections(mesh, 0) if lod_count else 0):
        try:
            section_slots.append(sms.get_lod_material_slot(mesh, 0, s))
        except Exception:
            section_slots.append(None)
    inv["lod0_section_material_slots"] = section_slots

    for i, sm in enumerate(mesh.materials):
        mat = sm.get_editor_property("material_interface")
        slot = {"index": i, "slot_name": str(sm.get_editor_property("material_slot_name")),
                "imported_slot_name": str(sm.get_editor_property("imported_material_slot_name")),
                "material": obj_path(mat)}
        inv["slots"].append(slot)
        if mat and obj_path(mat) not in inv["materials"]:
            inv["materials"][obj_path(mat)] = material_info(mat)

    # Look variants (enemy texture sets): slot name -> material; slots a set does not list keep the mesh's material.
    inv["variants"] = []
    if args.material_sets_file:
        with open(args.material_sets_file, "r", encoding="utf-8") as f:
            sets = json.load(f)
        slot_names = [s["slot_name"] for s in inv["slots"]]
        for index, entry in enumerate(sets):
            # [{slot: material}, ...] or [{"name": "Set2", "slots": {slot: material}}, ...]
            mapping = entry.get("slots", entry) if isinstance(entry, dict) else {}
            vname = entry.get("name") if isinstance(entry, dict) and "slots" in entry else "Set%d" % (index + 1)
            resolved = {}
            for slot in inv["slots"]:
                mpath = mapping.get(slot["slot_name"]) or slot["material"]
                resolved[slot["slot_name"]] = mpath
                mat = unreal.load_asset(mpath) if mpath else None
                if mat and mpath not in inv["materials"]:
                    inv["materials"][mpath] = material_info(mat)
            inv["variants"].append({"name": vname, "slots": resolved,
                                    "unknown_slots": sorted(k for k in mapping if k not in slot_names)})

    inv["texture_files"] = {}  # texture asset path -> png stem in 02_Textures_Source (filled below)
    tex_paths = set()
    for m in inv["materials"].values():
        tex_paths.update(p for p in m["texture_params"].values() if p)
        tex_paths.update(p for p in m["used_textures"] if p)
        tex_paths.update(graph_textures(m.get("graph", {})))
    stems = texture_file_stems(tex_paths)
    for tp in sorted(tex_paths):
        tex = unreal.load_asset(tp)
        if not tex:
            continue
        info = texture_info(tex)
        inv["textures"][tp] = info
        inv["texture_files"][tp] = stems[tp]
        if isinstance(tex, unreal.Texture2D) and not args.skip_textures:
            fn = os.path.join(out_tex, stems[tp] + ".png")
            if export(tex, fn, unreal.TextureExporterPNG()):
                inv["exports"]["textures"][tp] = os.path.relpath(fn, args.out).replace("\\", "/")

    # Who uses the mesh (Blueprints, other assets).
    pkg = mesh.get_outermost().get_name()
    try:
        opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                     include_hard_package_references=True)
        inv["referencers"] = sorted(str(r) for r in registry.get_referencers(pkg, opts) or [])
    except Exception as exc:
        inv["referencers_error"] = str(exc)

    # Animation sequences of the same skeleton (pose test candidates).
    anim_regex = re.compile(args.anim_regex, re.I) if args.anim_regex else None
    skel_path = obj_path(skeleton)
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "AnimSequence")],
                          package_paths=[args.anim_root], recursive_paths=True)
    exported_anims = 0
    for ad in registry.get_assets(flt):
        tag = str(ad.get_tag_value("Skeleton") or "")
        if skel_path.split(".")[0] not in tag:
            continue
        name = str(ad.asset_name)
        apath = str(ad.package_name) + "." + name
        inv["skeleton_animations"].append(apath)
        if anim_regex and anim_regex.search(name) and exported_anims < args.anim_max:
            anim = unreal.load_asset(apath)
            fn = os.path.join(out_src, "anims", name + ".fbx")
            os.makedirs(os.path.dirname(fn), exist_ok=True)
            opt = fbx_options()
            if export(anim, fn, unreal.AnimSequenceExporterFBX(), opt):
                inv["exports"]["animations"][apath] = os.path.relpath(fn, args.out).replace("\\", "/")
                exported_anims += 1
    inv["skeleton_animations"].sort()

    inv_path = os.path.join(out_src, "inventory.json")
    with open(inv_path, "w", encoding="utf-8") as f:  # written early: the FBX export below may assert
        json.dump(inv, f, indent=2)
    fbx = os.path.join(out_src, mesh_name + ".fbx")
    if args.skip_mesh and os.path.exists(fbx):  # inventory refresh: keep pointing at the FBX exported before
        inv["exports"]["mesh_fbx"] = os.path.relpath(fbx, args.out).replace("\\", "/")
    if not args.skip_mesh and export(mesh, fbx, unreal.SkeletalMeshExporterFBX(), fbx_options()):
        inv["exports"]["mesh_fbx"] = os.path.relpath(fbx, args.out).replace("\\", "/")

    with open(inv_path, "w", encoding="utf-8") as f:
        json.dump(inv, f, indent=2, sort_keys=False)
    unreal.log("PIPELINE_INVENTORY_DONE %s slots=%d textures=%d anims=%d" % (
        mesh_name, len(inv["slots"]), len(inv["textures"]), exported_anims))


main()
