"""Indoor cover clips (designer 2026-11-22, docs/design/INDOOR_COVER_ANIMATIONS.md): the designer's Riffle Cover Animation pack
/Game/Riffle_Cover_Animation/Animations (UE5 Manny skeleton .../Demo/Characters/Mannequins/Meshes/SK_Mannequin, 97 sequences, 30 fps,
root motion off, no notifies, nothing additive) retargeted onto BOTH operative bodies, baked, mirrored and turned into aim offsets so the
C++ cover layer (the "riffle" cover set) can play them.

Why: the outdoor cover set (M4 Cover Pack) stays; inside buildings the operative hugs walls with this pack (stand + crouch, shimmy, peek-aim
with real aim offsets, reload / grenade from cover, the over-the-top window aim). The clips carry their travel on the ROOT bone (the pelvis
follows it) and root motion is off, so the loops must be baked in place and the side-less over-the-top set must be mirrored.

Bodies (same recipe as setup_door_animation.py):
  UE4  operatives (UE4 mannequin hierarchy, SK_Post_Apo_Survivor_a; the Female_Soldier body plays the same copies): the M4 pack's
       RTG_UE5Manny_UE4Manny with its "Blend to Source" op off IN MEMORY (never saved).
  Pmc  SKM_PmcOperator (SK_PmcOperator_Skeleton, UE5 hierarchy + root_inner): a TRANSIENT retargeter from RTG_UE4Manny_UE5Manny's op stack
       with IK_Mannequin on both sides (auto-mapped chains, "Blend to Source" off), deleted at the end; the copies are re-keyed.
Skipped (designer): *_Dive*, *_Dodge*.

Bakes on the copies (root motion OFF everywhere, force root lock OFF so the mesh travel of the one-shots shows):
  IN PLACE  loops (_Idle, Walk/_Walk, _Aim_Idle / _Aim1_Idle / _Aim2_Idle), the offset poses, and (lead correction 2026-11-22) the walk
            transitions Walk_Start / Walk_Stop / Walk_End / Walk_Turn (the capsule itself moves along the wall while they play): the root
            keeps its frame-0 XY (its Z per frame), its yaw is removed and every bone parented to the root (root_inner on the PMC body
            counts as part of the root) is re-keyed so its component-space transform is the original one minus the removed travel.
            The visible pose is unchanged (Walk_Turn keeps its 90 deg body turn through the pelvis); loops stay seamless (logged).
  ONE-SHOT  _Start, _Exit / StandR _End, _To_L / _To_R, _Aim*_Start / _Aim*_End, _Reload, _Grenade_Throw: the root track stays (mesh travel
            only, the capsule does not move); a root ROTATION would be folded into the root's children the same way (root translation kept).
  MIRROR    (designer Q3) the over-the-top set exists for crouch LEFT only: AS_Cover_CrouchL_Aim2_{Start,Idle,End} -> AS_Cover_CrouchR_Aim2_*,
            offsets AS_Cover_CrouchL_Aim2_<P><S> -> AS_Cover_CrouchR_Aim2_<P><S'> (the name says the result: mirrored UL aims right = UR).
            Component space, lateral plane x -> -x (both bodies face +Y), _l / _r bones swapped, ref-pose-delta method:
            delta = Q_clip * Q_ref^-1, Q(B') = reflect(delta_B) * Q_ref(B'), positions reflected, back to local, keys written with the
            AnimationDataController. Checked: the mirrored Idle's head / hand_r / hand_l lateral offsets = the negated originals of head /
            hand_l / hand_r (a mirror swaps the hands) within 2 cm (own FK of the written keys and the engine's evaluation).
            NOTE: a mirror swaps the hands, so the mirrored set holds the rifle LEFT-handed (grip in hand_l): a weapon attached to the
            hand_r socket does not line up while it plays - the C++ / ABP must move the weapon to the mirrored (left) grip, use hand IK,
            or accept it. Its AO is measured on the hand_l -> hand_r rifle axis.
  AIM       offset poses -> mesh-space rotation additive on their set's Aim idle (frame 0); one AimOffset per set (axes Yaw -180..180,
            Pitch -90..90 like AO_Rifle_Aim / the AO_Sniper_* of setup_operative_sniper_animation.py); samples at the MEASURED rifle-axis
            (hand_r -> hand_l) yaw / pitch of each pose relative to its Aim idle; the centre (0, 0) is a zero additive
            (<idle>_AOZero: a copy of the Aim idle, additive against itself, ABPT_ANIM_SCALED).

Writes (nothing in the pack folder; no skeleton / ABP / engine asset saved; /Game/Animations is untracked - designer content):
  /Game/Animations/CoverIndoor/{UE4,Pmc}/AS_Cover_*            one copy per pack clip, PACK NAME (e.g. AS_Cover_StandL_Idle)
  /Game/Animations/CoverIndoor/{UE4,Pmc}/AS_Cover_CrouchR_Aim2_*   the mirrored over-the-top set
  /Game/Animations/CoverIndoor/{UE4,Pmc}/AS_Cover_*_Aim*_Idle_AOZero   the zero centre samples
  /Game/Animations/CoverIndoor/{UE4,Pmc}/AO_CoverIndoor_{StandL,StandR,CrouchL,CrouchR,CrouchL_Over,CrouchR_Over}
  /Game/Animations/CoverIndoor/_Tmp/RTG_CoverIndoor_Pmc_TMP     the transient retargeter (deleted again at the end of the run)
  Saved/Logs/setup_indoor_cover_animation.txt + .json           measurements (lengths, walk speeds, peek step, grenade release,
                                                                _Start travel, AO ranges, mirror check, the M4 side mapping)

Run with every editor of the project closed (shared lock: Scripts/agent_lock.ps1):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash -nop4
CODEX_COVERINDOOR_DRYRUN=1: retarget in memory and measure only (nothing saved, no mirror / AO asset made).
CODEX_RESET_COVERINDOOR=1: redo existing copies (retarget again, bake, mirror). CODEX_COVERINDOOR_BODIES=UE4,Pmc (default both).
Re-runs are idempotent: existing copies are re-checked (a baked clip has nothing left to bake), the additive settings and AOs are refreshed.
"""
import json
import math
import os
import re

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
DRY = os.environ.get("CODEX_COVERINDOOR_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_COVERINDOOR") == "1"
BODIES = [b.strip() for b in os.environ.get("CODEX_COVERINDOOR_BODIES", "UE4,Pmc").split(",") if b.strip() in ("UE4", "Pmc")]
PACK = "/Game/Riffle_Cover_Animation/Animations"
PACK_MESH = "/Game/Riffle_Cover_Animation/Demo/Characters/Mannequins/Meshes/SKM_Manny_Simple"
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
TARGET_MESH = "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a"
PMC_MESH = "/Game/Characters/Enemies/PMC/Operator/SKM_PmcOperator"
PMC_SKELETON = "SK_PmcOperator_Skeleton"
IK_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Rigs/IK_Mannequin"
TEMPLATE_RTG_UE5 = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE4Manny_UE5Manny"
M4_IDLE_L = "/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_idle_L"
OUT = "/Game/Animations/CoverIndoor"
TMP = f"{OUT}/_Tmp"
TMP_RTG = "RTG_CoverIndoor_Pmc_TMP"
AO_YAW_RANGE, AO_PITCH_RANGE = 180.0, 90.0  # AO_Rifle_Aim / AO_Sniper_* axes
MIRROR_TOLERANCE_CM = 2.0
# AimOffset sets: name -> (Aim idle copy, offset prefix). The offsets are the copies named <prefix><two letters>.
AO_SETS = {
    "StandL": ("AS_Cover_StandL_Aim_Idle", "AS_Cover_StandL_Aim_"),
    "StandR": ("AS_Cover_StandR_Aim_Idle", "AS_Cover_StandR_Aim_"),
    "CrouchL": ("AS_Cover_CrouchL_Aim1_Idle", "AS_Cover_CrouchL_Aim1_"),
    "CrouchR": ("AS_Cover_CrouchR_Aim_Idle", "AS_Cover_CrouchR_Aim_"),
    "CrouchL_Over": ("AS_Cover_CrouchL_Aim2_Idle", "AS_Cover_CrouchL_Aim2_"),
    "CrouchR_Over": ("AS_Cover_CrouchR_Aim2_Idle", "AS_Cover_CrouchR_Aim2_"),
}
MIRROR_SOURCE_PREFIX, MIRROR_TARGET_PREFIX = "AS_Cover_CrouchL_Aim2_", "AS_Cover_CrouchR_Aim2_"
OFFSET_RE = re.compile(r"^AS_Cover_(Stand|Crouch)[LR]_Aim[12]?_([LRUDM]{2})$")
WORLD = unreal.AnimPoseSpaces.WORLD
LOCAL = unreal.AnimPoseSpaces.LOCAL
LOG_DIR = os.path.join(unreal.Paths.project_saved_dir(), "Logs")
JSON_PATH = os.path.join(LOG_DIR, "setup_indoor_cover_animation.json")
log = []


