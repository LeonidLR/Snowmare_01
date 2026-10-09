"""Death clips from the Sniper_Animation pack the user imported (2026-10-09) for the operatives and the Marksman:

  standing (one at random): AS_Bullet_Death_F1, AS_Bullet_Death_L1, AS_Bullet_Death_L3, AS_Stand_Aim_Death2
  crouched (operatives):    AS_Knee_Aim_Death
  prone:                    AS_Prone_Aim_Death

  - ABP_Operative (UOperativeAnimInstance): DeathStandAnimations, DeathCrouchAnimation, DeathProneAnimation;
  - ABP_Enemy_Marksman (UMarksmanAnimInstance): DeathAnimations (standing / crouched), ProneDeathAnimations.
These are the user's explicit replacements, so the fields are overwritten. The clips sit on the pack's own UE5 SK_Mannequin;
the operative skeleton, the Marksman mesh skeleton and the Marksman ABP's target skeleton are made compatible with it (one
way, bone-name playback like the knockdown / cover clips). The clips themselves are not written.

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi
Result: Saved/Logs/setup_sniper_death_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
PACK = "/Game/Sniper_Animation/Animation/"
STAND = [PACK + "Death/AS_Bullet_Death_F1", PACK + "Death/AS_Bullet_Death_L1", PACK + "Death/AS_Bullet_Death_L3",
         PACK + "Stand/Stand_Death/AS_Stand_Aim_Death2"]
PRONE = PACK + "Prone/AS_Prone_Aim_Death"
KNEE = PACK + "Knee/AS_Knee_Aim_Death"
log = []


def load(path):
    asset = library.load_asset(path) if library.does_asset_exist(path) else None
    if asset is None:
        log.append(f"MISSING {path}")
    return asset


def make_compatible(skeleton, other):
    if not skeleton or not other or skeleton.get_path_name() == other.get_path_name():
        return
    compatible = list(skeleton.get_editor_property("compatible_skeletons"))
    if any(c and c.get_path_name() == other.get_path_name() for c in compatible):
        log.append(f"{skeleton.get_path_name()} already compatible with {other.get_path_name()}")
        return
    compatible.append(other)
    skeleton.set_editor_property("compatible_skeletons", compatible)
    library.save_loaded_asset(skeleton)
    log.append(f"{skeleton.get_path_name()} now compatible with {other.get_path_name()}")


def mesh_skeleton(bp_class_path):
    cdo = unreal.get_default_object(unreal.load_object(None, bp_class_path))
    mesh = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
    return mesh.get_editor_property("skeleton") if mesh else None


stand = [c for c in (load(p) for p in STAND) if c]
prone = load(PRONE)
knee = load(KNEE)
clip_skeleton = None
for clip in stand + [c for c in (prone, knee) if c]:
    clip_skeleton = clip_skeleton or clip.get_editor_property("skeleton")
    log.append(f"{clip.get_name()}: {clip.get_editor_property('sequence_length'):.3f} s, root motion "
               f"{clip.get_editor_property('enable_root_motion')}")
log.append(f"clip skeleton {clip_skeleton.get_path_name() if clip_skeleton else '-'}")

# Operatives.
library.load_asset("/Game/Characters/Operatives/BP_Operative")
make_compatible(mesh_skeleton("/Game/Characters/Operatives/BP_Operative.BP_Operative_C"), clip_skeleton)
abp = load("/Game/Characters/Operatives/ABP_Operative")
if abp and stand:
    cdo = unreal.get_default_object(unreal.load_object(None, "/Game/Characters/Operatives/ABP_Operative.ABP_Operative_C"))
    old = [c.get_name() for c in cdo.get_editor_property("death_stand_animations") if c]
    cdo.set_editor_property("death_stand_animations", stand)
    log.append(f"ABP_Operative.death_stand_animations {old} -> {[c.get_name() for c in stand]}")
    if prone:
        old_prone = cdo.get_editor_property("death_prone_animation")
        cdo.set_editor_property("death_prone_animation", prone)
        log.append(f"ABP_Operative.death_prone_animation {old_prone.get_name() if old_prone else '-'} -> {prone.get_name()}")
    if knee:
        old_knee = cdo.get_editor_property("death_crouch_animation")
        cdo.set_editor_property("death_crouch_animation", knee)
        log.append(f"ABP_Operative.death_crouch_animation {old_knee.get_name() if old_knee else '-'} -> {knee.get_name()}")
    log.append(f"ABP_Operative.death_start_offset = {cdo.get_editor_property('death_start_offset'):.2f}")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp)

# Marksman.
library.load_asset("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman")
make_compatible(mesh_skeleton("/Game/Characters/Enemies/Marksman/BP_Enemy_Marksman.BP_Enemy_Marksman_C"), clip_skeleton)
abp = load("/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman")
if abp and stand:
    make_compatible(abp.get_editor_property("target_skeleton"), clip_skeleton)
    cdo = unreal.get_default_object(unreal.load_object(None,
        "/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman.ABP_Enemy_Marksman_C"))
    old = [c.get_name() for c in cdo.get_editor_property("death_animations") if c]
    cdo.set_editor_property("death_animations", stand)
    log.append(f"ABP_Enemy_Marksman.death_animations {old} -> {[c.get_name() for c in stand]}")
    if prone:
        old_prone = [c.get_name() for c in cdo.get_editor_property("prone_death_animations") if c]
        cdo.set_editor_property("prone_death_animations", [prone])
        log.append(f"ABP_Enemy_Marksman.prone_death_animations {old_prone} -> [{prone.get_name()}]")
    log.append(f"ABP_Enemy_Marksman.death_start_offset = {cdo.get_editor_property('death_start_offset'):.2f}")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp)

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_sniper_death_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
