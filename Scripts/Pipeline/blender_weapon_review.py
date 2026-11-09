"""Weapon pipeline, stage 5 (Blender Cycles): before / after renders -> 06_Review/cells/*.png.

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_review.py -- --asset-dir <ArtPipeline/Weapons/X>
        [--samples 24] [--width 720] [--height 480] [--only Part,Part]

BEFORE = the untouched high objects with the pack's original materials (4K texture sets, loaded per part and freed),
AFTER = the reduced meshes with the ONE atlas material (the three baked 1K maps). Same camera, same light. The cells are
assembled into labelled sheets by Scripts/Pipeline/weapon_review_sheet.py (system Python + PIL).
"""
import argparse
import math
import os
import sys

import bpy
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_common as wc  # noqa: E402
import blender_weapon_bake as bk  # noqa: E402
import blender_weapon_export as ex  # noqa: E402


def setup_world(strength=0.9):
    sc = bpy.context.scene
    w = bpy.data.worlds.new("W") if not sc.world else sc.world
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    bg = nt.nodes.new("ShaderNodeBackground")
    bg.inputs["Color"].default_value = (0.42, 0.45, 0.50, 1)
    bg.inputs["Strength"].default_value = strength
    out = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(bg.outputs[0], out.inputs[0])
    sun = bpy.data.lights.new("Sun", "SUN")
    sun.energy = 3.5
    sun.angle = math.radians(12)
    so = bpy.data.objects.new("Sun", sun)
    sc.collection.objects.link(so)
    so.rotation_euler = (math.radians(50), math.radians(10), math.radians(-35))
    fill = bpy.data.lights.new("Fill", "SUN")
    fill.energy = 1.0
    fo = bpy.data.objects.new("Fill", fill)
    sc.collection.objects.link(fo)
    fo.rotation_euler = (math.radians(60), 0, math.radians(140))
    cam = bpy.data.cameras.new("Cam")
    cam.lens = 85
    cam.clip_end = 100000
    co = bpy.data.objects.new("Cam", cam)
    sc.collection.objects.link(co)
    sc.camera = co
    return co


def frame(cam, objs, direction, margin=0.8, up_hint=None):
    pts = []
    for o in objs:
        pts += [o.matrix_world @ Vector(c) for c in o.bound_box]
    mn = Vector([min(p[i] for p in pts) for i in range(3)])
    mx = Vector([max(p[i] for p in pts) for i in range(3)])
    c = (mn + mx) * 0.5
    r = (mx - mn).length * 0.5
    d = Vector(direction).normalized()
    sc = bpy.context.scene
    aspect = sc.render.resolution_x / sc.render.resolution_y
    fov = 2 * math.atan((cam.data.sensor_width * 0.5) / cam.data.lens)
    fov_v = 2 * math.atan(math.tan(fov / 2) / aspect) if aspect > 1 else fov
    dist = r * margin / math.sin(min(fov, fov_v) * 0.5)
    cam.location = c + d * dist
    up = Vector(up_hint) if up_hint else (Vector((0, 1, 0)) if abs(d.z) > 0.9 else Vector((0, 0, 1)))
    # build the camera basis explicitly (right-handed, looks along -Z)
    fwd = (c - cam.location).normalized()
    right = fwd.cross(up).normalized()
    realup = right.cross(fwd).normalized()
    from mathutils import Matrix
    m = Matrix((right, realup, -fwd)).transposed().to_4x4()
    cam.matrix_world = Matrix.Translation(cam.location) @ m


VIEWS = {"side": {"dir": (0, -1, 0.02)}, "top": {"dir": (0, 0, 1)}, "front34": {"dir": (0.75, -0.85, 0.5)}}


def render(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    sc.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    ap.add_argument("--samples", type=int, default=24)
    ap.add_argument("--width", type=int, default=720)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--only", default="")
    ap.add_argument("--phase", default="before,after", help="before / after / before,after (the 'before' cells do not need the bake)")
    args = ap.parse_args(argv)
    phases = args.phase.split(",")
    ctx = wc.Ctx(args.asset_dir)
    t = wc.Timer()
    bpy.ops.wm.open_mainfile(filepath=ctx.baked_blend if os.path.exists(ctx.baked_blend) and "after" in phases else ctx.low_blend)
    cfg = ctx.config
    device = wc.setup_cycles(args.samples)
    sc = bpy.context.scene
    sc.cycles.use_denoising = True
    try:
        sc.cycles.denoiser = "OPTIX"
    except Exception:
        pass
    sc.render.resolution_x, sc.render.resolution_y = args.width, args.height
    sc.render.film_transparent = False
    for k, v in (cfg.get("views") or {}).items():
        VIEWS[k] = v
    cam = setup_world()
    cells = os.path.join(ctx.review, "cells")
    os.makedirs(cells, exist_ok=True)
    highs = {}
    for o in bpy.data.objects:
        if o.type == "MESH" and "part" in o:
            highs.setdefault(o["part"], []).append(o)
    lows = {o.name: o for o in bpy.data.objects if o.type == "MESH" and "part" not in o}
    only = [s for s in args.only.split(",") if s]
    main_part = cfg.get("main_part", "")
    atlas = ex.atlas_material(ctx, "M_" + ctx.asset) if "after" in phases else None
    flip_green = bool(cfg.get("normal_flip_green", False))
    for o in list(highs.values()) + list(lows.values()):
        pass

    def set_visible(objs, state):
        for o in objs:
            o.hide_set(not state)
            o.hide_render = not state

    for o in bpy.data.objects:
        if o.type == "MESH":
            set_visible([o], False)
    done = []
    for part in cfg["parts"]:
        if (only and part not in only) or part not in highs or part not in lows:
            continue
        views = cfg["parts"][part].get("review_views") or (["side", "top", "front34"] if part == main_part else ["front34"])
        low = lows[part]
        if "before" in phases:
            mats = bk.dress_high(ctx, highs[part], flip_green)
            for m in mats.values():
                bk.set_pass(m, "normal")  # principled output
            set_visible(highs[part], True)
            for v in views:
                vw = VIEWS[v]
                frame(cam, highs[part], vw["dir"], up_hint=vw.get("up"))
                render(os.path.join(cells, "%s__%s__before.png" % (part, v)))
            set_visible(highs[part], False)
            bk.undress_high(highs[part])
            for m in mats.values():
                bpy.data.materials.remove(m)
            for img in [i for i in bpy.data.images if i.filepath and "T_%s_" % ctx.asset not in i.filepath and i.name != "Render Result"]:
                bpy.data.images.remove(img)
        if "after" in phases:
            low.data.materials.clear()
            low.data.materials.append(atlas)
            set_visible([low], True)
            for v in views:
                vw = VIEWS[v]
                frame(cam, [low], vw["dir"], up_hint=vw.get("up"))
                render(os.path.join(cells, "%s__%s__after.png" % (part, v)))
            set_visible([low], False)
        done.append(part)
        print("[weapon] reviewed %-12s (%.0fs)" % (part, t.elapsed()))
    wc.update_summary(ctx, "review", {"device": device, "parts": done, "cells": "06_Review/cells", "samples": args.samples})
    print("[weapon] REVIEW_DONE %d parts (%.0fs)" % (len(done), t.elapsed()))


if __name__ == "__main__":
    main()
