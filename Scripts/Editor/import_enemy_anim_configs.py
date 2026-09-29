"""Imports the Godot enemy animation configs (numbers / bools only) into UGodotBalanceAsset data assets:
  resources/enemies/anims/<name>.tres -> /Game/Data/Enemies/DA_EnemyAnim_<name>
Defaults come from resources/enemy_animation_config.gd (@export values); the .tres values override them. The C++ side
reads gameplay numbers from them (the cutter's jump attack: enable_jump_attack, jump_*). Animation clip names are
not imported (the operative / enemy Blueprints own the animations). Re-run after the Godot configs change.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_enemy_anim_configs.txt
"""
import glob
import os
import re
import unreal

GODOT = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01"
TARGET = "/Game/Data/Enemies"
log = []


def number(raw):
    raw = raw.strip()
    if raw in ("true", "false"):
        return 1.0 if raw == "true" else 0.0
    try:
        return float(raw)
    except ValueError:
        return None


source = open(os.path.join(GODOT, "resources", "enemy_animation_config.gd"), encoding="utf-8").read()
defaults = {}
for m in re.finditer(r'^@export\w*(?:\([^)]*\))?\s+var\s+(\w+)\s*(?::\s*[\w\[\]]+)?\s*=\s*([^#\n]+)', source, re.M):
    value = number(m.group(2))
    if value is not None:
        defaults[m.group(1)] = value

tools = unreal.AssetToolsHelpers.get_asset_tools()
for path in sorted(glob.glob(os.path.join(GODOT, "resources", "enemies", "anims", "*.tres"))):
    name = os.path.splitext(os.path.basename(path))[0]
    values = dict(defaults)
    text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    body = text.split("[resource]", 1)[1]
    overridden = 0
    for m in re.finditer(r'^(\w+) = (.+)$', body, re.M):
        value = number(m.group(2))
        if value is not None:
            values[m.group(1)] = value
            overridden += 1
    asset_name = "DA_EnemyAnim_" + name
    full = TARGET + "/" + asset_name
    asset = unreal.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else \
        tools.create_asset(asset_name, TARGET, unreal.GodotBalanceAsset, unreal.DataAssetFactory())
    asset.set_editor_property("source_file", "res://resources/enemies/anims/" + name + ".tres")
    asset.set_editor_property("numbers", {unreal.Name(k): float(v) for k, v in values.items()})
    saved = unreal.EditorAssetLibrary.save_asset(full)
    log.append("%s <- %s: %d values (%d from the .tres) saved=%s" % (full, name, len(values), overridden, saved))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_enemy_anim_configs.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
