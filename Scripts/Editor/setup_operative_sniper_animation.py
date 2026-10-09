"""Female Soldier body for the Medic-Sapper + the sniper rifle set (user request 2026-10-09).

1. Skeletons: /Game/Female_Soldier/Mesh/Female_Soldier_Skeleton (UE4 mannequin, 68 bones, same names / parents as the operative's
   Post_Apo UE4_Mannequin_Skeleton minus Backpack_01..03) becomes compatible with the Post_Apo skeleton and with every skeleton
   the Post_Apo one already lists (RifleAnims, Crawl A / Rifle / Pistol, Characters Manny, M4 Manny 196 = cover / grenade /
   knockdown clips, Sniper_Animation SK_Mannequin); the Post_Apo skeleton lists the female one back. Pack assets (they live in
   the main checkout through the content junctions): run only while no editor has the MAIN project open.
2. Weapon: /Game/Data/Weapons/DA_Weapon_sniper_rifle (created when missing): id sniper_rifle, Handling = SniperRifle, HandMesh =
   the M16 model (stand-in until a real sniper model is assigned), the numbers of weapons_tuning.json "sniper_rifle" (the JSON
   stays the master copy, applied again at game start).
3. ABP_Operative class defaults (UOperativeAnimInstance Sniper* fields, [stand, knee, prone]) from /Game/Sniper_Animation:
   idle / aim start / aim end / fire / bolt (Bullet_Reload) / magazine reload / hit react; only empty fields
   (CODEX_RESET_SNIPER_CLIPS=1 overwrites). RifleAimOffset = AO_Rifle_Aim when empty.
4. Aim offsets: the pack's Offset_F / L / R / U / D poses are copied to /Game/Characters/Operatives/Sniper/AimOffsets as
   additive mesh-space rotation offsets (base = the F pose) and AO_Sniper_Stand / _Knee / _Prone are built (axes Yaw -180..180,
   Pitch -90..90 like AO_Rifle_Aim; samples at the measured pose angles). The AnimGraph is not touched (HANDOFF section 6).

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_sniper_animation.txt
"""
import json
import math
import os
import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PACK = "/Game/Sniper_Animation/Animation/"
FEMALE_SKELETON = "/Game/Female_Soldier/Mesh/Female_Soldier_Skeleton"
POSTAPO_SKELETON = "/Game/Post_Apo_Survivor/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton"
ABP = "/Game/Characters/Operatives/ABP_Operative"
WEAPON = "/Game/Data/Weapons/DA_Weapon_sniper_rifle"
M16_MESH = "/Game/Weapons/M16/m16_01/StaticMeshes/m16_01"
RIFLE_AO = "/Game/RifleAnims/Animations/AimOffsets/AO_Aim/NotUsed_AimOffset2D/AO_Rifle_Aim"
AO_DIR = "/Game/Characters/Operatives/Sniper/AimOffsets"
STANCES = ("Stand", "Knee", "Prone")
CLIPS = {
    "sniper_idle": ["Stand/Stand_Idle/AS_Stand_Aim_Idle", "Knee/Knee_Idle/AS_Knee_Aim_Idle", "Prone/Prone_Idle/AS_Prone_Aim_Idle"],
    "sniper_aim_start": ["Stand/Stand_Idle/AS_Stand_Aim_Start", "Knee/Knee_Idle/AS_Knee_Aim_Start", "Prone/Prone_Idle/AS_Prone_Aim_Idle_Start"],
    "sniper_aim_end": ["Stand/Stand_Idle/AS_Stand_Aim_End", "Knee/Knee_Idle/AS_Knee_Aim_End", "Prone/Prone_Idle/AS_Prone_Aim_Idle_End"],
    "sniper_fire": ["Stand/AS_Stand_Aim_Fire", "Knee/AS_Knee_Aim_Fire", "Prone/AS_Prone_Aim_Fire"],
    "sniper_bolt_cycle": ["Stand/Stand_Reaload/AS_Stand_Aim_Bullet_Reload", "Knee/Knee_Reload/AS_Knee_Aim_Bullet_Reload",
                          "Prone/Prone_Reload/AS_Prone_Aim_Bullet_Reload"],
    "sniper_reload": ["Stand/Stand_Reaload/AS_Stand_Aim_Magazine_Reload", "Knee/Knee_Reload/AS_Knee_Aim_Magazine_Reload",
                      "Prone/Prone_Reload/AS_Prone_Aim_Magazine_Reload"],
    "sniper_hit_react": ["Stand/AS_Stand_Aim_HitReact", "Knee/AS_Knee_Aim_HitReact", "Prone/AS_Prone_Aim_HitReact"],
}
OFFSETS = {"Stand": "Stand/Stand_Offset/AS_Stand_Aim_Offset_", "Knee": "Knee/Knee_Offset/AS_Knee_Aim_Offset_",
           "Prone": "Prone/Prone_Offset/AS_Prone_Aim_Offset_"}
