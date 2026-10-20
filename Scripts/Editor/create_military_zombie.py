"""Creates the Military Zombie enemy (game designer request 2026-10-20) from the user's /Game/military_zombie_set2 pack.

Inspection (read-only, logged into Saved/Logs/create_military_zombie.txt; docs/systems/animation-setup.md "Military Zombie"):

  skeleton   /Game/military_zombie_set2/Meshes/base_mesh/SK_Mannequin - the pack's own copy of the Epic UE5 Manny skeleton
             (161 bones, the same names in the same order as /Game/Characters/Mannequins/Meshes/SK_Mannequin; no compatible
             skeletons listed). Every mesh (base_mesh/SKM_base_mesh_set2_1/2/3, mech_separate/*) and every clip of animation/ is on it;
             the meshes have no extra FBX root bone (unlike Cyberpunk_Explorer_A), so an ABP on that skeleton spawns on them.
  clips      all in place (no root motion, the root bone stays at the origin); the walks are slow shambles (~15-30 cm/s at rate 1),
             run_1 ~360, run_2 ~660 cm/s (a sprint); anim_transformation 20.5 s, anim_devour 24.5 s (kneels and feeds), anim_rage
             3.4 s, attacks 2.6-4.6 s, anim_Dying 3.3 s (falls forward ~1.6 m).
  variants   SKM_base_mesh_set2_1/2/3 are three ASSEMBLED bodies (1: helmet + kit + armor, 2: blouse + armor + hair, 3: bare body),
             one mesh each - the mech_separate parts are the same pieces split out (for custom assemblies; not needed here).
             material/set_1/2/3 are three texture sets of the same slots (MI_<part>_set_N_Inst; the helmet exists in set 1 only).
  Frostbitten  ABP_Enemy_Frostbitten targets /Game/mutant_monster_2/.../SK_UE4_Mannequin_Skeleton (68 bones; the zombie skeleton
             has those 68 names + 93 more and another hierarchy for spine / neck / fingers): not the same skeleton.

Route (user guidance 2026-10-20 "use the Frostbitten Animation Blueprint, just swap its animations"): the Frostbitten graph plays
only CLASS-DEFAULT clips (sequence players bound to IdleAnimation / WalkAnimation / RunAnimation, the one-shot slots driven by
UEnemyAnimInstance) and references no asset itself, so

  ABP_Enemy_MilitaryZombie   a CHILD Animation Blueprint of ABP_Enemy_Frostbitten (inherits its graph, slots, upper-body hit
                             layer and C++ logic; the user's ABP stays untouched) whose target skeleton is the pack's skeleton,
                             with the zombie's own clips on its class defaults (no retarget: they are native to that skeleton) and
                             the extras of UEnemyAnimInstance: idle variety (seeded idle loop + fidget breaks), walk / run variants,
                             attacks 1-4 at random, hit, death, rage = alert roar, devour = calm idle activity, transformation =
                             spawn entrance.
  BP_Enemy_MilitaryZombie    a CHILD of BP_Enemy_Frostbitten (archetype Frostbitten through the class mapping: stats, melee AI,
                             knockdown, cone), body SKM_base_mesh_set2_1, LookVariants = the 3 bodies x the 3 texture sets (picked
                             per instance by the spawn seed), capsule 40 / 90, mesh z -90 / yaw -90 (faces +Y, feet at its origin),
                             nameplate "Military Zombie", knockdown clips = /Game/Animations_KnockDown itself (the Epic Manny
                             originals: same bone names / hierarchy, played by name like on the operatives).

Idempotent: assets are created only when missing; an existing ABP / BP is left as it is (CODEX_MZ_REAPPLY=1 re-applies the class
defaults / Blueprint fields). Nothing in /Game/military_zombie_set2 is saved (the script reports any pack package it dirtied).

Run with this checkout's editor closed (commandlet):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
Result: Saved/Logs/create_military_zombie.txt
"""
import os

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
REAPPLY = os.environ.get("CODEX_MZ_REAPPLY") == "1"
WORLD = unreal.AnimPoseSpaces.WORLD

PACK = "/Game/military_zombie_set2"
CLIPS = PACK + "/animation/"
MESHES = PACK + "/Meshes/base_mesh/"
MATERIALS = PACK + "/material/"
SKELETON = MESHES + "SK_Mannequin"
BODY_MESHES = [MESHES + "SKM_base_mesh_set2_1", MESHES + "SKM_base_mesh_set2_2", MESHES + "SKM_base_mesh_set2_3"]
EPIC_MANNY = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
FROST_MESH = "/Game/mutant_monster_2/base_mesh/SK_base_mesh"
FROST_ABP = "/Game/Characters/Enemies/Frostbitten/ABP_Enemy_Frostbitten"
FROST_BP = "/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten"

