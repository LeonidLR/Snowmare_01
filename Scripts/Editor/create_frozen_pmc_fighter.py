"""Creates the Frozen PMC Fighter enemy (game designer request 2026-10-20) on the user's /Game/Cyberpunk_Explorer_A pack.

The designer wanted the new body to use the Frostbitten's Animation Blueprint. The skeletons differ (read-only check in
Saved/Logs/create_frozen_pmc_fighter.txt, docs/systems/enemy-ai-and-perception.md "Frozen PMC Fighter"):

  Frostbitten  /Game/mutant_monster_2/base_mesh/SK_UE4_Mannequin_Skeleton      UE4 mannequin, 68 bones
  PMC body     Cyberpunk_Explorer_A_UE5 -> /Game/Characters/Mannequins/Meshes/SK_Mannequin (Epic UE5 Manny, 161 bones = the
               68 + spine_04/05, neck_02, metacarpals, twist / corrective bones; 12 shared bones have another parent:
               clavicles / neck on spine_05, head on neck_02, finger roots on the metacarpals; arms ~20 deg lower in the
               reference pose). The MESH has 162 bones: its FBX scene root "SKM_Cyberpunk_Explorer_A" sits above "root" and is
               missing from SK_Mannequin (and cannot be merged into it: a second root). USkeletalMeshComponent spawns an ABP
               only when its target skeleton holds every mesh bone (USkeleton::IsCompatibleMesh), so NO Animation Blueprint
               on SK_Mannequin can drive this mesh as imported (T-pose; the editor logs "SK_Mannequin is missing bones ...
               Please save the Skeleton!" at every load, and a Layered-blend ensure fires for the unmapped bone).

So ABP_Enemy_Frostbitten cannot run the body (other skeleton, other hierarchy) and "Compatible Skeletons" would neither fix the
hierarchy nor be allowed (it re-saves the user's / Epic template skeleton). Route = IK retarget onto a Claude-owned copy, every
asset under /Game/Characters/Enemies/PMC/FrozenPMC_Fighter/ (the user's mesh / clips / ABP / skeletons are only read):

  Anim/SK_FrozenPMC_Fighter_Skeleton  built from the pack mesh's own bone tree (162 bones, Manny's translation retargeting modes
                                      and slot groups; SK_Mannequin listed as compatible)
  Mesh/SKM_FrozenPMC_Fighter          a copy of Cyberpunk_Explorer_A_UE5 (same geometry, the pack's materials and physics asset
                                      by reference) pointed at that skeleton - the only change from the pack's mesh
  Anim/IK_Frostbitten_Source          IK Rig on the Frostbitten mesh (auto retarget chains + auto FBIK)
  Anim/IK_FrozenPMC_Fighter           IK Rig on the copy (auto retarget chains + auto FBIK)
  Anim/RTG_Frostbitten_To_FrozenPMC   IK Retargeter (default op stack, chains auto-mapped, target pose auto-aligned to the source)
  Anim/ABP_Enemy_FrozenPMC_Fighter    + the 7 Frostbitten clips (idle, walk, run, 2 attacks, hit, death) retargeted by
                                      IKRetargetBatchOperation (duplicate-and-retarget of ABP_Enemy_Frostbitten: the same graph,
                                      the same rates, the class-default clips remapped to the copies)

and BP_Enemy_FrozenPMC_Fighter = a CHILD of BP_Enemy_Frostbitten (inherits the archetype through the class mapping, the AI
tunables, the LookAuthored tag), with the mesh copy, the retargeted ABP, capsule 40/90, mesh z -90 / yaw -90 / scale 1 (the
181 cm body has its feet at its origin and faces +Y like the Frostbitten), the nameplate "Frozen PMC" (DisplayNameOverride)
and the knockdown clips of /Game/Animations_KnockDown itself (the UE5 Manny "RTGT" originals the operatives use; the
Frostbitten's copies sit on its UE4 skeleton).

Once the user removes the stray bone from his mesh (Skeletal Mesh editor -> Skeleton Editing -> delete "SKM_Cyberpunk_Explorer_A",
or a re-import), the BP can point at the pack's mesh again only together with an ABP on SK_Mannequin (re-run the batch).

Idempotent: every asset is created only when missing; an existing BP / ABP / mesh is never overwritten (a user-saved one is left
as it is; CODEX_PMC_REAPPLY=1 re-applies the BP fields). Nothing outside /Game/Characters/Enemies/PMC is saved.

Run with this checkout's editor closed (commandlet):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
Result: Saved/Logs/create_frozen_pmc_fighter.txt (skeleton comparison, assets, retarget proportion check).
"""
import os

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
graph = unreal.OperativeAnimGraphLibrary
REAPPLY = os.environ.get("CODEX_PMC_REAPPLY") == "1"
LOCAL = unreal.AnimPoseSpaces.LOCAL
WORLD = unreal.AnimPoseSpaces.WORLD
SOURCE = unreal.RetargetSourceOrTarget.SOURCE
TARGET = unreal.RetargetSourceOrTarget.TARGET

