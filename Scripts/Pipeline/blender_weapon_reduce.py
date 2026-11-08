"""Weapon pipeline, stage 2 (Blender, background): reduce every part to its triangle budget and lay out ONE 1K atlas.

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_reduce.py -- --asset-dir <ArtPipeline/Weapons/X>

Reads 03_Blender/<Asset>_high.blend (stage 1) and weapon.json {"res", "margin_px", "parts": {name: {"target_tris",
"weight", "protect_below_tris"}}}. Non-destructive: the high objects are kept (hidden) next to the new low objects in
03_Blender/<Asset>_low.blend.

Per part: duplicates of the part's high objects are joined, welded (0.002 cm), and decimated (collapse) by bisection to the
triangle budget; sub-objects smaller than `protect_below_tris` (trigger, sights, pins ...) are shielded from the collapse
through a vertex group. Normals: smooth + sharp edges above `sharp_angle` (the bake and the export use the same frame).
Atlas: Smart UV Project per part, islands scaled to the part's texel-density weight, all parts packed together in one UV
map `UV_Atlas`.
"""
import argparse
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_common as wc  # noqa: E402


def part_high_objects(part):
    return [o for o in bpy.data.objects if o.type == "MESH" and o.get("part") == part and not o.name.startswith("LOW__")]


def build_joined(part, protect_below, seam_weight, dissolve_deg=1.0):
    """Join copies of the part's high objects into one mesh `LOW__<part>` (no materials / uv / custom normals)."""
    copies = []
    for o in part_high_objects(part):
        me = o.data.copy()
        n = sum(len(p.vertices) - 2 for p in me.polygons)
        c = bpy.data.objects.new("tmp_" + o.name, me)
        bpy.context.scene.collection.objects.link(c)
        me.materials.clear()
        for uv in list(me.uv_layers)[1:]:
            me.uv_layers.remove(uv)
        for a in list(me.color_attributes):
            me.color_attributes.remove(a)
        vg = c.vertex_groups.new(name="free")
        # decimate collapses HIGH weights first: 1 = free, 0 = protected
        vg.add(list(range(len(me.vertices))), 0.0 if n < protect_below else 1.0, "REPLACE")
        copies.append(c)
    bpy.ops.object.select_all(action="DESELECT")
    for c in copies:
        c.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    low = bpy.context.view_layer.objects.active
    low.name = "LOW__" + part
    low.data.name = "LOW__" + part
    if low.data.has_custom_normals:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    bm = bmesh.new()
    bm.from_mesh(low.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.002)
    bmesh.ops.dissolve_degenerate(bm, edges=bm.edges, dist=1e-5)
    # flat areas (the pack's meshes are dense triangle soups on flat sides) carry no shape: dissolve them first so the
    # collapse spends its budget on the curves; uv islands and materials are respected
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(dissolve_deg), verts=bm.verts, edges=bm.edges, delimit={"UV", "MATERIAL"})
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(low.data)
    bm.free()
    low.data.update()
    shield_seams(low, seam_weight)
    return low


def shield_seams(low, seam_weight):
    """Lower the collapse weight of vertices on uv seams / open borders so the artist's islands survive the decimation."""
    me = low.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    seam = set()
    for e in bm.edges:
        if len(e.link_faces) != 2:
            seam.update(v.index for v in e.verts)
            continue
        f1, f2 = e.link_faces
        for v in e.verts:
            l1 = next(l for l in f1.loops if l.vert == v)
            l2 = next(l for l in f2.loops if l.vert == v)
            if (l1[uvl].uv - l2[uvl].uv).length > 1e-4:
                seam.update(x.index for x in e.verts)
                break
    bm.free()
    vg = low.vertex_groups["free"]
    for i in seam:
        try:
            w = vg.weight(i)
        except RuntimeError:
            w = 1.0
        vg.add([i], min(w, seam_weight), "REPLACE")



def dominant_groups(obj):
    """Per vertex: index of the vertex group with the biggest weight (-1 when ungrouped)."""
    out = []
    for v in obj.data.vertices:
        out.append(max(v.groups, key=lambda g: g.weight).group if v.groups else -1)
    return out


