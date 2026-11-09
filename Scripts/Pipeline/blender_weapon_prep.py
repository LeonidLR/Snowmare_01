"""Weapon pipeline, stage 1 (Blender, background): build the HIGH source parts in weapon space.

    blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_prep.py -- --asset-dir <ArtPipeline/Weapons/X>

Writes 03_Blender/<Asset>_high.blend: one object per source object (all centimetres, scene unit scale 0.01), custom
properties  part  (the optimized part it belongs to) and  src_mat  (the pack material name -> texture set), plus
01_Source/parts.json (mount transforms, pivots, source triangle counts).

Pistol 9mm Custom (Fab, DTry): the FBX is a display sheet - four assembled / lying pistols and every part laid out in a
row. The lying pistol (muzzle +X, up +Z) is the base; every attachment is moved from its row position to its mounted
position on the pistol, found by matching it to the same part in an assembled pistol of the sheet (rigid Kabsch fit on the
vertices, the meshes are identical vertex for vertex) and carrying that fit over to the lying pistol. Everything is then
shifted so the weapon origin is the grip pivot (every part shares it: mounted parts attach at identity).
Shotgun D-1: the two UE-exported skeletal meshes are used as they are (skin + armature kept for the export stage).

Docs: docs/pipeline/WEAPON_SINGLE_MATERIAL.md
"""
import argparse
import json
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import weapon_common as wc  # noqa: E402

PISTOL_FBX = r"C:\Users\Zephyrus15Duo\Documents\Codex\ASSETS\Weapons\Pistol9mmCustom\extracted\source\CustomPistol9mmPack.fbx"

# part -> pack materials, default mount (assembled pistol of the sheet: 1 = the one with the suppressor / flashlight 1 /
# laser / rail 1 pieces, 2 = the one with flashlight 2 / rail 2), pivot kind
PISTOL_PARTS = {
    "Pistol": dict(mats=["CustomPistol9mm"], mount=0),
    "Magazine": dict(mats=["Magazine9mm"], mount=1),
    "Bullet": dict(mats=["Bullet9x19"], mount=None),
    "Suppressor1": dict(mats=["Supressor1"], mount=1),
    "Suppressor2": dict(mats=["Supressor2"], mount=1),
    "Suppressor3": dict(mats=["Supressor3"], mount=None),
    "Laser": dict(mats=["LaserBlock"], mount=1),
    "Flashlight1": dict(mats=["Fleshlight1"], mount=1),
    "Flashlight2": dict(mats=["Fleshlight2"], mount=2),
    "RedDot": dict(mats=["AIM"], mount=2),
    "Picatinny1": dict(mats=["PicatinyBracing1"], mount=1),
    "Picatinny2": dict(mats=["PicatinyBracing2"], mount=2),
}
SKIP_MATS = {"SPOT"}  # 2-triangle reticle / lens quads of the red dot (emissive in the pack): dropped, see README


def kabsch(P, Q):
    pc_, qc = P.mean(0), Q.mean(0)
    H = (P - pc_).T @ (Q - qc)
    U, _S, Vt = np.linalg.svd(H)
    d = np.sign(np.linalg.det(Vt.T @ U.T))
    R = Vt.T @ np.diag([1, 1, d]) @ U.T
    t = qc - R @ pc_
    rms = float(np.sqrt((((P @ R.T + t) - Q) ** 2).sum(1).mean()))
    M = Matrix.Identity(4)
    for i in range(3):
        for j in range(3):
            M[i][j] = R[i, j]
        M[i][3] = t[i]
    return M, rms


def world_pts(o, S):
    me = o.data
    a = np.empty(len(me.vertices) * 3, np.float32)
    me.vertices.foreach_get("co", a)
    a = a.reshape(-1, 3).astype(np.float64)
    W = np.array(S @ o.matrix_world)
    return a @ W[:3, :3].T + W[:3, 3]


def tris(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)