ROOT = "/Game/Characters/Enemies/PMC/FrozenPMC_Fighter"
ANIM = ROOT + "/Anim"
PACK_MESH = "/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5"
FROST_MESH = "/Game/mutant_monster_2/base_mesh/SK_base_mesh"
FROST_ABP = "/Game/Characters/Enemies/Frostbitten/ABP_Enemy_Frostbitten"
FROST_BP = "/Game/Characters/Enemies/Frostbitten/BP_Enemy_Frostbitten"
PMC_SKELETON = ANIM + "/SK_FrozenPMC_Fighter_Skeleton"
PMC_MESH = ROOT + "/Mesh/SKM_FrozenPMC_Fighter"
SOURCE_RIG = ANIM + "/IK_Frostbitten_Source"
TARGET_RIG = ANIM + "/IK_FrozenPMC_Fighter"
RETARGETER = ANIM + "/RTG_Frostbitten_To_FrozenPMC"
PMC_ABP = ANIM + "/ABP_Enemy_FrozenPMC_Fighter"
PMC_BP = ROOT + "/BP_Enemy_FrozenPMC_Fighter"
DISPLAY_NAME = "Frozen PMC"
CLIP_PROPS = ("idle_animations", "walk_animation", "run_animation", "attack_animations", "hit_animations", "death_animations",
              "jump_attack_animation")
log = []
created = []


def say(text):
    log.append(text)
    unreal.log("create_frozen_pmc_fighter: " + text)


def asset_name(path):
    return path.rsplit("/", 1)[1]


def folder(path):
    return path.rsplit("/", 1)[0]


def bones(skeleton):
    return [str(n) for n in skeleton.get_reference_pose().get_bone_names()]


def unpack(result):
    """UFUNCTIONs with an out FString come back as (return value, report)."""
    return result if isinstance(result, tuple) else (result, "")


def probe_pose(mesh, clip, time_s=0.0):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, time_s, options)


def compare_skeletons(pack_mesh, frost_mesh):
    pack_skeleton = pack_mesh.get_editor_property("skeleton")
    frost_skeleton = frost_mesh.get_editor_property("skeleton")
    a, b = bones(pack_skeleton), bones(frost_skeleton)
    say("skeleton PMC   %s: %d bones" % (pack_skeleton.get_path_name(), len(a)))
    say("skeleton Frost %s: %d bones" % (frost_skeleton.get_path_name(), len(b)))
    say("  shared names %d, only PMC %d (spine_04/05, neck_02, metacarpals, twist / corrective ...), only Frostbitten %d"
        % (len(set(a) & set(b)), len(set(a) - set(b)), len(set(b) - set(a))))
    diffs = []
    for name in sorted(set(a) & set(b)):
        if name == "root":
            continue
        pa, pb = str(pack_mesh.get_bone_parent(name)), str(frost_mesh.get_bone_parent(name))
        if pa != pb:
            diffs.append("%s (%s vs %s)" % (name, pa, pb))
    say("  parent differences %d: %s" % (len(diffs), ", ".join(diffs)))
    say("  same skeleton asset: %s; compatible link: %s" % (
        pack_skeleton == frost_skeleton,
        any(c and c.get_path_name() == frost_skeleton.get_path_name() for c in pack_skeleton.get_editor_property("compatible_skeletons"))))
    say("  pack mesh root bone: parent of 'root' = %s (not in %s: the mesh has one bone more than its skeleton)"
        % (pack_mesh.get_bone_parent("root"), pack_skeleton.get_name()))