def build_skinned(part, protect_below, seam_weight):
    """Skinned part (armature + rigid bone weights): one copy of the high object, weights / hierarchy kept.
    The 'free' group shields (a) vertices where the dominant bone changes (a joint between rigid pieces), (b) every
    vertex of a bone that owns fewer than `protect_below` triangles (small details), (c) uv seams (see shield_seams)."""
    o = part_high_objects(part)[0]
    c = o.copy()
    c.data = o.data.copy()
    bpy.context.scene.collection.objects.link(c)
    c.name = "LOW__" + part
    for key in ("part", "src_mat", "src_mats"):  # the copy is a LOW object: it must not look like a high one
        if key in c:
            del c[key]
    me = c.data
    me.materials.clear()
    for uv in list(me.uv_layers)[1:]:
        me.uv_layers.remove(uv)
    for a in list(me.color_attributes):
        me.color_attributes.remove(a)
    if me.has_custom_normals:
        bpy.context.view_layer.objects.active = c
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    dom = dominant_groups(c)
    tri_by_group = {}
    for p in me.polygons:
        g = max(set(dom[i] for i in p.vertices), key=lambda x: sum(1 for i in p.vertices if dom[i] == x))
        tri_by_group[g] = tri_by_group.get(g, 0) + len(p.vertices) - 2
    boundary = set()
    for e in me.edges:
        a, b = e.vertices
        if dom[a] != dom[b]:
            boundary.add(a)
            boundary.add(b)
    vg = c.vertex_groups.new(name="free")
    for i, g in enumerate(dom):
        small = tri_by_group.get(g, 0) < protect_below
        vg.add([i], 0.0 if (small or i in boundary) else 1.0, "REPLACE")
    # weights are rigid after the collapse (dominant bone, weight 1): remember the names
    c["bone_groups"] = json.dumps([c.vertex_groups[i].name for i in range(len(c.vertex_groups)) if c.vertex_groups[i].name != "free"])
    shield_seams(c, seam_weight)
    return c


def rigidify(obj, names):
    """After the collapse every vertex keeps only its dominant bone group at weight 1 (the source is rigidly skinned)."""
    me = obj.data
    idx = {obj.vertex_groups[n].index: n for n in names if n in obj.vertex_groups}
    for v in me.vertices:
        gs = [g for g in v.groups if g.group in idx]
        if not gs:
            continue
        top = max(gs, key=lambda g: g.weight).group
        for g in gs:
            if g.group != top:
                obj.vertex_groups[g.group].remove([v.index])
        obj.vertex_groups[top].add([v.index], 1.0, "REPLACE")
    if "free" in obj.vertex_groups:
        obj.vertex_groups.remove(obj.vertex_groups["free"])


def build_swatch(part, spec, size=1.0):
    """A 1 cm x 1 cm (x geometry_scale) quad that only reserves an atlas rectangle for a flat colour."""
    me = bpy.data.meshes.new("LOW__" + part)
    me.from_pydata([(0, 0, 0), (size, 0, 0), (size, size, 0), (0, size, 0)], [], [(0, 1, 2, 3)])
    me.update()
    o = bpy.data.objects.new("LOW__" + part, me)
    bpy.context.scene.collection.objects.link(o)
    me.uv_layers.new(name=wc.ATLAS_UV)
    uvd = me.uv_layers[0].data
    for i, uv in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
        uvd[i].uv = uv
    o["swatch"] = json.dumps(spec)
    return o


def eval_tris(obj):
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me = ev.to_mesh()
    n = sum(len(p.vertices) - 2 for p in me.polygons)
    ev.to_mesh_clear()
    return n


