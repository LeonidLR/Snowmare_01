"""Character single-slot pipeline, stage 4: bake the atlas (Blender Cycles, GPU when available).

    blender --background --factory-startup --python Scripts/Pipeline/blender_bake.py -- --asset-dir <...>
        [--res 4096] [--orm-res 2048] [--aux-res 1024] [--samples 4] [--margin-px 16] [--cpu]

The mesh bakes from itself: every original material samples its textures through the ORIGINAL uv; the result lands
in the atlas through UV_Atlas (blender_atlas.py).
  BaseColor  4K sRGB    emit pass of OUT_base_color (Unreal's view-dependent Fresnel term excluded, see Sheen)
  Normal     4K linear  real tangent-space normal bake in the UV_Atlas tangent frame (islands may be rotated),
                        written DirectX style (green down) for Unreal, 16-bit PNG
  ORM        2K linear  AO / Roughness / Metallic baked separately (exact values Unreal feeds the material)
                        then packed R=AO G=Roughness B=Metallic
  Emissive   2K sRGB    only if any source material has emissive
  Sheen      1K linear  weight of Unreal's "BaseColor + Fresnel * k" term (0 where unused)
  Coverage   1K         white where the atlas has islands (margin 0) -> coverage %
"""
import argparse
import os
import sys

import bpy
import numpy as np

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402

ATLAS_UV = "UV_Atlas"


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--res", type=int, default=4096)
    p.add_argument("--orm-res", type=int, default=0, help="default: half of --res")
    p.add_argument("--aux-res", type=int, default=1024)
    p.add_argument("--samples", type=int, default=4)
    p.add_argument("--margin-px", type=int, default=16)
    p.add_argument("--cpu", action="store_true")
    return p.parse_args(pc.script_args())


def new_image(name, res, colorspace, fill, float_buffer=False):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, res, res, alpha=False, float_buffer=float_buffer)
    img.colorspace_settings.name = colorspace
    img.generated_color = fill
    return img


def prepare(mesh, img, socket_name, kind):
    """Wire every material for one bake: target image node active, OUT_<socket> -> emission (or BSDF for normal)."""
    for slot in mesh.material_slots:
        nt = slot.material.node_tree
        for n in [n for n in nt.nodes if n.label.startswith("BAKE_")]:
            nt.nodes.remove(n)
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.label = "BAKE_target"
        tex.image = img
        tex.location = (800, 400)
        uvn = nt.nodes.new("ShaderNodeUVMap")
        uvn.label = "BAKE_uv"
        uvn.uv_map = ATLAS_UV
        nt.links.new(uvn.outputs[0], tex.inputs[0])
        nt.nodes.active = tex
        for n in nt.nodes:
            n.select = n == tex
        if kind == "normal":
            bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
            nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
        else:
            em = nt.nodes.new("ShaderNodeEmission")
            em.label = "BAKE_emit"
            em.inputs["Strength"].default_value = 1.0
            if socket_name == "COVERAGE":
                em.inputs["Color"].default_value = (1, 1, 1, 1)
            else:
                nt.links.new(nt.nodes["OUT_" + socket_name].outputs[0], em.inputs["Color"])
            nt.links.new(em.outputs[0], out.inputs["Surface"])


def restore(mesh):
    for slot in mesh.material_slots:
        nt = slot.material.node_tree
        for n in [n for n in nt.nodes if n.label.startswith("BAKE_")]:
            nt.nodes.remove(n)
        out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        nt.links.new(bsdf.outputs[0], out.inputs["Surface"])


def bake(mesh, img, socket_name, margin, kind="emit"):
    prepare(mesh, img, socket_name, kind)
    scene = bpy.context.scene
    scene.render.bake.margin = margin
    scene.render.bake.margin_type = "ADJACENT_FACES" if margin else "EXTEND"
    scene.render.bake.use_clear = True
    with pc.Timer("bake %s %dpx" % (socket_name, img.size[0])) as t:
        if kind == "normal":
            bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", normal_r="POS_X", normal_g="NEG_Y",
                                normal_b="POS_Z", uv_layer=ATLAS_UV, margin=margin, use_clear=True)
        else:
            bpy.ops.object.bake(type="EMIT", uv_layer=ATLAS_UV, margin=margin, use_clear=True)
    return t.seconds


def pixels(img):
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def save_png(img, path):
    """Save a byte image as is (no view transform; the buffer already holds the image's colour space encoding)."""
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()


