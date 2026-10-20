"""Character single-slot pipeline, stage 3: atlas UV (Blender background + UVPackmaster 4).

    blender --background --factory-startup --python Scripts/Pipeline/blender_atlas.py -- --asset-dir <...>
        [--res 4096] [--margin-px 12] [--weights "regex=w,..."] [--rot-step 90] [--search-time 60] [--builtin]

Copies the ORIGINAL uv islands of every material into a new uv map `UV_Atlas` (no re-unwrap), scales them per
material / cluster to the wanted texel density, moves every cluster into its own tile (so islands of different
materials never count as "overlapping"), then packs everything into 0..1 with UVPackmaster 4 (lock overlapping:
islands the artist stacked on the same texels stay stacked). Falls back to Blender's packer with --builtin or if
UVPackmaster cannot run.
"""
import argparse
import collections
import math
import os
import re
import sys

import bpy
import bmesh

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402

DEFAULT_WEIGHTS = ("mask|face|head|helmet=2.0,hoodie|jacket|chest|torso|vest=1.4,pants|trouser|legs=1.0,"
                   "boot|glove|shoe=0.9,backpack|bag|pack|radio|flashlight=0.9,axe|shovel|weapon=0.8,"
                   "rope|strap|carabiner|buckle=0.7")
DOWN_FACING_WEIGHT = 0.5  # islands facing the ground (soles, under-sides): rarely seen from the tactical camera
ATLAS_UV = "UV_Atlas"


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--res", type=int, default=4096)
    p.add_argument("--margin-px", type=int, default=12)
    p.add_argument("--border-px", type=int, default=4)
    p.add_argument("--weights", default=None, help="default: pipeline.json texel_weights, else DEFAULT_WEIGHTS")
    p.add_argument("--rot-step", type=int, default=90)
    p.add_argument("--search-time", type=int, default=60, help="UVPackmaster heuristic search seconds")
    p.add_argument("--fill", type=float, default=0.72, help="expected coverage, for the source-density clamp")
    p.add_argument("--builtin", action="store_true")
    p.add_argument("--source", choices=("auto", "clean", "reduce"), default="auto",
                   help="auto: the reduced mesh when 03_Blender/<mesh>_reduce.blend exists")
    return p.parse_args(pc.script_args())


def material_weight(name, rules):
    for pattern, w in rules:
        if re.search(pattern, name, re.I):
            return w
    return 1.0


def parse_rules(s):
    out = []
    for part in s.split(","):
        if "=" in part:
            k, v = part.rsplit("=", 1)
            out.append((k.strip(), float(v)))
    return out


def tri_area_uv(uvs):
    (a, b, c) = uvs
    return abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1])) * 0.5


