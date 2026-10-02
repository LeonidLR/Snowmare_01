"""Sets up the Marksman enemy (AMarksmanEnemyCharacter, UE-only archetype) on the user's Biochemical_Monster_1:

  - the monster skeleton (UE4 mannequin rig) is made compatible with the RifleAnims and Crawl_MocapAnimPack skeletons,
    whose clips fill what the monster pack lacks (crouch from RifleAnims, prone / stance transitions / prone hit and
    death from the crawl pack; no retarget, same rig);
  - /Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman — parent UMarksmanAnimInstance, graph built by
    UOperativeAnimGraphLibrary.BuildMarksmanLocomotionGraph (stance / aim still poses, walk / run, upper-body and
    full-body slots), clips on the class defaults;
  - /Game/Characters/Enemies/Marksman/BP_Enemy_Marksman — parent AMarksmanEnemyCharacter, SKM_baze_mesh1 (the variant
    with the rifle), turned to face +X; ACodexTacticsGameMode::EnemyClasses spawns it for EEnemyArchetype::Marksman.

Re-running keeps hand edits: the graph is built only while empty (CODEX_REBUILD_ANIM_GRAPHS=1 forces it), clips only
fill empty fields, the look (capsule, mesh offset) is written once (tag LookAuthored).

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi
Result: Saved/Logs/setup_marksman_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
REBUILD = os.environ.get("CODEX_REBUILD_ANIM_GRAPHS") == "1"
log = []

ROOT = "/Game/Characters/Enemies/Marksman"
MON = "/Game/Biochemical_Monster_1/animation/anim_ue4/"
MESH = "/Game/Biochemical_Monster_1/baze_mech/ue4/SKM_baze_mesh1"
RIFLE = "/Game/RifleAnims/Animations/"
CRAWL = "/Game/Crawl_MocapAnimPack/Animations/"
OTHER_SKELETONS = ("/Game/RifleAnims/ShowcaseAssets/Meshes/Mannequin/UE4_Mannequin_Skeleton",
                   "/Game/Crawl_MocapAnimPack/Demo/Models/SK_Mannequin_A_Skeleton",
                   "/Game/Crawl_MocapAnimPack/Demo/Models/Rifle/Rifle_Mannequin_A_Skeleton",
                   "/Game/Crawl_MocapAnimPack/Demo/Models/Pistol/Pistol_Mannequin_A_Skeleton")

CLIPS = {
    # Monster pack: rifle-up idles (anim_idle_1 = the aim hold), walk / run, four scoped shots, death.
    "idle_animations": [MON + "anim_idle_2", MON + "anim_idle_3"],
    "stand_aim_animation": MON + "anim_idle_1",
    "walk_animation": MON + "anim_walking",
    "run_animation": MON + "anim_run",
    "attack_animations": [MON + "anim_attack_1", MON + "anim_attack_2", MON + "anim_attack_3", MON + "anim_attack_4"],
    "death_animations": [MON + "anim_death"],
    # RifleAnims (crouch).
    "crouch_idle_animation": RIFLE + "BlendSpaces/Crouch_IdleWalk/AS_Rifle_Crouch",
    "crouch_aim_animation": RIFLE + "BlendSpaces/Crouch_IdleWalk_Aim/AS_Rifle_Crouch_Aim",
    "crouch_fire_animation": RIFLE + "Fire_Reload_Equip_Jump/AS_Rifle_Fire_Aim",
    # Crawl pack (prone).
    "prone_idle_animation": CRAWL + "Rifle_Set/Idle/Crawl_Rifle_Idle01",
    "prone_aim_animation": CRAWL + "Rifle_Set/Idle/Crawl_Rifle_Aim_Idle",
    "prone_fire_animations": [CRAWL + "Rifle_Set/Shoots/Crawl_Rifle_Shoot_Hard"],
    "prone_hit_animations": [CRAWL + "Hit_Death_Set/Crawl_Hit_F"],
    "prone_death_animations": [CRAWL + "Hit_Death_Set/Crawl_Death01"],
    "stand_to_prone_animation": CRAWL + "Transitions_Set/Crawl_from_Act",
    "prone_to_stand_animation": CRAWL + "Transitions_Set/Crawl_to_Act",
    "crouch_to_prone_animation": CRAWL + "Transitions_Set/Crawl_from_Cr",
    "prone_to_crouch_animation": CRAWL + "Transitions_Set/Crawl_to_Cr",
}
RATES = {"run_speed_threshold": 420.0, "walk_clip_speed": 160.0, "run_clip_speed": 480.0, "attack_play_rate": 1.0,
         "death_play_rate": 1.0, "death_start_offset": 0.0, "fire_play_rate": 1.0, "stance_transition_play_rate": 1.5}


def load(path):
    asset = library.load_asset(path) if path else None
    if path and asset is None:
        log.append(f"  MISSING {path}")
    return asset


mesh = load(MESH)
skeleton = mesh.get_editor_property("skeleton")
log.append(f"mesh {mesh.get_path_name()} skeleton {skeleton.get_path_name()}")

# Same rig, several skeleton assets: let each use the other's animations.
for path in OTHER_SKELETONS:
    if not library.does_asset_exist(path):
        log.append(f"  no skeleton {path}")
        continue
    other = library.load_asset(path)
    for a, b in ((skeleton, other), (other, skeleton)):
        compatible = list(a.get_editor_property("compatible_skeletons"))
        if not any(c and c.get_path_name() == b.get_path_name() for c in compatible):
            compatible.append(b)
            a.set_editor_property("compatible_skeletons", compatible)
            library.save_loaded_asset(a)
            log.append(f"{a.get_path_name()} now compatible with {b.get_path_name()}")

abp_path = f"{ROOT}/ABP_Enemy_Marksman"
created_abp = False
if library.does_asset_exist(abp_path):
    abp = library.load_asset(abp_path)
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("parent_class", unreal.MarksmanAnimInstance)
    abp = tools.create_asset("ABP_Enemy_Marksman", ROOT, unreal.AnimBlueprint, factory)
    created_abp = True
    log.append(f"created {abp_path}")
if REBUILD or unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp) == 0:
    result = unreal.OperativeAnimGraphLibrary.build_marksman_locomotion_graph(abp, "DefaultSlot", 0.2, 0.25, "UpperBody", "spine_01")
    ok, report = result if isinstance(result, tuple) else ("errors 0" in result, result)
    log.append(f"graph built ok={ok} | {report.strip()}")
else:
    log.append("graph kept (edited by hand)")

anim = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.ABP_Enemy_Marksman_C"))
for prop, value in CLIPS.items():
    current = anim.get_editor_property(prop)
    empty = current is None or (hasattr(current, "__len__") and len(current) == 0)
    if created_abp or empty:
        anim.set_editor_property(prop, [load(p) for p in value] if isinstance(value, list) else load(value))
if created_abp:
    for prop, value in RATES.items():
        anim.set_editor_property(prop, value)
anim.set_editor_property("upper_body_hit_reactions", True)
anim.set_editor_property("upper_body_slot", "UpperBody")
unreal.BlueprintEditorLibrary.compile_blueprint(abp)
library.save_loaded_asset(abp)
log.append(f"ABP nodes {unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp)}")

bp_path = f"{ROOT}/BP_Enemy_Marksman"
if library.does_asset_exist(bp_path):
    bp = library.load_asset(bp_path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.MarksmanEnemyCharacter)
    bp = tools.create_asset("BP_Enemy_Marksman", ROOT, None, factory)
    log.append(f"created {bp_path}")
cdo = unreal.get_default_object(unreal.load_object(None, f"{bp_path}.BP_Enemy_Marksman_C"))
body = cdo.get_editor_property("mesh")
capsule = cdo.get_editor_property("capsule_component")
if body.get_editor_property("skeletal_mesh_asset") is None:
    body.set_editor_property("skeletal_mesh_asset", mesh)
    # The model faces +Y; UE characters face +X.
    body.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, -90.0))
if body.get_editor_property("anim_class") is None:
    body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
    body.set_editor_property("anim_class", unreal.load_object(None, f"{abp_path}.ABP_Enemy_Marksman_C"))
tags = list(cdo.get_editor_property("tags"))
if "LookAuthored" not in [str(t) for t in tags]:
    cdo.set_editor_property("tags", tags + ["LookAuthored"])
    capsule.set_editor_property("capsule_radius", 40.0)
    capsule.set_editor_property("capsule_half_height", 90.0)  # = FMarksmanConfig::StandHalfHeight
    body.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
    body.set_editor_property("relative_scale3d", unreal.Vector(1.0, 1.0, 1.0))
    log.append("look: capsule 40/90, mesh z -90, scale 1")
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
library.save_loaded_asset(bp)
log.append(f"BP mesh={mesh.get_name()} anim=ABP_Enemy_Marksman_C")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_marksman_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
