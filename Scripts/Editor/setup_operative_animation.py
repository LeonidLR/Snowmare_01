"""Wires the imported operative art into the Blueprints (run after import_operative_assets.py):

  - ABP_Operative: AnimBlueprint (parent UOperativeAnimInstance, Explorer skeleton) with the baseline clips.
    Its graph is empty: the C++ parent blends the clips until the graph is built and bUseNativeLocomotion is off.
  - BP_Operative: Explorer skeletal mesh, ABP_Operative, M16 on hand_r.
  - Root motion clips used in place are root-locked.

Only fills empty slots, so the user's own choices in the Blueprints are kept.

Run headless:
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import unreal

ROOT = "/Game/Characters/Operatives"
ANIMS = f"{ROOT}/Animations"
library = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

skeleton = library.load_asset(f"{ROOT}/Explorer/Explorer/SkeletalMeshes/Explorer_Skeleton")
mesh = library.load_asset(f"{ROOT}/Explorer/Explorer/SkeletalMeshes/Explorer")
rifle = library.load_asset("/Game/Weapons/M16/m16_01/StaticMeshes/m16_01")

CLIPS = {
    "idle": f"{ANIMS}/Rifle/AS_Rifle_Idle",
    "walk": f"{ANIMS}/Rifle/AS_Rifle_WalkFwd",
    "run": f"{ANIMS}/Rifle/AS_Rifle_RunFwd",
    "crouch_idle": f"{ANIMS}/Rifle/AS_Rifle_Crouch",
    "crouch_walk": f"{ANIMS}/Rifle/AS_Rifle_CrouchWalkFwd",
    "prone_idle": f"{ANIMS}/Prone/AS_Prone_Aim_Idle",
    "prone_crawl": f"{ANIMS}/Injured/Crawl_Injured_RM",
}

def godot_transform(values):
    """Godot Transform3D text (basis rows, then origin) -> 4x4 row-major list, column-vector convention."""
    return [values[0:3] + [values[9]], values[3:6] + [values[10]], values[6:9] + [values[11]], [0.0, 0.0, 0.0, 1.0]]


def mat_mul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def rifle_in_hand():
    """M16 offset from hand_r, converted from Godot player_commander.tscn (M16_Socket * M16_Visual * Model * glb node).

    glTF / Godot hand space -> UE hand space: vectors (x, y, z) -> (x, z, y) (Interchange GLTF::ConvertVec3),
    units already cm (the Explorer skeleton is baked to scale 1, the rifle glb node scale 0.01 is baked on import).
    """
    socket = godot_transform([-4.3711392e-08, -1.0000001, -4.371139e-08, 0, -4.3711392e-08, 1, -1.0000001,
                              4.3711392e-08, 1.9106855e-15, 29.803703, -7.7502646, 7.156439])
    visual = godot_transform([-4.371139e-08, 0, -1, 0, 1, 0, 1, 0, -4.371139e-08, 0, 0, 0])
    model = godot_transform([-33.244003, 12.883988, -93.42826, 19.554504, 97.851425, 6.5359964, 92.26297, -16.096607,
                             -35.049137, -1.1107006, 3.2056975, -0.8231326])
    node_scale = [[0.01, 0, 0, 0], [0, 0.01, 0, 0], [0, 0, 0.01, 0], [0, 0, 0, 1]]
    m = mat_mul(mat_mul(mat_mul(socket, visual), model), node_scale)
    swap = [0, 2, 1]  # P: swap Y and Z
    rot = [[m[swap[r]][swap[c]] for c in range(3)] for r in range(3)]  # P * A * P
    loc = [m[swap[r]][3] for r in range(3)]
    # FMatrix rows are the local axes expressed in the parent: columns of rot.
    axes = [[rot[r][c] for r in range(3)] for c in range(3)]
    for axis in axes:
        length = sum(v * v for v in axis) ** 0.5
        axis[:] = [v / length for v in axis]
    matrix = unreal.Matrix(unreal.Plane(*axes[0], 0.0), unreal.Plane(*axes[1], 0.0), unreal.Plane(*axes[2], 0.0),
                           unreal.Plane(*loc, 1.0))
    return matrix.transform()


# In-place use of root motion clips.
for path in library.list_assets(f"{ANIMS}/Injured", recursive=False, include_folder=False):
    sequence = library.load_asset(path)
    if isinstance(sequence, unreal.AnimSequence) and not sequence.get_editor_property("force_root_lock"):
        sequence.set_editor_property("force_root_lock", True)
        library.save_loaded_asset(sequence)

# Animation Blueprint.
abp_path = f"{ROOT}/ABP_Operative"
if library.does_asset_exist(abp_path):
    abp = library.load_asset(abp_path)
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("target_skeleton", skeleton)
    factory.set_editor_property("parent_class", unreal.OperativeAnimInstance)
    abp = tools.create_asset("ABP_Operative", ROOT, unreal.AnimBlueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
abp_class = unreal.load_object(None, f"{abp_path}.ABP_Operative_C")
abp_cdo = unreal.get_default_object(abp_class)
anim_set = abp_cdo.get_editor_property("animations")
for field, path in CLIPS.items():
    if anim_set.get_editor_property(field) is None:
        anim_set.set_editor_property(field, library.load_asset(path))
abp_cdo.set_editor_property("animations", anim_set)
library.save_loaded_asset(abp)
unreal.log(f"SETUP ABP_Operative clips: { {f: str(anim_set.get_editor_property(f).get_name()) for f in CLIPS} }")

# Operative Blueprint.
bp = library.load_asset(f"{ROOT}/BP_Operative")
bp_cdo = unreal.get_default_object(unreal.load_object(None, f"{ROOT}/BP_Operative.BP_Operative_C"))
body = bp_cdo.get_editor_property("mesh")
if body.get_editor_property("skeletal_mesh_asset") is None:
    body.set_editor_property("skeletal_mesh_asset", mesh)
body.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
if body.get_editor_property("anim_class") is None:
    body.set_editor_property("anim_class", abp_class)
weapon = bp_cdo.get_editor_property("weapon_mesh")
if weapon.get_editor_property("static_mesh") is None:
    weapon.set_editor_property("static_mesh", rifle)
if weapon.get_editor_property("relative_location").is_nearly_zero(0.01):  # offset not tuned yet
    offset = rifle_in_hand()
    # Component templates on a CDO keep only property writes (SetRelativeTransform is not persisted).
    weapon.set_editor_property("relative_location", offset.translation)
    weapon.set_editor_property("relative_rotation", offset.rotation.rotator())
    print(f"SETUP weapon offset loc={offset.translation} rot={offset.rotation.rotator()}")
library.save_loaded_asset(bp)
unreal.log(f"SETUP BP_Operative mesh={body.get_editor_property('skeletal_mesh_asset').get_name()} "
           f"anim={body.get_editor_property('anim_class').get_name()} weapon={weapon.get_editor_property('static_mesh').get_name()}")
