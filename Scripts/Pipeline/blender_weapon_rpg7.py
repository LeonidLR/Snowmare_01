"""Weapon pipeline for the RPG-7 of the game designer's launcher pack (Blender, background; 2026-11-09, feat/rpg7-launcher-pack).

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_rpg7.py -- --asset-dir ArtPipeline/Weapons/RPG7

The pack (/Game/Launcher_Animations, the user's, read only; exported by run_unreal_export_weapon.ps1 into 01_Source/) already has ONE material
and ONE 4K texture set for every part, so unlike the pistol / D-1 there is no re-atlas and no bake: each part (weapon.json "parts": its source
skeletal meshes joined) is taken at its bind pose, welded, decimated (collapse) to its triangle budget with the artist's uv seams / open borders
shielded (the islands survive, so the resampled 1K maps still fit), smoothed by angle (45 deg) and exported as ONE static FBX with ONE slot
weapon.json "slot". Frame = the pack's weapon frame (SK_Weapon_Body_Mesh component space, cm: muzzle +Y, up +Z, the optic on +X), found by
fitting the imported boxes to the Unreal bounds in 01_Source/inventory.json, so the pack's socket transform mounts the parts unchanged.
Writes 05_Export/SM_<Asset>_<Part>.fbx + export_manifest.json, 06_Review/<Asset>_review.png (workbench, before | after) and summary.json.
The textures: Scripts/Pipeline/rpg7_textures.py (system Python + Pillow).
"""
import argparse
import itertools
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.dont_write_bytecode = True


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.01


