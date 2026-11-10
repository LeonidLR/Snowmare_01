"""The injured locomotion set (the game designer 2026-11-10: "operatives sometimes walk very slowly and I cannot see why"): retarget, check and
measure the CON_Injured clips that UOperativeAnimInstance's native injured layer plays (OperativeAnimInstanceInjured.cpp, InjuredLocomotionRules).

The pack /Game/CON_Injured is the game designer's (an untracked folder through a junction): it is only READ - nothing in it is modified or saved.
Its UE4 set (Animations/In-Place, Animations/RootMotion) sits on the pack's own UE4 mannequin (EpicContent/Mannequin/Character/Mesh/SK_Mannequin,
UE4_Mannequin_Skeleton - the UE4 hierarchy, a different asset from the operatives'), so - exactly like the prone crawl
(Scripts/Editor/setup_crawl_animation.py) - the clips are IK-retargeted with the M4 pack's IK_UE4_Mannequin on BOTH sides (a transient
retargeter, never saved; "Blend to Source" off) onto
  male:   SK_Post_Apo_Survivor_a -> /Game/Characters/Animations/Injured/Male/<pack clip name>   (the operatives' UE4_Mannequin_Skeleton)
  female: SK_Female_soldier      -> /Game/Characters/Animations/Injured/Female/<pack clip name> (Female_Soldier_Skeleton)
The clip list = every pack path in Content/Data/Weapons/weapon_anim_sets.json "injured". Each copy is re-keyed (the retarget leaves the
source's key data behind) and has root motion off. Measured per clip (component space, +Y = the body's forward, +X = its LEFT):
  travel      the pack's root-motion twin (RootMotion/<name>_RM, where there is one): root travel / length (cm/s) and its heading
  contact     the planted feet (walks: foot_* / ball_*; the crawl: hands, knees = calf_*, balls): their horizontal speed while within 2 cm of
              their lowest height - the ground speed the in-place loop is authored for (a planted foot sweeps back at the walk speed); its
              heading (the opposite of the sweep) = the direction the clip walks
  best speed  the ground speed at which the loop's best-planted contact (the slowest of them each frame) moves least over the ground, median
              over the loop (the layer's walk_speeds / slow_walk_speed / crawl_speed); "own slide" = that median / the speed - what the clip
              itself slides at its best (a stance foot that pivots in a strafe has no bone that stands still)
  strikes     foot_l touch-downs per loop (1 = one gait cycle per loop: the shared phase plays one cycle per loop)
  phase       walks: the share of the loop where foot_l strikes (the layer's phase_offsets: a direction blend plants the same foot);
              the crawl: where hand_l is furthest along the travel (as the prone crawl)
  upper       hands relative to the pelvis (cm, mean) and the hands' nearest distance to the torso (spine_01 .. spine_03): a hand held on the
              wound sits on the torso - the clips are UNARMED, the input to the upper-body decision; the pelvis / spine_03 rotation range
              over the loop (deg: the limp in the hips vs the torso)
  drift       the pelvis between the loop's first and last frame (cm; an in-place loop ~0)
  posture     head / hands relative to the pelvis vs the pack clip (cm)
  stride      male / female contact speed vs the pack's (the layer's stride_scale)

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path, forward slashes> -unattended -nullrhi -nosplash
CODEX_INJURED_DRYRUN=1: retarget in memory and measure only (nothing saved). CODEX_RESET_INJURED_CLIPS=1: re-retarget existing copies.
Result: Saved/Logs/setup_injured_animation.json (+ .txt summary)
"""
import json
import math
import os

import unreal

