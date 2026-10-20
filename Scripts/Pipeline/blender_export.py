"""Character single-slot pipeline, stage 5: FBX for Unreal with ONE material slot (Blender background).

    blender --background --factory-startup --python Scripts/Pipeline/blender_export.py -- --asset-dir <...>
        [--slot M_Body]

- UV0 = UV_Atlas (every other uv map removed), one material slot `M_Body` (baked atlas textures);
- the Unreal skeleton root: the importer turned the FBX `root` bone into the armature OBJECT, so a real `root` bone
  is added back (identity, at the origin) and the object is named `Armature` - Unreal drops a top node named
  "Armature", so the skeleton is root -> pelvis / ik_foot_root / ik_hand_root exactly as before;
- no leaf bones, Y primary / X secondary bone axes (same as the import), centimetres (scene unit scale 0.01);
- re-imports the written FBX and checks: one slot, uv maps, bone names / hierarchy / rest positions vs the source.
"""
import argparse
import os
import sys

import bpy
from mathutils import Matrix, Vector

sys.dont_write_bytecode = True  # no __pycache__ next to the tracked scripts
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pipeline_common as pc  # noqa: E402

ATLAS_UV = "UV_Atlas"


def parse():
    p = argparse.ArgumentParser()
    p.add_argument("--asset-dir", required=True)
    p.add_argument("--slot", default="M_Body")
    return p.parse_args(pc.script_args())


