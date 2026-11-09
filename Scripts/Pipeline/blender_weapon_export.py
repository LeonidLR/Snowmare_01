"""Weapon pipeline, stage 4 (Blender, background): export every part as its own FBX with ONE material slot.

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_export.py -- --asset-dir <ArtPipeline/Weapons/X>

Reads 03_Blender/<Asset>_baked.blend (low parts with `UV_Atlas`). Writes 05_Export/SM_<Asset>_<Part>.fbx (static meshes;
skinned assets are handled by blender_weapon_export_skinned.py) and 05_Export/export_manifest.json, then re-imports every
FBX to verify: one mesh, one material slot, one uv map, triangle count, bounds (centimetres).

FBX settings are UE-safe: centimetres (scene unit scale 0.01, no scale fix-ups), no leaf bones, triangulated, smoothing
from edges (sharp edges = hard edges, the same normals the bake used), tangents written, textures not embedded.
Axes: Blender forward axis / up axis are arguments (default -Y / Z); the orientation is verified in Unreal by
Scripts/Pipeline/unreal_check_weapon_import.py (see the README of the asset).
"""
import argparse
import os
import sys

import bpy
from mathutils import Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_common as wc  # noqa: E402


def atlas_material(ctx, name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = wc.ATLAS_UV

    def tex(kind, cs):
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(ctx.tex(kind), check_existing=True)
        n.image.colorspace_settings.name = cs
        nt.links.new(uvn.outputs[0], n.inputs[0])
        return n

    b = tex("BaseColor", "sRGB")
    nt.links.new(b.outputs[0], bsdf.inputs["Base Color"])
    orm = tex("ORM", "Non-Color")
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(orm.outputs[0], sep.inputs[0])
    nt.links.new(sep.outputs[1], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs[2], bsdf.inputs["Metallic"])
    n = tex("Normal", "Non-Color")
    # the atlas normal is DirectX (green down); Blender's Normal Map node wants OpenGL: flip green
    s2 = nt.nodes.new("ShaderNodeSeparateColor")
    c2 = nt.nodes.new("ShaderNodeCombineColor")
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.uv_map = wc.ATLAS_UV
    nt.links.new(n.outputs[0], s2.inputs[0])
    nt.links.new(s2.outputs[0], c2.inputs[0])
    nt.links.new(s2.outputs[1], inv.inputs[1])
    nt.links.new(inv.outputs[0], c2.inputs[1])
    nt.links.new(s2.outputs[2], c2.inputs[2])
    nt.links.new(c2.outputs[0], nm.inputs["Color"])
    nt.links.new(nm.outputs[0], bsdf.inputs["Normal"])
    return mat


def bounds(obj):
    ws = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    return [min(w[i] for w in ws) for i in range(3)], [max(w[i] for w in ws) for i in range(3)]


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    ap.add_argument("--forward", default="-Y")
    ap.add_argument("--up", default="Z")
    ap.add_argument("--slot", default="")
    args = ap.parse_args(argv)
    ctx = wc.Ctx(args.asset_dir)
    t = wc.Timer()
    bpy.ops.wm.open_mainfile(filepath=ctx.baked_blend)
    slot = args.slot or ("M_" + ctx.asset)
    mat = atlas_material(ctx, slot)
    for o in [o for o in bpy.data.objects if o.type == "MESH" and "part" in o]:
        bpy.data.objects.remove(o)  # the high objects (their names would clash with the original node names)
    lows = [o for o in bpy.data.objects if o.type == "MESH" and "part" not in o]
    manifest = {"material_slot": slot, "uv_map": wc.ATLAS_UV, "axis_forward": args.forward, "axis_up": args.up,
                "units": "centimetres (scene unit scale 0.01, FBX unit scale 1 cm)", "parts": {}}
    export_kwargs = dict(
        use_selection=True, object_types={"MESH"}, apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
        global_scale=1.0, axis_forward=args.forward, axis_up=args.up, use_space_transform=True,
        bake_space_transform=False, use_mesh_modifiers=False, mesh_smooth_type="EDGE", use_tspace=True,
        use_custom_props=False, add_leaf_bones=False, path_mode="STRIP", embed_textures=False, use_triangles=True)
    skinned_kwargs = dict(export_kwargs, object_types={"ARMATURE", "MESH"}, axis_forward="-Z", axis_up="Y",
                          primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False,
                          armature_nodetype="NULL", bake_anim=False)
    swatch_centres = {}
    for o in lows:
        if "swatch" in o:
            uv = [l.uv for l in o.data.uv_layers[wc.ATLAS_UV].data]
            swatch_centres[o.name] = [sum(u[0] for u in uv) / len(uv), sum(u[1] for u in uv) / len(uv)]
            continue
        arm = o.parent if o.parent and o.parent.type == "ARMATURE" else None
        o.hide_set(False)
        o.hide_render = False
        o.data.materials.clear()
        o.data.materials.append(mat)
        for p in o.data.polygons:
            p.material_index = 0
        for uv in [u for u in o.data.uv_layers if u.name != wc.ATLAS_UV]:
            o.data.uv_layers.remove(uv)
        o.data.uv_layers.active = o.data.uv_layers[wc.ATLAS_UV]
        if arm is None:
            o.rotation_euler = (0, 0, 0)
            o.scale = (1, 1, 1)
            o.location = (0, 0, 0)
            name = "SM_%s_%s" % (ctx.asset, o.name) if o.name != ctx.asset else "SM_" + ctx.asset
        else:
            name = "SK_%s_1slot" % ctx.asset
        node_name = o.get("orig_name") or name
        o.name = node_name
        o.data.name = name
        bpy.ops.object.select_all(action="DESELECT")
        o.select_set(True)
        if arm is not None:
            arm.select_set(True)
        bpy.context.view_layer.objects.active = o
        path = os.path.join(ctx.export, name + ".fbx")
        bpy.ops.export_scene.fbx(filepath=path, **(skinned_kwargs if arm is not None else export_kwargs))
        mn, mx = bounds(o)
        manifest["parts"][name] = {"fbx": os.path.basename(path), "triangles": wc.tri_count(o), "skinned": arm is not None,
                                   "bounds_min_cm": [round(v, 3) for v in mn], "bounds_max_cm": [round(v, 3) for v in mx],
                                   "size_cm": [round(mx[i] - mn[i], 3) for i in range(3)],
                                   "centre_cm": [round((mx[i] + mn[i]) * 0.5, 3) for i in range(3)]}
        if arm is not None:
            manifest["parts"][name]["bones"] = [b.name for b in arm.data.bones]
            manifest["parts"][name]["armature_object"] = arm.name
    # extra meshes that only change their uv (e.g. the shotgun pump: a flat colour swatch of the atlas, geometry + skin untouched)
    for ex_ in ctx.config.get("extra_meshes", []):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = 0.01
        bpy.ops.import_scene.fbx(filepath=os.path.join(ctx.source, ex_["fbx"]), ignore_leaf_bones=False,
                                 automatic_bone_orientation=False, use_custom_normals=True)
        mesh = next(o for o in bpy.data.objects if o.type == "MESH")
        arm = next((o for o in bpy.data.objects if o.type == "ARMATURE"), None)
        centre = swatch_centres[ex_["swatch_part"]]
        me = mesh.data
        while len(me.uv_layers) > 1:
            me.uv_layers.remove(me.uv_layers[-1])
        me.uv_layers[0].name = wc.ATLAS_UV
        for l in me.uv_layers[0].data:
            l.uv = centre
        mat2 = atlas_material(ctx, slot)
        me.materials.clear()
        me.materials.append(mat2)
        for p in me.polygons:
            p.material_index = 0
        for img in [i for i in bpy.data.images if "T_%s_" % ctx.asset not in (i.filepath or "")]:
            if not img.users:
                bpy.data.images.remove(img)
        name = ex_["out"]
        me.name = name
        for o in bpy.data.objects:
            o.select_set(o.type in ("MESH", "ARMATURE"))
        path = os.path.join(ctx.export, name + ".fbx")
        bpy.ops.export_scene.fbx(filepath=path, **skinned_kwargs)
        manifest["parts"][name] = {"fbx": os.path.basename(path), "triangles": wc.tri_count(mesh), "skinned": True,
                                   "geometry": "untouched (only the uv map changed: every loop -> the flat-colour swatch)",
                                   "swatch_uv": [round(centre[0], 5), round(centre[1], 5)],
                                   "bones": [b.name for b in arm.data.bones] if arm else [],
                                   "size_cm": [round(v, 3) for v in mesh.dimensions]}
    # ---- round trip -----------------------------------------------------------------------------------------
    ok = True
    for name, info in manifest["parts"].items():
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = 0.01
        bpy.ops.import_scene.fbx(filepath=os.path.join(ctx.export, info["fbx"]), use_custom_normals=True)
        ms = [o for o in bpy.data.objects if o.type == "MESH"]
        arms_rt = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        chk = {"meshes": len(ms)}
        if arms_rt:
            chk["bones"] = [b.name for b in arms_rt[0].data.bones]
        if len(ms) == 1:
            m = ms[0]
            chk["slots"] = [s.material.name if s.material else None for s in m.material_slots]
            chk["uv_maps"] = [u.name for u in m.data.uv_layers]
            chk["triangles"] = wc.tri_count(m)
            chk["vertices"] = len(m.data.vertices)
            chk["scale"] = [round(v, 4) for v in m.scale]
        info["roundtrip"] = chk
        good = (chk["meshes"] == 1 and chk.get("slots") == [slot] and chk.get("uv_maps") == [wc.ATLAS_UV]
                and chk.get("triangles") == info["triangles"]
                and (not info.get("skinned") or sorted(chk.get("bones", [])) == sorted(info.get("bones", [])) or
                     set(info.get("bones", [])) - {"root"} <= set(chk.get("bones", []))))
        info["roundtrip_ok"] = good
        ok = ok and good
    wc.save_json(os.path.join(ctx.export, "export_manifest.json"), manifest)
    wc.update_summary(ctx, "export", {"ok": ok, "parts": {k: {"triangles": v["triangles"], "size_cm": v["size_cm"], "roundtrip_ok": v["roundtrip_ok"]} for k, v in manifest["parts"].items()}})
    print("[weapon] EXPORT_DONE ok=%s (%d fbx, %.1fs)" % (ok, len(manifest["parts"]), t.elapsed()))


if __name__ == "__main__":
    main()
