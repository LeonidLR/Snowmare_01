"""Character single-slot pipeline, stage 6: review renders + numbers (Blender background, Cycles).

    blender --background --factory-startup --python Scripts/Pipeline/blender_review.py -- --asset-dir <...>
        [--res 1024] [--samples 64] [--cpu]

Renders the ORIGINAL (03_Blender/<mesh>_original.blend: all slots, source textures, hidden parts present) and the
RESULT (05_Export/<mesh>_1slot.fbx with the baked atlas) from the same cameras and neutral light:
front 3/4, back 3/4, tactical (high top-down like the game camera) and a face close-up. Writes
06_Review/original_<view>.png, result_<view>.png, compare_<view>.png (original | result | |difference| x4) and the
QA numbers into 06_Review/summary.json: per-view RGB RMSE, mean / p95 CIE76 dE, silhouette IoU; slots, triangles,
texture memory before / after.
"""
import argparse
import math
import os
import struct
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402
from blender_export import build_atlas_material  # noqa: E402
from blender_clean import POSES, apply_pose, reset_pose  # noqa: E402

POSE_SHEET = ("crouch_deep", "kneel_aim", "arms_up", "twist_bend", "prone_look_up")

VIEWS = {  # name: (azimuth from the character's forward, elevation, framing)
    "front34": (35.0, 10.0, "body"),
    "back34": (215.0, 15.0, "body"),
    "tactical": (30.0, 58.0, "body"),
    "face": (15.0, 5.0, "face"),
}


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--res", type=int, default=1024)
    p.add_argument("--samples", type=int, default=64)
    p.add_argument("--cpu", action="store_true")
    return p.parse_args(pc.script_args())


def setup_render(args):
    scene = bpy.context.scene
    pc.setup_cycles(samples=args.samples, device="CPU" if args.cpu else "GPU")
    scene.cycles.use_denoising = True
    scene.render.resolution_x = scene.render.resolution_y = args.res
    scene.render.film_transparent = True
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    world = bpy.data.worlds.new("Neutral")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background") or world.node_tree.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.35, 0.35, 0.35, 1)
    bg.inputs["Strength"].default_value = 1.0
    scene.world = world
    sun = bpy.data.lights.new("Key", "SUN")
    sun.energy = 3.0
    sun.angle = math.radians(5)
    so = bpy.data.objects.new("Key", sun)
    scene.collection.objects.link(so)
    return so


def forward_and_head(arm, mesh):
    bones = arm.data.bones
    hb = bones.get("head") or next((b for b in bones if b.name.lower().endswith("head")), None) or         next((b for b in bones if "head" in b.name.lower()), None) or max(bones, key=lambda b: b.head_local.z)
    head = arm.matrix_world @ hb.head_local
    pelvis = next((b for b in bones if "pelvis" in b.name.lower()), None)
    face = [i for i, s in enumerate(mesh.material_slots) if s.material and "mask" in s.material.name.lower()]
    acc, n = Vector(), 0
    for p in mesh.data.polygons:
        if p.material_index in face:
            acc += mesh.matrix_world @ p.center
            n += 1
    if n:
        f = acc / n - head
    elif pelvis is not None:  # no face material (a quadruped): the body points from the pelvis to the head
        f = head - arm.matrix_world @ pelvis.head_local
    else:
        f = Vector((0, -1, 0))
    f.z = 0
    return (f.normalized() if f.length > 1e-3 else Vector((0, -1, 0))), head


def camera_specs(mesh, arm):
    fwd, head = forward_and_head(arm, mesh)
    pts = [mesh.matrix_world @ Vector(c) for c in mesh.bound_box]
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    center = (lo + hi) / 2
    radius = (hi - lo).length / 2
    specs = {}
    for name, (az, el, frame) in VIEWS.items():
        d = Matrix.Rotation(math.radians(az), 3, "Z") @ fwd
        d = (d * math.cos(math.radians(el)) + Vector((0, 0, math.sin(math.radians(el))))).normalized()
        if frame == "face":
            target, lens, dist = head + Vector((0, 0, 4.0)), 85.0, 120.0
        else:
            lens = 50.0
            fov = 2 * math.atan(18.0 / lens)
            target, dist = center, radius / math.sin(fov / 2) * 1.02
        loc = target + d * dist
        rot = (target - loc).to_track_quat("-Z", "Y").to_euler()
        specs[name] = {"location": list(loc), "rotation": list(rot), "lens": lens}
    sun_dir = (Matrix.Rotation(math.radians(-40), 3, "Z") @ fwd)
    sun_dir = (sun_dir * math.cos(math.radians(50)) + Vector((0, 0, math.sin(math.radians(50))))).normalized()
    return specs, list(sun_dir)