def import_part(fbx):
    """The source skeletal mesh at its bind pose as plain mesh objects (world space, Blender axes, armatures removed)."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=fbx, use_anim=False, ignore_leaf_bones=True)
    new = [o for o in bpy.data.objects if o not in before]
    deps = bpy.context.evaluated_depsgraph_get()
    out = []
    for o in [o for o in new if o.type == "MESH"]:
        ev = o.evaluated_get(deps)
        me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=deps)
        me.transform(o.matrix_world)
        c = bpy.data.objects.new("HIGH_" + os.path.splitext(os.path.basename(fbx))[0], me)
        bpy.context.scene.collection.objects.link(c)
        out.append(c)
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    return out


def box(obj):
    vs = [v.co for v in obj.data.vertices]
    mn = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    mx = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    return (mn + mx) * 0.5, (mx - mn) * 0.5


def fit_mapping(pairs):
    """Signed axis permutation + uniform scale + offset taking the imported boxes onto the Unreal boxes (centre, extent per part)."""
    best = None
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            M = Matrix.Identity(3)
            for r in range(3):
                for c in range(3):
                    M[r][c] = signs[r] if perm[r] == c else 0.0
            ratios = []
            for (cb, eb), (cu, eu) in pairs:
                e = M @ eb
                for i in range(3):
                    if abs(e[i]) > 0.05:
                        ratios.append(eu[i] / abs(e[i]))
            if not ratios:
                continue
            ratios.sort()
            s = ratios[len(ratios) // 2]
            off = Vector((0, 0, 0))
            for (cb, eb), (cu, eu) in pairs:
                off += cu - s * (M @ cb)
            off /= len(pairs)
            err = 0.0
            for (cb, eb), (cu, eu) in pairs:
                err += (cu - (s * (M @ cb) + off)).length + sum(abs(abs(x) - y) for x, y in zip(s * (M @ eb), eu))
            if best is None or err < best[0]:
                best = (err, M.copy(), s, off.copy())
    return best


def shield_and_decimate(obj, target, seam_weight=0.05):
    """Weld, then collapse-decimate to `target` triangles with uv seams / open borders shielded (their collapse weight seam_weight)."""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.001)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-5)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.update()
    before = tri_count(obj)
    if target <= 0 or before <= target:
        return before, before
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    seam = set()
    for e in bm.edges:
        if len(e.link_faces) != 2:
            seam.update(v.index for v in e.verts)
            continue
        if uvl is None:
            continue
        f1, f2 = e.link_faces
        for v in e.verts:
            l1 = next(l for l in f1.loops if l.vert == v)
            l2 = next(l for l in f2.loops if l.vert == v)
            if (l1[uvl].uv - l2[uvl].uv).length > 1e-4:
                seam.update(x.index for x in e.verts)
                break
    bm.free()
    vg = obj.vertex_groups.new(name="free")
    vg.add(list(range(len(me.vertices))), 1.0, "REPLACE")
    if seam:
        vg.add(sorted(seam), seam_weight, "REPLACE")
    lo, hi = 0.0, 1.0
    ratio = target / before
    for _ in range(12):  # bisection on the ratio: the shielded vertices make the result miss the plain ratio
        mod = obj.modifiers.new("dec", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = ratio
        mod.vertex_group = "free"
        mod.vertex_group_factor = 1.0
        mod.use_collapse_triangulate = True
        deps = bpy.context.evaluated_depsgraph_get()
        n = sum(len(p.vertices) - 2 for p in obj.evaluated_get(deps).data.polygons)
        obj.modifiers.remove(mod)
        if abs(n - target) <= max(10, target * 0.02):
            break
        if n > target:
            hi = ratio
        else:
            lo = ratio
        ratio = (lo + hi) * 0.5
    mod = obj.modifiers.new("dec", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = ratio
    mod.vertex_group = "free"
    mod.vertex_group_factor = 1.0
    mod.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier="dec")
    obj.vertex_groups.clear()
    return before, tri_count(obj)


def smooth_by_angle(obj, deg=45.0):
    me = obj.data
    for p in me.polygons:
        p.use_smooth = True
    if hasattr(me, "set_sharp_from_angle"):
        me.set_sharp_from_angle(angle=math.radians(deg))


def make_material(slot, tex_path):
    mat = bpy.data.materials.new(slot)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    if tex_path and os.path.exists(tex_path):
        t = nt.nodes.new("ShaderNodeTexImage")
        t.image = bpy.data.images.load(tex_path, check_existing=True)
        nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


def render_review(objs_high, objs_low, out_png, tex_high, tex_low):
    """Workbench (texture colour) renders: side + top of the high parts (4K albedo) and the low parts (1K BaseColor), one strip."""
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 900, 420
    sc.render.film_transparent = False
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 125.0
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    cells = []
    for tag, objs, tex in (("high", objs_high, tex_high), ("low", objs_low, tex_low)):
        mat = make_material("rev_" + tag, tex)
        for o in bpy.data.objects:
            if o.type == "MESH":
                o.hide_render = o not in objs
        for o in objs:
            o.data.materials.clear()
            o.data.materials.append(mat)
        # Blender export space: muzzle along -Y (UE +Y), up +Z
        for view, loc, rot in (("side", (-200, -10, 0), (math.radians(90), 0, math.radians(-90))),
                               ("top", (0, -10, 200), (0, 0, math.radians(90)))):
            cam.location = loc
            cam.rotation_euler = rot
            path = out_png.replace(".png", f"_{tag}_{view}.png")
            sc.render.filepath = path
            bpy.ops.render.render(write_still=True)
            cells.append(path)
    return cells


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    ap.add_argument("--no-review", action="store_true")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.asset_dir)
    cfg = json.load(open(os.path.join(root, "weapon.json"), encoding="utf-8"))
    inv = json.load(open(os.path.join(root, "01_Source", "inventory.json"), encoding="utf-8"))
    asset, slot = cfg["asset"], cfg.get("slot", "M_" + cfg["asset"])
    export_dir = os.path.join(root, "05_Export")
    review_dir = os.path.join(root, "06_Review")
    os.makedirs(export_dir, exist_ok=True)
    os.makedirs(review_dir, exist_ok=True)
    reset_scene()
    ue_boxes = {m["name"]: (Vector(m["bounds_origin"]), Vector(m["bounds_extent"])) for m in inv["meshes"]}
    high = {}
    for name in sorted(ue_boxes):
        fbx = os.path.join(root, "01_Source", name + ".fbx")
        objs = import_part(fbx)
        if objs:
            bpy.context.view_layer.objects.active = objs[0]
            for o in objs[1:]:
                o.select_set(True)
            if len(objs) > 1:
                objs[0].select_set(True)
                bpy.ops.object.join()
            high[name] = bpy.context.view_layer.objects.active if len(objs) > 1 else objs[0]
    pairs = [(box(o), ue_boxes[n]) for n, o in high.items() if n in ue_boxes]
    err, M3, scale, off = fit_mapping(pairs)
    # imported Blender coords -> Unreal mesh coords (cm) -> Blender export space (x, -y, z; the FBX export below maps it back with forward -Y)
    to_ue = Matrix.Translation(off) @ (M3.to_4x4() @ Matrix.Scale(scale, 4))
    to_export = Matrix(((1, 0, 0, 0), (0, -1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))
    for o in high.values():
        o.data.transform(to_export @ to_ue)
        o.data.update()
    summary = {"mapping": {"error": round(err, 3), "scale": round(scale, 4), "axes": [list(r) for r in M3], "offset": list(off)}, "parts": {}}
    export_kwargs = dict(
        use_selection=True, object_types={"MESH"}, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
        global_scale=1.0, axis_forward="-Y", axis_up="Z", use_space_transform=True, bake_space_transform=False, use_mesh_modifiers=False,
        mesh_smooth_type="EDGE", use_tspace=True, use_custom_props=False, add_leaf_bones=False, path_mode="STRIP", embed_textures=False,
        use_triangles=True)
    mat = bpy.data.materials.new(slot)
    lows, highs_for_review = [], []
    for part, spec in cfg["parts"].items():
        srcs = [high[s] for s in spec["src"] if s in high]
        if not srcs:
            summary["parts"][part] = {"error": "no source"}
            continue
        copies = []
        for s in srcs:
            c = s.copy()
            c.data = s.data.copy()
            bpy.context.scene.collection.objects.link(c)
            copies.append(c)
            highs_for_review.append(s)
        bpy.ops.object.select_all(action="DESELECT")
        for c in copies:
            c.select_set(True)
        bpy.context.view_layer.objects.active = copies[0]
        if len(copies) > 1:
            bpy.ops.object.join()
        low = bpy.context.view_layer.objects.active
        name = f"SM_{asset}_{part}"
        low.name = name
        low.data.name = name
        for uv in list(low.data.uv_layers)[1:]:
            low.data.uv_layers.remove(uv)
        for a in list(low.data.color_attributes):
            low.data.color_attributes.remove(a)
        src_tris = sum(tri_count(s) for s in srcs)
        welded, after = shield_and_decimate(low, int(spec.get("target_tris", 0)))
        smooth_by_angle(low)
        low.data.materials.clear()
        low.data.materials.append(mat)
        for p in low.data.polygons:
            p.material_index = 0
        bpy.ops.object.select_all(action="DESELECT")
        low.select_set(True)
        bpy.context.view_layer.objects.active = low
        path = os.path.join(export_dir, name + ".fbx")
        bpy.ops.export_scene.fbx(filepath=path, **export_kwargs)
        vs = [v.co for v in low.data.vertices]
        mn = [min(v[i] for v in vs) for i in range(3)]
        mx = [max(v[i] for v in vs) for i in range(3)]
        # report in Unreal mesh coords (y flipped back)
        ue_min = [mn[0], -mx[1], mn[2]]
        ue_max = [mx[0], -mn[1], mx[2]]
        summary["parts"][part] = {"fbx": os.path.basename(path), "sources": spec["src"], "tris_source": src_tris, "tris_welded": welded,
                                  "tris": after, "target": spec.get("target_tris"),
                                  "bounds_min_ue_cm": [round(v, 2) for v in ue_min], "bounds_max_ue_cm": [round(v, 2) for v in ue_max]}
        lows.append(low)
        print(f"RPG7_PART {part}: {src_tris} -> {after} tris (target {spec.get('target_tris')})")
    json.dump({"material_slot": slot, "frame": "pack weapon frame (cm): muzzle +Y, up +Z, the optic on +X", "parts": summary["parts"]},
              open(os.path.join(export_dir, "export_manifest.json"), "w"), indent=2)
    if not args.no_review:
        tex_high = os.path.join(root, "02_Textures_Source", "T_Launcher_Weapon_Albedo.png")
        tex_low = os.path.join(root, "04_Textures_Baked", f"T_{asset}_BaseColor.png")
        main_parts = [o for o in lows if o.name.endswith(("_Body", "_Sight01", "_Rocket"))]
        main_high = [high[n] for n in ("SK_Weapon_Body_Mesh", "SK_Weapon_Sight_01_Mesh", "SK_Weapon_Glasses_Mesh", "SK_Weapon_Rocket_Mesh") if n in high]
        summary["review_cells"] = render_review(main_high, main_parts, os.path.join(review_dir, f"{asset}_review.png"), tex_high, tex_low)
    old = {}
    sp = os.path.join(review_dir, "summary.json")
    if os.path.exists(sp):
        old = json.load(open(sp, encoding="utf-8"))
    old["blender"] = summary
    json.dump(old, open(sp, "w"), indent=2)
    print("RPG7_DONE")


main()
