"""Character single-slot pipeline: which body meshes does the game really use? (Unreal, READ-ONLY, saves nothing)

    UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
    with CODEX_PIPELINE_ARGS='["--out", "<json>"]'

Reads BP_CodexTacticsGameMode (OperativeBlueprint + Squad Roster: member Blueprint / body mesh), every operative and enemy
Blueprint under /Game/Characters (mesh component, override materials, anim class, the enemy LookVariants: meshes x material
sets) and writes one JSON: Blueprints -> meshes, and per distinct mesh: slots, materials, LODs, vertices, sections.
"""
import argparse
import json
import os
import sys

import unreal


def parse():
    argv = sys.argv[1:]
    if not argv and os.environ.get("CODEX_PIPELINE_ARGS"):
        argv = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--root", default="/Game/Characters")
    p.add_argument("--game-mode", default="/Game/Blueprints/BP_CodexTacticsGameMode")
    return p.parse_args(argv)


def path(o):
    return o.get_path_name() if o else None


def soft(v):
    """Soft object / class reference -> path string ('' when unset)."""
    if v is None:
        return None
    for conv in ("conv_soft_object_reference_to_string", "conv_soft_class_reference_to_string"):
        try:
            s = str(getattr(unreal.SystemLibrary, conv)(v))
            return s or None
        except Exception:
            continue
    if isinstance(v, unreal.Object):
        return v.get_path_name()
    return str(v) or None


def mesh_info(mesh, sms):
    lods = sms.get_lod_count(mesh)
    return {
        "path": path(mesh),
        "skeleton": path(mesh.skeleton),
        "physics_asset": path(mesh.physics_asset),
        "slots": [{"name": str(m.get_editor_property("material_slot_name")),
                   "material": path(m.get_editor_property("material_interface"))} for m in mesh.materials],
        "lods": [{"lod": i, "verts": sms.get_num_verts(mesh, i), "sections": sms.get_num_sections(mesh, i)} for i in range(lods)],
    }


def bp_info(bp_path, meshes, sms):
    bp = unreal.load_asset(bp_path)
    if not bp:
        return {"error": "missing"}
    cls = bp.generated_class()
    cdo = unreal.get_default_object(cls)
    comp = cdo.get_editor_property("mesh") if hasattr(cdo, "get_editor_property") else None
    info = {"class": cls.get_path_name()}
    if comp:
        mesh = comp.get_editor_property("skeletal_mesh_asset")
        info["mesh"] = path(mesh)
        info["anim_class"] = path(comp.get_editor_property("anim_class"))
        info["override_materials"] = [path(m) for m in comp.get_editor_property("override_materials")]
        info["mesh_relative_transform"] = str(comp.get_editor_property("relative_location"))
        if mesh:
            meshes.setdefault(path(mesh), mesh_info(mesh, sms))
    try:
        look = cdo.get_editor_property("look_variants")
        vm = [m for m in look.get_editor_property("meshes") if m]
        info["look_variant_meshes"] = [path(m) for m in vm]
        for m in vm:
            meshes.setdefault(path(m), mesh_info(m, sms))
        info["look_variant_material_sets"] = [
            {str(k): path(v) for k, v in s.get_editor_property("slot_materials").items()}
            for s in look.get_editor_property("material_sets")]
    except Exception:
        pass
    try:
        info["archetype"] = str(cdo.get_editor_property("archetype"))
    except Exception:
        pass
    return info


def main():
    args = parse()
    sms = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = {"game_mode": {}, "blueprints": {}, "meshes": {}}
    gm = unreal.load_asset(args.game_mode)
    if gm:
        cdo = unreal.get_default_object(gm.generated_class())
        out["game_mode"]["operative_blueprint"] = soft(cdo.get_editor_property("operative_blueprint"))
        roster = []
        for e in cdo.get_editor_property("squad_roster"):
            body = e.get_editor_property("body_mesh")
            member_bp = e.get_editor_property("member_blueprint")
            roster.append({"name": str(e.get_editor_property("display_name")), "body_mesh": soft(body),
                           "member_blueprint": soft(member_bp)})
            if body and soft(body):
                m = unreal.load_asset(soft(body))
                if m:
                    out["meshes"].setdefault(path(m), mesh_info(m, sms))
        out["game_mode"]["squad_roster"] = roster
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")],
                          package_paths=[args.root], recursive_paths=True)
    for ad in reg.get_assets(flt):
        name = str(ad.asset_name)
        if not (name.startswith("BP_Operative") or name.startswith("BP_Enemy")):
            continue
        p = str(ad.package_name)
        out["blueprints"][p] = bp_info(p, out["meshes"], sms)
    for b in [r.get("member_blueprint") for r in out["game_mode"].get("squad_roster", [])] + \
            [out["game_mode"].get("operative_blueprint")]:
        if b and b.split(".")[0] not in out["blueprints"]:
            out["blueprints"][b.split(".")[0]] = bp_info(b.split(".")[0], out["meshes"], sms)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    unreal.log("PIPELINE_ROSTER_DONE %d blueprints, %d meshes" % (len(out["blueprints"]), len(out["meshes"])))


main()
