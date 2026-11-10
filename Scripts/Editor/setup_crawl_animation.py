"""The prone crawl set (the game designer's CrawlAndShoot_Bug_01, 2026-11-09, fix/prone-crawl-under-fire): retarget, check and measure the
8-direction in-place crawl loops + the crawl idle that UOperativeAnimInstance's native prone crawl layer plays (OperativeAnimInstanceProneCrawl.cpp).

The pack /Game/Crawl_MocapAnimPack is the user's (an untracked folder through a junction): it is only READ - nothing in it is modified or saved.
Its skeleton (Demo/Models/SK_Mannequin_A_Skeleton) is the UE4 mannequin hierarchy, so - exactly like the launcher set
(Scripts/Editor/setup_launcher_animation.py) - the clips are IK-retargeted with the M4 pack's IK_UE4_Mannequin on BOTH sides (a transient
retargeter, never saved; "Blend to Source" off) from the pack's SK_Mannequin_A onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/Crawl/Male/<pack clip name>   (the operatives' UE4_Mannequin_Skeleton)
  female: SK_Female_soldier      -> /Game/Characters/Animations/Crawl/Female/<pack clip name> (Female_Soldier_Skeleton)
The clip list = every pack path in Content/Data/Weapons/weapon_anim_sets.json "prone_crawl". Each copy is re-keyed (the retarget leaves the
source's key data behind) and has root motion off. Measured per clip (component space, +Y = the body's forward, +X = its LEFT):
  travel      the pack's root-motion twin (Locomotion_Set/Loco_Linear_root/<name without _IPC>): root travel / length (cm/s) and its yaw
              (deg, clockwise from forward) - the direction the clip really crawls
  contact     the in-place copy's planted limbs (hands, knees = calf_*, balls of the feet): their horizontal speed while within 2 cm of their
              lowest height - the ground speed the clip is authored for (a planted limb of an in-place loop sweeps back at the crawl speed)
  phase       the share of the loop where hand_l is furthest along the travel (the reach before its plant; the layer's phase_offsets)
  drift       the pelvis between the loop's first and last frame (cm; an in-place loop ~0)
  posture     head / hands relative to the pelvis vs the pack clip (cm)
  stride      male / female contact speed vs the pack's (the layer's stride_scale)

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_CRAWL_DRYRUN=1: retarget in memory and measure only. CODEX_RESET_CRAWL_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_crawl_animation.json (+ .txt summary)
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_CRAWL_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_CRAWL_CLIPS") == "1"
PACK = "/Game/Crawl_MocapAnimPack/"
PACK_MESH = PACK + "Demo/Models/SK_Mannequin_A"
LINEAR_ROOT = PACK + "Animations/Locomotion_Set/Loco_Linear_root/"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Animations/Crawl/Male", "She": "/Game/Characters/Animations/Crawl/Female"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
WORLD = unreal.AnimPoseSpaces.WORLD
CONTACTS = ("hand_l", "hand_r", "calf_l", "calf_r", "ball_l", "ball_r")
RATE = 60.0  # samples per second
log = []
report = {"clips": {}, "bodies": {}}


def say(text):
    log.append(text)
    unreal.log("setup_crawl_animation: " + text)


def load(path):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def pack_paths():
    with open(os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapon_anim_sets.json"), encoding="utf-8") as f:
        block = json.load(f)["prone_crawl"]
    out = []

    def walk(v):
        if isinstance(v, str) and v.startswith(PACK):
            out.append(v.split(".")[0])
        elif isinstance(v, dict):
            for x in v.values():
                walk(x)
        elif isinstance(v, list):
            for x in v:
                walk(x)
    walk(block)
    return sorted(set(out))


def pose_at(clip, t, mesh=None, root_motion=False):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        options.set_editor_property("extract_root_motion", root_motion)
    except Exception:  # noqa: BLE001 - not every version exposes it
        pass
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD)


def heading_deg(v):
    """Yaw of a component-space direction, clockwise from the body's forward (+Y), + = to the body's right (-X)."""
    return round(math.degrees(math.atan2(-v.x, v.y)), 1)


def root_travel(name):
    """The pack's linear-root twin of an _IPC loop: its root's travel per second and heading."""
    twin = load(LINEAR_ROOT + name.replace("_IPC", ""))
    if not twin:
        return None
    length = twin.get_editor_property("sequence_length")
    a = bone(pose_at(twin, 0.0), "root").translation
    b = bone(pose_at(twin, length), "root").translation
    d = b - a
    flat = math.hypot(d.x, d.y)
    return {"length": round(length, 3), "travel_cm": round(flat, 1), "speed": round(flat / max(length, 0.01), 1),
            "heading_deg": heading_deg(d) if flat > 1.0 else None}


def contact_speed(clip, mesh=None):
    """The planted limbs' horizontal speed (component space) while each is within 2 cm of its lowest height: median, cm/s."""
    length = clip.get_editor_property("sequence_length")
    n = max(2, int(length * RATE))
    track = {b: [] for b in CONTACTS}
    for i in range(n + 1):
        p = pose_at(clip, i * length / n, mesh)
        for b in CONTACTS:
            track[b].append(bone(p, b).translation)
    speeds, per_bone = [], {}
    dt = length / n
    for b, pts in track.items():
        low = min(v.z for v in pts)
        mine = []
        for i in range(1, len(pts)):
            if pts[i].z < low + 2.0 and pts[i - 1].z < low + 2.0:
                mine.append(math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y) / dt)
        mine.sort()
        per_bone[b] = round(mine[len(mine) // 2], 1) if mine else None
        speeds += mine
    speeds.sort()
    return (round(speeds[len(speeds) // 2], 1) if speeds else None), per_bone


def reach_phase(clip, heading, mesh=None):
    """Share of the loop where hand_l is furthest along the travel heading (component space)."""
    if heading is None:
        return 0.0
    length = clip.get_editor_property("sequence_length")
    n = max(2, int(length * RATE))
    rad = math.radians(heading)
    axis = (-math.sin(rad), math.cos(rad))  # heading -> component (x, y)
    best = (-1e9, 0.0)
    for i in range(n):
        t = i * length / n
        p = pose_at(clip, t, mesh)
        h = bone(p, "hand_l").translation - bone(p, "pelvis").translation
        along = h.x * axis[0] + h.y * axis[1]
        if along > best[0]:
            best = (along, t / length)
    return round(best[1], 3)


def posture(clip, t, mesh=None):
    p = pose_at(clip, t, mesh)
    pelvis = bone(p, "pelvis").translation
    return {b: bone(p, b).translation - pelvis for b in ("head", "hand_l", "hand_r")}, pelvis


def make_retargeter():
    """UE4 rig -> UE4 rig, transient (never saved): the template's op stack with both sides on IK_UE4_Mannequin."""
    template = load(TEMPLATE_RTG)
    ik = load(IK_UE4)
    if not template or not ik:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_CrawlPack_UE4_TMP", "/Game/Characters/Animations/Crawl/_Tmp", template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik)
    try:
        ctrl.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, ik)
    except Exception as e:  # noqa: BLE001 - older signature
        say(f"assign_ik_rig_to_all_ops: {e}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    except Exception as e:  # noqa: BLE001
        say(f"auto_map_chains: {e}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE4 -> UE4 (IK_UE4_Mannequin both sides, ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off)")
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


def folder_mb(path):
    disk = path.replace("/Game/", unreal.Paths.project_content_dir())
    total = 0
    for root, _, files in os.walk(disk):
        total += sum(os.path.getsize(os.path.join(root, f)) for f in files)
    return total / 1048576.0


# --- The pack clips -------------------------------------------------------------------------------------------------------------------
paths = pack_paths()
say(f"{len(paths)} pack clips listed in weapon_anim_sets.json \"prone_crawl\"")
pack = {p.rsplit("/", 1)[1]: load(p) for p in paths}
for name, clip in pack.items():
    if not clip:
        continue
    length = clip.get_editor_property("sequence_length")
    entry = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip),
             "root_motion": bool(clip.get_editor_property("enable_root_motion"))}
    (_, p0), (_, p1) = posture(clip, 0.0), posture(clip, length)
    entry["drift_cm"] = round((p1 - p0).length(), 2)
    entry["pelvis_z"] = round(p0.z, 1)
    if "_IPC" in name:
        entry["travel"] = root_travel(name)
        heading = entry["travel"]["heading_deg"] if entry["travel"] else None
        entry["contact_speed"], entry["contact_by_bone"] = contact_speed(clip)
        entry["reach_phase"] = reach_phase(clip, heading)
    report["clips"][name] = entry
    say(f"  {name}: {length:.2f} s, travel {entry.get('travel')}, contact {entry.get('contact_speed')} cm/s, phase {entry.get('reach_phase')}")

# --- Retarget -------------------------------------------------------------------------------------------------------------------------
retargeter = make_retargeter()
source_mesh = load(PACK_MESH)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
mb_before = sum(folder_mb(o) for o in OUT.values())
for gender in ("He", "She"):
    target_mesh = load(TARGET_MESH[gender])
    copies, todo = {}, []
    for path in paths:
        name = path.rsplit("/", 1)[1]
        dest = f"{OUT[gender]}/{name}"
        if lib.does_asset_exist(dest) and not RESET:
            copies[name] = lib.load_asset(dest)
            continue
        data = registry.get_asset_by_object_path(f"{path}.{name}")
        if data.is_valid():
            todo.append(data)
        else:
            say(f"  {name}: not in the pack")
    if todo and retargeter and source_mesh and target_mesh:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False, overwrite_existing_files=True)
        for data in result:
            asset = data.get_asset()
            if isinstance(asset, unreal.AnimSequence):
                asset.set_editor_property("enable_root_motion", False)
                err = rebake(asset)
                if err > 0.5:
                    say(f"  {data.asset_name}: re-key error {err:.2f} cm")
                copies[str(data.asset_name)] = asset
                if not DRY:
                    lib.save_loaded_asset(asset, False)
        say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}{' (in memory)' if DRY else ''}")
    body = {"mesh": TARGET_MESH[gender], "clips": {}}
    strides = []
    for name, clip in pack.items():
        new = copies.get(name)
        if not clip or not new:
            body["clips"][name] = {"missing": True}
            continue
        length = clip.get_editor_property("sequence_length")
        row = {"length": round(new.get_editor_property("sequence_length"), 3), "frames": unreal.AnimationLibrary.get_num_frames(new)}
        worst = 0.0
        for t in (0.0, length * 0.25, length * 0.5, length * 0.75, length):
            (a, _), (b, _) = posture(clip, t), posture(new, t, target_mesh)
            worst = max(worst, max((a[k] - b[k]).length() for k in a))
        (_, n0), (_, n1) = posture(new, 0.0, target_mesh), posture(new, length, target_mesh)
        row.update({"posture_cm": round(worst, 1), "drift_cm": round((n1 - n0).length(), 2), "pelvis_z": round(n0.z, 1)})
        if "_IPC" in name:
            heading = (report["clips"][name].get("travel") or {}).get("heading_deg")
            row["contact_speed"], row["contact_by_bone"] = contact_speed(new, target_mesh)
            row["reach_phase"] = reach_phase(new, heading, target_mesh)
            src = report["clips"][name].get("contact_speed")
            if src and row["contact_speed"]:
                row["stride"] = round(row["contact_speed"] / src, 3)
                strides.append(row["stride"])
        body["clips"][name] = row
    strides.sort()
    body["stride_median"] = strides[len(strides) // 2] if strides else None
    report["bodies"][gender] = body
    say(f"{gender}: {len(copies)} / {len(paths)} copies; stride median {body['stride_median']}; worst posture "
        f"{max((r.get('posture_cm', 0) for r in body['clips'].values()), default=0):.1f} cm")
mb_after = sum(folder_mb(o) for o in OUT.values())
report["size_mb"] = {"before": round(mb_before, 2), "after": round(mb_after, 2)}
if retargeter:
    lib.delete_loaded_asset(retargeter)
say(f"copies {mb_after:.1f} MB")
say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_crawl_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_crawl_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