def body_material(paths, name):
    """Preview material of the exported mesh: the baked atlas (the Unreal master material is built in stage 7)."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    build_atlas_material(mat, paths)
    return mat


def build_atlas_material(mat, paths, uv_name=ATLAS_UV, with_sheen=True):
    nt = mat.node_tree
    nt.nodes.clear()
    uvn = nt.nodes.new("ShaderNodeUVMap")
    uvn.uv_map = uv_name

    def img(kind, colorspace):
        path = paths.tex(kind)
        if not os.path.exists(path):
            return None
        n = nt.nodes.new("ShaderNodeTexImage")
        n.image = bpy.data.images.load(path, check_existing=True)
        n.image.colorspace_settings.name = colorspace
        n.label = kind
        nt.links.new(uvn.outputs[0], n.inputs[0])
        return n

    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(bsdf.outputs[0], out.inputs["Surface"])
    bc = img("BaseColor", "sRGB")
    orm = img("ORM", "Non-Color")
    nrm = img("Normal", "Non-Color")
    em = None  # no Emissive map (designer decision 2026-10-20)
    sheen = img("Sheen", "Non-Color") if with_sheen else None
    base = bc.outputs["Color"]
    if sheen is not None:  # BaseColor + Sheen * Fresnel(0.04 + 0.96 (1-NdotV)^5), as the Unreal materials did
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        dot = nt.nodes.new("ShaderNodeVectorMath")
        dot.operation = "DOT_PRODUCT"
        nt.links.new(geo.outputs["Normal"], dot.inputs[0])
        nt.links.new(geo.outputs["Incoming"], dot.inputs[1])
        prev = dot.outputs["Value"]
        for op, a, b in (("ABSOLUTE", None, None), ("SUBTRACT", 1.0, None), ("POWER", None, 5.0),
                         ("MULTIPLY_ADD", None, (0.96, 0.04))):
            m = nt.nodes.new("ShaderNodeMath")
            m.operation = op
            if op == "SUBTRACT":
                m.inputs[0].default_value = 1.0
                nt.links.new(prev, m.inputs[1])
            else:
                nt.links.new(prev, m.inputs[0])
                if op == "POWER":
                    m.inputs[1].default_value = b
                if op == "MULTIPLY_ADD":
                    m.inputs[1].default_value, m.inputs[2].default_value = b
            prev = m.outputs[0]
        k = nt.nodes.new("ShaderNodeMath")
        k.operation = "MULTIPLY"
        nt.links.new(prev, k.inputs[0])
        nt.links.new(sheen.outputs["Color"], k.inputs[1])
        add = nt.nodes.new("ShaderNodeVectorMath")
        add.operation = "ADD"
        nt.links.new(bc.outputs["Color"], add.inputs[0])
        nt.links.new(k.outputs[0], add.inputs[1])
        base = add.outputs[0]
    nt.links.new(base, bsdf.inputs["Base Color"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(orm.outputs["Color"], sep.inputs[0])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    # baked normal is DirectX (green down): flip back for Blender
    ns = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(nrm.outputs["Color"], ns.inputs[0])
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    nt.links.new(ns.outputs["Green"], inv.inputs[1])
    nc = nt.nodes.new("ShaderNodeCombineColor")
    nt.links.new(ns.outputs["Red"], nc.inputs[0])
    nt.links.new(inv.outputs[0], nc.inputs[1])
    nt.links.new(ns.outputs["Blue"], nc.inputs[2])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.uv_map = uv_name
    nt.links.new(nc.outputs[0], nm.inputs["Color"])
    nt.links.new(nm.outputs[0], bsdf.inputs["Normal"])
    if em is not None:
        nt.links.new(em.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 1.0
    return mat


def add_root_bone(arm):
    """Bring the `root` bone back as a real bone; the object becomes `Armature` (dropped by Unreal's importer)."""
    root_name = arm.name.split(".")[0]  # the imported object carries the FBX root bone's name
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    tops = [b for b in eb if b.parent is None]
    top_names = [b.name for b in tops]  # edit bones are invalid after leaving edit mode
    root = eb.new(root_name)
    root.head = Vector((0, 0, 0))
    root.tail = Vector((0, 10.0, 0))  # identity rest matrix (bone Y axis = +Y), 10 cm long
    root.roll = 0.0
    root.use_deform = False
    for b in tops:
        b.parent = root
        b.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT")
    arm.name = "Armature"
    arm.data.name = "Armature"
    return root_name, top_names


def bone_table(arm):
    mw = arm.matrix_world
    return {b.name: {"parent": b.parent.name if b.parent else None,
                     "head": list(mw @ b.head_local), "tail": list(mw @ b.tail_local)} for b in arm.data.bones}


def main():
    args = parse()
    paths = pc.Paths(args.asset_dir)
    bpy.ops.wm.open_mainfile(filepath=paths.bake_blend)
    mesh = bpy.data.objects[paths.mesh_name]
    arm = mesh.parent if mesh.parent and mesh.parent.type == "ARMATURE" else \
        next(o for o in bpy.data.objects if o.type == "ARMATURE")
    src_bones = bone_table(arm)
    me = mesh.data

    # one uv map
    for uvl in [u for u in me.uv_layers if u.name != ATLAS_UV]:
        me.uv_layers.remove(uvl)
    me.uv_layers.active = me.uv_layers[ATLAS_UV]
    me.uv_layers[ATLAS_UV].active_render = True
    # one material slot
    mat = body_material(paths, args.slot)
    me.materials.clear()
    me.materials.append(mat)
    for p in me.polygons:
        p.material_index = 0

    root_name, tops = add_root_bone(arm)
    for o in bpy.data.objects:
        o.select_set(o in (mesh, arm))
    bpy.context.view_layer.objects.active = arm
    export_kwargs = dict(
        filepath=paths.export_fbx, use_selection=True, object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", global_scale=1.0,
        axis_forward="-Z", axis_up="Y", use_space_transform=True, bake_space_transform=False,
        use_mesh_modifiers=False, mesh_smooth_type="FACE", use_tspace=True, use_custom_props=False,
        add_leaf_bones=False, primary_bone_axis="Y", secondary_bone_axis="X", use_armature_deform_only=False,
        armature_nodetype="NULL", bake_anim=False, path_mode="STRIP", embed_textures=False, use_triangles=True)
    bpy.ops.export_scene.fbx(**export_kwargs)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(paths.blend, paths.mesh_name + "_export.blend"), compress=True)

    # ---- verify by re-importing the written FBX
    pc.reset_scene()
    a2, meshes, others = pc.import_fbx(paths.export_fbx)
    checks = {}
    m2 = meshes[0] if len(meshes) == 1 else None
    checks["one_mesh"] = len(meshes) == 1
    checks["armature_object"] = a2.name if a2 else None
    if m2:
        checks["material_slots"] = [s.material.name if s.material else None for s in m2.material_slots]
        checks["uv_maps"] = [u.name for u in m2.data.uv_layers]
        checks["triangles"] = len(m2.data.polygons)
        checks["vertices"] = len(m2.data.vertices)
        checks["vertex_groups"] = len(m2.vertex_groups)
        checks["shape_keys"] = len(m2.data.shape_keys.key_blocks) if m2.data.shape_keys else 0
        checks["custom_normals"] = bool(getattr(m2.data, "has_custom_normals", False))
    new_bones = bone_table(a2) if a2 else {}
    # the source import had `root` as the object: compare everything else + root parenting
    missing = sorted(set(src_bones) - set(new_bones))
    extra = sorted(set(new_bones) - set(src_bones) - {root_name})
    parent_mismatch = sorted(b for b in src_bones if b in new_bones and
                             (src_bones[b]["parent"] or root_name) != new_bones[b]["parent"])
    max_dev = 0.0
    for b in src_bones:
        if b in new_bones:
            for k in ("head", "tail"):
                max_dev = max(max_dev, (Vector(src_bones[b][k]) - Vector(new_bones[b][k])).length)
    checks["bones_source"] = len(src_bones)
    checks["bones_export"] = len(new_bones)
    checks["root_bone"] = root_name in new_bones and new_bones[root_name]["parent"] is None
    checks["bones_missing"] = missing
    checks["bones_extra"] = extra
    checks["parent_mismatch"] = parent_mismatch
    checks["max_bone_deviation_cm"] = round(max_dev, 4)
    checks["leaf_bones"] = sorted(b for b in new_bones if b.endswith("_end"))
    ok = (checks["one_mesh"] and checks.get("material_slots") == [args.slot] and checks.get("uv_maps") == [ATLAS_UV]
          and not missing and not extra and not parent_mismatch and max_dev < 0.01 and checks["root_bone"]
          and not checks["leaf_bones"])
    checks["ok"] = ok
    settings = {k: (sorted(v) if isinstance(v, set) else v) for k, v in export_kwargs.items() if k != "filepath"}
    pc.save_json(os.path.join(paths.export, "export_manifest.json"), {
        "fbx": os.path.basename(paths.export_fbx), "material_slot": args.slot, "uv0": ATLAS_UV,
        "skeleton": paths.inventory.get("skeleton"), "root_bone_added": root_name, "top_bones_reparented": tops,
        "blender_fbx_settings": settings, "roundtrip_checks": checks,
        "textures": {k: os.path.basename(paths.tex(k)) for k in ("BaseColor", "Normal", "ORM", "Sheen")
                     if os.path.exists(paths.tex(k))},
        "unreal_import_notes": [
            "Import onto the SAME skeleton: %s" % paths.inventory.get("skeleton"),
            "Normals: Import Normals (custom split normals are in the FBX); tangents: MikkTSpace (same as the bake)",
            "Normal map is DirectX (green down) - no Flip Green; compression TC_Normalmap (BC5), sRGB off",
            "ORM: sRGB off, TC_Masks; R=AO G=Roughness B=Metallic - values are FINAL (no Power / sRGB decode)",
            "BaseColor: sRGB on, TC_Default (no alpha -> BC1/DXT1)",
            "Sheen: BaseColor + Fresnel * Sheen reproduces the old cloth Fresnel rim (0.07)",
            "No Emissive texture (designer decision 2026-10-20)",
        ]})
    pc.update_summary(paths, "export", {"fbx": os.path.relpath(paths.export_fbx, paths.root).replace("\\", "/"),
                                        "checks": checks})
    print("[pipeline] EXPORT_DONE ok=%s %s" % (ok, checks))


if __name__ == "__main__":
    main()
