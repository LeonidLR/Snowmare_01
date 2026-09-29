"""Imports the Godot level configs (data/configs/levels/*.json) into ULevelConfigAsset data assets
/Game/Data/Levels/DA_Level_<file name> (e.g. DA_Level_level_01_outpost — the one Godot main.gd loads by default,
active_level_json_path). Waves, spawns (enemy type by name), modifiers, preparation / rest durations.
Re-run after the Godot levels change; existing assets are updated in place. stage_template.json is skipped.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_levels.txt
"""
import glob
import json
import os
import unreal

GODOT = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01"
TARGET = "/Game/Data/Levels"
# Godot enemy_type strings -> UE EEnemyArchetype (python names)
ARCHETYPES = {"HOUND": "FROST_HOUND", "FROST_HOUND": "FROST_HOUND", "SPITTER": "SPITTER", "BRUTE": "BRUTE",
              "FROSTBITTEN": "FROSTBITTEN", "CUTTER": "CUTTER", "CRYO_DRONE": "CRYO_DRONE"}
log = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
for path in sorted(glob.glob(os.path.join(GODOT, "data", "configs", "levels", "*.json"))):
    base = os.path.splitext(os.path.basename(path))[0]
    if base == "stage_template":
        continue
    data = json.load(open(path, encoding="utf-8"))
    name = "DA_Level_" + base
    full = TARGET + "/" + name
    try:
        config = unreal.LevelCombatConfig()
        config.set_editor_property("level_id", data.get("level_id", base))
        config.set_editor_property("level_name", unreal.Text(data.get("level_name", base)))
        config.set_editor_property("prep_phase_duration", float(data.get("prep_phase_duration", 60)))
        config.set_editor_property("wave_rest_duration", float(data.get("wave_rest_duration", 20)))
        waves = []
        # main.gd _load_active_level_config keeps only waves without "is_active": false
        for wave in [w for w in data.get("waves", []) if w.get("is_active", True) is not False]:
            definition = unreal.WaveDefinition()
            definition.set_editor_property("wave_index", int(wave.get("wave_index", len(waves) + 1)))
            definition.set_editor_property("name", unreal.Text(wave.get("name", "")))
            definition.set_editor_property("max_simultaneous_enemies", int(wave.get("max_simultaneous_enemies", 8)))
            spawns = []
            for spawn in wave.get("spawns", []):
                entry = unreal.EnemySpawnEntry()
                archetype = ARCHETYPES[spawn["enemy_type"].upper()]
                entry.set_editor_property("enemy_type", getattr(unreal.EnemyArchetype, archetype))
                entry.set_editor_property("count", int(spawn.get("count", 1)))
                entry.set_editor_property("spawn_lane", spawn.get("spawn_lane", "ANY"))
                entry.set_editor_property("spawn_delay_sec", float(spawn.get("spawn_delay_sec", 1.0)))
                entry.set_editor_property("initial_delay_sec", float(spawn.get("initial_delay_sec", 0.0)))
                entry.set_editor_property("custom_health", float(spawn.get("custom_stats", {}).get("health", spawn.get("custom_health", 0.0))))
                spawns.append(entry)
            definition.set_editor_property("spawns", spawns)
            mods = wave.get("wave_modifiers", {})
            modifiers = unreal.WaveModifiers()
            modifiers.set_editor_property("enemy_hp_mult", float(mods.get("enemy_hp_mult", 1.0)))
            modifiers.set_editor_property("enemy_damage_mult", float(mods.get("enemy_damage_mult", 1.0)))
            modifiers.set_editor_property("enemy_speed_mult", float(mods.get("enemy_speed_mult", 1.0)))
            modifiers.set_editor_property("cold_drain_mult", float(mods.get("cold_drain_mult", 1.0)))
            definition.set_editor_property("modifiers", modifiers)
            waves.append(definition)
        config.set_editor_property("waves", waves)
        asset = unreal.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else \
            tools.create_asset(name, TARGET, unreal.LevelConfigAsset, unreal.DataAssetFactory())
        asset.set_editor_property("config", config)
        saved = unreal.EditorAssetLibrary.save_asset(full)
        totals = [sum(s.get("count", 1) for s in w.get("spawns", [])) for w in data.get("waves", []) if w.get("is_active", True) is not False]
        log.append("%s <- %s: %d waves %s saved=%s" % (full, os.path.basename(path), len(waves), totals, saved))
    except Exception as error:
        log.append("FAILED %s: %s" % (full, error))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_levels.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
