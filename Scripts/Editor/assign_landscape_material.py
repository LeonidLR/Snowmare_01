# Assigns the weather-driven landscape material to the Landscape of a map (environment §13; user request 2026-10-23: «on the Frozen Lake
# I added a landscape - it needs a material tied to the weather system»). Changes ONE property on every ALandscapeProxy in the map:
# LandscapeMaterial = /Game/VFX/Materials/MI_Landscape_Master. Nothing else is touched.
#
# Run with the user's editor CLOSED, under the build lock, as a commandlet:
#   UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="Scripts/Editor/assign_landscape_material.py" -unattended -nullrhi
# Environment:
#   CODEX_LS_MAP   map path (default /Game/Maps/L_Demo01_A_FrozenLake)
#   CODEX_LS_APPLY 1 = save the map; anything else = DRY RUN (reports what would change, saves nothing)
# Before saving it copies the .umap to Saved/Backups/landscape_material_<timestamp>/ with its sha1 in manifest.txt, and it writes a
# report (actors, old / new material, sha1 before / after) to Saved/Logs/assign_landscape_material.txt.
# Alternative without a script: select the Landscape actor in the editor, Details > Landscape > Landscape Material = MI_Landscape_Master.

import datetime
import hashlib
import os
import shutil

import unreal

MATERIAL = "/Game/VFX/Materials/MI_Landscape_Master"


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    map_path = os.environ.get("CODEX_LS_MAP", "/Game/Maps/L_Demo01_A_FrozenLake")
    apply = os.environ.get("CODEX_LS_APPLY", "") == "1"
    saved = unreal.Paths.project_saved_dir()
    report = []

    file_path = unreal.Paths.convert_relative_path_to_full(
        unreal.Paths.project_content_dir() + map_path.replace("/Game/", "", 1) + ".umap")
    report.append("map %s (%s), mode %s" % (map_path, file_path, "APPLY" if apply else "DRY RUN"))
    if not os.path.exists(file_path):
        report.append("ERROR: map file not found")
        return report
    report.append("sha1 before %s" % sha1(file_path))

    material = unreal.load_asset(MATERIAL)
    if not material:
        report.append("ERROR: %s not found" % MATERIAL)
        return report

    if not unreal.EditorLoadingAndSavingUtils.load_map(map_path):
        report.append("ERROR: could not load the map")
        return report
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    proxies = [a for a in actors if isinstance(a, unreal.LandscapeProxy)]
    if not proxies:
        report.append("ERROR: no Landscape / LandscapeProxy actor in the map - nothing changed")
        return report

    changed = 0
    for proxy in proxies:
        old = proxy.get_editor_property("landscape_material")
        old_path = old.get_path_name() if old else "None"
        same = old is not None and old.get_path_name() == material.get_path_name()
        report.append("%s (%s): landscape_material %s -> %s%s" % (proxy.get_actor_label(), proxy.get_class().get_name(), old_path,
                                                                  MATERIAL, " (already set)" if same else ""))
        if apply and not same:
            proxy.modify()
            proxy.set_editor_property("landscape_material", material)
            # Landscape proxies apply the material through PostEditChange (component material instances rebuild).
            unreal.LandscapeAuthoringLibrary.notify_landscape_material_changed(proxy) \
                if hasattr(unreal.LandscapeAuthoringLibrary, "notify_landscape_material_changed") else None
            changed += 1

    if apply and changed:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_dir = os.path.join(saved, "Backups", "landscape_material_" + stamp)
        os.makedirs(backup_dir, exist_ok=True)
        shutil.copy2(file_path, backup_dir)
        with open(os.path.join(backup_dir, "manifest.txt"), "w", encoding="utf-8") as f:
            f.write("%s  %s\n" % (sha1(file_path), os.path.basename(file_path)))
        report.append("backup %s" % backup_dir)
        unreal.EditorLoadingAndSavingUtils.save_current_level()
        report.append("saved; sha1 after %s" % sha1(file_path))
    report.append("%d proxy(ies) changed" % changed)
    return report


lines = main()
log_path = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "assign_landscape_material.txt")
with open(log_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
for line in lines:
    unreal.log("[assign_landscape_material] " + line)
