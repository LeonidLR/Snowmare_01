"""The optimized weapon models (the designer, 2026-11-08: weapons optimized like the characters - decimated, ONE material, one 1K atlas) from the art
agent's package (branch art/weapon-optimization; the exports are git-ignored there, read from that worktree):
  ArtPipeline/Weapons/Pistol9mm/05_Export/SM_Pistol9mm_<Part>.fbx (12 parts, one slot M_Pistol9mm, shared pivot = the grip centre, muzzle +X,
      up +Z, cm; attachments in their mounted pose) -> /Game/Weapons/Pistol9mm/ (Pistol, Magazine, Bullet) + /Game/Weapons/Pistol9mm/Attachments/
  ArtPipeline/Weapons/ShotgunD1/05_Export/SK_ShotgunD1_1slot.fbx + _Pump_1slot.fbx (the original skeletons / scale / pivot, one slot M_ShotgunD1)
      -> /Game/Weapons/ShotgunD1/, on the user's original skeletons in /Game/Fab/_Model_D-1__Shotgun (used, never saved)
  04_Textures_Baked/T_<Weapon>_{BaseColor,Normal,ORM}.png (1K, as given) -> <weapon>/Textures
One material instance per weapon from /Game/Weapons/Materials/M_WeaponMaster (BaseColor sRGB, Normal TC_Normalmap DirectX, ORM TC_Masks
R=AO G=Roughness B=Metallic; no emissive for now - the designer's decision is pending).

  UnrealEditor-Cmd.exe <CodexTactics.uproject> -run=pythonscript -script=<abs path> -unattended -nullrhi
Report: Saved/Logs/import_optimized_weapons.txt
"""
import os

import unreal

PACK = r"C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-artpipe/ArtPipeline/Weapons"
MASTER = "/Game/Weapons/Materials/M_WeaponMaster"
FAB = "/Game/Fab/_Model_D-1__Shotgun/model_d00m_1_shotgun/SkeletalMeshes"
PISTOL_MAIN = ("Pistol", "Magazine", "Bullet")
lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
log = []


def say(text):
    log.append(text)
    unreal.log("import_optimized_weapons: " + text)


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
    assets = [lib.load_asset(p) for p in task.imported_object_paths]
    return [a for a in assets if a]


