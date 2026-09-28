"""Imports the Godot dialogue resources (DialogueSequence .tres) into UDialogueSequenceAsset data assets.
Source (read only): <Godot repo>/resources/dialogue_intro.tres, dialogue_prep.tres, dialogue_wave_rest.tres,
post_combat_dialogue.tres, dialogue_susanin_recruitment.tres. Target: /Game/Data/Dialogues/DA_<name>.
Re-run after the Godot texts change; existing assets are updated in place.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_dialogues.txt
"""
import os
import re
import unreal

GODOT_RESOURCES = r"C:\Users\Zephyrus15Duo\Documents\Codex\godot-test-01\resources"
TARGET = "/Game/Data/Dialogues"
SOURCES = {
    "DA_DialogueIntro": "dialogue_intro.tres",
    "DA_DialoguePrep": "dialogue_prep.tres",
    "DA_DialogueWaveRest": "dialogue_wave_rest.tres",
    "DA_DialogueVictory": "post_combat_dialogue.tres",
    "DA_DialogueSusaninRecruitment": "dialogue_susanin_recruitment.tres",
}

STRING = r'"((?:[^"\\]|\\.)*)"'


def unescape(value):
    out, i = [], 0
    while i < len(value):
        c = value[i]
        if c == "\\" and i + 1 < len(value):
            n = value[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(n, n))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_block(body):
    props = {}
    for m in re.finditer(r'^(\w+) = (' + STRING + r'|[^\n]+)$', body, re.M | re.S):
        key, raw, string = m.group(1), m.group(2), m.group(3)
        props[key] = unescape(string) if string is not None and raw.startswith('"') else raw.strip()
    return props


def parse_tres(path):
    text = open(path, encoding="utf-8").read().replace("\r\n", "\n")
    blocks = re.split(r'^\[', text, flags=re.M)
    subs, resource = {}, {}
    for block in blocks:
        header, _, body = block.partition("]\n")
        if header.startswith("sub_resource"):
            sub_id = re.search(r'id="([^"]+)"', header).group(1)
            subs[sub_id] = parse_block(body)
        elif header.startswith("resource"):
            resource = parse_block(body)
    order = re.findall(r'SubResource\("([^"]+)"\)', resource.get("lines", ""))
    return resource, [subs[i] for i in order if i in subs]


log = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
for asset_name, file_name in SOURCES.items():
    resource, lines = parse_tres(os.path.join(GODOT_RESOURCES, file_name))
    full = TARGET + "/" + asset_name
    asset = unreal.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else \
        tools.create_asset(asset_name, TARGET, unreal.DialogueSequenceAsset, unreal.DataAssetFactory())
    asset.set_editor_property("title", resource.get("title", ""))
    asset.set_editor_property("custom_finish_button_text", resource.get("custom_finish_button_text", ""))
    asset.set_editor_property("is_recruitment_dialogue", resource.get("is_recruitment_dialogue", "false") == "true")
    out = []
    for props in lines:
        line = unreal.DialogueLine()
        line.set_editor_property("speaker_name", props.get("speaker_name", "Командир"))
        line.set_editor_property("text", props.get("text", ""))
        line.set_editor_property("delay_after", float(props.get("delay_after", "4.0")))
        out.append(line)
    asset.set_editor_property("lines", out)
    saved = unreal.EditorAssetLibrary.save_asset(full)
    log.append("%s <- %s: %d lines, saved=%s" % (full, file_name, len(out), saved))

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_dialogues.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
