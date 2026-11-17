"""First aid by wound tier (the game designer 2026-11-16, feat/first-aid): survey the healing clip pack and retarget the clips first_aid.json names.

The pack /Game/HealingAnimations is the game designer's (an untracked folder through a junction): it is only READ - nothing in it is modified or
saved. Its clips (Animations/AS_*) sit on the pack's own UE5 mannequin (Demo/Mannequins/Meshes/SKM_Manny, SK_Mannequin - the UE5 hierarchy,
spine_01..05, a different asset from the operatives'), so the clips are IK-retargeted UE5 -> UE4 with a TRANSIENT copy of the M4 pack's
RTG_UE5Manny_UE4Manny (source = the healing pack's IK_Mannequin, target = the M4 pack's IK_UE4_Mannequin; never saved; "Blend to Source" off) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/FirstAid/Male/<pack clip name>   (the operatives' UE4_Mannequin_Skeleton)
  female: SK_Female_soldier      -> /Game/Characters/Animations/FirstAid/Female/<pack clip name> (Female_Soldier_Skeleton)
Which clips: every clip name in Content/Data/Survival/first_aid.json ("items" -> "<item>" -> "clips" -> any list, and "patient" -> "poses" ->
"<pose>" -> "clips", R17-6) - add a name there and run the script again to get its copies. A name with a folder ("Injured/Idle_Crawl_Injured1")
is a copy another setup script already made (/Game/Characters/Animations/<Folder>/{Male,Female}/<Name>): it is only surveyed, never retargeted. Each copy is re-keyed (the retarget leaves the source's key data behind) and has root motion OFF (the clips are
played in place on the FullBody slot; the capsule stays where the treatment put it).

SURVEY (always, every pack clip): skeleton, length, frames, the root-motion flag, the root's travel over the clip (cm), and the pose: the pelvis
height above the lower foot / the root at the first, middle and last frame, the head height, the hands' mean forward reach from the pelvis
(+Y = the body's forward), the body's LONG AXIS (pelvis -> head on the ground; None when upright) and the way the CHEST faces on the ground
(upright bodies), both as an actor yaw - deg from the body's forward, clockwise seen from above (toward his right, -X) - and its BELLY ("up" =
on the back, "down" = face down / on all fours, "side") at the first / middle / last frame (R17-6: a patient clip whose long axis or belly
differs from the pose he lies in turns / rolls the body over the blend-in) - a pose class from the mid-frame pelvis height (standing > 75 cm, kneeling / squatting 40..75, sitting 18..40,
lying < 18) and a role from the name (Friend = a helper clip, Wounded* = the patient).

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_FIRST_AID_DRYRUN=1: the survey only (nothing retargeted or saved). CODEX_RESET_FIRST_AID_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_first_aid_animation.json (+ .txt summary)
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_FIRST_AID_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_FIRST_AID_CLIPS") == "1"
PACK = "/Game/HealingAnimations/Animations"
PACK_MESH = "/Game/HealingAnimations/Demo/Mannequins/Meshes/SKM_Manny"
PACK_IK = "/Game/HealingAnimations/Demo/Mannequins/Rigs/IK_Mannequin"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT_ROOT = "/Game/Characters/Animations/FirstAid"
OUT = {"He": OUT_ROOT + "/Male", "She": OUT_ROOT + "/Female"}
ANIM_ROOT = "/Game/Characters/Animations"
GENDER_FOLDER = {"He": "Male", "She": "Female"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
CONFIG = os.path.join(unreal.Paths.project_content_dir(), "Data", "Survival", "first_aid.json")
WORLD = unreal.AnimPoseSpaces.WORLD
SAMPLES = 24
log = []
report = {"pack": PACK, "clips": {}, "bodies": {}, "wanted": [], "missing": []}


def say(text):
    log.append(text)
    unreal.log("setup_first_aid_animation: " + text)


def load(path, quiet=False):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None and not quiet:
        say(f"MISSING {path}")
    return asset


def wanted_clips():
    """Every clip name first_aid.json lists under items.<item>.clips.<set> (lists of names)."""
    names = []
    data = {}
    try:
        with open(CONFIG, encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:  # noqa: BLE001
        say(f"no config {CONFIG}: {e}")
        return names
    lists = []
    for item in (data.get("items") or {}).values():
        lists += [c for c in ((item or {}).get("clips") or {}).values() if isinstance(c, list)]
    for pose in (((data.get("patient") or {}).get("poses")) or {}).values():
        if isinstance(pose, dict) and isinstance(pose.get("clips"), list):
            lists.append(pose["clips"])
    for clips in lists:
        for name in clips:
            if isinstance(name, str) and name not in names:
                names.append(name)
    return names


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        options.set_editor_property("extract_root_motion", False)
    except Exception:  # noqa: BLE001
        pass
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    try:
        return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD).translation
    except Exception:  # noqa: BLE001
        return None


def pose_class(pelvis_z):
    if pelvis_z is None:
        return "?"
    if pelvis_z > 75.0:
        return "standing"
    if pelvis_z > 40.0:
        return "kneeling/squat"
    if pelvis_z > 18.0:
        return "sitting"
    return "lying"


def role(name):
    low = name.lower()
    if "friend" in low:
        return "helper"
    if low.startswith("as_wounded"):
        return "patient"
    return "self"


def actor_yaw(dx, dy):
    """A component-space direction on the ground as an actor yaw: deg from +Y (the body's forward), clockwise seen from above (toward -X, his right)."""
    return round(math.degrees(math.atan2(-dx, dy)), 1)


def long_axis_yaw(frame):
    """The body's long axis (pelvis -> head) on the ground plane as an actor yaw; None when upright (standing / sitting / kneeling)."""
    p, h = frame.get("pelvis"), frame.get("head")
    if p is None or h is None:
        return None
    dx, dy = h.x - p.x, h.y - p.y
    if math.hypot(dx, dy) < 0.35 * max(1.0, (h - p).length()):
        return None
    return actor_yaw(dx, dy)


def chest_normal(frame):
    p, h, tl, tr = (frame.get(b) for b in ("pelvis", "head", "thigh_l", "thigh_r"))
    if None in (p, h, tl, tr):
        return None
    sx, sy, sz = h.x - p.x, h.y - p.y, h.z - p.z
    lx, ly, lz = tl.x - tr.x, tl.y - tr.y, tl.z - tr.z
    nx, ny, nz = sy * lz - sz * ly, sz * lx - sx * lz, sx * ly - sy * lx
    n = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return nx / n, ny / n, nz / n


def belly(frame):
    """Which way the chest faces: the normal of (pelvis -> head) x (right thigh -> left thigh); "up" / "down" / "side" (|z| < 0.5)."""
    n = chest_normal(frame)
    if n is None:
        return None
    return "up" if n[2] > 0.5 else ("down" if n[2] < -0.5 else f"side {n[2]:+.2f}")


def facing_yaw(frame):
    """Where the chest faces on the ground (an actor yaw) - the upright body's facing; None when the chest faces up / down."""
    n = chest_normal(frame)
    if n is None or math.hypot(n[0], n[1]) < 0.5:
        return None
    return actor_yaw(n[0], n[1])


def measure(clip, mesh=None):
    length = clip.get_editor_property("sequence_length")
    times = [i * length / SAMPLES for i in range(SAMPLES)] + [max(0.0, length - 0.001)]
    frames = []
    for t in times:
        p = pose_at(clip, t, mesh)
        frames.append({b: bone(p, b) for b in ("root", "pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r", "thigh_l", "thigh_r")})
    r0 = frames[0]["root"]

    def height(frame, b):
        v = frame.get(b)
        feet = [frame[f].z for f in ("foot_l", "foot_r") if frame.get(f) is not None]
        ground = min(feet + [frame["root"].z]) if feet else frame["root"].z
        return None if v is None else round(v.z - ground, 1)

    mid = frames[len(frames) // 2]
    out = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip),
           "root_travel_cm": round(((frames[-1]["root"] - r0).length()), 1) if r0 is not None else None,
           "pelvis_z": [height(frames[0], "pelvis"), height(mid, "pelvis"), height(frames[-1], "pelvis")],
           "head_z_mid": height(mid, "head"),
           "pelvis_drift_cm": round((frames[-1]["pelvis"] - frames[0]["pelvis"]).length(), 1) if frames[0]["pelvis"] else None}
    reach = []
    for f in frames:
        if f["pelvis"] is not None:
            for h in ("hand_l", "hand_r"):
                if f[h] is not None:
                    reach.append(f[h].y - f["pelvis"].y)
    out["hands_forward_cm"] = round(sum(reach) / len(reach), 1) if reach else None
    out["pose"] = pose_class(out["pelvis_z"][1])
    picks = [frames[0], mid, frames[-1]]
    out["long_axis_yaw"] = [long_axis_yaw(f) for f in picks]
    out["belly"] = [belly(f) for f in picks]
    out["facing_yaw"] = [facing_yaw(f) for f in picks]
    out["bones_first"] = {b: [round(v.x, 1), round(v.y, 1), round(v.z, 1)] for b, v in frames[0].items() if v is not None}
    return out