def master():
    if lib.does_asset_exist(MASTER):
        return lib.load_asset(MASTER)
    folder, name = MASTER.rsplit("/", 1)
    mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())

    def param(cls, pname, x, y, sampler=None):
        node = mel.create_material_expression(mat, cls, x, y)
        node.set_editor_property("parameter_name", pname)
        if sampler is not None:
            node.set_editor_property("sampler_type", sampler)
        return node
    base = param(unreal.MaterialExpressionTextureSampleParameter2D, "BaseColor", -500, -250)
    normal = param(unreal.MaterialExpressionTextureSampleParameter2D, "Normal", -500, 0, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    orm = param(unreal.MaterialExpressionTextureSampleParameter2D, "ORM", -500, 250, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    mel.connect_material_property(base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(orm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(orm, "B", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(mat)
    lib.save_loaded_asset(mat, False)
    say(f"master {MASTER} created (BaseColor / Normal / ORM, opaque, no emissive)")
    return mat


def material_for(weapon, root):
    textures = {}
    for kind in ("BaseColor", "Normal", "ORM"):
        name = f"T_{weapon}_{kind}"
        tex = run(os.path.join(PACK, weapon, "04_Textures_Baked", name + ".png"), root + "/Textures", name)
        tex = tex[0] if tex else None
        if not tex:
            say(f"  {name}: NOT imported")
            continue
        if kind == "Normal":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("flip_green_channel", False)  # DirectX, as UE expects
        elif kind == "ORM":
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        lib.save_loaded_asset(tex, False)
        textures[kind] = tex
    path = f"{root}/Materials/MI_{weapon}"
    mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(f"MI_{weapon}", root + "/Materials", unreal.MaterialInstanceConstant,
                                                                                    unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master())
    for kind, tex in textures.items():
        mel.set_material_instance_texture_parameter_value(mi, kind, tex)
    lib.save_loaded_asset(mi, False)
    say(f"{path}: {sorted(textures)}")
    return mi


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


def skeletal_options(skeleton):
    o = unreal.FbxImportUI()
    o.import_mesh = True
    o.import_as_skeletal = True
    o.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    o.skeleton = skeleton
    o.import_materials = False
    o.import_textures = False
    o.import_animations = False
    o.create_physics_asset = False
    o.skeletal_mesh_import_data.import_uniform_scale = 1.0
    return o


def bounds(mesh):
    if isinstance(mesh, unreal.StaticMesh):
        box = mesh.get_bounding_box()
        size, low = box.max - box.min, box.min
    else:
        b = mesh.get_bounds()
        ext, origin = b.box_extent, b.origin
        size, low = unreal.Vector(ext.x * 2, ext.y * 2, ext.z * 2), origin - ext
    return f"{size.x:.1f} x {size.y:.1f} x {size.z:.1f} (min {low.x:.1f}, {low.y:.1f}, {low.z:.1f})"


def slot_assign(mesh, mi):
    if isinstance(mesh, unreal.StaticMesh):
        for i in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(i, mi)
    else:
        mats = mesh.get_editor_property("materials")
        for m in mats:
            m.set_editor_property("material_interface", mi)
        mesh.set_editor_property("materials", mats)
    lib.save_loaded_asset(mesh, False)


# --- Pistol 9mm --------------------------------------------------------------------------------------------------------------
root = "/Game/Weapons/Pistol9mm"
mi = material_for("Pistol9mm", root)
export = os.path.join(PACK, "Pistol9mm", "05_Export")
for fbx in sorted(f for f in os.listdir(export) if f.lower().endswith(".fbx")):
    name = os.path.splitext(fbx)[0]
    part = name.replace("SM_Pistol9mm_", "")
    folder = root if part in PISTOL_MAIN else root + "/Attachments"
    meshes = [a for a in run(os.path.join(export, fbx), folder, name, static_options()) if isinstance(a, unreal.StaticMesh)]
    if not meshes:
        say(f"  {name}: NOT imported")
        continue
    slot_assign(meshes[0], mi)
    say(f"  {folder}/{name}: {bounds(meshes[0])} cm, {meshes[0].get_num_triangles(0)} tris")

# --- Shotgun D-1 ---------------------------------------------------------------------------------------------------------------
root = "/Game/Weapons/ShotgunD1"
mi = material_for("ShotgunD1", root)
export = os.path.join(PACK, "ShotgunD1", "05_Export")
for fbx, skeleton_name, original in (("SK_ShotgunD1_1slot.fbx", "Stock_Plastic_-_Matte__Dark_Gray__0_Skeleton", "Stock_Plastic_-_Matte__Dark_Gray__0"),
                                     ("SK_ShotgunD1_Pump_1slot.fbx", "Pump_01_Skeleton", "Object_29")):
    skeleton = lib.load_asset(f"{FAB}/{skeleton_name}")
    if not skeleton:
        say(f"  no skeleton {FAB}/{skeleton_name}")
        continue
    name = os.path.splitext(fbx)[0]
    meshes = [a for a in run(os.path.join(export, fbx), root, name, skeletal_options(skeleton)) if isinstance(a, unreal.SkeletalMesh)]
    if not meshes:
        say(f"  {name}: NOT imported")
        continue
    slot_assign(meshes[0], mi)
    old = lib.load_asset(f"{FAB}/{original}")
    say(f"  {root}/{name}: {bounds(meshes[0])}; the original {original}: {bounds(old) if old else '?'}; skeleton {meshes[0].get_editor_property('skeleton').get_name()}")
say("done (the Fab pack's assets are only read; its skeletons are not saved)")
with open(os.path.join(unreal.Paths.project_saved_dir(), "Logs", "import_optimized_weapons.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(log) + "\n")
