"""Character single-slot pipeline, stage 2b: triangle reduction (Blender background, non-destructive).

    blender --background --factory-startup --python Scripts/Pipeline/blender_reduce.py -- --asset-dir <...>
        [--target-tris 50000] [--keep-weights "regex=w,..."] [--protect-factor 2.0] [--sharp-angle 60]

Input 03_Blender/<mesh>_clean.blend (never modified); output 03_Blender/<mesh>_reduce.blend with
  <mesh>       the reduced LOD0 (armature, skin weights, original uvs kept; welded; frozen custom normals)
  <mesh>_high  an untouched copy of the cleaned mesh: the bake source (blender_bake.py projects it onto the low mesh)

Collapse decimation only, per material (= per cluster) budget:
  ratio_m = min(1, keep_weight_m * g), g solved so that sum(ratio_m * tris_m) = target.
Materials that already are low-poly and deform (mask, hoodie, pants, boots / gloves) keep everything; the dense
props (ropes, carabiners, pouches, axe / shovel, backpack) take the cut. Inside a material a vertex group steers the
collapse away from: uv seams and material borders (texture layout), open borders, and skin-weight blend zones
(shoulders / elbows / knees / hips / neck deform there). Blender's decimate collapses vertices with HIGH group
weight first, so the group holds 1 - protection.
"""
import argparse
import collections
import math
import os
import re
import sys
import time

import bpy
import bmesh

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402

# weight >= 100 = keep every triangle (the deforming body and the face silhouette are already low-poly)
DEFAULT_KEEP = ("mask|face|head|helmet=100,hoodie|jacket|chest|torso|vest|pants|trouser|legs=100,"
                "boot|glove|shoe=100,radio|flashlight=1.0,backpack=0.8,bag|pack|pouch=0.7,axe|shovel|weapon=0.6,"
                "carabiner|buckle=0.7,rope|strap|cord=1.0")  # thin coils fall apart below ~20 %
GROUP = "PIPELINE_DECIMATE"


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--target-tris", type=int, default=0, help="default: pipeline.json target_tris, else 50000")
    p.add_argument("--keep-weights", default=None, help="default: pipeline.json keep_weights, else DEFAULT_KEEP")
    p.add_argument("--protect-factor", type=float, default=2.0, help="decimate vertex group factor")
    p.add_argument("--weld", type=float, default=0.001, help="merge distance (cm) before decimating")
    p.add_argument("--sharp-angle", type=float, default=60.0, help="hard edges of the reduced mesh (degrees)")
    p.add_argument("--min-tris", type=int, default=24, help="never reduce a material below this")
    return p.parse_args(pc.script_args())


def parse_rules(s):
    out = []
    for part in s.split(","):
        if "=" in part:
            k, v = part.rsplit("=", 1)
            out.append((k.strip(), float(v)))
    return out


def keep_weight(name, rules):
    for pat, w in rules:
        if re.search(pat, name, re.I):
            return w
    return 1.0


def solve_budgets(tris, weights, target, min_tris):
    """ratio_m = min(1, w_m * g); bisection on g."""
    total = sum(tris.values())
    if target >= total:
        return {m: 1.0 for m in tris}, 1.0

    def kept(g):
        return sum(max(min(t, min_tris), min(1.0, weights[m] * g) * t) for m, t in tris.items())

    lo, hi = 0.0, 1.0 / max(1e-6, min(weights.values()))
    for _ in range(60):
        mid = (lo + hi) / 2
        if kept(mid) > target:
            hi = mid
        else:
            lo = mid
    g = lo
    return {m: max(min(1.0, weights[m] * g), min(1.0, min_tris / max(1, t))) for m, t in tris.items()}, g


def protection_weights(obj, uv_name):
    """Per-vertex protection 0..1: uv seams / material borders / open borders / skin blend zones."""
    me = obj.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv[uv_name]
    prot = [0.0] * len(bm.verts)
    reasons = collections.Counter()
    for e in bm.edges:
        lf = e.link_faces
        if len(lf) != 2:
            for v in e.verts:
                prot[v.index] = max(prot[v.index], 0.7)
            reasons["open_border_edges"] += 1
            continue
        if lf[0].material_index != lf[1].material_index:
            for v in e.verts:
                prot[v.index] = 1.0
            reasons["material_border_edges"] += 1
            continue
        seam = False
        for v in e.verts:
            a = next(l[uv].uv for l in lf[0].loops if l.vert == v)
            b = next(l[uv].uv for l in lf[1].loops if l.vert == v)
            if (a - b).length_squared > 1e-12:
                seam = True
                break
        if seam:
            for v in e.verts:
                prot[v.index] = 1.0
            reasons["uv_seam_edges"] += 1
    bm.free()
    # skin blend zones: a vertex shared ~50/50 between bones sits on a deforming joint
    deform = {g.index for g in obj.vertex_groups if g.name != GROUP}
    for v in me.vertices:
        ws = [g.weight for g in v.groups if g.group in deform and g.weight > 0.01]
        if len(ws) > 1:
            blend = 1.0 - max(ws)  # 0 rigid .. 0.5 half / half
            p = min(1.0, blend * 2.0)
            if p > prot[v.index]:
                prot[v.index] = p
            if p > 0.5:
                reasons["joint_blend_vertices"] += 1
    return prot, reasons


def tri_counts(obj):
    c = collections.Counter(p.material_index for p in obj.data.polygons)
    return {obj.material_slots[i].material.name: c.get(i, 0) for i in range(len(obj.material_slots))}