def render_views(specs, sun_dir, sun_obj, prefix, review_dir):
    scene = bpy.context.scene
    sun_obj.rotation_euler = (-Vector(sun_dir)).to_track_quat("-Z", "Y").to_euler()
    cam_data = bpy.data.cameras.new("ReviewCam")
    cam_data.sensor_width = 36.0
    cam_data.clip_start = 1.0
    cam_data.clip_end = 10000.0
    cam = bpy.data.objects.new("ReviewCam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    out = {}
    for name, s in specs.items():
        cam.location = s["location"]
        cam.rotation_euler = s["rotation"]
        cam_data.lens = s["lens"]
        path = os.path.join(review_dir, "%s_%s.png" % (prefix, name))
        tmp = os.path.join(review_dir, "_render_tmp.png")
        scene.render.filepath = tmp
        with pc.Timer("render %s %s" % (prefix, name)):
            bpy.ops.render.render(write_still=True)
        out[name] = replace_file(tmp, path)
    return out


def replace_file(src, dst):
    """Move a finished render into place; an image viewer may hold the old file open (Windows lock)."""
    import time
    for _ in range(10):
        try:
            os.replace(src, dst)
            return dst
        except OSError:
            time.sleep(1.0)
    alt = dst[:-4] + "_new.png"
    os.replace(src, alt)
    print("[pipeline] WARNING %s is locked, wrote %s" % (dst, alt))
    return alt


def render_poses(arm, fwd, specs, sun_dir, sun, prefix, review_dir, res=512):
    """The same camera (front 3/4) for every extreme pose of the pose sheet."""
    scene = bpy.context.scene
    keep = (scene.render.resolution_x, scene.cycles.samples)
    scene.render.resolution_x = scene.render.resolution_y = res
    scene.cycles.samples = 32
    out = {}
    for pose in POSE_SHEET:
        apply_pose(arm, POSES[pose], fwd)
        out[pose] = render_views({"front34": specs["front34"]}, sun_dir, sun, "%s_pose_%s" % (prefix, pose),
                                 review_dir)["front34"]
    reset_pose(arm)
    scene.render.resolution_x = scene.render.resolution_y = keep[0]
    scene.cycles.samples = keep[1]
    return out


def pose_sheet(orig, res, path):
    """Columns = poses; top row original, bottom row reduced result."""
    bg = np.array([0.5, 0.5, 0.5])
    rows = []
    for d in (orig, res):
        tiles = []
        for pose in POSE_SHEET:
            a = pc.read_png_rgb(d[pose])
            tiles.append(a[..., :3] * a[..., 3:4] + bg * (1 - a[..., 3:4]))
        rows.append(np.concatenate(tiles, axis=1))
    tmp = os.path.join(os.path.dirname(path), "_sheet_tmp.png")
    pc.write_png(tmp, np.concatenate(rows, axis=0), 8)
    return replace_file(tmp, path)


def srgb_to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116.0)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def compare(orig_path, res_path, out_path):
    a = pc.read_png_rgb(orig_path)
    b = pc.read_png_rgb(res_path)
    ma, mb = a[..., 3] > 0.5, b[..., 3] > 0.5
    union, inter = ma | mb, ma & mb
    bg = np.array([0.5, 0.5, 0.5])
    ca = a[..., :3] * a[..., 3:4] + bg * (1 - a[..., 3:4])
    cb = b[..., :3] * b[..., 3:4] + bg * (1 - b[..., 3:4])
    diff = np.abs(ca - cb)
    rmse = float(np.sqrt((diff[union] ** 2).mean()) * 255) if union.any() else 0.0
    de = np.linalg.norm(srgb_to_lab(np.clip(ca, 0, 1)) - srgb_to_lab(np.clip(cb, 0, 1)), axis=-1)[union]
    panel = np.concatenate([ca, cb, np.clip(diff * 4, 0, 1)], axis=1)
    tmp = os.path.join(os.path.dirname(out_path), "_compare_tmp.png")
    pc.write_png(tmp, panel, 8)
    out_path = replace_file(tmp, out_path)
    return {"rgb_rmse_0_255": round(rmse, 2), "mean_delta_e76": round(float(de.mean()), 2) if de.size else 0.0,
            "p95_delta_e76": round(float(np.percentile(de, 95)), 2) if de.size else 0.0,
            "silhouette_iou": round(float(inter.sum() / max(1, union.sum())), 4),
            "pixels_compared": int(union.sum()), "image": os.path.basename(out_path)}


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(26)
    return struct.unpack(">II", head[16:24])


