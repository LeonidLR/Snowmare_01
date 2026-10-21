"""Marksman feet / death fix (user decision 2026-10-21, option A), with its own revert.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=Scripts/Pipeline/marksman_anim_fix.py -unattended
        -nullrhi -stdout -FullStdOutLogOutput
    env CODEX_MARKSMAN_FIX = dry | apply | revert

apply:
  1. /Game/Biochemical_Monster_1/baze_mech/ue4/SK_UE4_Mannequin_Skeleton: every bone's translation retargeting mode =
     Animation. The pack's own clips (anim_ue4) carry other leg bone lengths than this skeleton's reference pose; with the legs
     in Skeleton mode the body stood 10-15 cm above the ground (MarksmanFeetProbe Clips). Users of this skeleton asset: the
     Marksman (BP / ABP / its bodies) and the pack's unused demo ThirdPersonCharacter / example map.
  2. ABP_Enemy_Marksman DeathAnimations: the pack's own anim_ue4/anim_death instead of the Manny death clips (they sank
     10-38 cm into the ground on this body); the prone death clips stay.
The old modes / clip list go to ArtPipeline/Characters/marksman_anim_revert.json first (kept from the first apply);
revert restores them.
"""
import json
import os

import unreal

ROOT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REVERT = os.path.join(ROOT, "ArtPipeline", "Characters", "marksman_anim_revert.json")
SKELETON = "/Game/Biochemical_Monster_1/baze_mech/ue4/SK_UE4_Mannequin_Skeleton"
ABP = "/Game/Characters/Enemies/Marksman/ABP_Enemy_Marksman"
OWN_DEATH = "/Game/Biochemical_Monster_1/animation/anim_ue4/anim_death"
MODE = unreal.BoneTranslationRetargetingMode


def bone_names(skeleton):
    return [str(n) for n in skeleton.get_reference_pose().get_bone_names()]


def read_modes(skeleton):
    tree = skeleton.get_editor_property("bone_tree")
    return {n: tree[i].get_editor_property("translation_retargeting_mode").name for i, n in enumerate(bone_names(skeleton))}


def write_modes(skeleton, wanted):
    # BoneTree is read-only as a whole in Python and tree[i] hands out a copy: edit the copy, assign it back by index.
    skeleton.modify()
    tree = skeleton.get_editor_property("bone_tree")
    for i, n in enumerate(bone_names(skeleton)):
        node = tree[i]
        node.set_editor_property("translation_retargeting_mode", getattr(MODE, wanted[n]))
        tree[i] = node


def abp_cdo():
    abp = unreal.load_asset(ABP)
    return abp, unreal.get_default_object(abp.generated_class())


def p(o):
    return o.get_path_name() if o else None


def main():
    mode = os.environ.get("CODEX_MARKSMAN_FIX", "dry")
    skeleton = unreal.load_asset(SKELETON)
    abp, cdo = abp_cdo()
    before = {"skeleton": SKELETON, "modes": read_modes(skeleton), "abp": ABP,
              "death_animations": [p(a) for a in cdo.get_editor_property("death_animations")],
              "prone_death_animations": [p(a) for a in cdo.get_editor_property("prone_death_animations")]}
    hist = {}
    for m in before["modes"].values():
        hist[m] = hist.get(m, 0) + 1
    unreal.log("MARKSMAN_FIX before: modes %s, death %s, prone death %s" % (hist, before["death_animations"], before["prone_death_animations"]))
    if mode == "dry":
        unreal.log("MARKSMAN_FIX_DONE dry")
        return
    if mode == "apply":
        if not os.path.exists(REVERT):
            with open(REVERT, "w", encoding="utf-8") as f:
                json.dump(before, f, indent=1)
        write_modes(skeleton, {n: "ANIMATION" for n in before["modes"]})
        cdo.set_editor_property("death_animations", [unreal.load_asset(OWN_DEATH)])
    elif mode == "revert":
        old = json.load(open(REVERT, encoding="utf-8"))
        write_modes(skeleton, old["modes"])
        cdo.set_editor_property("death_animations", [unreal.load_asset(x) for x in old["death_animations"]])
    else:
        raise RuntimeError("CODEX_MARKSMAN_FIX must be dry | apply | revert")
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    for path in (SKELETON, ABP):
        if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
            raise RuntimeError("could not save %s" % path)
    skeleton = unreal.load_asset(SKELETON)
    abp, cdo = abp_cdo()
    hist = {}
    for m in read_modes(skeleton).values():
        hist[m] = hist.get(m, 0) + 1
    unreal.log("MARKSMAN_FIX after: modes %s, death %s, prone death %s" % (
        hist, [p(a) for a in cdo.get_editor_property("death_animations")], [p(a) for a in cdo.get_editor_property("prone_death_animations")]))
    unreal.log("MARKSMAN_FIX_DONE %s" % mode)


main()
