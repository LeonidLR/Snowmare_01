"""Imports the user's knockdown clips (Sprint 14, TANDEM request #12) into /Game/Animations_KnockDown and builds the montages.

Source art (kept outside the repo, not copied):
  C:/Users/Zephyrus15Duo/Desktop/KnockedDown/Game/KnockedDown/Animations/RTGT/
  Death_Back, Death_Front, Knocked_Back, Knocked_Front, Revive_Back, Revive_Front, Revive_Left (.FBX)

The FBX files carry the UE5 Manny bone set (spine_01..05, neck_01..02, ik_foot_root, ik_hand_gun: "RTGT" = retargeted to
the UE5 mannequin), so they are imported animation-only onto the M4 pack's UE5 SK_Mannequin skeleton
(/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SK_Mannequin) - the same skeleton the grenade and cover clips use,
already in the operative skeleton's (Post_Apo_Survivor UE4_Mannequin_Skeleton) compatible list. The skeleton itself is
never saved by this script (an untracked user pack).

Per clip: root motion off + force root lock (the gameplay owns the capsule; turn-based units sit on grid cells), the
length, the pelvis height over time (ground contact of a fall, the moment a get-up is standing) and the root drift are
measured and written to the log. Montages AM_<clip> are created on the "FullBody" slot (the operative ABP's full-body
slot; enemies play the sequences on their own one-shot slot from C++): Knocked_* hold their last frame (no auto blend
out: the downed phase), Revive_* blend out into the locomotion, Death_* hold. Existing assets are kept
(CODEX_REIMPORT_KNOCKDOWN=1 re-imports / rebuilds).

The script also reports which enemy skeletons can play the clips (bone overlap with the enemy's walk clip).

Run with the editor closed (or in the agents' worktree) under the build lock:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/import_knockdown_animations.txt
"""
import os
import unreal

SOURCE_DIR = "C:/Users/Zephyrus15Duo/Desktop/KnockedDown/Game/KnockedDown/Animations/RTGT"
DEST = "/Game/Animations_KnockDown"
CLIPS = ("Knocked_Back", "Knocked_Front", "Revive_Back", "Revive_Front", "Revive_Left", "Death_Back", "Death_Front")
SKELETON_PATH = "/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SK_Mannequin"
SLOT = "FullBody"
OPERATIVE_BP = "/Game/Characters/Operatives/BP_Operative"
ENEMIES = ("Frostbitten", "Brute", "Cutter", "Hound", "Marksman")

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
reimport = os.environ.get("CODEX_REIMPORT_KNOCKDOWN") == "1"
log = []


def import_clip(name, skeleton, dest=DEST):
    path = f"{dest}/{name}"
    if library.does_asset_exist(path) and not reimport:
        log.append(f"{name}: kept (exists)")
        return library.load_asset(path)
    source = f"{SOURCE_DIR}/{name}.FBX"
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
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    tools.import_asset_tasks([task])
    imported = [p for p in task.get_editor_property("imported_object_paths")]
    log.append(f"{name}: imported {imported}")
    return library.load_asset(path) if library.does_asset_exist(path) else None


def pelvis_height(anim, t):
    root = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", t, False)
    pelvis = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "pelvis", t, False)
    return root.translation.z + pelvis.translation.z, root.translation, pelvis.translation


def measure(name, anim):
    length = anim.get_editor_property("sequence_length")
    steps = 60
    samples = []
    for i in range(steps + 1):
        t = length * i / steps
        h, root, pelvis = pelvis_height(anim, t)
        samples.append((t, h, root, pelvis))
    heights = [s[1] for s in samples]
    lo, hi = min(heights), max(heights)
    start_h, end_h = heights[0], heights[-1]
    info = {"length": length, "start": start_h, "end": end_h, "min": lo, "max": hi}
    if name.startswith("Knocked") or name.startswith("Death"):
        contact = next((s[0] for s in samples if s[1] <= lo + 8.0), length)
        info["contact"] = contact
    if name.startswith("Revive"):
        standing = next((s[0] for s in samples if s[1] >= start_h + 0.9 * (end_h - start_h)), length)
        info["standing"] = standing
    root0, root1 = samples[0][2], samples[-1][2]
    pel0, pel1 = samples[0][3], samples[-1][3]
    info["root_drift"] = (root1 - root0).length()
    info["pelvis_xy_drift"] = unreal.Vector(pel1.x - pel0.x, pel1.y - pel0.y, 0).length()
    log.append(f"  {name} pelvis start ({pel0.x:.0f}, {pel0.y:.0f}, {pel0.z:.0f}) end ({pel1.x:.0f}, {pel1.y:.0f}, {pel1.z:.0f})")
    curve = " ".join(f"{s[1]:.0f}" for s in samples[::6])
    extra = " ".join(f"{k} {v:.3f}" for k, v in info.items() if k not in ("length",))
    log.append(f"MEASURE {name}: length {length:.3f} s, frames {anim.get_editor_property('number_of_sampled_frames') if hasattr(anim, 'number_of_sampled_frames') else '?'}; {extra}; pelvis cm every 0.1 L: {curve}")
    return info


def configure(anim):
    anim.set_editor_property("enable_root_motion", False)
    anim.set_editor_property("force_root_lock", True)
    library.save_loaded_asset(anim)