def islands(bm, uv):
    """Union-find of faces sharing an edge with continuous uvs (and the same material)."""
    parent = list(range(len(bm.faces)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def uv_at(f, v):
        for l in f.loops:
            if l.vert == v:
                return l[uv].uv
        return None

    for e in bm.edges:
        lf = e.link_faces
        if len(lf) != 2 or lf[0].material_index != lf[1].material_index:
            continue
        ok = True
        for v in e.verts:
            a, b = uv_at(lf[0], v), uv_at(lf[1], v)
            if (a - b).length_squared > 1e-12:
                ok = False
                break
        if ok:
            ra, rb = find(lf[0].index), find(lf[1].index)
            if ra != rb:
                parent[ra] = rb
    groups = collections.defaultdict(list)
    for f in bm.faces:
        groups[find(f.index)].append(f.index)
    return list(groups.values())


def bbox_clusters(isl_list, bboxes):
    """Merge islands whose uv bounding boxes overlap (they may share texels): they are scaled / moved together."""
    n = len(isl_list)
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    order = sorted(range(n), key=lambda i: bboxes[i][0])
    active = []
    eps = 1e-5
    for i in order:
        x0, y0, x1, y1 = bboxes[i]
        active = [j for j in active if bboxes[j][2] > x0 + eps]
        for j in active:
            if bboxes[j][1] < y1 - eps and bboxes[j][3] > y0 + eps:
                ra, rb = find(i), find(j)
                if ra != rb:
                    parent[ra] = rb
        active.append(i)
    groups = collections.defaultdict(list)
    for i in range(n):
        groups[find(i)].append(i)
    return list(groups.values())


def cross_material_overlap(me, res=1024):
    """Pixels (at `res`) whose centre lies inside triangles of two different materials: texture conflicts in the
    atlas. Stacking inside one material is the artist's texel sharing and is allowed."""
    import numpy as np
    owner = np.full((res, res), -1, dtype=np.int32)
    conflict = np.zeros((res, res), dtype=bool)
    uvd = me.uv_layers[ATLAS_UV].data
    for p in me.polygons:
        pts = [uvd[li].uv * res for li in p.loop_indices]
        for t in range(1, len(pts) - 1):
            a, b, c = pts[0], pts[t], pts[t + 1]
            x0, x1 = int(max(0, min(a.x, b.x, c.x))), int(min(res - 1, max(a.x, b.x, c.x)))
            y0, y1 = int(max(0, min(a.y, b.y, c.y))), int(min(res - 1, max(a.y, b.y, c.y)))
            if x1 < x0 or y1 < y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)
            if abs(d) < 1e-12:
                continue
            w1 = ((xs - a.x) * (c.y - a.y) - (c.x - a.x) * (ys - a.y)) / d
            w2 = ((b.x - a.x) * (ys - a.y) - (xs - a.x) * (b.y - a.y)) / d
            inside = (w1 >= 0) & (w2 >= 0) & (w1 + w2 <= 1)
            if not inside.any():
                continue
            sub = owner[y0:y1 + 1, x0:x1 + 1]
            csub = conflict[y0:y1 + 1, x0:x1 + 1]
            csub |= inside & (sub >= 0) & (sub != p.material_index)
            sub[inside & (sub < 0)] = p.material_index
    covered = int((owner >= 0).sum())
    return int(conflict.sum()), covered, owner


def write_layout(path, owner, names):
    """Atlas layout preview: one colour per material, legend in the json summary."""
    import colorsys
    import numpy as np
    n = max(1, len(names))
    pal = np.array([colorsys.hsv_to_rgb(i / n, 0.65, 0.95) for i in range(n)] + [(0.08, 0.08, 0.08)])
    img = pal[np.where(owner >= 0, owner, n)]
    pc.write_png(path, img[::-1], 8)  # uv v goes up, image rows go down
    return {names[i]: "#%02x%02x%02x" % tuple(int(c * 255) for c in pal[i]) for i in range(len(names))}


def run_uvpackmaster(mesh, args, rot180_faces=()):
    if not pc.enable_uvpackmaster():
        return False, "UVPackmaster 4 add-on could not be enabled"
    scene = bpy.context.scene
    mp = scene.uvpm4_props.default_main_props
    mp.precision = 1000
    mp.margin = args.margin_px / float(args.res)
    mp.pixel_margin_enable = True
    mp.pixel_margin = args.margin_px
    mp.pixel_border_margin_enable = True
    mp.pixel_border_margin = args.border_px
    mp.pixel_margin_tex_size = args.res
    mp.rotation_enable = True
    mp.rotation_step = args.rot_step
    mp.flipping_enable = False  # flipping would mirror the baked normals' tangent frame needlessly
    mp.normalize_scale = False  # our own per-cluster texel density
    mp.lock_overlapping_enable = True
    mp.lock_overlapping_mode = "1"  # any part overlapping -> locked together (authored texel sharing)
    mp.heuristic_enable = args.search_time > 0
    mp.heuristic_search_time = args.search_time
    mp.fully_inside = True
    mp.active_pack_mode_id = "pack.single_tile"
    prefs = bpy.context.preferences.addons["bl_ext.user_default.uvpackmaster4"].preferences
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    me = mesh.data
    mp.island_rot_step_enable = bool(rot180_faces)
    if rot180_faces:
        sel = set(rot180_faces)
        for p in me.polygons:
            p.select = p.index in sel
    bpy.ops.object.mode_set(mode="EDIT")
    scene.tool_settings.use_uv_select_sync = True
    if rot180_faces:
        bpy.ops.uvpackmaster4.std_set_iparam(iparam_info_type="RotStepIParamInfo", iparam_value_attr=180)
    bpy.ops.mesh.select_all(action="SELECT")
    try:
        res = bpy.ops.uvpackmaster4.pack(mode_id="pack.single_tile", pack_op_type="0")
    except Exception as exc:
        bpy.ops.object.mode_set(mode="OBJECT")
        return False, "UVPackmaster operator failed: %s" % exc
    bpy.ops.object.mode_set(mode="OBJECT")
    code = getattr(prefs, "engine_retcode", None)
    status = getattr(prefs, "engine_status_msg", "")
    ok = "FINISHED" in res and code in (0, 1, None)  # 0 success, 1 warning (see UvpmRetCode)
    return ok, "UVPackmaster result %s retcode %s (%s)" % (res, code, status)


def run_builtin(mesh, args, no_rotation=False):
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    bpy.ops.mesh.select_all(action="SELECT")
    kw = dict(rotate=not no_rotation, margin_method="FRACTION", margin=args.margin_px / float(args.res), scale=True,
              udim_source="CLOSEST_UDIM")
    try:
        bpy.ops.uv.pack_islands(shape_method="CONCAVE", merge_overlap=True, **kw)
    except TypeError:
        bpy.ops.uv.pack_islands(**kw)
    bpy.ops.object.mode_set(mode="OBJECT")
    return True, "Blender built-in packer (concave, merge overlap)"


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    src = args.source
    if src == "auto":
        src = "reduce" if os.path.exists(paths.reduce_blend) else "clean"
    bpy.ops.wm.open_mainfile(filepath=paths.reduce_blend if src == "reduce" else paths.clean_blend)
    print("[pipeline] atlas source:", src)
    mesh = bpy.data.objects[paths.mesh_name]
    me = mesh.data
    src_uv = me.uv_layers[0].name
    if ATLAS_UV in me.uv_layers:
        me.uv_layers.remove(me.uv_layers[ATLAS_UV])
    me.uv_layers.new(name=ATLAS_UV, do_init=True)  # copy of the source uv
    me.uv_layers[ATLAS_UV].data.foreach_set("uv", [c for l in me.uv_layers[src_uv].data for c in l.uv])
    rules = parse_rules(args.weights or paths.config.get("texel_weights") or DEFAULT_WEIGHTS)

    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    uv = bm.loops.layers.uv[ATLAS_UV]
    slots = [s.material.name for s in mesh.material_slots]

    # source resolution per material = its base colour image (exported PNG)
    src_res = {}
    for s in mesh.material_slots:
        imgs = [n.image for n in s.material.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]
        bc = [i for i in imgs if re.search(r"basecolor|albedo|diffuse", i.name, re.I)] or imgs
        src_res[s.material.name] = max(bc[0].size) if bc else args.res

    with pc.Timer("islands"):
        isl = islands(bm, uv)
    face_area = [f.calc_area() for f in bm.faces]
    face_uv_area = [tri_area_uv([l[uv].uv for l in f.loops]) for f in bm.faces]
    per_mat = collections.defaultdict(lambda: [0.0, 0.0])
    for f in bm.faces:
        per_mat[f.material_index][0] += face_area[f.index]
        per_mat[f.material_index][1] += face_uv_area[f.index]
    u_m = {m: math.sqrt(a[1] / a[0]) if a[0] > 0 and a[1] > 0 else 1.0 for m, a in per_mat.items()}

    # clusters per material
    clusters = []  # (material index, [face indices], weight)
    by_mat = collections.defaultdict(list)
    for faces in isl:
        by_mat[bm.faces[faces[0]].material_index].append(faces)
    down = 0
    for m, lst in by_mat.items():
        boxes = []
        for faces in lst:
            us = [l[uv].uv for fi in faces for l in bm.faces[fi].loops]
            boxes.append((min(u.x for u in us), min(u.y for u in us), max(u.x for u in us), max(u.y for u in us)))
        for group in bbox_clusters(lst, boxes):
            faces = [fi for gi in group for fi in lst[gi]]
            nz = sum(bm.faces[fi].normal.z * face_area[fi] for fi in faces)
            area = sum(face_area[fi] for fi in faces) or 1.0
            w = material_weight(slots[m], rules)
            if nz / area < -0.6:
                w *= DOWN_FACING_WEIGHT
                down += 1
            clusters.append((m, faces, w))

    # Islands stacked on the same texels with OPPOSITE uv handedness (mirrored halves) need the same tangent-space
    # values only while the group is rotated by 0 / 180 degrees: a 90-degree turn rotates the mirrored frame the
    # other way and the normal bake of one half overwrites the other with wrong values. Such clusters may only turn
    # by 180 degrees (UVPackmaster per-island rotation step).
    def uv_sign(f):
        a, b, c = (l[uv].uv for l in list(f.loops)[:3])
        d = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)
        return 0 if abs(d) < 1e-12 else (1 if d > 0 else -1)

    mixed_faces = []
    mixed_by_mat = collections.Counter()
    for m, faces, w in clusters:
        signs = {uv_sign(bm.faces[fi]) for fi in faces} - {0}
        if len(signs) > 1:
            mixed_faces.extend(faces)
            mixed_by_mat[slots[m]] += 1

    # texel density: k = relative scale of a cluster; clamp so the atlas never exceeds the source density
    def cluster_uv_area(c):
        return sum(face_uv_area[fi] for fi in c[1])

    ks = [c[2] / u_m[c[0]] for c in clusters]
    for _ in range(4):
        total = sum(k * k * cluster_uv_area(c) for k, c in zip(ks, clusters)) or 1.0
        g = math.sqrt(args.fill / total)  # expected final uniform scale after packing
        changed = False
        for i, c in enumerate(clusters):
            td_atlas = ks[i] * g * u_m[c[0]] * args.res
            td_src = u_m[c[0]] * src_res[slots[c[0]]]
            if td_atlas > td_src * 1.001:
                ks[i] = td_src / (g * u_m[c[0]] * args.res)
                changed = True
        if not changed:
            break

    # scale every cluster about its centre and move it into its own tile outside 0..1 (tiles never overlap:
    # the scale is normalised so the largest scaled cluster fits one tile)
    boxes = []
    for i, c in enumerate(clusters):
        us = [l[uv].uv for fi in c[1] for l in bm.faces[fi].loops]
        boxes.append((min(u.x for u in us), min(u.y for u in us), max(u.x for u in us), max(u.y for u in us)))
    norm = 1.0 / max(max(b[2] - b[0], b[3] - b[1]) * k for b, k in zip(boxes, ks))
    cols, tile = 64, 1.25
    for i, c in enumerate(clusters):
        loops = [l for fi in c[1] for l in bm.faces[fi].loops]
        x0, y0, x1, y1 = boxes[i]
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        k = ks[i] * norm
        ox, oy = 2 + (i % cols) * tile, (i // cols) * tile
        for l in loops:
            l[uv].uv.x = (l[uv].uv.x - cx) * k + ox
            l[uv].uv.y = (l[uv].uv.y - cy) * k + oy
    bm.to_mesh(me)
    bm.free()
    me.uv_layers.active = me.uv_layers[ATLAS_UV]

    packer, note = "uvpackmaster4", ""
    with pc.Timer("pack") as t_pack:
        ok = False
        if not args.builtin:
            ok, note = run_uvpackmaster(mesh, args, mixed_faces)
            print("[pipeline]", note)
        if not ok:
            packer = "blender_builtin"
            ok, note2 = run_builtin(mesh, args, no_rotation=bool(mixed_faces))
            note = (note + " -> fallback: " if note else "") + note2

    # validation + texel density report
    uvs = me.uv_layers[ATLAS_UV].data
    out_of_range = sum(1 for l in uvs if not (-1e-4 <= l.uv.x <= 1.0001 and -1e-4 <= l.uv.y <= 1.0001))
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv[ATLAS_UV]
    atlas_area = collections.Counter()
    for f in bm.faces:
        atlas_area[f.material_index] += tri_area_uv([l[uvl].uv for l in f.loops])
    bm.free()
    td = {}
    for m, (a3, _) in per_mat.items():
        tda = math.sqrt(atlas_area[m] / a3) * args.res if a3 else 0
        tds = u_m[m] * src_res[slots[m]]
        td[slots[m]] = {"atlas_px_per_cm": round(tda, 2), "source_px_per_cm": round(tds, 2),
                        "ratio": round(tda / tds, 3) if tds else None, "weight": material_weight(slots[m], rules),
                        "source_res": src_res[slots[m]]}
    me.uv_layers.active = me.uv_layers[ATLAS_UV]
    with pc.Timer("cross-material overlap check"):
        conflicts, covered, owner = cross_material_overlap(me)
    legend = write_layout(os.path.join(paths.review, "atlas_layout.png"), owner, slots)
    bpy.ops.wm.save_as_mainfile(filepath=paths.atlas_blend, compress=True)
    pc.update_summary(paths, "atlas", {
        "source_blend": src, "packer": packer, "packer_note": note, "pack_seconds": round(t_pack.seconds, 1), "resolution": args.res,
        "margin_px": args.margin_px, "border_px": args.border_px, "rotation_step": args.rot_step,
        "islands": len(isl), "clusters": len(clusters), "down_facing_clusters": down,
        "mirrored_stacked_clusters_rot180": dict(mixed_by_mat), "mirrored_stacked_faces": len(mixed_faces),
        "uv_area_sum": round(sum(atlas_area.values()), 4),
        "loops_out_of_0_1": out_of_range, "texel_density": td,
        "cross_material_overlap_px_1k": conflicts, "covered_px_1k": covered,
        "cross_material_overlap_percent": round(100.0 * conflicts / max(1, covered), 3),
        "layout_image": "06_Review/atlas_layout.png", "layout_legend": legend,
        "note": "coverage % is measured on the baked mask in blender_bake.py"})
    print("[pipeline] ATLAS_DONE packer=%s islands=%d clusters=%d uv_area_sum=%.3f out_of_range=%d "
          "cross_material_overlap_px=%d" % (packer, len(isl), len(clusters), sum(atlas_area.values()), out_of_range,
                                             conflicts))


main()