def make_retargeter():
    """UE5 Manny (the healing pack's IK_Mannequin) -> UE4 rig (IK_UE4_Mannequin), transient copy of the M4 pack's template (never saved)."""
    template = load(TEMPLATE_RTG)
    ik_src = load(PACK_IK)
    ik_dst = load(IK_UE4)
    if not template or not ik_src or not ik_dst:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_FirstAidPack_TMP", OUT_ROOT + "/_Tmp", template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik_src)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik_dst)
    for side, ik in ((unreal.RetargetSourceOrTarget.SOURCE, ik_src), (unreal.RetargetSourceOrTarget.TARGET, ik_dst)):
        try:
            ctrl.assign_ik_rig_to_all_ops(side, ik)
        except Exception as e:  # noqa: BLE001 - older signature / not needed for the source
            say(f"assign_ik_rig_to_all_ops({side}): {e}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    except Exception as e:  # noqa: BLE001
        say(f"auto_map_chains: {e}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE5 -> UE4 (IK_Mannequin -> IK_UE4_Mannequin, ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off)")
    return rtg


def rebake(clip):
    """Re-key the clip from its own frames (drops the key data the retarget left behind); returns the largest pose error (cm)."""
    model = clip.get_editor_property("data_model_interface")
    bones = [str(b) for b in model.get_bone_track_names()]
    keys = model.get_number_of_keys()
    length = clip.get_editor_property("sequence_length")
    times = [0.0, length * 0.37, max(0.0, length - 0.02)]
    probe = ("head", "hand_l", "hand_r", "foot_l", "foot_r")

    def sample():
        out = []
        for t in times:
            p = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, t, unreal.AnimPoseEvaluationOptions())
            out += [unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation for b in probe]
        return out
    before = sample()
    tracks = {b: ([], [], []) for b in bones}
    for f in range(keys):
        for b, t in zip(bones, unreal.AnimationLibrary.get_bone_poses_for_frame(clip, bones, f, False)):
            tracks[b][0].append(unreal.Vector(t.translation.x, t.translation.y, t.translation.z))
            tracks[b][1].append(unreal.Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w))
            tracks[b][2].append(unreal.Vector(t.scale3d.x, t.scale3d.y, t.scale3d.z))
    ctrl = clip.controller
    ctrl.open_bracket(unreal.Text("rebake"), False)
    ctrl.remove_all_bone_tracks(False)
    for b in bones:
        ctrl.add_bone_track(b, False)
        ctrl.set_bone_track_keys(b, tracks[b][0], tracks[b][1], tracks[b][2], False)
    ctrl.close_bracket(False)
    return max((a - b).length() for a, b in zip(before, sample()))