ROOT = "/Game/Characters/Enemies/MilitaryZombie"
ABP = ROOT + "/ABP_Enemy_MilitaryZombie"
BP = ROOT + "/BP_Enemy_MilitaryZombie"
DISPLAY_NAME = "Military Zombie"

IDLES = ["anim_idle_1", "anim_idle_3", "anim_idle_4", "anim_idle_5", "anim_idle_6"]
# Ground speed at rate 1 (cm/s), measured from the planted foot's travel per stride (inspection, rounded).
WALKS = [("anim_walking_1", 20.0), ("anim_walking_2", 30.0), ("anim_walking_3", 15.0), ("anim_walking_4", 25.0)]
RUNS = [("anim_run_1", 360.0), ("anim_run_2", 660.0)]
ATTACKS = ["anim_attack_1", "anim_attack_2", "anim_attack_3", "anim_attack_4"]
# Class defaults beyond the clips. The attack / hit / run-threshold numbers are the Frostbitten's (same archetype, same timing:
# the zombie's attacks average 3.4 s, the Frostbitten's 3.6 s at the same rate 1.2).
ANIM_FIELDS = {
    "run_speed_threshold": 200.0,
    "attack_play_rate": 1.2,
    "hit_play_rate": 1.0,
    "death_play_rate": 1.0,
    "death_start_offset": 0.0,
    "death_decay_delay": 5.0,
    "seeded_idle_variant": True,
    "idle_activity_chance": 0.35,
    "idle_break_interval": unreal.Vector2D(6.0, 12.0),
    "idle_break_play_rate": 1.0,
    "idle_break_blend_seconds": 0.35,
    # The roar (3.4 s) a little faster: 2.8 s before it charges.
    "alert_play_rate": 1.2,
    "alert_max_seconds": 0.0,
    # The 20.5 s transformation: its last part (twitching, then the arms come up) from 14.5 s at x1.5 = a 4 s entrance.
    "entrance_play_rate": 1.5,
    "entrance_start_offset": 14.5,
    "entrance_max_seconds": 0.0,
    "upper_body_hit_reactions": True,
    "upper_body_slot": "UpperBody",
}
log = []
created = []


def say(text):
    log.append(text)
    unreal.log("create_military_zombie: " + text)


def asset_name(path):
    return path.rsplit("/", 1)[1]


def bones(skeleton):
    return [str(n) for n in skeleton.get_reference_pose().get_bone_names()]


def clip(name):
    asset = library.load_asset(CLIPS + name)
    if asset is None:
        raise RuntimeError("missing clip " + CLIPS + name)
    return asset


def inspect(skeleton):
    names = bones(skeleton)
    manny = library.load_asset(EPIC_MANNY)
    frost = library.load_asset(FROST_MESH).get_editor_property("skeleton")
    frost_names = bones(frost)
    say("skeleton %s: %d bones; same names / order as the Epic Manny (%s): %s; compatible list %s" % (
        skeleton.get_path_name(), len(names), EPIC_MANNY, manny is not None and names == bones(manny),
        [c.get_path_name() for c in skeleton.get_editor_property("compatible_skeletons") if c]))
    say("  vs the Frostbitten's %s (%d): shared %d, only zombie %d, only Frostbitten %d -> not the same skeleton" % (
        frost.get_name(), len(frost_names), len(set(names) & set(frost_names)), len(set(names) - set(frost_names)), len(set(frost_names) - set(names))))
    for path in BODY_MESHES:
        mesh = library.load_asset(path)
        b = mesh.get_bounds()
        slots = [(str(s.get_editor_property("material_slot_name")), asset_name(s.get_editor_property("material_interface").get_path_name()).split(".")[0])
                 for s in mesh.get_editor_property("materials")]
        say("  body %s: skeleton %s, root parent %s, height %.0f cm (z %.1f..%.1f), slots %s" % (
            asset_name(path), mesh.get_editor_property("skeleton").get_name(), mesh.get_bone_parent("root"), 2 * b.box_extent.z,
            b.origin.z - b.box_extent.z, b.origin.z + b.box_extent.z, slots))
    options = unreal.AnimPoseEvaluationOptions()
    for path in sorted(p.split(".")[0] for p in library.list_assets(CLIPS, False, False)):
        c = library.load_asset(path)
        if not isinstance(c, unreal.AnimSequence):
            continue
        length = c.get_play_length()
        start = unreal.AnimPoseExtensions.get_anim_pose_at_time(c, 0.0, options)
        end = unreal.AnimPoseExtensions.get_anim_pose_at_time(c, length, options)
        travel = (end.get_bone_pose("root", WORLD).translation - start.get_bone_pose("root", WORLD).translation).length()
        say("  clip %-22s %5.2f s, skeleton %s, root motion %s, root travel %.0f cm" % (
            asset_name(path), length, c.get_editor_property("skeleton") == skeleton, c.get_editor_property("enable_root_motion"), travel))


