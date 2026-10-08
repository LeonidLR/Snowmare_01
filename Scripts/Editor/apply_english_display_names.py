"""Copies the English display names from the repo data files onto the DataAssets (user decision 2026-10-08: all in-game
text is English for now; localization comes later).

  * /Game/Data/Levels/DA_Level_<file>  <- Content/Data/LevelJson/<file>.json : level_name, waves[].name
  * /Game/Data/Weapons/DA_Weapon_<id>  <- Content/Data/Weapons/weapons_tuning.json : weapons.<id>.name -> weapon_name

Only names are written (no gameplay values). Dialogue texts are NOT touched here: they come from
Content/Data/Narrative/narrative_manifest.json at runtime (Scripts/Narrative/sync_narrative.py).

Run headless (only while no editor has the project open):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/apply_english_display_names.txt
"""
import glob
import json
import os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LOG = os.path.join(ROOT, "Saved", "Logs", "apply_english_display_names.txt")
lines = []


def log(text):
    lines.append(text)
    unreal.log(text)


def set_levels():
    for path in sorted(glob.glob(os.path.join(ROOT, "Content", "Data", "LevelJson", "*.json"))):
        base = os.path.splitext(os.path.basename(path))[0]
        full = "/Game/Data/Levels/DA_Level_" + base
        if base == "stage_template" or not unreal.EditorAssetLibrary.does_asset_exist(full):
            continue
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
        asset = unreal.load_asset(full)
        config = asset.get_editor_property("config")
        config.set_editor_property("level_name", unreal.Text(data.get("level_name", base)))
        waves = list(config.get_editor_property("waves"))
        for index, wave in enumerate(data.get("waves", [])):
            if index < len(waves):
                waves[index].set_editor_property("name", unreal.Text(wave.get("name", "Wave %d" % (index + 1))))
        config.set_editor_property("waves", waves)
        asset.set_editor_property("config", config)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        log("level %s -> %s" % (base, data.get("level_name")))


def set_weapons():
    with open(os.path.join(ROOT, "Content", "Data", "Weapons", "weapons_tuning.json"), encoding="utf-8") as fh:
        weapons = json.load(fh).get("weapons", {})
    for weapon_id, values in weapons.items():
        full = "/Game/Data/Weapons/DA_Weapon_" + weapon_id
        if not unreal.EditorAssetLibrary.does_asset_exist(full) or "name" not in values:
            continue
        asset = unreal.load_asset(full)
        asset.set_editor_property("weapon_name", unreal.Text(values["name"]))
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        log("weapon %s -> %s" % (weapon_id, values["name"]))


try:
    set_levels()
    set_weapons()
except Exception as error:  # keep the log even when an asset fails
    log("FAILED: %s" % error)
os.makedirs(os.path.dirname(LOG), exist_ok=True)
with open(LOG, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))