def prep_pistol(ctx):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=PISTOL_FBX)  # default scene (metres): world points x100 = cm
    S = Matrix.Scale(100.0, 4)
    objs = [o for o in bpy.data.objects if o.type == "MESH"]
    mat_of = {o.name: o.data.materials[0].name for o in objs}
    cen = {o.name: world_pts(o, S).mean(0) for o in objs}

    lying = [o for o in objs if mat_of[o.name] == "CustomPistol9mm" and 45 < cen[o.name][1] < 50 and "Reload" not in o.name]
    frame_lying = max(lying, key=tris)
    PL = world_pts(frame_lying, S)
    frames = {}  # assembled pistols of the sheet
    for o in objs:
        if o is not frame_lying and mat_of[o.name] == "CustomPistol9mm" and len(o.data.vertices) == len(frame_lying.data.vertices) and "Reload" not in o.name:
            frames[o.name] = o
    log = {"pistol_lying_objects": len(lying), "frame_object": frame_lying.name, "assembled_frames": sorted(frames)}
    # pistol-1 = the assembled one at y<0, pistol-2 = the one at y>0 (comp index 1 / 2)
    asm = {}
    for name, fo in frames.items():
        M, rms = kabsch(world_pts(fo, S), PL)
        key = 1 if cen[name][1] < 0 else 2
        asm[key] = dict(frame=fo, M=M, rms=rms, centroid=cen[name])
        log["frame_fit_rms_%d" % key] = round(rms, 5)

    # pivot: grip centre (x), mid plane (y), middle of the grip height (z), in the lying pistol's frame
    zmin = PL[:, 2].min()
    grip = PL[PL[:, 2] < zmin + 3.5]
    pivot = np.array([grip[:, 0].mean(), (PL[:, 1].min() + PL[:, 1].max()) * 0.5, zmin + 4.2])
    log["pivot_in_sheet_cm"] = [round(float(v), 3) for v in pivot]
    Tpiv = Matrix.Translation(-Vector(pivot))

    def lineup_objs(mats):
        return [o for o in objs if mat_of[o.name] in mats and cen[o.name][1] > 57 and "Reload" not in o.name]

    def pts_of(members):
        return np.vstack([world_pts(o, S) for o in members])

    def apply_pts(T, P):
        A = np.array(T)
        return P @ A[:3, :3].T + A[:3, 3]

    members = {"Pistol": lying}
    T_part = {"Pistol": Tpiv @ Matrix.Identity(4)}  # sheet (cm world) -> weapon space
    info = {"Pistol": {"mount": "base (the lying pistol of the sheet)"}}
    for part, spec in PISTOL_PARTS.items():
        if part == "Pistol":
            continue
        lo = lineup_objs(spec["mats"])
        members[part] = lo
        big = max(lo, key=tris)
        cands = {}
        for key, a in asm.items():
            best = None
            for o in objs:
                if mat_of[o.name] != mat_of[big.name] or o is big or cen[o.name][1] > 57:
                    continue
                if len(o.data.vertices) != len(big.data.vertices) or len(o.data.polygons) != len(big.data.polygons):
                    continue
                K, rms = kabsch(world_pts(big, S), world_pts(o, S))
                if rms > 0.01:
                    continue
                d = float(np.linalg.norm(cen[o.name] - a["centroid"]))
                if d > 40 or (best and d >= best[0]):
                    continue
                best = (d, K, o.name, rms)
            if best:
                cands[key] = dict(d=best[0], T=Tpiv @ a["M"] @ best[1], src=best[2])
        want = spec["mount"]
        pinfo = {"sheet_candidates": {str(k): v["src"] for k, v in cands.items()}}
        if want in cands:
            T_part[part] = cands[want]["T"]
            pinfo["mount"] = "mounted pose of assembled pistol %d of the sheet" % want
        else:
            pinfo["mount"] = "none"
        info[part] = pinfo

    def align_like(ref_part, part):
        """Long axis of `part` onto the muzzle axis (+X), narrow (threaded) end rearwards, rear face on the reference's
        rear face, centred on the reference's axis."""
        Pr = apply_pts(T_part[ref_part], pts_of(members[ref_part]))
        P = pts_of(members[part])
        e = P.max(0) - P.min(0)
        axis = int(np.argmax(e))
        v = Vector([1.0 if i == axis else 0.0 for i in range(3)])
        best = None
        for flip in (1.0, -1.0):
            R = (v * flip).rotation_difference(Vector((1, 0, 0))).to_matrix().to_4x4()
            Q = apply_pts(R, P)
            n = len(Q)
            lo_s = Q[Q[:, 0] < Q[:, 0].min() + 0.1 * (Q[:, 0].max() - Q[:, 0].min())]
            hi_s = Q[Q[:, 0] > Q[:, 0].max() - 0.1 * (Q[:, 0].max() - Q[:, 0].min())]
            sp = lambda X: (X[:, 1:].max(0) - X[:, 1:].min(0)).sum()  # noqa: E731
            score = sp(hi_s) - sp(lo_s)  # rear (low x) should be the narrow one
            if best is None or score > best[0]:
                best = (score, R, Q)
        _s, R, Q = best
        off = Vector((Pr[:, 0].min() - Q[:, 0].min(), Pr[:, 1:].mean(0)[0] - Q[:, 1].mean() if False else 0.0, 0.0))
        c_ref = Pr.mean(0)
        c_q = Q.mean(0)
        off = Vector((Pr[:, 0].min() - Q[:, 0].min(), c_ref[1] - c_q[1], c_ref[2] - c_q[2]))
        return Matrix.Translation(off) @ R

    for part in ("Suppressor2", "Suppressor3"):
        T_part[part] = align_like("Suppressor1", part)
        info[part]["mount"] = "ESTIMATED: long axis on the muzzle axis, narrow end rearwards, rear face on suppressor 1's (no mounted twin in the sheet)"
    # bullet: points +X, origin at the case base centre
    P = pts_of(members["Bullet"])
    e = P.max(0) - P.min(0)
    axis = int(np.argmax(e))
    v = Vector([1.0 if i == axis else 0.0 for i in range(3)])
    other = [i for i in range(3) if i != axis]
    mid = P[:, axis].mean()
    sp = lambda X: (X[:, other].max(0) - X[:, other].min(0)).sum()  # noqa: E731
    base_low = sp(P[P[:, axis] < mid]) >= sp(P[P[:, axis] >= mid])
    base_val = P[:, axis].min() if base_low else P[:, axis].max()
    ctr = P.mean(0).copy()
    ctr[axis] = base_val
    R = (v * (1.0 if base_low else -1.0)).rotation_difference(Vector((1, 0, 0))).to_matrix().to_4x4()
    T_part["Bullet"] = R @ Matrix.Translation(-Vector(ctr))
    info["Bullet"]["mount"] = "standalone: cartridge points +X, origin at the case base centre"

    # build the high objects -----------------------------------------------------------------------------------
    new_objs = []
    stats = {}
    for part, objs_p in members.items():
        for o in objs_p:
            if mat_of[o.name] in SKIP_MATS:
                continue
            me = o.data.copy()
            M4 = T_part[part] @ (S @ o.matrix_world)
            me.transform(M4)
            if M4.determinant() < 0:
                me.flip_normals()
            no = bpy.data.objects.new("%s__%s" % (part, o.name), me)
            no["part"] = part
            no["src_mat"] = mat_of[o.name]
            no["src_mats"] = json.dumps([mat_of[o.name]])
            new_objs.append(no)
            st = stats.setdefault(part, [0, 0])
            st[0] += 1
            st[1] += sum(len(p.vertices) - 2 for p in me.polygons)
    parts_json = info
    # SPOT quads (counted for the report only)
    spot_tris = sum(tris(o) for o in objs if mat_of[o.name] in SKIP_MATS and cen[o.name][1] > 57)

    for o in objs:  # drop the imported sheet, keep the new meshes
        bpy.data.objects.remove(o)
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.01
    for no in new_objs:
        sc.collection.objects.link(no)
    bpy.ops.outliner.orphans_purge(do_recursive=True)
    return {"log": log, "parts": {k: {kk: vv for kk, vv in v.items() if not kk.startswith("_")} for k, v in parts_json.items()},
            "stats_objects_tris": stats, "spot_tris_dropped": spot_tris,
            "pivot_note": "weapon origin = grip centre (x), pistol mid plane (y), middle of the grip height (z)",
            "orientation": "muzzle +X, up +Z (Blender axes, centimetres)"}


