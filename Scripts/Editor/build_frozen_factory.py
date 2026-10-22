"""Campaign level 0.1 "Frozen Factory" (game designer 2026-10-22: "make the next level now, using the very first map we used - Movement
Test ... when the next level starts, behind the players you can see the last point of the previous level - the small fenced compound").

Creates /Game/Maps/L_Demo01_B_FrozenFactory as a COPY of /Game/Maps/L_MovementTest (the user's standard test map is only read, never
saved) and adds, behind the squad's start, a recreation of the Demo 01 lake's last compound (layout 5k of build_demo01_lake_layout.py):
the east wall with the fully OPEN blast door (AGateActor, bSetDressing: open from the start, inert, never the level's gate), the
compound walls, the generator shed, the base building and the burnt BTR-82 hull (static meshes), a floor slab under it (the copy's floor
ends there) and a navmesh bounds volume over it. The copy's PlayerStart moves to just OUTSIDE the open door, facing away from it into the
factory. Everything placed is tagged FrozenFactory_Compound (idempotent: deleted and placed again on every run); the only edit of a copied
actor is the PlayerStart move. The saved RecastNavMesh actors are destroyed before the save (docs/port/TRAPS.md: a commandlet-saved
navmesh stays empty at runtime; RuntimeGeneration=Dynamic builds it at load) - re-save the copy once in the editor for a baked navmesh.

The lake pieces are the lake map's actors as dumped on 2026-10-22 (Saved/LayoutDumps; lake ground z -0.5), turned +90 deg about the lake
door's centre so the lake's "out" (+X, the road east) becomes the factory's +Y (into the map) and the door sits on the south edge of the
copy's Floor2 (Y -7850).

Modes (env CODEX_FACTORY_MODE): build (default) | dump. CODEX_FACTORY_FRESH=1 deletes the copy first (a fresh duplicate of
L_MovementTest). Report: Saved/Logs/FrozenFactoryLayout.txt, dumps Saved/LayoutDumps/L_Demo01_B_FrozenFactory_<when>_<stamp>.json.
Run (this project's editor closed):
  UnrealEditor-Cmd.exe <worktree>/CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
"""
import datetime
import json
import os

import unreal

SOURCE_MAP = "/Game/Maps/L_MovementTest"
TARGET_MAP = os.environ.get("CODEX_FACTORY_TARGET", "/Game/Maps/L_Demo01_B_FrozenFactory")
TAG = "FrozenFactory_Compound"
FOLDER = "FrozenFactory_Compound"

# The lake door's centre and ground (Gate_Compound_BlastDoor) and where it goes on the copy: the south edge of Floor2, X -1500
# (the compound spans X -2310..1690, inside the copy's side walls at X -3800 / 2200).
LAKE_GATE = (32320.1, -1300.0)
LAKE_GROUND_Z = -0.5
FACTORY_GATE = (-1500.0, -7850.0)
FACTORY_GROUND_Z = 0.0
# The squad's start: 6 m out of the doorway (the followers 2.8 m behind the leader stay > 3 m clear of it), facing away (+Y).
START_OUT_CM = 600.0
START_Z = 100.0

CUBE = "/Engine/BasicShapes/Cube.Cube"
GRID = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
APC_MESH = "/Game/VigilanteContent/Vehicles/East_APC_BTR82/Damaged/SM_East_APC_BTR82_Damaged"