def say(text):
    log.append(text)
    unreal.log("setup_indoor_cover_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


# --- pure quaternion / vector math (x, y, z, w) - UE FQuat conventions: (A * B) applies B first, then A ---------------------------
def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qinv(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q)) or 1.0
    return tuple(c / n for c in q)


def qrot(q, v):
    x, y, z, w = q
    tx, ty, tz = 2.0 * (y * v[2] - z * v[1]), 2.0 * (z * v[0] - x * v[2]), 2.0 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx))


def qyaw(deg):
    h = math.radians(deg) * 0.5
    return (0.0, 0.0, math.sin(h), math.cos(h))


def yaw_of(q):
    f = qrot(q, (1.0, 0.0, 0.0))
    return math.degrees(math.atan2(f[1], f[0]))


def qangle(q):
    return math.degrees(2.0 * math.acos(min(1.0, abs(q[3]))))


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vlen(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def compose(local, parent):
    """Component-space transform of a child: local (q, t) under parent (q, t) (scale 1)."""
    return qnorm(qmul(parent[0], local[0])), tuple(p + r for p, r in zip(parent[1], qrot(parent[0], local[1])))


def relative(cs, parent):
    """Local transform of cs under parent (the inverse of compose)."""
    inv = qinv(parent[0])
    return qnorm(qmul(inv, cs[0])), qrot(inv, vsub(cs[1], parent[1]))


def reflect_q(q):
    return (q[0], -q[1], -q[2], q[3])  # mirror across the YZ plane (x -> -x)


def reflect_v(v):
    return (-v[0], v[1], v[2])


def wrap(deg):
    return (deg + 180.0) % 360.0 - 180.0


def to_q(r):
    return (r.x, r.y, r.z, r.w)


def to_v(t):
    return (t.x, t.y, t.z)


# --- skeleton / clip access -------------------------------------------------------------------------------------------------
class Rig:
    """Bone order, parents and the reference pose (local + component space) of a body."""

    def __init__(self, mesh):
        self.mesh = mesh
        self.skeleton = mesh.get_editor_property("skeleton")
        ref = unreal.AnimPoseExtensions.get_reference_pose(self.skeleton)
        self.bones = [str(b) for b in unreal.AnimPoseExtensions.get_bone_names(ref)]
        self.ref_local = {}
        world = {}
        for b in self.bones:
            t = unreal.AnimPoseExtensions.get_bone_pose(ref, b, LOCAL)
            self.ref_local[b] = (to_q(t.rotation), to_v(t.translation), to_v(t.scale3d))
            w = unreal.AnimPoseExtensions.get_bone_pose(ref, b, WORLD)
            world[b] = (to_q(w.rotation), to_v(w.translation))
        # the mesh hierarchy, walked down from the root with get_bone_children (get_bone_parent logs an error for a skeleton bone the
        # mesh does not have - the pack skeleton carries the full UE5 corrective bones, SKM_Manny_Simple does not)
        mesh_parent = {}
        todo = [self.bones[0]]
        while todo:
            b = todo.pop()
            for c in mesh.get_bone_children(b):
                c = str(c)
                if c not in mesh_parent:
                    mesh_parent[c] = b
                    todo.append(c)
        self.parent = {}
        self.guessed = []
        for i, b in enumerate(self.bones):
            if i == 0 or b in mesh_parent:
                self.parent[b] = mesh_parent.get(b) if i else None
                continue
            # a skeleton bone the mesh does not have: the earlier bone whose ref transform composes to this bone's ref transform
            local = self.ref_local[b][:2]
            best = min(self.bones[:i], key=lambda c: vlen(vsub(compose(local, world[c])[1], world[b][1]))
                       + qangle(qmul(compose(local, world[c])[0], qinv(world[b][0]))))
            self.parent[b] = best
            self.guessed.append(f"{b}<-{best}")
        self.ref_cs = self.fk({b: (q, t) for b, (q, t, _) in self.ref_local.items()})
        self.children = {b: [c for c in self.bones if self.parent[c] == b] for b in self.bones}
        self.root = self.bones[0]
        # root_inner (PMC body): a bone between root and pelvis - treated as part of the root by the in-place bake
        self.carriers = [self.root] + [c for c in self.children[self.root] if c == "root_inner"]

    def fk(self, local):
        cs = {}
        for b in self.bones:
            p = self.parent[b]
            cs[b] = local[b] if p is None else compose(local[b], cs[p])
        return cs

    def swap(self, bone):
        parts = bone.split("_")
        table = {"l": "r", "r": "l", "L": "R", "R": "L", "left": "right", "right": "left", "Left": "Right", "Right": "Left"}
        for i in range(len(parts) - 1, -1, -1):
            if parts[i] in table:
                other = "_".join(parts[:i] + [table[parts[i]]] + parts[i + 1:])
                return other if other in self.parent else bone
        return bone


def read_tracks(clip, rig):
    """Raw local keys of every tracked bone (+ the reference pose for untracked bones). Returns (tracked names, frames list of dicts)."""
    model = clip.get_editor_property("data_model_interface")
    tracked = [str(b) for b in model.get_bone_track_names()]
    tracked = [b for b in tracked if b in rig.parent]
    keys = model.get_number_of_keys()
    frames = []
    for f in range(keys):
        local = {b: (q, t) for b, (q, t, _) in rig.ref_local.items()}
        scale = {b: s for b, (_, _, s) in rig.ref_local.items()}
        if tracked:
            for b, tr in zip(tracked, unreal.AnimationLibrary.get_bone_poses_for_frame(clip, tracked, f, False)):
                local[b] = (to_q(tr.rotation), to_v(tr.translation))
                scale[b] = to_v(tr.scale3d)
        frames.append((local, scale))
    return tracked, frames


def write_tracks(clip, bones, frames, label):
    ctrl = clip.controller
    ctrl.open_bracket(unreal.Text(label), False)
    ctrl.remove_all_bone_tracks(False)
    for b in bones:
        ctrl.add_bone_track(b, False)
        pos = [unreal.Vector(*fr[0][b][1]) for fr in frames]
        rot = [unreal.Quat(*fr[0][b][0]) for fr in frames]
        scl = [unreal.Vector(*fr[1][b]) for fr in frames]
        ctrl.set_bone_track_keys(b, pos, rot, scl, False)
    ctrl.close_bracket(False)


def cs_frames(rig, frames):
    return [rig.fk(local) for local, _ in frames]


def length_of(clip):
    return clip.get_editor_property("sequence_length")


def classify(name):
    """'skip' / 'offset' / 'inplace' / 'oneshot' for a pack clip name (case-insensitive: the pack has 'AS_Cover_CrouchL_Walk_turn')."""
    n = name.lower()
    if "_dive" in n or "_dodge" in n:
        return "skip"
    if OFFSET_RE.match(name):
        return "offset"
    if n.endswith("_idle") or n.endswith("_walk") or re.search(r"_walk_(start|stop|end|turn)$", n):
        return "inplace"
    return "oneshot"


# --- bakes ------------------------------------------------------------------------------------------------------------------
def bake(clip, rig, in_place):
    """In place: root XY = frame 0 (Z kept), root yaw removed, the root's children keep their component-space pose minus the removed
    travel. One-shot: only a root rotation is folded (root translation kept). Returns a report dict."""
    tracked, frames = read_tracks(clip, rig)
    cs = cs_frames(rig, frames)
    root, carriers = rig.root, rig.carriers
    r0 = cs[0][root][1]
    travel = vsub(cs[-1][root][1], r0)
    yaws = [yaw_of(c[root][0]) for c in cs]
    max_yaw = max(abs(y) for y in yaws)
    max_shift = max(math.hypot(c[root][1][0] - r0[0], c[root][1][1] - r0[1]) for c in cs) if in_place else 0.0
    report = {"root_travel_cm": [round(v, 1) for v in travel], "root_travel_xy_cm": round(math.hypot(travel[0], travel[1]), 1),
              "root_yaw_deg": [round(yaws[0], 1), round(yaws[-1], 1)], "baked": False}
    report["max_root_yaw_deg"] = round(max_yaw, 1)
    if max_yaw < 0.5 and max_shift < 0.05:
        return report, tracked, frames  # nothing to bake (a re-run, or a clip that does not travel / turn)
    dependents = [b for b in rig.bones if rig.parent[b] in carriers and b not in carriers]
    out = []
    for f, (local, scale) in enumerate(frames):
        rq, rt = cs[f][root]
        new_root_q = qnorm(qmul(qyaw(-yaw_of(rq)), rq))
        new_root_t = (r0[0], r0[1], rt[2]) if in_place else rt
        shift = (rt[0] - r0[0], rt[1] - r0[1], 0.0) if in_place else (0.0, 0.0, 0.0)
        new_local = dict(local)
        new_local[root] = (new_root_q, new_root_t)
        new_cs = {root: (new_root_q, new_root_t)}
        for c in carriers[1:]:
            new_cs[c] = compose(local[c], new_cs[rig.parent[c]])  # root_inner keeps its local key
        for b in dependents:
            q, t = cs[f][b]
            new_local[b] = relative((q, vsub(t, shift)), new_cs[rig.parent[b]])
        out.append((new_local, scale))
    bones = list(tracked)
    for b in [root] + dependents:
        if b not in bones:
            bones.append(b)
    bones = [b for b in rig.bones if b in bones]  # parents first
    write_tracks(clip, bones, out, "indoor cover bake")  # a dry run edits the in-memory copy only (never saved): the measurements see it
    report["baked"] = True
    report["moved_yaw_deg"] = round(max_yaw, 1)
    report["removed_travel_cm"] = round(max_shift, 1)
    # seam check (loops): the pelvis component-space pose at the first vs the last key after the bake
    new_cs = cs_frames(rig, out)
    pel = "pelvis" if "pelvis" in rig.parent else dependents[0]
    report["seam_pelvis_cm"] = round(vlen(vsub(new_cs[-1][pel][1], new_cs[0][pel][1])), 2)
    report["seam_pelvis_deg"] = round(qangle(qmul(new_cs[-1][pel][0], qinv(new_cs[0][pel][0]))), 2)
    # the pose is unchanged apart from the removed travel: the head's component position vs the original minus the shift
    if "head" in rig.parent:
        err = 0.0
        for f in range(len(out)):
            shift = (cs[f][root][1][0] - r0[0], cs[f][root][1][1] - r0[1], 0.0) if in_place else (0.0, 0.0, 0.0)
            err = max(err, vlen(vsub(new_cs[f]["head"][1], vsub(cs[f]["head"][1], shift))))
        report["pose_error_cm"] = round(err, 3)
    return report, bones, out


def mirror_frames(rig, frames):
    """Mirror across the body's lateral plane (x -> -x) in component space with the ref-pose-delta method; returns new local frames."""
    out = []
    for local, scale in frames:
        cs = rig.fk(local)
        new_cs = {}
        for b in rig.bones:
            src = rig.swap(b)
            delta = qmul(cs[src][0], qinv(rig.ref_cs[src][0]))
            new_cs[b] = (qnorm(qmul(reflect_q(delta), rig.ref_cs[b][0])), reflect_v(cs[src][1]))
        new_local = {}
        new_scale = {}
        for b in rig.bones:
            p = rig.parent[b]
            new_local[b] = new_cs[b] if p is None else relative(new_cs[b], new_cs[p])
            new_scale[b] = scale[rig.swap(b)]
        out.append((new_local, new_scale))
    return out


def mirrored_name(name):
    tail = name[len(MIRROR_SOURCE_PREFIX):]
    if len(tail) == 2 and tail.isupper():
        tail = tail[0] + {"L": "R", "R": "L"}.get(tail[1], tail[1])
    return MIRROR_TARGET_PREFIX + tail


# --- measurements -----------------------------------------------------------------------------------------------------------
def rifle_line(cs, grip="hand_r", support="hand_l"):
    """Yaw / pitch of the rifle axis grip hand -> support hand (component space). A MIRRORED clip holds the rifle left-handed (the
    mirror swaps the hands), so its axis runs hand_l -> hand_r."""
    d = vsub(cs[support][1], cs[grip][1])
    return math.degrees(math.atan2(d[1], d[0])), math.degrees(math.asin(max(-1.0, min(1.0, d[2] / max(vlen(d), 0.01)))))


def facing_left(cs):
    """The body's own LEFT axis (horizontal) from the hands' side of the pelvis (the rifle is held in front)."""
    hands = [(cs["hand_r"][1][i] + cs["hand_l"][1][i]) * 0.5 for i in range(3)]
    f = vsub(hands, cs["pelvis"][1])
    n = math.hypot(f[0], f[1]) or 1.0
    fx, fy = f[0] / n, f[1] / n
    return (fy, -fx), (fx, fy)  # UE: +yaw turns right, right of (fx, fy) is (-fy, fx), so left is (fy, -fx)


def lateral_head(cs):
    left, forward = facing_left(cs)
    d = vsub(cs["head"][1], cs["pelvis"][1])
    return d[0] * left[0] + d[1] * left[1], forward


def evaluated(clip, t, bone):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("extract_root_motion", False)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, max(0.0, length_of(clip) - 0.001)), options)
    return to_v(unreal.AnimPoseExtensions.get_bone_pose(pose, bone, WORLD).translation)