def material(set_index, part):
    path = "%sset_%d/MI_%s_set_%d_Inst" % (MATERIALS, set_index, part, set_index)
    return library.load_asset(path) if library.does_asset_exist(path) else None


def material_sets():
    """Texture set N for the slots by name (the slot names are the pack's, the same on all three bodies)."""
    sets = []
    for n in (1, 2, 3):
        slots = {}
        for slot, part in (("M_armor_set_2", "armor"), ("M_blouse_set_2", "blouse"), ("M_bodi_set_2", "bodi"),
                           ("M_bodi_kit_set_2", "body_kit"), ("M_head_set_2", "head"), ("helmet_set_3", "helmet")):
            mi = material(n, part)
            if mi:
                slots[slot] = mi
        entry = unreal.EnemyMaterialSet()
        entry.set_editor_property("slot_materials", slots)
        sets.append(entry)
        say("  material set %d: %s" % (n, sorted(slots.keys())))
    return sets


def ensure_abp(skeleton, preview_mesh):
    if library.does_asset_exist(ABP):
        abp = library.load_asset(ABP)
        say("kept %s%s" % (ABP, " (re-applying the class defaults: CODEX_MZ_REAPPLY=1)" if REAPPLY else ""))
        if not REAPPLY:
            return abp
    else:
        factory = unreal.AnimBlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_object(None, FROST_ABP + ".ABP_Enemy_Frostbitten_C"))
        factory.set_editor_property("target_skeleton", skeleton)
        factory.set_editor_property("preview_skeletal_mesh", preview_mesh)
        abp = tools.create_asset(asset_name(ABP), ROOT, unreal.AnimBlueprint, factory)
        if abp is None:
            raise RuntimeError("could not create " + ABP)
        created.append(ABP)
        say("created %s (child of ABP_Enemy_Frostbitten, target skeleton %s)" % (ABP, skeleton.get_name()))
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    cdo = unreal.get_default_object(unreal.load_object(None, ABP + "." + asset_name(ABP) + "_C"))
    cdo.set_editor_property("idle_animations", [clip(n) for n in IDLES])
    cdo.set_editor_property("idle_break_animations", [clip(n) for n in IDLES])
    cdo.set_editor_property("idle_activity_animations", [clip("anim_devour")])
    walks = [unreal.EnemyLocomotionVariant(clip=clip(n), ground_speed=s) for n, s in WALKS]
    runs = [unreal.EnemyLocomotionVariant(clip=clip(n), ground_speed=s) for n, s in RUNS]
    cdo.set_editor_property("walk_animation_variants", walks)
    cdo.set_editor_property("run_animation_variants", runs)
    cdo.set_editor_property("walk_animation", clip(WALKS[0][0]))
    cdo.set_editor_property("walk_clip_speed", WALKS[0][1])
    cdo.set_editor_property("run_animation", clip(RUNS[0][0]))
    cdo.set_editor_property("run_clip_speed", RUNS[0][1])
    cdo.set_editor_property("attack_animations", [clip(n) for n in ATTACKS])
    cdo.set_editor_property("hit_animations", [clip("anim_hit")])
    cdo.set_editor_property("death_animations", [clip("anim_Dying")])
    cdo.set_editor_property("jump_attack_animation", None)
    cdo.set_editor_property("alert_animations", [clip("anim_rage")])
    cdo.set_editor_property("entrance_animations", [clip("anim_transformation")])
    for key, value in ANIM_FIELDS.items():
        cdo.set_editor_property(key, value)
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp, False)
    say("ABP class defaults: idles %s (+ the same as fidget breaks), devour activity, walks %s, runs %s, attacks %s, hit, Dying, rage, "
        "transformation from %.1f s x%.1f" % (IDLES, WALKS, RUNS, ATTACKS, ANIM_FIELDS["entrance_start_offset"], ANIM_FIELDS["entrance_play_rate"]))
    return abp