lib = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_INJURED_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_INJURED_CLIPS") == "1"
PACK = "/Game/CON_Injured/"
PACK_MESH = PACK + "EpicContent/Mannequin/Character/Mesh/SK_Mannequin"
ROOT_MOTION = PACK + "Animations/RootMotion/"
IK_UE4 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/IK_UE4_Mannequin"
TEMPLATE_RTG = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Animations/Injured/Male", "She": "/Game/Characters/Animations/Injured/Female"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
WORLD = unreal.AnimPoseSpaces.WORLD
WALK_CONTACTS = ("foot_l", "foot_r", "ball_l", "ball_r")
CRAWL_CONTACTS = ("hand_l", "hand_r", "calf_l", "calf_r", "ball_l", "ball_r")
TORSO = ("spine_01", "spine_02", "spine_03")
RATE = 60.0  # samples per second
log = []
report = {"clips": {}, "bodies": {}}


def say(text):
    log.append(text)
    unreal.log("setup_injured_animation: " + text)


def load(path, quiet=False):
    asset = lib.load_asset(path) if lib.does_asset_exist(path) else None
    if asset is None and not quiet:
        say(f"MISSING {path}")
    return asset


def pack_paths():
    with open(os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapon_anim_sets.json"), encoding="utf-8") as f:
        block = json.load(f)["injured"]
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


def is_crawl(name):
    return "Crawl" in name


def is_loop(name):
    return name.startswith("Walk_") or name.startswith("Crawl_") or name.startswith("Run_")


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    try:
        options.set_editor_property("extract_root_motion", False)
    except Exception:  # noqa: BLE001 - not every version exposes it
        pass
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, max(0.0, min(t, length - 0.001)), options)


def bone(pose, name):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, WORLD)


def heading_deg(dx, dy):
    """Yaw of a component-space direction, clockwise from the body's forward (+Y), + = to the body's right (-X)."""
    return round(math.degrees(math.atan2(-dx, dy)), 1)


def samples(clip, bones, mesh=None):
    length = clip.get_editor_property("sequence_length")
    n = max(2, int(length * RATE))
    frames = []
    for i in range(n):
        p = pose_at(clip, i * length / n, mesh)
        frames.append({b: bone(p, b) for b in bones})
    return length, n, frames


def root_travel(name):
    """The pack's root-motion twin of an in-place loop: its root's travel per second and heading."""
    twin = load(ROOT_MOTION + name + "_RM", quiet=True)
    if not twin:
        return None
    length = twin.get_editor_property("sequence_length")
    a = bone(pose_at(twin, 0.0), "root").translation
    b = bone(pose_at(twin, length), "root").translation
    d = b - a
    flat = math.hypot(d.x, d.y)
    return {"length": round(length, 3), "travel_cm": round(flat, 1), "speed": round(flat / max(length, 0.01), 1),
            "heading_deg": heading_deg(d.x, d.y) if flat > 1.0 else None}