# --- Survey: every pack clip ------------------------------------------------------------------------------------------------------------
pack_paths = {}
for p in lib.list_assets(PACK, recursive=True, include_folder=False):
    pkg = p.split(".")[0]
    pack_paths[pkg.rsplit("/", 1)[1]] = pkg
source_mesh = load(PACK_MESH)
pack = {}
for name, path in sorted(pack_paths.items()):
    clip = load(path)
    if not isinstance(clip, unreal.AnimSequence):
        continue
    pack[name] = clip
    entry = measure(clip)
    skeleton = clip.get_editor_property("skeleton")
    entry["skeleton"] = skeleton.get_path_name() if skeleton else None
    entry["root_motion_flag"] = bool(clip.get_editor_property("enable_root_motion"))
    entry["role"] = role(name)
    report["clips"][name] = entry
    say(f"  {name}: {entry['length']} s ({entry['frames']} fr), {entry['pose']} pelvis {entry['pelvis_z']} head {entry['head_z_mid']}, "
        f"hands fwd {entry['hands_forward_cm']}, root travel {entry['root_travel_cm']} (RM {entry['root_motion_flag']}), {entry['role']}, "
        f"long axis {entry['long_axis_yaw']}, belly {entry['belly']}, facing {entry['facing_yaw']}, "
        f"skeleton {entry['skeleton']}")
