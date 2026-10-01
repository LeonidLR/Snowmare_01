"""Godot GameBalanceConfig resources <-> the Unreal balance assets (UGameBalanceConfig):
  resources/balance.tres             -> /Game/Data/Balance/DA_Balance            (turn-based manager loads this one first)
  resources/game_balance_config.tres -> /Game/Data/Balance/DA_GameBalanceConfig  (camera, enemies, deployables)

Unreal is the master copy (user decision 2026-10-01): the values are tuned in these assets in the editor.
  - Asset missing -> created as UGameBalanceConfig and filled from Godot (once; an old flat UGodotBalanceAsset is
    reported: delete its .uasset with the editor closed and re-run).
  - Otherwise (default) nothing is written: Saved/Logs/import_balance.txt lists where Godot differs from Unreal.
  - CODEX_BALANCE_REIMPORT=1 overwrites the assets with the Godot values (your Unreal edits are lost).
Godot defaults come from resources/game_balance_config.gd (@export values); the .tres values override them. Keys that have
no typed field (not exported by game_balance_config.gd) go to Numbers. Fields come from Scripts/generate_balance_config.py.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import os
import re
import unreal

GODOT = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01"
TARGET = "/Game/Data/Balance"
SOURCES = {"DA_Balance": "balance.tres", "DA_GameBalanceConfig": "game_balance_config.tres"}
REIMPORT = os.environ.get("CODEX_BALANCE_REIMPORT") == "1"
library = unreal.EditorAssetLibrary
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
for m in re.finditer(r'^@export\w*(?:\([^)]*\))?\s+var\s+(\w+)\s*(?::\s*[\w\[\]]+)?\s*=\s*([^#\n:]+)', source, re.M):
    value = number(m.group(2))
    if value is not None:
        defaults[m.group(1)] = value

tools = unreal.AssetToolsHelpers.get_asset_tools()
for asset_name, file_name in SOURCES.items():
    values = dict(defaults)
    text = open(os.path.join(GODOT, "resources", file_name), encoding="utf-8").read().replace("\r\n", "\n")
    body = text.split("[resource]", 1)[1]
    for m in re.finditer(r'^(\w+) = (.+)$', body, re.M):
        value = number(m.group(2))
        if value is not None:
            values[m.group(1)] = value

    full = TARGET + "/" + asset_name
    asset = library.load_asset(full) if library.does_asset_exist(full) else None
    if asset is not None and not isinstance(asset, unreal.GameBalanceConfig):
        # An asset's class cannot change in place and a deleted package stays loaded in this session.
        log.append(f"{full}: old flat UGodotBalanceAsset — delete Content{full[5:]}.uasset (editor closed) and re-run")
        continue
    migrate = asset is None
    if migrate:
        asset = tools.create_asset(asset_name, TARGET, unreal.GameBalanceConfig, unreal.DataAssetFactory())

    if migrate or REIMPORT:
        asset.set_editor_property("source_file", "res://resources/" + file_name)
        asset.set_editor_property("numbers", {})
        for key, value in values.items():
            asset.set_number(key, value)
        saved = library.save_loaded_asset(asset, False)
        extra = len(asset.get_editor_property("numbers"))
        log.append(f"{full} <- {file_name}: {len(values)} values written ({len(values) - extra} fields, {extra} in Numbers) saved={saved}")
        continue

    diffs = []
    for key, godot in sorted(values.items()):
        ours = asset.get_number(key, float("nan"))
        if ours != ours:
            diffs.append(f"  {key}: missing in Unreal (Godot {godot:g}) — re-run generate_balance_config.py")
        elif abs(ours - godot) > 1e-4 * max(1.0, abs(godot)):
            diffs.append(f"  {key}: Unreal {ours:g}, Godot {godot:g}")
    log.append(f"{full} vs {file_name}: {len(diffs)} differences (nothing written; CODEX_BALANCE_REIMPORT=1 overwrites)")
    log.extend(diffs)

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_balance.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