log = []


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        log.append(f"MISSING {path}")
    return asset


def make_compatible(skeleton, other):
    if not skeleton or not other or skeleton.get_path_name() == other.get_path_name():
        return False
    compatible = list(skeleton.get_editor_property("compatible_skeletons"))
    if any(c and c.get_path_name() == other.get_path_name() for c in compatible):
        return False
    compatible.append(other)
    skeleton.set_editor_property("compatible_skeletons", compatible)
    log.append(f"{skeleton.get_name()} now compatible with {other.get_path_name()}")
    return True


# --- 1. Skeletons -------------------------------------------------------------------------------------------------
female = load(FEMALE_SKELETON)
postapo = load(POSTAPO_SKELETON)
if female and postapo:
    changed_female = make_compatible(female, postapo)
    for other in list(postapo.get_editor_property("compatible_skeletons")):
        changed_female = make_compatible(female, other) or changed_female
    if changed_female:
        library.save_loaded_asset(female)
    if make_compatible(postapo, female):
        library.save_loaded_asset(postapo)
    log.append("female compatible: " + ", ".join(c.get_name() for c in female.get_editor_property("compatible_skeletons") if c))

# --- 2. Weapon data asset -----------------------------------------------------------------------------------------
tuning_path = os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapons_tuning.json")
with open(tuning_path, encoding="utf-8") as f:
    tuning = json.load(f).get("weapons", {}).get("sniper_rifle", {})
weapon = load(WEAPON) if library.does_asset_exist(WEAPON) else None
if weapon is None:
    weapon = tools.create_asset("DA_Weapon_sniper_rifle", "/Game/Data/Weapons", unreal.WeaponDataAsset, unreal.DataAssetFactory())
    log.append("created " + WEAPON)
if weapon:
    weapon.set_editor_property("weapon_id", "sniper_rifle")
    weapon.set_editor_property("weapon_name", unreal.Text(tuning.get("name", "Sniper Rifle")))
    weapon.set_editor_property("handling", unreal.WeaponHandling.SNIPER_RIFLE)
    weapon.set_editor_property("damage_type", unreal.DamageType.KINETIC)
    if not weapon.get_editor_property("hand_mesh"):
        weapon.set_editor_property("hand_mesh", load(M16_MESH))
    for key, prop, scale in (("base_damage", "base_damage", 1.0), ("attack_range_m", "attack_range_cm", 100.0),
                             ("fire_rate", "fire_rate", 1.0), ("armor_penetration", "armor_penetration", 1.0),
                             ("reload_time", "reload_time", 1.0)):
        if key in tuning:
            weapon.set_editor_property(prop, float(tuning[key]) * scale)
    for key in ("max_clip_size", "default_reserve_ammo", "max_range_cells"):
        if key in tuning:
            weapon.set_editor_property(key, int(tuning[key]))
    for key in ("base_hit_chances", "distance_damage_multipliers"):
        if key in tuning:
            weapon.set_editor_property(key, [float(v) for v in tuning[key]])
    library.save_loaded_asset(weapon)
    log.append(f"{WEAPON}: damage {weapon.get_editor_property('base_damage')}, range {weapon.get_editor_property('attack_range_cm')} cm, "
               f"fire {weapon.get_editor_property('fire_rate')} s, clip {weapon.get_editor_property('max_clip_size')}, "
               f"reserve {weapon.get_editor_property('default_reserve_ammo')}, reload {weapon.get_editor_property('reload_time')} s")

# --- 4. Aim offsets (before the ABP so its fields can point at them) ----------------------------------------------
opts = unreal.AnimPoseEvaluationOptions()


def rifle_line(seq):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, 0.0, opts)
    r = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_r", unreal.AnimPoseSpaces.WORLD).translation
    left = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_l", unreal.AnimPoseSpaces.WORLD).translation
    d = left - r
    return math.degrees(math.atan2(d.y, d.x)), math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))