say(f"{len(pack)} pack clips surveyed")
if source_mesh:
    say(f"source mesh {source_mesh.get_name()}, skeleton {source_mesh.get_editor_property('skeleton').get_path_name()}")

# --- Retarget the configured clips -------------------------------------------------------------------------------------------------------
wanted = wanted_clips()
report["wanted"] = wanted
foreign = [n for n in wanted if "/" in n]  # copies of other packs (another setup script made them): survey only
wanted = [n for n in wanted if "/" not in n]
report["missing"] = [n for n in wanted if n not in pack]
report["foreign"] = {}
for name in foreign:
    folder, clip_name = name.split("/", 1)
    for gender in ("He", "She"):
        path = f"{ANIM_ROOT}/{folder}/{GENDER_FOLDER[gender]}/{clip_name}"
        clip = load(path, quiet=True)
        if not isinstance(clip, unreal.AnimSequence):
            report["foreign"][f"{gender} {name}"] = {"missing": True}
            say(f"  {gender} {name}: MISSING {path}")
            continue
        row = measure(clip, load(TARGET_MESH[gender], quiet=True))
        report["foreign"][f"{gender} {name}"] = row
        say(f"  {gender} {name}: {row['length']} s, {row['pose']} pelvis {row['pelvis_z']}, long axis {row['long_axis_yaw']}, belly {row['belly']}, facing {row['facing_yaw']}")
say(f"first_aid.json names {len(wanted)} clips; not in the pack: {report['missing'] or 'none'}")
if not DRY and wanted:
    retargeter = make_retargeter()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for gender in ("He", "She"):
        target_mesh = load(TARGET_MESH[gender])
        copies, todo = {}, []
        for name in wanted:
            if name not in pack:
                continue
            dest = f"{OUT[gender]}/{name}"
            if lib.does_asset_exist(dest) and not RESET:
                copies[name] = lib.load_asset(dest)
                continue
            data = registry.get_asset_by_object_path(f"{pack_paths[name]}.{name}")
            if data.is_valid():
                todo.append(data)
        if todo and retargeter and source_mesh and target_mesh:
            result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
                todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False, overwrite_existing_files=True)
            for data in result:
                asset = data.get_asset()
                if isinstance(asset, unreal.AnimSequence):
                    err = rebake(asset)
                    asset.set_editor_property("enable_root_motion", False)
                    if err > 0.5:
                        say(f"  {data.asset_name}: re-key error {err:.2f} cm")
                    copies[str(data.asset_name)] = asset
                    lib.save_loaded_asset(asset, False)
            say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}")
        body = {"mesh": TARGET_MESH[gender], "clips": {}}
        for name in wanted:
            new = copies.get(name)
            if not new:
                body["clips"][name] = {"missing": True}
                continue
            row = measure(new, target_mesh)
            row["root_motion_enabled"] = bool(new.get_editor_property("enable_root_motion"))
            row["skeleton"] = new.get_editor_property("skeleton").get_path_name()
            body["clips"][name] = row
            say(f"    {gender} {name}: {row['length']} s, {row['pose']} pelvis {row['pelvis_z']} (pack {report['clips'][name]['pelvis_z']}), "
                f"hands fwd {row['hands_forward_cm']}, root motion {row['root_motion_enabled']}, long axis {row['long_axis_yaw']}, belly {row['belly']}, facing {row['facing_yaw']}")
        report["bodies"][gender] = body
    if retargeter:
        lib.delete_loaded_asset(retargeter)
    if lib.does_directory_exist(OUT_ROOT + "/_Tmp"):
        lib.delete_directory(OUT_ROOT + "/_Tmp")