def evaluated_cs(clip, t, bones):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("extract_root_motion", False)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, max(0.0, length_of(clip) - 0.001)), options)
    out = {}
    for b in bones:
        tr = unreal.AnimPoseExtensions.get_bone_pose(pose, b, WORLD)
        out[b] = (to_q(tr.rotation), to_v(tr.translation))
    return out


def grenade_release(rig, frames, length):
    """release_s = the peak of the right hand's speed TOWARDS the enemy (-Y: the side beyond the wall), 3-key smoothed - the forward
    swing of the throw (the overall speed peak can be the reach for the grenade or the arm's return). Also: the overall peak and the
    time the hand is furthest out from its first-frame position.
    TODO(indoor-cover): not reliable yet - the two bodies disagree on CrouchL (0.65 / 0.78 s) and StandR (1.38 / 0.72 s): the root turns up
    to 110 deg during the throws, so -Y is not the throw direction for the whole clip. Probe the per-frame hand path and pick the release
    against the clip (or add a notify by eye) before the game uses release_s."""
    cs = cs_frames(rig, frames)
    n = len(cs)
    dt = length / max(1, n - 1)
    start = cs[0]["hand_r"][1]
    speeds = [vlen(vsub(cs[f + 1]["hand_r"][1], cs[f]["hand_r"][1])) / dt for f in range(n - 1)]
    toward = [-(cs[f + 1]["hand_r"][1][1] - cs[f]["hand_r"][1][1]) / dt for f in range(n - 1)]
    smooth = [sum(toward[max(0, i - 1):i + 2]) / len(toward[max(0, i - 1):i + 2]) for i in range(len(toward))]
    peak = max(range(len(speeds)), key=lambda i: speeds[i]) if speeds else 0
    forward = max(range(len(smooth)), key=lambda i: smooth[i]) if smooth else 0
    reach = [vlen(vsub(c["hand_r"][1], start)) for c in cs]
    far = max(range(n), key=lambda i: reach[i])
    return {"release_s": round((forward + 0.5) * dt, 3), "forward_speed_cm_s": round(smooth[forward], 0) if smooth else 0.0,
            "peak_speed_s": round((peak + 0.5) * dt, 3), "peak_speed_cm_s": round(speeds[peak], 0) if speeds else 0.0,
            "furthest_out_s": round(far * dt, 3), "furthest_out_cm": round(reach[far], 1),
            "hand_r_at_release_cm": [round(v, 1) for v in cs[min(n - 1, forward + 1)]["hand_r"][1]]}


