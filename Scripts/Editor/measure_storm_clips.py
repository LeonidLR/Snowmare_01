"""Read-only measurement of the storm clips of /Game/90+_Weather_Animations (game designer 2026-10-22: blizzard walk upper body).

Per clip: length, root motion, root / pelvis drift, the feet (a walk cycle swings them fore and aft in turn), the hands relative to
spine_03 (forward = the mesh's +Y for the pack's UE5 Manny; up = +Z). Nothing is saved.
Run (editor closed for this checkout):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path>" -unattended -nullrhi -nosplash
Result: Saved/Logs/measure_storm_clips.txt
"""
import os

import unreal

WORLD = unreal.AnimPoseSpaces.WORLD
PATHS = ["/Game/90+_Weather_Animations/Animations/NEW_version/Storm", "/Game/90+_Weather_Animations/Animations/OLD_version"]
EXTRA = os.environ.get("CODEX_STORM_EXTRA", "")  # optional ';'-separated asset paths to measure too (e.g. a retargeted copy)
MESH = os.environ.get("CODEX_STORM_MESH", "")    # optional mesh to evaluate on
log = []


def say(text):
    log.append(text)


def pose_at(clip, t, mesh=None):
    options = unreal.AnimPoseEvaluationOptions()
    if mesh:
        options.set_editor_property("optional_skeletal_mesh", mesh)
    length = clip.get_editor_property("sequence_length")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, min(max(t, 0.0), length - 0.001), options)


def g(p, b):
    return unreal.AnimPoseExtensions.get_bone_pose(p, b, WORLD).translation


def measure(clip, mesh=None):
    length = clip.get_editor_property("sequence_length")
    n = 16
    rows = []
    for i in range(n + 1):
        p = pose_at(clip, length * i / n, mesh)
        names = set(str(x) for x in p.get_bone_names())
        get = lambda b: g(p, b) if b in names else unreal.Vector(0, 0, 0)  # noqa: E731
        rows.append({b: get(b) for b in ("root", "pelvis", "spine_03", "head", "hand_l", "hand_r", "foot_l", "foot_r")})
    fwd = lambda r, b: r[b].y - r["spine_03"].y  # noqa: E731
    up = lambda r, b: r[b].z - r["spine_03"].z  # noqa: E731
    hands_fwd = sum(fwd(r, "hand_l") + fwd(r, "hand_r") for r in rows) / (2 * len(rows))
    hands_up = sum(up(r, "hand_l") + up(r, "hand_r") for r in rows) / (2 * len(rows))
    hand_side = sum(abs(r["hand_l"].x - r["hand_r"].x) for r in rows) / len(rows)
    # Walk: the feet's fore-aft difference changes sign during the clip.
    diffs = [r["foot_l"].y - r["foot_r"].y for r in rows]
    swing = max(diffs) - min(diffs)
    crossings = sum(1 for a, b in zip(diffs, diffs[1:]) if (a > 0) != (b > 0))
    pelvis = [r["pelvis"] for r in rows]
    root = [r["root"] for r in rows]
    say(f"{clip.get_name()}: {length:.2f} s, root motion {clip.get_editor_property('enable_root_motion')}, "
        f"root drift ({root[-1].x - root[0].x:.0f},{root[-1].y - root[0].y:.0f}) pelvis drift ({pelvis[-1].x - pelvis[0].x:.0f},{pelvis[-1].y - pelvis[0].y:.0f}) "
        f"pelvis z {sum(p.z for p in pelvis) / len(pelvis):.0f} | feet fore-aft swing {swing:.0f} cm, {crossings} crossings | "
        f"hands vs spine_03: forward {hands_fwd:.0f} up {hands_up:.0f}, apart {hand_side:.0f} | head-spine fwd {sum(fwd(r, 'head') for r in rows) / len(rows):.0f}")
    say("    hand_r fwd per 1/16: " + " ".join(f"{fwd(r, 'hand_r'):.0f}" for r in rows))


registry = unreal.AssetRegistryHelpers.get_asset_registry()
mesh = unreal.EditorAssetLibrary.load_asset(MESH) if MESH else None
for path in PATHS:
    for data in sorted(registry.get_assets_by_path(path, recursive=False), key=lambda d: str(d.asset_name)):
        name = str(data.asset_name)
        if path.endswith("OLD_version") and not name.lower().startswith("storm"):
            continue
        clip = data.get_asset()
        if isinstance(clip, unreal.AnimSequence):
            measure(clip)
for extra in [e for e in EXTRA.split(";") if e]:
    clip = unreal.EditorAssetLibrary.load_asset(extra)
    if clip:
        measure(clip, mesh)
first = registry.get_assets_by_path(PATHS[0], recursive=False)
if first:
    sk = first[0].get_asset().get_editor_property("skeleton")
    say(f"skeleton {sk.get_path_name()}")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "measure_storm_clips.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
