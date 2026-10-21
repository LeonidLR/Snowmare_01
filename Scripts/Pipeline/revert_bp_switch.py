"""Restores the Blueprints switched by switch_bp_bodies.py from ArtPipeline/Characters/bp_switch_revert.json.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=Scripts/Pipeline/revert_bp_switch.py -unattended
        -nullrhi -stdout -FullStdOutLogOutput
    env CODEX_BP_REVERT = "all" or a comma list of Blueprint paths (default: all)

Restores per Blueprint: the body mesh, OverrideMaterials, the enemy look variants (Meshes / MaterialSets / Bodies) and the
operatives' SquadOutfits exactly as recorded before the first switch. A full byte-level fallback: the .uasset backups the
switch run copied to the session scratchpad (opt_bp_backup_<time>/ with sha1 sums).
"""
import json
import os

import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REVERT = os.path.join(ROOT, "ArtPipeline", "Characters", "bp_switch_revert.json")


def obj(path):
    if not path:
        return None
    o = unreal.load_asset(path)
    if o is None:
        raise RuntimeError("missing asset %s" % path)
    return o


def main():
    data = json.load(open(REVERT, encoding="utf-8"))
    wanted = os.environ.get("CODEX_BP_REVERT", "all")
    paths = list(data) if wanted in ("", "all") else [w.strip() for w in wanted.split(",") if w.strip()]
    for bp_path in paths:
        old = data[bp_path]
        bp = obj(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        mesh = cdo.get_editor_property("mesh")
        mesh.set_editor_property("skeletal_mesh_asset", obj(old["mesh"]))
        mesh.set_editor_property("override_materials", [obj(m) for m in old["override_materials"]])
        if "look_variants" in old:
            lv = cdo.get_editor_property("look_variants")
            lv.set_editor_property("meshes", [obj(m) for m in old["look_variants"]["meshes"]])
            sets = []
            for s in old["look_variants"]["material_sets"]:
                ms = unreal.EnemyMaterialSet()
                ms.set_editor_property("slot_materials", {unreal.Name(k): obj(v) for k, v in s.items()})
                sets.append(ms)
            lv.set_editor_property("material_sets", sets)
            bodies = []
            for b in old["look_variants"].get("bodies", []):
                lb = unreal.EnemyLookBody()
                lb.set_editor_property("mesh", obj(b["mesh"]))
                lb.set_editor_property("sets", [obj(m) for m in b["sets"]])
                bodies.append(lb)
            lv.set_editor_property("bodies", bodies)
            cdo.set_editor_property("look_variants", lv)
        if "squad_outfits" in old:
            outfits = []
            for o in old["squad_outfits"]:
                of = unreal.OperativeOutfit()
                of.set_editor_property("slot_materials", {unreal.Name(k): obj(v) for k, v in o.items()})
                outfits.append(of)
            cdo.set_editor_property("squad_outfits", outfits)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        if not unreal.EditorAssetLibrary.save_asset(bp_path, only_if_is_dirty=False):
            raise RuntimeError("could not save %s" % bp_path)
        unreal.log("BP_REVERT %s -> %s" % (bp_path, old["mesh"]))
    unreal.log("BP_REVERT_DONE %d" % len(paths))


main()
