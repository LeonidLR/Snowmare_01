"""Character single-slot pipeline: extra look variants (texture sets) for an asset that is ALREADY imported.

    blender --background --factory-startup --python Scripts/Pipeline/blender_variant_bake.py -- --asset-dir <...>
        --variants Set2,Set3 [--res 4096] [bake options]

The imported mesh must stay unchanged, so the bake target is the exported low mesh itself (05_Export/SK_<Asset>_1slot.fbx,
its UV_Atlas) and the source is the original body (01_Source FBX, re-exported from Unreal with the variants in the inventory).
Every low triangle takes the material of the nearest high triangle (the per-material projection needs it), then
blender_bake.py runs on that set-up for the named variants only: T_<Asset>_<Variant>_*.png next to the existing maps.
"""
import argparse
import os
import subprocess
import sys

import bpy
from mathutils.bvhtree import BVHTree

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402
import material_graph as mg  # noqa: E402


def parse():
    argv = pc.script_args()
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--variants", required=True, help="comma separated variant names from the inventory")
    args, rest = p.parse_known_args(argv)
    return args, rest


def main():
    args, bake_args = parse()
    paths = pc.Paths(args.asset_dir)
    inv = paths.inventory
    wanted = [v.strip() for v in args.variants.split(",") if v.strip()]
    known = [v["name"] for v in inv.get("variants") or []]
    missing = [v for v in wanted if v not in known]
    if missing:
        raise RuntimeError("variants %s not in the inventory (%s)" % (missing, known))
    pc.reset_scene()
    # low: the exported (= imported) mesh
    arm_low, low_meshes, others = pc.import_fbx(paths.export_fbx)
    low = low_meshes[0]
    low.name = paths.mesh_name
    for o in others:
        bpy.data.objects.remove(o)
    # high: the original body with its Unreal materials
    arm_high, high_meshes, others = pc.import_fbx(paths.source_fbx)
    for o in others:
        for child in list(o.children):
            mw = child.matrix_world.copy()
            child.parent = None
            child.matrix_world = mw
        bpy.data.objects.remove(o)
    high = high_meshes[0]
    high.name = paths.high_name
    # the FBX images point at the vendor's drive (F:\...): drop them all, the materials are rebuilt from the exported PNGs
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    import blender_clean
    warnings = blender_clean.rebuild_materials(high, inv, paths)
    # hair / glass are not bake sources (removed from the optimized body too)
    import re
    hair_re = re.compile(pc.HAIR_REGEX_DEFAULT, re.I)
    drop = set()
    for i, slot in enumerate(high.material_slots):
        mpath = mg.material_of(inv, slot.material.name)
        blend = str(inv["materials"].get(mpath or "", {}).get("blend_mode", ""))
        if hair_re.search(slot.material.name) or any(k in blend for k in ("TRANSLUCENT", "ADDITIVE", "MODULATE")):
            drop.add(i)
    if drop:
        blender_clean.delete_faces(high, {p.index for p in high.data.polygons if p.material_index in drop})
    # low triangles take the nearest high triangle's material
    hm = high.data
    bvh = BVHTree.FromPolygons([high.matrix_world @ v.co for v in hm.vertices], [tuple(p.vertices) for p in hm.polygons])
    low.data.materials.clear()
    for slot in high.material_slots:
        low.data.materials.append(slot.material)
    far = 0
    for poly in low.data.polygons:
        loc, _n, index, dist = bvh.find_nearest(low.matrix_world @ poly.center)
        poly.material_index = hm.polygons[index].material_index if index is not None else 0
        far += 1 if (dist or 0) > 2.0 else 0
    print("[pipeline] low faces mapped to high materials; %d farther than 2 cm" % far)
    if "UV_Atlas" not in low.data.uv_layers:
        raise RuntimeError("the exported mesh has no UV_Atlas")
    low.data.uv_layers.active = low.data.uv_layers["UV_Atlas"]
    variant_blend = os.path.join(paths.blend, paths.mesh_name + "_variants.blend")
    bpy.ops.wm.save_as_mainfile(filepath=variant_blend, compress=True)
    pc.update_summary(paths, "variant_bake_setup", {"variants": wanted, "low_faces": len(low.data.polygons),
                                                    "faces_far_from_high": far, "warnings": warnings[:20]})
    # the regular bake on this set-up, only the named variants, every material projected (the low has no source uv)
    cmd = [bpy.app.binary_path, "--background", "--factory-startup", "--python-exit-code", "3", "--python",
           os.path.join(os.path.dirname(os.path.abspath(__file__)), "blender_bake.py"), "--",
           "--asset-dir", paths.root, "--input-blend", variant_blend, "--only-variants", ",".join(wanted),
           "--project-all", "--no-save"] + bake_args
    print("[pipeline] running", " ".join(cmd))
    code = subprocess.call(cmd)
    if code != 0:
        raise RuntimeError("blender_bake.py failed (%d)" % code)
    print("[pipeline] VARIANT_BAKE_DONE %s" % ",".join(wanted))


main()