def texture_memory(paths):
    """Estimated GPU memory (full mip chain) of the source textures used by the mesh vs the baked atlas."""
    import material_graph as mg
    inv = paths.inventory
    roles = pc.load_json(os.path.join(paths.source, "texture_roles.json")).get("textures", {})
    before, detail = 0.0, {}
    for tpath, t in inv["textures"].items():
        name = tpath.split(".")[-1]
        f = os.path.join(paths.tex_source, mg.texture_stem(inv, tpath) + ".png")
        if not os.path.exists(f):
            continue
        w, h = png_size(f)
        if t.get("compression") == "TC_NORMALMAP":
            fmt, bpp = "BC5", 1.0
        else:
            a = pc.read_png_rgb(f)[..., 3]
            has_alpha = bool((a < 0.999).any())
            fmt, bpp = ("DXT5", 1.0) if has_alpha else ("DXT1", 0.5)
        mb = w * h * bpp * 4 / 3 / 2 ** 20
        before += mb
        detail[name] = {"size": [w, h], "format": fmt, "mb": round(mb, 2), "role": roles.get(name, {}).get("role")}
    after_detail = {}
    for kind, fmt, bpp in (("BaseColor", "DXT1", 0.5), ("Normal", "BC5", 1.0), ("ORM", "DXT1 (TC_Masks)", 0.5),
                           ("Sheen", "G8", 1.0), ("Emissive", "DXT1", 0.5)):
        v = paths.inventory.get("variants") or []
        f = paths.tex(kind, variant=v[0]["name"] if v else None)
        if os.path.exists(f):
            w, h = png_size(f)
            after_detail[kind] = {"size": [w, h], "format": fmt, "mb": round(w * h * bpp * 4 / 3 / 2 ** 20, 2)}
    after = sum(d["mb"] for d in after_detail.values())
    return {"before_mb": round(before, 1), "after_mb": round(after, 1),
            "after_without_optional_mb": round(after - sum(after_detail.get(k, {}).get("mb", 0)
                                                       for k in ("Sheen",)), 1),
            "before_textures": len(detail), "before_detail": detail, "after_detail": after_detail,
            "assumptions": "full mip chain (x4/3); Default -> DXT1 when the PNG alpha is opaque else DXT5; "
                           "normal maps BC5; streaming / LOD bias not applied"}


def clean_face_keys(paths):
    bpy.ops.wm.open_mainfile(filepath=paths.clean_blend)
    m = bpy.data.objects[paths.mesh_name]
    return {tuple(round(c, 2) for c in p.center) for p in m.data.polygons}


