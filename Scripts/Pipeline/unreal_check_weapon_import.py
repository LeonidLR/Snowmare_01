"""Weapon pipeline: import-check of the exported FBX files into a THROW-AWAY scratch project (never the game project).

Run with UnrealEditor-Cmd -run=pythonscript against a content-only scratch project (see docs/pipeline/WEAPON_SINGLE_MATERIAL.md);
arguments as JSON in CODEX_PIPELINE_ARGS:
    {"out": "<json report path>", "dest": "/Game/WeaponCheck",
     "items": [{"fbx": "<path>", "kind": "static"|"skeletal", "skeleton": "/Game/.../Skeleton" (optional)}]}
Reports per item (UE axes, centimetres): bounds origin / extent, vertex / triangle numbers, material slots, uv channels,
skeletal: bone names. Used to verify scale, pivot and axis orientation before the README import notes are written.
"""
import json
import os
import sys

import unreal


def main():
    args = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    results = []
    for item in args["items"]:
        name = os.path.splitext(os.path.basename(item["fbx"]))[0]
        task = unreal.AssetImportTask()
        task.filename = item["fbx"]
        task.destination_path = args["dest"]
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = False
        ui = unreal.FbxImportUI()
        ui.set_editor_property("import_mesh", True)
        ui.set_editor_property("import_textures", False)
        ui.set_editor_property("import_materials", False)
        ui.set_editor_property("import_animations", False)
        if item["kind"] == "skeletal":
            ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
            if item.get("skeleton"):
                ui.set_editor_property("skeleton", unreal.load_asset(item["skeleton"]))
        else:
            ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
            ui.static_mesh_import_data.set_editor_property("combine_meshes", True)
        task.options = ui
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        res = {"name": name, "fbx": item["fbx"], "imported": [str(p) for p in task.imported_object_paths]}
        for p in task.imported_object_paths:
            a = unreal.load_asset(str(p))
            if a is None:
                continue
            cls = a.get_class().get_name()
            if cls not in ("StaticMesh", "SkeletalMesh"):
                continue
            b = a.get_bounds()
            res["class"] = cls
            res["bounds_origin"] = [round(b.origin.x, 3), round(b.origin.y, 3), round(b.origin.z, 3)]
            res["bounds_extent"] = [round(b.box_extent.x, 3), round(b.box_extent.y, 3), round(b.box_extent.z, 3)]
            if cls == "StaticMesh":
                res["vertices"] = unreal.EditorStaticMeshLibrary.get_number_verts(a, 0)
                res["slots"] = [str(s.material_slot_name) for s in a.static_materials]
                res["uv_channels"] = unreal.EditorStaticMeshLibrary.get_num_uv_channels(a, 0)
                try:
                    res["triangles"] = a.get_num_triangles(0) if hasattr(a, "get_num_triangles") else None
                except Exception:  # noqa: BLE001
                    res["triangles"] = None
            else:
                res["slots"] = [str(s.get_editor_property("material_slot_name")) for s in a.get_editor_property("materials")]
                skel = a.get_editor_property("skeleton")
                res["skeleton"] = skel.get_path_name() if skel else None
                try:
                    res["bones"] = [str(n) for n in unreal.EditorSkeletalMeshLibrary.get_all_bone_names(a)]
                except Exception:  # noqa: BLE001
                    res["bones"] = None
        results.append(res)
        unreal.log("WEAPON_CHECK " + json.dumps(res))
    with open(args["out"], "w") as f:
        json.dump(results, f, indent=2)
    unreal.log("WEAPON_CHECK_DONE %d" % len(results))


main()
