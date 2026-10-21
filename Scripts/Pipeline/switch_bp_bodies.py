"""Switches character Blueprints to the single-slot optimized bodies (user 2026-10-21).

Run headless with the editor closed:
    UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=Scripts/Pipeline/switch_bp_bodies.py -unattended
        -nullrhi -stdout -FullStdOutLogOutput
    env CODEX_BP_SWITCH = "<batch>[:dry]"  (batch = operatives_enemies1 | enemies2; ":dry" = report only, nothing saved)

Per Blueprint only the body mesh, its OverrideMaterials, the enemy look variants (FEnemyLookVariants: Meshes / MaterialSets
cleared, Bodies = each optimized body with ITS colour sets) and the operatives' SquadOutfits change; skeleton, physics asset
and ABP stay (the optimized bodies are on the original skeleton / physics asset). Before saving, the old values are written
to ArtPipeline/Characters/bp_switch_revert.json (merged per Blueprint, first value kept), which
Scripts/Pipeline/revert_bp_switch.py restores.
"""
import json
import os

import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REVERT = os.path.join(ROOT, "ArtPipeline", "Characters", "bp_switch_revert.json")
OPS = "/Game/Characters/Operatives/Optimized"
ENS = "/Game/Characters/Enemies/Optimized"


def a(path):
    return "%s.%s" % (path, path.rsplit("/", 1)[-1])


def body(folder, asset, sets):
    return {"mesh": a("%s/%s/SK_%s_1slot" % (folder, asset, asset)),
            "sets": [a("%s/%s/MI_%s%s" % (folder, asset, asset, s)) for s in sets]}


# Blueprint -> target. "default" = (mesh, slot-0 MI) the Blueprint shows in the editor; "bodies" = enemy look bodies;
# "outfits" = operative SquadOutfits (slot name -> MI per squad index); "clear_outfits" = an empty list of its own (a child
# Blueprint must not inherit the parent's M_Body outfits: they are another body's atlas).
BATCHES = {
    "operatives_enemies1": [
        ("/Game/Characters/Operatives/BP_Operative", {
            "default": body(OPS, "Post_Apo_Survivor_a", [""]),
            # today: hoodie / pants 1st, 2nd, 3rd per squad member -> the same looks baked as Set1 / Set2 / Set3
            "outfits": [{"M_Body": a(OPS + "/Post_Apo_Survivor_a/MI_Post_Apo_Survivor_a")},
                        {"M_Body": a(OPS + "/Post_Apo_Survivor_a/MI_Post_Apo_Survivor_a_Set2")},
                        {"M_Body": a(OPS + "/Post_Apo_Survivor_a/MI_Post_Apo_Survivor_a_Set3")}]}),
        ("/Game/Characters/Operatives/BP_Operative_FemaleSoldier", {
            "default": body(OPS, "Female_Soldier", [""]), "clear_outfits": True}),
        ("/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten", {
            "default": body(ENS, "Frostbitten", ["_Set4"]),
            "bodies": [body(ENS, "Frostbitten", ["_Set1", "_Set2", "_Set3", "_Set4"])]}),
        ("/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie", {
            "default": body(ENS, "MilitaryZombie_1", ["_Set1"]),
            "bodies": [body(ENS, "MilitaryZombie_%d" % n, ["_Set1", "_Set2", "_Set3"]) for n in (1, 2, 3)]}),
        ("/Game/Characters/Enemies/Hound/BP_Enemy_Hound", {
            "default": body(ENS, "Hound", ["_Set1"]),
            "bodies": [body(ENS, "Hound", ["_Set1", "_Set2", "_Set3", "_Set4", "_Set5"])]}),
    ],
    "enemies2": [
        ("/Game/Characters/Enemies/Brute/BP_Enemy_Brute", {
            "default": body(ENS, "Brute", ["_Set2"]),
            "bodies": [body(ENS, "Brute", ["_Set1", "_Set2", "_Set3", "_Set4"])]}),
        ("/Game/Characters/Enemies/Cutter/BP_Enemy_Cutter", {
            "default": body(ENS, "Cutter", ["_Set3"]),
            "bodies": [body(ENS, "Cutter", ["_Set1", "_Set2", "_Set3", "_Set4"])]}),
    ],
}


def p(o):
    return o.get_path_name() if o else None


def load(path):
    obj = unreal.load_asset(path)
    if obj is None:
        raise RuntimeError("missing asset %s" % path)
    return obj