def decimate_to(obj, target):
    """Bisect the collapse ratio so the result lands within 2 % of `target` triangles; applies the modifier."""
    n0 = sum(len(p.vertices) - 2 for p in obj.data.polygons)
    if n0 <= target:
        return n0, 1.0
    armature_mods = [(m.object, m.name) for m in obj.modifiers if m.type == "ARMATURE"]
    for m in [m for m in obj.modifiers if m.type == "ARMATURE"]:
        obj.modifiers.remove(m)
    mod = obj.modifiers.new("decim", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.use_collapse_triangulate = True
    mod.vertex_group = "free"
    mod.vertex_group_factor = 1.0
    lo, hi = 0.001, 1.0
    ratio = min(1.0, target / n0)
    best = (None, None)
    for _ in range(14):
        mod.ratio = ratio
        n = eval_tris(obj)
        if best[0] is None or abs(n - target) < abs(best[0] - target):
            best = (n, ratio)
        if abs(n - target) <= 0.02 * target:
            break
        if n > target:
            hi = ratio
            ratio = (lo + ratio) * 0.5 if ratio - lo > 1e-5 else ratio * 0.9
        else:
            lo = ratio
            ratio = (ratio + hi) * 0.5
    mod.ratio = best[1]
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=mod.name)
    for arm, name in armature_mods:
        m = obj.modifiers.new(name, "ARMATURE")
        m.object = arm
    return sum(len(p.vertices) - 2 for p in obj.data.polygons), best[1]


def shade_with_sharp_edges(obj, angle_deg):
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    # no recalc_face_normals: the decimation keeps the source orientation, a recalculation flips open shells the wrong way
    lim = math.radians(angle_deg)
    for e in bm.edges:
        if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > lim:
            e.smooth = False
        else:
            e.smooth = True
    for f in bm.faces:
        f.smooth = True
    bm.to_mesh(me)
    bm.free()
    me.update()


def unwrap(obj, angle_deg):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    me = obj.data
    while me.uv_layers:
        me.uv_layers.remove(me.uv_layers[0])
    me.uv_layers.new(name=wc.ATLAS_UV)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle_deg), island_margin=0.0, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def uv_and_area(obj):
    me = obj.data
    uv = me.uv_layers[wc.ATLAS_UV]
    co = np.empty(len(me.loops) * 2, np.float32)
    uv.data.foreach_get("uv", co)
    co = co.reshape(-1, 2)
    area3 = 0.0
    areauv = 0.0
    for p in me.polygons:
        area3 += p.area
        ls = p.loop_indices
        a, b, c = co[ls[0]], co[ls[1]], co[ls[2]]
        areauv += abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) * 0.5
        if len(ls) == 4:
            d = co[ls[3]]
            areauv += abs((c[0] - a[0]) * (d[1] - a[1]) - (c[1] - a[1]) * (d[0] - a[0])) * 0.5
    return co, float(area3), float(areauv)


def scale_uv(obj, s):
    me = obj.data
    uv = me.uv_layers[wc.ATLAS_UV]
    co = np.empty(len(me.loops) * 2, np.float32)
    uv.data.foreach_get("uv", co)
    uv.data.foreach_set("uv", (co * s).astype(np.float32))