def decimate_material(obj, index, budget, factor):
    """Several passes: the vertex group slows the collapse down, so repeat until the budget is met."""
    passes = []
    for attempt in range(8):
        cur = sum(1 for p in obj.data.polygons if p.material_index == index)
        if cur <= budget * 1.02 or cur == 0:
            break
        ratio = max(0.02, budget / float(cur))
        use_group = attempt < 6  # last passes without the group: the budget wins
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="DESELECT")
        obj.active_material_index = index
        bpy.ops.object.material_slot_select()
        bpy.ops.mesh.decimate(ratio=ratio, use_vertex_group=use_group, vertex_group_factor=factor,
                              invert_vertex_group=False, use_symmetry=False)
        bpy.ops.object.mode_set(mode="OBJECT")
        passes.append(round(ratio, 3))
    return passes


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    t0 = time.time()
    bpy.ops.wm.open_mainfile(filepath=paths.clean_blend)
    low = bpy.data.objects[paths.mesh_name]
    # untouched bake source
    high = low.copy()
    high.data = low.data.copy()
    high.name = paths.mesh_name + "_high"
    high.data.name = high.name
    bpy.context.scene.collection.objects.link(high)
    high.hide_render = True  # bake / review switch it on when needed

    for o in bpy.data.objects:
        o.select_set(o == low)
    bpy.context.view_layer.objects.active = low
    me = low.data
    uv_name = me.uv_layers[0].name
    before = tri_counts(low)

    # weld split vertices (Unreal exports split render vertices); keep hard edges from the custom normals
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=args.weld, use_sharp_edge_from_normals=True)
    bpy.ops.object.mode_set(mode="OBJECT")
    welded_verts = len(me.vertices)
    # remember the authored normals: materials the budget leaves untouched get them back after the recompute
    cn = me.corner_normals
    authored = {}
    for poly in me.polygons:
        fc = tuple(round(c, 3) for c in poly.center)
        for li in poly.loop_indices:
            v = me.loops[li].vertex_index
            authored[(fc, tuple(round(c, 3) for c in me.vertices[v].co))] = tuple(cn[li].vector)

    prot, reasons = protection_weights(low, uv_name)
    if GROUP in low.vertex_groups:
        low.vertex_groups.remove(low.vertex_groups[GROUP])
    vg = low.vertex_groups.new(name=GROUP)
    by_w = collections.defaultdict(list)
    for i, p in enumerate(prot):
        by_w[round(1.0 - 0.9 * p, 2)].append(i)  # high weight = collapsed first
    for w, idx in by_w.items():
        vg.add(idx, w, "REPLACE")
    low.vertex_groups.active = vg

    rules = parse_rules(args.keep_weights or paths.config.get("keep_weights") or DEFAULT_KEEP)
    args.target_tris = args.target_tris or int(paths.config.get("target_tris", 50000))
    weights = {m: keep_weight(m, rules) for m in before}
    ratios, g = solve_budgets(before, weights, args.target_tris, args.min_tris)
    budgets = {m: int(round(before[m] * ratios[m])) for m in before}
    passes = {}
    with pc.Timer("decimate") as t_dec:
        for i, s in enumerate(low.material_slots):
            name = s.material.name
            if ratios[name] < 0.999:
                passes[name] = decimate_material(low, i, budgets[name], args.protect_factor)
    low.vertex_groups.remove(low.vertex_groups[GROUP])

    # clean normals for the reduced surface, frozen as custom normals (the bake and the FBX use exactly these)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.delete_loose()
    bpy.ops.object.mode_set(mode="OBJECT")
    try:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    except RuntimeError:
        pass
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(args.sharp_angle))
    me.update()
    normals = [tuple(n.vector) for n in me.corner_normals]
    kept_mats = {i for i, sl in enumerate(low.material_slots) if ratios[sl.material.name] >= 0.999}
    restored = 0
    for poly in me.polygons:
        if poly.material_index not in kept_mats:
            continue
        fc = tuple(round(c, 3) for c in poly.center)
        for li in poly.loop_indices:
            key = (fc, tuple(round(c, 3) for c in me.vertices[me.loops[li].vertex_index].co))
            n = authored.get(key)
            if n is not None:
                normals[li] = n
                restored += 1
    me.normals_split_custom_set(normals)

    after = tri_counts(low)
    degenerate = sum(1 for p in me.polygons if p.area < 1e-6)
    bm = bmesh.new()
    bm.from_mesh(me)
    non_manifold = sum(1 for e in bm.edges if len(e.link_faces) > 2)
    bm.free()
    unweighted = sum(1 for v in me.vertices if not any(g.weight > 0.0 for g in v.groups))
    bpy.ops.wm.save_as_mainfile(filepath=paths.reduce_blend, compress=True)
    per = {m: {"before": before[m], "budget": budgets[m], "after": after.get(m, 0), "keep_weight": weights[m],
               "ratio": round(after.get(m, 0) / max(1, before[m]), 3), "passes": passes.get(m, [])}
           for m in before}
    pc.update_summary(paths, "reduce", {
        "target_tris": args.target_tris, "triangles_before": sum(before.values()),
        "triangles_after": sum(after.values()), "vertices_welded": welded_verts, "vertices_after": len(me.vertices),
        "per_material": per, "g": round(g, 4), "protect_factor": args.protect_factor,
        "protection": dict(reasons), "degenerate_faces": degenerate, "non_manifold_edges": non_manifold,
        "unweighted_vertices": unweighted, "sharp_angle": args.sharp_angle,
        "authored_normals_restored_loops": restored,
        "seconds": round(time.time() - t0, 1), "decimate_seconds": round(t_dec.seconds, 1)})
    print("[pipeline] REDUCE_DONE tris %d -> %d (target %d) unweighted=%d degenerate=%d" % (
        sum(before.values()), sum(after.values()), args.target_tris, unweighted, degenerate))


main()
