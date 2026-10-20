"""Character single-slot pipeline, stage 4: bake the atlas (Blender Cycles, GPU when available).

    blender --background --factory-startup --python Scripts/Pipeline/blender_bake.py -- --asset-dir <...>
        [--res 4096] [--orm-res 2048] [--aux-res 1024] [--samples 4] [--margin-px 16]
        [--cage-extrusion 1.5] [--max-ray 4.0] [--cpu]

Two modes, picked from 03_Blender/<mesh>_atlas.blend:
- PROJECT (a `<mesh>_high` object exists, i.e. blender_reduce.py ran): the cleaned high mesh is projected onto the
  reduced mesh (Cycles selected-to-active, cage extrusion / max ray distance in cm). Each material is baked on its
  own (only that material's high part is a ray target) so a strap never paints the cloth under it; misses are
  measured against the low mesh's own uv coverage; the 16 px dilation is done afterwards (numpy) because per-pass
  bake margins would overwrite the neighbouring materials.
- SELF (no high mesh): the mesh bakes from itself through its original uv (first pilot run).

Outputs (04_Textures_Baked):
  BaseColor  4K sRGB    emit pass of OUT_base_color (Unreal's view-dependent Fresnel term excluded, see Sheen)
  Normal     4K linear  tangent-space normal in the UV_Atlas frame, DirectX (green down), 16-bit PNG
  ORM        2K linear  AO / Roughness / Metallic baked separately (final values), packed R/G/B
  Sheen      1K linear  weight of Unreal's "BaseColor + Fresnel * k" term (0 where unused)
  Coverage   1K         preview of the atlas islands -> coverage %
No Emissive map (designer decision 2026-10-20: the flashlight / radio lights are not worth a texture).
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
    p.add_argument("--cage-extrusion", type=float, default=0.6, help="cm, project mode")
    p.add_argument("--max-ray", type=float, default=3.0, help="cm, project mode (0 = unlimited)")
    p.add_argument("--cpu", action="store_true")
    p.add_argument("--emissive-threshold", type=float, default=0.1,
                   help="write an Emissive map only when more than this %% of the atlas texels glow (designer 2026-10-20: "
                        "no Emissive unless something really glows)")
    return p.parse_args(pc.script_args())


def new_image(name, res, colorspace, fill, float_buffer=False, alpha=False):
    if name in bpy.data.images:
        bpy.data.images.remove(bpy.data.images[name])
    img = bpy.data.images.new(name, res, res, alpha=alpha, float_buffer=float_buffer)
    img.colorspace_settings.name = colorspace
    img.generated_color = fill
    return img


def pixels(img):
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def set_pixels(img, arr):
    img.pixels.foreach_set(np.ascontiguousarray(arr, dtype=np.float32).ravel())


def save_png(img, path):
    """Save a byte image as is (no view transform; the buffer already holds the image's colour space encoding)."""
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()


def preview(arr, path, size=512):
    """Box-filtered small copy of a pixel array (bottom row first, as Blender stores it)."""
    a = arr[::-1, :, :3]
    f = a.shape[0] // size
    small = a[: size * f, : size * f].reshape(size, f, size, f, 3).mean(axis=(1, 3))
    pc.write_png(path, small, 8)


def dilate(rgb, mask, iterations):
    """Grow the baked texels into the empty space (4-neighbour average), like a bake margin."""
    rgb = rgb.copy()
    m = mask.copy()
    for _ in range(iterations):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(m.shape, dtype=np.float32)
        for axis, shift in ((0, 1), (0, -1), (1, 1), (1, -1)):
            ms = np.roll(m, shift, axis=axis)
            acc += np.roll(rgb, shift, axis=axis) * ms[..., None]
            cnt += ms
        grow = (~m) & (cnt > 0)
        if not grow.any():
            break
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        m |= grow
    return rgb


# ------------------------------------------------------------------------------------------- material wiring

def wire_emission(mat, socket_name):
    """Material output <- emission(OUT_<socket>) (or white for COVERAGE / HIT); BSDF for the normal pass."""
    nt = mat.node_tree
    for n in [n for n in nt.nodes if n.label.startswith("BAKE_emit")]:
        nt.nodes.remove(n)
    out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    if socket_name == "normal":
        bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
        nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
        return
    em = nt.nodes.new("ShaderNodeEmission")
    em.label = "BAKE_emit"
    em.inputs["Strength"].default_value = 1.0
    if socket_name in ("COVERAGE", "HIT"):
        em.inputs["Color"].default_value = (1, 1, 1, 1)
    else:
        nt.links.new(nt.nodes["OUT_" + socket_name].outputs[0], em.inputs["Color"])
    nt.links.new(em.outputs[0], out.inputs["Surface"])


def target_material(name, img):
    """Low-mesh material whose only job is to name the bake target image."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = img
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = ATLAS_UV
    nt.links.new(uvn.outputs[0], tex.inputs[0])
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    nt.nodes.active = tex
    for n in nt.nodes:
        n.select = n == tex
    return mat


def add_target_node(mat, img):
    nt = mat.node_tree
    remove_target_nodes(mat)
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.label = "BAKE_target"
    tex.image = img
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.label = "BAKE_target"
    uvn.uv_map = ATLAS_UV
    nt.links.new(uvn.outputs[0], tex.inputs[0])
    nt.nodes.active = tex
    for n in nt.nodes:
        n.select = n == tex


def remove_target_nodes(mat):
    nt = mat.node_tree
    for n in [n for n in nt.nodes if n.label == "BAKE_target"]:
        nt.nodes.remove(n)


def self_target(mesh, img, socket_name):
    """SELF mode: every material of the mesh gets the target image node, emission of its own OUT_* socket."""
    for slot in mesh.material_slots:
        nt = slot.material.node_tree
        for n in [n for n in nt.nodes if n.label == "BAKE_target"]:
            nt.nodes.remove(n)
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.label = "BAKE_target"
        tex.image = img
        uvn = nt.nodes.new("ShaderNodeUVMap")
        uvn.label = "BAKE_target"
        uvn.uv_map = ATLAS_UV
        nt.links.new(uvn.outputs[0], tex.inputs[0])
        nt.nodes.active = tex
        for n in nt.nodes:
            n.select = n == tex
        wire_emission(slot.material, socket_name)


def run_bake(kind, margin, selected_to_active=False, args=None):
    kw = dict(uv_layer=ATLAS_UV, margin=margin, use_clear=not selected_to_active, target="IMAGE_TEXTURES")
    if selected_to_active:
        kw.update(use_selected_to_active=True, cage_extrusion=args.cage_extrusion, max_ray_distance=args.max_ray)
    if kind == "normal":
        bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT", normal_r="POS_X", normal_g="NEG_Y",
                            normal_b="POS_Z", **kw)
    else:
        bpy.ops.object.bake(type="EMIT", **kw)


# ------------------------------------------------------------------------------------------- project mode

class Projector:
    """High -> low bake, one material at a time."""

    def __init__(self, low, high, args, self_slots=()):
        self.low, self.args = low, args
        # materials the reduce stage left untouched bake from themselves (exact; a projection onto identical
        # geometry only adds ray errors at thin double-sided edges such as the hood rim)
        self.self_slots = set(self_slots)
        self.low_mats = list(low.data.materials)
        self.parts = []  # (slot index, high part object)
        import bmesh
        for i, mat in enumerate(high.data.materials):
            part = high.copy()
            part.data = high.data.copy()
            part.name = "BAKE_high_%02d" % i
            bpy.context.scene.collection.objects.link(part)
            bm = bmesh.new()
            bm.from_mesh(part.data)
            bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.material_index != i], context="FACES")
            bm.to_mesh(part.data)
            bm.free()
            part.hide_render = False
            part.hide_set(False)
            if len(part.data.polygons):
                self.parts.append((i, part))
        high.hide_render = True
        self.dummy = new_image("BAKE_dummy", 8, "Non-Color", (0, 0, 0, 0))
        self.dummy_mat = target_material("BAKE_dummy_mat", self.dummy)

    def bake(self, img, socket_name, kind="emit"):
        tmat = target_material("BAKE_target_mat", img)
        for _, part in self.parts:
            for slot in part.material_slots:
                wire_emission(slot.material, socket_name)
        for i, part in self.parts:
            own = i in self.self_slots
            for s in range(len(self.low.data.materials)):
                if s == i:
                    self.low.data.materials[s] = self.low_mats[s] if own else tmat
                else:
                    self.low.data.materials[s] = self.dummy_mat
            if own:
                add_target_node(self.low_mats[i], img)
                wire_emission(self.low_mats[i], socket_name)
            for o in bpy.context.view_layer.objects:
                o.select_set(o is self.low or (o is part and not own))
            bpy.context.view_layer.objects.active = self.low
            if own:
                bpy.ops.object.bake(type="NORMAL" if kind == "normal" else "EMIT", normal_space="TANGENT",
                                    normal_r="POS_X", normal_g="NEG_Y", normal_b="POS_Z", uv_layer=ATLAS_UV,
                                    margin=0, use_clear=False, target="IMAGE_TEXTURES")
                remove_target_nodes(self.low_mats[i])
            else:
                run_bake(kind, 0, selected_to_active=True, args=self.args)
        for s, m in enumerate(self.low_mats):
            self.low.data.materials[s] = m

    def cleanup(self):
        for _, part in self.parts:
            bpy.data.meshes.remove(part.data)


def look_materials(slot_names, var, inv):
    """Material asset paths the mesh shows in this look (variant or the mesh's own)."""
    merged = inv.get("blender_material_slots", {})
    out = []
    for name in slot_names:
        keys = [name] + merged.get(name, [])
        if var:
            path = next((var["slots"].get(k) for k in keys if var["slots"].get(k)), None)
        else:
            path = next((s["material"] for s in inv["slots"] if s["slot_name"] in keys), None) or next(
                (s["material"] for s in inv["slots"] if s["material"] and s["material"].split(".")[-1] == name), None)
        if path:
            out.append(path)
    return out


def apply_variant(mesh, high, slot_names, var, inv, paths, warnings):
    """Look variant (texture set): every slot gets the variant's material, rebuilt from the inventory, on low and high."""
    import material_graph as mg
    uv_name = mesh.data.uv_layers[0].name
    merged = inv.get("blender_material_slots", {})
    for i, slot in enumerate(slot_names):
        mpath = var["slots"].get(slot) or next((var["slots"].get(s) for s in merged.get(slot, []) if var["slots"].get(s)), None)
        minfo = inv["materials"].get(mpath) if mpath else None
        if not minfo:
            warnings.append("variant %s: no material for slot %s" % (var["name"], slot))
            continue
        mat = mg.build_material("%s__%s" % (slot, var["name"]), minfo, inv, paths.tex_source, uv_name, warnings)
        mesh.data.materials[i] = mat
        if high:
            high.data.materials[i] = mat


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    orm_res = args.orm_res or args.res // 2
    bpy.ops.wm.open_mainfile(filepath=paths.atlas_blend)
    mesh = bpy.data.objects[paths.mesh_name]
    high = bpy.data.objects.get(paths.high_name)
    mode = "project" if high else "self"
    for o in bpy.data.objects:
        o.select_set(o == mesh)
        if o.type == "MESH" and o not in (mesh, high):
            o.hide_render = True
    bpy.context.view_layer.objects.active = mesh
    device = pc.setup_cycles(samples=args.samples, device="CPU" if args.cpu else "GPU")
    bpy.context.scene.render.bake.target = "IMAGE_TEXTURES"
    warnings, times = [], {}
    m = args.margin_px
    scale = lambda r: max(2, int(round(m * r / float(args.res))))  # same dilation in uv space
    # stale outputs of earlier runs that this version does not produce
    if os.path.exists(paths.tex("Emissive_preview")):
        os.remove(paths.tex("Emissive_preview"))

    # low-mesh uv coverage (always a self bake, margin 0)
    if high:
        high.hide_render = True
    cov = new_image("BAKE_coverage", args.aux_res, "Non-Color", (0, 0, 0, 1))
    self_target(mesh, cov, "COVERAGE")
    with pc.Timer("bake coverage") as t:
        run_bake("emit", 0)
    times["coverage"] = t.seconds
    covp = pixels(cov)
    covered = covp[..., 0] > 0.5
    coverage = float(covered.mean())
    preview(covp, paths.tex("Coverage_preview"))
    for slot in mesh.material_slots:  # drop the self-bake target nodes again
        nt = slot.material.node_tree
        for n in [n for n in nt.nodes if n.label in ("BAKE_target", "BAKE_emit")]:
            nt.nodes.remove(n)

    import material_graph as mg
    mg.rebuild_mesh_materials([o for o in (mesh, high) if o], paths.inventory, paths.tex_source, warnings)
    slot_names = [sl.material.name for sl in mesh.material_slots]  # = the Unreal slot names (blender_clean.py)
    inv = paths.inventory
    variants = inv.get("variants") or [None]
    red = pc.load_json(paths.report_path).get("reduce", {}).get("per_material", {})
    self_slots = [i for i, n in enumerate(slot_names) if red.get(n, {}).get("ratio", 0) >= 0.999] if high else []
    per_variant = {}
    miss_pct = None
    normal_clamped_pct = None
    chans = {}
    sheen_values = []
    for vi, var in enumerate(variants):
        vname = var["name"] if var else None
        if var:
            apply_variant(mesh, high, slot_names, var, inv, paths, warnings)
        proj = Projector(mesh, high, args, self_slots) if mode == "project" else None
        tex = lambda kind: paths.tex(kind, variant=vname)

        def bake_channel(label, res, socket_name, colorspace, kind="emit", dil=None):
            """Returns the dilated float array (bottom row first)."""
            img = new_image("BAKE_" + label, res, colorspace, (0, 0, 0, 0), float_buffer=True, alpha=True)
            with pc.Timer("bake %s %dpx (%s)" % (label, res, mode)) as tm:
                if proj:
                    proj.bake(img, socket_name, kind)
                else:
                    self_target(mesh, img, socket_name)
                    run_bake(kind, dil if dil is not None else scale(res))
            times[label] = tm.seconds
            arr = pixels(img)
            bpy.data.images.remove(img)
            if proj:
                filled = arr[..., 3] > 0.5
                arr[..., :3] = dilate(arr[..., :3], filled, dil if dil is not None else scale(res))
            if not np.isfinite(arr).all():
                warnings.append("%s bake has NaN / inf texels" % label)
                arr = np.nan_to_num(arr)
            return arr

        # ray misses: high hit mask vs the low mesh's own coverage
        if proj and vi == 0:
            hit = bake_channel("hit", args.aux_res, "HIT", "Non-Color", dil=0)
            hit_mask = hit[..., 3] > 0.5
            misses = covered & ~hit_mask
            miss_pct = round(100.0 * misses.sum() / max(1, covered.sum()), 3)
            if miss_pct > 0.5:
                warnings.append("%.2f %% of the atlas texels found no high surface (raise --max-ray / --cage-extrusion)"
                                % miss_pct)
            pc.write_png(os.path.join(paths.review, "bake_misses.png"),
                         np.stack([misses, covered & hit_mask, np.zeros_like(misses)], -1)[::-1].astype(np.float32), 8)

        mats = [inv["materials"].get(m_, {}) for m_ in look_materials(slot_names, var, inv)]
        masked = any("MASKED" in str(m_.get("blend_mode", "")) for m_ in mats)
        two_sided = any(m_.get("two_sided") for m_ in mats)
        bc = bake_channel("base_color", args.res, "base_color", "Non-Color")
        # the emit values are linear: encode sRGB for the 8-bit colour texture
        lin = np.clip(bc[..., :3], 0, 1)
        srgb = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055)
        if masked:
            # Masked look (cut-outs): the opacity mask goes into the BaseColor alpha (Unreal: BC3, clip 0.3333).
            op = bake_channel("opacity", args.res, "opacity", "Non-Color")
            srgb = np.concatenate([srgb, np.clip(op[..., :1], 0, 1)], axis=-1)
        pc.write_png(tex("BaseColor"), srgb[::-1], 8)
        srgb = srgb[..., :3]
        preview(np.concatenate([srgb, np.ones_like(srgb[..., :1])], -1), tex("BaseColor_preview"))

        nrm = bake_channel("normal", args.res, "normal", "Non-Color", kind="normal")
        # A ray that reaches the back of a thin high part (buckle, strap edge) returns a normal pointing INTO the low
        # surface (tangent z < 0): renders black. Clamp to the front hemisphere and renormalise.
        n = nrm[..., :3] * 2.0 - 1.0
        flipped = n[..., 2] < 0.15
        if proj:
            n[..., 2] = np.maximum(n[..., 2], 0.15)
            n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
            nrm[..., :3] = n * 0.5 + 0.5
        normal_clamped_pct = round(100.0 * float((flipped & (np.kron(covered, np.ones((args.res // args.aux_res,) * 2,
                                                                                       dtype=bool)))).sum())
                                   / max(1, covered.sum() * (args.res // args.aux_res) ** 2), 3)
        pc.write_png(tex("Normal"), np.clip(nrm[::-1, :, :3], 0, 1), 16)
        preview(nrm, tex("Normal_preview"))

        chans = {}
        for key, sock in (("ao", "ao"), ("roughness", "roughness"), ("metallic", "metallic")):
            chans[key] = bake_channel(key, orm_res, sock, "Non-Color")[..., 0].copy()
        packed = np.clip(np.stack([chans["ao"], chans["roughness"], chans["metallic"]], axis=-1), 0, 1)
        pc.write_png(tex("ORM"), packed[::-1], 8)
        preview(np.concatenate([packed, np.ones_like(packed[..., :1])], -1), tex("ORM_preview"))

        # Emissive: measured always (aux res); the map is written only if a real part of the body glows.
        em = bake_channel("emissive_probe", args.aux_res, "emissive", "Non-Color")
        glow = (em[..., :3].max(-1) > 0.05) & covered
        glow_pct = round(100.0 * float(glow.sum()) / max(1, covered.sum()), 3)
        emissive_written = False
        emissive_scale = 1.0
        if glow_pct >= args.emissive_threshold:
            emf = bake_channel("emissive", orm_res, "emissive", "Non-Color")
            # HDR emission (strength > 1): stored normalised, the MI's EmissiveStrength carries the scale
            emissive_scale = max(1.0, float(emf[..., :3].max()))
            linf = np.clip(emf[..., :3] / emissive_scale, 0, 1)
            pc.write_png(tex("Emissive"), np.where(linf <= 0.0031308, linf * 12.92,
                                                  1.055 * np.power(linf, 1 / 2.4) - 0.055)[::-1], 8)
            emissive_written = True
        elif os.path.exists(tex("Emissive")):
            os.remove(tex("Emissive"))

        sh = bake_channel("sheen", args.aux_res, "sheen", "Non-Color")
        sp = np.clip(sh[..., :3].mean(-1), 0, 1)
        pc.write_png(tex("Sheen"), sp[::-1], 8)
        sheen_values = sorted({round(float(v), 3) for v in np.unique(np.round(sp[covered], 3))})[:10]

        per_variant[vname or "default"] = {
            "outputs": {k: os.path.basename(tex(k)) for k in ("BaseColor", "Normal", "ORM", "Sheen", "Emissive")
                        if os.path.exists(tex(k))},
            "emissive_glow_percent": glow_pct, "emissive_written": emissive_written,
            "emissive_strength": round(emissive_scale, 3), "masked": masked, "two_sided": two_sided}
        if proj:
            proj.cleanup()
            for sl in mesh.material_slots:
                wire_emission(sl.material, "normal")  # back to the BSDF

    bpy.ops.wm.save_as_mainfile(filepath=paths.bake_blend, compress=True)

    stats = {}
    f = orm_res // args.aux_res if orm_res % args.aux_res == 0 else 0
    for key, arr in chans.items():
        inside = arr[np.kron(covered, np.ones((f, f), dtype=bool))] if f else arr.ravel()
        stats[key] = {"min": round(float(inside.min()), 3), "mean": round(float(inside.mean()), 3),
                      "max": round(float(inside.max()), 3)}
    pc.update_summary(paths, "bake", {
        "mode": mode, "device": device, "samples": args.samples, "margin_px": m, "resolution": args.res,
        "orm_resolution": orm_res, "cage_extrusion_cm": args.cage_extrusion if high else None,
        "max_ray_cm": args.max_ray if high else None, "ray_miss_percent": miss_pct, "variants": per_variant,
        "self_baked_slots": [mesh.material_slots[i].material.name for i in self_slots],
        "normal_back_facing_clamped_percent": normal_clamped_pct,
        "atlas_coverage_percent": round(coverage * 100, 2), "seconds": {k: round(v, 1) for k, v in times.items()},
        "orm_channel_stats": stats, "sheen_values": sheen_values, "bake_warnings": warnings,
        "outputs": {k: os.path.relpath(paths.tex(k, variant=(variants[0] or {}).get("name")), paths.root).replace("\\", "/")
                    for k in ("BaseColor", "Normal", "ORM", "Sheen")}})
    print("[pipeline] BAKE_DONE mode=%s coverage=%.2f%% misses=%s%% device=%s" % (
        mode, coverage * 100, miss_pct, device))


main()
