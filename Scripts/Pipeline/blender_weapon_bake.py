"""Weapon pipeline, stage 3 (Blender Cycles, GPU when available): bake the shared 1K atlas set.

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_bake.py -- --asset-dir <ArtPipeline/Weapons/X>
        [--samples 4] [--cage-extrusion 0.2] [--max-ray 0.8] [--dilate-px 8] [--only Part,Part]

Reads 03_Blender/<Asset>_low.blend (stage 2: low parts with `UV_Atlas` + hidden high objects with `part` / `src_mat`)
and weapon.json {"materials": {src_mat: {"textures": {"base","normal","rough","metal","ao"} (png names, no extension) |
"flat": {"base": [r,g,b] linear, "rough", "metal"}}}, "normal_flip_green": false}.

Each part is projected from its own high objects (Cycles selected-to-active: only that part's high is a ray target) into
the one atlas. Per part the high materials are rebuilt from the source textures and freed afterwards (4K x ~60 images).
Passes (emit bake of the texture, linear values): base colour, roughness, metallic, ambient occlusion (the pack's own AO
map) and the tangent-space normal (which includes the high's normal map). A "hit" pass finds ray misses; the atlas is then
dilated (8-neighbour) into the gutters / misses.

Outputs (04_Textures_Baked): T_<Asset>_BaseColor.png (8-bit sRGB), T_<Asset>_Normal.png (16-bit, DirectX / green down),
T_<Asset>_ORM.png (8-bit linear: R = AO, G = roughness, B = metallic) + 06_Review/atlas_layout.png (islands, per part).
"""
import argparse
import json
import math
import os
import sys

import bpy
import numpy as np

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_common as wc  # noqa: E402

PASSES = ("base", "rough", "metal", "ao", "hit", "normal")


def new_image(name, res, colorspace, fill):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, res, res, alpha=True, float_buffer=True)
    img.colorspace_settings.name = colorspace
    img.generated_color = fill
    img.alpha_mode = "STRAIGHT"
    return img


def pixels(img):
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def tex_path(ctx, name):
    for d in (ctx.tex_src, ctx.config.get("texture_dir", "")):
        if d:
            p = os.path.join(d, name + ".png")
            if os.path.exists(p):
                return p
    raise FileNotFoundError("texture %s not found (02_Textures_Source / texture_dir)" % name)


def build_source_material(ctx, src, spec, flip_green):
    """High-poly material of a pack material: Principled (normal map wired) + an Emission used for the colour passes."""
    mat = bpy.data.materials.new("SRC_" + src)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = ctx.config.get("source_uv", "UVMap")
    sockets = {}

    def image(kind, colorspace):
        n = nt.nodes.new("ShaderNodeTexImage")
        img = bpy.data.images.load(tex_path(ctx, spec["textures"][kind]), check_existing=False)
        img.colorspace_settings.name = colorspace
        n.image = img
        n.interpolation = "Linear"
        nt.links.new(uvn.outputs[0], n.inputs[0])
        return n

    if "flat" in spec:
        f = spec["flat"]
        for kind, default in (("base", [0.5, 0.5, 0.5]), ("rough", 0.5), ("metal", 0.0), ("ao", 1.0)):
            v = f.get(kind, default)
            n = nt.nodes.new("ShaderNodeRGB")
            n.outputs[0].default_value = (v[0], v[1], v[2], 1) if isinstance(v, list) else (v, v, v, 1)
            sockets[kind] = n.outputs[0]
    else:
        tx = spec["textures"]
        sockets["base"] = image("base", "sRGB").outputs[0]
        for kind in ("rough", "metal", "ao"):
            if kind in tx:
                sockets[kind] = image(kind, "Non-Color").outputs[0]
            else:
                n = nt.nodes.new("ShaderNodeRGB")
                v = {"rough": 0.5, "metal": 0.0, "ao": 1.0}[kind]
                n.outputs[0].default_value = (v, v, v, 1)
                sockets[kind] = n.outputs[0]
        if "normal" in tx:
            nm = nt.nodes.new("ShaderNodeNormalMap")
            nm.space = "TANGENT"
            nm.uv_map = ctx.config.get("source_uv", "UVMap")
            ni = image("normal", "Non-Color")
            if flip_green:
                sep = nt.nodes.new("ShaderNodeSeparateColor")
                com = nt.nodes.new("ShaderNodeCombineColor")
                inv = nt.nodes.new("ShaderNodeMath")
                inv.operation = "SUBTRACT"
                inv.inputs[0].default_value = 1.0
                nt.links.new(ni.outputs[0], sep.inputs[0])
                nt.links.new(sep.outputs[0], com.inputs[0])
                nt.links.new(sep.outputs[1], inv.inputs[1])
                nt.links.new(inv.outputs[0], com.inputs[1])
                nt.links.new(sep.outputs[2], com.inputs[2])
                nt.links.new(com.outputs[0], nm.inputs["Color"])
            else:
                nt.links.new(ni.outputs[0], nm.inputs["Color"])
            nt.links.new(nm.outputs[0], bsdf.inputs["Normal"])
    # separate R of the grey maps
    def red(sock):
        sep = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(sock, sep.inputs[0])
        return sep.outputs[0]

    nt.links.new(sockets["base"], bsdf.inputs["Base Color"])
    nt.links.new(red(sockets["rough"]), bsdf.inputs["Roughness"])
    nt.links.new(red(sockets["metal"]), bsdf.inputs["Metallic"])
    em = nt.nodes.new("ShaderNodeEmission")
    em.label = "PASS_EMIT"
    mat["bsdf"] = bsdf.name
    mat["emit"] = em.name
    mat["out"] = out.name
    # remember the output sockets for pass switching (node name + index)
    mat["src_nodes"] = json.dumps({k: [s.node.name, [i for i, o in enumerate(s.node.outputs) if o == s][0]] for k, s in sockets.items()})
    return mat