# (lake label, lake location, lake yaw, scale) - cubes with the grid material, as on the lake (dump 2026-10-22).
LAKE_CUBES = [
    ("Compound_Wall_W1", (28320.1, -3602.6, 124.5), 90.0, (17.8, 0.4, 2.5)),
    ("Compound_Wall_W2", (28320.1, -1377.6, 124.5), 90.0, (17.8, 0.4, 2.5)),
    ("Compound_Wall_E1", (32320.1, -3540.1, 124.5), 90.0, (19.0, 0.4, 2.5)),
    ("Compound_Wall_E2", (32320.1, -2020.1, 124.5), 90.0, (7.4, 0.4, 2.5)),
    ("Compound_Wall_E2b", (32320.1, -720.1, 124.5), 0.0, (0.4, 4.6, 2.5)),
    ("Compound_Wall_N", (30320.1, -490.1, 124.5), 0.0, (40.0, 0.4, 2.5)),
    ("Compound_Wall_S", (30320.1, -4490.1, 124.5), 0.0, (40.0, 0.4, 2.5)),
    ("Base_Wall_W1", (30920.1, -2980.1, 159.5), 90.0, (8.2, 0.4, 3.2)),
    ("Base_Wall_W2", (30920.1, -2000.1, 159.5), 90.0, (8.2, 0.4, 3.2)),
    ("Base_Wall_E", (31920.1, -2490.1, 159.5), 90.0, (18.0, 0.4, 3.2)),
    ("Base_Wall_N", (31420.1, -1590.1, 159.5), 0.0, (10.0, 0.4, 3.2)),
    ("Base_Wall_S", (31420.1, -3390.1, 159.5), 0.0, (10.0, 0.4, 3.2)),
    ("Base_Roof", (31420.1, -2490.1, 329.5), 0.0, (10.4, 18.4, 0.2)),
    ("GeneratorShed_Wall_0", (30180.1, -940.1, 119.5), 0.0, (1.2, 0.3, 2.4)),
    ("GeneratorShed_Wall_1", (30460.1, -940.1, 119.5), 0.0, (1.2, 0.3, 2.4)),
    ("GeneratorShed_Wall_2", (30520.1, -790.1, 119.5), 90.0, (3.0, 0.3, 2.4)),
    ("GeneratorShed_Wall_3", (30320.1, -640.1, 119.5), 180.0, (4.0, 0.3, 2.4)),
    ("GeneratorShed_Wall_4", (30120.1, -790.1, 119.5), -90.0, (3.0, 0.3, 2.4)),
]
# The burnt BTR-82 (Quest_APC's art mesh: box centre (31558.1, -3940.1, 161.5) + the mesh pivot offset (-63.4, -13.3, -158)).
LAKE_APC_MESH_AT = (31558.1 - 63.4, -3940.1 - 13.3, 161.5 - 158.0)
# The floor slab under the compound (the copy's Floor2 ends at Y -7850) and the navmesh bounds over it: X -2510..1890, Y -12050..-7850.
FLOOR_BOX = (-2510.0, 1890.0, -12050.0, -7850.0)

SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = []


def report(line):
    REPORT.append(str(line))


def flush_report():
    os.makedirs(SAVED + "Logs", exist_ok=True)
    with open(SAVED + "Logs/FrozenFactoryLayout.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def vec(v):
    return [round(v.x, 1), round(v.y, 1), round(v.z, 1)]


def to_factory(lx, ly, lz):
    """Lake -> factory: +90 deg about the lake door (lake +X = factory +Y), the door on FACTORY_GATE."""
    dx, dy = lx - LAKE_GATE[0], ly - LAKE_GATE[1]
    return FACTORY_GATE[0] - dy, FACTORY_GATE[1] + dx, lz - LAKE_GROUND_Z + FACTORY_GROUND_Z


def describe(actor):
    r = actor.get_actor_rotation()
    d = {"label": actor.get_actor_label(), "name": actor.get_name(), "class": actor.get_class().get_path_name(),
         "location": vec(actor.get_actor_location()), "rotation": [round(r.pitch, 2), round(r.yaw, 2), round(r.roll, 2)],
         "scale": vec(actor.get_actor_scale3d()), "tags": [str(t) for t in actor.tags]}
    if isinstance(actor, unreal.StaticMeshActor):
        mesh = actor.static_mesh_component.static_mesh
        d["mesh"] = mesh.get_path_name() if mesh else None
    return d


def dump(when, stamp):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    os.makedirs(SAVED + "LayoutDumps", exist_ok=True)
    path = SAVED + "LayoutDumps/%s_%s_%s.json" % (TARGET_MAP.split("/")[-1], when, stamp)
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"map": TARGET_MAP, "actors": [describe(a) for a in actors]}, f, indent=1, ensure_ascii=False)
    report("dump %s -> %s (%d actors)" % (when, path, len(actors)))


class Placer:
    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.cube = unreal.load_asset(CUBE)
        self.grid = unreal.load_asset(GRID)
        self.placed = []

    def spawn(self, cls, label, location, yaw, section):
        actor = self.actors.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0))
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER + "/" + section)
        actor.set_editor_property("tags", [TAG])
        self.placed.append(actor)
        report("placed %-34s %-20s at (%.0f, %.0f, %.0f) yaw %.0f" % (label, cls.static_class().get_name(), location[0], location[1],
                                                                       location[2], yaw))
        return actor

    def cube_actor(self, label, location, yaw, scale, section, mesh=None, material=None):
        actor = self.spawn(unreal.StaticMeshActor, label, location, yaw, section)
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh or self.cube)
        if material is not False:
            comp.set_material(0, material or self.grid)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor


