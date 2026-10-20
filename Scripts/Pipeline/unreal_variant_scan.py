"""Character single-slot pipeline: colour / texture-set variants a pack ships for a body mesh (Unreal, READ-ONLY).

    UnrealEditor-Cmd.exe <Project>.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
    with CODEX_PIPELINE_ARGS='["--out", "<json>", "--mesh", "/Game/...", "--mesh", "/Game/..."]'

For every slot material of the mesh, the pack's other materials whose path differs only in its numbers (set_1 -> set_2,
_1st -> _2nd, ..._A_3 -> ..._A_5) are its variants; materials that share the same numbers form one variant (a texture set).
A variant lists every slot: the variant's material where the pack has one, else the mesh's own. Nothing is loaded beyond the
asset registry and the mesh; nothing is saved.
"""
import argparse
import json
import os
import re
import sys

import unreal

ORDINAL = re.compile(r"(\d+)(st|nd|rd|th)\b", re.I)
DIGITS = re.compile(r"\d+")


def parse():
    argv = sys.argv[1:]
    if not argv and os.environ.get("CODEX_PIPELINE_ARGS"):
        argv = json.loads(os.environ["CODEX_PIPELINE_ARGS"])
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--mesh", action="append", required=True)
    return p.parse_args(argv)


def signature(path):
    """Path with every number replaced (ordinals too): materials of the same slot in different sets share it."""
    s = ORDINAL.sub("#", path)
    return DIGITS.sub("#", s)


def numbers(path):
    s = ORDINAL.sub(lambda m: m.group(1), path)
    return tuple(DIGITS.findall(s))


def pack_root(path):
    parts = path.split("/")
    return "/".join(parts[:3])  # /Game/<Pack>


def main():
    args = parse()
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = {}
    for mesh_path in args.mesh:
        mesh = unreal.load_asset(mesh_path)
        if not mesh:
            out[mesh_path] = {"error": "missing"}
            continue
        slots = [(str(m.get_editor_property("material_slot_name")), m.get_editor_property("material_interface"))
                 for m in mesh.materials]
        root = pack_root(mesh.get_path_name())
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant"),
                                           unreal.TopLevelAssetPath("/Script/Engine", "Material")],
                              package_paths=[root], recursive_paths=True)
        by_sig = {}
        for ad in reg.get_assets(flt):
            p = str(ad.package_name) + "." + str(ad.asset_name)
            by_sig.setdefault(signature(p), []).append(p)
        variants = {}
        per_slot = {}
        for slot, mat in slots:
            own = mat.get_path_name() if mat else None
            if not own:
                continue
            cands = sorted(by_sig.get(signature(own), [own]))
            per_slot[slot] = cands
            own_nums = numbers(own)
            for c in cands:
                nums = numbers(c)
                # the variant key = the numbers that differ from the mesh's own material
                key = "_".join(n for n, o in zip(nums, own_nums) if n != o) or "own"
                variants.setdefault(key, {})[slot] = c
        resolved = {}
        for key, mapping in variants.items():
            resolved[key] = {slot: mapping.get(slot) or (mat.get_path_name() if mat else None) for slot, mat in slots}
            resolved[key]["_covered_slots"] = sorted(mapping)
        out[mesh_path] = {"pack": root, "slots": [s for s, _ in slots], "per_slot_candidates": per_slot,
                          "variants": resolved}
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    unreal.log("PIPELINE_VARIANTS_DONE %s" % json.dumps({m: len(v.get("variants", {})) for m, v in out.items()}))


main()