def check_abp(abp, skeleton):
    target = abp.get_editor_property("target_skeleton")
    child_of_frost = unreal.MathLibrary.class_is_child_of(unreal.load_object(None, ABP + "." + asset_name(ABP) + "_C"),
                                                          unreal.load_object(None, FROST_ABP + ".ABP_Enemy_Frostbitten_C"))
    cdo = unreal.get_default_object(unreal.load_object(None, ABP + "." + asset_name(ABP) + "_C"))
    bad = (0 if target == skeleton else 1) + (0 if child_of_frost else 1)
    clips = []
    for prop in ("idle_animations", "idle_break_animations", "idle_activity_animations", "attack_animations", "hit_animations",
                 "death_animations", "alert_animations", "entrance_animations"):
        clips += [c for c in cdo.get_editor_property(prop) if c]
    clips += [v.get_editor_property("clip") for v in list(cdo.get_editor_property("walk_animation_variants")) + list(cdo.get_editor_property("run_animation_variants"))]
    clips += [cdo.get_editor_property("walk_animation"), cdo.get_editor_property("run_animation")]
    off = [c.get_name() for c in clips if not c or c.get_editor_property("skeleton") != skeleton]
    bad += len(off)
    say("ABP %s: child of ABP_Enemy_Frostbitten %s, target skeleton %s (the pack's: %s), %d clips on it%s" % (
        abp.get_name(), child_of_frost, target.get_name() if target else None, target == skeleton,
        len(clips) - len(off), (", NOT on it: %s" % off) if off else ""))
    return bad


def ensure_bp(mesh, sets):
    if library.does_asset_exist(BP):
        bp = library.load_asset(BP)
        say("kept %s%s" % (BP, " (re-applying the fields: CODEX_MZ_REAPPLY=1)" if REAPPLY else ""))
        if not REAPPLY:
            return bp
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_object(None, FROST_BP + ".BP_Enemy_Frostbitten_C"))
        bp = tools.create_asset(asset_name(BP), ROOT, None, factory)
        created.append(BP)
        say("created %s (child of BP_Enemy_Frostbitten)" % BP)
    cdo = unreal.get_default_object(unreal.load_object(None, BP + "." + asset_name(BP) + "_C"))
    body = cdo.get_editor_property("mesh")
    capsule = cdo.get_editor_property("capsule_component")
    body.set_editor_property("skeletal_mesh_asset", mesh)
    body.set_editor_property("override_materials", [])
    body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
    body.set_editor_property("anim_class", unreal.load_object(None, ABP + "." + asset_name(ABP) + "_C"))
    # The ~181 cm bodies stand on their origin and face +Y (like the Frostbitten): the parent's capsule 40 / 90, mesh at z -90 turned
    # yaw -90, scale 1 - written explicitly so a later edit of the Frostbitten does not move him.
    capsule.set_editor_property("capsule_radius", 40.0)
    capsule.set_editor_property("capsule_half_height", 90.0)
    body.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
    body.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, -90.0))
    body.set_editor_property("relative_scale3d", unreal.Vector(1.0, 1.0, 1.0))
    cdo.set_editor_property("display_name_override", DISPLAY_NAME)
    cdo.set_editor_property("override_knockdown_clip_folder", True)
    cdo.set_editor_property("knockdown_clip_folder", "")
    variants = unreal.EnemyLookVariants()
    variants.set_editor_property("meshes", [library.load_asset(p) for p in BODY_MESHES])
    variants.set_editor_property("material_sets", sets)
    cdo.set_editor_property("look_variants", variants)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    library.save_loaded_asset(bp, False)
    say("BP: mesh %s (+ look variants: %d bodies x %d texture sets), anim %s, capsule 40/90, mesh z -90 yaw -90 scale 1, name \"%s\", "
        "knockdown clips /Game/Animations_KnockDown" % (mesh.get_name(), len(BODY_MESHES), len(sets), asset_name(ABP), DISPLAY_NAME))
    return bp


def report_dirty_pack():
    try:
        dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages() if p.get_name().startswith(PACK)]
    except Exception as error:  # an engine without the call: only the report is missing
        dirty = ["<unknown: %s>" % error]
    say("pack packages dirtied in memory (NOT saved): %s" % (dirty if dirty else "none"))


try:
    skeleton = library.load_asset(SKELETON)
    mesh = library.load_asset(BODY_MESHES[0])
    inspect(skeleton)
    sets = material_sets()
    abp = ensure_abp(skeleton, mesh)
    bad = check_abp(abp, skeleton)
    ensure_bp(mesh, sets)
    report_dirty_pack()
    say("RESULT: %s (%d problems, created %d assets: %s)" % ("PASS" if bad == 0 else "CHECK", bad, len(created), created))
except Exception:
    import traceback
    say("ERROR " + traceback.format_exc())

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_military_zombie.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