# --- retarget ---------------------------------------------------------------------------------------------------------------
def make_pmc_retargeter():
    """UE5 Manny (the pack) -> UE5 Manny (the PMC body), transient: RTG_UE4Manny_UE5Manny's op stack, IK_Mannequin on both sides."""
    template, ik = load(TEMPLATE_RTG_UE5), load(IK_UE5)
    if not template or not ik:
        return None
    if library.does_asset_exist(f"{TMP}/{TMP_RTG}"):
        library.delete_asset(f"{TMP}/{TMP_RTG}")
    rtg = tools.duplicate_asset(TMP_RTG, TMP, template)
    if not rtg:
        return None
    ctrl = unreal.IKRetargeterController.get_controller(rtg)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, ik)
    ctrl.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, ik)
    for side in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
        try:
            ctrl.assign_ik_rig_to_all_ops(side, ik)
        except Exception as error:  # noqa: BLE001 - older signature / source not per op
            say(f"assign_ik_rig_to_all_ops({side}): {error}")
    try:
        ctrl.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    except Exception as error:  # noqa: BLE001
        say(f"auto_map_chains: {error}")
    index = ctrl.get_index_of_op_by_name("Blend to Source")
    if index >= 0:
        ctrl.set_retarget_op_enabled(index, False)
    say(f"transient retargeter UE5 -> UE5 (IK_Mannequin both sides), ops {ctrl.get_num_retarget_ops()}, 'Blend to Source' off")
    return rtg