def render_removed(mesh, keys, specs, sun_dir, sun, review_dir):
    """Ghosted original with the removed triangles in red: what the clean-up took away."""
    removed = [p.index for p in mesh.data.polygons if tuple(round(c, 2) for c in p.center) not in keys]
    ghost = bpy.data.materials.new("REVIEW_ghost")
    ghost.use_nodes = True
    b = ghost.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.8, 0.8, 0.8, 1)
    b.inputs["Alpha"].default_value = 0.12
    red = bpy.data.materials.new("REVIEW_removed")
    red.use_nodes = True
    nt = red.node_tree
    nt.nodes.remove(nt.nodes["Principled BSDF"])
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1.0, 0.05, 0.02, 1)
    em.inputs["Strength"].default_value = 2.0
    nt.links.new(em.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    me = mesh.data
    me.materials.clear()
    me.materials.append(ghost)
    me.materials.append(red)
    rs = set(removed)
    for p in me.polygons:
        p.material_index = 1 if p.index in rs else 0
    sub = {k: v for k, v in specs.items() if k in ("front34", "back34")}
    out = render_views(sub, sun_dir, sun, "removed", review_dir)
    return len(removed), out


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    keys = clean_face_keys(paths)

    # ORIGINAL
    bpy.ops.wm.open_mainfile(filepath=paths.original_blend)
    mesh = bpy.data.objects[paths.mesh_name]
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    import material_graph as mg
    mg.rebuild_mesh_materials([mesh], paths.inventory, paths.tex_source, [])
    specs, sun_dir = camera_specs(mesh, arm)
    pc.save_json(os.path.join(paths.review, "cameras.json"), {"views": specs, "sun_dir": sun_dir})
    slots_before = len(mesh.material_slots)
    tris_before = len(mesh.data.polygons)
    sun = setup_render(args)
    orig = render_views(specs, sun_dir, sun, "original", paths.review)
    fwd, _ = forward_and_head(arm, mesh)
    orig_poses = render_poses(arm, fwd, specs, sun_dir, sun, "original", paths.review)
    n_removed, _ = render_removed(mesh, keys, specs, sun_dir, sun, paths.review)

    # RESULT
    pc.reset_scene()
    a2, meshes, _ = pc.import_fbx(paths.export_fbx)
    m2 = meshes[0]
    mat = bpy.data.materials.new("M_Body_preview")
    mat.use_nodes = True
    build_atlas_material(mat, paths, uv_name=m2.data.uv_layers[0].name)
    m2.data.materials.clear()
    m2.data.materials.append(mat)
    slots_after = len(m2.material_slots)
    tris_after = len(m2.data.polygons)
    sun = setup_render(args)
    res = render_views(specs, sun_dir, sun, "result", paths.review)
    res_poses = render_poses(a2, fwd, specs, sun_dir, sun, "result", paths.review)
    sheet = pose_sheet(orig_poses, res_poses, os.path.join(paths.review, "pose_sheet.png"))
    for f in list(orig_poses.values()) + list(res_poses.values()):
        os.remove(f)

    views = {}
    for name in VIEWS:
        views[name] = compare(orig[name], res[name], os.path.join(paths.review, "compare_%s.png" % name))
    with pc.Timer("texture memory"):
        mem = texture_memory(paths)
    summary = pc.load_json(paths.report_path)
    numbers = {
        "slots_before": slots_before, "slots_after": slots_after,
        "triangles_before": tris_before, "triangles_after": tris_after,
        "triangles_removed_hidden": summary.get("clean", {}).get("hidden_triangles_removed"),
        "triangles_removed_hair": summary.get("clean", {}).get("hair_triangles_removed"),
        "triangles_removed_check_by_position": n_removed,
        "atlas_coverage_percent": summary.get("bake", {}).get("atlas_coverage_percent"),
        "texture_memory_before_mb": mem["before_mb"], "texture_memory_after_mb": mem["after_mb"],
        "hidden_in_all_poses_before_safety_ring": (summary.get("clean", {}).get("hidden_method", {})
                                                   .get("poses") or [{}])[-1].get("still_hidden"),
        "bake_warnings": summary.get("bake", {}).get("bake_warnings", []) +
                         summary.get("source", {}).get("material_warnings", []) + [
            "%s: %s" % (name, w) for name, t in pc.load_json(os.path.join(paths.source, "texture_roles.json"))
            .get("textures", {}).items() for w in t.get("warnings", []) if "Normal sampler" in w],
        "emissive_texel_fraction": (summary.get("bake", {}).get("emissive") or {}).get("nonzero_fraction"),
        "cross_material_overlap_percent": summary.get("atlas", {}).get("cross_material_overlap_percent"),
        "export_roundtrip_ok": summary.get("export", {}).get("checks", {}).get("ok"),
        "worst_view_mean_delta_e": max(v["mean_delta_e76"] for v in views.values()),
        "min_silhouette_iou": min(v["silhouette_iou"] for v in views.values()),
    }
    reduce = summary.get("reduce", {}).get("per_material", {})
    numbers["triangles_per_material_after"] = {k: v["after"] for k, v in reduce.items()} or None
    numbers["ray_miss_percent"] = summary.get("bake", {}).get("ray_miss_percent")
    pc.update_summary(paths, "review", {"pose_sheet": os.path.basename(sheet), "pose_sheet_poses": list(POSE_SHEET),
                                        "views": views, "texture_memory": mem, "render_samples": args.samples,
                                        "resolution": args.res})
    pc.update_summary(paths, "qa", numbers)
    print("[pipeline] REVIEW_DONE", numbers)


main()
