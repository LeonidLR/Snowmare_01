"""Character single-slot pipeline, stage 2: clean-up in Blender (background).

    blender --background --factory-startup --python Scripts/Pipeline/blender_clean.py -- --asset-dir <ArtPipeline/...>
        [--hair-regex REGEX] [--hidden-exclude REGEX] [--no-hidden] [--dirs 80] [--anim-frames 2]

- imports 01_Source/<mesh>.fbx in centimetres (scene unit scale 0.01, every object at scale 1);
- rebuilds the original Unreal materials from 01_Source/inventory.json (images on the original UV);
- writes the ground-truth tables 01_Source/texture_roles.json and 01_Source/slot_decisions.json;
- deletes hair (material name regex) and triangles hidden in EVERY test pose (see hidden_faces());
- saves 03_Blender/<mesh>_original.blend (untouched, for the review) and 03_Blender/<mesh>_clean.blend.
"""
import argparse
import collections
import glob
import math
import os
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402
import material_graph as mg  # noqa: E402


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--hair-regex", default=pc.HAIR_REGEX_DEFAULT)
    p.add_argument("--hidden-exclude", default="", help="materials never touched by the hidden-triangle removal")
    p.add_argument("--no-hidden", action="store_true", help="skip the hidden-triangle removal")
    p.add_argument("--dirs", type=int, default=80, help="ray directions on the sphere (about half are used)")
    p.add_argument("--anim-frames", type=int, default=2, help="frames sampled per exported animation")
    p.add_argument("--keep-ring", type=int, default=1, help="hidden triangles this close to a visible one are kept")
    return p.parse_args(pc.script_args())


# --------------------------------------------------------------------------------------------- materials

def rebuild_materials(mesh, inv, paths):
    warnings = []
    uv_name = mesh.data.uv_layers[0].name
    by_slot = {s["slot_name"]: s for s in inv["slots"]}
    for i, slot in enumerate(mesh.material_slots):
        name = slot.material.name if slot.material else None
        info = by_slot.get(name)
        if not info or not info["material"]:
            warnings.append("slot %d (%s) not in the inventory" % (i, name))
            continue
        minfo = inv["materials"][info["material"]]
        mat = mg.build_material(name, minfo, inv, paths.tex_source, uv_name, warnings)
        slot.material = mat
    return warnings


# --------------------------------------------------------------------------------------------- poses

def forward_dir(arm, mesh, inv):
    """Character forward (horizontal): from the head bone towards the face (mask / head slot), else -Y."""
    head = arm.matrix_world @ arm.data.bones["head"].head_local if "head" in arm.data.bones else Vector((0, 0, 160))
    face_idx = [i for i, s in enumerate(mesh.material_slots) if s.material and
                any(k in s.material.name.lower() for k in ("mask", "face", "head"))]
    if face_idx:
        me = mesh.data
        acc, n = Vector(), 0
        for p in me.polygons:
            if p.material_index in face_idx:
                acc += mesh.matrix_world @ p.center
                n += 1
        if n:
            d = acc / n - head
            d.z = 0
            if d.length > 1e-3:
                return d.normalized()
    return Vector((0, -1, 0))


