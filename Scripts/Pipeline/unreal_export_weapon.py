"""Weapon pipeline, Unreal side (READ-ONLY): inventory + FBX / texture export of the meshes in a content folder.

Writes <out>/01_Source/<Mesh>.fbx, <out>/01_Source/inventory.json (slots, materials, expression graph summary,
texture usage, bounds, skeleton + bones) and <out>/02_Textures_Source/<Texture>.png for every referenced texture.
Never saves or modifies an asset (the Fab packs are the user's).

Run through Scripts/Pipeline/run_unreal_export_weapon.ps1 (arguments as JSON in CODEX_PIPELINE_ARGS: paths with
spaces do not survive -script=):  ["--folder", "/Game/Fab/...", "--out", "<asset folder>"]

Docs: docs/pipeline/WEAPON_SINGLE_MATERIAL.md
"""
import argparse
import json
import os
import re
import sys

import unreal


def parse_args():
    argv = sys.argv[1:]
    if not argv and os.environ.get("CODEX_PIPELINE_ARGS"):
        argv = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    p = argparse.ArgumentParser()
    p.add_argument("--folder", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--meshes", default="", help="regex on asset names (default: every mesh)")
    return p.parse_args([a.strip('"') for a in argv])


def vec(v):
    return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]


def safe(fn, default=None):
    try:
        return fn()
    except Exception as exc:  # noqa: BLE001
        return default if default is not None else "ERR: %s" % exc


def graph_summary(mat):
    """Per material output: the expression that feeds it (constants / texture samples / parameters)."""
    out = {}
    out["textures"] = [t.get_path_name() for t in safe(lambda: unreal.MaterialEditingLibrary.get_used_textures(mat), [])]
    for prop in ("BASE_COLOR", "METALLIC", "ROUGHNESS", "EMISSIVE_COLOR", "NORMAL", "OPACITY", "SPECULAR"):
        def probe():
            node = unreal.MaterialEditingLibrary.get_material_property_input_node(mat, getattr(unreal.MaterialProperty, "MP_" + prop))
            if not node:
                return None
            d = {"class": node.get_class().get_name()}
            for attr in ("constant", "r", "g", "b", "parameter_name"):
                v = safe(lambda: node.get_editor_property(attr), None)
                if v is not None:
                    d[attr] = str(v)
            return d
        out[prop.lower()] = safe(probe, None)
    return out


def main():
    args = parse_args()
    out = args.out
    src_dir = os.path.join(out, "01_Source")
    tex_dir = os.path.join(out, "02_Textures_Source")
    os.makedirs(src_dir, exist_ok=True)
    os.makedirs(tex_dir, exist_ok=True)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = reg.get_assets_by_path(args.folder, recursive=True)
    inv = {"folder": args.folder, "meshes": [], "materials": {}, "animations": []}
    rx = re.compile(args.meshes) if args.meshes else None
    textures = set()
    for ad in assets:
        cls = str(ad.asset_class_path.asset_name)
        name = str(ad.asset_name)
        if cls == "AnimSequence":
            inv["animations"].append(name)
        if cls not in ("SkeletalMesh", "StaticMesh"):
            continue
        if rx and not rx.search(name):
            continue
        mesh = ad.get_asset()
        info = {"name": name, "class": cls, "path": mesh.get_path_name()}
        slots = []
        if cls == "SkeletalMesh":
            b = mesh.get_bounds()
            info["bounds_origin"] = vec(b.origin)
            info["bounds_extent"] = vec(b.box_extent)
            skel = mesh.get_editor_property("skeleton")
            info["skeleton"] = skel.get_path_name() if skel else None
            info["bones"] = [str(n) for n in safe(lambda: unreal.EditorSkeletalMeshLibrary.get_all_bone_names(mesh), [])] if hasattr(unreal.EditorSkeletalMeshLibrary, "get_all_bone_names") else []
            info["num_lods"] = safe(lambda: unreal.EditorSkeletalMeshLibrary.get_lod_count(mesh))
            for slot in mesh.get_editor_property("materials"):
                mi = slot.get_editor_property("material_interface")
                slots.append((str(slot.get_editor_property("material_slot_name")), mi))
        else:
            b = mesh.get_bounds()
            info["bounds_origin"] = vec(b.origin)
            info["bounds_extent"] = vec(b.box_extent)
            info["num_triangles_lod0"] = safe(lambda: unreal.EditorStaticMeshLibrary.get_number_verts(mesh, 0))
            for s in mesh.static_materials:
                slots.append((str(s.material_slot_name), s.material_interface))
        info["slots"] = []
        for sname, mi in slots:
            info["slots"].append({"slot": sname, "material": mi.get_path_name() if mi else None})
            if mi and mi.get_path_name() not in inv["materials"]:
                base = safe(lambda: mi.get_base_material(), None) if hasattr(mi, "get_base_material") else mi
                entry = {"class": mi.get_class().get_name()}
                if base and not isinstance(base, str):
                    entry.update(graph_summary(base))
                    for t in entry.get("textures", []):
                        textures.add(t)
                if isinstance(mi, unreal.MaterialInstanceConstant):
                    entry["vector_params"] = {str(p.parameter_info.name): [p.parameter_value.r, p.parameter_value.g, p.parameter_value.b, p.parameter_value.a] for p in mi.get_editor_property("vector_parameter_values")}
                    entry["scalar_params"] = {str(p.parameter_info.name): p.parameter_value for p in mi.get_editor_property("scalar_parameter_values")}
                    tp = {}
                    for p in mi.get_editor_property("texture_parameter_values"):
                        tp[str(p.parameter_info.name)] = p.parameter_value.get_path_name() if p.parameter_value else None
                        if p.parameter_value:
                            textures.add(p.parameter_value.get_path_name())
                    entry["texture_params"] = tp
                inv["materials"][mi.get_path_name()] = entry
        task = unreal.AssetExportTask()
        task.object = mesh
        task.filename = os.path.join(src_dir, name + ".fbx")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        opt = unreal.FbxExportOption()
        opt.set_editor_property("vertex_color", True)
        opt.set_editor_property("level_of_detail", False)
        opt.set_editor_property("collision", False)
        task.options = opt
        ok = unreal.Exporter.run_asset_export_task(task)
        info["fbx"] = task.filename
        info["export_ok"] = bool(ok)
        inv["meshes"].append(info)
        unreal.log("PIPELINE_EXPORTED %s (%s) ok=%s" % (name, cls, ok))
    inv["textures"] = sorted(textures)
    for tpath in sorted(textures):
        tex = unreal.load_asset(tpath)
        if not tex:
            continue
        task = unreal.AssetExportTask()
        task.object = tex
        task.filename = os.path.join(tex_dir, str(tex.get_name()) + ".png")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        unreal.Exporter.run_asset_export_task(task)
    with open(os.path.join(src_dir, "inventory.json"), "w") as f:
        json.dump(inv, f, indent=2, default=str)
    unreal.log("PIPELINE_INVENTORY_DONE %d meshes" % len(inv["meshes"]))


main()
