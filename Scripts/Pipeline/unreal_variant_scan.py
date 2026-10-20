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
    """Path with every number replaced (ordinals too) and the master / instance naming dropped (MA_x_1st, M_x_2nd_Inst,
    MI_x_Inst): materials of the same slot in different sets share it."""
    folder, _, name = path.rpartition("/")
    name = name.split(".")[0]
    name = re.sub(r"^(MA|MI|M)_", "", name)
    name = re.sub(r"_Inst$", "", name)
    s = ORDINAL.sub("#", folder + "/" + name)
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
        # the packs of the slot materials (a mesh moved into /Game/Characters keeps its pack's materials)
        roots = sorted({pack_root(m.get_path_name()) for _, m in slots if m} | {pack_root(mesh.get_path_name())})
        root = ", ".join(roots)
        flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant"),
                                           unreal.TopLevelAssetPath("/Script/Engine", "Material")],
                              package_paths=roots, recursive_paths=True)
        by_sig = {}
        all_paths = []
        for ad in reg.get_assets(flt):
            p = str(ad.package_name) + "." + str(ad.asset_name)
            by_sig.setdefault(signature(p), []).append(p)
            all_paths.append(p)
        variants = {}
        per_slot = {}
        for slot, mat in slots:
            own = mat.get_path_name() if mat else None
            if not own:
                continue
            cands = set(by_sig.get(signature(own), [own]))
            # "<Material>_Inst_2nd" style colour instances of the slot material (Post_Apo_Survivor: hoodie / pants)
            base_name = own.split(".")[-1]
            cands |= {c for c in all_paths if c.split(".")[-1].startswith(base_name + "_Inst")}
            cands = sorted(cands)
            per_slot[slot] = cands
            own_nums = numbers(own)
            own_name = own.split(".")[-1]

            def style(path):
                # the same naming style as the material the mesh uses wins (MI_x_Inst over the pack's M_x master)
                name = path.split(".")[-1]
                return (name.split("_")[0] == own_name.split("_")[0]) + (name.endswith("_Inst") == own_name.endswith("_Inst"))

            for c in cands:
                nums = numbers(c)
                # the variant key = the numbers that differ from the mesh's own material (extra trailing numbers count)
                diff = [n for n, o in zip(nums, own_nums) if n != o] + list(nums[len(own_nums):])
                key = "_".join(diff) or "own"
                if key == "own":
                    continue  # the mesh's own look = its own materials
                current = variants.setdefault(key, {}).get(slot)
                if current is None or style(c) > style(current):
                    variants[key][slot] = c
            variants.setdefault("own", {})[slot] = own
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