POSES = {
    # (bone, rotation axis, degrees, where the bone tail must move) - signs are resolved per bone automatically
    "crouch_deep": [("thigh_l", "lat", 115, "fwd"), ("thigh_r", "lat", 115, "fwd"), ("calf_l", "lat", 140, "back"),
                    ("calf_r", "lat", 140, "back"), ("spine_01", "lat", 15, "fwd"), ("spine_02", "lat", 15, "fwd"),
                    ("spine_03", "lat", 15, "fwd"), ("neck_01", "lat", 20, "fwd")],
    "kneel_aim": [("thigh_l", "lat", 95, "fwd"), ("calf_l", "lat", 120, "back"), ("thigh_r", "lat", 25, "back"),
                  ("calf_r", "lat", 130, "back"), ("upperarm_l", "lat", 80, "fwd"), ("upperarm_r", "lat", 70, "fwd"),
                  ("lowerarm_l", "up", 90, "in"), ("lowerarm_r", "up", 90, "in"), ("head", "lat", 20, "fwd")],
    "arms_up": [("upperarm_l", "fwd", 110, "up"), ("upperarm_r", "fwd", 110, "up"), ("lowerarm_l", "lat", 40, "fwd"),
                ("lowerarm_r", "lat", 40, "fwd"), ("spine_03", "lat", 15, "back")],
    "arms_hug": [("clavicle_l", "up", 20, "fwd"), ("clavicle_r", "up", 20, "fwd"), ("upperarm_l", "up", 75, "fwd"),
                 ("upperarm_r", "up", 75, "fwd"), ("lowerarm_l", "up", 120, "in"), ("lowerarm_r", "up", 120, "in")],
    "twist_bend": [("spine_01", "up", 25, None), ("spine_02", "up", 25, None), ("spine_03", "up", 20, None),
                   ("spine_02", "fwd", 20, None), ("neck_01", "up", 35, None), ("head", "up", 35, None),
                   ("head", "fwd", 20, None)],
    "twist_bend_other": [("spine_01", "up", -25, None), ("spine_02", "up", -25, None), ("spine_03", "up", -20, None),
                         ("spine_02", "fwd", -20, None), ("neck_01", "up", -35, None), ("head", "up", -35, None),
                         ("head", "fwd", -20, None)],
    "prone_look_up": [("spine_01", "lat", 15, "back"), ("spine_02", "lat", 15, "back"), ("spine_03", "lat", 15, "back"),
                      ("neck_01", "lat", 35, "back"), ("head", "lat", 30, "back"), ("upperarm_l", "lat", 100, "fwd"),
                      ("upperarm_r", "lat", 100, "fwd"), ("lowerarm_l", "lat", 70, "up"), ("lowerarm_r", "lat", 70, "up"),
                      ("thigh_l", "fwd", 20, "out"), ("thigh_r", "fwd", 20, "out")],
    "lunge_spread": [("thigh_l", "fwd", 50, "out"), ("thigh_r", "lat", 45, "back"), ("calf_r", "lat", 60, "back"),
                     ("thigh_l", "lat", 40, "fwd"), ("calf_l", "lat", 70, "back")],
    "sit_reach": [("thigh_l", "lat", 90, "fwd"), ("thigh_r", "lat", 90, "fwd"), ("calf_l", "lat", 95, "back"),
                  ("calf_r", "lat", 95, "back"), ("spine_01", "lat", 20, "fwd"), ("spine_02", "lat", 20, "fwd"),
                  ("spine_03", "lat", 20, "fwd"), ("upperarm_l", "lat", 60, "fwd"), ("upperarm_r", "lat", 60, "fwd"),
                  ("head", "lat", 30, "fwd")],
}


def apply_pose(arm, ops, fwd):
    lat = Vector((1, 0, 0)) if abs(fwd.x) < 0.7 else Vector((0, 1, 0))
    axes = {"lat": lat, "up": Vector((0, 0, 1)), "fwd": fwd}
    reset_pose(arm)
    for bone, axis, deg, want in ops:
        pb = arm.pose.bones.get(bone)
        if pb is None:
            continue
        side = 1.0 if pb.head.dot(lat) >= 0 else -1.0
        dirs = {"fwd": fwd, "back": -fwd, "up": Vector((0, 0, 1)), "down": Vector((0, 0, -1)),
                "out": lat * side, "in": -lat * side}
        before = pb.matrix.copy()
        tail0 = pb.tail.copy()
        rotate(pb, axes[axis], deg)
        if want is not None and (pb.tail - tail0).dot(dirs[want]) < 0:
            pb.matrix = before
            bpy.context.view_layer.update()
            rotate(pb, axes[axis], -deg)


def rotate(pb, axis, deg):
    m = pb.matrix.copy()
    head = m.translation.copy()
    r = Matrix.Rotation(math.radians(deg), 4, axis)
    pb.matrix = Matrix.Translation(head) @ r @ Matrix.Translation(-head) @ m
    bpy.context.view_layer.update()


def reset_pose(arm):
    if arm.animation_data:
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()


def anim_poses(arm, paths, frames_per_anim):
    """Yield (label, None) after posing `arm` with frames of every exported animation of its skeleton."""
    files = sorted(glob.glob(os.path.join(paths.source, "anims", "*.fbx")))
    for f in files:
        a2, meshes, others = pc.import_fbx(f, with_anim=True)
        act = a2.animation_data.action if (a2 and a2.animation_data) else None
        for o in [a2] + meshes + others:
            if o:
                bpy.data.objects.remove(o)
        if act is None:
            print("[pipeline] no action in", f)
            continue
        act.use_fake_user = True
        arm.animation_data_create()
        arm.animation_data.action = act
        if hasattr(arm.animation_data, "action_slot") and len(getattr(act, "slots", [])):
            arm.animation_data.action_slot = act.slots[0]
        f0, f1 = act.frame_range
        for k in range(frames_per_anim):
            frame = int(round(f0 + (f1 - f0) * (k + 0.5) / frames_per_anim))
            bpy.context.scene.frame_set(frame)
            bpy.context.view_layer.update()
            yield "%s@%d" % (os.path.splitext(os.path.basename(f))[0], frame)
        arm.animation_data.action = None


