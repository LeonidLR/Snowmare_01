"""Interaction clips for the operatives (game designer 2026-10-15): the user's Polygonmaker pack
/Game/Polygonmaker/Animations/Interactions/UE4/ (user content - NOT modified):

  Anim_Regular_chest_open     -> AM_Interact_ChestOpen(_Upper)     an operative opens a loot crate, then the loot window opens
  Anim_Regular_chest_search   -> AM_Interact_ChestSearch(_Upper)   after Take / Take All in the loot window (cosmetic)
  Anim_Regular_pickup_ground  -> AM_Interact_PickupGround(_Upper)  after a successful mine / tripwire disarm
  Anim_Regular_idle_cold      -> played directly (UOperativeAnimInstance::ColdIdleVariants): an occasional variant inside the
                                 cold idle (weather idle layer), chance / cooldown / loops in Content/Data/Survival/tactical_heat.json

Skeleton check (read-only, logged): the clips sit on the pack's own UE4 mannequin skeleton
(/Game/Polygonmaker/Characters/Mannequin_UE4/Meshes/SK_Mannequin_Skeleton, 68 bones) - the same rig (bone names / parents) as the
operatives' skeletons (Post_Apo UE4_Mannequin_Skeleton = the same 68 + Backpack_01..03, Female_Soldier_Skeleton = the same 68).
UE 5.8 plays a montage / sequence of another skeleton by bone name (skeleton remapping; Montage_Play does not check the
compatible-skeleton list), like the RifleAnims / Crawl / M4 / Sniper packs. The log compares the reference poses (local rotation of
the key bones) and measures each clip evaluated on the pack mesh, SK_Post_Apo_Survivor_a and SK_Female_soldier (pelvis / hands /
head / lowest foot): no retarget is needed when they agree. No skeleton, no ABP, no pack asset is written.

Montages (Claude-owned, /Game/Characters/Operatives/Anims/Interactions/, claimed in docs/port/TANDEM.md):
  AM_Interact_<Name>        slot FullBody (the ABP's full-body slot): standing operatives
  AM_Interact_<Name>_Upper  slot = ABP_Operative's UpperBodySlot (the graph's layered blend from spine_01): crouched operatives -
                            the legs keep the crouch pose (prone: no clip for now)
  blend in 0.2 s / out 0.25 s, auto blend-out on. UOperativeAnimInstance loads them by path when its fields are empty (no ABP edit).
PickupGround: the time of the lowest pelvis (the hands at the ground) is logged - the full-body pickup after a crouched disarm starts
there (UOperativeAnimInstance::PickupGround.FullBodyStartSeconds default, keep them in sync when the clip changes).

Idempotent: existing montages are kept (CODEX_RESET_INTERACTION_CLIPS=1 recreates them).
Run with no editor of THIS checkout open (the pack is read through the content junction):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
Result: Saved/Logs/setup_operative_interaction_animation.txt
"""
import math
import os

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
RESET = os.environ.get("CODEX_RESET_INTERACTION_CLIPS") == "1"
PACK = "/Game/Polygonmaker/Animations/Interactions/UE4/"
PACK_MESH = "/Game/Polygonmaker/Characters/Mannequin_UE4/Meshes/SK_Mannequin"
DEST = "/Game/Characters/Operatives/Anims/Interactions"
POSTAPO_MESH = "/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a"
FEMALE_MESH = "/Game/Female_Soldier/Mesh/SK_Female_soldier"
POSTAPO_SKELETON = "/Game/Post_Apo_Survivor/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton"
FEMALE_SKELETON = "/Game/Female_Soldier/Mesh/Female_Soldier_Skeleton"
ABP_CLASS = "/Game/Characters/Operatives/ABP_Operative.ABP_Operative_C"
MONTAGES = {"ChestOpen": "Anim_Regular_chest_open", "ChestSearch": "Anim_Regular_chest_search", "PickupGround": "Anim_Regular_pickup_ground"}
VARIANT = "Anim_Regular_idle_cold"
FULL_BODY_SLOT = "FullBody"
BLEND_IN = 0.2
BLEND_OUT = 0.25
LOCAL = unreal.AnimPoseSpaces.LOCAL
WORLD = unreal.AnimPoseSpaces.WORLD
KEY_BONES = ["pelvis", "spine_01", "spine_02", "spine_03", "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "clavicle_r", "upperarm_r",
             "neck_01", "head", "thigh_l", "calf_l", "foot_l"]
log = []


def say(text):
    log.append(text)
    unreal.log("setup_operative_interaction_animation: " + text)


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        say(f"MISSING {path}")
    return asset


def bone_names(skeleton):
    return [str(n) for n in skeleton.get_reference_pose().get_bone_names()]


def ref_local(skeleton):
    pose = skeleton.get_reference_pose()
    names = set(str(n) for n in pose.get_bone_names())
    return {b: pose.get_ref_bone_pose(b, LOCAL) for b in KEY_BONES if b in names}


def quat_angle(a, b):
    return math.degrees(2.0 * math.acos(min(1.0, abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))))


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(t, clip.get_editor_property("sequence_length") - 0.001), options)


def world(pose, bone):
    return unreal.AnimPoseExtensions.get_bone_pose(pose, bone, WORLD).translation


def fmt(v):
    return f"({v.x:.0f},{v.y:.0f},{v.z:.0f})"


