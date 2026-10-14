"""Prone dive on the move with real root motion (rev 5f, game designer 2026-10-14: "he stops dead, dives in place, then the
mesh snaps back").

The pack's walk / run-to-prone clips (/Game/Crawl_MocapAnimPack/Animations/Transitions_Set/Crawl_from_Walk / Crawl_from_Run, the
user's assets - NOT modified) carry their travel in the root bone (+Y ~3.1 / ~3.4 m) but have Enable Root Motion off, so the
capsule stayed behind the body. This script makes Claude-owned COPIES with root motion on:

  /Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Walk_RM
  /Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Run_RM

  enable_root_motion = True, root_motion_root_lock = ANIM_FIRST_FRAME (the body stays on the capsule from the first frame),
  force_root_lock = False. Played as a dynamic montage on the full-body slot, the anim instance extracts the root motion and
  the CharacterMovementComponent moves the capsule with it (collision, floor, ledges) - no mesh offsets.
UOperativeAnimInstance loads these copies by default (WalkToProneAnimation / RunToProneAnimation empty); a clip assigned by hand
must have Enable Root Motion on (else the dive falls back to "stop, then lie down").

Idempotent: existing copies are kept (CODEX_RESET_PRONE_DIVE=1 recreates them). Writes the root track of every clip (travel per
0.1 s) to Saved/Logs/create_prone_dive_root_motion_clips.txt.

Run with no editor of THIS checkout open (the pack is only read):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import os
import unreal

library = unreal.EditorAssetLibrary
SOURCE_DIR = "/Game/Crawl_MocapAnimPack/Animations/Transitions_Set/"
DEST_DIR = "/Game/Characters/Operatives/Anims/ProneDive/"
CLIPS = {"Crawl_from_Walk": "AS_Crawl_from_Walk_RM", "Crawl_from_Run": "AS_Crawl_from_Run_RM"}
LOG = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_prone_dive_root_motion_clips.txt")
log = []


def root_track(seq):
    """Root bone translation every 0.1 s (bone 0 of the clip's skeleton, local = component space for the root)."""
    length = seq.get_play_length()
    skeleton = seq.get_editor_property("skeleton")
    root_name = "root"
    try:
        ref = skeleton.get_reference_pose() if skeleton else None
        names = ref.get_bone_names() if ref else []
        root_name = str(names[0]) if names else "root"
    except Exception:  # older API: the pack's root is "root"
        pass
    samples = []
    t = 0.0
    while t <= length + 1e-4:
        pose = unreal.AnimationLibrary.get_bone_pose_for_time(seq, root_name, min(t, length), False)
        loc = pose.translation
        samples.append((round(t, 2), round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)))
        t += 0.1
    return root_name, length, samples


reset = os.environ.get("CODEX_RESET_PRONE_DIVE") == "1"
for source_name, dest_name in CLIPS.items():
    source_path = SOURCE_DIR + source_name
    dest_path = DEST_DIR + dest_name
    if not library.does_asset_exist(source_path):
        log.append(f"MISSING source {source_path} (the Crawl_MocapAnimPack is user content: import it first)")
        continue
    source = library.load_asset(source_path)
    log.append(f"source {source_path}: root motion {source.get_editor_property('enable_root_motion')}, "
               f"skeleton {source.get_editor_property('skeleton').get_path_name()}")
    if reset and library.does_asset_exist(dest_path):
        library.delete_asset(dest_path)
    if not library.does_asset_exist(dest_path):
        library.duplicate_asset(source_path, dest_path)
        log.append(f"created {dest_path}")
    else:
        log.append(f"kept {dest_path}")
    copy = library.load_asset(dest_path)
    copy.set_editor_property("enable_root_motion", True)
    copy.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.ANIM_FIRST_FRAME)
    copy.set_editor_property("force_root_lock", False)
    library.save_loaded_asset(copy)
    root_name, length, samples = root_track(source)
    first, last = samples[0], samples[-1]
    travel = ((last[1] - first[1]) ** 2 + (last[2] - first[2]) ** 2) ** 0.5
    log.append(f"{dest_name}: root motion on (lock: first frame), length {length:.2f} s, root bone '{root_name}', travel {travel:.0f} cm "
               f"(x {last[1] - first[1]:+.0f}, y {last[2] - first[2]:+.0f})")
    log.append("  t / root x y z: " + "  ".join(f"{s[0]:.1f}:{s[1]:.0f},{s[2]:.0f},{s[3]:.0f}" for s in samples))

os.makedirs(os.path.dirname(LOG), exist_ok=True)
with open(LOG, "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
unreal.log("\n".join(log))
