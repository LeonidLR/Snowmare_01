"""Gives the operatives' AnimBPs the M4 Cover Pack clips for the Sprint 12 cover system (Gemini spec: the pack
`Content/M4_Cover_Pack`, MandB Animation, imported by the user — read only here).

  - Finds the cover clips (anim_M4_cvr_*) and reports the skeleton they are on. The operative's skeleton
    (Post_Apo_Survivor UE4 mannequin) gets that skeleton in its compatible list (one way, like the Rifle_2 set-up:
    setup_operative_rifle2_animation.py made it compatible with the UE5 SK_Mannequin; the pack's own assets are not
    written). Bone names of the UE4 / UE5 mannequins match, so the clips play through the compatible skeleton without a
    retarget — proportions to check by eye.
  - ABP_Operative (and ABP_Operative_Rifle2 when it exists): the UOperativeAnimInstance cover clip arrays, index 0 =
    Left, 1 = Right (ECoverFacing = the corner the operative works), set only while unset (hand-picked clips are kept;
    CODEX_RESET_COVER_CLIPS=1 overwrites). The AnimGraph is not touched: UOperativeAnimInstance::UpdateCoverLayer plays
    the clips on its FullBody slot (bUseNativeCoverClips) until the user builds cover states into the graph.

Mapping (pack name -> property):
  cvr_std_idle_L/R                      -> CoverStandIdle            cvr_crch_idle_L/R                 -> CoverCrouchIdle
  cvr_std_walk_fwd_loop_L/R             -> CoverStandMoveForward     cvr_std_walk_bwd_loop_L/R         -> CoverStandMoveBackward
  cvr_crch_walk_fwd_loop_L/R            -> CoverCrouchMoveForward    cvr_crch_walk_bwd_loop_L/R        -> CoverCrouchMoveBackward
  cvr_std_fire_L/R                      -> CoverStandFire            cvr_crch_fire_L/R                 -> CoverCrouchFire
  std_idle_fwd_to_cvr_std_idle_L/R      -> CoverStandEnter           crch_idle_fwd_to_cvr_crch_idle_L/R -> CoverCrouchEnter
  (no blind fire clip in the pack: CoverBlindFire stays empty, the fire clip stands in)

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_cover_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
PACK = "/Game/M4_Cover_Pack/Animations"
CLIPS = {
    "cover_stand_idle": ("stand/anim_M4_cvr_std_idle_L", "stand/anim_M4_cvr_std_idle_R"),
    "cover_crouch_idle": ("crouch/anim_M4_cvr_crch_idle_L", "crouch/anim_M4_cvr_crch_idle_R"),
    "cover_stand_move_forward": ("stand/anim_M4_cvr_std_walk_fwd_loop_L", "stand/anim_M4_cvr_std_walk_fwd_loop_R"),
    "cover_stand_move_backward": ("stand/anim_M4_cvr_std_walk_bwd_loop_L", "stand/anim_M4_cvr_std_walk_bwd_loop_R"),
    "cover_crouch_move_forward": ("crouch/anim_M4_cvr_crch_walk_fwd_loop_L", "crouch/anim_M4_cvr_crch_walk_fwd_loop_R"),
    "cover_crouch_move_backward": ("crouch/anim_M4_cvr_crch_walk_bwd_loop_L", "crouch/anim_M4_cvr_crch_walk_bwd_loop_R"),
    "cover_stand_fire": ("stand/anim_M4_cvr_std_fire_L", "stand/anim_M4_cvr_std_fire_R"),
    "cover_crouch_fire": ("crouch/anim_M4_cvr_crch_fire_L", "crouch/anim_M4_cvr_crch_fire_R"),
    "cover_stand_enter": ("stand/anim_M4_std_idle_fwd_to_cvr_std_idle_L", "stand/anim_M4_std_idle_fwd_to_cvr_std_idle_R"),
    "cover_crouch_enter": ("crouch/anim_M4_crch_idle_fwd_to_cvr_crch_idle_L", "crouch/anim_M4_crch_idle_fwd_to_cvr_crch_idle_R"),
}
log = []


def load_clip(rel):
    path = f"{PACK}/{rel}"
    if not library.does_asset_exist(path):
        log.append(f"MISSING {path}")
        return None
    return library.load_asset(path)


def make_compatible(skeleton, other):
    if not skeleton or not other or skeleton.get_path_name() == other.get_path_name():
        return
    compatible = list(skeleton.get_editor_property("compatible_skeletons"))
    if not any(c and c.get_path_name() == other.get_path_name() for c in compatible):
        compatible.append(other)
        skeleton.set_editor_property("compatible_skeletons", compatible)
        library.save_loaded_asset(skeleton)
        log.append(f"{skeleton.get_path_name()} now compatible with {other.get_path_name()}")


clips = {}
pack_skeleton = None
for prop, (left, right) in CLIPS.items():
    pair = [load_clip(left), load_clip(right)]
    clips[prop] = pair
    for clip in pair:
        if clip and not pack_skeleton:
            pack_skeleton = clip.get_editor_property("skeleton")
log.append(f"M4 clips on skeleton {pack_skeleton.get_path_name() if pack_skeleton else '-'}")

# The operative skeleton (from BP_Operative's mesh) learns the pack skeleton; the pack's assets stay untouched.
bp = library.load_asset("/Game/Characters/Operatives/BP_Operative")
operative_skeleton = None
if bp:
    cdo = unreal.get_default_object(unreal.load_object(None, "/Game/Characters/Operatives/BP_Operative.BP_Operative_C"))
    mesh_component = cdo.get_editor_property("mesh")
    mesh = mesh_component.get_editor_property("skeletal_mesh_asset") if mesh_component else None
    operative_skeleton = mesh.get_editor_property("skeleton") if mesh else None
log.append(f"operative skeleton {operative_skeleton.get_path_name() if operative_skeleton else '-'}")
make_compatible(operative_skeleton, pack_skeleton)

reset = os.environ.get("CODEX_RESET_COVER_CLIPS") == "1"
for abp_path in ("/Game/Characters/Operatives/ABP_Operative", "/Game/Characters/Operatives/ABP_Operative_Rifle2"):
    if not library.does_asset_exist(abp_path):
        continue
    abp = library.load_asset(abp_path)
    name = abp_path.rsplit("/", 1)[1]
    cdo = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.{name}_C"))
    changed = False
    for prop, pair in clips.items():
        current = list(cdo.get_editor_property(prop))
        if not reset and any(c for c in current):
            continue  # hand-picked clips kept
        if not any(pair):
            continue
        cdo.set_editor_property(prop, [pair[0] or pair[1], pair[1] or pair[0]])
        changed = True
        log.append(f"{name}.{prop} = [{(pair[0] or pair[1]).get_name()}, {(pair[1] or pair[0]).get_name()}]")
    if changed:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        library.save_loaded_asset(abp)
    else:
        log.append(f"{name}: cover clips already set (CODEX_RESET_COVER_CLIPS=1 overwrites)")

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_cover_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
