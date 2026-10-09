"""Gives the operatives' AnimBPs hit-reaction clips (user request 2026-10-08: operatives showed no reaction when hit).

Clips found in the imported packs (measured 2026-10-08 with a read-only commandlet, component-space head / pelvis):
  /Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_* (UE5 SK_Mannequin, already in the operative skeleton's
  compatible list; rifle held, in place, no root motion, pelvis stays 84-93 cm, legs nearly still):
    Front_Lgt_01 0.70 s (head ~10 cm), Front_Lgt_02 0.80 s, Front_Med_01 0.77 s (head 12 cm back), Front_Hvy_01 0.80 s,
    Back_Med_01 0.87 s (head 15 cm forward = shot from behind), Front_Lgt_03 / 04 / Front_Med_02.
  /Game/Crawl_MocapAnimPack/.../Crawl_Hit_F (prone, already HitProneAnimation).
They play on UOperativeAnimInstance::UpperBodySlot ("Fire", the graph's layered blend from spine_01): the legs keep the
locomotion; the prone one on FullBody. The pack's MM_Death_* clips stop half-way down (pelvis 83-88 cm at the end, no
lying pose) and are NOT used as death clips: a standing / crouched death falls with the knockdown Knocked_* clip
(UKnockdownComponent::PlayDeathFall).

Only empty properties are set (hand-picked clips are kept; CODEX_RESET_HIT_CLIPS=1 overwrites). The graph is not touched.
Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_hit_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
PACK = "/Game/Characters/Mannequins/Anims/Rifle/HitReact"
CLIPS = {
    "hit_stand_animation": "MM_HitReact_Front_Med_01",
    "hit_crouch_animation": "MM_HitReact_Front_Lgt_02",
    "pistol_hit_animation": "MM_HitReact_Front_Lgt_01",
    "hit_back_animation": "MM_HitReact_Back_Med_01",
}
log = []

clips = {}
for prop, asset in CLIPS.items():
    path = f"{PACK}/{asset}"
    if not library.does_asset_exist(path):
        log.append(f"MISSING {path}")
        continue
    clip = library.load_asset(path)
    clips[prop] = clip
    log.append(f"{asset}: length {clip.get_play_length():.3f} s, root motion {clip.get_editor_property('enable_root_motion')}, "
               f"skeleton {clip.get_editor_property('skeleton').get_path_name()}")

reset = os.environ.get("CODEX_RESET_HIT_CLIPS") == "1"
for abp_path in ("/Game/Characters/Operatives/ABP_Operative", "/Game/Characters/Operatives/ABP_Operative_Rifle2"):
    if not library.does_asset_exist(abp_path):
        log.append(f"{abp_path}: not in the project")
        continue
    abp = library.load_asset(abp_path)
    name = abp_path.rsplit("/", 1)[1]
    cdo = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.{name}_C"))
    changed = False
    for prop, clip in clips.items():
        current = cdo.get_editor_property(prop)
        if current and not reset:
            log.append(f"{name}.{prop} already set: {current.get_name()}")
            continue
        cdo.set_editor_property(prop, clip)
        changed = True
        log.append(f"{name}.{prop} = {clip.get_name()}")
    prone = cdo.get_editor_property("hit_prone_animation")
    log.append(f"{name}.hit_prone_animation = {prone.get_name() if prone else '-'}; upper_body_slot = {cdo.get_editor_property('upper_body_slot')}")
    if changed:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        library.save_loaded_asset(abp)
        log.append(f"{name} saved")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_hit_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