def rekey(clip, rig):
    """Re-key the clip from its own frames (drops the key data an IK retarget leaves behind, as in setup_door_animation.py)."""
    tracked, frames = read_tracks(clip, rig)
    write_tracks(clip, tracked, frames, "indoor cover rekey")


def registry_skeleton(path):
    name = path.rsplit("/", 1)[-1]
    data = registry.get_asset_by_object_path(f"{path}.{name}")  # the string overload (deprecated, as in setup_door_animation.py)
    return str(data.get_tag_value("Skeleton") or "") if data.is_valid() else None


def retarget_body(body, pack_datas, source_mesh, mesh, retargeter):
    dest = f"{OUT}/{body}"
    want_skeleton = mesh.get_editor_property("skeleton").get_name()
    copies, todo = {}, []
    for name, data in pack_datas.items():
        path = f"{dest}/{name}"
        tag = registry_skeleton(path)
        if tag is not None and want_skeleton in tag and not RESET:
            copies[name] = None  # loaded below
        else:
            if tag is not None and want_skeleton not in tag:
                say(f"{body} {name}: on {tag.rsplit('.', 1)[-1]} - redone by the retarget")
            todo.append(name)
    retargeted = set()
    if todo and retargeter:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget([pack_datas[n] for n in todo], source_mesh, mesh, retargeter,
                                                                        target_path=dest, include_referenced_assets=False,
                                                                        overwrite_existing_files=True)
        for d in result:
            asset = d.get_asset()
            if not isinstance(asset, unreal.AnimSequence):
                continue
            name = asset.get_name()
            path = asset.get_path_name().split(".")[0]
            if path != f"{dest}/{name}":
                say(f"  {body} {name}: landed at {path} - moved to {dest}")
                if library.rename_asset(path, f"{dest}/{name}"):
                    asset = library.load_asset(f"{dest}/{name}")
            copies[name] = asset
            retargeted.add(name)
        say(f"{body}: retargeted {len(retargeted)} / {len(todo)} clip(s) onto {mesh.get_name()} -> {dest}{' (in memory)' if DRY else ''}")
        missing = [n for n in todo if n not in retargeted]
        if missing:
            say(f"  {body}: the retarget gave no clip for {missing}")
    for name in list(copies):
        if copies[name] is None:
            copies[name] = library.load_asset(f"{dest}/{name}")
        if copies[name] is None:
            del copies[name]
    return copies, retargeted


# --- AimOffset -----------------------------------------------------------------------------------------------------------------
def library_function(name):
    wanted = name.replace("_", "").lower()
    return next((getattr(unreal.OperativeAnimGraphLibrary, n) for n in dir(unreal.OperativeAnimGraphLibrary)
                 if n.replace("_", "").lower() == wanted), None)


def fill_aim_offset(ao, zero, samples):
    """samples: [(clip, yaw, pitch)] at their measured angles; zero = the (0, 0) centre. Returns (mode, report string).

    Python CANNOT write free-position blend-space samples: SampleData / BlendParameters are readable and settable as editor properties,
    but the runtime triangulation is only rebuilt by UBlendSpace::ResampleData(), which no Python / Blueprint API calls
    (UBlendSpace::PostEditChangeProperty does not resample - engine BlendSpace.cpp; an AO with stale data outputs the reference pose).
    TODO(indoor-cover): add a C++ editor helper UOperativeAnimGraphLibrary::FillAimOffsetSamples(UBlendSpace*, const TArray<UAnimSequence*>&,
    const TArray<FVector>&, float YawRange, float PitchRange, FString& OutReport) (FillAimOffset2D with free sample positions: set the axes,
    delete the samples, AddSample each, ValidateSampleData + ResampleData + PostEditChange). This script uses it as soon as it exists.
    Until then the existing FillAimOffset2D builds a CROSS (centre + the measured middle-row L / R and middle-column U / D poses at
    the mean measured angle of each pair); the corner poses are left out and the JSON keeps every measured sample for the helper."""
    exact = library_function("fill_aim_offset_samples")
    if exact is not None:
        clips = [zero] + [c for c, _, _ in samples]
        values = [unreal.Vector(0.0, 0.0, 0.0)] + [unreal.Vector(y, p, 0.0) for _, y, p in samples]
        result = exact(ao, clips, values, AO_YAW_RANGE, AO_PITCH_RANGE)
        return "exact", str(result[-1] if isinstance(result, tuple) else result)
    cross = library_function("fill_aim_offset2d")

    def letters(clip):
        # the pack codes: <yaw><pitch> (Aim_LU, Aim1_RM) or, over the top, <pitch><yaw> (Aim2_UL, Aim2_DM)
        code = clip.get_name()[-2:]
        return (code[1], code[0]) if code[0] in "UD" else (code[0], code[1])

    def pick(yaw_letter, pitch_letter):
        return next(((c, y, p) for c, y, p in samples if letters(c) == (yaw_letter, pitch_letter)), None)

    left, right, up, down = pick("L", "M"), pick("R", "M"), pick("M", "U"), pick("M", "D")
    yaws = [abs(s[1]) for s in (left, right) if s]
    pitches = [abs(s[2]) for s in (up, down) if s]
    sample_yaw = sum(yaws) / len(yaws) if yaws else 10.0
    sample_pitch = sum(pitches) / len(pitches) if pitches else 10.0
    result = cross(ao, zero, left[0] if left else None, right[0] if right else None, up[0] if up else None, down[0] if down else None,
                   AO_YAW_RANGE, AO_PITCH_RANGE, sample_yaw, sample_pitch)
    used = {k: (s[0].get_name(), s[1], s[2]) for k, s in (("L", left), ("R", right), ("U", up), ("D", down)) if s}
    dropped = sorted(c.get_name() for c, _, _ in samples if all(c is not s[0] for s in (left, right, up, down) if s))
    return "cross", f"{result[-1] if isinstance(result, tuple) else result}; used {used}; corners left out (TODO helper): {dropped}"


# =============================================================================================================================
previous = {}
if os.path.exists(JSON_PATH):
    try:
        with open(JSON_PATH, encoding="utf-8") as handle:
            previous = json.load(handle)
    except Exception:  # noqa: BLE001
        previous = {}
report = {"dry_run": DRY, "bodies": {}, "pack": {}}

pack_assets = [d for d in registry.get_assets_by_path(PACK, recursive=True) if str(d.asset_class_path.asset_name) == "AnimSequence"]
pack_datas = {}
skipped = []
for d in sorted(pack_assets, key=lambda d: str(d.asset_name)):
    name = str(d.asset_name)
    if classify(name) == "skip":
        skipped.append(name)
    else:
        pack_datas[name] = d