def snapshot(cdo):
    """The values this switch touches, as asset paths (the revert map)."""
    mesh = cdo.get_editor_property("mesh")
    out = {"mesh": p(mesh.get_editor_property("skeletal_mesh_asset")),
           "override_materials": [p(m) for m in mesh.get_editor_property("override_materials")],
           "skeleton": p(mesh.get_editor_property("skeletal_mesh_asset").get_editor_property("skeleton"))
           if mesh.get_editor_property("skeletal_mesh_asset") else None,
           "anim_class": p(mesh.get_editor_property("anim_class"))}
    if isinstance(cdo, unreal.EnemyCharacter):
        lv = cdo.get_editor_property("look_variants")
        out["look_variants"] = {
            "meshes": [p(m) for m in lv.get_editor_property("meshes")],
            "material_sets": [{str(k): p(v) for k, v in s.get_editor_property("slot_materials").items()}
                              for s in lv.get_editor_property("material_sets")],
            "bodies": [{"mesh": p(b.get_editor_property("mesh")), "sets": [p(m) for m in b.get_editor_property("sets")]}
                       for b in lv.get_editor_property("bodies")]}
    if isinstance(cdo, unreal.OperativeCharacter):
        out["squad_outfits"] = [{str(k): p(v) for k, v in s.get_editor_property("slot_materials").items()}
                                for s in cdo.get_editor_property("squad_outfits")]
    return out


def apply(cdo, target):
    mesh_comp = cdo.get_editor_property("mesh")
    old_mesh = mesh_comp.get_editor_property("skeletal_mesh_asset")
    new_mesh = load(target["default"]["mesh"])
    if old_mesh and (old_mesh.get_editor_property("skeleton") != new_mesh.get_editor_property("skeleton")):
        raise RuntimeError("%s: %s is on another skeleton than %s" % (cdo.get_name(), p(new_mesh), p(old_mesh)))
    if old_mesh and old_mesh.get_editor_property("physics_asset") != new_mesh.get_editor_property("physics_asset"):
        raise RuntimeError("%s: %s has another physics asset than %s" % (cdo.get_name(), p(new_mesh), p(old_mesh)))
    mesh_comp.set_editor_property("skeletal_mesh_asset", new_mesh)
    # the 1-slot body carries its default colour set in its own slot: no overrides (a child Blueprint with another body,
    # e.g. BP_Enemy_FrozenPMC_Fighter under BP_Enemy_Frostbitten, must not inherit this body's MI)
    mesh_comp.set_editor_property("override_materials", [])
    if p(new_mesh.get_editor_property("materials")[0].get_editor_property("material_interface")) != target["default"]["sets"][0]:
        raise RuntimeError("%s: %s slot 0 is not %s" % (cdo.get_name(), p(new_mesh), target["default"]["sets"][0]))
    if "bodies" in target:
        lv = cdo.get_editor_property("look_variants")
        bodies = []
        for b in target["bodies"]:
            lb = unreal.EnemyLookBody()
            m = load(b["mesh"])
            if m.get_editor_property("skeleton") != new_mesh.get_editor_property("skeleton"):
                raise RuntimeError("%s: body %s on another skeleton" % (cdo.get_name(), b["mesh"]))
            lb.set_editor_property("mesh", m)
            lb.set_editor_property("sets", [load(s) for s in b["sets"]])
            bodies.append(lb)
        lv.set_editor_property("meshes", [])
        lv.set_editor_property("material_sets", [])
        lv.set_editor_property("bodies", bodies)
        cdo.set_editor_property("look_variants", lv)
    if "outfits" in target:
        outfits = []
        for o in target["outfits"]:
            of = unreal.OperativeOutfit()
            of.set_editor_property("slot_materials", {unreal.Name(k): load(v) for k, v in o.items()})
            outfits.append(of)
        cdo.set_editor_property("squad_outfits", outfits)
    if target.get("clear_outfits"):
        cdo.set_editor_property("squad_outfits", [])


def main():
    spec = os.environ.get("CODEX_BP_SWITCH", "")
    batch, _, flag = spec.partition(":")
    if batch not in BATCHES:
        raise RuntimeError("CODEX_BP_SWITCH must be one of %s (optionally ':dry')" % list(BATCHES))
    dry = flag == "dry"
    revert = json.load(open(REVERT, encoding="utf-8")) if os.path.exists(REVERT) else {}
    report = {}
    for bp_path, target in BATCHES[batch]:
        bp = load(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        before = snapshot(cdo)
        report[bp_path] = {"before": before}
        if dry:
            continue
        revert.setdefault(bp_path, before)  # the first switch keeps the original values
        apply(cdo, target)
        report[bp_path]["after"] = snapshot(cdo)
    if not dry:
        os.makedirs(os.path.dirname(REVERT), exist_ok=True)
        with open(REVERT, "w", encoding="utf-8") as f:
            json.dump(revert, f, indent=1)
        for bp_path, _target in BATCHES[batch]:
            bp = load(bp_path)
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            if not unreal.EditorAssetLibrary.save_asset(bp_path, only_if_is_dirty=False):
                raise RuntimeError("could not save %s" % bp_path)
        # re-read after compile + save: the values must have survived
        for bp_path, _target in BATCHES[batch]:
            cdo = unreal.get_default_object(load(bp_path).generated_class())
            report[bp_path]["saved"] = snapshot(cdo)
    unreal.log("BP_SWITCH_REPORT %s" % json.dumps(report))
    unreal.log("BP_SWITCH_DONE %s %s" % (batch, "dry" if dry else "saved"))


main()