def prep_shotgun(ctx):
    """Shotgun D-1: the UE-exported skeletal mesh (7 flat-colour slots, 10 rigid bone groups). Imported as it is (the
    armature, its names and its rest pose are untouched: the export stage must give UE the same skeleton)."""
    sc = wc.new_scene_cm()
    fbx = os.path.join(ctx.source, "Stock_Plastic_-_Matte__Dark_Gray__0.fbx")
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=fbx, ignore_leaf_bones=False, automatic_bone_orientation=False, use_custom_normals=True)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    arms = [o for o in new if o.type == "ARMATURE"]
    assert len(meshes) == 1 and len(arms) == 1, (meshes, arms)
    m = meshes[0]
    names = [slot.material.name for slot in m.material_slots]
    m["part"] = "Stock"
    m["orig_name"] = m.name
    m["src_mats"] = json.dumps(names)
    m["src_mat"] = names[0]
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    tri_by_mat = {}
    for p in m.data.polygons:
        n = names[p.material_index]
        tri_by_mat[n] = tri_by_mat.get(n, 0) + len(p.vertices) - 2
    return {"log": {"mesh": m.name, "armature": arms[0].name, "bones": len(arms[0].data.bones), "vertex_groups": [g.name for g in m.vertex_groups]},
            "stats_objects_tris": {"Stock": [1, tris(m)]}, "tris_by_material": tri_by_mat,
            "orientation": "as exported by Unreal (skeleton + mesh untouched)"}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--asset-dir", required=True)
    args = ap.parse_args(argv)
    ctx = wc.Ctx(args.asset_dir)
    t = wc.Timer()
    info = prep_pistol(ctx) if ctx.asset == "Pistol9mm" else prep_shotgun(ctx)
    os.makedirs(ctx.blend, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=ctx.high_blend, compress=True)
    wc.save_json(os.path.join(ctx.source, "parts.json"), info)
    print("[weapon] PREP_DONE", json.dumps(info["stats_objects_tris"]), "%.1fs" % t.elapsed())


if __name__ == "__main__":
    main()