say(f"pack: {len(pack_assets)} sequences, {len(pack_datas)} used, {len(skipped)} skipped (Dive / Dodge)")
source_mesh = load(PACK_MESH)
pack_rig = Rig(source_mesh) if source_mesh else None

# Pack-side measurements (the original travel / lengths, independent of the copies)
pack_clips = {}
for name, d in pack_datas.items():
    clip = d.get_asset()
    pack_clips[name] = clip
    if pack_rig is None:
        continue
    _, frames = read_tracks(clip, pack_rig)
    cs = cs_frames(pack_rig, frames)
    travel = vsub(cs[-1][pack_rig.root][1], cs[0][pack_rig.root][1])
    report["pack"][name] = {"kind": classify(name), "length_s": round(length_of(clip), 3), "keys": len(frames),
                            "root_travel_cm": round(math.hypot(travel[0], travel[1]), 1),
                            "root_yaw_deg": [round(yaw_of(cs[0][pack_rig.root][0]), 1), round(yaw_of(cs[-1][pack_rig.root][0]), 1)]}

# Side mapping vs the M4 pack (pack L = the operative's own LEFT; M4 _L said to be his own RIGHT)
side = {}
m4 = load(M4_IDLE_L)
pack_idle = pack_clips.get("AS_Cover_StandL_Idle")
if m4 and pack_idle:
    bones = ["pelvis", "head", "hand_r", "hand_l"]
    a, b = evaluated_cs(pack_idle, 0.0, bones), evaluated_cs(m4, 0.0, bones)
    la, fa = lateral_head(a)
    lb, fb = lateral_head(b)
    side = {"pack_StandL_head_x_cm": round(a["head"][1][0], 1), "pack_StandL_head_left_cm": round(la, 1),
            "pack_forward": [round(fa[0], 2), round(fa[1], 2)],
            "m4_idle_L_head_x_cm": round(b["head"][1][0], 1), "m4_idle_L_head_left_cm": round(lb, 1),
            "m4_forward": [round(fb[0], 2), round(fb[1], 2)]}
    # both packs face +Y in component space (the operative's own left = +X); the hands-based facing is logged as a cross-check
    report["pack_L_matches_m4"] = "L" if (a["head"][1][0] > 0) == (b["head"][1][0] > 0) else "R"
    side["agrees_with_hands_facing"] = ((la > 0) == (lb > 0)) == (report["pack_L_matches_m4"] == "L")
    say(f"side mapping: pack StandL idle head x {a['head'][1][0]:+.0f} cm, M4 idle_L head x {b['head'][1][0]:+.0f} cm (component, +X = own "
        f"left) -> pack_L_matches_m4 = {report['pack_L_matches_m4']}; hands-facing cross-check: pack head {la:+.0f} cm to his left (facing {fa[0]:.2f},{fa[1]:.2f}); M4 idle_L head {lb:+.0f} cm to his "
        f"left (facing {fb[0]:.2f},{fb[1]:.2f})")
report["side_check"] = side

bodies = {}
if "UE4" in BODIES:
    bodies["UE4"] = load(TARGET_MESH)
if "Pmc" in BODIES:
    bodies["Pmc"] = load(PMC_MESH)
