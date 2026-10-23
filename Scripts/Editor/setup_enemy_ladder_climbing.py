"""Marks the climbing enemy Blueprints (docs/design/LADDERS.md 7.5, user decision 2026-10-22: non-mutant humans climb ladders).

The flag is AEnemyCharacter::bCanClimbLadders (class default false). The Marksman gets it from its C++ class
(AMarksmanEnemyCharacter); the zombified humans are Blueprint children of BP_Enemy_Frostbitten (a mutant: it never climbs), so
the flag is set on their own Blueprints:
  /Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter   -> bCanClimbLadders = True
  /Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie            -> bCanClimbLadders = True
That is the ONLY property this script writes (class defaults of the two Blueprints; nothing else, no ABP / mesh / clip / skeleton).
The other enemy Blueprints are only read and must stay False (Brute, Cutter, Frostbitten, Hound) / True (Marksman).

Backup first: Saved/Backups/enemy_ladder_<timestamp>/Content/... (restore = copy the .uasset back with the editor closed).
CODEX_LADDER_DRYRUN=1 = read and report only (no backup, no save).

Run with the editor of THIS checkout closed (commandlet):
  UnrealEditor-Cmd.exe <project>.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
Result: Saved/Logs/setup_enemy_ladder_climbing.txt
"""
import os
import shutil
import time

import unreal

DRY = os.environ.get("CODEX_LADDER_DRYRUN") == "1"
CLIMBERS = [
    "/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter",
    "/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie",
]
# Expected value of every other enemy Blueprint (read only).
OTHERS = {
    "/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman": True,
    "/Game/Characters/Enemies/Brute/BP_Enemy_Brute": False,
    "/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter": False,
    "/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten": False,
    "/Game/Characters/Enemies/Hound/BP_Enemy_Hound": False,
}
library = unreal.EditorAssetLibrary
log = []


def say(text):
    log.append(text)
    unreal.log("setup_enemy_ladder_climbing: " + text)


def asset_name(path):
    return path.rsplit("/", 1)[1]


def cdo_of(path):
    cls = unreal.load_object(None, path + "." + asset_name(path) + "_C")
    return unreal.get_default_object(cls) if cls else None


def package_file(path):
    rel = path[len("/Game/"):] + ".uasset"
    return os.path.join(unreal.Paths.project_content_dir(), rel)


def backup(paths):
    stamp = time.strftime("%Y%m%d_%H%M%S")
    root = os.path.abspath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "Backups", "enemy_ladder_" + stamp, "Content"))
    for path in paths:
        src = os.path.abspath(package_file(path))
        dst = os.path.join(root, path[len("/Game/"):] + ".uasset")
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)
        say("backup %s -> %s" % (src, dst))
    return root


try:
    say("dry run (nothing saved)" if DRY else "apply")
    if not DRY:
        backup(CLIMBERS)
    for path in CLIMBERS:
        bp = library.load_asset(path)
        cdo = cdo_of(path)
        if not bp or not cdo:
            say("MISSING %s" % path)
            continue
        before = cdo.get_editor_property("can_climb_ladders")
        if before:
            say("%s: bCanClimbLadders already True (unchanged)" % asset_name(path))
            continue
        if DRY:
            say("%s: bCanClimbLadders False -> would set True" % asset_name(path))
            continue
        cdo.set_editor_property("can_climb_ladders", True)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        library.save_loaded_asset(bp, False)
        after = cdo_of(path).get_editor_property("can_climb_ladders")
        say("%s: bCanClimbLadders False -> %s (compiled, saved)" % (asset_name(path), after))
    bad = 0
    for path, expected in OTHERS.items():
        cdo = cdo_of(path)
        value = cdo.get_editor_property("can_climb_ladders") if cdo else None
        ok = value == expected
        bad += 0 if ok else 1
        say("%s %s: bCanClimbLadders %s (expected %s, read only)" % ("ok  " if ok else "FAIL", asset_name(path), value, expected))
    say("RESULT: %s" % ("PASS" if bad == 0 else "CHECK (%d)" % bad))
except Exception:
    import traceback
    say("ERROR " + traceback.format_exc())

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_enemy_ladder_climbing.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
