"""Weather idle clips for the operatives (user request 2026-10-12): /Game/90+_Weather_Animations NEW_version/Cold.

Sets (UOperativeAnimInstance FWarmIdleClipSet fields, per gender: male = AS_He_*, female = AS_She_*; recruits use the male set):
  ColdIdleMale / ColdIdleFemale   <- Cold_1       standing, shivering (idle while cold, exploration only)
  HandWarmMale / HandWarmFemale   <- Cold_Fire_1  standing, hands held out to a fire (barrel; the outer ring at a campfire / stove)
  SitWarmMale  / SitWarmFemale    <- Cold_Fire_2  sitting on the ground at a fire (the inner seats at a campfire / stove)
Which Cold_Fire set stands and which sits is MEASURED (pelvis height of the loops: He 92 / 13 cm, She 95 / 35 cm), not assumed.
FWarmIdleClipSet = {Start, LoopA, TransitAB, LoopB, TransitBA, End} from the pack's 1Start, 2Loop, 3Tranzit, 4Loop (She Cold_Fire_*:
5Loop), 5Tranzit, 6End (She Cold_1 has no 6End: the layer blends out instead).

Retarget (why not bone-name playback): the pack is on its own UE5 Manny skeleton (Demo/Characters/Mannequins, 161 bones). Played by
bone name on the operative's UE4 Post_Apo skeleton (like the cover / sniper clips) the reference poses differ (clavicle / upper arm
~24 deg, spine_01..03 7-11 deg local rotation): measured 2026-10-12, the sitting loop leans the torso ~40 cm BACK (head y +1 -> -41 cm)
and lifts the hands to the chest (z 53 -> 93 cm) instead of holding them out to the fire. The clips are therefore IK-retargeted
with the M4 pack's RTG_UE5Manny_UE4Manny (IK_Mannequin -> IK_UE4_Mannequin; its Blend to Source op off, see below) onto
  male:   SK_Post_Apo_Survivor_a (operative UE4 skeleton)  -> /Game/Characters/Operatives/Anims/Weather/Male/AS_He_*
  female: SK_Female_soldier (Female_Soldier_Skeleton)       -> /Game/Characters/Operatives/Anims/Weather/Female/AS_She_*
(source meshes: the pack's SKM_Manny / SKM_Quinn). No skeleton or pack asset is modified. The retargeted copies are the ones
the ABP uses; the log compares pack vs retargeted pose per clip (pelvis, head, hands, lowest foot).

ABP_Operative class defaults: the six sets (only empty fields; CODEX_RESET_WEATHER_CLIPS=1 re-retargets and overwrites). The ABP is
backed up first to Saved/Backups/weather_anim_<timestamp>/.

Run with the MAIN project's editor closed (the pack is read through the content junction):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
CODEX_WEATHER_DRYRUN=1: measure only (retargets in memory, nothing saved).
Result: Saved/Logs/setup_operative_weather_animation.txt
"""
import math
import os
import shutil
import time

import unreal

library = unreal.EditorAssetLibrary
DRY = os.environ.get("CODEX_WEATHER_DRYRUN") == "1"
RESET = os.environ.get("CODEX_RESET_WEATHER_CLIPS") == "1"
PACK = "/Game/90+_Weather_Animations/Animations/NEW_version/Cold/"
PACK_MESHES = "/Game/90+_Weather_Animations/Demo/Characters/Mannequins/Meshes/"
RETARGETER = "/Game/M4_Cover_Pack/Demo/Characters/Mannequin_UE4/Rigs/RTG_UE5Manny_UE4Manny"
OUT = {"He": "/Game/Characters/Operatives/Anims/Weather/Male", "She": "/Game/Characters/Operatives/Anims/Weather/Female"}
SOURCE_MESH = {"He": PACK_MESHES + "SKM_Manny", "She": PACK_MESHES + "SKM_Quinn"}
TARGET_MESH = {"He": "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a", "She": "/Game/Female_Soldier/Mesh/SK_Female_soldier"}
POSTAPO_SKELETON = "/Game/Post_Apo_Survivor/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton"
FEMALE_SKELETON = "/Game/Female_Soldier/Mesh/Female_Soldier_Skeleton"
ABP = "/Game/Characters/Operatives/ABP_Operative"
ABP_CLASS = ABP + ".ABP_Operative_C"
ROLES = {"cold": "Cold_1", "hand": "Cold_Fire_1", "sit": "Cold_Fire_2"}
STAMP = time.strftime("%Y%m%d_%H%M%S")
LOCAL = unreal.AnimPoseSpaces.LOCAL
WORLD = unreal.AnimPoseSpaces.WORLD
SLOTS = ("start", "loop_a", "transit_ab", "loop_b", "transit_ba", "end")
TAGS = {"1Start": "start", "2Loop": "loop_a", "3Tranzit": "transit_ab", "4Loop": "loop_b", "5Loop": "loop_b",
        "5Tranzit": "transit_ba", "6End": "end"}
log = []