# --- Blend curves (R17-6): the visible body's turn while a lying clip blends in over the pose the patient lies in -----------------------------
# The FullBody slot blends the LOCAL bone rotations (normalized lerp, shortest way), so the long axis does not turn linearly with the blend weight
# (measured in game: the linear counter-turn left a 77 deg/s wobble on AS_WoundedGetInjection). For every clip of a pose list that turns the body
# (|long axis| > 45 deg) the turn at blend weights 0, 0.1 .. 1 over the pose's FIRST clip (the pose he lies in) is computed here - the chain
# root -> pelvis -> spine_01..03 -> neck_01 -> head, both bodies, averaged - for first_aid.json patient.clip_data.<clip>.long_axis_curve.
CHAIN = ("root", "pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "head")
LOCAL = unreal.AnimPoseSpaces.LOCAL


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def nlerp(a, b, t):
    if sum(i * j for i, j in zip(a, b)) < 0:
        b = tuple(-i for i in b)
    q = tuple(i * (1 - t) + j * t for i, j in zip(a, b))
    n = math.sqrt(sum(i * i for i in q)) or 1.0
    return tuple(i / n for i in q)


def chain_locals(clip, t, mesh):
    pose = pose_at(clip, t, mesh)
    out = []
    for b in CHAIN:
        tr = unreal.AnimPoseExtensions.get_bone_pose(pose, b, LOCAL)
        r = tr.rotation
        out.append(((tr.translation.x, tr.translation.y, tr.translation.z), (r.x, r.y, r.z, r.w)))
    return out, pose


def chain_axis(locals_):
    pos, rot = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)
    points = {}
    for b, (t, q) in zip(CHAIN, locals_):
        d = qrot(rot, t)
        pos = (pos[0] + d[0], pos[1] + d[1], pos[2] + d[2])
        rot = qmul(rot, q)
        points[b] = pos
    p, h = points["pelvis"], points["head"]
    return h[0] - p[0], h[1] - p[1], points


def blend_curve(base, clip, mesh):
    a, pose_a = chain_locals(base, 0.0, mesh)
    b, _ = chain_locals(clip, 0.0, mesh)
    check = unreal.AnimPoseExtensions.get_bone_pose(pose_a, "head", WORLD).translation
    _, _, pts = chain_axis(a)
    err = math.dist(pts["head"], (check.x, check.y, check.z))
    yaws = []
    for i in range(11):
        w = i / 10.0
        mixed = [(tuple(ta[k] * (1 - w) + tb[k] * w for k in range(3)), nlerp(qa, qb, w)) for (ta, qa), (tb, qb) in zip(a, b)]
        dx, dy, _ = chain_axis(mixed)
        yaws.append(math.degrees(math.atan2(-dx, dy)))
    turn, prev = [0.0], yaws[0]  # unwound, relative to weight 0
    for y in yaws[1:]:
        step = (y - prev + 180.0) % 360.0 - 180.0
        turn.append(turn[-1] + step)
        prev = y
    return [round(v, 1) for v in turn], round(err, 2)


report["blend_curves"] = {}
try:
    with open(CONFIG, encoding="utf-8") as f:
        poses = ((json.load(f).get("patient") or {}).get("poses")) or {}
except Exception:  # noqa: BLE001
    poses = {}


def copy_of(name, gender):
    if "/" in name:
        folder, clip_name = name.split("/", 1)
        return load(f"{ANIM_ROOT}/{folder}/{GENDER_FOLDER[gender]}/{clip_name}", quiet=True)
    return load(f"{OUT[gender]}/{name}", quiet=True)


for pose_name, pose in poses.items():
    clips = (pose or {}).get("clips") or []
    if len(clips) < 2:
        continue
    for name in clips[1:]:
        rows = {}
        for gender in ("He", "She"):
            base, clip, mesh = copy_of(clips[0], gender), copy_of(name, gender), load(TARGET_MESH[gender], quiet=True)
            if not isinstance(base, unreal.AnimSequence) or not isinstance(clip, unreal.AnimSequence):
                continue
            rows[gender] = blend_curve(base, clip, mesh)
        if not rows or max(abs(c[-1]) for c, _ in rows.values()) < 45.0:
            continue
        mean = [round(sum(c[i] for c, _ in rows.values()) / len(rows), 1) for i in range(11)]
        report["blend_curves"][f"{pose_name}/{name}"] = {"base": clips[0], "mean": mean, **{g: {"curve": c, "chain_err_cm": e} for g, (c, e) in rows.items()}}
        say(f"  blend curve {pose_name}: {clips[0]} -> {name}: {mean} (chain check {', '.join(f'{g} {e} cm' for g, (_, e) in rows.items())})")

say("DRY RUN: survey only, nothing saved" if DRY else "done")
os.makedirs(os.path.join(unreal.Paths.project_saved_dir(), "Logs"), exist_ok=True)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_first_aid_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_first_aid_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
