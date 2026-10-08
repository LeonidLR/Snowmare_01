"""Replaces the Russian display strings stored on actors of /Game/Maps/L_MovementTest with English (user decision 2026-10-08).

Only these string properties are touched, matched by the current (Russian) value, everything else in the map stays:
  AInteractableActor.display_name, ACameraZoneVolume.zone_name / enter_message / exit_message, AEnemySpawnPoint.spawn_lane.
NarrativeElementActor title / content / source are NOT changed (the narrative manifest covers them) but are listed.
Spawn lanes: SpawnLaneRules matches "North gate" / "West flank" / "East flank" / "Far perimeter" (case-insensitive substring),
so the English lane labels below keep the waves spawning on the right lanes.

Idempotent (already-English values are skipped). Set CODEX_MAP_DRY=1 to print without saving.
Run headless with the editor CLOSED, after backing the map up:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path>" -unattended -nullrhi
Result: Saved/Logs/apply_english_map_names.txt
"""
import os
import re
import unreal

MAP = "/Game/Maps/L_MovementTest"
DRY = os.environ.get("CODEX_MAP_DRY") == "1"
LOG = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "apply_english_map_names.txt")
CYR = re.compile("[Ѐ-ӿ]")

# Russian (current value) -> English. Lane labels must stay matchable by SpawnLaneRules.
NAMES = {
    "Армейский ящик снабжения (КПП)": "Army Supply Crate (Checkpoint)",
    "Заминированный ящик аванпоста": "Booby-Trapped Outpost Crate",
    "📦 Армейский ящик снабжения (КПП)": "📦 Army Supply Crate (Checkpoint)",
    "📦 Заминированный ящик аванпоста": "📦 Booby-Trapped Outpost Crate",
    "Брошенный БМП-2": "Abandoned APC",
    "Пустая канистра": "Empty Fuel Canister",
    "Резервный генератор": "Backup Diesel Generator",
    "Пульт управления воротами": "Gate Control Terminal",
    "Северные ворота": "North gate",
    "Комендатура КПП": "Checkpoint HQ",
    "Сектор наблюдения 01": "Observation Sector 01",
    "Дальний периметр": "Far perimeter",
    "Левый фланг (Прорыв)": "West flank (Breach)",
    "Правый фланг": "East flank",
}
LANES = {
    "Северные ворота": "North gate",
    "Левый фланг (Прорыв)": "West flank (Breach)",
    "Левый фланг": "West flank",
    "Правый фланг": "East flank",
    "Дальний периметр": "Far perimeter",
}
TEXT_PROPS = {"AInteractableActor": ["display_name"], "ACameraZoneVolume": ["zone_name", "enter_message", "exit_message"]}
lines = []


def log(text):
    lines.append(text)
    unreal.log(text)


def text_of(value):
    return str(value)


unreal.EditorLoadingAndSavingUtils.load_map(MAP)
changed = 0
leftover = []
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    cls = actor.get_class().get_name()
    label = actor.get_actor_label()
    props = []
    if isinstance(actor, unreal.InteractableActor):
        props += ["display_name"]
    if cls == "CameraZoneVolume":
        props += ["zone_name", "enter_message", "exit_message"]
    for prop in props:
        try:
            current = text_of(actor.get_editor_property(prop))
        except Exception:
            continue
        if not CYR.search(current):
            continue
        new = NAMES.get(current)
        if new is None:
            leftover.append("%s [%s].%s = %s" % (label, cls, prop, current))
            continue
        log("%s [%s].%s: '%s' -> '%s'" % (label, cls, prop, current, new))
        if not DRY:
            actor.set_editor_property(prop, unreal.Text(new))
        changed += 1
    if cls == "EnemySpawnPoint":
        try:
            lane = str(actor.get_editor_property("spawn_lane"))
        except Exception:
            lane = ""
        if CYR.search(lane):
            new = LANES.get(lane.strip())
            if new is None:
                leftover.append("%s [%s].spawn_lane = %s" % (label, cls, lane))
            else:
                log("%s [%s].spawn_lane: '%s' -> '%s'" % (label, cls, lane, new))
                if not DRY:
                    actor.set_editor_property("spawn_lane", new)
                changed += 1
    if cls == "NarrativeElementActor":
        for prop in ("title", "content_text", "author_or_source"):
            try:
                value = text_of(actor.get_editor_property(prop))
            except Exception:
                continue
            if CYR.search(value):
                leftover.append("(left on purpose) %s [%s].%s = %s" % (label, cls, prop, value))

log("changed: %d%s" % (changed, " (dry run)" if DRY else ""))
for item in leftover:
    log("LEFT: " + item)
if changed and not DRY:
    unreal.EditorLevelLibrary.save_current_level()
    log("map saved")
os.makedirs(os.path.dirname(LOG), exist_ok=True)
with open(LOG, "w", encoding="utf-8") as fh:
    fh.write("\n".join(lines))
