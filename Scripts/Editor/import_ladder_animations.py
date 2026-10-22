"""Imports the ladder climbing clips (ladder system, user request 2026-10-22) into /Game/Characters/Operatives/Anims/Ladder.

Source art (kept outside the repo, not copied): the Udemy course "Enhance animation skill with Ladder Climbing",
  .../ClimbingLadderSystemFiles/Animations/CorrectAnimations/
  AS_UpLadderLoopFixed, AS_LadderClimbOutTopFixed, AS_LAdderIntoFromTopFixed (.FBX)
(the "NotFixedAnimations" folder holds the same clips before the course's root-height fix - not imported).

The FBX files carry the UE5 Manny bone set (88 bones: spine_04 / spine_05, neck_02, ik_*, interaction, center_of_mass),
so they are imported animation-only onto the M4 pack's UE5 SK_Mannequin skeleton
(/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SK_Mannequin) - the skeleton the cover, grenade and knockdown
clips use, already in the operative skeleton's (Post_Apo UE4_Mannequin_Skeleton) compatible list. The skeleton is never
saved by this script (an untracked user pack).

Unlike the knockdown clips, ROOT MOTION IS KEPT (enable_root_motion on, no root lock): the climb lives on the root
bone (up loop +72.7 cm = two rungs of 36.4 cm; climb out +261 cm up / 77 cm forward; into-from-top 97 cm back and
180 cm down), and the ladder system decides later whether it drives the capsule with it or with code.

Per clip the log gets: length, frame count, frame rate, root-motion delta (root bone translation end - start) and the
pelvis height at both ends. Existing assets are kept (CODEX_REIMPORT_LADDER=1 re-imports).

Run with the editor closed under the build lock:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_ladder_animations.txt
"""
import os
import unreal

SOURCE_DIR = ("C:/Users/Zephyrus15Duo/Documents/Study/Udemy_-_Unreal_Engine_5Enhance_animation_skill_with_Ladder_Climbing/"
              "2 - 02Import Files Absouloute Beginner/2 - ClimbingLadderSystemFiles/Animations/CorrectAnimations")
DEST = "/Game/Characters/Operatives/Anims/Ladder"
# Source file name -> asset name.
CLIPS = (
    ("AS_UpLadderLoopFixed", "AS_Ladder_UpLoop"),
    ("AS_LadderClimbOutTopFixed", "AS_Ladder_ClimbOutTop"),
    ("AS_LAdderIntoFromTopFixed", "AS_Ladder_IntoFromTop"),
)
SKELETON_PATH = "/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SK_Mannequin"
OPERATIVE_BP = "/Game/Characters/Operatives/BP_Operative"

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
reimport = os.environ.get("CODEX_REIMPORT_LADDER") == "1"
log = []


def import_clip(source_name, name, skeleton):
    path = f"{DEST}/{name}"
    if library.does_asset_exist(path) and not reimport:
        log.append(f"{name}: kept (exists)")
        return library.load_asset(path)
    source = f"{SOURCE_DIR}/{source_name}.FBX"
    if not os.path.exists(source):
        log.append(f"{name}: MISSING source {source}")
        return None
    ui = unreal.FbxImportUI()
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("skeleton", skeleton)
    anim_data = ui.get_editor_property("anim_sequence_import_data")
    anim_data.set_editor_property("import_meshes_in_bone_hierarchy", False)
    anim_data.set_editor_property("remove_redundant_keys", False)
    anim_data.set_editor_property("do_not_import_curve_with_zero", True)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    tools.import_asset_tasks([task])
    imported = [str(p) for p in task.get_editor_property("imported_object_paths")]
    log.append(f"{name}: imported {imported}")
    return library.load_asset(path) if library.does_asset_exist(path) else None


def configure(anim):
    anim.set_editor_property("enable_root_motion", True)
    anim.set_editor_property("force_root_lock", False)
    library.save_loaded_asset(anim)


def measure(name, anim):
    length = anim.get_editor_property("sequence_length")
    frames = unreal.AnimationLibrary.get_num_frames(anim)
    rate = frames / length if length > 0 else 0.0
    root0 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False).translation
    root1 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False).translation
    pel0 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "pelvis", 0.0, False).translation
    pel1 = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "pelvis", length, False).translation
    delta = root1 - root0
    tracks = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(anim)]
    log.append(f"MEASURE {name}: length {length:.3f} s, frames {frames}, ~{rate:.1f} fps, "
               f"root start ({root0.x:.1f}, {root0.y:.1f}, {root0.z:.1f}) delta ({delta.x:.1f}, {delta.y:.1f}, {delta.z:.1f}) cm, "
               f"pelvis local z start {pel0.z:.1f} end {pel1.z:.1f}, {len(tracks)} bone tracks, "
               f"root motion {anim.get_editor_property('enable_root_motion')}, "
               f"skeleton {anim.get_editor_property('skeleton').get_path_name()}")


unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")
skeleton = library.load_asset(SKELETON_PATH)
log.append(f"target skeleton {skeleton.get_path_name() if skeleton else 'MISSING'} ({type(skeleton).__name__})")
if skeleton:
    for source_name, name in CLIPS:
        anim = import_clip(source_name, name, skeleton)
        if not anim:
            continue
        configure(anim)
        measure(name, anim)

# Operative skeleton compatibility (read-only report): the UE4 body plays the clips by bone name through it.
try:
    cdo = unreal.get_default_object(unreal.load_object(None, f"{OPERATIVE_BP}.BP_Operative_C"))
    mesh = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
    op_skel = mesh.get_editor_property("skeleton") if mesh else None
    compatible = [c.get_path_name() for c in op_skel.get_editor_property("compatible_skeletons") if c] if op_skel else []
    log.append(f"operative mesh {mesh.get_path_name() if mesh else '-'}, skeleton {op_skel.get_path_name() if op_skel else '-'}; "
               f"compatible with target: {any(SKELETON_PATH in c for c in compatible)}")
except Exception as error:
    log.append(f"operative skeleton check failed: {error}")

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_ladder_animations.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
