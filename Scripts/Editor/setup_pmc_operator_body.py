"""PMC Operator body on the operative Animation Blueprint (user decision 2026-11-03, docs/systems/pmc-squad-ai.md "Body and look").

The PMC fighter body (Mesh/SKM_FrozenPMC_Fighter = the Cyberpunk_Explorer_A body on its own 162-bone Manny-type skeleton
SK_FrozenPMC_Fighter_Skeleton, made by create_frozen_pmc_fighter.py) is now driven by ABP_Operative like every operative body,
instead of the retargeted Frostbitten clips: the operative skeleton (the Explorer mesh's) and the PMC skeleton list each other as
COMPATIBLE SKELETONS, so USkeletalMeshComponent spawns the operative anim instance on the PMC mesh (USkeleton::IsCompatibleMesh accepts
a compatible skeleton since UE 5.1) and every operative clip plays on it by bone name (UE4 names are a subset of the Manny's;
the Manny-only spine_04 / neck_02 / twist bones keep the reference pose). The look variants: material instances of the PMC
mesh's coat material under /Game/Characters/Enemies/PMC/Operator (MI_PmcOperator_Grey / _Dark / _Snowed) with the first
colour-like vector parameter the material exposes set per look (the tint is also applied at runtime through the operative's
role-colour parameter; a material without such a parameter leaves the tint to the user's material work - the report says which).

Idempotent: the compatibility entries are added once, an existing material instance is never overwritten (CODEX_PMC_REAPPLY=1
re-applies the parameter values). Nothing outside /Game/Characters/Enemies/PMC and the operative skeleton is saved.

Run with this checkout's editor closed (commandlet):
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path, forward slashes>" -unattended -nullrhi -nosplash
Result: Saved/Logs/setup_pmc_operator_body.txt (skeletons, material slots + parameters, the instances made).
"""
import os

import unreal

library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
REAPPLY = os.environ.get("CODEX_PMC_REAPPLY") == "1"

PMC_MESH = "/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Mesh/SKM_FrozenPMC_Fighter"
PMC_SKELETON = "/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Anim/SK_FrozenPMC_Fighter_Skeleton"
OPERATIVE_BP = "/Game/Characters/Operatives/BP_Operative"
OPERATOR_DIR = "/Game/Characters/Enemies/PMC/Operator"
# The operator body: a copy of the PMC mesh WITHOUT the FBX scene root bone above "root" (USkeletalMeshComponent spawns an anim
# instance only when every mesh bone - or an ancestor of it - exists in the ABP's target skeleton, USkeleton::IsCompatibleMesh; the
# stray root has no ancestor there). The operative skeleton (the user's pack) is never saved.
OPERATOR_MESH = OPERATOR_DIR + "/SKM_PmcOperator"
OPERATOR_SKELETON = OPERATOR_DIR + "/SK_PmcOperator_Skeleton"
STRAY_ROOT = "SKM_Cyberpunk_Explorer_A"
# look id -> sRGB tint (Content/Data/AI/pmc_operators.json "looks")
LOOKS = {
    "Grey": (154, 158, 163),
    "Dark": (51, 55, 59),
    "Snowed": (216, 220, 224),
}
COLOUR_WORDS = ("color", "colour", "tint", "albedo", "diffuse", "base")
log = []


def say(text):
    log.append(text)
    unreal.log("setup_pmc_operator_body: " + text)


def make_compatible(skeleton, other):
    """Lists other in skeleton's compatible skeletons (an editor filter; the runtime check is bone-based). Only Claude-owned skeletons are saved."""
    if not skeleton or not other or skeleton.get_path_name() == other.get_path_name():
        return False
    if not skeleton.get_path_name().startswith("/Game/Characters/Enemies/PMC/"):
        say("%s is not ours (the user's pack): not touched" % skeleton.get_path_name())
        return False
    compatible = list(skeleton.get_editor_property("compatible_skeletons"))
    if any(c and c.get_path_name() == other.get_path_name() for c in compatible):
        say("%s already compatible with %s" % (skeleton.get_name(), other.get_name()))
        return False
    compatible.append(other)
    skeleton.set_editor_property("compatible_skeletons", compatible)
    library.save_loaded_asset(skeleton)
    say("%s now compatible with %s (saved)" % (skeleton.get_path_name(), other.get_path_name()))
    return True


def bone_names(mesh):
    modifier = unreal.SkeletonModifier()
    return [str(n) for n in modifier.get_all_bone_names()] if mesh and modifier.set_skeletal_mesh(mesh) else []