# --- 1. Skeletons (read-only) -----------------------------------------------------------------------------------------------------
clips = {key: load(PACK + name) for key, name in list(MONTAGES.items()) + [("IdleCold", VARIANT)]}
clip_skeleton = next((c.get_editor_property("skeleton") for c in clips.values() if c), None)
postapo_skeleton = load(POSTAPO_SKELETON)
female_skeleton = load(FEMALE_SKELETON)
if clip_skeleton:
    pack_names = bone_names(clip_skeleton)
    say(f"clip skeleton {clip_skeleton.get_path_name()} ({len(pack_names)} bones)")
    pack_ref = ref_local(clip_skeleton)
    for skeleton in (postapo_skeleton, female_skeleton):
        if not skeleton:
            continue
        names = bone_names(skeleton)
        compatible = [c.get_path_name() for c in skeleton.get_editor_property("compatible_skeletons") if c]
        say(f"{skeleton.get_name()}: {len(names)} bones, common {len(set(names) & set(pack_names))}, only in the clips "
            f"{sorted(set(pack_names) - set(names))}, only on the body {sorted(set(names) - set(pack_names))}; clip skeleton in its "
            f"compatible list: {any(clip_skeleton.get_path_name() == c for c in compatible)} (not needed for playback in 5.8)")
        other = ref_local(skeleton)
        say(f"  ref pose clip skeleton vs {skeleton.get_name()} (local rotation diff, deg): " + ", ".join(
            f"{b} {quat_angle(pack_ref[b].rotation, other[b].rotation):.0f}" for b in KEY_BONES if b in pack_ref and b in other))

# --- 2. Clips evaluated on the pack mesh and on the operative bodies ------------------------------------------------------------
meshes = {"pack": load(PACK_MESH), "postapo": load(POSTAPO_MESH), "female": load(FEMALE_MESH)}
pickup_low_time = None
for key, clip in clips.items():
    if not clip:
        continue
    length = clip.get_editor_property("sequence_length")
    say(f"{key} = {clip.get_name()}: {length:.2f} s, root motion {clip.get_editor_property('enable_root_motion')}, "
        f"skeleton {clip.get_editor_property('skeleton').get_name()}")
    for tag, mesh in meshes.items():
        if not mesh:
            continue
        steps = max(1, int(round(length / 0.1)))
        samples = []
        for i in range(steps + 1):
            t = min(length, i * 0.1)
            pose = pose_at(clip, t, mesh)
            samples.append((t, world(pose, "pelvis"), world(pose, "hand_l"), world(pose, "hand_r"), world(pose, "head"),
                            min(world(pose, "foot_l").z, world(pose, "foot_r").z)))
        low = min(samples, key=lambda s: s[1].z)
        hands_low = min(samples, key=lambda s: min(s[2].z, s[3].z))
        say(f"  on {tag}: pelvis start {fmt(samples[0][1])} lowest {low[1].z:.0f} at {low[0]:.1f} s end {fmt(samples[-1][1])}; hands lowest "
            f"{min(hands_low[2].z, hands_low[3].z):.0f} at {hands_low[0]:.1f} s; head start {fmt(samples[0][4])}; lowest foot "
            f"{min(s[5] for s in samples):.0f}; pelvis z every 0.2 s: " + " ".join(f"{s[1].z:.0f}" for s in samples[::2]))
        if key == "PickupGround" and tag == "postapo":
            pickup_low_time = low[0]
if pickup_low_time is not None:
    say(f"PickupGround lowest pelvis on SK_Post_Apo_Survivor_a at {pickup_low_time:.1f} s -> FullBodyStartSeconds default "
        f"(the rise after a crouched disarm starts at the low pose)")

# --- 3. Montages ----------------------------------------------------------------------------------------------------------------
upper_slot = "DefaultSlot"
try:
    anim_cdo = unreal.get_default_object(unreal.load_object(None, ABP_CLASS))
    upper_slot = str(anim_cdo.get_editor_property("upper_body_slot"))
    say(f"ABP_Operative UpperBodySlot '{upper_slot}', FullBodySlot '{anim_cdo.get_editor_property('full_body_slot')}' (read only)")
except Exception as error:  # noqa: BLE001
    say(f"ABP_Operative read failed ({error}); upper-body slot '{upper_slot}'")


def make_montage(name, clip, slot):
    path = f"{DEST}/{name}"
    if library.does_asset_exist(path) and RESET:
        library.delete_asset(path)
    if library.does_asset_exist(path):
        montage = library.load_asset(path)
        state = "kept"
    else:
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", clip)
        montage = tools.create_asset(name, DEST, unreal.AnimMontage, factory)
        state = "created"
    unreal.OperativeAnimGraphLibrary.set_montage_slot(montage, slot)
    slot_ok = all(str(t.get_editor_property("slot_name")) == slot for t in montage.get_editor_property("slot_anim_tracks"))
    montage.set_editor_property("enable_auto_blend_out", True)
    blend_in = montage.get_editor_property("blend_in")
    blend_in.set_editor_property("blend_time", BLEND_IN)
    montage.set_editor_property("blend_in", blend_in)
    blend_out = montage.get_editor_property("blend_out")
    blend_out.set_editor_property("blend_time", BLEND_OUT)
    montage.set_editor_property("blend_out", blend_out)
    library.save_loaded_asset(montage)
    say(f"{name}: {state}, slot {slot if slot_ok else 'NOT SET'}, length {montage.get_editor_property('sequence_length'):.2f} s, "
        f"skeleton {montage.get_editor_property('skeleton').get_name()}")


for key, source in MONTAGES.items():
    clip = clips.get(key)
    if not clip:
        continue
    make_montage(f"AM_Interact_{key}", clip, FULL_BODY_SLOT)
    make_montage(f"AM_Interact_{key}_Upper", clip, upper_slot)

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_interaction_animation.txt")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as handle:
    handle.write("\n".join(log) + "\n")
