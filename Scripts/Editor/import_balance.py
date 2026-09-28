"""Imports the Godot GameBalanceConfig resources into UGodotBalanceAsset data assets:
  resources/balance.tres             -> /Game/Data/Balance/DA_Balance            (turn-based manager loads this one first)
  resources/game_balance_config.tres -> /Game/Data/Balance/DA_GameBalanceConfig  (camera, enemies, deployables)
Defaults come from resources/game_balance_config.gd (@export values); the .tres values override them. Only numbers and
bools are stored (bools as 0 / 1), under their Godot names. Re-run after the Godot balance changes.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_balance.txt
"""
import os
import re
import unreal

GODOT = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01"
TARGET = "/Game/Data/Balance"
SOURCES = {"DA_Balance": "balance.tres", "DA_GameBalanceConfig": "game_balance_config.tres"}
log = []


def number(raw):
    raw = raw.strip()
    if raw in ("true", "false"):
        return 1.0 if raw == "true" else 0.0
    try:
        return float(raw)
    except ValueError:
        return None


source = open(os.path.join(GODOT, "resources", "game_balance_config.gd"), encoding="utf-8").read()
defaults = {}
for m in re.finditer(r'^@export\w*(?:\([^)]*\))?\s+var\s+(\w+)\s*(?::\s*[\w\[\]]+)?\s*=\s*([^#\n]+)', source, re.M):
    value = number(m.group(2))
    if value is not None:
        defaults[m.group(1)] = value

tools = unreal.AssetToolsHelpers.get_asset_tools()
for asset_name, file_name in SOURCES.items():
    values = dict(defaults)
    text = open(os.path.join(GODOT, "resources", file_name), encoding="utf-8").read().replace("\r\n", "\n")
    body = text.split("[resource]", 1)[1]
    overridden = 0
    for m in re.finditer(r'^(\w+) = (.+)$', body, re.M):
        value = number(m.group(2))
        if value is not None:
            values[m.group(1)] = value
            overridden += 1
    full = TARGET + "/" + asset_name
    asset = unreal.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else \
        tools.create_asset(asset_name, TARGET, unreal.GodotBalanceAsset, unreal.DataAssetFactory())
    asset.set_editor_property("source_file", "res://resources/" + file_name)
    asset.set_editor_property("numbers", {unreal.Name(k): float(v) for k, v in values.items()})
    saved = unreal.EditorAssetLibrary.save_asset(full)
    log.append("%s <- %s: %d values (%d from the .tres) saved=%s" % (full, file_name, len(values), overridden, saved))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_balance.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
