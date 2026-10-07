"""Gives the operatives' AnimBPs the four grenade-throw clips the user imported into /Game/Animations_Grenade
(2026-10-07): Walk / Run / Crouch / Prone  ->  UOperativeAnimInstance GrenadeThrow*Animation class defaults of
ABP_Operative and ABP_Operative_Rifle2 (only while a property is empty: hand-picked clips are kept;
CODEX_RESET_GRENADE_CLIPS=1 overwrites).

The clips are on the M4 pack's UE5 SK_Mannequin (/Game/M4_Cover_Pack/Demo/Characters/Mannequins/Meshes/SK_Mannequin,
bones root / ik_foot_* / hand_r ...), already in the operative skeleton's compatible list (setup_operative_cover_animation.py);
this script adds it when missing (one way; the clips themselves are not written). The AnimGraph is not touched: the throw
plays on UOperativeAnimInstance::UpperBodySlot ("Fire", the graph's layered blend from spine_01), the legs keep the locomotion.
The release times (GrenadeThrow*ReleaseSeconds, measured: walk 1.28 / run 1.73 / crouch 0.89 / prone 1.14 s) are C++ defaults
and are only reported here. The script also reports (and fixes, when empty or "DefaultSlot") UpperBodySlot.

Run with the editor closed:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Result: Saved/Logs/setup_operative_grenade_animation.txt
"""
import os
import unreal

library = unreal.EditorAssetLibrary
PACK = "/Game/Animations_Grenade"
CLIPS = {
    "grenade_throw_walk_animation": "Grenade_01root_root_WalkGrenadeThrow_UE",
    "grenade_throw_run_animation": "Grenade_01root_root_RunAndThrowGrenade_UE",
    "grenade_throw_crouch_animation": "Grenade_01root_root_CrouchThrowGrenade_UE",
    "grenade_throw_prone_animation": "Grenade_01root_root_ProneThrowGrenade_UE",
}
UPPER_BODY_SLOT = "Fire"
log = []


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
clip_skeleton = None
for prop, asset in CLIPS.items():
    path = f"{PACK}/{asset}"
    if not library.does_asset_exist(path):
        log.append(f"MISSING {path}")
        continue
    clip = library.load_asset(path)
    clips[prop] = clip
    clip_skeleton = clip_skeleton or clip.get_editor_property("skeleton")
    log.append(f"{asset}: length {clip.get_editor_property('sequence_length'):.3f} s, root motion {clip.get_editor_property('enable_root_motion')}")
log.append(f"grenade clips on skeleton {clip_skeleton.get_path_name() if clip_skeleton else '-'}")

bp = library.load_asset("/Game/Characters/Operatives/BP_Operative")
operative_skeleton = None
if bp:
    cdo = unreal.get_default_object(unreal.load_object(None, "/Game/Characters/Operatives/BP_Operative.BP_Operative_C"))
    mesh = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
    operative_skeleton = mesh.get_editor_property("skeleton") if mesh else None
log.append(f"operative skeleton {operative_skeleton.get_path_name() if operative_skeleton else '-'}")
make_compatible(operative_skeleton, clip_skeleton)

reset = os.environ.get("CODEX_RESET_GRENADE_CLIPS") == "1"
for abp_path in ("/Game/Characters/Operatives/ABP_Operative", "/Game/Characters/Operatives/ABP_Operative_Rifle2"):
    if not library.does_asset_exist(abp_path):
        continue
    abp = library.load_asset(abp_path)
    name = abp_path.rsplit("/", 1)[1]
    cdo = unreal.get_default_object(unreal.load_object(None, f"{abp_path}.{name}_C"))
    changed = False
    for prop, clip in clips.items():
        if cdo.get_editor_property(prop) and not reset:
            log.append(f"{name}.{prop} already set: {cdo.get_editor_property(prop).get_name()}")
            continue
        cdo.set_editor_property(prop, clip)
        changed = True
        log.append(f"{name}.{prop} = {clip.get_name()}")
    slot = str(cdo.get_editor_property("upper_body_slot"))
    log.append(f"{name}.upper_body_slot = {slot}")
    if slot in ("", "None", "DefaultSlot"):
        cdo.set_editor_property("upper_body_slot", UPPER_BODY_SLOT)
        changed = True
        log.append(f"{name}.upper_body_slot -> {UPPER_BODY_SLOT}")
    log.append(f"{name}: release s walk/run/crouch/prone = " + "/".join(
        f"{cdo.get_editor_property(p):.2f}" for p in ("grenade_throw_walk_release_seconds", "grenade_throw_run_release_seconds",
                                                      "grenade_throw_crouch_release_seconds", "grenade_throw_prone_release_seconds")))
    if changed:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        library.save_loaded_asset(abp)

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_operative_grenade_animation.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
