"""Test ladders on campaign level 0.1 /Game/Maps/L_Demo01_B_FrozenFactory (user request 2026-10-22: "on the new map add a test object with
a ladder against a wall and a ladder without a wall"; docs/design/LADDERS.md).

Near the squad's start (PlayerStart (-1500, -7250), facing +Y; the start camera looks from the south-west at yaw 45), in its view:
  1. a solid block 2.5 x 4 x 4.004 m (11 rungs of 36.4 cm; a walkable roof) east of the start, with an ALadderActor on its west face
     (rung plane 10 cm off the wall, three wall brackets);
  2. a FREE-STANDING ladder west of the start: an ALadderActor standing on its own rails up to a 2.5 x 2.5 m deck on four posts
     (8 rungs = 2.912 m; no wall behind the rungs - the deck edge is 5 cm behind the rung plane, nothing below it);
  3. a navmesh bounds volume over both (the dynamic navmesh builds the roof / deck and the ladders' smart links at load).
Everything placed is tagged FrozenFactory_Ladders (idempotent: deleted and placed again on every run). No copied actor is touched. The saved
RecastNavMesh actors are destroyed before the save (docs/port/TRAPS.md).

Run (this project's editor closed, the worktree's build lock):
  UnrealEditor-Cmd.exe <worktree>/CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Report: Saved/Logs/FrozenFactoryLadders.txt
"""
import datetime
import os

import unreal

TARGET_MAP = "/Game/Maps/L_Demo01_B_FrozenFactory"
TAG = "FrozenFactory_Ladders"
FOLDER = "FrozenFactory_Ladders"
CUBE = "/Engine/BasicShapes/Cube.Cube"
GRID = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
PITCH = 36.4

# 1. The wall block (X -350..-100, Y -7300..-6900) and its ladder on the west face.
WALL_RUNGS = 11
WALL_BOX = (-350.0, -100.0, -7300.0, -6900.0)
WALL_GAP = 10.0
# 2. The free-standing ladder's deck (X -2475..-2225, Y -7175..-6925) on four posts.
DECK_RUNGS = 8
DECK_BOX = (-2475.0, -2225.0, -7175.0, -6925.0)
DECK_THICK = 20.0
DECK_GAP = 5.0
POST = 20.0
# 3. Navmesh bounds over the test ground (both roofs included).
NAV_BOX = (-2800.0, 200.0, -7650.0, -6550.0, -100.0, 700.0)

SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = []


def report(line):
    REPORT.append(str(line))


def flush():
    os.makedirs(SAVED + "Logs", exist_ok=True)
    with open(SAVED + "Logs/FrozenFactoryLadders.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


class Placer:
    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.cube = unreal.load_asset(CUBE)
        self.grid = unreal.load_asset(GRID)
        self.count = 0

    def spawn(self, cls, label, location, yaw, section):
        actor = self.actors.spawn_actor_from_class(cls, unreal.Vector(*location), unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0))
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER + "/" + section)
        actor.set_editor_property("tags", [TAG])
        self.count += 1
        report("placed %-30s %-18s at (%.1f, %.1f, %.1f) yaw %.0f" % (label, cls.static_class().get_name(), location[0], location[1], location[2], yaw))
        return actor

    def box(self, label, x0, x1, y0, y1, z0, z1, section):
        actor = self.spawn(unreal.StaticMeshActor, label, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5), 0.0, section)
        comp = actor.static_mesh_component
        comp.set_static_mesh(self.cube)
        comp.set_material(0, self.grid)
        actor.set_actor_scale3d(unreal.Vector((x1 - x0) / 100.0, (y1 - y0) / 100.0, (z1 - z0) / 100.0))
        return actor

    def ladder(self, label, location, yaw, rungs, brackets, wall_search, section):
        actor = self.spawn(unreal.LadderActor, label, location, yaw, section)
        actor.set_editor_property("height", rungs * PITCH)
        actor.set_editor_property("wall_bracket_count", brackets)
        actor.set_editor_property("wall_search_cm", wall_search)
        # set_editor_property re-runs the construction (PostEditChangeProperty): the ladder lays itself out.
        report("  %s: %d rungs, climbable %s, wall %s, validation '%s'" % (label, actor.get_rung_count(), actor.is_climbable(), actor.has_wall(),
                                                                         actor.get_validation_text() if hasattr(actor, "get_validation_text") else "-"))
        return actor


def build():
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level_editor.load_level(TARGET_MAP):
        report("ERROR: could not load %s" % TARGET_MAP)
        return
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    old = [a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    for a in old:
        actors.destroy_actor(a)
    report("removed %d actors of the previous run (tag %s)" % (len(old), TAG))

    p = Placer()
    # 1. Ladder against a wall.
    x0, x1, y0, y1 = WALL_BOX
    top = WALL_RUNGS * PITCH
    p.box("FF_LadderTest_WallBlock", x0, x1, y0, y1, 0.0, top, "WallLadder")
    p.ladder("FF_LadderTest_WallLadder", (x0 - WALL_GAP, (y0 + y1) * 0.5, 0.0), 180.0, WALL_RUNGS, 3, WALL_GAP + 2.0, "WallLadder")

    # 2. Free-standing ladder up to a deck on posts.
    x0, x1, y0, y1 = DECK_BOX
    top = DECK_RUNGS * PITCH
    p.box("FF_LadderTest_Deck", x0, x1, y0, y1, top - DECK_THICK, top, "FreeLadder")
    for i, (px, py) in enumerate(((x0, y0), (x0, y1 - POST), (x1 - POST, y0), (x1 - POST, y1 - POST))):
        p.box("FF_LadderTest_DeckPost%d" % i, px, px + POST, py, py + POST, 0.0, top - DECK_THICK, "FreeLadder")
    p.ladder("FF_LadderTest_FreeLadder", (x1 + DECK_GAP, (y0 + y1) * 0.5, 0.0), 0.0, DECK_RUNGS, 0, 25.0, "FreeLadder")

    # 3. Navmesh bounds over the test ground.
    nx0, nx1, ny0, ny1, nz0, nz1 = NAV_BOX
    nav = p.spawn(unreal.NavMeshBoundsVolume, "FF_LadderTest_NavMeshBounds", ((nx0 + nx1) * 0.5, (ny0 + ny1) * 0.5, (nz0 + nz1) * 0.5), 0.0, "Navigation")
    nav.set_actor_scale3d(unreal.Vector((nx1 - nx0) / 200.0, (ny1 - ny0) / 200.0, (nz1 - nz0) / 200.0))

    navmeshes = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in navmeshes:
        actors.destroy_actor(a)
    report("destroyed %d RecastNavMesh actors (rebuilt at load, RuntimeGeneration=Dynamic)" % len(navmeshes))
    saved = level_editor.save_current_level()
    report("placed %d actors, saved=%s" % (p.count, saved))


def main():
    report("place_factory_test_ladders.py %s (%s)" % (TARGET_MAP, datetime.datetime.now().isoformat()))
    try:
        build()
    except Exception as exc:  # the report must be written whatever happens
        report("ERROR: %r" % exc)
    flush()


main()
