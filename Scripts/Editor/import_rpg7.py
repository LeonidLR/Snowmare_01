"""The RPG-7 model from the game designer's launcher pack, optimized (2026-11-09, feat/rpg7-launcher-pack): imports the art package
ArtPipeline/Weapons/RPG7 (Scripts/Pipeline/blender_weapon_rpg7.py + rpg7_textures.py; the FBX / PNG are git-ignored there) into the game:
  05_Export/SM_RPG7_{Body,Sight01,Rocket}.fbx -> /Game/Weapons/RPG7/          (the model in the hand: body + the optic + the loaded rocket;
                                                                               the rocket is also the flying projectile)
  05_Export/SM_RPG7_{Sight02,Sight03}.fbx     -> /Game/Weapons/RPG7/Attachments/ (the iron sights, for the later customisation)
  04_Textures_Baked/T_RPG7_{BaseColor,Normal,ORM}.png (1K) -> /Game/Weapons/RPG7/Textures
ONE material instance MI_RPG7 of /Game/Weapons/Materials/M_WeaponMaster (BaseColor sRGB, Normal TC_Normalmap, ORM TC_Masks linear: R = AO,
G = roughness, B = metallic), as the pistol / D-1. Every part is in the pack's weapon frame (cm: muzzle +Y, up +Z), so they all mount at
identity on the body and the body mounts with the pack's hand socket transform. The pack (/Game/Launcher_Animations) is only read, never saved.

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/import_rpg7.txt
"""
import os

import unreal

ROOT = "/Game/Weapons/RPG7"
MASTER = "/Game/Weapons/Materials/M_WeaponMaster"
PACKAGE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "ArtPipeline", "Weapons", "RPG7")
MAIN = ("Body", "Sight01", "Rocket")
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
log = []


def say(text):
    log.append(text)
    unreal.log("import_rpg7: " + text)


def run(filename, dest, name, options=None):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = dest
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    if options:
        task.options = options
    try:
        tools.import_asset_tasks([task])
    except RuntimeError as e:
        say(f"  {name}: {e}")
    return [a for a in (lib.load_asset(p) for p in task.imported_object_paths) if a]


def static_options():
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = False
    o.import_materials = False
    o.import_textures = False
    o.import_animations = False
    o.static_mesh_import_data.combine_meshes = True
    o.static_mesh_import_data.auto_generate_collision = False
    o.static_mesh_import_data.generate_lightmap_u_vs = False
    o.static_mesh_import_data.import_uniform_scale = 1.0
    return o


def material():
    textures = {}
    for kind in ("BaseColor", "Normal", "ORM"):
        name = f"T_RPG7_{kind}"
        tex = run(os.path.join(PACKAGE, "04_Textures_Baked", name + ".png"), ROOT + "/Textures", name)
        tex = tex[0] if tex else None
        if not tex:
            say(f"  {name}: NOT imported")
            continue
        if kind == "Normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("flip_green_channel", False)  # the pack's map is already in Unreal's convention
        elif kind == "ORM":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        lib.save_loaded_asset(tex, False)
        textures[kind] = tex
    path = f"{ROOT}/Materials/MI_RPG7"
    mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset("MI_RPG7", ROOT + "/Materials", unreal.MaterialInstanceConstant,
                                                                                     unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, lib.load_asset(MASTER))
    for kind, tex in textures.items():
        mel.set_material_instance_texture_parameter_value(mi, kind, tex)
    lib.save_loaded_asset(mi, False)
    say(f"{path}: {sorted(textures)} on {MASTER}")
    return mi


def bounds(mesh):
    box = mesh.get_bounding_box()
    size, low = box.max - box.min, box.min
    return f"{size.x:.1f} x {size.y:.1f} x {size.z:.1f} (min {low.x:.1f}, {low.y:.1f}, {low.z:.1f})"


mi = material()
export = os.path.join(PACKAGE, "05_Export")
for fbx in sorted(f for f in os.listdir(export) if f.lower().endswith(".fbx")):
    name = os.path.splitext(fbx)[0]
    part = name.replace("SM_RPG7_", "")
    folder = ROOT if part in MAIN else ROOT + "/Attachments"
    meshes = [a for a in run(os.path.join(export, fbx), folder, name, static_options()) if isinstance(a, unreal.StaticMesh)]
    if not meshes:
        say(f"  {name}: NOT imported")
        continue
    mesh = meshes[0]
    for i in range(len(mesh.get_editor_property("static_materials"))):
        mesh.set_material(i, mi)
    lib.save_loaded_asset(mesh, False)
    say(f"  {folder}/{name}: {bounds(mesh)} cm, {mesh.get_num_triangles(0)} tris, {len(mesh.get_editor_property('static_materials'))} slot(s)")
say("done (the launcher pack is only read)")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_rpg7.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