def operator_mesh(pmc_mesh, op_skeleton):
    """The operator body copy without the stray scene root (created once; an existing copy is kept)."""
    if library.does_asset_exist(OPERATOR_MESH):
        mesh = library.load_asset(OPERATOR_MESH)
        names = bone_names(mesh)
        say("kept %s (%d bones, root '%s')" % (OPERATOR_MESH, len(names), names[0] if names else "?"))
        return mesh
    mesh = library.duplicate_asset(pmc_mesh.get_path_name().split(".")[0], OPERATOR_MESH)
    if not mesh:
        say("could not duplicate %s" % OPERATOR_MESH)
        return None
    names_before = bone_names(mesh)
    # USkeletonModifier refuses to remove a root bone and its commit opens a Slate dialog in a commandlet, so the rename is done on
    # the reference skeleton directly (UOperativeAnimGraphLibrary::RenameMeshBones): the stray scene root BECOMES "root" (the name
    # the ABP's skeleton knows; the clips' root is the identity) and the real root is renamed below it (no clip animates it: it keeps
    # its reference pose).
    renamed, report = False, "stray root absent"
    if STRAY_ROOT in names_before and "root" in names_before:
        result = unreal.OperativeAnimGraphLibrary.rename_mesh_bones(mesh, ["root", STRAY_ROOT], ["root_inner", "root"])
        renamed, report = result if isinstance(result, tuple) else ("renamed" in str(result), str(result))
    say("operator mesh %s: %s (%d bones before)" % (OPERATOR_MESH, report, len(names_before)))
    if not renamed:
        return mesh
    # Its own skeleton from its own (renamed) bone tree, the PMC skeleton as the template (translation retargeting modes, slot groups).
    template = mesh.get_editor_property("skeleton")
    result = unreal.OperativeAnimGraphLibrary.create_skeleton_from_mesh(mesh, OPERATOR_SKELETON, template)
    skeleton, report = result if isinstance(result, tuple) else (result, "")
    say("operator skeleton: %s" % report)
    if skeleton:
        result = unreal.OperativeAnimGraphLibrary.set_skeletal_mesh_skeleton(mesh, skeleton)
        say("operator mesh skeleton: %s" % (result[1] if isinstance(result, tuple) else result))
        make_compatible(skeleton, op_skeleton)
        library.save_loaded_asset(skeleton, False)
    library.save_loaded_asset(mesh, False)
    names_after = bone_names(mesh)
    say("operator mesh saved: %d bones, root '%s' then '%s'" % (len(names_after), names_after[0] if names_after else "?", names_after[1] if len(names_after) > 1 else "?"))
    return mesh


def operative_skeleton():
    bp_class = unreal.load_object(None, OPERATIVE_BP + "." + OPERATIVE_BP.rsplit("/", 1)[1] + "_C")
    cdo = unreal.get_default_object(bp_class) if bp_class else None
    mesh_component = cdo.get_editor_property("mesh") if cdo else None
    mesh = mesh_component.get_editor_property("skeletal_mesh_asset") if mesh_component else None
    skeleton = mesh.get_editor_property("skeleton") if mesh else None
    say("operative body %s on skeleton %s" % (mesh.get_path_name() if mesh else "?", skeleton.get_path_name() if skeleton else "?"))
    return skeleton


def colour_parameter(material):
    """The first vector parameter of the material whose name sounds like a colour (None when it has none)."""
    names = []
    try:
        names = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)]
    except Exception as error:  # a material instance parent chain the library refuses
        say("vector parameter names unavailable: %s" % error)
    say("vector parameters of %s: %s" % (material.get_name(), ", ".join(names) if names else "none"))
    for name in names:
        if any(word in name.lower() for word in COLOUR_WORDS):
            return name
    return names[0] if names else None


def main():
    pmc_mesh = library.load_asset(PMC_MESH)
    pmc_skeleton = library.load_asset(PMC_SKELETON)
    op_skeleton = operative_skeleton()
    if not pmc_mesh or not pmc_skeleton or not op_skeleton:
        say("MISSING: mesh %s, skeleton %s, operative skeleton %s" % (bool(pmc_mesh), bool(pmc_skeleton), bool(op_skeleton)))
        return
    say("PMC mesh %s on skeleton %s" % (pmc_mesh.get_path_name(), pmc_mesh.get_editor_property("skeleton").get_path_name()))
    # 1. The operator body copy without the stray root (the runtime gate), the PMC skeleton listing the operative one (the editor filter).
    body = operator_mesh(pmc_mesh, op_skeleton)
    make_compatible(pmc_skeleton, op_skeleton)
    # 2. The look variants.
    materials = pmc_mesh.get_editor_property("materials")
    say("PMC mesh material slots: %s" % ", ".join("%d:%s=%s" % (i, m.material_slot_name, m.material_interface.get_name() if m.material_interface else "none")
        for i, m in enumerate(materials)))
    coat_index = 0
    coat = materials[coat_index].material_interface if materials else None
    if not coat:
        say("no coat material: no instances made")
        return
    parameter = colour_parameter(coat)
    say("coat slot %d '%s' = %s, tint parameter %s" % (coat_index, materials[coat_index].material_slot_name, coat.get_path_name(), parameter or "NONE"))
    if not library.does_directory_exist(OPERATOR_DIR):
        library.make_directory(OPERATOR_DIR)
    for look, (r, g, b) in LOOKS.items():
        path = "%s/MI_PmcOperator_%s" % (OPERATOR_DIR, look)
        instance = library.load_asset(path) if library.does_asset_exist(path) else None
        fresh = instance is None
        if fresh:
            factory = unreal.MaterialInstanceConstantFactoryNew()
            instance = tools.create_asset("MI_PmcOperator_%s" % look, OPERATOR_DIR, unreal.MaterialInstanceConstant, factory)
            if not instance:
                say("could not create %s" % path)
                continue
            unreal.MaterialEditingLibrary.set_material_instance_parent(instance, coat)
        if fresh or REAPPLY:
            if parameter:
                colour = unreal.LinearColor(r / 255.0, g / 255.0, b / 255.0, 1.0)
                unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(instance, parameter, colour)
            unreal.MaterialEditingLibrary.update_material_instance(instance)
            library.save_loaded_asset(instance)
            say("%s %s (parent %s%s)" % ("created" if fresh else "re-applied", path, coat.get_name(), (", %s = %d %d %d" % (parameter, r, g, b)) if parameter else ", no tint parameter"))
        else:
            say("kept %s" % path)
    say("tint parameter for pmc_operators.json: %s on slot '%s'" % (parameter or "NONE", materials[coat_index].material_slot_name))


try:
    main()
except Exception as error:  # the report must be written whatever happened
    say("ERROR %r" % (error,))
    raise
finally:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "setup_pmc_operator_body.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(log) + "\n")