def dress_high(ctx, objs, flip_green):
    """Give every high object the rebuilt source materials of its slots (face material indices are kept)."""
    cfg = ctx.config
    mats = {}
    for o in objs:
        names = json.loads(o["src_mats"]) if "src_mats" in o else [o["src_mat"]]
        for src in names:
            if src not in mats:
                mats[src] = build_source_material(ctx, src, cfg["materials"][src], flip_green or cfg["materials"][src].get("flip_green", False))
        # replace slot by slot: clearing the slots would reset every face's material index to 0
        for i, src in enumerate(names):
            if i < len(o.data.materials):
                o.data.materials[i] = mats[src]
            else:
                o.data.materials.append(mats[src])
    return mats


def undress_high(objs):
    """After the bake: bare placeholder materials (original names) instead of the rebuilt ones (4K images freed)."""
    for o in objs:
        names = json.loads(o["src_mats"]) if "src_mats" in o else [o["src_mat"]]
        for i, src in enumerate(names):
            ph = bpy.data.materials.get("PH_" + src) or bpy.data.materials.new("PH_" + src)
            o.data.materials[i] = ph


def set_pass(mat, kind):
    nt = mat.node_tree
    out = nt.nodes[mat["out"]]
    em = nt.nodes[mat["emit"]]
    for l in list(em.inputs["Color"].links):
        nt.links.remove(l)
    if kind == "normal":
        nt.links.new(nt.nodes[mat["bsdf"]].outputs[0], out.inputs["Surface"])
        return
    if kind == "hit":
        em.inputs["Color"].default_value = (1, 1, 1, 1)
    else:
        node_name, idx = json.loads(mat["src_nodes"])[kind]
        sock = nt.nodes[node_name].outputs[idx]
        if kind in ("rough", "metal", "ao"):
            # grey maps: take the red channel into all three
            sep = next((n for n in nt.nodes if n.label == "RED_" + kind), None)
            if sep is None:
                sep = nt.nodes.new("ShaderNodeSeparateColor")
                sep.label = "RED_" + kind
                nt.links.new(sock, sep.inputs[0])
            comb = next((n for n in nt.nodes if n.label == "GREY_" + kind), None)
            if comb is None:
                comb = nt.nodes.new("ShaderNodeCombineColor")
                comb.label = "GREY_" + kind
                for i in range(3):
                    nt.links.new(sep.outputs[0], comb.inputs[i])
            sock = comb.outputs[0]
        nt.links.new(sock, em.inputs["Color"])
    nt.links.new(em.outputs[0], out.inputs["Surface"])


