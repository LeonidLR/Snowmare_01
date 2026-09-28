"""Imports the operative art from Codex/ASSETS (same sources the Godot project uses):

  - Explorer_Mountain_Climber_Rigged_Unreal_1.glb -> /Game/Characters/Operatives/Explorer (one combined skeletal mesh)
  - m16_01.glb                                    -> /Game/Weapons/M16
  - rifle / prone / injured FBX animations        -> /Game/Characters/Operatives/Animations/<group>

Existing assets are skipped, so re-running never overwrites user edits.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import json
import os
import struct
import unreal

ASSETS = r"C:/Users/Zephyrus15Duo/Documents/Codex/ASSETS"
CHAR_DIR = "/Game/Characters/Operatives/Explorer"
WEAPON_DIR = "/Game/Weapons/M16"
ANIM_ROOT = "/Game/Characters/Operatives/Animations"

ANIM_GROUPS = {
    "Rifle": f"{ASSETS}/FBX/RifleAnimations/Game/RifleAnims/Retargeted_UE5",
    "RifleIdle": f"{ASSETS}/FBX/RifleIdle",
    "Prone": f"{ASSETS}/FBX/ProneStartEndIdle",
    "Injured": f"{ASSETS}/FBX/InjuredAnimations",
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary


def run_import(source, destination, options=None):
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = destination
    task.automated = True
    task.replace_existing = False
    task.save = True
    if options is not None:
        task.options = options
    tools.import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    unreal.log(f"IMPORT {os.path.basename(source)} -> {len(paths)} objects {paths[:4]}")
    return paths


def find_assets(folder, class_name):
    if not library.does_directory_exist(folder):
        return []
    found = []
    for path in library.list_assets(folder, recursive=True, include_folder=False):
        data = library.find_asset_data(path)
        if str(data.asset_class_path.asset_name) == class_name:
            found.append(path)
    return found


def combine_glb_meshes(source, target):
    """Writes a copy of a skinned glb whose mesh nodes are merged into one mesh (all primitives kept).

    The Explorer glb has 17 mesh nodes sharing one skin; Interchange creates a skeleton per node, so the
    character would import as 17 skeletal meshes. Merging the primitives gives one mesh and one skeleton.
    """
    with open(source, "rb") as f:
        _magic, version, _length = struct.unpack("<III", f.read(12))
        json_len, _ = struct.unpack("<II", f.read(8))
        gltf = json.loads(f.read(json_len))
        bin_len, bin_type = struct.unpack("<II", f.read(8))
        binary = f.read(bin_len)
    nodes = gltf["nodes"]
    mesh_nodes = [i for i, node in enumerate(nodes) if "mesh" in node]
    keep = mesh_nodes[0]
    merged = gltf["meshes"][nodes[keep]["mesh"]]
    merged["name"] = "Explorer"
    # The coat gets its own material slot: Godot tints only Explorer_Coat with the role colour.
    coat_material = dict(gltf["materials"][0], name="Explorer_Coat")
    gltf["materials"].append(coat_material)
    for index in mesh_nodes:
        if nodes[index].get("name") == "Explorer_Coat":
            for primitive in gltf["meshes"][nodes[index]["mesh"]]["primitives"]:
                primitive["material"] = len(gltf["materials"]) - 1
    for index in mesh_nodes[1:]:
        merged["primitives"].extend(gltf["meshes"][nodes[index]["mesh"]]["primitives"])
        del nodes[index]["mesh"]
        nodes[index].pop("skin", None)
        for node in nodes:
            if index in node.get("children", []):
                node["children"].remove(index)
    # Drop the now unreferenced meshes (they would import as static meshes).
    gltf["meshes"] = [merged]
    nodes[keep]["mesh"] = 0
    nodes[keep]["name"] = "Explorer"

    # Make the scene root node the root bone (UE animations carry a "root" track; otherwise Interchange adds
    # root_ProxyTrueRootJoint) and bake its 0.01 scale into the skeleton so "root" has scale 1 like the UE5
    # mannequin: joint translations *= s, inverse bind matrices = S * IBM, root IBM = identity.
    skin = gltf["skins"][0]
    root = gltf["scenes"][0]["nodes"][0]
    scale = nodes[root].get("scale", [1.0, 1.0, 1.0])[0]
    nodes[root].pop("scale", None)
    for joint in skin["joints"]:
        if "translation" in nodes[joint]:
            nodes[joint]["translation"] = [value * scale for value in nodes[joint]["translation"]]
    accessor = gltf["accessors"][skin["inverseBindMatrices"]]
    view = gltf["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    matrices = list(struct.unpack_from(f"<{16 * accessor['count']}f", binary, start))
    for m in range(accessor["count"]):
        for column in range(4):
            for row in range(3):
                matrices[m * 16 + column * 4 + row] *= scale
    matrices += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    skin["joints"].append(root)
    skin["skeleton"] = root
    ibm_bytes = struct.pack(f"<{len(matrices)}f", *matrices)
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(ibm_bytes)})
    gltf["accessors"].append({"bufferView": len(gltf["bufferViews"]) - 1, "componentType": 5126,
                              "count": accessor["count"] + 1, "type": "MAT4"})
    skin["inverseBindMatrices"] = len(gltf["accessors"]) - 1
    binary += ibm_bytes
    gltf["buffers"][0]["byteLength"] = len(binary)

    payload = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    payload += b" " * ((4 - len(payload) % 4) % 4)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(target, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, version, 12 + 8 + len(payload) + 8 + len(binary)))
        f.write(struct.pack("<II", len(payload), 0x4E4F534A))
        f.write(payload)
        f.write(struct.pack("<II", len(binary), bin_type))
        f.write(binary)
    unreal.log(f"COMBINED {len(mesh_nodes)} mesh nodes -> {target}")


# 1. Character: one skeletal mesh with one skeleton.
if not find_assets(CHAR_DIR, "SkeletalMesh"):
    combined = os.path.join(unreal.Paths.project_saved_dir(), "SourceArt", "Explorer.glb")
    combine_glb_meshes(f"{ASSETS}/gLTF/Explorer/Explorer_Mountain_Climber_Rigged_Unreal_1.glb", combined)
    run_import(combined, CHAR_DIR)
meshes = find_assets(CHAR_DIR, "SkeletalMesh")
skeletons = find_assets(CHAR_DIR, "Skeleton")
unreal.log(f"CHARACTER meshes={meshes} skeletons={skeletons}")
if len(skeletons) != 1:
    raise RuntimeError(f"Expected one skeleton, got {skeletons}")
skeleton = library.load_asset(skeletons[0])

# 2. Weapon.
if not find_assets(WEAPON_DIR, "StaticMesh"):
    run_import(f"{ASSETS}/gLTF/Weapons/M16/m16_01.glb", WEAPON_DIR)

# 3. Animations onto the Explorer skeleton through the classic FBX importer (animation-only import).
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")
for group, folder in ANIM_GROUPS.items():
    destination = f"{ANIM_ROOT}/{group}"
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith(".fbx"):
            continue
        asset_name = os.path.splitext(name)[0].replace(".", "_")
        if library.does_asset_exist(f"{destination}/{asset_name}"):
            continue
        ui = unreal.FbxImportUI()
        ui.set_editor_property("automated_import_should_detect_type", False)
        ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
        ui.set_editor_property("import_mesh", False)
        ui.set_editor_property("import_as_skeletal", True)
        ui.set_editor_property("import_animations", True)
        ui.set_editor_property("import_materials", False)
        ui.set_editor_property("import_textures", False)
        ui.set_editor_property("skeleton", skeleton)
        run_import(f"{folder}/{name}", destination, ui)

unreal.log("IMPORT DONE: " + ", ".join(
    f"{group}={len(find_assets(f'{ANIM_ROOT}/{group}', 'AnimSequence'))}" for group in ANIM_GROUPS))