# --------------------------------------------------------------------------------------------- hidden faces

def posed_geometry(mesh):
    deps = bpy.context.evaluated_depsgraph_get()
    ev = mesh.evaluated_get(deps)
    me = ev.to_mesh()
    mw = mesh.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    ev.to_mesh_clear()
    return verts


def visible_set(verts, polys, candidates, dirs, eps, maxdist):
    """Indices of `candidates` that a viewer outside the mesh can see (single-sided rendering:
    back faces do not occlude, so rays continue through them)."""
    bvh = BVHTree.FromPolygons(verts, polys, all_triangles=True)
    visible = set()
    for t in candidates:
        a, b, c = (verts[i] for i in polys[t])
        n = (b - a).cross(c - a)
        if n.length < 1e-10:
            visible.add(t)  # degenerate: never remove
            continue
        n.normalize()
        samples = ((a + b + c) / 3.0, a * 0.6 + b * 0.2 + c * 0.2, a * 0.2 + b * 0.6 + c * 0.2,
                   a * 0.2 + b * 0.2 + c * 0.6)
        hemi = [n] + [d for d in dirs if d.dot(n) > 0.1]
        seen = False
        for p in samples:
            o0 = p + n * eps
            for d in hemi:
                o = o0
                for _ in range(8):
                    loc, nrm, idx, _dist = bvh.ray_cast(o, d, maxdist)
                    if loc is None:
                        seen = True
                        break
                    if idx != t and nrm.dot(d) > 0.0:
                        break  # a face turned towards the viewer blocks the view
                    o = loc + d * eps  # own face / back face (culled in Unreal): look through
                if seen:
                    break
            if seen:
                break
        if seen:
            visible.add(t)
    return visible


def hidden_faces(arm, mesh, inv, paths, args, exclude_mats):
    me = mesh.data
    polys = [tuple(p.vertices) for p in me.polygons]
    if any(len(p) != 3 for p in polys):
        raise RuntimeError("expected a triangulated mesh from Unreal")
    candidates = [i for i, p in enumerate(me.polygons) if p.material_index not in exclude_mats]
    dirs = [Vector(d) for d in pc.fibonacci_sphere(args.dirs)]
    fwd = forward_dir(arm, mesh, inv)
    log = []
    eps, maxdist = 0.05, 1000.0  # cm

    def run(label):
        nonlocal candidates
        with pc.Timer("visibility %s (%d candidates)" % (label, len(candidates))) as t:
            vis = visible_set(posed_geometry(mesh), polys, candidates, dirs, eps, maxdist)
        candidates = [c for c in candidates if c not in vis]
        log.append({"pose": label, "still_hidden": len(candidates), "seconds": round(t.seconds, 1)})

    reset_pose(arm)
    run("rest")
    for name, ops in POSES.items():
        if not candidates:
            break
        apply_pose(arm, ops, fwd)
        run(name)
    for label in anim_poses(arm, paths, args.anim_frames):
        if not candidates:
            break
        run(label)
    reset_pose(arm)

    hidden = set(candidates)
    # Safety ring: keep hidden triangles that share a vertex with a visible one (n rings).
    vert_faces = collections.defaultdict(list)
    for i, p in enumerate(polys):
        for v in p:
            vert_faces[v].append(i)
    for _ in range(args.keep_ring):
        border = set()
        for t in hidden:
            for v in polys[t]:
                if any(f not in hidden for f in vert_faces[v]):
                    border.add(t)
                    break
        hidden -= border
    return hidden, log, fwd


def delete_faces(mesh, faces):
    bm = bmesh.new()
    bm.from_mesh(mesh.data)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[i] for i in sorted(faces)], context="FACES")
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context="VERTS")
    bm.to_mesh(mesh.data)
    bm.free()
    mesh.data.update()


def tri_counts(mesh):
    c = collections.Counter(p.material_index for p in mesh.data.polygons)
    return {mesh.material_slots[i].material.name: c.get(i, 0) for i in range(len(mesh.material_slots))}


# --------------------------------------------------------------------------------------------- main

