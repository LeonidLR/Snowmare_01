"""Gives the operatives' AnimBPs the Rifle_2 hurdle clips for vaulting (user request 2026-10-06):

  - /Game/RifleAnims/Rifle_2/Traverse/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_{Lfoot,Rfoot}_Rifle are root-locked
    (force_root_lock): the game's vault arc moves the body (its height fits the obstacle), the clips only animate it.
  - Their landing time (the root reaching 97 % of its forward travel) is measured.
  - ABP_Operative (and ABP_Operative_Rifle2 when it exists): VaultLeftFootAnimation / VaultRightFootAnimation and the
    landing times, set only while unset (hand-picked clips are kept; CODEX_RESET_VAULT_CLIPS=1 overwrites). The AnimGraph
    is not touched: UOperativeAnimInstance plays the clip on its FullBody slot.

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_vault_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
TRAVERSE = "/Game/RifleAnims/Rifle_2/Traverse"
CLIPS = {"Lfoot": f"{TRAVERSE}/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Lfoot_Rifle",
         "Rfoot": f"{TRAVERSE}/M_Neutral_Traversal_Hurdle_1_0_stand_F_V2_Rfoot_Rifle"}
log = []


def landing_time(anim):
    """First time the root has covered 97 % of its forward (mesh +Y) travel; the clip length when not measurable."""
    length = unreal.AnimationLibrary.get_sequence_length(anim)
    try:
        start = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", 0.0, False).translation.y
        end = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", length, False).translation.y
        steps = 80
        for i in range(steps + 1):
            t = length * i / steps
            y = unreal.AnimationLibrary.get_bone_pose_for_time(anim, "root", t, False).translation.y
            if abs(end - start) > 1.0 and (y - start) / (end - start) >= 0.97:
                return t
    except Exception as error:
        log.append(f"landing of {anim.get_name()} not measured: {error}")
    return length * 0.6


clips = {}
for foot, path in CLIPS.items():
    if not library.does_asset_exist(path):
        log.append(f"MISSING {path}")
        continue
    anim = library.load_asset(path)
    if not anim.get_editor_property("force_root_lock"):
        anim.set_editor_property("force_root_lock", True)
        library.save_loaded_asset(anim)
        log.append(f"{anim.get_name()}: root locked")
    clips[foot] = (anim, landing_time(anim))
    log.append(f"{anim.get_name()}: length {unreal.AnimationLibrary.get_sequence_length(anim):.2f} s, lands at {clips[foot][1]:.2f} s")

reset = os.environ.get("CODEX_RESET_VAULT_CLIPS") == "1"
for abp_path in ("/Game/Characters/Operatives/ABP_Operative", "/Game/Characters/Operatives/ABP_Operative_Rifle2"):
    if not library.does_asset_exist(abp_path):
        continue
    abp = library.load_asset(abp_path)
    name = abp_path.rsplit("/", 1)[1]
    cdo = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.{name}_C"))
    for foot, prop, landing_prop in (("Lfoot", "vault_left_foot_animation", "vault_left_foot_landing_time"),
                                     ("Rfoot", "vault_right_foot_animation", "vault_right_foot_landing_time")):
        if foot in clips and (reset or cdo.get_editor_property(prop) is None):
            cdo.set_editor_property(prop, clips[foot][0])
            cdo.set_editor_property(landing_prop, clips[foot][1])
            log.append(f"{name}.{prop} = {clips[foot][0].get_name()} (lands {clips[foot][1]:.2f} s)")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    library.save_loaded_asset(abp)

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_vault_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
