"""Fixes /Game/Weapons/Materials/M_WeaponMaster so that it compiles (2026-11-09, branch fix/weapon-master-material).

Bug: the Normal (sampler Normal) and ORM (sampler Masks) texture parameters had no default texture, so the compiler fell back to
/Engine/EngineResources/DefaultTexture (a colour texture) -> "Sampler type is Normal / Masks, should be Color" -> the master failed to
compile and every weapon material instance rendered with the default grey material.

Fix (idempotent):
  * /Game/Weapons/Materials/T_WeaponDefault_ORM: 4x4 linear TC_Masks texture, AO=1 (R), roughness=0.5 (G), metallic=0 (B).
  * Normal parameter default = /Engine/EngineMaterials/DefaultNormal (TC_Normalmap), ORM default = T_WeaponDefault_ORM,
    BaseColor default = /Engine/EngineMaterials/DefaultWhiteGrid (sRGB colour).
  * Used with Skeletal Mesh is switched on (the D-1 is skinned; without it the skinned meshes fall back to the default material).
  * the master is recompiled and saved; every weapon MI is checked (parent, 3 texture parameters, textures set).

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/fix_weapon_master_defaults.txt
"""
import os

import unreal

MASTER = "/Game/Weapons/Materials/M_WeaponMaster"
ORM_DEFAULT = "/Game/Weapons/Materials/T_WeaponDefault_ORM"
NORMAL_DEFAULT = "/Engine/EngineMaterials/DefaultNormal"
COLOR_DEFAULT = "/Engine/EngineMaterials/DefaultWhiteGrid"
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
log = []


def say(text):
    log.append(text)
    unreal.log("fix_weapon_master_defaults: " + text)


def build_orm_from_png():
    """Texture2DFactoryNew has no pixel API in Python: write a PNG to Saved/ and import it (6 bytes of data, no source art)."""
    import struct
    import zlib
    w = h = 4
    row = b"\x00" + bytes([255, 128, 0]) * w          # filter 0, R=AO 255, G=rough 128, B=metal 0
    raw = row * h

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    path = os.path.join(unreal.Paths.project_saved_dir(), "T_WeaponDefault_ORM.png")
    with open(path, "wb") as f:
        f.write(png)
    task = unreal.AssetImportTask()
    task.filename = path
    task.destination_path = "/Game/Weapons/Materials"
    task.destination_name = "T_WeaponDefault_ORM"
    task.automated = True
    task.replace_existing = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = lib.load_asset(ORM_DEFAULT)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    lib.save_loaded_asset(tex, False)
    return tex


orm_tex = build_orm_from_png()
normal_tex = unreal.load_asset(NORMAL_DEFAULT)
color_tex = unreal.load_asset(COLOR_DEFAULT)
say(f"defaults: ORM {orm_tex.get_path_name()} (masks, linear), Normal {normal_tex.get_path_name()}, BaseColor {color_tex.get_path_name()}")

mat = lib.load_asset(MASTER)
# Material.Expressions is protected in Python: reach the three parameters through the pins they feed.
for pname, prop, tex in (("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR, color_tex), ("Normal", unreal.MaterialProperty.MP_NORMAL, normal_tex),
                         ("ORM", unreal.MaterialProperty.MP_ROUGHNESS, orm_tex)):
    expr = mel.get_material_property_input_node(mat, prop)
    if not isinstance(expr, unreal.MaterialExpressionTextureSampleParameter2D) or str(expr.get_editor_property("parameter_name")) != pname:
        say(f"  ERROR: pin of {pname} is not the {pname} parameter ({expr})")
        continue
    expr.set_editor_property("texture", tex)
    say(f"  master parameter {pname}: default -> {tex.get_name()}, sampler {expr.get_editor_property('sampler_type')}")
# The D-1 (and its pump) are SKELETAL meshes: a master without "Used with Skeletal Mesh" renders them with the default material.
mat.set_editor_property("used_with_skeletal_mesh", True)
say("  master: used_with_skeletal_mesh = True (the D-1 gun and pump are skeletal meshes)")
mel.recompile_material(mat)
lib.save_loaded_asset(mat, False)

# The D-1 gun and pump (skeletal meshes) kept the engine's WorldGridMaterial in their single slot (the importer's slot assignment never took): point every slot of the weapon meshes whose slot is named M_<Weapon> at MI_<Weapon>. Only the mesh packages are saved - the Fab
# pack's skeletons are never touched (TRAP, HANDOFF 2026-11-09 a).
for mesh_path, mi_path in (("/Game/Weapons/ShotgunD1/SK_ShotgunD1_1slot", "/Game/Weapons/ShotgunD1/Materials/MI_ShotgunD1"),
                           ("/Game/Weapons/ShotgunD1/SK_ShotgunD1_Pump_1slot", "/Game/Weapons/ShotgunD1/Materials/MI_ShotgunD1")):
    mesh, mi = lib.load_asset(mesh_path), lib.load_asset(mi_path)
    # TRAP: the slot structs read from the mesh are copies - assigning material_interface on them and writing the array back is silently
    # ignored; a FRESH unreal.SkeletalMaterial (same slot name) in the array is accepted.
    mats = [unreal.SkeletalMaterial(material_interface=mi, material_slot_name=slot.get_editor_property("material_slot_name"))
            for slot in mesh.get_editor_property("materials")]
    mesh.set_editor_property("materials", mats)
    now = [m.get_editor_property("material_interface") for m in mesh.get_editor_property("materials")]
    say(f"  {mesh_path}: slots -> {[x.get_name() if x else None for x in now]}")
    lib.save_loaded_asset(mesh, False)

# Every MI of the master must keep its own textures and compile.
registry = unreal.AssetRegistryHelpers.get_asset_registry()
bad = 0
for data in registry.get_assets_by_path("/Game/Weapons", recursive=True):
    if str(data.asset_class_path.asset_name) != "MaterialInstanceConstant":
        continue
    mi = data.get_asset()
    if mi.get_editor_property("parent") != mat:
        continue
    state = []
    for pname in ("BaseColor", "Normal", "ORM"):
        tex = mel.get_material_instance_texture_parameter_value(mi, pname)
        state.append(f"{pname}={tex.get_name() if tex else 'NONE'}")
        if not tex or tex.get_name() in (orm_tex.get_name(), "DefaultNormal", "DefaultWhiteGrid"):
            bad += 1
    say(f"  MI {mi.get_path_name()}: " + ", ".join(state))
say(f"done, {bad} instance parameter(s) without their own texture")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "fix_weapon_master_defaults.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
