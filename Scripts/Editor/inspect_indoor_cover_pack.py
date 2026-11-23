"""Read-only inspection of the designer's Riffle Cover Animation pack (/Game/Riffle_Cover_Animation, untracked).

Per clip: skeleton, length, frames, root-motion flags, additive type, notifies, root travel / yaw, pelvis travel / height,
the facing of the pelvis (UE5 Manny faces +Y in component space) and the head / right-hand offsets vs the clip's first frame
(peek side and over-the-top rise of the aim sets). Writes Saved/Logs/inspect_indoor_cover_pack.txt. Nothing is saved.
"""
import math
import os
import unreal

LOG = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "inspect_indoor_cover_pack.txt")
ROOT = "/Game/Riffle_Cover_Animation"
out = []
reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path(ROOT, recursive=True)
WORLD = unreal.AnimPoseSpaces.WORLD


def bone(seq, name, t):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, unreal.AnimPoseEvaluationOptions())
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD)


def fmt(v):
    return "(%.0f, %.0f, %.0f)" % (v.x, v.y, v.z)


def yaw(q, axis):
    v = q.rotate_vector(axis)
    return math.degrees(math.atan2(v.y, v.x))


classes = {}
for a in assets:
    cls = str(a.asset_class_path.asset_name)
    classes.setdefault(cls, []).append(str(a.package_name))
for cls, names in sorted(classes.items()):
    out.append(f"## {cls}: {len(names)}")
    if cls != "AnimSequence":
        for n in names:
            out.append("   " + n)

seqs = sorted(classes.get("AnimSequence", []))
for path in seqs:
    seq = unreal.EditorAssetLibrary.load_asset(path)
    name = path.split("/")[-1]
    if "_Dive" in name or "_Dodge" in name:
        out.append(f"== {name}: skipped (designer)")
        continue
    length = seq.get_play_length()
    skel = seq.get_editor_property("skeleton")
    try:
        frames = seq.get_editor_property("number_of_sampled_frames")
    except Exception:
        frames = -1
    rm = seq.get_editor_property("enable_root_motion")
    lock = seq.get_editor_property("root_motion_root_lock")
    add = seq.get_editor_property("additive_anim_type")
    ref = seq.get_editor_property("ref_pose_type")
    try:
        refseq = seq.get_editor_property("ref_pose_seq")
        refname = refseq.get_name() if refseq else "-"
    except Exception:
        refname = "?"
    loop = seq.get_editor_property("loop") if hasattr(seq, "loop") else "?"
    notifies = []
    try:
        for ev in seq.get_editor_property("notifies"):
            nm = ev.get_editor_property("notify_name")
            notifies.append(str(nm))
    except Exception:
        pass
    out.append(f"== {name}: {length:.2f} s, frames {frames}, skel {skel.get_path_name() if skel else None}, rm {rm} lock {lock}, additive {add} ref {ref}/{refname}, notifies {notifies}")
    if str(add) != "AdditiveAnimationType.AAT_NONE" and "NONE" not in str(add).upper():
        continue
    try:
        r0, r1 = bone(seq, "root", 0.0), bone(seq, "root", length)
        p0, p1 = bone(seq, "pelvis", 0.0), bone(seq, "pelvis", length)
        h0, h1 = bone(seq, "head", 0.0), bone(seq, "head", length)
        hr0, hr1 = bone(seq, "hand_r", 0.0), bone(seq, "hand_r", length)
        mid = length * 0.5
        hm = bone(seq, "head", mid)
        dr = r1.translation - r0.translation
        dp = p1.translation - p0.translation
        out.append("   root %s -> %s (travel %.0f cm, yaw %.0f -> %.0f)" % (fmt(r0.translation), fmt(r1.translation),
                   math.hypot(dr.x, dr.y), r0.rotation.rotator().yaw, r1.rotation.rotator().yaw))
        out.append("   pelvis %s -> %s (travel %.0f cm) facing(+Y axis yaw) %.0f -> %.0f | facing(+X) %.0f -> %.0f" % (
            fmt(p0.translation), fmt(p1.translation), math.hypot(dp.x, dp.y),
            yaw(p0.rotation, unreal.Vector(0, 1, 0)), yaw(p1.rotation, unreal.Vector(0, 1, 0)),
            yaw(p0.rotation, unreal.Vector(1, 0, 0)), yaw(p1.rotation, unreal.Vector(1, 0, 0))))
        out.append("   head %s mid %s end %s | hand_r %s -> %s" % (fmt(h0.translation), fmt(hm.translation), fmt(h1.translation),
                   fmt(hr0.translation), fmt(hr1.translation)))
        out.append("   head facing(+X yaw) %.0f -> %.0f, pitch-ish dz %.0f" % (yaw(h0.rotation, unreal.Vector(1, 0, 0)), yaw(h1.rotation, unreal.Vector(1, 0, 0)), h1.translation.z - h0.translation.z))
    except Exception as exc:
        out.append(f"   pose error {exc}")

os.makedirs(os.path.dirname(LOG), exist_ok=True)
with open(LOG, "w", encoding="utf-8") as handle:
    handle.write("\n".join(out) + "\n")
unreal.log("inspect_indoor_cover_pack: wrote " + LOG)