def build():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    lib = unreal.EditorAssetLibrary
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if os.environ.get("CODEX_FACTORY_FRESH") == "1" and lib.does_asset_exist(TARGET_MAP):
        report("fresh: deleting %s -> %s" % (TARGET_MAP, lib.delete_asset(TARGET_MAP)))
    if not lib.does_asset_exist(TARGET_MAP):
        # The source is only read: the duplicate is a new package, L_MovementTest is never saved.
        if not lib.duplicate_asset(SOURCE_MAP, TARGET_MAP) or not lib.save_asset(TARGET_MAP, only_if_is_dirty=False):
            report("ERROR: could not duplicate %s -> %s" % (SOURCE_MAP, TARGET_MAP))
            return
        report("duplicated %s -> %s" % (SOURCE_MAP, TARGET_MAP))
    if not level_editor.load_level(TARGET_MAP):
        report("ERROR: could not load %s" % TARGET_MAP)
        return
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    dump("before", stamp)

    old = [a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    for a in old:
        actors.destroy_actor(a)
    report("removed %d actors of the previous run (tag %s)" % (len(old), TAG))

    p = Placer()
    for label, (lx, ly, lz), yaw, scale in LAKE_CUBES:
        p.cube_actor("FF_" + label, to_factory(lx, ly, lz), yaw + 90.0, scale, "Compound")

    gx, gy, gz = to_factory(LAKE_GATE[0], LAKE_GATE[1], LAKE_GROUND_Z)
    gate = p.spawn(unreal.GateActor, "FF_Compound_BlastDoor_Open", (gx, gy, gz), 90.0, "Compound")
    gate.set_editor_property("set_dressing", True)  # open from the start, inert, never the level's gate (C++ AGateActor::bSetDressing)

    apc_mesh = unreal.load_asset(APC_MESH)
    if apc_mesh:
        p.cube_actor("FF_Compound_BTR82_Burnt", to_factory(*LAKE_APC_MESH_AT), 90.0, (1.0, 1.0, 1.0), "Compound", mesh=apc_mesh,
                     material=False)
    else:
        report("REVIEW: %s not found - no BTR hull" % APC_MESH)

    x0, x1, y0, y1 = FLOOR_BOX
    p.cube_actor("FF_Compound_Floor", ((x0 + x1) * 0.5, (y0 + y1) * 0.5, FACTORY_GROUND_Z - 10.0), 0.0,
                 ((x1 - x0) / 100.0, (y1 - y0) / 100.0, 0.2), "Compound")
    nav = p.spawn(unreal.NavMeshBoundsVolume, "FF_Compound_NavMeshBounds", ((x0 + x1) * 0.5, (y0 + y1) * 0.5, 200.0), 0.0, "Navigation")
    nav.set_actor_scale3d(unreal.Vector((x1 - x0) / 200.0, (y1 - y0) / 200.0, 5.0))

    # The squad starts just outside the open door, facing away from it (the copy's PlayerStart; the only moved copied actor).
    starts = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    for ps in starts:
        before = (vec(ps.get_actor_location()), round(ps.get_actor_rotation().yaw, 1))
        ps.set_actor_location_and_rotation(unreal.Vector(gx, gy + START_OUT_CM, START_Z), unreal.Rotator(pitch=0.0, yaw=90.0, roll=0.0),
                                           False, True)
        report("moved %s: %s yaw %s -> %s yaw 90" % (ps.get_actor_label(), before[0], before[1], vec(ps.get_actor_location())))
    if len(starts) != 1:
        report("REVIEW: %d PlayerStart actors in the copy" % len(starts))

    # Saved navmesh actors stay empty at runtime when saved from a commandlet (docs/port/TRAPS.md): drop them, built at load.
    navmeshes = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in navmeshes:
        actors.destroy_actor(a)
    report("destroyed %d RecastNavMesh actors (rebuilt at load, RuntimeGeneration=Dynamic; re-save the copy in the editor)" % len(navmeshes))

    saved = level_editor.save_current_level()
    report("placed %d actors, saved=%s" % (len(p.placed), saved))
    dump("after", stamp)


def main():
    mode = os.environ.get("CODEX_FACTORY_MODE", "build")
    report("build_frozen_factory.py mode=%s target=%s (%s)" % (mode, TARGET_MAP, datetime.datetime.now().isoformat()))
    try:
        if mode == "dump":
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(TARGET_MAP)
            dump("dump", datetime.datetime.now().strftime("%Y%m%d_%H%M%S"))
        else:
            build()
    except Exception as exc:  # the report must be written whatever happens
        report("ERROR: %r" % exc)
    flush_report()


main()