def preview(img, path, size=512):
    """Box-filtered small copy straight from the pixel buffer (values as stored, no colour conversion)."""
    a = pixels(img)[::-1, :, :3]
    f = a.shape[0] // size
    small = a[: size * f, : size * f].reshape(size, f, size, f, 3).mean(axis=(1, 3))
    pc.write_png(path, small, 8)


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    orm_res = args.orm_res or args.res // 2
    bpy.ops.wm.open_mainfile(filepath=paths.atlas_blend)
    mesh = bpy.data.objects[paths.mesh_name]
    for o in bpy.data.objects:
        o.select_set(o == mesh)
        o.hide_render = o != mesh and o.type == "MESH"
    bpy.context.view_layer.objects.active = mesh
    device = pc.setup_cycles(samples=args.samples, device="CPU" if args.cpu else "GPU")
    scene = bpy.context.scene
    scene.render.bake.target = "IMAGE_TEXTURES"
    warnings = []
    times = {}
    m = args.margin_px
    scale = lambda r: max(2, int(round(m * r / float(args.res))))  # same dilation in uv space

    # coverage first (margin 0)
    cov = new_image("BAKE_coverage", args.aux_res, "Non-Color", (0, 0, 0, 1))
    times["coverage"] = bake(mesh, cov, "COVERAGE", 0)
    cp = pixels(cov)[..., 0]
    coverage = float((cp > 0.5).mean())
    preview(cov, paths.tex("Coverage_preview"))

    bc = new_image("T_%s_BaseColor" % paths.short, args.res, "sRGB", (0.5, 0.5, 0.5, 1))
    times["base_color"] = bake(mesh, bc, "base_color", m)
    save_png(bc, paths.tex("BaseColor"))
    preview(bc, paths.tex("BaseColor_preview"))

    nrm = new_image("T_%s_Normal" % paths.short, args.res, "Non-Color", (0.5, 0.5, 1.0, 1), float_buffer=True)
    times["normal"] = bake(mesh, nrm, "normal", m, kind="normal")
    npx = pixels(nrm)
    if not np.isfinite(npx).all():
        warnings.append("normal bake has NaN / inf texels")
    pc.write_png(paths.tex("Normal"), npx[::-1, :, :3], 16)  # float buffer -> exact 16-bit PNG
    preview(nrm, paths.tex("Normal_preview"))

    chans = {}
    for key, sock in (("ao", "ao"), ("roughness", "roughness"), ("metallic", "metallic")):
        img = new_image("BAKE_%s" % key, orm_res, "Non-Color", (0, 0, 0, 1), float_buffer=True)
        times[key] = bake(mesh, img, sock, scale(orm_res))
        chans[key] = pixels(img)[..., 0].copy()
        bpy.data.images.remove(img)
    orm = new_image("T_%s_ORM" % paths.short, orm_res, "Non-Color", (1, 0.5, 0, 1))
    packed = np.stack([chans["ao"], chans["roughness"], chans["metallic"], np.ones_like(chans["ao"])], axis=-1)
    orm.pixels.foreach_set(np.clip(packed, 0, 1).astype(np.float32).ravel())
    save_png(orm, paths.tex("ORM"))
    preview(orm, paths.tex("ORM_preview"))

    has_emissive = any(
        any(l.from_node.type != "RGB" for l in s.material.node_tree.nodes["OUT_emissive"].inputs[0].links)
        for s in mesh.material_slots if "OUT_emissive" in s.material.node_tree.nodes)
    emissive_stats = None
    if has_emissive:
        em = new_image("T_%s_Emissive" % paths.short, orm_res, "sRGB", (0, 0, 0, 1))
        times["emissive"] = bake(mesh, em, "emissive", scale(orm_res))
        ep = pixels(em)
        emissive_stats = {"nonzero_fraction": float((ep[..., :3].max(-1) > 0.02).mean()), "res": orm_res}
        save_png(em, paths.tex("Emissive"))
        preview(em, paths.tex("Emissive_preview"))

    sh = new_image("BAKE_sheen", args.aux_res, "Non-Color", (0, 0, 0, 1), float_buffer=True)
    times["sheen"] = bake(mesh, sh, "sheen", scale(args.aux_res))
    sp = pixels(sh)[..., :3].mean(-1)
    sheen_img = new_image("T_%s_Sheen" % paths.short, args.aux_res, "Non-Color", (0, 0, 0, 1))
    sheen_img.pixels.foreach_set(np.repeat(np.clip(sp, 0, 1)[..., None], 4, -1).astype(np.float32).ravel())
    save_png(sheen_img, paths.tex("Sheen"))
    sheen_values = sorted({round(float(v), 3) for v in np.unique(np.round(sp[cp > 0.5], 3))})[:10]

    restore(mesh)
    bpy.ops.wm.save_as_mainfile(filepath=paths.bake_blend, compress=True)

    stats = {}
    for key, arr in chans.items():
        inside = arr[np.kron(cp > 0.5, np.ones((orm_res // args.aux_res,) * 2, dtype=bool))] \
            if orm_res % args.aux_res == 0 else arr.ravel()
        stats[key] = {"min": round(float(inside.min()), 3), "mean": round(float(inside.mean()), 3),
                      "max": round(float(inside.max()), 3)}
    pc.update_summary(paths, "bake", {
        "device": device, "samples": args.samples, "margin_px": m, "resolution": args.res, "orm_resolution": orm_res,
        "atlas_coverage_percent": round(coverage * 100, 2), "seconds": {k: round(v, 1) for k, v in times.items()},
        "orm_channel_stats": stats, "emissive": emissive_stats, "sheen_values": sheen_values,
        "bake_warnings": warnings,
        "outputs": {k: os.path.relpath(paths.tex(k), paths.root).replace("\\", "/") for k in
                    ["BaseColor", "Normal", "ORM", "Sheen"] + (["Emissive"] if has_emissive else [])}})
    print("[pipeline] BAKE_DONE coverage=%.2f%% device=%s" % (coverage * 100, device))


main()