def target_material(name):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.label = "BAKE_target"
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = wc.ATLAS_UV
    nt.links.new(uvn.outputs[0], tex.inputs[0])
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1, 1, 1, 1)
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    nt.nodes.active = tex
    return mat, tex


def raster_islands(ctx, lows, res, path):
    """Atlas layout preview: every part's triangles in its own colour (numpy scan conversion)."""
    import colorsys
    img = np.zeros((res, res, 3), np.float32)
    rng = {}
    for i, (part, low) in enumerate(lows.items()):
        me = low.data
        uv = np.empty(len(me.loops) * 2, np.float32)
        me.uv_layers[wc.ATLAS_UV].data.foreach_get("uv", uv)
        uv = uv.reshape(-1, 2) * res
        col = np.array(colorsys.hsv_to_rgb(i / max(1, len(lows)), 0.55, 0.95), np.float32)
        for p in me.polygons:
            tri = uv[list(p.loop_indices)[:3]]
            x0, y0 = np.floor(tri.min(0)).astype(int)
            x1, y1 = np.ceil(tri.max(0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0)
            x1, y1 = min(x1, res - 1), min(y1, res - 1)
            if x1 < x0 or y1 < y0:
                continue
            xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            a, b, c = tri
            d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(d) < 1e-9:
                continue
            l1 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
            l2 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
            m = (l1 >= -0.02) & (l2 >= -0.02) & (1 - l1 - l2 >= -0.02)
            img[y0:y1 + 1, x0:x1 + 1][m] = col
        rng[part] = col.tolist()
    wc.write_png(path, img[::-1], 8)
    return rng


def srgb_encode(a):
    a = np.clip(a, 0.0, 1.0)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * np.power(a, 1 / 2.4) - 0.055)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    ap.add_argument("--samples", type=int, default=4)
    ap.add_argument("--cage-extrusion", type=float, default=0.2)
    ap.add_argument("--max-ray", type=float, default=0.8)
    ap.add_argument("--dilate-px", type=int, default=8)
    ap.add_argument("--only", default="")
    ap.add_argument("--cpu", action="store_true")
    args = ap.parse_args(argv)
    ctx = wc.Ctx(args.asset_dir)
    t = wc.Timer()
    bpy.ops.wm.open_mainfile(filepath=ctx.low_blend)
    cfg = ctx.config
    res = int(cfg.get("res", 1024))
    flip_green = bool(cfg.get("normal_flip_green", False))
    device = wc.setup_cycles(args.samples, "CPU" if args.cpu else "GPU")
    print("[weapon] cycles device:", device)
    scene = bpy.context.scene
    scene.cycles.use_adaptive_sampling = False

    lows = {o["part"] if "part" in o else o.name: o for o in bpy.data.objects if o.type == "MESH" and not o.hide_render and "part" not in o}
    # low objects carry no custom property: names are the part names
    lows = {o.name: o for o in bpy.data.objects if o.type == "MESH" and "part" not in o}
    highs = {}
    for o in bpy.data.objects:
        if o.type == "MESH" and "part" in o:
            highs.setdefault(o["part"], []).append(o)
    only = [s for s in args.only.split(",") if s]

    images = {
        # every pass image is Non-Color: Image.pixels of a float image in the sRGB space returns sRGB-ENCODED values
        # (a double encode when the PNG is written); the emission bake itself stores linear values
        "base": new_image("A_base", res, "Non-Color", (0.0, 0.0, 0.0, 0.0)),
        "rough": new_image("A_rough", res, "Non-Color", (0.5, 0.5, 0.5, 0.0)),
        "metal": new_image("A_metal", res, "Non-Color", (0.0, 0.0, 0.0, 0.0)),
        "ao": new_image("A_ao", res, "Non-Color", (1.0, 1.0, 1.0, 0.0)),
        "hit": new_image("A_hit", res, "Non-Color", (0.0, 0.0, 0.0, 0.0)),
        "cover": new_image("A_cover", res, "Non-Color", (0.0, 0.0, 0.0, 0.0)),
        "normal": new_image("A_normal", res, "Non-Color", (0.5, 0.5, 1.0, 0.0)),
    }
    tmat, ttex = target_material("BAKE_TARGET")

    def bake(kind, low, hi, margin=0):
        ttex.image = images[kind]
        bpy.context.view_layer.objects.active = low
        low.data.uv_layers.active = low.data.uv_layers[wc.ATLAS_UV]
        kw = dict(uv_layer=wc.ATLAS_UV, margin=margin, use_clear=False, target="IMAGE_TEXTURES")
        if hi:
            gs = float(cfg.get("geometry_scale", 1.0))
            kw.update(use_selected_to_active=True, cage_extrusion=args.cage_extrusion * gs, max_ray_distance=args.max_ray * gs)
        if kind == "normal":
            bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", normal_r="POS_X", normal_g="NEG_Y", normal_b="POS_Z", **kw)
        else:
            bpy.ops.object.bake(type="EMIT", **kw)

    report = {"device": device, "parts": {}}
    for part, low in lows.items():
        if only and part not in only:
            continue
        if part not in highs:
            continue
        # unhide for baking
        for o in highs[part] + [low]:
            o.hide_set(False)
            o.hide_render = False
            o.hide_viewport = False
        mats = dress_high(ctx, highs[part], flip_green)
        low.data.materials.clear()
        low.data.materials.append(tmat)
        # coverage of the low's own triangles (white emission, no projection)
        bpy.ops.object.select_all(action="DESELECT")
        low.select_set(True)
        bpy.context.view_layer.objects.active = low
        bake("cover", low, False)
        for kind in PASSES:
            for m in mats.values():
                set_pass(m, kind)
            bpy.ops.object.select_all(action="DESELECT")
            for o in highs[part]:
                o.select_set(True)
            low.select_set(True)
            bake(kind, low, True)
        report["parts"][part] = {"high_materials": sorted(mats), "bake_s": round(t.elapsed(), 1)}
        print("[weapon] baked %-12s (%.0fs)" % (part, t.elapsed()))
        # free the 4K textures of this part and hide its high objects again
        undress_high(highs[part])
        for o in highs[part]:
            o.hide_set(True)
            o.hide_render = True
        for m in mats.values():
            bpy.data.materials.remove(m)
        for img in [i for i in bpy.data.images if i.name not in [x.name for x in images.values()] and i.filepath]:
            bpy.data.images.remove(img)

    # ---- post: coverage, misses, dilation, packing -------------------------------------------------------------
    px = {k: pixels(v) for k, v in images.items()}
    # flat-colour swatches (parts that only need a colour: no projection, the rectangle is filled directly)
    swatch_rects = {}
    for part, low in lows.items():
        if "swatch" not in low:
            continue
        spec = json.loads(low["swatch"])
        flat = cfg["materials"][spec["material"]]["flat"]
        uv = np.empty(len(low.data.loops) * 2, np.float32)
        low.data.uv_layers[wc.ATLAS_UV].data.foreach_get("uv", uv)
        uv = uv.reshape(-1, 2) * res
        x0, y0 = np.ceil(uv.min(0) + 1.5).astype(int)
        x1, y1 = np.floor(uv.max(0) - 1.5).astype(int)
        sl = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        px["base"][sl][..., :3] = np.array(flat["base"], np.float32)
        px["rough"][sl][..., :3] = flat.get("rough", 0.5)
        px["metal"][sl][..., :3] = flat.get("metal", 0.0)
        px["ao"][sl][..., :3] = 1.0
        px["normal"][sl][..., :3] = (0.5, 0.5, 1.0)
        px["hit"][sl][..., 0] = 1.0
        px["cover"][sl][..., 0] = 1.0
        swatch_rects[part] = [int(x0), int(y0), int(x1), int(y1)]
        report["parts"][part] = {"swatch_px": [int(x1 - x0 + 1), int(y1 - y0 + 1)], "material": spec["material"]}
    report["swatch_rects_px_bottom_up"] = swatch_rects
    cover = px["cover"][..., 0] > 0.5
    hit = px["hit"][..., 0] > 0.5
    valid = cover & hit
    misses = cover & ~hit
    report["coverage_percent"] = round(float(cover.mean()) * 100, 2)
    report["ray_miss_percent_of_covered"] = round(float(misses.sum()) / max(1, int(cover.sum())) * 100, 3)
    d = args.dilate_px
    base = wc.dilate(px["base"][..., :3], valid, d)
    rough = wc.dilate(px["rough"][..., :3], valid, d)[..., 0]
    metal = wc.dilate(px["metal"][..., :3], valid, d)[..., 0]
    ao = wc.dilate(px["ao"][..., :3], valid, d)[..., 0]
    nrm = wc.dilate(px["normal"][..., :3], valid, d)
    filled = wc.dilate(np.zeros((res, res, 3), np.float32), valid, d)  # unused, keeps shapes honest
    ever = wc.dilate(valid[..., None].astype(np.float32).repeat(3, 2), valid, d)[..., 0] > 0
    base = np.where(ever[..., None], base, 0.05)
    rough = np.where(ever, rough, 0.6)
    metal = np.where(ever, metal, 0.0)
    ao = np.where(ever, ao, 1.0)
    nrm = np.where(ever[..., None], nrm, np.array([0.5, 0.5, 1.0], np.float32))
    # normals: re-normalise (the dilation averages vectors)
    v = nrm * 2.0 - 1.0
    ln = np.maximum(np.linalg.norm(v, axis=2, keepdims=True), 1e-6)
    nrm = (v / ln) * 0.5 + 0.5
    # clamp normals that point into the surface (rays that reached a back face)
    v = nrm * 2.0 - 1.0
    bad = v[..., 2] < 0.05
    report["normal_texels_clamped_percent"] = round(float((bad & valid).sum()) / max(1, int(valid.sum())) * 100, 3)
    v[bad, 2] = 0.05
    v /= np.maximum(np.linalg.norm(v, axis=2, keepdims=True), 1e-6)
    nrm = v * 0.5 + 0.5
    os.makedirs(ctx.baked, exist_ok=True)
    wc.write_png(ctx.tex("BaseColor"), srgb_encode(base)[::-1], 8)
    wc.write_png(ctx.tex("Normal"), nrm[::-1], 16)
    wc.write_png(ctx.tex("ORM"), np.stack([ao, rough, metal], axis=2)[::-1], 8)
    report["ao_mean"] = round(float(ao[valid].mean()), 3)
    report["roughness_mean"] = round(float(rough[valid].mean()), 3)
    report["metallic_mean"] = round(float(metal[valid].mean()), 3)
    report["metallic_percent_texels_above_0_5"] = round(float((metal[valid] > 0.5).mean()) * 100, 2)
    # miss map for the review (red = miss)
    vis = np.zeros((res, res, 3), np.float32)
    vis[cover] = (0.25, 0.25, 0.25)
    vis[misses] = (1.0, 0.0, 0.0)
    wc.write_png(os.path.join(ctx.review, "bake_misses.png"), vis[::-1], 8)
    layout = raster_islands(ctx, {k: v for k, v in lows.items() if k in highs}, res, os.path.join(ctx.review, "atlas_layout.png"))
    report["layout_colors"] = layout
    report["settings"] = {"res": res, "samples": args.samples, "cage_extrusion_cm": args.cage_extrusion,
                          "max_ray_cm": args.max_ray, "dilate_px": d, "normal_flip_green_source": flip_green,
                          "normal_output": "DirectX (green down), 16-bit PNG", "orm": "R=AO G=Roughness B=Metallic, linear 8-bit"}
    report["seconds"] = round(t.elapsed(), 1)
    wc.update_summary(ctx, "bake", report)
    bpy.ops.wm.save_as_mainfile(filepath=ctx.baked_blend, compress=True)
    print("[weapon] BAKE_DONE misses %.2f%% clamped %.2f%% (%.0fs)" % (
        report["ray_miss_percent_of_covered"], report["normal_texels_clamped_percent"], t.elapsed()))


if __name__ == "__main__":
    main()