def ensure_skeleton(pack_mesh):
    skeleton, report = unpack(graph.create_skeleton_from_mesh(pack_mesh, PMC_SKELETON, pack_mesh.get_editor_property("skeleton")))
    say("skeleton: " + report)
    if skeleton is None:
        raise RuntimeError("no skeleton")
    if report.startswith("created"):
        library.save_loaded_asset(skeleton, False)
        created.append(PMC_SKELETON)
    return skeleton


def ensure_mesh(pack_mesh, skeleton):
    if library.does_asset_exist(PMC_MESH):
        say("kept %s" % PMC_MESH)
        return library.load_asset(PMC_MESH)
    mesh = library.duplicate_asset(PACK_MESH, PMC_MESH)
    # A bool UFUNCTION with an out FString comes back as the string alone (the bool is dropped by the Python binding).
    result = graph.set_skeletal_mesh_skeleton(mesh, skeleton)
    ok, report = result if isinstance(result, tuple) else (" -> " in str(result), str(result))
    if not ok:
        raise RuntimeError("mesh copy: " + report)
    skeleton.set_preview_mesh(mesh) if hasattr(skeleton, "set_preview_mesh") else None
    library.save_loaded_asset(mesh, False)
    library.save_loaded_asset(skeleton, False)
    created.append(PMC_MESH)
    say("created %s (copy of %s on %s: %s)" % (PMC_MESH, asset_name(PACK_MESH), asset_name(PMC_SKELETON), report))
    return mesh


