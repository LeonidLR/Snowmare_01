"""User decision 2026-10-01: the Godot panic (disabled there) is on in Unreal, but milder:

  - at most ONE squad member panics at a time (panic_max_panicked_members 2 -> 1);
  - the core squad (commander, engineer, medic) resists panic much harder: a third of the stress gain, faster calming,
    wounds / cold scare them later, monsters only up close;
  - the incidental members (the recruit Susanin) keep an ordinary susceptibility (Godot gave Susanin 0.1 stress gain).

Writes DA_GameBalanceConfig (Unreal is the master copy of the balance) and logs every old -> new value to
Saved/Logs/tune_panic_balance.txt. Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import os
import unreal

ASSET = "/Game/Data/Balance/DA_GameBalanceConfig"
VALUES = {
    "panic_max_panicked_members": 1,
    # Core squad: sharply more resistant.
    "commander_stress_gain_multiplier": 0.3, "commander_panic_recovery_rate": 9.0,
    "commander_panic_hp_threshold": 0.2, "commander_panic_cold_threshold": 0.85, "commander_panic_monster_threat_distance": 5.0,
    "engineer_stress_gain_multiplier": 0.4, "engineer_panic_recovery_rate": 8.0,
    "engineer_panic_hp_threshold": 0.25, "engineer_panic_cold_threshold": 0.8, "engineer_panic_monster_threat_distance": 5.5,
    "medic_stress_gain_multiplier": 0.35, "medic_panic_recovery_rate": 8.5,
    "medic_panic_hp_threshold": 0.22, "medic_panic_cold_threshold": 0.82, "medic_panic_monster_threat_distance": 5.0,
    # The recruit: an ordinary person, no special resistance.
    "susanin_stress_gain_multiplier": 1.0, "susanin_panic_recovery_mult": 1.0,
}

asset = unreal.EditorAssetLibrary.load_asset(ASSET)
log = []
for key, value in VALUES.items():
    old = asset.get_number(key, float("nan"))
    asset.set_number(key, float(value))
    log.append(f"{key}: {old:g} -> {asset.get_number(key, float('nan')):g}")
saved = unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
log.append(f"saved={saved}")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "tune_panic_balance.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