def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    inv = paths.inventory
    pc.reset_scene()
    arm, mesh = pc.import_unreal_mesh(paths)

    with pc.Timer("materials"):
        mat_warnings = rebuild_materials(mesh, inv, paths)
    before = tri_counts(mesh)
    inv["lod0_section_triangles"] = before
    pc.save_json(paths.inventory_path, inv)

    roles = mg.texture_roles(inv)
    pc.save_json(os.path.join(paths.source, "texture_roles.json"), {
        "_doc": "Ground truth: texture -> role. method=material_graph means the role was read from the Unreal "
                "material graph (which output / channel the texture feeds); name_guess is the suffix heuristic.",
        "textures": roles})

    pc.update_summary(paths, "source", {
        "slots": len(mesh.material_slots), "triangles": sum(before.values()), "vertices": len(mesh.data.vertices),
        "triangles_per_slot": before, "bones": len(arm.data.bones), "uv_maps": [u.name for u in mesh.data.uv_layers],
        "material_warnings": mat_warnings})

    bpy.ops.wm.save_as_mainfile(filepath=paths.original_blend, compress=True)

    # --- hair
    import re
    hair_re = re.compile(args.hair_regex, re.I) if args.hair_regex else None
    hair_mats = [i for i, s in enumerate(mesh.material_slots)
                 if hair_re and s.material and hair_re.search(s.material.name)]
    hair_faces = {p.index for p in mesh.data.polygons if p.material_index in hair_mats}

    # --- hidden triangles
    excl_re = re.compile(args.hidden_exclude, re.I) if args.hidden_exclude else None
    exclude = set(hair_mats) | {i for i, s in enumerate(mesh.material_slots)
                                if excl_re and s.material and excl_re.search(s.material.name)}
    hidden, pose_log, fwd = (set(), [], None)
    if not args.no_hidden:
        hidden, pose_log, fwd = hidden_faces(arm, mesh, inv, paths, args, exclude)
    hidden_by_mat = collections.Counter(mesh.material_slots[mesh.data.polygons[i].material_index].material.name
                                        for i in hidden)

    # --- slot decisions (ground truth for later automatic triage)
    decisions = {}
    by_slot = {s["slot_name"]: s for s in inv["slots"]}
    for i, s in enumerate(mesh.material_slots):
        name = s.material.name
        minfo = inv["materials"].get(by_slot.get(name, {}).get("material") or "", {})
        blend = str(minfo.get("blend_mode", "BLEND_OPAQUE"))
        if i in hair_mats:
            d, why = "remove-hair", "material name matches the hair regex '%s'" % args.hair_regex
        elif "TRANSLUCENT" in blend or "ADDITIVE" in blend or "MODULATE" in blend:
            d, why = "separate-transparent", "blend mode %s cannot share an opaque atlas slot" % blend
        elif before[name] and hidden_by_mat.get(name, 0) >= before[name]:
            d, why = "remove-hidden", "every triangle hidden in all %d test poses" % len(pose_log)
        else:
            d = "keep"
            why = "opaque%s; %d of %d triangles hidden in every test pose and removed" % (
                " (masked: needs an opacity mask in the atlas)" if "MASKED" in blend else "",
                hidden_by_mat.get(name, 0), before[name])
        decisions[name] = {"slot_index": i, "decision": d, "reason": why, "blend_mode": blend,
                           "two_sided": minfo.get("two_sided"), "triangles": before[name],
                           "hidden_triangles_removed": hidden_by_mat.get(name, 0)}
    pc.save_json(os.path.join(paths.source, "slot_decisions.json"), {
        "_doc": "Ground truth: slot -> keep / remove-hair / remove-hidden / separate-transparent with the reason. "
                "Hidden = no ray from the triangle escapes to a viewer in the rest pose, %d procedural extreme poses "
                "and the sampled animation frames (single-sided: back faces do not occlude)." % len(POSES),
        "hair_regex": args.hair_regex, "slots": decisions})

    remove = hair_faces | hidden
    if remove:
        delete_faces(mesh, remove)
    after = tri_counts(mesh)
    pc.update_summary(paths, "clean", {
        "hair_slots": [mesh.material_slots[i].material.name for i in hair_mats],
        "hair_triangles_removed": len(hair_faces),
        "hidden_triangles_removed": len(hidden),
        "hidden_triangles_by_slot": dict(hidden_by_mat),
        "triangles_after": sum(after.values()), "triangles_after_per_slot": after,
        "vertices_after": len(mesh.data.vertices),
        "hidden_method": {"rays_sphere": args.dirs, "samples_per_triangle": 4, "keep_ring": args.keep_ring,
                          "anim_frames_per_anim": args.anim_frames, "forward": list(fwd) if fwd else None,
                          "poses": pose_log}})
    # a vertex group per removed set is not needed; keep the armature, weights, morphs as imported
    bpy.ops.wm.save_as_mainfile(filepath=paths.clean_blend, compress=True)
    print("[pipeline] CLEAN_DONE tris %d -> %d (hair %d, hidden %d)" % (
        sum(before.values()), sum(after.values()), len(hair_faces), len(hidden)))


main()