aim_offsets = []
for stance in STANCES:
    poses = {k: load(PACK + OFFSETS[stance] + k) for k in "FLRUD"}
    if not poses["F"]:
        aim_offsets.append(None)
        continue
    yaw_f, pitch_f = rifle_line(poses["F"])
    yaws = [abs(rifle_line(poses[k])[0] - yaw_f) for k in "LR" if poses[k]]
    pitches = [abs(rifle_line(poses[k])[1] - pitch_f) for k in "UD" if poses[k]]
    sample_yaw = max(10.0, min(90.0, round(sum(yaws) / max(1, len(yaws)))))
    sample_pitch = max(10.0, min(90.0, round(sum(pitches) / max(1, len(pitches)))))
    log.append(f"{stance} offsets: rifle line yaw L/R {['%.1f' % y for y in yaws]}, pitch U/D {['%.1f' % p for p in pitches]} "
               f"-> samples yaw +-{sample_yaw:.0f}, pitch +-{sample_pitch:.0f}")
    copies = {}
    for key, source in poses.items():
        if not source:
            continue
        dest = f"{AO_DIR}/AS_Sniper_{stance}_Offset_{key}"
        if not library.does_asset_exist(dest):
            library.duplicate_asset(source.get_path_name().split(".")[0], dest)
        copies[key] = library.load_asset(dest)
    base = copies["F"]
    for key, seq in copies.items():
        seq.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_ROTATION_OFFSET_MESH_SPACE)
        seq.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
        seq.set_editor_property("ref_pose_seq", poses["F"])
        seq.set_editor_property("ref_frame_index", 0)
        library.save_loaded_asset(seq)
    ao_path = f"{AO_DIR}/AO_Sniper_{stance}"
    ao = library.load_asset(ao_path) if library.does_asset_exist(ao_path) else None
    if ao is None:
        factory = unreal.AimOffsetBlendSpaceFactoryNew()
        factory.set_editor_property("target_skeleton", poses["F"].get_editor_property("skeleton"))
        ao = tools.create_asset(f"AO_Sniper_{stance}", AO_DIR, unreal.AimOffsetBlendSpace, factory)
    fill = next(getattr(unreal.OperativeAnimGraphLibrary, n) for n in dir(unreal.OperativeAnimGraphLibrary)
                if n.lower().replace("_", "") == "fillaimoffset2d")
    result = fill(ao, base, copies.get("L"), copies.get("R"), copies.get("U"), copies.get("D"), 180.0, 90.0, sample_yaw, sample_pitch)
    report = result[-1] if isinstance(result, tuple) else result  # (bool, out string) or the out string alone
    log.append(f"AO_Sniper_{stance}: {report}")
    library.save_loaded_asset(ao)
    aim_offsets.append(ao)

# --- 3. ABP_Operative class defaults ------------------------------------------------------------------------------
reset = os.environ.get("CODEX_RESET_SNIPER_CLIPS") == "1"
abp = load(ABP)
if abp:
    cdo = unreal.get_default_object(unreal.load_object(None, ABP + ".ABP_Operative_C"))
    for prop, paths in CLIPS.items():
        current = list(cdo.get_editor_property(prop))
        wanted = [load(PACK + p) for p in paths]
        if any(current) and not reset:
            log.append(f"ABP_Operative.{prop} kept: {[c.get_name() if c else '-' for c in current]}")
            continue
        cdo.set_editor_property(prop, wanted)
        log.append(f"ABP_Operative.{prop} = {[c.get_name() if c else '-' for c in wanted]} "
                   f"({', '.join('%.2f s' % c.get_editor_property('sequence_length') for c in wanted if c)})")
    current_ao = list(cdo.get_editor_property("sniper_aim_offsets"))
    if not any(current_ao) or reset:
        cdo.set_editor_property("sniper_aim_offsets", aim_offsets)
        log.append(f"ABP_Operative.sniper_aim_offsets = {[a.get_name() if a else '-' for a in aim_offsets]}")
    if not cdo.get_editor_property("rifle_aim_offset"):
        cdo.set_editor_property("rifle_aim_offset", load(RIFLE_AO))
        log.append("ABP_Operative.rifle_aim_offset = AO_Rifle_Aim")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp)

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_sniper_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