pmc_retargeter = None
for body, mesh in bodies.items():
    if mesh is None:
        continue
    rig = Rig(mesh)
    say(f"== {body}: {mesh.get_name()} skeleton {rig.skeleton.get_name()}, {len(rig.bones)} bones, root '{rig.root}', carriers {rig.carriers}"
        + (f", parents found from the ref pose (not in the mesh): {rig.guessed}" if rig.guessed else ""))
    if body == "UE4":
        retargeter = load(RETARGETER)
        if retargeter:
            controller = unreal.IKRetargeterController.get_controller(retargeter)
            index = controller.get_index_of_op_by_name("Blend to Source")
            if index >= 0:
                controller.set_retarget_op_enabled(index, False)
                say("RTG_UE5Manny_UE4Manny: 'Blend to Source' off in memory for this batch (asset not saved)")
    else:
        pmc_retargeter = pmc_retargeter or make_pmc_retargeter()
        retargeter = pmc_retargeter
    copies, fresh = retarget_body(body, pack_datas, source_mesh, mesh, retargeter)
    old_body = previous.get("bodies", {}).get(body, {}).get("clips", {})
    rep = {"clips": {}, "count": 0}
    dirty = set()
    for name in sorted(copies):
        clip = copies[name]
        kind = classify(name)
        if name in fresh and body == "Pmc":
            rekey(clip, rig)
        clip.set_editor_property("enable_root_motion", False)
        clip.set_editor_property("force_root_lock", False)  # one-shots show their mesh travel; in-place clips have none
        bake_report, _, _ = bake(clip, rig, in_place=kind in ("inplace", "offset"))
        entry = {"kind": kind, "length_s": round(length_of(clip), 3), "fresh": name in fresh}
        entry.update(bake_report)
        if bake_report["baked"]:
            entry["copy_travel_cm"] = bake_report["root_travel_xy_cm"]
        elif "copy_travel_cm" in old_body.get(name, {}):
            entry["copy_travel_cm"] = old_body[name]["copy_travel_cm"]  # baked by an earlier run: its travel is gone from the clip
        else:
            entry["copy_travel_cm"] = bake_report["root_travel_xy_cm"]
        entry["pack_travel_cm"] = report["pack"].get(name, {}).get("root_travel_cm")
        rep["clips"][name] = entry
        if bake_report["baked"] or name in fresh:
            dirty.add(name)
        if bake_report["baked"]:
            say(f"  {name} [{kind}]: baked - removed travel {bake_report.get('removed_travel_cm', 0)} cm, root yaw "
                f"{bake_report['root_yaw_deg']} folded; seam {bake_report.get('seam_pelvis_cm')} cm / {bake_report.get('seam_pelvis_deg')} deg, "
                f"pose error {bake_report.get('pose_error_cm')} cm")
    # --- mirror the over-the-top set (crouch L Aim2 -> crouch R Aim2) ---
    mirror_report = {}
    for name in sorted(copies):
        if not name.startswith(MIRROR_SOURCE_PREFIX):
            continue
        target = mirrored_name(name)
        path = f"{OUT}/{body}/{target}"
        exists = library.does_asset_exist(path)
        src = copies[name]
        tracked, frames = read_tracks(src, rig)
        mirrored = mirror_frames(rig, frames)
        bones = [b for b in rig.bones if b in set(tracked) | {rig.swap(b) for b in tracked}]
        if not DRY and (not exists or RESET or name in fresh):
            if exists:
                clip = library.load_asset(path)
            else:
                clip = tools.duplicate_asset(target, f"{OUT}/{body}", src)
            if clip is None:
                say(f"  MIRROR {target}: could not create")
                continue
            clip.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_NONE)
            clip.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_NONE)
            write_tracks(clip, bones, mirrored, "indoor cover mirror")
            clip.set_editor_property("enable_root_motion", False)
            clip.set_editor_property("force_root_lock", False)
            copies[target] = clip
            dirty.add(target)
            state = "made"
        elif exists:
            copies[target] = library.load_asset(path)
            state = "kept"
        else:
            state = "computed (dry run)"
        src_cs = cs_frames(rig, frames)
        mir_cs = cs_frames(rig, mirrored)
        if target in copies:
            _, written = read_tracks(copies[target], rig)
            mir_cs = cs_frames(rig, written)  # check what the asset holds
        err = 0.0
        for f in range(len(src_cs)):
            for a, b in (("head", "head"), ("hand_r", "hand_l"), ("hand_l", "hand_r"), ("pelvis", "pelvis")):
                err = max(err, abs(mir_cs[f][a][1][0] + src_cs[f][b][1][0]), abs(mir_cs[f][a][1][1] - src_cs[f][b][1][1]),
                          abs(mir_cs[f][a][1][2] - src_cs[f][b][1][2]))
        entry = {"from": name, "state": state, "max_error_cm": round(err, 3)}
        if name.endswith("_Idle"):
            f0s, f0m = src_cs[0], mir_cs[0]
            entry["idle_lateral_x_cm"] = {
                "head": [round(f0s["head"][1][0], 1), round(f0m["head"][1][0], 1)],
                "hand_r (mirrored) vs hand_l (orig)": [round(f0s["hand_l"][1][0], 1), round(f0m["hand_r"][1][0], 1)],
                "hand_l (mirrored) vs hand_r (orig)": [round(f0s["hand_r"][1][0], 1), round(f0m["hand_l"][1][0], 1)]}
            entry["pass"] = err <= MIRROR_TOLERANCE_CM
            if target in copies and not DRY:
                try:
                    ev = [evaluated(copies[target], 0.0, b)[0] for b in ("head", "hand_r", "hand_l")]
                    entry["engine_eval_x_cm"] = [round(v, 1) for v in ev]
                    expected = [-f0s["head"][1][0], -f0s["hand_l"][1][0], -f0s["hand_r"][1][0]]
                    entry["engine_matches_negated_original"] = all(abs(e - x) <= MIRROR_TOLERANCE_CM for e, x in zip(ev, expected))
                except Exception as error:  # noqa: BLE001
                    entry["engine_eval_x_cm"] = str(error)
        mirror_report[target] = entry
        say(f"  MIRROR {name} -> {target}: {state}, max lateral / height error {err:.3f} cm"
            + (f", idle x (orig -> mirrored) {entry['idle_lateral_x_cm']}" if "idle_lateral_x_cm" in entry else ""))
        if target in copies:
            rep["clips"].setdefault(target, {"kind": classify(target), "length_s": round(length_of(copies[target]), 3), "mirrored_from": name})
    rep["mirror"] = mirror_report
    # --- save the clips before they become additive bases / samples ---
    if not DRY:
        for name in sorted(dirty):
            library.save_loaded_asset(copies[name], False)
    # --- measurements on the copies ---
    walk = {}
    peek = {}
    grenade = {}
    start = {}
    for name in sorted(copies):
        if name not in rep["clips"]:
            continue
        clip = copies[name]
        n = name.lower()
        length = length_of(clip)
        entry = rep["clips"][name]
        if re.search(r"_walk(_start|_stop|_end|_turn)?$", n):
            pack_travel = entry.get("pack_travel_cm") or 0.0
            walk[name] = {"length_s": round(length, 3), "pack_travel_cm": pack_travel, "copy_travel_cm": entry.get("copy_travel_cm"),
                          "pack_speed_cm_s": round(pack_travel / max(length, 0.001), 1),
                          "copy_speed_cm_s": round((entry.get("copy_travel_cm") or 0.0) / max(length, 0.001), 1)}
        if re.search(r"_aim[12]?_start$", n):
            _, frames = read_tracks(clip, rig)
            cs = cs_frames(rig, frames)
            peek[name] = {b: [round(v, 1) for v in vsub(cs[-1][b][1], cs[0][b][1])] for b in ("pelvis", "head", "hand_l")}
            peek[name]["step_cm"] = round(math.hypot(*vsub(cs[-1]["pelvis"][1], cs[0]["pelvis"][1])[:2]), 1)
        if n.endswith("_grenade_throw"):
            _, frames = read_tracks(clip, rig)
            grenade[name] = grenade_release(rig, frames, length)
            grenade[name]["length_s"] = round(length, 3)
        if n.endswith("_start") and not re.search(r"_(aim[12]?|walk)_start$", n):
            _, frames = read_tracks(clip, rig)
            cs = cs_frames(rig, frames)
            first = {b: [round(v, 1) for v in vsub(cs[0][b][1], cs[-1][b][1])] for b in (rig.root, "pelvis")}
            travel = vsub(cs[-1][rig.root][1], cs[0][rig.root][1])
            start[name] = {"length_s": round(length, 3), "root_travel_cm": round(math.hypot(travel[0], travel[1]), 1),
                           "first_frame_offset_vs_end_cm": first}
    rep["walk"], rep["aim_start_peek"], rep["grenade"], rep["start"] = walk, peek, grenade, start
    # --- aim offsets ---
    aos = {}
    for set_name, (idle_name, prefix) in AO_SETS.items():
        base = copies.get(idle_name)
        if base is None:
            say(f"  AO {set_name}: no {idle_name}")
            continue
        _, base_frames = read_tracks(base, rig)
        hands = ("hand_l", "hand_r") if idle_name.startswith(MIRROR_TARGET_PREFIX) else ("hand_r", "hand_l")
        yaw0, pitch0 = rifle_line(rig.fk(base_frames[0][0]), *hands)
        poses = sorted(n for n in copies if n.startswith(prefix) and len(n) == len(prefix) + 2 and n[len(prefix):].isupper())
        samples = []
        for pname in poses:
            _, frames = read_tracks(copies[pname], rig)
            yaw, pitch = rifle_line(rig.fk(frames[0][0]), *hands)
            samples.append((pname, round(wrap(yaw - yaw0), 1), round(pitch - pitch0, 1)))
        zero_name = f"{idle_name}_AOZero"
        ao_name = f"AO_CoverIndoor_{set_name}"
        entry = {"base": idle_name, "base_rifle_yaw_pitch": [round(yaw0, 1), round(pitch0, 1)], "rifle_axis": f"{hands[0]} -> {hands[1]}",
                 "samples": {p: [y, pt] for p, y, pt in samples}}
        if samples:
            entry["yaw_range"] = [min(s[1] for s in samples), max(s[1] for s in samples)]
            entry["pitch_range"] = [min(s[2] for s in samples), max(s[2] for s in samples)]
        if not DRY:
            for pname, _, _ in samples:
                seq = copies[pname]
                seq.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
                seq.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
                seq.set_editor_property("ref_pose_seq", base)
                seq.set_editor_property("ref_frame_index", 0)
                library.save_loaded_asset(seq, False)
            zero_path = f"{OUT}/{body}/{zero_name}"
            if library.does_asset_exist(zero_path) and not RESET and idle_name not in dirty:
                zero = library.load_asset(zero_path)
            else:
                if library.does_asset_exist(zero_path):
                    zero = library.load_asset(zero_path)
                    tracked, frames = read_tracks(base, rig)
                    write_tracks(zero, tracked, frames, "indoor cover AO zero")
                else:
                    zero = tools.duplicate_asset(zero_name, f"{OUT}/{body}", base)
            zero.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
            zero.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_SCALED)
            zero.set_editor_property("ref_pose_seq", base)
            zero.set_editor_property("enable_root_motion", False)
            library.save_loaded_asset(zero, False)
            ao_path = f"{OUT}/{body}/{ao_name}"
            ao = library.load_asset(ao_path) if library.does_asset_exist(ao_path) else None
            if ao is None:
                factory = unreal.AimOffsetBlendSpaceFactoryNew()
                factory.set_editor_property("target_skeleton", rig.skeleton)
                ao = tools.create_asset(ao_name, f"{OUT}/{body}", unreal.AimOffsetBlendSpace, factory)
            try:
                entry["fill_mode"], entry["fill"] = fill_aim_offset(ao, zero, [(copies[p], y, pt) for p, y, pt in samples])
                stored = [(s.get_editor_property("animation").get_name() if s.get_editor_property("animation") else None,
                           round(s.get_editor_property("sample_value").x, 1), round(s.get_editor_property("sample_value").y, 1))
                          for s in ao.get_editor_property("sample_data")]
                entry["stored_samples"] = stored
                library.save_loaded_asset(ao, False)
            except Exception as error:  # noqa: BLE001
                entry["fill_mode"], entry["fill"] = "failed", f"FAILED: {error}"
                say(f"  AO {ao_name}: sample fill FAILED ({error})")
        aos[ao_name] = entry
        say(f"  AO {ao_name}: base {idle_name} (rifle yaw {yaw0:.0f} pitch {pitch0:.0f}), {len(samples)} poses "
            f"{entry['samples']}, yaw {entry.get('yaw_range')}, pitch {entry.get('pitch_range')}, {entry.get('fill', 'dry run')}")
    rep["aim_offsets"] = aos
    rep["clip_copies"] = len(copies)
    on_disk = [str(d.asset_name) for d in registry.get_assets_by_path(f"{OUT}/{body}", recursive=False)]
    rep["assets_in_folder"] = len(on_disk)
    rep["aim_offset_assets"] = sorted(n for n in on_disk if n.startswith("AO_CoverIndoor_"))
    report["bodies"][body] = rep
    say(f"{body}: {len(copies)} clip copies (incl. mirrored), {len(aos)} aim offsets; walk speeds "
        + ", ".join(f"{k.replace('AS_Cover_', '')} {v['copy_speed_cm_s']}" for k, v in walk.items() if k.lower().endswith("_walk")))
    for k, v in peek.items():
        say(f"  peek {k}: pelvis {v['pelvis']} head {v['head']} hand_l {v['hand_l']} (step {v['step_cm']} cm)")
    for k, v in grenade.items():
        say(f"  grenade {k}: release {v['release_s']} s (hand_r towards the enemy {v['forward_speed_cm_s']} cm/s), overall speed peak "
            f"{v['peak_speed_s']} s, furthest out {v['furthest_out_s']} s")
    for k, v in start.items():
        say(f"  start {k}: travel {v['root_travel_cm']} cm, first frame vs end {v['first_frame_offset_vs_end_cm']}")