def ensure_ik_rig(path, mesh):
    if library.does_asset_exist(path):
        say("kept %s" % path)
        return library.load_asset(path)
    rig = tools.create_asset(asset_name(path), ANIM, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(mesh)
    controller.apply_auto_generated_retarget_definition()
    controller.apply_auto_fbik()
    chains = [str(c.get_editor_property("chain_name")) for c in controller.get_retarget_chains()]
    say("created %s on %s: root %s, %d chains %s" % (path, mesh.get_name(), controller.get_retarget_root(), len(chains), chains))
    library.save_loaded_asset(rig, False)
    created.append(path)
    return rig


def ensure_retargeter(source_rig, target_rig, frost_mesh, pmc_mesh):
    if library.does_asset_exist(RETARGETER):
        say("kept %s" % RETARGETER)
        return library.load_asset(RETARGETER)
    retargeter = tools.create_asset(asset_name(RETARGETER), ANIM, unreal.IKRetargeter, unreal.IKRetargetFactory())
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    controller.set_ik_rig(SOURCE, source_rig)
    controller.set_ik_rig(TARGET, target_rig)
    controller.set_preview_mesh(SOURCE, frost_mesh)
    controller.set_preview_mesh(TARGET, pmc_mesh)
    if controller.get_num_retarget_ops() == 0:
        controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(TARGET, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Both reference poses are A-poses, the Manny's arms ~20 deg lower: align the target's retarget pose to the source's.
    controller.auto_align_all_bones(TARGET, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    ops = [str(controller.get_op_name(i)) for i in range(controller.get_num_retarget_ops())]
    say("created %s: ops %s" % (RETARGETER, ops))
    library.save_loaded_asset(retargeter, False)
    created.append(RETARGETER)
    return retargeter


def abp_clips(cdo):
    clips = {}
    for prop in CLIP_PROPS:
        value = cdo.get_editor_property(prop)
        clips[prop] = [x for x in (list(value) if isinstance(value, unreal.Array) else [value]) if x]
    return clips


def ensure_abp(retargeter, frost_mesh, pmc_mesh):
    if library.does_asset_exist(PMC_ABP):
        say("kept %s" % PMC_ABP)
        return library.load_asset(PMC_ABP)
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", [library.find_asset_data(FROST_ABP)])
    inputs.set_editor_property("source_mesh", frost_mesh)
    inputs.set_editor_property("target_mesh", pmc_mesh)
    inputs.set_editor_property("ik_retarget_asset", retargeter)
    inputs.set_editor_property("search", "Frostbitten")
    inputs.set_editor_property("replace", "FrozenPMC_Fighter")
    inputs.set_editor_property("target_path", ANIM)
    inputs.set_editor_property("include_referenced_assets", True)
    inputs.set_editor_property("overwrite_existing_files", False)
    before = set(p.split(".")[0] for p in library.list_assets(ANIM, False, False))
    outputs = [str(d.package_name) for d in unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)]
    for package in outputs:
        if not package.startswith(ROOT + "/"):
            say("  WARNING batch output outside %s: %s (not saved)" % (ROOT, package))
    # The returned list holds the clips only; the duplicated ABP is new in the folder too: save everything new there.
    for package in sorted(set(p.split(".")[0] for p in library.list_assets(ANIM, False, False)) - before):
        library.save_asset(package, False)
        created.append(package)
        say("  retargeted %s" % package)
    if not library.does_asset_exist(PMC_ABP):
        raise RuntimeError("batch retarget did not create %s" % PMC_ABP)
    return library.load_asset(PMC_ABP)


def check_abp(abp, pmc_mesh):
    skeleton = pmc_mesh.get_editor_property("skeleton")
    target = abp.get_editor_property("target_skeleton")
    say("ABP %s target skeleton %s (the mesh's: %s), %d graph nodes" % (abp.get_name(), target.get_path_name(), target == skeleton,
                                                                         graph.count_anim_graph_nodes(abp)))
    cdo = unreal.get_default_object(unreal.load_object(None, PMC_ABP + "." + asset_name(PMC_ABP) + "_C"))
    bad = 0 if target == skeleton else 1
    clips = abp_clips(cdo)
    for prop, items in clips.items():
        for clip in items:
            ok = clip.get_editor_property("skeleton") == skeleton and clip.get_path_name().startswith(ROOT + "/")
            bad += 0 if ok else 1
            say("  %-22s %-60s %.2f s %s" % (prop, clip.get_path_name(), clip.get_play_length(), "ok" if ok else "NOT RETARGETED"))
    return clips, bad


CHAINS = {
    "torso": ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02", "head"],
    "leg": ["calf_l", "foot_l", "calf_r", "foot_r"],
    "arm": ["lowerarm_l", "hand_l", "lowerarm_r", "hand_r"],
}


def proportion_check(clips, pmc_mesh):
    """Per clip: body chain length vs the mesh's own (1.00 = no stretch) and the lowest toe over the clip (ground contact)."""
    worst = {}
    for prop, items in clips.items():
        for clip in items:
            length = clip.get_play_length()
            row = {}
            low_toe = 1.0e9
            ref = None
            for f in (0.0, 0.25, 0.5, 0.75, 0.99):
                pose = probe_pose(pmc_mesh, clip, length * f)
                if ref is None:
                    ref = {k: sum(pose.get_ref_bone_pose(b, LOCAL).translation.length() for b in bs) for k, bs in CHAINS.items()}
                for k, bs in CHAINS.items():
                    ratio = sum(pose.get_bone_pose(b, LOCAL).translation.length() for b in bs) / max(ref[k], 0.01)
                    row[k] = max(row.get(k, 1.0), ratio, key=lambda r: abs(r - 1.0))
                low_toe = min(low_toe, pose.get_bone_pose("ball_l", WORLD).translation.z, pose.get_bone_pose("ball_r", WORLD).translation.z)
            for k, v in row.items():
                worst[k] = max(worst.get(k, 1.0), v, key=lambda r: abs(r - 1.0))
            say("  proportions %-24s torso x%.2f leg x%.2f arm x%.2f, lowest toe %.1f cm" % (clip.get_name(), row["torso"], row["leg"],
                                                                                              row["arm"], low_toe))
    return worst


def ensure_bp(pmc_mesh):
    if library.does_asset_exist(PMC_BP):
        bp = library.load_asset(PMC_BP)
        say("kept %s%s" % (PMC_BP, " (re-applying the fields: CODEX_PMC_REAPPLY=1)" if REAPPLY else ""))
        if not REAPPLY:
            return bp
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_object(None, FROST_BP + ".BP_Enemy_Frostbitten_C"))
        bp = tools.create_asset(asset_name(PMC_BP), ROOT, None, factory)
        created.append(PMC_BP)
        say("created %s (child of BP_Enemy_Frostbitten)" % PMC_BP)
    cdo = unreal.get_default_object(unreal.load_object(None, PMC_BP + "." + asset_name(PMC_BP) + "_C"))
    body = cdo.get_editor_property("mesh")
    capsule = cdo.get_editor_property("capsule_component")
    body.set_editor_property("skeletal_mesh_asset", pmc_mesh)
    body.set_editor_property("override_materials", [])  # the pack's own materials (referenced by the copy)
    body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
    body.set_editor_property("anim_class", unreal.load_object(None, PMC_ABP + "." + asset_name(PMC_ABP) + "_C"))
    # The parent's capsule 40 / 90, mesh at z -90 turned yaw -90, scale 1 fit this body; written explicitly so a later edit of
    # the Frostbitten does not move him.
    capsule.set_editor_property("capsule_radius", 40.0)
    capsule.set_editor_property("capsule_half_height", 90.0)
    body.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
    body.set_editor_property("relative_rotation", unreal.Rotator(0.0, 0.0, -90.0))
    body.set_editor_property("relative_scale3d", unreal.Vector(1.0, 1.0, 1.0))
    cdo.set_editor_property("display_name_override", DISPLAY_NAME)
    cdo.set_editor_property("override_knockdown_clip_folder", True)
    cdo.set_editor_property("knockdown_clip_folder", "")
    cdo.set_editor_property("can_climb_ladders", True)  # a zombified human climbs ladders (docs/design/LADDERS.md 7.5)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    library.save_loaded_asset(bp, False)
    say("BP: mesh %s, anim %s, capsule 40/90, mesh z -90 yaw -90 scale 1, name \"%s\", knockdown clips /Game/Animations_KnockDown"
        % (pmc_mesh.get_name(), asset_name(PMC_ABP), DISPLAY_NAME))
    return bp


try:
    pack_mesh = library.load_asset(PACK_MESH)
    frost_mesh = library.load_asset(FROST_MESH)
    bounds = pack_mesh.get_bounds()
    say("pack mesh bounds z %.1f..%.1f cm, %d material slots" % (bounds.origin.z - bounds.box_extent.z, bounds.origin.z + bounds.box_extent.z,
                                                                 len(pack_mesh.get_editor_property("materials"))))
    compare_skeletons(pack_mesh, frost_mesh)
    skeleton = ensure_skeleton(pack_mesh)
    pmc_mesh = ensure_mesh(pack_mesh, skeleton)
    source_rig = ensure_ik_rig(SOURCE_RIG, frost_mesh)
    target_rig = ensure_ik_rig(TARGET_RIG, pmc_mesh)
    retargeter = ensure_retargeter(source_rig, target_rig, frost_mesh, pmc_mesh)
    abp = ensure_abp(retargeter, frost_mesh, pmc_mesh)
    clips, bad = check_abp(abp, pmc_mesh)
    worst = proportion_check(clips, pmc_mesh)
    say("worst proportions over all clips: %s" % {k: round(v, 3) for k, v in worst.items()})
    ensure_bp(pmc_mesh)
    say("RESULT: %s (%d problems, created %d assets)" % ("PASS" if bad == 0 else "CHECK", bad, len(created)))
except Exception:
    import traceback
    say("ERROR " + traceback.format_exc())

with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "create_frozen_pmc_fighter.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
