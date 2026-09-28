"""Imports the Godot weapons (resources/weapons/*.tres, WeaponData) into UWeaponDataAsset data assets
/Game/Data/Weapons/DA_Weapon_<weapon_id>. Defaults come from resources/weapon_data.gd (@export values), the .tres
values override them — so a field missing in a .tres gets the Godot default, never a UE one.
Units: attack_range metres -> AttackRangeCm (x100). Enums are mapped by NAME (Godot order differs from UE).
Re-run after the Godot weapons change; existing assets are updated in place.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_weapons.txt
"""
import glob
import os
import re
import unreal

GODOT = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01"
TARGET = "/Game/Data/Weapons"
log = []


def parse_value(raw, enums):
    raw = raw.strip()
    if raw.startswith('"'):
        return raw[1:-1].replace('\\"', '"').replace("\\n", "\n")
    if raw in ("true", "false"):
        return raw == "true"
    m = re.match(r'Color\(([^)]*)\)', raw)
    if m:
        return [float(v) for v in m.group(1).split(",")]
    m = re.match(r'(?:Array\[\w+\]\()?\[([^\]]*)\]\)?', raw)
    if m:
        return [float(v) for v in m.group(1).split(",") if v.strip()]
    m = re.match(r'(\w+)\.(\w+)$', raw)
    if m and m.group(1) in enums:
        return enums[m.group(1)].index(m.group(2))
    try:
        return int(raw)
    except ValueError:
        return float(raw)


# Godot class: enums and @export defaults.
source = open(os.path.join(GODOT, "resources", "weapon_data.gd"), encoding="utf-8").read()
enums = {}
for m in re.finditer(r'enum (\w+)\s*\{([^}]*)\}', source, re.S):
    names = [re.sub(r'#.*', '', part).strip() for part in m.group(2).split("\n")]
    enums[m.group(1)] = [n.split(",")[0].strip() for n in names if n.split(",")[0].strip()]
defaults = {}
for m in re.finditer(r'^@export var (\w+)\s*:\s*[\w\[\]]+\s*=\s*([^#\n]+)', source, re.M):
    defaults[m.group(1)] = parse_value(m.group(2), enums)


def ue_enum(enum_class, godot_names, index, aliases=None):
    name = godot_names[int(index)]
    name = (aliases or {}).get(name, name)
    value = getattr(enum_class, name, None)
    if value is None:
        raise RuntimeError("no %s.%s" % (enum_class.__name__, name))
    return value


tools = unreal.AssetToolsHelpers.get_asset_tools()
for path in sorted(glob.glob(os.path.join(GODOT, "resources", "weapons", "*.tres"))):
    text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    values = dict(defaults)
    body = text.split("[resource]", 1)[1]
    for m in re.finditer(r'^(\w+) = (.+)$', body, re.M):
        if m.group(1) != "script":
            values[m.group(1)] = parse_value(m.group(2), enums)

    name = "DA_Weapon_" + values["weapon_id"]
    full = TARGET + "/" + name
    asset = unreal.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else \
        tools.create_asset(name, TARGET, unreal.WeaponDataAsset, unreal.DataAssetFactory())
    try:
        asset.set_editor_property("weapon_id", values["weapon_id"])
        asset.set_editor_property("weapon_name", unreal.Text(values["weapon_name"]))
        asset.set_editor_property("damage_type", ue_enum(unreal.DamageType, enums["DamageType"], values["damage_type"]))
        asset.set_editor_property("base_damage", float(values["base_damage"]))
        asset.set_editor_property("attack_range_cm", float(values["attack_range"]) * 100.0)
        asset.set_editor_property("fire_rate", float(values["fire_rate"]))
        asset.set_editor_property("armor_penetration", float(values["armor_penetration"]))
        asset.set_editor_property("uses_ammo", bool(values["uses_ammo"]))
        asset.set_editor_property("max_clip_size", int(values["max_clip_size"]))
        asset.set_editor_property("default_reserve_ammo", int(values["default_reserve_ammo"]))
        asset.set_editor_property("reload_time", float(values["reload_time"]))
        asset.set_editor_property("status_effect", ue_enum(unreal.StatusEffect, enums["StatusEffectType"], values["status_effect"]))
        asset.set_editor_property("status_duration", float(values["status_duration"]))
        asset.set_editor_property("status_tick_damage", float(values["status_tick_damage"]))
        asset.set_editor_property("self_cold_generation", float(values["self_cold_generation"]))
        asset.set_editor_property("self_warmth_generation", float(values["self_warmth_generation"]))
        c = values["tracer_color"] + [1.0] * (4 - len(values["tracer_color"]))
        asset.set_editor_property("tracer_color", unreal.LinearColor(c[0], c[1], c[2], c[3]))
        asset.set_editor_property("attack_shape", ue_enum(unreal.AttackShape, enums["AttackShape"], values["attack_shape"],
                                                          {"RAYS_8": "RAYS8", "RAYS_4": "RAYS4"}))
        asset.set_editor_property("max_range_cells", int(values["max_range_cells"]))
        asset.set_editor_property("base_hit_chances", [float(v) for v in values["base_hit_chances"]])
        asset.set_editor_property("distance_damage_multipliers", [float(v) for v in values["distance_damage_multipliers"]])
        saved = unreal.EditorAssetLibrary.save_asset(full)
        log.append("%s <- %s saved=%s" % (full, os.path.basename(path), saved))
    except Exception as error:  # keep going, report the field
        log.append("FAILED %s: %s" % (full, error))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_weapons.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