if pmc_retargeter:
    library.delete_loaded_asset(pmc_retargeter)
    if library.does_directory_exist(TMP):
        library.delete_directory(TMP)
# key facts of the JSON, repeated in the txt
say("== summary")
say(f"pack_L_matches_m4: {report.get('pack_L_matches_m4')} {report.get('side_check')}")
for body, rep in report["bodies"].items():
    say(f"{body}: {rep['clip_copies']} clip copies, {rep['assets_in_folder']} assets in {OUT}/{body}, AOs {rep['aim_offset_assets']}")
    say(f"  walk loop speed cm/s (copy / pack): " + ", ".join(f"{k.replace('AS_Cover_', '')} {v['copy_speed_cm_s']} / {v['pack_speed_cm_s']}"
                                                         for k, v in rep["walk"].items() if k.lower().endswith("_walk")))
    say(f"  walk transitions original travel cm (copy): " + ", ".join(f"{k.replace('AS_Cover_', '')} {v['copy_travel_cm']}"
                                                                     for k, v in rep["walk"].items() if not k.lower().endswith("_walk")))
    say(f"  peek step cm: " + ", ".join(f"{k.replace('AS_Cover_', '')} {v['step_cm']}" for k, v in rep["aim_start_peek"].items()))
    say(f"  grenade release s: " + ", ".join(f"{k.replace('AS_Cover_', '')} {v['release_s']}" for k, v in rep["grenade"].items()))
    say(f"  _Start travel cm: " + ", ".join(f"{k.replace('AS_Cover_', '')} {v['root_travel_cm']}" for k, v in rep["start"].items()))
    for k, v in rep["aim_offsets"].items():
        say(f"  {k}: yaw {v.get('yaw_range')} pitch {v.get('pitch_range')} fill {v.get('fill_mode', 'dry run')}")
    for k, v in rep["mirror"].items():
        if "idle_lateral_x_cm" in v:
            say(f"  mirror {k}: {v['idle_lateral_x_cm']} max error {v['max_error_cm']} cm, pass {v.get('pass')}, engine x {v.get('engine_eval_x_cm')}")
say("DRY RUN: nothing saved" if DRY else "done")
os.makedirs(LOG_DIR, exist_ok=True)
with open(JSON_PATH, "w", encoding="utf-8") as handle:
    json.dump(report, handle, indent=2)
with open(os.path.join(LOG_DIR, "setup_indoor_cover_animation.txt"), "w", encoding="utf-8") as handle:
    handle.write("\n".join(log) + "\n")
