"""Creates the enemies' Blueprints from the user's monster imports (stage 1 enemy types):

  FROST_HOUND  <- /Game/Combat_Dog              (Godot resources/enemies/anims/hound.tres)
  BRUTE        <- /Game/Mutant_monster_1         (brute.tres: anim_idle_1..3, anim_Walking, anim_attack_1..3 x0.9,
                                                  anim_Dying x0.6 from 1.0 s)
  FROSTBITTEN  <- /Game/mutant_monster_2         (frostbitten.tres: anim_idle_1, anim_run, anim_attack_1..2 x1.2,
                                                  anim_hit, anim_death)
  CUTTER       <- /Game/Biochemical_Monster_2    (cutter.tres: idles, run, attacks, dying from 1.0 s, jumping attack x1.85)

For each type:
  /Game/Characters/Enemies/<Type>/ABP_Enemy_<Type> — parent UEnemyAnimInstance, graph built by
      UOperativeAnimGraphLibrary.BuildEnemyLocomotionGraph (idle / walk / run + one-shot slot), clips on the class defaults;
  /Game/Characters/Enemies/<Type>/BP_Enemy_<Type>  — parent AEnemyCharacter, the skeletal mesh turned to face +X.
ACodexTacticsGameMode::EnemyClasses spawns these Blueprints; they own their look (capsule, mesh offset / rotation /
scale — AEnemyCharacter leaves them alone), the stats come from the Godot balance.
Re-running never overwrites what was set by hand: the graph is built only while empty (CODEX_REBUILD_ANIM_GRAPHS=1
forces it), clips / rates only fill empty fields, capsule and mesh transform are written once (tag LookAuthored).

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_enemy_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
REBUILD = os.environ.get("CODEX_REBUILD_ANIM_GRAPHS") == "1"
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []

DOG = "/Game/Combat_Dog/Anims/Anim_Combat_Dog_"
BRUTE = "/Game/Mutant_monster_1/animation/"
FROST = "/Game/mutant_monster_2/animation/"
CUTTER = "/Game/Biochemical_Monster_2/animation/anim_ue4/"

ENEMIES = {
    "Hound": {
        "mesh": "/Game/Combat_Dog/Mesh/SK_Combat_Dog",
        "idle": [DOG + "Idle_1", DOG + "Idle_2", DOG + "Idle_3"],
        "walk": DOG + "Walk", "run": DOG + "Run", "run_threshold": 300.0, "walk_speed": 150.0, "run_speed": 550.0,
        "attacks": [DOG + "Attack_1", DOG + "Attack_2", DOG + "Attack_3"], "attack_rate": 1.3,
        "hits": [DOG + "GetHit_1", DOG + "GetHit_2", DOG + "GetHit_3"],
        "deaths": [DOG + "Death"], "death_rate": 1.0, "death_offset": 0.0,
        "capsule": (35.0, 60.0), "scale": 0.8,
    },
    "Brute": {
        "mesh": "/Game/Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1",
        "idle": [BRUTE + "anim_idle_1", BRUTE + "anim_idle_2", BRUTE + "anim_idle_3"],
        # Godot brute run_animation = anim_Walking: it only walks.
        "walk": BRUTE + "anim_Walking", "run": None, "run_threshold": 100000.0, "walk_speed": 150.0, "run_speed": 450.0,
        "attacks": [BRUTE + "anim_attack_1", BRUTE + "anim_attack_2", BRUTE + "anim_attack_3"], "attack_rate": 0.9,
        "hits": [],
        "deaths": [BRUTE + "anim_Dying"], "death_rate": 0.6, "death_offset": 1.0,
        "capsule": (60.0, 120.0), "scale": 1.4,
    },
    "Frostbitten": {
        "mesh": "/Game/mutant_monster_2/base_mesh/SK_base_mesh",
        "idle": [FROST + "anim_idle_1"],
        "walk": FROST + "anim_walking", "run": FROST + "anim_run", "run_threshold": 200.0, "walk_speed": 150.0, "run_speed": 400.0,
        "attacks": [FROST + "anim_attack_1", FROST + "anim_attack_2"], "attack_rate": 1.2,
        "hits": [FROST + "anim_hit"],
        "deaths": [FROST + "anim_death"], "death_rate": 1.0, "death_offset": 0.0,
        "capsule": (40.0, 90.0), "scale": 1.0,
    },
    "Cutter": {
        "mesh": "/Game/Biochemical_Monster_2/baze_mech/ue4/SKM_baze_mesh",
        "idle": [CUTTER + "anim_idle_1", CUTTER + "anim_idle_2", CUTTER + "anim_idle_3", CUTTER + "anim_idle_4"],
        "walk": CUTTER + "anim_Walking", "run": CUTTER + "anim_run", "run_threshold": 300.0, "walk_speed": 150.0, "run_speed": 500.0,
        "attacks": [CUTTER + "anim_attack_1", CUTTER + "anim_attack_2"], "attack_rate": 1.3,
        "hits": [],  # Godot cutter.tres: enable_hit_reaction off
        "deaths": [CUTTER + "anim_Dying"], "death_rate": 1.0, "death_offset": 1.0,
        "jump": CUTTER + "anim_Jumping_attack", "jump_rate": 1.85,
        "capsule": (35.0, 60.0), "scale": 0.85,
    },
}


def load(path):
    asset = library.load_asset(path) if path else None
    if path and asset is None:
        log.append(f"  MISSING {path}")
    return asset


# Upper-body hit layer bone (TANDEM request 1): the spine of the humanoid rigs; the hound's front legs hang off its spine,
# so only its neck flinches.
UPPER_BODY_BONE = {"Hound": "bip001-neck", "Frostbitten": "spine_01", "Cutter": "spine_01", "Brute": "spine_01"}
GENERATED_NODE_COUNTS = (13, 18)

for name, cfg in ENEMIES.items():
    root = f"/Game/Characters/Enemies/{name}"
    mesh = load(cfg["mesh"])
    skeleton = mesh.get_editor_property("skeleton")

    abp_path = f"{root}/ABP_Enemy_{name}"
    created_abp = False
    if library.does_asset_exist(abp_path):
        abp = library.load_asset(abp_path)
    else:
        factory = unreal.AnimBlueprintFactory()
        factory.set_editor_property("target_skeleton", skeleton)
        factory.set_editor_property("parent_class", unreal.EnemyAnimInstance)
        abp = tools.create_asset(f"ABP_Enemy_{name}", root, unreal.AnimBlueprint, factory)
        created_abp = True
        log.append(f"created {abp_path}")
    # Graphs this script generated earlier (left as generated -> safe to regenerate with the new layout): 13 nodes =
    # before the upper-body hit layer.
    node_count = unreal.OperativeAnimGraphLibrary.count_anim_graph_nodes(abp)
    if REBUILD or node_count == 0 or node_count in GENERATED_NODE_COUNTS:
        result = unreal.OperativeAnimGraphLibrary.build_enemy_locomotion_graph(abp, "DefaultSlot", 0.2, "UpperBody", UPPER_BODY_BONE[name])
        ok, report = result if isinstance(result, tuple) else ("errors 0" in result, result)
        log.append(f"{name}: graph built ok={ok} | {report.strip()}")
    else:
        log.append(f"{name}: graph kept (edited by hand)")

    anim = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.ABP_Enemy_{name}_C"))

    def fill(prop, value):
        """Only empty fields (or a new ABP) get the default clip / rate."""
        current = anim.get_editor_property(prop)
        empty = current is None or (hasattr(current, "__len__") and len(current) == 0)
        if created_abp or empty:
            anim.set_editor_property(prop, value)

    anim.set_editor_property("upper_body_hit_reactions", True)
    anim.set_editor_property("upper_body_slot", "UpperBody")
    fill("idle_animations", [load(p) for p in cfg["idle"]])
    fill("walk_animation", load(cfg["walk"]))
    fill("run_animation", load(cfg["run"]))
    fill("attack_animations", [load(p) for p in cfg["attacks"]])
    fill("hit_animations", [load(p) for p in cfg["hits"]])
    fill("death_animations", [load(p) for p in cfg["deaths"]])
    fill("jump_attack_animation", load(cfg.get("jump")))
    if created_abp:
        anim.set_editor_property("run_speed_threshold", cfg["run_threshold"])
        anim.set_editor_property("walk_clip_speed", cfg["walk_speed"])
        anim.set_editor_property("run_clip_speed", cfg["run_speed"])
        anim.set_editor_property("attack_play_rate", cfg["attack_rate"])
        anim.set_editor_property("death_play_rate", cfg["death_rate"])
        anim.set_editor_property("death_start_offset", cfg["death_offset"])
        anim.set_editor_property("jump_attack_play_rate", cfg.get("jump_rate", 1.85))
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp)

    bp_path = f"{root}/BP_Enemy_{name}"
    if library.does_asset_exist(bp_path):
        bp = library.load_asset(bp_path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.EnemyCharacter)
        bp = tools.create_asset(f"BP_Enemy_{name}", root, None, factory)
        log.append(f"created {bp_path}")
    cdo = unreal.get_default_object(unreal.load_object(None, f"{bp_path}.BP_Enemy_{name}_C"))
    body = cdo.get_editor_property("mesh")
    capsule = cdo.get_editor_property("capsule_component")
    if body.get_editor_property("skeletal_mesh_asset") is None:
        body.set_editor_property("skeletal_mesh_asset", mesh)
        # The models face +Y; UE characters face +X.
        body.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, -90.0))
    if body.get_editor_property("anim_class") is None:
        body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
        body.set_editor_property("anim_class", unreal.load_object(None, f"{abp_path}.ABP_Enemy_{name}_C"))
    # Look written once (the Godot per-type capsule and model size); the "LookAuthored" tag then leaves it to the
    # Blueprint (edited by hand from there on).
    tags = list(cdo.get_editor_property("tags"))
    if "LookAuthored" not in [str(t) for t in tags]:
        cdo.set_editor_property("tags", tags + ["LookAuthored"])
        radius, half = cfg["capsule"]
        capsule.set_editor_property("capsule_radius", radius)
        capsule.set_editor_property("capsule_half_height", half)
        body.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -half))
        body.set_editor_property("relative_scale3d", unreal.Vector(cfg["scale"], cfg["scale"], cfg["scale"]))
        log.append(f"{name}: capsule {radius}/{half}, mesh z {-half}, scale {cfg['scale']}")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    library.save_loaded_asset(bp)
    log.append(f"{name}: BP mesh={mesh.get_name()} anim=ABP_Enemy_{name}_C")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_enemy_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