def coverage(objs, res):
    """Fraction of the 0..1 uv square covered by the objects' triangles (rasterised with PIL when available)."""
    try:
        from PIL import Image, ImageDraw  # noqa: F401
    except Exception:
        return None
    return None


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    ap.add_argument("--angle", type=float, default=66.0, help="smart uv angle limit")
    ap.add_argument("--sharp-angle", type=float, default=45.0)
    ap.add_argument("--only", default="", help="comma list of parts (debug)")
    args = ap.parse_args(argv)
    ctx = wc.Ctx(args.asset_dir)
    t = wc.Timer()
    bpy.ops.wm.open_mainfile(filepath=ctx.high_blend)
    cfg = ctx.config
    res = int(cfg.get("res", 1024))
    margin_px = float(cfg.get("margin_px", 8))
    report = {"parts": {}, "res": res}
    lows = {}
    for part, pc_ in cfg["parts"].items():
        if args.only and part not in args.only.split(","):
            continue
        if pc_.get("swatch"):
            lows[part] = build_swatch(part, pc_["swatch"], float(cfg.get("geometry_scale", 1.0)))
            report["parts"][part] = {"swatch": True, "tris_before": 0, "tris_after": 2, "objects_before": 0}
            continue
        high = part_high_objects(part)
        if not high:
            print("[weapon] WARNING no high objects for", part)
            continue
        n_high = sum(wc.tri_count(o) for o in high)
        seam_w = float(pc_.get("seam_weight", cfg.get("seam_weight", 0.35)))
        if pc_.get("skinned"):
            low = build_skinned(part, pc_.get("protect_below_tris", 150), seam_w)
        else:
            low = build_joined(part, pc_.get("protect_below_tris", 60), seam_w, float(pc_.get("dissolve_deg", cfg.get("dissolve_deg", 1.0))))
        n_weld = wc.tri_count(low)
        n_low, ratio = decimate_to(low, int(pc_["target_tris"]))
        if pc_.get("skinned"):
            rigidify(low, json.loads(low["bone_groups"]))
        else:
            low.vertex_groups.clear()
        shade_with_sharp_edges(low, args.sharp_angle)
        low.data.uv_layers[0].name = wc.ATLAS_UV  # the artist's islands are the atlas islands (repacked below)
        lows[part] = low
        report["parts"][part] = {"tris_before": n_high, "tris_welded": n_weld, "tris_after": n_low,
                                 "collapse_ratio": round(ratio, 4), "objects_before": len(high),
                                 "verts_after": len(low.data.vertices)}
        print("[weapon] %-12s %6d -> %5d tris (budget %d, ratio %.3f)" % (part, n_high, n_low, pc_["target_tris"], ratio))

    # texel-density weights, then pack everything into one atlas
    dens = {}
    for part, low in lows.items():
        _co, a3, auv = uv_and_area(low)
        dens[part] = math.sqrt(auv / a3)
    dref = float(np.median(list(dens.values())))
    for part, low in lows.items():
        w = float(cfg["parts"][part].get("weight", 1.0))
        scale_uv(low, w * dref / dens[part])
    # every part gets its own uv tile before packing: the artist's sheets all live in 0..1 and would otherwise be seen as
    # "overlapping" (merge_overlap) and share texels across parts
    for i, low in enumerate(lows.values()):
        me = low.data
        uv = me.uv_layers[wc.ATLAS_UV]
        co = np.empty(len(me.loops) * 2, np.float32)
        uv.data.foreach_get("uv", co)
        co = co.reshape(-1, 2)
        co[:, 0] += 4.0 * i
        uv.data.foreach_set("uv", co.ravel())
    bpy.ops.object.select_all(action="DESELECT")
    for low in lows.values():
        low.select_set(True)
    bpy.context.view_layer.objects.active = next(iter(lows.values()))
    bpy.ops.object.editmode_toggle()  # multi-object edit mode: every selected low part is packed together
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin_method="FRACTION", margin=margin_px / res, shape_method="CONCAVE",
                            merge_overlap=bool(cfg.get("merge_overlap", True)))
    bpy.ops.object.editmode_toggle()

    used = 0.0
    for part, low in lows.items():
        _co, a3, auv = uv_and_area(low)
        used += auv
        gs = float(cfg.get("geometry_scale", 1.0))  # source geometry that is gs x its real size (Sketchfab glTF scale)
        a3 = a3 / (gs * gs)
        report["parts"][part]["texel_density_px_per_cm"] = round(math.sqrt(auv / a3) * res, 2)
        report["parts"][part]["atlas_area_percent"] = round(auv * 100.0, 2)
        report["parts"][part]["surface_area_cm2"] = round(a3, 1)
    report["atlas_coverage_percent"] = round(used * 100.0, 2)
    for o in bpy.data.objects:
        if o.name.startswith("LOW__"):
            o.name = o.name[5:]
            o.data.name = o.name + "_low"
        elif o.type == "MESH" and "part" in o:
            o.hide_set(True)
            o.hide_render = True
    bpy.ops.wm.save_as_mainfile(filepath=ctx.low_blend, compress=True)
    report["total_tris_after"] = sum(p["tris_after"] for p in report["parts"].values())
    report["total_tris_before"] = sum(p["tris_before"] for p in report["parts"].values())
    wc.update_summary(ctx, "reduce", report)
    print("[weapon] REDUCE_DONE total %d -> %d tris, atlas coverage %.1f%% (%.1fs)" % (
        report["total_tris_before"], report["total_tris_after"], report["atlas_coverage_percent"], t.elapsed()))


if __name__ == "__main__":
    main()
