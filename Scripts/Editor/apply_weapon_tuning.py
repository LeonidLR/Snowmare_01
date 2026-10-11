"""Writes Content/Data/Weapons/weapons_tuning.json (the master copy of weapon power, Wave Editor «Оружие») onto the
DA_Weapon_<id> assets so the editor's Details show the same numbers the game applies at StartPlay (WeaponTuning::ApplyFile).

User request 2026-10-11 (M16 range 14 -> 25 m). Headless, with the editor closed:
  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path to this file>
Env CODEX_WEAPONS="m16,pistol" limits the saved assets (default: every weapon in the file); assets of other weapons that
differ are only reported. Only assets whose values change are saved. Idempotent.
"""
import json
import os

import unreal

FLOATS = (("base_damage", "base_damage", 1.0), ("attack_range_m", "attack_range_cm", 100.0), ("fire_rate", "fire_rate", 1.0),
          ("armor_penetration", "armor_penetration", 1.0), ("reload_time", "reload_time", 1.0),
          ("status_duration", "status_duration", 1.0), ("status_tick_damage", "status_tick_damage", 1.0))
INTS = ("max_clip_size", "default_reserve_ammo", "max_range_cells")
LISTS = ("base_hit_chances", "distance_damage_multipliers")

library = unreal.EditorAssetLibrary
path = os.path.join(unreal.Paths.project_content_dir(), "Data", "Weapons", "weapons_tuning.json")
with open(path, encoding="utf-8") as f:
    weapons = json.load(f).get("weapons", {})
only = [w.strip() for w in os.environ.get("CODEX_WEAPONS", "").split(",") if w.strip()]
log = []
for weapon_id, tuning in weapons.items():
    asset_path = f"/Game/Data/Weapons/DA_Weapon_{weapon_id}"
    if not library.does_asset_exist(asset_path):
        log.append(f"{weapon_id}: no asset {asset_path}")
        continue
    weapon = library.load_asset(asset_path)
    changes = []
    for key, prop, scale in FLOATS:
        if key in tuning:
            value = float(tuning[key]) * scale
            if abs(weapon.get_editor_property(prop) - value) > 1e-4:
                changes.append((prop, weapon.get_editor_property(prop), value))
    for key in INTS:
        if key in tuning and weapon.get_editor_property(key) != int(tuning[key]):
            changes.append((key, weapon.get_editor_property(key), int(tuning[key])))
    for key in LISTS:
        if key in tuning:
            value = [float(v) for v in tuning[key]]
            current = [float(v) for v in weapon.get_editor_property(key)]
            if len(current) != len(value) or any(abs(a - b) > 1e-4 for a, b in zip(current, value)):
                changes.append((key, current, value))
    if not changes:
        continue
    if only and weapon_id not in only:
        log.append(f"{weapon_id}: differs (not saved): " + "; ".join(f"{p} {old} -> {new}" for p, old, new in changes))
        continue
    for prop, _old, new in changes:
        weapon.set_editor_property(prop, new)
    library.save_loaded_asset(weapon, only_if_is_dirty=False)
    log.append(f"{weapon_id}: saved: " + "; ".join(f"{p} {old} -> {new}" for p, old, new in changes))

unreal.log("[apply_weapon_tuning] " + (" | ".join(log) if log else "every weapon asset matches weapons_tuning.json"))