def measure(clip, name, mesh=None):
    """Contacts, sweep heading, strikes, phase, upper body and the hip / torso rotation range of one loop (or idle)."""
    contacts = CRAWL_CONTACTS if is_crawl(name) else WALK_CONTACTS
    bones = sorted(set(contacts + TORSO + ("pelvis", "hand_l", "hand_r", "head", "foot_l")))
    length, n, frames = samples(clip, bones, mesh)
    dt = length / n
    out = {}
    if is_loop(name):
        speeds, sweep_x, sweep_y, per_bone = [], 0.0, 0.0, {}
        for b in contacts:
            pts = [f[b].translation for f in frames]
            low = min(v.z for v in pts)
            mine = []
            for i in range(n):
                a, c = pts[i - 1], pts[i]  # cyclic: frame 0 follows the last
                if a.z < low + 2.0 and c.z < low + 2.0:
                    mine.append(math.hypot(c.x - a.x, c.y - a.y) / dt)
                    sweep_x += c.x - a.x
                    sweep_y += c.y - a.y
            mine.sort()
            per_bone[b] = round(mine[len(mine) // 2], 1) if mine else None
            speeds += mine
        speeds.sort()
        out["contact_speed"] = round(speeds[len(speeds) // 2], 1) if speeds else None
        out["contact_by_bone"] = per_bone
        # The planted contacts sweep backwards: the clip walks the opposite way.
        out["walk_heading_deg"] = heading_deg(-sweep_x, -sweep_y) if math.hypot(sweep_x, sweep_y) > 1.0 else None
        # The clip played at a ground speed v along its heading: each frame's best-planted contact (the slowest of them over the ground) moves
        # at |its component velocity + v * heading|. The v that keeps it slowest (median over the loop) = the speed the loop is authored for; the
        # remaining median share = the loop's own slide (a foot that pivots in a strafe has no bone that stands still).
        if out["walk_heading_deg"] is not None:
            rad = math.radians(out["walk_heading_deg"])
            travel = (-math.sin(rad), math.cos(rad))  # heading -> component (x, y)
            vels = []
            for i in range(n):
                a_f, c_f = frames[i - 1], frames[i]
                vels.append([((c_f[b].translation.x - a_f[b].translation.x) / dt, (c_f[b].translation.y - a_f[b].translation.y) / dt) for b in contacts])

            def slide_at(v):
                mins = sorted(min(math.hypot(vx + v * travel[0], vy + v * travel[1]) for vx, vy in fr) for fr in vels)
                return mins[len(mins) // 2]
            base = out["contact_speed"] or 50.0
            best = min((slide_at(base * k / 50.0), base * k / 50.0) for k in range(25, 76))
            out["best_speed"] = round(best[1], 1)
            out["intrinsic_slide"] = round(best[0] / max(best[1], 1.0), 3)
            out["slide_at_contact"] = round(slide_at(base) / max(base, 1.0), 3)
        heel = [f["foot_l"].translation.z for f in frames]
        low = min(heel)
        planted = [z < low + 2.0 for z in heel]
        strikes = [i for i in range(n) if planted[i] and not planted[i - 1]]
        out["strikes_foot_l"] = len(strikes)
        if is_crawl(name):
            heading = out["walk_heading_deg"] or 0.0
            rad = math.radians(heading)
            axis = (-math.sin(rad), math.cos(rad))
            best = max(range(n), key=lambda i: (frames[i]["hand_l"].translation.x - frames[i]["pelvis"].translation.x) * axis[0]
                       + (frames[i]["hand_l"].translation.y - frames[i]["pelvis"].translation.y) * axis[1])
            out["phase"] = round(best / n, 3)
        else:
            out["phase"] = round(strikes[0] / n, 3) if strikes else 0.0
    # Upper body: hands vs the pelvis, the hands' nearest torso distance, the hip / torso rotation ranges.
    hands = {}
    for h in ("hand_l", "hand_r"):
        rel = [f[h].translation - f["pelvis"].translation for f in frames]
        near = [min((f[h].translation - f[t].translation).length() for t in TORSO) for f in frames]
        hands[h] = {"rel_mean": [round(sum(v.x for v in rel) / n, 1), round(sum(v.y for v in rel) / n, 1), round(sum(v.z for v in rel) / n, 1)],
                    "torso_dist_mean": round(sum(near) / n, 1)}
    out["hands"] = hands

    def rot_range(b):
        rots = [f[b].rotation.rotator() for f in frames]
        return [round(max(getattr(r, k) for r in rots) - min(getattr(r, k) for r in rots), 1) for k in ("roll", "pitch", "yaw")]
    out["pelvis_rot_range_deg"] = rot_range("pelvis")
    out["spine_03_rot_range_deg"] = rot_range("spine_03")
    p0, p1 = frames[0]["pelvis"].translation, frames[-1]["pelvis"].translation
    out["drift_cm"] = round((p1 - p0).length(), 2)
    out["pelvis_z"] = round(p0.z, 1)
    out["pelvis_bob_cm"] = round(max(f["pelvis"].translation.z for f in frames) - min(f["pelvis"].translation.z for f in frames), 1)
    return out


def posture(clip, t, mesh=None):
    p = pose_at(clip, t, mesh)
    pelvis = bone(p, "pelvis").translation
    return {b: bone(p, b).translation - pelvis for b in ("head", "hand_l", "hand_r")}


def make_retargeter():
    """UE4 rig -> UE4 rig, transient (never saved): the template's op stack with both sides on IK_UE4_Mannequin."""
    template = load(TEMPLATE_RTG)
    ik = load(IK_UE4)
    if not template or not ik:
        return None
    rtg = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset("RTG_InjuredPack_UE4_TMP", "/Game/Characters/Animations/Injured/_Tmp", template)
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
say(f"{len(paths)} pack clips listed in weapon_anim_sets.json \"injured\"{' (DRY RUN)' if DRY else ''}")
pack = {p.rsplit("/", 1)[1]: load(p) for p in paths}
for name, clip in pack.items():
    if not clip:
        continue
    length = clip.get_editor_property("sequence_length")
    entry = {"length": round(length, 3), "frames": unreal.AnimationLibrary.get_num_frames(clip),
             "root_motion": bool(clip.get_editor_property("enable_root_motion")),
             "skeleton": clip.get_editor_property("skeleton").get_path_name()}
    entry["travel"] = root_travel(name)
    entry.update(measure(clip, name))
    report["clips"][name] = entry
    say(f"  {name}: {length:.2f} s, travel {entry.get('travel')}, contact {entry.get('contact_speed')} cm/s, best speed {entry.get('best_speed')} "
        f"(own slide {entry.get('intrinsic_slide')}, at contact {entry.get('slide_at_contact')}), heading {entry.get('walk_heading_deg')}, "
        f"strikes {entry.get('strikes_foot_l')}, phase {entry.get('phase')}, hands {entry['hands']}, pelvis rot {entry['pelvis_rot_range_deg']}, "
        f"spine_03 rot {entry['spine_03_rot_range_deg']}, bob {entry['pelvis_bob_cm']} cm, drift {entry['drift_cm']} cm")

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
    strides = {"walk": [], "crawl": []}
    for name, clip in pack.items():
        new = copies.get(name)
        if not clip or not new:
            body["clips"][name] = {"missing": True}
            continue
        length = clip.get_editor_property("sequence_length")
        row = {"length": round(new.get_editor_property("sequence_length"), 3), "frames": unreal.AnimationLibrary.get_num_frames(new)}
        worst = 0.0
        for t in (0.0, length * 0.25, length * 0.5, length * 0.75, length):
            a, b = posture(clip, t), posture(new, t, target_mesh)
            worst = max(worst, max((a[k] - b[k]).length() for k in a))
        row["posture_cm"] = round(worst, 1)
        row.update(measure(new, name, target_mesh))
        src = report["clips"][name].get("contact_speed")
        if src and row.get("contact_speed"):
            row["stride"] = round(row["contact_speed"] / src, 3)
            strides["crawl" if is_crawl(name) else "walk"].append(row["stride"])
        body["clips"][name] = row
    for kind, values in strides.items():
        values.sort()
        body[f"stride_median_{kind}"] = values[len(values) // 2] if values else None
    report["bodies"][gender] = body
    say(f"{gender}: {len(copies)} / {len(paths)} copies; stride median walk {body['stride_median_walk']} crawl {body['stride_median_crawl']}; "
        f"worst posture {max((r.get('posture_cm', 0) for r in body['clips'].values()), default=0):.1f} cm")
    for name, row in body["clips"].items():
        if not row.get("missing"):
            say(f"    {gender} {name}: contact {row.get('contact_speed')} best speed {row.get('best_speed')} (own slide {row.get('intrinsic_slide')}, "
                f"at contact {row.get('slide_at_contact')}) heading {row.get('walk_heading_deg')} phase {row.get('phase')} "
                f"stride {row.get('stride')} posture {row.get('posture_cm')} drift {row.get('drift_cm')}")
mb_after = sum(folder_mb(o) for o in OUT.values())
report["size_mb"] = {"before": round(mb_before, 2), "after": round(mb_after, 2)}
if retargeter:
    lib.delete_loaded_asset(retargeter)
say(f"copies {mb_after:.1f} MB on disk")
say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_injured_animation.json"), "w", encoding="utf-8") as f:
    json.dump(report, f, indent=1)
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_injured_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