def say(text):
    log.append(text)
    unreal.log("setup_operative_weather_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def backup(asset):
    package = asset.get_path_name().split(".")[0]
    relative = package.replace("/Game/", "Content/", 1) + ".uasset"
    source = os.path.join(unreal.SystemLibrary.get_project_directory(), relative)
    if not os.path.exists(source):
        say(f"backup: {source} not found")
        return
    destination = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "Backups", "weather_anim_" + STAMP, relative)
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    shutil.copy2(source, destination)
    say(f"backup {relative} -> Saved/Backups/weather_anim_{STAMP}/")


# --- Inventory ------------------------------------------------------------------------------------------------------
registry = unreal.AssetRegistryHelpers.get_asset_registry()
clips = {str(d.asset_name): d for d in registry.get_assets_by_path(PACK.rstrip("/"), recursive=True)}
say(f"{len(clips)} clips in {PACK}")
inventory = {"He": {}, "She": {}}
for name in sorted(clips):
    gender = "He" if name.startswith("AS_He_") else "She" if name.startswith("AS_She_") else None
    base, _, tag = name[len("AS_" + (gender or "") + "_"):].rpartition("_")
    if gender is None or tag not in TAGS:
        say(f"  unknown clip {name}")
        continue
    inventory[gender].setdefault(base, {})[TAGS[tag]] = name
for gender, sets in inventory.items():
    for base, slots in sets.items():
        say(f"{gender} {base}: " + ", ".join(f"{s}={slots.get(s, '-')}" for s in SLOTS))

# --- Reference poses (why the clips are retargeted) ----------------------------------------------------------------
KEY_BONES = ["pelvis", "spine_01", "spine_02", "spine_03", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "neck_01", "head",
             "thigh_l", "calf_l", "foot_l"]


def ref_local(skeleton):
    pose = skeleton.get_reference_pose()
    names = set(str(n) for n in pose.get_bone_names())
    return {b: pose.get_ref_bone_pose(b, LOCAL) for b in KEY_BONES if b in names}


def quat_angle(a, b):
    return math.degrees(2.0 * math.acos(min(1.0, abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))))


pack_skeleton = clips[next(iter(sorted(clips)))].get_asset().get_editor_property("skeleton") if clips else None
if pack_skeleton:
    say(f"pack skeleton {pack_skeleton.get_path_name()} ({len(pack_skeleton.get_reference_pose().get_bone_names())} bones)")
    pack_ref = ref_local(pack_skeleton)
    for path in (POSTAPO_SKELETON, FEMALE_SKELETON):
        skeleton = load(path)
        if skeleton:
            other = ref_local(skeleton)
            say(f"ref pose pack vs {skeleton.get_name()} (local rotation diff): " + ", ".join(
                f"{b} {quat_angle(pack_ref[b].rotation, other[b].rotation):.0f}" for b in KEY_BONES if b in pack_ref and b in other))


# --- Retarget -------------------------------------------------------------------------------------------------------
def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), options)


def sample(clip, mesh=None):
    """Mid-clip pelvis / head / hands, the lowest foot over 9 samples (component space, cm; on Mesh when given)."""
    length = clip.get_editor_property("sequence_length")
    mid = pose_at(clip, length * 0.5, mesh)
    g = lambda p, b: unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation  # noqa: E731
    poses = [pose_at(clip, length * i / 8.0, mesh) for i in range(9)]
    lowest = min(min(g(p, "foot_l").z, g(p, "foot_r").z) for p in poses)
    ref = lambda b: mid.get_ref_bone_pose(b, WORLD).translation  # noqa: E731
    extension = (g(mid, "pelvis").z - lowest) / max(1.0, ref("pelvis").z - ref("foot_l").z)
    return {"pelvis": g(mid, "pelvis"), "head": g(mid, "head"), "hand_l": g(mid, "hand_l"), "hand_r": g(mid, "hand_r"), "foot": lowest,
            "legs": extension}


def fmt(v):
    return f"({v.x:.0f},{v.y:.0f},{v.z:.0f})"


retargeter = load(RETARGETER)
# The M4 retargeter's "Blend to Source" op pins the arm / leg IK goals to the SOURCE's absolute positions: on the taller Post_Apo
# body (mesh pelvis 104.9 cm) and the Female Soldier (100.5) it bent the knees to ~85 % extension (the pack stands at 95-100 %)
# and lowered her pelvis 13 cm. Switched off IN MEMORY for this batch (the shared M4 asset is never saved): legs 96-98 %, feet on
# the mesh's reference ground (measured 2026-10-12; scripts' log lists the numbers per clip).
if retargeter:
    rtg_controller = unreal.IKRetargeterController.get_controller(retargeter)
    blend_index = rtg_controller.get_index_of_op_by_name("Blend to Source")
    if blend_index >= 0:
        rtg_controller.set_retarget_op_enabled(blend_index, False)
        say("RTG_UE5Manny_UE4Manny: op 'Blend to Source' disabled in memory for this batch (asset not saved)")