def make_montage(name, anim, hold):
    path = f"{DEST}/AM_{name}"
    if library.does_asset_exist(path) and not reimport:
        montage = library.load_asset(path)
        log.append(f"AM_{name}: kept")
    else:
        if library.does_asset_exist(path):
            library.delete_asset(path)
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", anim)
        montage = tools.create_asset(f"AM_{name}", DEST, unreal.AnimMontage, factory)
        log.append(f"AM_{name}: created")
    # SlotAnimTracks is read-only in Python: the editor library renames the slot (CodexTacticsEditor).
    unreal.OperativeAnimGraphLibrary.set_montage_slot(montage, SLOT)
    slot_ok = all(str(t.get_editor_property("slot_name")) == SLOT for t in montage.get_editor_property("slot_anim_tracks"))
    montage.set_editor_property("enable_auto_blend_out", not hold)
    blend_in = montage.get_editor_property("blend_in")
    blend_in.set_editor_property("blend_time", 0.12 if name.startswith("Knocked") or name.startswith("Death") else 0.15)
    montage.set_editor_property("blend_in", blend_in)
    blend_out = montage.get_editor_property("blend_out")
    blend_out.set_editor_property("blend_time", 0.0 if hold else 0.25)
    montage.set_editor_property("blend_out", blend_out)
    library.save_loaded_asset(montage)
    log.append(f"AM_{name}: slot {SLOT if slot_ok else 'NOT SET'}, hold last frame {hold}, length {montage.get_editor_property('sequence_length'):.3f}")


unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")
skeleton = library.load_asset(SKELETON_PATH)
log.append(f"target skeleton {skeleton.get_path_name() if skeleton else 'MISSING'}")
anims = {}
if skeleton:
    for clip in CLIPS:
        anim = import_clip(clip, skeleton)
        if not anim:
            continue
        configure(anim)
        anims[clip] = anim
        tracks = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(anim)]
        log.append(f"{clip}: {len(tracks)} bone tracks, UE5 bones spine_05 {'spine_05' in tracks} neck_02 {'neck_02' in tracks}, skeleton {anim.get_editor_property('skeleton').get_path_name()}")
        measure(clip, anim)
        make_montage(clip, anim, hold=not clip.startswith("Revive"))

# Operative skeleton compatibility (read-only report).
try:
    cdo = unreal.get_default_object(unreal.load_object(None, f"{OPERATIVE_BP}.BP_Operative_C"))
    mesh = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
    op_skel = mesh.get_editor_property("skeleton") if mesh else None
    compatible = [c.get_path_name() for c in op_skel.get_editor_property("compatible_skeletons") if c] if op_skel else []
    log.append(f"operative skeleton {op_skel.get_path_name() if op_skel else '-'}; compatible with target: {any(SKELETON_PATH in c for c in compatible)}")
except Exception as error:
    log.append(f"operative skeleton check failed: {error}")

# Enemy skeletons: can they play these clips?
knock_tracks = set(str(n) for n in unreal.AnimationLibrary.get_animation_track_names(anims["Knocked_Back"])) if "Knocked_Back" in anims else set()
for enemy in ENEMIES:
    try:
        abp = f"/Game/Characters/Enemies/{enemy}/ABP_Enemy_{enemy}"
        anim_cdo = unreal.get_default_object(unreal.load_object(None, f"{abp}.ABP_Enemy_{enemy}_C"))
        walk = anim_cdo.get_editor_property("walk_animation")
        bp_cdo = unreal.get_default_object(unreal.load_object(None, f"/Game/Characters/Enemies/{enemy}/BP_Enemy_{enemy}.BP_Enemy_{enemy}_C"))
        mesh = bp_cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
        skel = mesh.get_editor_property("skeleton") if mesh else None
        tracks = set(str(n) for n in unreal.AnimationLibrary.get_animation_track_names(walk)) if walk else set()
        overlap = len(tracks & knock_tracks)
        compatible = [c.get_path_name() for c in skel.get_editor_property("compatible_skeletons") if c] if skel else []
        log.append(f"ENEMY {enemy}: skeleton {skel.get_path_name() if skel else '-'}, walk tracks {len(tracks)}, overlap with knock clip {overlap}/{len(knock_tracks)}, "
                   f"compatible list has target {any(SKELETON_PATH in c for c in compatible)}; sample bones {sorted(tracks)[:8]}")
    except Exception as error:
        log.append(f"ENEMY {enemy}: check failed {error}")

# Humanoid enemies on the UE4 mannequin rig (68 bones, a subset of the UE5 set: spine_04 / spine_05 / neck_02 are
# dropped like when the operative's UE4 skeleton plays the UE5 clips through skeleton compatibility). Their skeletons are
# untracked user packs (read-only here), so the clips are imported a second time directly onto each enemy skeleton
# instead of editing its compatible list. Hound (Bip001 dog rig) and Cutter (pounces, never knocked down) get none.
ENEMY_CLIPS = ("Knocked_Back", "Knocked_Front", "Revive_Back", "Revive_Front", "Death_Back", "Death_Front")
for enemy, mesh_path in (("Frostbitten", "/Game/mutant_monster_2/base_mesh/SK_base_mesh"),
                         ("Brute", "/Game/Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1")):
    mesh = library.load_asset(mesh_path)
    enemy_skeleton = mesh.get_editor_property("skeleton") if mesh else None
    if not enemy_skeleton:
        log.append(f"ENEMY {enemy}: no skeleton ({mesh_path})")
        continue
    for clip in ENEMY_CLIPS:
        anim = import_clip(clip, enemy_skeleton, f"{DEST}/{enemy}")
        if not anim:
            continue
        configure(anim)
        tracks = len(unreal.AnimationLibrary.get_animation_track_names(anim))
        log.append(f"ENEMY {enemy} {clip}: {tracks} tracks on {anim.get_editor_property('skeleton').get_path_name()}, "
                   f"length {anim.get_editor_property('sequence_length'):.3f}")

out = os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_knockdown_animations.txt")
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