# Ground reference: the operative's normal standing idle (RifleAnims) on the same meshes - the retargeted standing clips should
# put the feet at about the same height, or the body visibly rises / sinks when the weather pose blends in.
_reg_idle = [d for d in registry.get_assets_by_path("/Game/RifleAnims", recursive=True)
             if str(d.asset_name) == "AS_Rifle_IdleAim_FwdCenter"]
if _reg_idle:
    for gender in ("He", "She"):
        ground = sample(_reg_idle[0].get_asset(), load(TARGET_MESH[gender]))
        say(f"ground reference AS_Rifle_IdleAim_FwdCenter on {TARGET_MESH[gender].split('/')[-1]}: pelvis {ground['pelvis'].z:.0f}, lowest foot {ground['foot']:.0f}")
retargeted = {}
worst = 0.0
for gender in ("He", "She"):
    source_mesh, target_mesh = load(SOURCE_MESH[gender]), load(TARGET_MESH[gender])
    wanted = [n for base in ROLES.values() for n in inventory[gender].get(base, {}).values()]
    todo = []
    for name in wanted:
        dest = f"{OUT[gender]}/{name}"
        if library.does_asset_exist(dest) and not RESET:
            retargeted[name] = library.load_asset(dest)
        else:
            todo.append(clips[name])
    if todo and retargeter and source_mesh and target_mesh:
        result = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            todo, source_mesh, target_mesh, retargeter, target_path=OUT[gender], include_referenced_assets=False,
            overwrite_existing_files=True)
        for data in result:
            asset = data.get_asset()
            if isinstance(asset, unreal.AnimSequence):
                retargeted[str(data.asset_name)] = asset
                if not DRY:
                    library.save_loaded_asset(asset, False)
        say(f"{gender}: retargeted {len(todo)} clips onto {target_mesh.get_name()} -> {OUT[gender]}{' (in memory)' if DRY else ''}")
    for name in wanted:
        new = retargeted.get(name)
        if not new:
            say(f"  {name}: NOT retargeted")
            continue
        a, b = sample(clips[name].get_asset()), sample(new, target_mesh)
        # Pose error: head and hands relative to the pelvis (posture), plus the floor contact of the feet.
        err = max((a[k] - a["pelvis"] - (b[k] - b["pelvis"])).length() for k in ("head", "hand_l", "hand_r"))
        worst = max(worst, err)
        say(f"  {name}: {new.get_editor_property('sequence_length'):.2f} s, root motion {new.get_editor_property('enable_root_motion')}; "
            f"pack pelvis {fmt(a['pelvis'])} head {fmt(a['head'])} hands {fmt(a['hand_l'])} {fmt(a['hand_r'])} foot {a['foot']:.0f} legs {a['legs']:.2f} | "
            f"retargeted pelvis {fmt(b['pelvis'])} head {fmt(b['head'])} hands {fmt(b['hand_l'])} {fmt(b['hand_r'])} foot {b['foot']:.0f} legs {b['legs']:.2f} "
            f"| posture error {err:.0f} cm")
say(f"worst posture error (head / hands vs pelvis, pack vs retargeted): {worst:.0f} cm")
for gender in ("He", "She"):
    for role, base in ROLES.items():
        loops = [retargeted[n] for s, n in inventory[gender].get(base, {}).items() if s in ("loop_a", "loop_b") and n in retargeted]
        if loops:
            z = sum(sample(c, load(TARGET_MESH[gender]))["pelvis"].z for c in loops) / len(loops)
            say(f"{gender} {role} ({base}): loop pelvis height {z:.0f} cm")


# --- ABP class defaults -------------------------------------------------------------------------------------------
abp = load(ABP)
if abp and hasattr(unreal, "WarmIdleClipSet"):
    cdo = unreal.get_default_object(unreal.load_object(None, ABP_CLASS))
    wrote = False
    for field, gender, role in (("cold_idle_male", "He", "cold"), ("cold_idle_female", "She", "cold"),
                                ("hand_warm_male", "He", "hand"), ("hand_warm_female", "She", "hand"),
                                ("sit_warm_male", "He", "sit"), ("sit_warm_female", "She", "sit")):
        current = cdo.get_editor_property(field)
        if current.get_editor_property("loop_a") and not RESET:
            say(f"ABP_Operative.{field} kept ({current.get_editor_property('loop_a').get_name()})")
            continue
        value = unreal.WarmIdleClipSet()
        names = []
        for slot in SLOTS:
            clip = retargeted.get(inventory[gender].get(ROLES[role], {}).get(slot, ""))
            value.set_editor_property(slot, clip)
            names.append(clip.get_name() if clip else "-")
        if not wrote and not DRY:
            backup(abp)
        cdo.set_editor_property(field, value)
        wrote = True
        say(f"ABP_Operative.{field} = {ROLES[role]}: {names}")
    if wrote and not DRY:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        if not library.save_loaded_asset(abp, False):
            say("ERROR: ABP_Operative save failed")
elif abp:
    say("unreal.WarmIdleClipSet missing: build the C++ module first (class defaults not written)")

say("DRY RUN: nothing saved" if DRY else "done")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_weather_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
