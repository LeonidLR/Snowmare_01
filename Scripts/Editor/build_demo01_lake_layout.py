"""Demo 01 «Frozen Lake» auto layout: places every beacon / volume / marker / object of the rev 5 beat sheet
(docs/demo/DEMO01_BEAT_SHEET.md §4 / §10.1) on a COPY of the user's map, relative to the user's own anchors.

Idempotent: actors tagged AutoLayout_Demo01 are deleted and placed again on every run; every other actor is left alone,
except (a) the §10.1 re-ids (VOL_A1_BEACON4 -> VOL_A3_WIND, VOL_A1_RIDGE(_FAR) -> VOL_A3_RIDGE(_FAR), VOL_A6_FIELD ->
VOL_A3_FIELD) on the user's own volumes when they exist, and (b) the saved RecastNavMesh actors, which are destroyed before
the save (HANDOFF §2: a commandlet-saved navmesh stays empty at runtime; RuntimeGeneration=Dynamic rebuilds it at load).
A volume / marker id the user already placed is kept where it is and not placed again.

Anchors read from the map (fallbacks in brackets): PlayerStart; VOL_A0_BEACON1 [PlayerStart + 25 m]; the crevasse = the gap
between the two largest floor slabs plus the plank that spans it [a graybox crack + plank is built half-way to the hut];
the fishermen's hut = the Shelter ACameraZoneVolume holding a loot crate [a point 40 m past the bridge]. Everything from
beacon 3 on (camp, approach, fence, field, overlook, patrols, compound) is laid out east of the hut along +X.
No enemies are placed (beat sheet §1.3: placed enemies turn the level into an ambush level); patrol routes + TargetPoints only.
Rev 5c (designer review 2026-10-13): the TACTICAL HUT ENTRY - the hut's walls and door are read from its geometry (axis-aligned
walls >= 1.8 m round the Shelter zone; the door = the widest opening of the side facing the approach from the bridge);
VOL_A1_HUT_APPROACH (~10 m in front of the door), VOL_A1_DOOR_L / VOL_A1_DOOR_R (the outer wall left / right of the door as
seen by the arriving squad), VOL_A1_HUT = the interior only (inset 40 cm, Any Operative, Fire Once off), markers
A1_hut_stack_l / _r / _enter; a door wider than 2.6 m with no wall to hug on one side gets a graybox jamb there (review).

Modes (env CODEX_LAYOUT_MODE):
  build (default)  editor commandlet: duplicate the source map to the target if the target does not exist yet
                   (CODEX_LAYOUT_FRESH=1 deletes the target first), place the layout, save, dump before / after.
  dump             editor commandlet: dump the target (or CODEX_LAYOUT_TARGET) to Saved/LayoutDumps.
  additive         editor commandlet (rev 5d, the user edits the copy by hand): load the EXISTING target, delete only the
                   AutoLayout_Demo01_5d actors, apply EDITS_5D, place the rev 5d section again; saved only when something
                   changed (no fresh copy, no reset of anything else). Report: Saved/Logs/Demo01Layout5d.txt.
  verify           in game (-game -nullrhi): volume ids vs demo_01_steps.json, nav paths along the critical path, the bridge
                   as the only crossing, the fence (standing path refused, prone path through), actors on the ground.
                   Result: Saved/Logs/Demo01LayoutVerify.txt ("RESULT: PASS / FAIL").
  shots            in game with rendering (-RenderOffscreen): top-down screenshots + Saved/Screenshots/demo01_autolayout_labels.json.
  (system python)  `python build_demo01_lake_layout.py annotate` draws the labels -> Saved/Screenshots/demo01_autolayout_*.png.
Env CODEX_LAYOUT_SOURCE / CODEX_LAYOUT_TARGET override the map paths.

Run (editor of THIS project closed is safest; another project's editor may stay open):
  set CODEX_LAYOUT_MODE=build
  UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
  The game modes run through the console command `py <file>`, which cannot take a path with spaces from -ExecCmds and
  -ExecutePythonScript is editor-only: copy this file to a path without spaces first (e.g. %TEMP%\demo01_layout.py).
  set CODEX_LAYOUT_MODE=verify
  UnrealEditor-Cmd.exe CodexTactics.uproject /Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout -game -nullrhi -unattended
      -nosound -ExecCmds="py %TEMP%\demo01_layout.py"
  set CODEX_LAYOUT_MODE=shots
  UnrealEditor.exe CodexTactics.uproject /Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout -game -RenderOffscreen -ResX=1920
      -ResY=1080 -nosound -unattended -ExecCmds="py %TEMP%\demo01_layout.py"
  python <this file> annotate
The whole sequence for PowerShell is in docs/demo/DEMO01_AUTOLAYOUT.md.
"""
import datetime
import json
import math
import os
import sys

try:
    import unreal
except ImportError:  # system python: the annotate step only
    unreal = None

SOURCE_MAP = os.environ.get("CODEX_LAYOUT_SOURCE", "/Game/Maps/L_Demo01_A_FrozenLake")
TARGET_MAP = os.environ.get("CODEX_LAYOUT_TARGET", "/Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout")
TAG = "AutoLayout_Demo01"
FOLDER = "AutoLayout_Demo01"
# Rev 5d (2026-10-14): the ADDITIVE section. The review copy is the user's now (he edits it by hand): mode "additive" never
# resets / moves / deletes any existing actor (incl. the AutoLayout_Demo01 ones he may have repositioned); it deletes and
# re-places only actors tagged TAG_5D, and touches pre-existing actors only through the explicit, minimal edits of EDITS_5D.
TAG_5D = "AutoLayout_Demo01_5d"
FOLDER_5D = "AutoLayout_Demo01_5d"
# The ONLY changes the additive mode makes to pre-existing actors (idempotent, each logged old -> new; DEMO01_AUTOLAYOUT.md section 8).
EDITS_5D = [
    ("hut_crate_heat_packs", "the hut's ALootCrateActor (inside the hut's Shelter camera zone): Contents.HeatPacks = 3 (nothing else)"),
]

# §10.1 re-ids of the user's A1 volumes (they belong to the A3 approach now).
RENAMES = {
    "VOL_A1_BEACON4": "VOL_A3_WIND",
    "VOL_A1_RIDGE": "VOL_A3_RIDGE",
    "VOL_A1_RIDGE_FAR": "VOL_A3_RIDGE_FAR",
    "VOL_A6_FIELD": "VOL_A3_FIELD",
}

if unreal:
    SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    CONTENT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
else:
    _ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    SAVED = os.path.join(_ROOT, "Saved") + os.sep
    CONTENT = os.path.join(_ROOT, "Content") + os.sep

REPORT = []


def report(line):
    REPORT.append(str(line))


def flush_report(name):
    os.makedirs(SAVED + "Logs", exist_ok=True)
    with open(SAVED + "Logs/" + name, "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def vec(v):
    return [round(v.x, 1), round(v.y, 1), round(v.z, 1)]


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def name_str(value):
    s = str(value) if value is not None else "None"
    return "" if s == "None" else s


def effective_volume_id(volume):
    """ATutorialTriggerVolume::GetEffectiveVolumeId: VolumeId, else StepId."""
    return name_str(prop(volume, "volume_id")) or name_str(prop(volume, "step_id"))


def is_class(actor, class_name):
    return actor.get_class().get_name().startswith(class_name)


# ----------------------------------------------------------------------------------------------------------------------
# Dump (editor)
# ----------------------------------------------------------------------------------------------------------------------

def describe(actor):
    d = {
        "label": actor.get_actor_label(),
        "name": actor.get_name(),
        "class": actor.get_class().get_path_name(),
        "location": vec(actor.get_actor_location()),
        "rotation": [round(actor.get_actor_rotation().pitch, 2), round(actor.get_actor_rotation().yaw, 2),
                     round(actor.get_actor_rotation().roll, 2)],
        "scale": vec(actor.get_actor_scale3d()),
        "tags": [str(t) for t in actor.tags],
        "folder": str(actor.get_folder_path()),
    }
    try:
        origin, extent = actor.get_actor_bounds(False)
        d["bounds_origin"], d["bounds_extent"] = vec(origin), vec(extent)
    except Exception:
        pass
    if isinstance(actor, unreal.StaticMeshActor):
        mesh = actor.static_mesh_component.static_mesh
        d["mesh"] = mesh.get_path_name() if mesh else None
    if is_class(actor, "TutorialTriggerVolume"):
        for k in ("step_id", "beat_id", "volume_id", "required_beat", "activated_by", "fire_once"):
            d[k] = str(prop(actor, k))
        box = prop(actor, "box")
        if box:
            d["box_extent"] = vec(box.get_scaled_box_extent())
    if is_class(actor, "TutorialMarkerActor"):
        for k in ("step_id", "beat_id", "ring_radius_cm"):
            d[k] = str(prop(actor, k))
    if is_class(actor, "CameraZoneVolume"):
        for k in ("environment", "enable_camera_switch", "zone_name"):
            d[k] = str(prop(actor, k))
        box = prop(actor, "box")
        if box:
            d["box_extent"] = vec(box.get_scaled_box_extent())
    if is_class(actor, "LootCrateActor"):
        d["trapped"] = str(prop(actor, "trapped"))
        contents = prop(actor, "contents")
        if contents:
            d["heat_packs"] = str(prop(contents, "heat_packs"))
    if is_class(actor, "BridgeCrossingVolume"):
        d["span_extent"] = vec(prop(actor, "span").get_scaled_box_extent())
        d["capacity"] = prop(actor, "capacity")
    if is_class(actor, "CrawlGapActor"):
        for k in ("gap_width_cm", "clearance_cm", "bar_top_cm", "depth_cm"):
            d[k] = prop(actor, k)
    if is_class(actor, "PatrolRouteActor"):
        spline = prop(actor, "route_spline")
        d["waypoints"] = [vec(spline.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD))
                          for i in range(spline.get_number_of_spline_points())]
    return d


def dump(map_path, out_name):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    data = {"map": map_path, "time": datetime.datetime.now().isoformat(), "actors": [describe(a) for a in actors]}
    os.makedirs(SAVED + "LayoutDumps", exist_ok=True)
    path = SAVED + "LayoutDumps/" + out_name
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1, ensure_ascii=False)
    report("dump %s -> %s (%d actors)" % (map_path, path, len(actors)))
    return data


# ----------------------------------------------------------------------------------------------------------------------
# Placement helpers (editor)
# ----------------------------------------------------------------------------------------------------------------------

class Placer:
    def __init__(self, tag=None, folder=None):
        self.tag = tag or TAG          # the tag this run places (and only ever deletes)
        self.folder = folder or FOLDER
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        self.placed = []
        self.table = []  # rows for the doc: label, class, id, position, anchor
        self.warnings = []
        self.cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
        self.cylinder = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
        self.grid = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial")
        self.basic = unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")

    # -- ground --------------------------------------------------------------------------------------------------------
    def collect_ground(self):
        """Walkable surfaces of the user's map. The commandlet's editor world has no physics scene (line traces return
        nothing), so the ground is read from the colliding static mesh slabs' bounds (tops up to 1.5 m: floors, planks,
        not roofs / walls) and from landscapes (GetHeightAtLocation); the in-game verify re-checks it with real traces."""
        self.surfaces = []
        self.landscapes = []
        for a in self.actors.get_all_level_actors():
            if self.tag in [str(t) for t in a.tags]:
                continue
            if isinstance(a, unreal.LandscapeProxy):
                self.landscapes.append(a)
            if not isinstance(a, unreal.StaticMeshActor):
                continue
            comp = a.static_mesh_component
            if comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                continue
            o, e = a.get_actor_bounds(False)
            if e.z > 50000 or o.z + e.z > 150:
                continue
            self.surfaces.append((o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y, o.z + e.z))

    def ground(self, x, y, what=""):
        """Top of the walkable ground at (x, y) (user slabs / landscape, never this script's actors); 0 with a warning."""
        best = None
        for x0, x1, y0, y1, top in self.surfaces:
            if x0 <= x <= x1 and y0 <= y <= y1 and (best is None or top > best):
                best = top
        for land in self.landscapes:
            try:
                h = land.get_height_at_location(unreal.Vector(x, y, 0))
                h = h[1] if isinstance(h, tuple) else h
                if h is not None and (best is None or h > best):
                    best = h
            except Exception:
                pass
        if best is not None:
            return best
        self.warnings.append("no ground under %s at (%.0f, %.0f): z = 0 used - moved, review" % (what, x, y))
        return 0.0

    # -- spawning ------------------------------------------------------------------------------------------------------
    def spawn(self, cls, label, location, yaw=0.0, section="Misc", anchor="", ident="", pitch=0.0):
        actor = self.actors.spawn_actor_from_class(cls, location, unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.0))
        actor.set_actor_label(label)
        actor.set_folder_path(self.folder + "/" + section)
        actor.set_editor_property("tags", [self.tag])
        self.placed.append(actor)
        self.table.append({"label": label, "class": cls.static_class().get_name(), "id": ident,
                           "x": round(location.x), "y": round(location.y), "z": round(location.z),
                           "anchor": anchor, "section": section})
        return actor

    def block(self, label, section, x, y, size, yaw=0.0, base_z=None, material=None, collision=True, nav=True,
              tags=(), anchor="", mesh=None, pitch=0.0, hidden=False):
        """Box of size (sx, sy, sz) cm standing on the ground (or on base_z); the engine cube is 100 cm, pivot centre."""
        z0 = self.ground(x, y, label) if base_z is None else base_z
        actor = self.spawn(unreal.StaticMeshActor, label, unreal.Vector(x, y, z0 + size[2] * 0.5), yaw, section, anchor,
                           pitch=pitch)
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh or self.cube)
        comp.set_material(0, material or self.grid)
        actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
        if not collision:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_editor_property("can_ever_affect_navigation", False)
        elif not nav:
            comp.set_editor_property("can_ever_affect_navigation", False)
        if hidden:
            actor.set_actor_hidden_in_game(True)
        if tags:
            actor.set_editor_property("tags", [self.tag] + list(tags))
        return actor

    def wall(self, label, section, a, b, height, thickness=40.0, base_z=None, anchor="", tags=()):
        """Straight wall from 2D point a to b."""
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        yaw = math.degrees(math.atan2(dy, dx))
        return self.block(label, section, (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (length, thickness, height), yaw,
                          base_z, anchor=anchor, tags=tags)

    def pole(self, label, x, y, anchor):
        """Beacon pole (art graybox): 4 m, no collision, no navmesh effect."""
        return self.block(label, "Beacons", x, y, (30, 30, 400), mesh=self.cylinder, material=self.basic,
                          collision=False, anchor=anchor)

    def volume(self, volume_id, x, y, extent, beat="", any_operative=False, required_beat="", anchor="", base_z=None):
        z0 = self.ground(x, y, volume_id) if base_z is None else base_z
        actor = self.spawn(unreal.TutorialTriggerVolume, volume_id, unreal.Vector(x, y, z0 + extent[2]), 0.0,
                           "TutorialVolumes", anchor, volume_id)
        actor.set_editor_property("volume_id", volume_id)
        if beat:
            actor.set_editor_property("beat_id", beat)
        if required_beat:
            actor.set_editor_property("required_beat", required_beat)
        actor.set_editor_property("activated_by", unreal.TutorialVolumeFilter.ANY_OPERATIVE if any_operative
                                  else unreal.TutorialVolumeFilter.LEADER)
        actor.get_editor_property("box").set_box_extent(unreal.Vector(*extent))
        self.table[-1]["id"] = volume_id + (" (Beat %s)" % beat if beat else "") + (" Any" if any_operative else " Leader")
        return actor

    def marker(self, step_id, x, y, radius=150.0, anchor="", beacon=True):
        z0 = self.ground(x, y, step_id)
        actor = self.spawn(unreal.TutorialMarkerActor, "Marker_" + step_id, unreal.Vector(x, y, z0 + 2.0), 0.0,
                           "TutorialMarkers", anchor, step_id)
        actor.set_editor_property("step_id", step_id)
        actor.set_editor_property("ring_radius_cm", radius)
        actor.set_editor_property("show_beacon", beacon)
        return actor

    def zone(self, label, x, y, extent, environment, camera=None, zone_name="", anchor="", base_z=None):
        z0 = self.ground(x, y, label) if base_z is None else base_z
        actor = self.spawn(unreal.CameraZoneVolume, label, unreal.Vector(x, y, z0 + extent[2]), 0.0, "ColdZones", anchor,
                           str(environment))
        actor.set_editor_property("environment", environment)
        actor.set_editor_property("enable_camera_switch", camera is not None)
        if camera is not None:
            actor.set_editor_property("target_camera", camera)
        if zone_name:
            actor.set_editor_property("zone_name", zone_name)
        actor.get_editor_property("box").set_box_extent(unreal.Vector(*extent))
        return actor

    def thing(self, cls, label, x, y, z_offset, yaw=0.0, section="Objects", anchor="", ident=""):
        z0 = self.ground(x, y, label)
        return self.spawn(cls, label, unreal.Vector(x, y, z0 + z_offset), yaw, section, anchor, ident)

    def target_point(self, label, x, y, yaw=0.0, anchor=""):
        return self.thing(unreal.TargetPoint, label, x, y, 0.0, yaw, "Patrols", anchor)


# ----------------------------------------------------------------------------------------------------------------------
# Anchors (editor)
# ----------------------------------------------------------------------------------------------------------------------

def find_anchors(all_actors):
    user = [a for a in all_actors if TAG not in [str(t) for t in a.tags]]
    anchors = {}
    ps = next((a for a in user if isinstance(a, unreal.PlayerStart)), None)
    anchors["player_start"] = ps.get_actor_location() if ps else unreal.Vector(0, 0, 0)
    vols = {effective_volume_id(a): a for a in user if is_class(a, "TutorialTriggerVolume")}
    anchors["user_volumes"] = vols
    anchors["user_markers"] = {name_str(prop(a, "step_id")): a for a in user if is_class(a, "TutorialMarkerActor")}
    b1 = vols.get("VOL_A0_BEACON1")
    anchors["beacon1"] = b1.get_actor_location() if b1 else anchors["player_start"] + unreal.Vector(2500, 0, 0)
    anchors["beacon1_source"] = "user VOL_A0_BEACON1" if b1 else "PlayerStart + 25 m (fallback)"

    # Floors: the largest static mesh slabs; the crevasse = the gap between the two largest along X.
    slabs = []
    for a in user:
        if isinstance(a, unreal.StaticMeshActor):
            o, e = a.get_actor_bounds(False)
            if e.x > 3000 and e.y > 3000 and e.z < 1000:
                slabs.append((e.x * e.y, a, o, e))
    slabs.sort(key=lambda s: -s[0])
    anchors["floors"] = [(s[2].x - s[3].x, s[2].x + s[3].x, s[2].y - s[3].y, s[2].y + s[3].y, s[2].z + s[3].z)
                         for s in slabs]
    crack = None
    if len(slabs) >= 2:
        f1, f2 = sorted(slabs[:2], key=lambda s: s[2].x)
        west_end = f1[2].x + f1[3].x
        east_start = f2[2].x - f2[3].x
        if 0 < east_start - west_end < 1000:
            plank = None
            for a in user:
                if a in (f1[1], f2[1]) or not isinstance(a, unreal.StaticMeshActor):
                    continue
                o, e = a.get_actor_bounds(False)
                if o.x - e.x <= west_end + 50 and o.x + e.x >= east_start - 50 and e.y < 500:
                    plank = (a, o, e)
            crack = {"x0": west_end, "x1": east_start, "y0": max(f1[2].y - f1[3].y, f2[2].y - f2[3].y),
                     "y1": min(f1[2].y + f1[3].y, f2[2].y + f2[3].y), "top": f1[2].z + f1[3].z, "plank": plank}
    anchors["crack"] = crack

    # The fishermen's hut: a Shelter camera zone with a loot crate inside.
    hut = None
    crates = [a for a in user if is_class(a, "LootCrateActor")]
    for z in [a for a in user if is_class(a, "CameraZoneVolume")]:
        o, e = z.get_actor_bounds(False)
        for c in crates:
            l = c.get_actor_location()
            if abs(l.x - o.x) <= e.x and abs(l.y - o.y) <= e.y:
                hut = {"zone": z, "crate": c, "centre": o, "extent": e}
    anchors["hut"] = hut
    return anchors


# ----------------------------------------------------------------------------------------------------------------------
# Build (editor)
# ----------------------------------------------------------------------------------------------------------------------

def build():
    lib = unreal.EditorAssetLibrary
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if os.environ.get("CODEX_LAYOUT_FRESH") == "1" and lib.does_asset_exist(TARGET_MAP):
        lib.delete_asset(TARGET_MAP)
        report("deleted old target " + TARGET_MAP)
    if not lib.does_asset_exist(TARGET_MAP):
        if not lib.duplicate_asset(SOURCE_MAP, TARGET_MAP) or not lib.save_asset(TARGET_MAP, only_if_is_dirty=False):
            report("ERROR: could not duplicate %s -> %s" % (SOURCE_MAP, TARGET_MAP))
            return
        report("duplicated %s -> %s" % (SOURCE_MAP, TARGET_MAP))
    if not level_editor.load_level(TARGET_MAP):
        report("ERROR: could not load " + TARGET_MAP)
        return
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    dump(TARGET_MAP, "%s_before_%s.json" % (TARGET_MAP.split("/")[-1], stamp))

    p = Placer()
    removed = 0
    for a in p.actors.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            p.actors.destroy_actor(a)
            removed += 1
    report("removed %d old %s actors" % (removed, TAG))
    p.collect_ground()
    report("ground: %d user slabs, %d landscapes" % (len(p.surfaces), len(p.landscapes)))

    all_actors = p.actors.get_all_level_actors()
    # §10.1 re-ids on the user's own volumes.
    for a in [a for a in all_actors if is_class(a, "TutorialTriggerVolume")]:
        old = effective_volume_id(a)
        if old in RENAMES:
            if name_str(prop(a, "step_id")) == old:
                a.set_editor_property("step_id", "None")
            a.set_editor_property("volume_id", RENAMES[old])
            report("RENAMED user volume %s (%s) -> %s (kept in place: review its position)" % (a.get_actor_label(), old,
                                                                                               RENAMES[old]))
    anchors = find_anchors(p.actors.get_all_level_actors())
    user_vols = anchors["user_volumes"]
    user_markers = anchors["user_markers"]
    ps = anchors["player_start"]
    b1 = anchors["beacon1"]
    report("anchor PlayerStart (%.0f, %.0f); beacon 1 (%.0f, %.0f) from %s" % (ps.x, ps.y, b1.x, b1.y, anchors["beacon1_source"]))

    def want_volume(vid):
        if vid in user_vols:
            l = user_vols[vid].get_actor_location()
            report("kept user volume %s at (%.0f, %.0f, %.0f)" % (vid, l.x, l.y, l.z))
            return False
        return True

    def want_marker(step):
        if step in user_markers:
            l = user_markers[step].get_actor_location()
            report("kept user marker %s at (%.0f, %.0f)" % (step, l.x, l.y))
            return False
        return True

    # ---- A1: the crevasse and the shaky bridge --------------------------------------------------------------------
    crack = anchors["crack"]
    hut = anchors["hut"]
    hut_c = hut["centre"] if hut else None
    if crack and crack["plank"]:
        plank, po, pe = crack["plank"]
        bridge_x0, bridge_x1, bridge_y = po.x - pe.x, po.x + pe.x, po.y
        crack_x = (crack["x0"] + crack["x1"]) * 0.5
        bridge_anchor = "user crevasse (floor gap X %.0f..%.0f) + plank %s" % (crack["x0"], crack["x1"], plank.get_actor_label())
        report("anchor crevasse: floor gap X %.0f..%.0f across Y %.0f..%.0f; plank %s X %.0f..%.0f at Y %.0f (%.0f cm wide)"
               % (crack["x0"], crack["x1"], crack["y0"], crack["y1"], plank.get_actor_label(), bridge_x0, bridge_x1,
                  bridge_y, 2 * pe.y))
    else:
        # Fallback: a graybox crack half-way between beacon 1 and the hut (or 60 m past beacon 1), across every floor.
        crack_x = (b1.x + hut_c.x) * 0.5 if hut_c else b1.x + 6000
        bridge_y = b1.y
        bridge_x0, bridge_x1 = crack_x - 250, crack_x + 250
        bridge_anchor = "graybox crack (no floor gap found) - moved, review"
        ys = [f[2] for f in anchors["floors"]] + [f[3] for f in anchors["floors"]]
        y_min, y_max = (min(ys), max(ys)) if ys else (-11250, 11250)
        for i, (a, b) in enumerate(((y_min, bridge_y - 130), (bridge_y + 130, y_max))):
            p.block("Crack_Fissure_%d" % i, "Crevasse", crack_x, (a + b) * 0.5, (200, b - a, 4), material=p.basic,
                    collision=False, anchor=bridge_anchor)
            p.block("Crack_Blocker_%d" % i, "Crevasse", crack_x, (a + b) * 0.5, (200, b - a, 300), hidden=True,
                    anchor=bridge_anchor)
        p.block("Bridge_Planks", "Crevasse", crack_x, bridge_y, (500, 200, 10), anchor=bridge_anchor)
        p.warnings.append("no floor gap / plank found: built a graybox crack at X %.0f - moved, review" % crack_x)

    if want_volume("VOL_A1_BEACON2"):
        p.volume("VOL_A1_BEACON2", bridge_x0 - 450, bridge_y, (250, 500, 150), anchor="4.5 m before the bridge (west end)")
    # Rev 5c: the bridge teaches nothing; its marker shows the crossing objective (was Step Id A1_solo).
    if want_marker("A1_bridge"):
        p.marker("A1_bridge", bridge_x0 - 180, bridge_y, 120, anchor="near end of the bridge (crossing objective)")
    span_half = (bridge_x1 - bridge_x0) * 0.5 + 100.0
    bridge_cx = (bridge_x0 + bridge_x1) * 0.5
    gz = p.ground(bridge_x1 - 50, bridge_y, "bridge")
    bridge = p.spawn(unreal.BridgeCrossingVolume, "BridgeCrossing_A1", unreal.Vector(bridge_cx, bridge_y, gz + 125), 0.0,
                     "Crevasse", bridge_anchor + "; span = planks + 1 m each end", "Capacity 1")
    bridge.get_editor_property("span").set_box_extent(unreal.Vector(span_half, 100, 125))
    bridge.set_editor_property("capacity", 1)
    p.zone("WindZone_A1_FarBank", bridge_x1 + 100 + 600, bridge_y, (600, 600, 250), unreal.CameraZoneEnvironment.BLIZZARD,
           zone_name="Crevasse wind", anchor="far side of the bridge, 12 x 12 m, x2.5")

    # Beacon poles: beacon 1 -> beacon 2 every ~25 m.
    poles = 0
    d = math.hypot(bridge_x0 - 450 - b1.x, bridge_y - b1.y)
    for i in range(int(d // 2500) + 1):
        t = min(1.0, i * 2500.0 / d) if d > 0 else 0
        x, y = b1.x + (bridge_x0 - 450 - b1.x) * t, b1.y + (bridge_y - b1.y) * t
        poles += 1
        p.pole("BeaconPole_%02d" % poles, x, y + 300, "beacon road beacon 1 -> bridge")
    poles += 1
    p.pole("BeaconPole_%02d_Bridge" % poles, bridge_x0 - 450, bridge_y + 350, "beacon 2 at the bridge")

    # ---- A1: the hut ----------------------------------------------------------------------------------------------
    if hut:
        hc, he = hut["centre"], hut["extent"]
        hut_anchor = "user hut (zone %s)" % hut["zone"].get_actor_label()
        report("anchor hut: zone %s centre (%.0f, %.0f) extent (%.0f, %.0f); crate %s"
               % (hut["zone"].get_actor_label(), hc.x, hc.y, he.x, he.y, hut["crate"].get_actor_label()))
        hut_x, hut_y = hc.x, hc.y
        frame = find_hut_frame(p, hc, he)
        if frame is None:
            p.warnings.append("hut %s: walls / interior not recognised (needs 4 axis-aligned walls >= 1.8 m round the zone) - "
                              "VOL_A1_HUT over the zone, no door entry placed - review" % hut["zone"].get_actor_label())
            if want_volume("VOL_A1_HUT"):
                vol = p.volume("VOL_A1_HUT", hc.x, hc.y, (max(100.0, he.x - 90), max(100.0, he.y - 80), 150), any_operative=True,
                               anchor=hut_anchor + ", over the floor")
                vol.set_editor_property("fire_once", False)
        else:
            build_hut_entry(p, frame, (bridge_x1, bridge_y), hut_anchor, want_volume, want_marker)
        if want_marker("A1_loot_open"):
            cl = hut["crate"].get_actor_location()
            p.marker("A1_loot_open", cl.x, cl.y, 90, anchor="on the user's hut crate", beacon=False)
        heat = prop(prop(hut["crate"], "contents"), "heat_packs")
        if heat != 3:
            p.warnings.append("user's hut crate %s has Heat Packs %s (beat sheet: 3) - not changed (user actor), review"
                              % (hut["crate"].get_actor_label(), heat))
    else:
        hut_x, hut_y = bridge_x1 + 4000, bridge_y
        hut_anchor = "graybox hut (no user hut found) - moved, review"
        p.warnings.append("no user hut found: graybox hut at (%.0f, %.0f) - moved, review" % (hut_x, hut_y))
        for i, (a, b) in enumerate((((-300, -200), (300, -200)), ((300, -200), (300, 200)), ((-300, 200), (-300, -200)),
                                    ((-300, 200), (-100, 200)), ((100, 200), (300, 200)))):
            p.wall("Hut_Wall_%d" % i, "Hut", (hut_x + a[0], hut_y + a[1]), (hut_x + b[0], hut_y + b[1]), 240,
                   anchor=hut_anchor)
        p.zone("Hut_Interior_Shelter", hut_x, hut_y, (300, 200, 200), unreal.CameraZoneEnvironment.SHELTER, anchor=hut_anchor)
        crate = p.thing(unreal.LootCrateActor, "Hut_Crate_HeatPacks", hut_x + 150, hut_y - 100, 40, 0, "Hut", hut_anchor)
        set_contents(crate, {"heat_packs": 3})
        p.volume("VOL_A1_HUT", hut_x, hut_y, (260, 160, 150), any_operative=True, anchor=hut_anchor)
        p.marker("A1_loot_open", hut_x + 150, hut_y - 100, 90, anchor=hut_anchor, beacon=False)
        if want_marker("A1_obj_hut"):
            p.marker("A1_obj_hut", hut_x, hut_y + 450, 150, anchor=hut_anchor)

    # Poles bridge -> hut.
    d = math.hypot(hut_x - bridge_x1, hut_y - bridge_y)
    for i in range(1, int(d // 2500) + 1):
        t = i * 2500.0 / d
        poles += 1
        p.pole("BeaconPole_%02d" % poles, bridge_x1 + (hut_x - bridge_x1) * t + 300, bridge_y + (hut_y - bridge_y) * t,
               "beacon road bridge -> hut")

    # ---- everything east of the hut: offsets from the hut centre (O) ----------------------------------------------
    ox, oy = hut_x, hut_y

    def P(dx, dy):
        return ox + dx, oy + dy

    # Main lane of the approach / complex runs along +X at O.y + 4600.
    lane = 4600.0

    # A1 end: beacon 3 (formation lines).
    x, y = P(4700, 1800)
    if want_volume("VOL_A1_BEACON3"):
        p.volume("VOL_A1_BEACON3", x, y, (300, 600, 150), anchor="hut + (47, 18) m, on the way to the camp")
    poles += 1
    p.pole("BeaconPole_%02d_Beacon3" % poles, x, y + 700, "beacon 3")

    # ---- A2: the camp -----------------------------------------------------------------------------------------------
    camp_anchor = "hut + (102, 26) m (graybox camp hut 1)"
    cx, cy = P(10200, 2600)
    # Camp hut 1: 6 x 4 m, walls 2.4 m, 2 m door in the west wall (towards the arriving squad), lee on the east.
    for i, (a, b) in enumerate((((-300, -200), (300, -200)), ((300, -200), (300, 200)), ((300, 200), (-300, 200)),
                                ((-300, 200), (-300, 100)), ((-300, -100), (-300, -200)))):
        p.wall("Camp_Hut1_Wall_%d" % i, "Camp", (cx + a[0], cy + a[1]), (cx + b[0], cy + b[1]), 240, anchor=camp_anchor)
    p.block("Camp_Hut1_Roof", "Camp", cx, cy, (640, 440, 20), base_z=p.ground(cx, cy, "roof") + 240, anchor=camp_anchor)
    p.zone("Camp_Hut1_Lee_Shelter", cx + 300 + 260, cy, (250, 300, 200), unreal.CameraZoneEnvironment.SHELTER,
           zone_name="Hut lee", anchor="east (lee) side of camp hut 1")
    p.thing(unreal.BarrelActor, "Camp_Barrel_1", cx - 1000, cy - 600, 70, 0, "Camp", "10 m west of camp hut 1, in the wind")
    p.thing(unreal.BarrelActor, "Camp_Barrel_2_Spare", cx + 600, cy + 900, 70, 0, "Camp", "spare, NE of camp hut 1")
    for i, (dx, dy, l) in enumerate(((-200, 500, 400), (200, -500, 300))):
        p.block("Camp_FishRack_%d" % i, "Camp", cx + dx, cy + dy + (300 if dy > 0 else -300), (l, 40, 100),
                anchor="camp low cover 1.0 m")
    x, y = P(8000, 2400)
    if want_volume("VOL_A2_CAMP"):
        p.volume("VOL_A2_CAMP", x, y, (400, 600, 150), beat="A2", anchor="22 m before camp hut 1")
    poles += 1
    p.pole("BeaconPole_%02d_Camp" % poles, x, y - 700, "camp")

    # ---- A3: the approach ---------------------------------------------------------------------------------------------
    def L(dx, dy=0.0):  # along the lane
        return ox + dx, oy + lane + dy

    x, y = L(13200)
    if want_volume("VOL_A3_APPROACH"):
        p.volume("VOL_A3_APPROACH", x, y, (300, 800, 150), beat="A3", anchor="hut + 132 m: the complex comes into view")
    # Wind strip x2.5 + VOL_A3_WIND.
    x, y = L(15000)
    p.zone("WindStrip_A3", x, y, (400, 1500, 250), unreal.CameraZoneEnvironment.BLIZZARD, zone_name="Open ice wind",
           anchor="approach, 8 x 30 m strip, x2.5")
    if want_volume("VOL_A3_WIND"):
        p.volume("VOL_A3_WIND", x, y, (400, 1500, 150), any_operative=True, anchor="same box as the wind strip")
    # Vault ridge 0.8 m x 8 m, tagged Vault; volumes before / after.
    x, y = L(17000)
    p.block("VaultRidge_A3", "Approach", x, y, (100, 800, 80), material=p.basic, tags=("Vault",),
            anchor="approach, 20 m past the wind strip")
    if want_volume("VOL_A3_RIDGE"):
        p.volume("VOL_A3_RIDGE", x - 450, y, (200, 350, 150), anchor="4.5 m before the ridge")
    if want_volume("VOL_A3_RIDGE_FAR"):
        p.volume("VOL_A3_RIDGE_FAR", x + 450, y, (200, 350, 150), anchor="4.5 m after the ridge")

    # The low fence: closed across every floor, one crawl gap (ACrawlGapActor) on the lane.
    fx, fy = L(19200)
    gap = 600.0
    ys = [f[2] for f in anchors["floors"]] + [f[3] for f in anchors["floors"]]
    y_min, y_max = (min(ys), max(ys)) if ys else (fy - 15000, fy + 15000)
    fence_anchor = "approach, 22 m past the ridge; closed from floor edge to floor edge"
    p.block("LowFence_North", "Approach", fx, (fy + gap * 0.5 + y_max) * 0.5, (20, y_max - fy - gap * 0.5, 120),
            anchor=fence_anchor)
    p.block("LowFence_South", "Approach", fx, (y_min + fy - gap * 0.5) * 0.5, (20, fy - gap * 0.5 - y_min, 120),
            anchor=fence_anchor)
    gz = p.ground(fx, fy, "fence")
    p.block("LowFence_GapRail", "Approach", fx, fy, (10, gap, 30), base_z=gz + 90, material=p.basic, collision=False,
            anchor="crawl gap top rail 0.9-1.2 m (look only)")
    crawl = p.spawn(unreal.CrawlGapActor, "CrawlGap_A3", unreal.Vector(fx, fy, gz), 0.0, "Approach",
                    "on the fence line, local X across the fence", "Gap 600 / Clear 90 / Bar 200")
    crawl.set_editor_property("gap_width_cm", gap)
    crawl.set_editor_property("clearance_cm", 90.0)
    crawl.set_editor_property("bar_top_cm", 200.0)
    crawl.set_editor_property("depth_cm", 20.0)
    if want_volume("VOL_A3_FENCE"):
        p.volume("VOL_A3_FENCE", fx - 350, fy, (150, 500, 150), any_operative=True, anchor="3.5 m before the low fence")

    # Cover field (zigzag, low 1.0 / high 2.2 m, 5-6 m apart) + VOL_A3_FIELD at its entrance.
    x, y = L(20100)
    if want_volume("VOL_A3_FIELD"):
        p.volume("VOL_A3_FIELD", x, y, (300, 700, 150), anchor="cover field entrance, 9 m past the fence")
    for i, (dx, dy, length, h) in enumerate(((21000, 800, 400, 100), (21600, -800, 300, 220), (22200, 600, 400, 100),
                                             (22800, -600, 300, 220), (23400, 300, 400, 100))):
        x, y = L(dx, dy)
        p.block("Cover_A3_%d_%s" % (i + 1, "Low" if h < 130 else "High"), "CoverField", x, y, (40, length, h),
                anchor="cover field, zigzag")
    # Two hidden PMC mines on the obvious straight line through the field (the shortcut), 5 m apart.
    for i, dx in enumerate((21900, 22400)):
        x, y = L(dx, -50 + 100 * i)
        mine = p.thing(unreal.ProximityMineActor, "PMC_Mine_A3_%d" % (i + 1), x, y, 30, 0, "Traps",
                       "shortcut through the middle of the cover field", "hidden, damage 80")
        mine.set_editor_property("explosion_damage", 80.0)
        mine.set_editor_property("placed_by_squad", False)
    # The obvious gap at the field exit: two rock blocks 3 m apart, the PMC tripwire across it.
    x, y = L(24200)
    p.block("Rock_A3_GapNorth", "Traps", x, y + 150 + 300, (300, 600, 220), material=p.basic, anchor="field exit gap")
    p.block("Rock_A3_GapSouth", "Traps", x, y - 150 - 300, (300, 600, 220), material=p.basic, anchor="field exit gap")
    wire = p.thing(unreal.PMCTripwireActor, "PMC_Tripwire_A3", x, y, 0, 0, "Traps", "across the 3 m field exit gap",
                   "PMC, hidden")
    wire.set_editor_property("anchor_a", unreal.Vector(0, -140, 0))
    wire.set_editor_property("anchor_b", unreal.Vector(0, 140, 0))

    # ---- A4: the overlook + patrol routes ---------------------------------------------------------------------------
    x, y = L(25500)
    if want_volume("VOL_A4_OVERLOOK"):
        p.volume("VOL_A4_OVERLOOK", x, y, (300, 600, 150), beat="A4", anchor="hut + 255 m: the yard comes into view")
    p.block("Overlook_LowCover", "Overlook", x + 400, y, (40, 600, 100), anchor="overlook low cover 1.0 m")
    # Choke 3 m wide 20 m past the overlook (two wrecked containers), both loops cross it.
    chx, chy = L(27500)
    p.block("Container_A4_North", "Patrols", chx, chy + 150 + 300, (240, 600, 260), material=p.basic,
            anchor="patrol choke, 20 m from the overlook")
    p.block("Container_A4_South", "Patrols", chx, chy - 150 - 300, (240, 600, 260), material=p.basic,
            anchor="patrol choke, 20 m from the overlook")
    routes = {
        # Linear loops; every leg keeps >= 1.5 m off the containers (they span +-120 in X, 150..750 off the gap in Y).
        "PatrolRoute_A4_Frostbitten": [(-400, 0), (400, 0), (1200, 1000), (-100, 2200)],  # ~61 m loop
        "PatrolRoute_A4_Hounds": [(400, 0), (-400, 0), (-700, -800), (500, -1000)],        # ~39 m loop
    }
    for label, pts in routes.items():
        gz = p.ground(chx, chy, label)
        route = p.spawn(unreal.PatrolRouteActor, label, unreal.Vector(chx, chy, gz), 0.0, "Patrols",
                        "loop through the choke", "%d points" % len(pts))
        spline = route.get_editor_property("route_spline")
        spline.clear_spline_points(True)
        for (dx, dy) in pts:
            spline.add_spline_point(unreal.Vector(chx + dx, chy + dy, p.ground(chx + dx, chy + dy, label) + 5),
                                    unreal.SplineCoordinateSpace.WORLD, True)
        for i in range(len(pts)):
            spline.set_spline_point_type(i, unreal.SplinePointType.LINEAR, True)
        spline.set_closed_loop(True, True)
        route.set_editor_property("is_loop", True)
    for label, (dx, dy) in (("PatrolSpot_Frostbitten_1", (1200, 1000)), ("PatrolSpot_Frostbitten_2", (-100, 2200)),
                            ("PatrolSpot_Hound_1", (-700, -800)), ("PatrolSpot_Hound_2", (500, -1000))):
        p.target_point(label, chx + dx, chy + dy, anchor="where the patrol stands (place the enemy here in Phase 2)")

    # ---- A5 / A6: the compound (graybox) ------------------------------------------------------------------------------
    # 40 x 40 m perimeter (walls 2.5 m), gate 4.5 m west towards the overlook, back gap 2 m east.
    kx0, ky = L(30500)
    kx1 = kx0 + 4000
    y0, y1 = ky - 2000, ky + 2000
    comp_anchor = "graybox compound, hut + 305 m (no complex in the map)"
    for label, a, b in (("Compound_Wall_W1", (kx0, y0), (kx0, ky - 225)), ("Compound_Wall_W2", (kx0, ky + 225), (kx0, y1)),
                        ("Compound_Wall_E1", (kx1, y0), (kx1, ky - 100)), ("Compound_Wall_E2", (kx1, ky + 100), (kx1, y1)),
                        ("Compound_Wall_N", (kx0, y1), (kx1, y1)), ("Compound_Wall_S", (kx0, y0), (kx1, y0))):
        p.wall(label, "Compound", a, b, 250, anchor=comp_anchor)
    # Main building 10 x 18 m, 3.2 m, door 1.6 m in its west wall; roof; interior Shelter + fixed camera.
    bx0, bx1 = kx0 + 2600, kx0 + 3600
    by0, by1 = ky - 900, ky + 900
    for label, a, b in (("Base_Wall_W1", (bx0, by0), (bx0, ky - 80)), ("Base_Wall_W2", (bx0, ky + 80), (bx0, by1)),
                        ("Base_Wall_E", (bx1, by0), (bx1, by1)), ("Base_Wall_N", (bx0, by1), (bx1, by1)),
                        ("Base_Wall_S", (bx0, by0), (bx1, by0))):
        p.wall(label, "Compound", a, b, 320, anchor="main building 10 x 18 m")
    gz = p.ground((bx0 + bx1) * 0.5, ky, "base")
    p.block("Base_Roof", "Compound", (bx0 + bx1) * 0.5, ky, (1040, 1840, 20), base_z=gz + 320, anchor="main building roof")
    cam = p.spawn(unreal.CameraActor, "Base_Interior_Camera", unreal.Vector(bx0 + 120, by0 + 150, gz + 290),
                  65.0, "Compound", "inside the base, SW corner", pitch=-30.0)
    p.zone("Base_Interior_Shelter", (bx0 + bx1) * 0.5, ky, (460, 860, 200), unreal.CameraZoneEnvironment.SHELTER, cam,
           "PMC base", "main building interior (fixed camera)")
    if want_volume("VOL_A5_BASE"):
        p.volume("VOL_A5_BASE", bx0 - 250, ky, (200, 250, 150), beat="A5", anchor="in front of the base door")
    crate = p.thing(unreal.LootCrateActor, "Base_Crate_Clean", bx1 - 250, by0 + 200, 40, 0, "Compound", "supply corner")
    set_contents(crate, {"rifle_ammo": 30, "medkits": 1, "matches": 2})
    crate = p.thing(unreal.LootCrateActor, "Base_Crate_Trapped", bx1 - 250, by1 - 200, 40, 0, "Compound", "supply corner",
                    "trapped, trap damage 60")
    set_contents(crate, {"heat_packs": 1, "canned_food": 1})
    crate.set_editor_property("trapped", True)
    crate.set_editor_property("trap_damage", 60.0)
    p.thing(unreal.BarrelActor, "Base_Barrel_Outside", bx0 - 300, by1 - 100, 70, 0, "Compound", "unlit, outside the base")
    p.thing(unreal.BarrelActor, "Yard_Barrel_1", kx0 + 1200, y0 + 600, 70, 0, "Compound", "yard")
    p.thing(unreal.BarrelActor, "Yard_Barrel_2", kx0 + 1200, y1 - 600, 70, 0, "Compound", "yard")
    p.thing(unreal.BarricadeActor, "Yard_Barricade_1", kx0 + 700, ky - 700, 50, 90, "Compound", "near the gate")
    p.thing(unreal.BarricadeActor, "Yard_Barricade_2", kx0 + 700, ky + 700, 50, 90, "Compound", "near the gate")
    if want_volume("VOL_A6_PREP"):
        p.volume("VOL_A6_PREP", kx0 + 1400, ky, (500, 600, 150), beat="A6", required_beat="A5",
                 anchor="yard centre (fires from beat A5 on)")
    # High ground: 4 x 4 m platform 2 m high in the SW corner, a 1.2 m ramp (24 deg) from the yard.
    hx, hy = kx0 + 300, y0 + 300
    gz = p.ground(hx, hy, "platform")
    p.block("HighGround_Platform", "Compound", hx, hy, (400, 400, 200), base_z=gz, anchor="yard SW corner, 2 m high")
    run, rise = 450.0, 200.0
    p.block("HighGround_Ramp", "Compound", hx + 200 + run * 0.5, hy, (math.hypot(run, rise), 120, 20),
            base_z=gz + rise * 0.5 - 10, pitch=-math.degrees(math.atan2(rise, run)), anchor="ramp 1.2 m wide")
    if want_volume("VOL_A6_HIGH"):
        p.volume("VOL_A6_HIGH", hx, hy, (180, 180, 100), any_operative=True, anchor="on the platform top",
                 base_z=gz + 200)
    # Generator shed 4 x 3 m, 2.4 m, door 1.6 m towards the yard (A7 / A10 placeholder: no quest object placed).
    sx, sy = kx0 + 2000, y1 - 300
    for i, (a, b) in enumerate((((-200, -150), (-200 + 120, -150)), ((-200 + 280, -150), (200, -150)), ((200, -150), (200, 150)),
                                ((200, 150), (-200, 150)), ((-200, 150), (-200, -150)))):
        p.wall("GeneratorShed_Wall_%d" % i, "Compound", (sx + a[0], sy + a[1]), (sx + b[0], sy + b[1]), 240,
               thickness=30, anchor="generator shed graybox (A7 / A10)")

    # ---- navigation bounds over the new layout ------------------------------------------------------------------------
    xs = [r["x"] for r in p.table]
    ys = [r["y"] for r in p.table]
    nx0, nx1, ny0, ny1 = min(xs) - 2000, max(xs) + 2000, min(ys) - 2000, max(ys) + 2000
    nav = p.spawn(unreal.NavMeshBoundsVolume, "NavMeshBounds_AutoLayout", unreal.Vector((nx0 + nx1) * 0.5, (ny0 + ny1) * 0.5, 200),
                  0.0, "Navigation", "covers every placed actor + 20 m")
    nav.set_actor_scale3d(unreal.Vector((nx1 - nx0) / 200.0, (ny1 - ny0) / 200.0, 5.0))

    # Saved navmesh actors stay empty at runtime when saved from a commandlet (HANDOFF §2): drop them, built at load.
    navmeshes = [a for a in p.actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in navmeshes:
        p.actors.destroy_actor(a)
    report("destroyed %d RecastNavMesh actors (rebuilt at load, RuntimeGeneration=Dynamic)" % len(navmeshes))

    saved = level_editor.save_current_level()
    report("placed %d actors, saved=%s" % (len(p.placed), saved))
    counts = {}
    for r in p.table:
        counts[r["class"]] = counts.get(r["class"], 0) + 1
    report("counts: " + json.dumps(counts, sort_keys=True))
    for w in p.warnings:
        report("REVIEW: " + w)
    with open(SAVED + "LayoutDumps/demo01_autolayout_table.json", "w", encoding="utf-8") as f:
        json.dump({"table": p.table, "warnings": p.warnings, "counts": counts}, f, indent=1, ensure_ascii=False)
    dump(TARGET_MAP, "%s_after_%s.json" % (TARGET_MAP.split("/")[-1], stamp))


def find_hut_frame(p, centre, extent):
    """The hut's frame from its geometry (rev 5c): colliding static meshes >= 1.8 m tall and <= 80 cm thick round the hut's
    Shelter zone (bounds + 1.5 m); axis-aligned walls only. Returns {"walls", "west", "east", "south", "north", "x0", "x1",
    "y0", "y1"} (x0..x1 / y0..y1 = between the inner faces of the outer walls) or None."""
    walls = []
    for a in p.actors.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        if a.static_mesh_component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            continue
        o, e = a.get_actor_bounds(False)
        if e.z * 2 < 180 or abs(o.x - centre.x) > extent.x + 150 or abs(o.y - centre.y) > extent.y + 150:
            continue
        if min(e.x, e.y) > 40 or max(e.x, e.y) < 20:
            continue
        walls.append({"actor": a, "o": o, "e": e, "long": "x" if e.x >= e.y else "y"})
    ns = [w for w in walls if w["long"] == "y" and w["e"].y * 2 >= 300]
    ew = [w for w in walls if w["long"] == "x" and w["e"].x * 2 >= 300]
    if len(ns) < 2 or len(ew) < 2:
        return None
    west, east = min(ns, key=lambda w: w["o"].x), max(ns, key=lambda w: w["o"].x)
    south, north = min(ew, key=lambda w: w["o"].y), max(ew, key=lambda w: w["o"].y)
    f = {"walls": walls, "west": west, "east": east, "south": south, "north": north,
         "x0": west["o"].x + west["e"].x, "x1": east["o"].x - east["e"].x,
         "y0": south["o"].y + south["e"].y, "y1": north["o"].y - north["e"].y}
    return f if f["x1"] - f["x0"] > 200 and f["y1"] - f["y0"] > 200 else None


def hut_sides(f):
    """Per side: the outward normal, the tangent axis, the outer face, the side wall band and the covered intervals along it."""
    sides = {}
    for name, wall, normal in (("N", f["north"], (0.0, 1.0)), ("S", f["south"], (0.0, -1.0)),
                               ("E", f["east"], (1.0, 0.0)), ("W", f["west"], (-1.0, 0.0))):
        along = "x" if name in ("N", "S") else "y"
        across = "y" if along == "x" else "x"
        line = getattr(wall["o"], across)
        thick = getattr(wall["e"], across)
        face = line + (thick if normal[0] + normal[1] > 0 else -thick)
        span = (f["x0"], f["x1"]) if along == "x" else (f["y0"], f["y1"])
        covered = []
        for w in f["walls"]:
            o, e = w["o"], w["e"]
            lo, hi = getattr(o, across) - getattr(e, across), getattr(o, across) + getattr(e, across)
            if hi < line - thick + 5 or lo > line + thick - 5:
                continue  # not on this side's wall band
            covered.append((getattr(o, along) - getattr(e, along), getattr(o, along) + getattr(e, along)))
        covered.sort()
        runs = []
        for lo, hi in covered:
            if runs and lo <= runs[-1][1] + 2:
                runs[-1][1] = max(runs[-1][1], hi)
            else:
                runs.append([lo, hi])
        gaps = []
        cursor = span[0]
        for lo, hi in runs:
            if lo > cursor + 1 and lo > span[0]:
                gaps.append((cursor, min(lo, span[1])))
            cursor = max(cursor, hi)
        if cursor < span[1] - 1:
            gaps.append((cursor, span[1]))
        gaps = [g for g in gaps if g[1] - g[0] >= 80]
        sides[name] = {"wall": wall, "normal": normal, "along": along, "across": across, "line": line, "thick": thick,
                       "face": face, "runs": runs, "gap": max(gaps, key=lambda g: g[1] - g[0]) if gaps else None}
    return sides


def build_hut_entry(p, f, approach_from, hut_anchor, want_volume, want_marker):
    """Rev 5c tactical hut entry: the door (the widest opening of the side facing the squad's approach), the two stack volumes
    hugging the outer wall left / right of it, the interior volume, the approach volume, markers, a graybox jamb if a side of
    a wide door has no wall to hug."""
    cx, cy = (f["x0"] + f["x1"]) * 0.5, (f["y0"] + f["y1"]) * 0.5
    sides = hut_sides(f)
    dx, dy = cx - approach_from[0], cy - approach_from[1]
    dl = math.hypot(dx, dy) or 1.0
    facing = sorted(sides, key=lambda n: -(sides[n]["normal"][0] * -dx / dl + sides[n]["normal"][1] * -dy / dl))
    report("hut frame: interior X %.0f..%.0f, Y %.0f..%.0f; openings %s; sides by facing the approach: %s"
           % (f["x0"], f["x1"], f["y0"], f["y1"], {n: (sides[n]["gap"] and [round(v) for v in sides[n]["gap"]]) for n in sides},
              facing))
    door_side = next((n for n in facing if sides[n]["gap"]), None)
    if door_side is None:
        p.warnings.append("hut: no door opening >= 80 cm found in its walls - no door entry placed, review")
        return
    if door_side != facing[0]:
        p.warnings.append("hut: the side facing the approach (%s) has no opening; the door is taken on side %s - review"
                          % (facing[0], door_side))
    sd = sides[door_side]
    g0, g1 = sd["gap"]
    nx, ny = sd["normal"]
    along_x = sd["along"] == "x"

    def run_length(end, upward):
        """Length of the outer wall continuing from a door end (a wall to hug beside the door)."""
        for lo, hi in sd["runs"]:
            if upward and abs(lo - end) <= 3:
                return hi - lo, (lo, hi)
            if not upward and abs(hi - end) <= 3:
                return hi - lo, (lo, hi)
        return 0.0, None

    low_len, low_run = run_length(g0, False)
    high_len, high_run = run_length(g1, True)
    width = g1 - g0
    report("hut door on side %s: along %s %.0f..%.0f (%.0f cm wide); wall to hug beside it: low end %.0f cm, high end %.0f cm"
           % (door_side, sd["along"].upper(), g0, g1, width, low_len, high_len))

    def point(along, out):
        """World XY at `along` on the door side's tangent axis, `out` cm off the outer face (negative = inside)."""
        c = sd["face"] + out * (nx + ny)
        return (along, c) if along_x else (c, along)

    wall = sd["wall"]
    base_z = wall["o"].z - wall["e"].z
    height = wall["e"].z * 2
    door_width_target = 200.0
    for end in ("low", "high"):
        run_len = low_len if end == "low" else high_len
        if run_len >= 150:
            continue
        if width - door_width_target < 120:
            p.warnings.append("hut door side %s, %s end: only %.0f cm of wall to hug and the door is too narrow for a jamb - "
                              "that stack spot may give no cover, review" % (door_side, end, run_len))
            continue
        # A graybox jamb (rev 5c): narrows the wide opening to ~2 m and gives the stacker a wall beside the door.
        length = width - door_width_target if (low_len >= 150 or high_len >= 150) else max(150.0, (width - door_width_target) * 0.5)
        run = low_run if end == "low" else high_run
        if end == "low":
            a0 = run[0] if run else g0
            a1 = g0 + length
            g0 = a1
        else:
            a0 = g1 - length
            a1 = run[1] if run else g1
            g1 = a0
        mid = (a0 + a1) * 0.5
        jx, jy = (mid, sd["line"]) if along_x else (sd["line"], mid)
        size = (a1 - a0, sd["thick"] * 2, height) if along_x else (sd["thick"] * 2, a1 - a0, height)
        jamb = p.block("HutDoor_Jamb_%s" % ("L" if end == "low" else "R"), "HutEntry", jx, jy, size, base_z=base_z,
                       material=wall["actor"].static_mesh_component.get_material(0),
                       anchor="graybox door jamb (rev 5c) beside the hut door, %.0f cm" % (a1 - a0))
        p.warnings.append("hut door: graybox jamb %s added (%.0f cm, %.0f cm high) - the user's doorway was %.0f cm wide with "
                          "no wall to hug on that side; the door is now %.0f cm - review / replace with art"
                          % (jamb.get_actor_label(), a1 - a0, height, width, g1 - g0))
        if end == "low":
            low_len = a1 - (run[0] if run else a0)
        else:
            high_len = (run[1] if run else a1) - a0
    width = g1 - g0
    door_c = (g0 + g1) * 0.5
    # Left / right as seen by the squad arriving at the door (facing -normal): UE right = yaw + 90 deg.
    yaw = math.atan2(-ny, -nx)
    rx, ry = math.cos(yaw + math.pi * 0.5), math.sin(yaw + math.pi * 0.5)
    high_is_right = (rx if along_x else ry) > 0
    gz = p.ground(*point(door_c, 200.0), what="hut door")
    stack_len = 250.0
    for end in ("low", "high"):
        right = (end == "high") == high_is_right
        vid = "VOL_A1_DOOR_R" if right else "VOL_A1_DOOR_L"
        step = "A1_hut_stack_r" if right else "A1_hut_stack_l"
        if end == "low":
            a0, a1 = g0 - stack_len, g0 + 20
            spot = g0 - 90
        else:
            a0, a1 = g1 - 20, g1 + stack_len
            spot = g1 + 90
        mid = (a0 + a1) * 0.5
        vx, vy = point(mid, 60.0)
        extent = ((a1 - a0) * 0.5, 60.0, 150.0) if along_x else (60.0, (a1 - a0) * 0.5, 150.0)
        if want_volume(vid):
            p.volume(vid, vx, vy, extent, any_operative=True, base_z=gz,
                     anchor="outer wall %s of the hut door (as seen arriving), 2.5 m along it, 1.2 m out" % ("right" if right else "left"))
        if want_marker(step):
            mx, my = point(spot, 55.0)
            p.marker(step, mx, my, 70, anchor="stack spot %s of the door" % ("right" if right else "left"))
    # The interior only (the stackers outside never stand in it): inset 40 cm from the inner wall faces.
    if want_volume("VOL_A1_HUT"):
        vol = p.volume("VOL_A1_HUT", cx, cy, ((f["x1"] - f["x0"]) * 0.5 - 40, (f["y1"] - f["y0"]) * 0.5 - 40, 150), any_operative=True,
                       anchor=hut_anchor + ", the interior only (inset 40 cm), Fire Once off", base_z=gz)
        vol.set_editor_property("fire_once", False)
    else:
        p.warnings.append("VOL_A1_HUT is the user's: make it cover the INTERIOR only and set Fire Once off (rev 5c) - review")
    if want_marker("A1_hut_enter"):
        inner = sd["face"] - (nx + ny) * (sd["thick"] * 2 + 120)
        mx, my = (door_c, inner) if along_x else (inner, door_c)
        p.marker("A1_hut_enter", mx, my, 90, anchor="1.2 m inside the door")
    if want_volume("VOL_A1_HUT_APPROACH"):
        ax, ay = point(door_c, 1000.0)
        extent = (1200.0, 300.0, 150.0) if along_x else (300.0, 1200.0, 150.0)
        p.volume("VOL_A1_HUT_APPROACH", ax, ay, extent, anchor="10 m in front of the hut door, 24 m wide")
    report("hut entry: door side %s, door %.0f cm at %s %.0f; left = %s end" % (door_side, width, sd["along"].upper(), door_c,
                                                                                 "high" if not high_is_right else "low"))


def additive():
    """Rev 5d ADDITIVE section on the user's latest review copy (TAG_5D actors + EDITS_5D only)."""
    lib = unreal.EditorAssetLibrary
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not lib.does_asset_exist(TARGET_MAP):
        report("ERROR: %s does not exist (the additive mode never makes a copy)" % TARGET_MAP)
        return
    if not level_editor.load_level(TARGET_MAP):
        report("ERROR: could not load " + TARGET_MAP)
        return
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    before = dump(TARGET_MAP, "%s_5d_before_%s.json" % (TARGET_MAP.split("/")[-1], stamp))
    p = Placer(TAG_5D, FOLDER_5D)
    removed = 0
    for a in p.actors.get_all_level_actors():
        if TAG_5D in [str(t) for t in a.tags]:
            p.actors.destroy_actor(a)
            removed += 1
    report("removed %d old %s actors (nothing else is ever deleted)" % (removed, TAG_5D))
    p.collect_ground()
    changes = removed
    anchors = find_anchors(p.actors.get_all_level_actors())

    # ---- EDITS_5D: the minimal edits of pre-existing actors -------------------------------------------------------
    hut = anchors["hut"]
    if hut is None:
        p.warnings.append("edit hut_crate_heat_packs: no hut crate found (a LootCrateActor inside a Shelter camera zone) - not edited, review")
    else:
        crate = hut["crate"]
        contents = crate.get_editor_property("contents")
        old = contents.get_editor_property("heat_packs")
        if old != 3:
            contents.set_editor_property("heat_packs", 3)
            crate.set_editor_property("contents", contents)
            changes += 1
            report("EDIT hut_crate_heat_packs: %s (%s) Contents.HeatPacks %s -> 3" % (crate.get_actor_label(), crate.get_name(), old))
        else:
            report("EDIT hut_crate_heat_packs: %s (%s) already holds Heat Packs 3 - unchanged" % (crate.get_actor_label(), crate.get_name()))

    # ---- rev 5d sections (new actors, tag AutoLayout_Demo01_5d) ----------------------------------------------------
    for build_section in SECTIONS_5D:
        build_section(p, anchors)
    changes += len(p.placed)

    if changes == 0:
        report("nothing to change: the map is NOT saved")
    else:
        # Saved navmesh actors stay empty at runtime when saved from a commandlet (HANDOFF section 2): generated data, not user content.
        navmeshes = [a for a in p.actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
        for a in navmeshes:
            p.actors.destroy_actor(a)
        report("destroyed %d generated RecastNavMesh actors (rebuilt at load, RuntimeGeneration=Dynamic)" % len(navmeshes))
        report("saved=%s" % level_editor.save_current_level())
    report("placed %d %s actors" % (len(p.placed), TAG_5D))
    for w in p.warnings:
        report("REVIEW: " + w)
    os.makedirs(SAVED + "LayoutDumps", exist_ok=True)
    with open(SAVED + "LayoutDumps/demo01_autolayout_5d_table.json", "w", encoding="utf-8") as f:
        json.dump({"table": p.table, "warnings": p.warnings}, f, indent=1, ensure_ascii=False)
    after = dump(TARGET_MAP, "%s_5d_after_%s.json" % (TARGET_MAP.split("/")[-1], stamp))
    # Proof that nothing pre-existing moved: every actor of the "before" dump (but the 5d ones / the generated navmesh) is still
    # there with the same transform.
    now = {a["name"]: a for a in after["actors"]}
    moved = []
    for a in before["actors"]:
        if TAG_5D in a["tags"] or a["class"].endswith("RecastNavMesh"):
            continue
        b = now.get(a["name"])
        if b is None or b["location"] != a["location"] or b["rotation"] != a["rotation"] or b["scale"] != a["scale"]:
            moved.append(a["label"])
    report("pre-existing actors moved / deleted: %d %s" % (len(moved), moved))


def build_ruin_5d(p, anchors):
    """Rev 5d A2 barrel lesson: a half-ruined building near the camp (graybox), the barrel OUTSIDE it on a straight push path
    through the wide gap of its north wall, VOL_A2_RUIN over the interior, a Shelter camera zone (no camera switch) and the
    marker A2_push on the spot inside. Placed relative to VOL_A2_CAMP (the user's / auto camp volume, wherever he moved it):
    the ruin centre 15 m east and 20 m south of it (the camp hut stands east of the volume). Only TAG_5D actors."""
    # Any pre-existing VOL_A2_CAMP (the auto layout's one, possibly moved by the user, or his own).
    camp = next((a for a in p.actors.get_all_level_actors() if is_class(a, "TutorialTriggerVolume")
                 and effective_volume_id(a) == "VOL_A2_CAMP" and TAG_5D not in [str(t) for t in a.tags]), None)
    if camp is None:
        p.warnings.append("ruin: no VOL_A2_CAMP in the map - the ruin is not placed, review")
        return
    cl = camp.get_actor_location()
    cx, cy = cl.x + 1480, cl.y - 2010
    anchor = "VOL_A2_CAMP + (15, -20) m (south of the camp hut)"
    report("ruin: centre (%.0f, %.0f) from VOL_A2_CAMP (%.0f, %.0f)" % (cx, cy, cl.x, cl.y))
    hx, hy = 300.0, 250.0                    # half size: 6 x 5 m between the wall lines
    x0, x1, y0, y1 = cx - hx, cx + hx, cy - hy, cy + hy
    gap0, gap1 = cx - 180, cx + 180          # the wide gap (3.6 m) in the north wall, facing the camp
    # Broken walls (40 cm thick; high cover 2.6 m where whole, 1.6-2.2 m where broken, low rubble 0.6 m).
    walls = (("Ruin_Wall_S", (x0, y0), (x1, y0), 260),             # the back wall, whole
             ("Ruin_Wall_W", (x0, y0), (x0, cy + 50), 260),        # west: 3 m standing, its north end fallen
             ("Ruin_Wall_E_High", (x1, y0), (x1, cy - 50), 200),   # east: 2 m broken to 2 m
             ("Ruin_Wall_E_Rubble", (x1, cy - 50), (x1, cy + 150), 60),  # then rubble (low cover), then open
             ("Ruin_Wall_N_W", (x0, y1), (gap0, y1), 160),         # stubs either side of the wide gap
             ("Ruin_Wall_N_E", (gap1, y1), (x1, y1), 220))
    for label, a, b, h in walls:
        p.wall(label, "Ruin", a, b, h, anchor=anchor)
    gz = p.ground(cx, cy, "ruin")
    # A partial roof: only over the south-west third (the rest fell in); no navmesh on it.
    p.block("Ruin_Roof_Partial", "Ruin", x0 + 175, y0 + 140, (390, 320, 15), base_z=gz + 260, nav=False,
            anchor="half the roof is gone (graybox)")
    vol = p.volume("VOL_A2_RUIN", cx, cy, (hx - 60, hy - 60, 150), any_operative=True, anchor=anchor + ", the interior (inset 40 cm)")
    vol.set_editor_property("fire_once", False)
    p.zone("Ruin_Interior_Shelter", cx, cy, (hx - 20, hy - 20, 200), unreal.CameraZoneEnvironment.SHELTER, zone_name="Ruin",
           anchor="the ruin's interior: out of the wind (cold x0.5, no camera switch)")
    p.marker("A2_push", cx, cy - 40, 110, anchor="the spot inside the ruin the barrel goes to")
    # The barrel: 6.5 m north of the gap, on the straight line through it (clear of the camp hut / racks).
    p.thing(unreal.BarrelActor, "Ruin_Barrel", cx, y1 + 650, 70, 0, "Ruin", "outside the ruin, 6.5 m in front of the wide gap")


# Rev 5d section builders: f(placer, anchors), each places only TAG_5D actors.
SECTIONS_5D = [build_ruin_5d]


def set_contents(crate, values):
    """FLootContents: every count 0 except values (the beat sheet lists exact crate contents)."""
    contents = crate.get_editor_property("contents")
    for field in ("medkits", "canned_food", "bread", "chocolate", "matches", "rifle_ammo", "pistol_ammo", "shotgun_ammo",
                  "flame_fuel", "cryo_ammo", "plasma_ammo", "turrets", "barricades", "mines", "heat_packs", "firewood",
                  "flares"):
        try:
            contents.set_editor_property(field, values.get(field, 0))
        except Exception:
            pass
    crate.set_editor_property("contents", contents)


# ----------------------------------------------------------------------------------------------------------------------
# In-game verify / shots
# ----------------------------------------------------------------------------------------------------------------------

def game_world():
    name = TARGET_MAP.split("/")[-1]
    world = unreal.find_object(None, "%s.%s" % (TARGET_MAP, name))
    return world


def steps_volume_ids():
    with open(CONTENT + "Data/Tutorial/demo_01_steps.json", encoding="utf-8") as f:
        data = json.load(f)
    ids = set()
    for step in data.get("steps", []):
        for key in ("trigger", "complete"):
            for field in ("volume", "in_volume"):  # rev 5c: cover entered INSIDE a volume (the hut door)
                v = (step.get(key) or {}).get(field)
                if v:
                    ids.add(v)
    beats = [b["id"] for b in data.get("beats", [])]
    return sorted(ids), beats


class Game:
    def __init__(self, world):
        self.world = world
        self.ok = True
        self.lines = []
        self.ruin = None

    def check(self, ok, what):
        self.lines.append(("ok   " if ok else "FAIL ") + what)
        self.ok = self.ok and bool(ok)

    def all_of(self, cls):
        return list(unreal.GameplayStatics.get_all_actors_of_class(self.world, cls))

    def ground(self, x, y, ignore=(), top=5000.0):
        """Highest blocking surface below `top` (start the trace under roofs for interiors)."""
        hit = unreal.SystemLibrary.line_trace_single(self.world, unreal.Vector(x, y, top), unreal.Vector(x, y, -5000.0),
                                                     unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, list(ignore),
                                                     unreal.DrawDebugTrace.NONE, True)
        if hit is None:
            return None, None
        t = hit.to_tuple()
        if not t[0]:
            return None, None
        actor = next((v for v in t if isinstance(v, unreal.Actor)), None)
        return t[4].z, actor

    def path(self, a, b, filter_class=None):
        nav = unreal.NavigationSystemV1
        if filter_class is not None:
            path = nav.find_path_to_location_synchronously(self.world, a, b, None, filter_class)
        else:
            path = nav.find_path_to_location_synchronously(self.world, a, b)
        if path is None:
            return None, [], 0.0, True
        pts = list(path.get_editor_property("path_points"))
        valid = path.is_valid()
        partial = path.is_partial()
        length = path.get_path_length() if valid else 0.0
        if pts:
            end_gap = math.hypot(pts[-1].x - b.x, pts[-1].y - b.y)
            partial = partial or end_gap > 150
        return valid, pts, length, partial


def crossings(pts, x_line):
    """Y values where the polyline crosses the vertical line X = x_line."""
    ys = []
    for a, b in zip(pts, pts[1:]):
        if (a.x - x_line) * (b.x - x_line) < 0:
            t = (x_line - a.x) / (b.x - a.x)
            ys.append(a.y + (b.y - a.y) * t)
    return ys


def verify(game):
    w = game.world
    vols = [a for a in game.all_of(unreal.TutorialTriggerVolume)]
    by_id = {}
    for v in vols:
        by_id.setdefault(effective_volume_id(v), []).append(v)
    ids, beats = steps_volume_ids()
    for vid in ids:
        game.check(len(by_id.get(vid, [])) == 1, "volume %s exists exactly once (%d)" % (vid, len(by_id.get(vid, []))))
    beat_vols = {name_str(prop(v, "beat_id")): effective_volume_id(v) for v in vols if name_str(prop(v, "beat_id"))}
    for beat in beats[1:]:
        game.check(beat in beat_vols, "beat %s is started by a volume (%s)" % (beat, beat_vols.get(beat)))

    def centre(vid):
        v = by_id.get(vid, [None])[0]
        if v is None:
            return None
        l = v.get_actor_location()
        z, _ = game.ground(l.x, l.y, [v], l.z)
        return unreal.Vector(l.x, l.y, (z if z is not None else 0.0) + 60)

    ps = game.all_of(unreal.PlayerStart)
    start = ps[0].get_actor_location() if ps else None
    bridge = (game.all_of(unreal.BridgeCrossingVolume) or [None])[0]
    crawl = (game.all_of(unreal.CrawlGapActor) or [None])[0]
    game.check(bridge is not None, "bridge crossing volume present")
    game.check(crawl is not None, "crawl gap actor present")
    if bridge is None or crawl is None or start is None:
        return
    bl = bridge.get_actor_location()
    bext = bridge.get_editor_property("span").get_scaled_box_extent()
    b_w = unreal.Vector(bl.x - bext.x - 150, bl.y, bl.z - 60)
    b_e = unreal.Vector(bl.x + bext.x + 150, bl.y, bl.z - 60)
    fl = crawl.get_actor_location()
    f_w = unreal.Vector(fl.x - 300, fl.y, fl.z + 60)
    f_e = unreal.Vector(fl.x + 300, fl.y, fl.z + 60)

    chain = [("PlayerStart", unreal.Vector(start.x, start.y, start.z - 30)), ("VOL_A0_BEACON1", centre("VOL_A0_BEACON1")),
             ("VOL_A1_BEACON2", centre("VOL_A1_BEACON2")), ("bridge far end", b_e),
             ("VOL_A1_HUT_APPROACH", centre("VOL_A1_HUT_APPROACH")), ("VOL_A1_HUT", centre("VOL_A1_HUT")),
             ("VOL_A1_BEACON3", centre("VOL_A1_BEACON3")), ("VOL_A2_CAMP", centre("VOL_A2_CAMP")),
             ("VOL_A3_APPROACH", centre("VOL_A3_APPROACH")), ("VOL_A3_WIND", centre("VOL_A3_WIND")),
             ("VOL_A3_RIDGE", centre("VOL_A3_RIDGE")), ("VOL_A3_RIDGE_FAR", centre("VOL_A3_RIDGE_FAR")),
             ("VOL_A3_FENCE", centre("VOL_A3_FENCE"))]
    # Leg by leg (standing operatives use UNavFilter_NoCrawl, like AOperativeCharacter::RequestMove).
    no_crawl = unreal.NavFilter_NoCrawl.static_class()
    for (na, a), (nb, b) in zip(chain, chain[1:]):
        if a is None or b is None:
            game.check(False, "path %s -> %s: missing end point" % (na, nb))
            continue
        valid, pts, length, partial = game.path(a, b, no_crawl)
        game.check(valid and not partial, "path %s -> %s (standing): valid=%s partial=%s %.0f m" % (na, nb, valid, partial,
                                                                                                  length / 100))
    verify_hut_entry(game, by_id, centre, no_crawl)
    game.ruin = verify_ruin(game, by_id, centre, no_crawl)
    # The crack: the path from the west bank to the far end crosses on the bridge only. The crack line = where the ground
    # trace finds nothing (a floor gap) 4 m beside the planks; a graybox crack blocks the traces too, so fall back to the span.
    holes = []
    x = bl.x - bext.x
    while x <= bl.x + bext.x:
        z, hit_actor = game.ground(x, bl.y + 400)
        if z is None or (hit_actor is not None and "Crack_Blocker" in hit_actor.get_actor_label()):
            holes.append(x)
        x += 10
    x_line = sum(holes) / len(holes) if holes else bl.x
    game.lines.append("info crack line X %.0f (%d hole samples)" % (x_line, len(holes)))
    valid, pts, length, partial = game.path(centre("VOL_A1_BEACON2"), b_e, no_crawl)
    ys = crossings(pts, x_line)
    game.check(valid and not partial and ys and all(abs(y - bl.y) <= bext.y for y in ys),
               "bridge leg crosses the crack only on the span (crossings at Y %s, span Y %.0f +- %.0f)"
               % ([round(y) for y in ys], bl.y, bext.y))
    for side in (-1, 1):
        a = unreal.Vector(bl.x - bext.x - 800, bl.y + side * 6000, bl.z - 60)
        b = unreal.Vector(bl.x + bext.x + 800, bl.y + side * 6000, bl.z - 60)
        za, _ = game.ground(a.x, a.y)
        zb, _ = game.ground(b.x, b.y)
        if za is None or zb is None:
            game.check(True, "bypass test %+d: no ground 60 m to the side (crack bank ends) - skipped" % side)
            continue
        a.z, b.z = za + 60, zb + 60
        valid, pts, length, partial = game.path(a, b, no_crawl)
        ys = crossings(pts, x_line)
        bypass = valid and not partial and any(abs(y - bl.y) > bext.y for y in ys)
        game.check(not bypass, "bypass test 60 m %s of the bridge: valid=%s partial=%s %.0f m, crossings at Y %s (must be on the span)"
                   % ("north" if side > 0 else "south", valid, partial, length / 100, [round(y) for y in ys]))
    # The fence: standing path refused / partial, prone path through the gap.
    valid, pts, length, partial = game.path(f_w, f_e, no_crawl)
    game.check(not valid or partial, "fence: standing path through the low fence is refused (valid=%s partial=%s %.0f m)"
               % (valid, partial, length / 100))
    valid, pts, length, partial = game.path(f_w, f_e)
    ys = crossings(pts, fl.x)
    gap = crawl.get_editor_property("gap_width_cm")
    game.check(valid and not partial and ys and all(abs(y - fl.y) <= gap * 0.5 for y in ys),
               "fence: prone path crawls under through the gap (valid=%s partial=%s, crossings at Y %s)" % (valid, partial,
                                                                                                              [round(y) for y in ys]))
    chain2 = [("beyond the fence", f_e), ("VOL_A3_FIELD", centre("VOL_A3_FIELD")),
              ("VOL_A4_OVERLOOK", centre("VOL_A4_OVERLOOK")), ("VOL_A5_BASE", centre("VOL_A5_BASE")),
              ("VOL_A6_HIGH", centre("VOL_A6_HIGH"))]
    for (na, a), (nb, b) in zip(chain2, chain2[1:]):
        if a is None or b is None:
            game.check(False, "path %s -> %s: missing end point" % (na, nb))
            continue
        valid, pts, length, partial = game.path(a, b, no_crawl)
        game.check(valid and not partial, "path %s -> %s (standing): valid=%s partial=%s %.0f m" % (na, nb, valid, partial,
                                                                                                  length / 100))
    # Patrol routes: loops of the expected size.
    for route in game.all_of(unreal.PatrolRouteActor):
        spline = route.get_editor_property("route_spline")
        n = spline.get_number_of_spline_points()
        name = route.get_actor_label()
        game.check(n >= 4, "%s: %d waypoints, loop %.0f m" % (name, n, spline.get_spline_length() / 100))
        pts = [spline.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD) for i in range(n)]
        worst = 0.0
        for a, b in zip(pts, pts[1:] + pts[:1]):
            valid, _, length, partial = game.path(unreal.Vector(a.x, a.y, a.z + 60), unreal.Vector(b.x, b.y, b.z + 60))
            straight = math.hypot(b.x - a.x, b.y - a.y)
            ratio = length / straight if valid and not partial and straight > 0 else 99.0
            worst = max(worst, ratio)
        game.check(worst <= 1.15, "%s: every leg walkable and unobstructed (worst path / straight = %.2f)" % (name, worst))
    # On the ground: every tagged volume / marker / mine / barrel / crate / route stands within 40 cm of the ground below.
    floating = []
    checked = 0
    crates = game.all_of(unreal.LootCrateActor)
    for a in game.all_of(unreal.Actor):
        if TAG not in [str(t) for t in a.tags] and TAG_5D not in [str(t) for t in a.tags]:
            continue
        cls = a.get_class().get_name()
        if cls.startswith(("NavMeshBoundsVolume", "CameraActor", "StaticMeshActor", "CameraZoneVolume", "BridgeCrossingVolume")):
            continue
        l = a.get_actor_location()
        if cls.startswith("TutorialTriggerVolume"):
            bottom = l.z - a.get_editor_property("box").get_scaled_box_extent().z
        elif cls.startswith(("BarrelActor",)):
            bottom = l.z - 70
        elif cls.startswith(("LootCrateActor",)):
            bottom = l.z - 40
        elif cls.startswith(("BarricadeActor",)):
            bottom = l.z - 50
        elif cls.startswith(("ProximityMineActor",)):
            bottom = l.z - 30
        elif cls.startswith(("TutorialMarkerActor",)):
            bottom = l.z - 2
        else:
            bottom = l.z
        # A marker ring may circle a crate (A1_loot_open): the crate is not its ground.
        ignore = [a] + (crates if cls.startswith("TutorialMarkerActor") else [])
        z, _ = game.ground(l.x, l.y, ignore, bottom + 50)
        checked += 1
        if z is None or abs(bottom - z) > 40:
            floating.append("%s (%s): bottom %.0f, ground %s" % (a.get_name(), cls, bottom, z))
    game.check(not floating, "%d tagged actors on the ground%s" % (checked, (": " + "; ".join(floating)) if floating else ""))


def verify_ruin(game, by_id, centre, no_crawl):
    """Rev 5d: the ruin is reachable and roomy, the barrel stands outside it with a clear push path through the gap."""
    ruin = (by_id.get("VOL_A2_RUIN") or [None])[0]
    camp = centre("VOL_A2_CAMP")
    barrel = next((b for b in game.all_of(unreal.BarrelActor) if b.get_actor_label() == "Ruin_Barrel"), None)
    if ruin is None or camp is None or barrel is None:
        game.check(False, "ruin: VOL_A2_RUIN / VOL_A2_CAMP / Ruin_Barrel missing")
        return None
    game.check(not ruin.get_editor_property("fire_once"), "VOL_A2_RUIN: Fire Once off")
    rc, re = box_of(ruin)
    bl = barrel.get_actor_location()
    game.check(not in_box(bl, (rc, re), 60.0), "Ruin_Barrel stands OUTSIDE the ruin (%.0f, %.0f)" % (bl.x, bl.y))
    z, _ = game.ground(rc.x, rc.y, [ruin], 200.0)
    inside = unreal.Vector(rc.x, rc.y, (z if z is not None else 0.0) + 60)
    valid, _, length, partial = game.path(camp, inside, no_crawl)
    game.check(valid and not partial, "ruin: VOL_A2_CAMP -> inside the ruin (standing): valid=%s partial=%s %.0f m" % (valid, partial, length / 100))
    # Room for the squad + the barrel: four spots spread over the interior are on the navmesh and reachable.
    spots = 0
    for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
        q = unreal.Vector(rc.x + sx * (re.x - 60), rc.y + sy * (re.y - 60), inside.z)
        valid, _, length, partial = game.path(camp, q, no_crawl)
        spots += 1 if valid and not partial else 0
    game.check(spots == 4, "ruin: %d / 4 interior spots reachable (room for three operatives and the barrel)" % spots)
    # The push path: from the barrel to the interior, nearly straight, crossing the north wall line only inside the gap.
    # From just in front of the barrel (the barrel itself cuts a hole in the navmesh) to the interior.
    d = math.hypot(inside.x - bl.x, inside.y - bl.y) or 1.0
    start = unreal.Vector(bl.x + (inside.x - bl.x) / d * 100, bl.y + (inside.y - bl.y) / d * 100, inside.z)
    valid, pts, length, partial = game.path(start, inside, no_crawl)
    straight = math.hypot(inside.x - start.x, inside.y - start.y)
    gap = [a for a in game.all_of(unreal.StaticMeshActor) if a.get_actor_label() in ("Ruin_Wall_N_W", "Ruin_Wall_N_E")]
    ok_gap = True
    if len(gap) == 2:
        bounds = sorted([a.get_actor_bounds(False) for a in gap], key=lambda b: b[0].x)
        g0, g1 = bounds[0][0].x + bounds[0][1].x, bounds[1][0].x - bounds[1][1].x
        wall_y = bounds[0][0].y
        xs = []
        for a, b in zip(pts, pts[1:]):
            if (a.y - wall_y) * (b.y - wall_y) < 0:
                xs.append(a.x + (b.x - a.x) * (wall_y - a.y) / (b.y - a.y))
        ok_gap = bool(xs) and all(g0 + 40 <= x <= g1 - 40 for x in xs)
        game.lines.append("info ruin gap X %.0f..%.0f (%.0f cm), push path crosses the wall line at X %s" % (g0, g1, g1 - g0, [round(x) for x in xs]))
    game.check(valid and not partial and length <= straight * 1.15 + 50 and ok_gap,
               "ruin: push path barrel -> inside through the wide gap: valid=%s partial=%s %.1f m (straight %.1f m)" % (valid, partial, length / 100, straight / 100))
    return {"barrel": barrel, "box": (rc, re), "inside": inside, "camp": camp}


def start_ruin_push(game, ruin):
    """Rev 5d: an operative really pushes Ruin_Barrel into the ruin (URelocationSubsystem::ExecuteRelocate, as the Push order)."""
    ops = game.all_of(unreal.OperativeCharacter)
    # (no SubsystemBlueprintLibrary in game Python: the world subsystem is found by its object name under the world)
    relocation = unreal.find_object(game.world, "RelocationSubsystem_0")
    if not ops or relocation is None:
        game.check(False, "ruin push: no operative / relocation subsystem")
        return None
    worker = ops[0]
    bl = ruin["barrel"].get_actor_location()
    inside = ruin["inside"]
    d = unreal.Vector(inside.x - bl.x, inside.y - bl.y, 0.0)
    n = math.hypot(d.x, d.y) or 1.0
    stand = unreal.Vector(bl.x - d.x / n * 250, bl.y - d.y / n * 250, inside.z + 40)
    worker.set_actor_location(stand, False, True)
    relocation.execute_relocate(worker, ruin["barrel"], unreal.Vector(inside.x, inside.y, bl.z), 0.0, False)
    game.lines.append("info ruin push: %s ordered to push the barrel from (%.0f, %.0f) into the ruin" % (worker.get_name(), bl.x, bl.y))
    return {"worker": worker, "relocation": relocation}


def box_of(volume):
    """(centre, extent) of a tutorial volume's box (axis-aligned in this layout)."""
    return volume.get_actor_location(), volume.get_editor_property("box").get_scaled_box_extent()


def in_box(point, box, margin=0.0):
    c, e = box
    return abs(point.x - c.x) <= e.x + margin and abs(point.y - c.y) <= e.y + margin


def slot_field(slot, name):
    return slot.get_editor_property(name)


def verify_hut_entry(game, by_id, centre, no_crawl):
    """Rev 5c tactical hut entry: the door volumes, their cover (a high wall with the door edge exposed, the slot inside the
    stack volume and outside the interior volume), reachable spots, the interior volume repeatable."""
    hut = (by_id.get("VOL_A1_HUT") or [None])[0]
    approach = centre("VOL_A1_HUT_APPROACH")
    if hut is None or approach is None:
        game.check(False, "hut entry: VOL_A1_HUT / VOL_A1_HUT_APPROACH missing")
        return
    game.check(not hut.get_editor_property("fire_once"), "VOL_A1_HUT: Fire Once off (the third operative may walk in after a stacker)")
    hut_box = box_of(hut)
    markers = {name_str(m.get_editor_property("step_id")): m for m in game.all_of(unreal.TutorialMarkerActor)}
    valid, _, length, partial = game.path(approach, centre("VOL_A1_HUT"), no_crawl)
    game.check(valid and not partial, "hut entry: approach -> inside (through the door): valid=%s partial=%s %.0f m" % (valid, partial, length / 100))
    for vid, step in (("VOL_A1_DOOR_L", "A1_hut_stack_l"), ("VOL_A1_DOOR_R", "A1_hut_stack_r")):
        vol = (by_id.get(vid) or [None])[0]
        marker = markers.get(step)
        if vol is None or marker is None:
            game.check(False, "hut entry: %s / marker %s missing" % (vid, step))
            continue
        c, e = box_of(vol)
        game.check(not (abs(c.x - hut_box[0].x) <= e.x + hut_box[1].x and abs(c.y - hut_box[0].y) <= e.y + hut_box[1].y),
                   "%s does not overlap the interior volume VOL_A1_HUT" % vid)
        # The wall: the volume's long side nearest the hut interior.
        if e.x < e.y:
            sign = 1.0 if hut_box[0].x > c.x else -1.0
            face = unreal.Vector(c.x + sign * e.x, marker.get_actor_location().y, 0)
            direction = unreal.Vector(sign, 0, 0)
        else:
            sign = 1.0 if hut_box[0].y > c.y else -1.0
            face = unreal.Vector(marker.get_actor_location().x, c.y + sign * e.y, 0)
            direction = unreal.Vector(0, sign, 0)
        z, _ = game.ground(face.x - direction.x * 80, face.y - direction.y * 80, top=200.0)  # under the hut's eaves
        face.z = (z if z is not None else 0.0) + 90
        res = unreal.CoverQueryLibrary.find_cover_slot_at(game.world, face, direction)
        ok, slot = (res[0], res[1]) if isinstance(res, tuple) else (res is not None, res)
        if not ok or slot is None:
            game.check(False, "%s: the wall at the stack spot (%.0f, %.0f) gives NO cover slot" % (vid, face.x, face.y))
            continue
        loc = slot_field(slot, "world_location")
        height = unreal.CoverQueryLibrary.get_cover_height_name(slot)
        edges = "L %s %.0f / R %s %.0f" % (slot_field(slot, "left_edge_exposed"), slot_field(slot, "left_edge_distance_cm"),
                                            slot_field(slot, "right_edge_exposed"), slot_field(slot, "right_edge_distance_cm"))
        near_edge = min([d for flag, d in ((slot_field(slot, "left_edge_exposed"), slot_field(slot, "left_edge_distance_cm")),
                                            (slot_field(slot, "right_edge_exposed"), slot_field(slot, "right_edge_distance_cm"))) if flag] or [9999])
        game.check(height == "High" and in_box(loc, (c, e)) and not in_box(loc, hut_box, 40.0) and near_edge <= 200,
                   "%s: cover slot %s at (%.0f, %.0f), wall top %.0f cm, edges %s; inside %s, outside VOL_A1_HUT (+40 cm), door edge %.0f cm"
                   % (vid, height, loc.x, loc.y, slot_field(slot, "wall_top_cm"), edges, vid, near_edge))
        valid, _, length, partial = game.path(approach, unreal.Vector(loc.x, loc.y, loc.z + 60), no_crawl)
        game.check(valid and not partial, "%s: the stack spot is reachable from the approach (%.0f m)" % (vid, length / 100))


def shots(game):
    """Top-down stills (overview + three close-ups) from an in-game camera; labels written for the annotate step."""
    w = game.world
    labels = []
    for a in game.all_of(unreal.Actor):
        cls = a.get_class().get_name()
        l = a.get_actor_location()
        text = None
        if cls.startswith("TutorialTriggerVolume"):
            text = effective_volume_id(a)
            kind = "volume"
            e = a.get_editor_property("box").get_scaled_box_extent()
            labels.append({"text": text, "kind": kind, "x": l.x, "y": l.y, "ex": e.x, "ey": e.y, "z": l.z - e.z,
                           "auto": TAG in [str(t) for t in a.tags] or TAG_5D in [str(t) for t in a.tags]})
            continue
        if cls.startswith("StaticMeshActor") and (TAG in [str(t) for t in a.tags] or TAG_5D in [str(t) for t in a.tags]):
            o, e = a.get_actor_bounds(False)
            label = a.get_actor_label()
            names = {"LowFence_North": "LOW FENCE (closed line)", "VaultRidge_A3": "vault ridge 0.8 m",
                     "HighGround_Platform": "high ground 2 m", "Base_Roof": "PMC base (graybox)",
                     "Camp_Hut1_Roof": "camp hut 1", "Overlook_LowCover": "overlook cover",
                     "Container_A4_North": "patrol choke 3 m", "Rock_A3_GapNorth": "field exit gap",
                     "Cover_A3_1_Low": "cover field", "HutDoor_Jamb_L": "door jamb (graybox)",
                     "HutDoor_Jamb_R": "door jamb (graybox)", "Ruin_Wall_S": "RUIN (half-ruined, graybox)",
                     "Ruin_Wall_E_Rubble": "rubble 0.6 m", "Ruin_Roof_Partial": "partial roof"}
            if e.x < 20000 or label.startswith("LowFence"):
                labels.append({"text": names.get(label, ""), "kind": "struct", "x": o.x, "y": o.y, "ex": e.x, "ey": e.y})
            continue
        if cls.startswith("TutorialMarkerActor"):
            text, kind = "marker " + name_str(a.get_editor_property("step_id")), "marker"
        elif cls.startswith("BridgeCrossingVolume"):
            e = a.get_editor_property("span").get_scaled_box_extent()
            labels.append({"text": "BRIDGE (capacity 1)", "kind": "bridge", "x": l.x, "y": l.y, "ex": e.x, "ey": e.y})
            continue
        elif cls.startswith("CrawlGapActor"):
            text, kind = "CRAWL GAP", "fence"
        elif cls.startswith("CameraZoneVolume"):
            env = str(a.get_editor_property("environment")).split(".")[-1].split(":")[0]
            e = a.get_editor_property("box").get_scaled_box_extent()
            labels.append({"text": "zone " + env, "kind": "zone", "x": l.x, "y": l.y, "ex": e.x, "ey": e.y, "auto": True})
            continue
        elif cls.startswith("ProximityMineActor"):
            text, kind = "MINE", "trap"
        elif cls.startswith("PMCTripwireActor"):
            text, kind = "TRIPWIRE", "trap"
        elif cls.startswith("PatrolRouteActor"):
            spline = a.get_editor_property("route_spline")
            pts = [spline.get_location_at_spline_point(i, unreal.SplineCoordinateSpace.WORLD)
                   for i in range(spline.get_number_of_spline_points())]
            labels.append({"text": a.get_actor_label(), "kind": "route", "x": l.x, "y": l.y,
                           "points": [[q.x, q.y] for q in pts]})
            continue
        elif cls.startswith("PlayerStart"):
            text, kind = "PLAYER START", "start"
        elif cls.startswith(("BarrelActor",)):
            text, kind = "barrel", "object"
        elif cls.startswith(("LootCrateActor",)):
            text, kind = "crate" + (" (trapped)" if a.get_editor_property("trapped") else ""), "object"
        elif cls.startswith(("BarricadeActor",)):
            text, kind = "barricade", "object"
        if text:
            labels.append({"text": text, "kind": kind, "x": l.x, "y": l.y, "z": l.z})
    xs = [q["x"] for q in labels]
    ys = [q["y"] for q in labels]
    views = [("top", min(xs) - 1500, max(xs) + 1500, min(ys) - 1500, max(ys) + 1500)]
    # Close-ups: the crossing (start -> hut), the camp + approach, the overlook + compound.
    vol = {q["text"]: q for q in labels if q["kind"] == "volume"}

    def span(names, margin=2500):
        pts = [vol[n] for n in names if n in vol]
        return (min(q["x"] for q in pts) - margin, max(q["x"] for q in pts) + margin,
                min(q["y"] for q in pts) - margin, max(q["y"] for q in pts) + margin) if pts else None

    hut_names = ["VOL_A1_HUT", "VOL_A1_DOOR_L", "VOL_A1_DOOR_R", "VOL_A1_HUT_APPROACH"]
    for name, names in (("crossing", ["VOL_A0_BEACON1", "VOL_A1_BEACON2", "VOL_A1_HUT"]),
                        ("approach", ["VOL_A1_BEACON3", "VOL_A2_CAMP", "VOL_A3_APPROACH", "VOL_A3_FIELD"]),
                        ("compound", ["VOL_A3_FIELD", "VOL_A4_OVERLOOK", "VOL_A5_BASE", "VOL_A6_HIGH"])):
        s = span(names)
        if s:
            views.append((name,) + s)
    # Rev 5c: the hut entry - top-down with the hut roof hidden for the shot, and a perspective view from the approach.
    roof = None
    hut_vol = vol.get("VOL_A1_HUT")
    if hut_vol:
        for a in game.all_of(unreal.StaticMeshActor):
            o, e = a.get_actor_bounds(False)
            if abs(o.x - hut_vol["x"]) < hut_vol["ex"] + 300 and abs(o.y - hut_vol["y"]) < hut_vol["ey"] + 300 and e.z < 60 and o.z > 200:
                roof = a
        s = span(["VOL_A1_HUT", "VOL_A1_DOOR_L", "VOL_A1_DOOR_R"], 500)
        if s:
            views.append(("hut",) + s)
    # Rev 5d: the ruin (top-down, its partial roof hidden for the shot) + a perspective from the barrel's side.
    ruin_vol = vol.get("VOL_A2_RUIN")
    ruin_roof = None
    if ruin_vol:
        ruin_roof = next((a for a in game.all_of(unreal.StaticMeshActor) if a.get_actor_label() == "Ruin_Roof_Partial"), None)
        s = span(["VOL_A2_RUIN"], 900)
        if s:
            views.append(("ruin",) + s)
    res_x, res_y = 3840, 2160
    fov = 30.0
    meta = {"labels": labels, "views": [], "res": [res_x, res_y]}
    # Python cannot spawn actors in a game world: the layout's own CameraActor (the base interior camera) is borrowed and
    # moved for each view (nothing is saved in game).
    camera = next((a for a in game.all_of(unreal.CameraActor) if TAG in [str(t) for t in a.tags]), None)
    if camera is None:
        camera = (game.all_of(unreal.CameraActor) or [None])[0]
    comp = camera.get_editor_property("camera_component")
    comp.set_editor_property("field_of_view", fov)
    comp.set_editor_property("constrain_aspect_ratio", False)
    cams = []
    for name, x0, x1, y0, y1 in views:
        cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
        width = max(x1 - x0, (y1 - y0) * res_x / res_y)  # image right = +X, image down = +Y
        height = width / (2.0 * math.tan(math.radians(fov * 0.5)))
        transform = unreal.Transform(unreal.Vector(cx, cy, height), unreal.Rotator(pitch=-89.9, yaw=-90.0, roll=0.0),
                                     unreal.Vector(1, 1, 1))
        hide = [roof] if name == "hut" and roof else ([ruin_roof] if name == "ruin" and ruin_roof else [])
        cams.append((name, camera, transform, hide, fov))
        meta["views"].append({"name": name, "cx": cx, "cy": cy, "width_cm": width, "file": "demo01_autolayout_raw_%s.png" % name})
    # Perspective view of the hut door from the approach side: 9 m out, 5 m up, looking at the door.
    door_l, door_r = vol.get("VOL_A1_DOOR_L"), vol.get("VOL_A1_DOOR_R")
    if hut_vol and door_l and door_r:
        dx, dy = (door_l["x"] + door_r["x"]) * 0.5, (door_l["y"] + door_r["y"]) * 0.5
        ox, oy = dx - hut_vol["x"], dy - hut_vol["y"]
        n = math.hypot(ox, oy) or 1.0
        ox, oy = ox / n, oy / n
        z0 = door_l.get("z", 0.0)
        loc = unreal.Vector(dx + ox * 900, dy + oy * 900, z0 + 500)
        look_yaw = math.degrees(math.atan2(-oy, -ox))
        look_pitch = -math.degrees(math.atan2(500 - 100, 900))
        angle_fov = 60.0
        transform = unreal.Transform(loc, unreal.Rotator(pitch=look_pitch, yaw=look_yaw, roll=0.0), unreal.Vector(1, 1, 1))
        cams.append(("hut_angle", camera, transform, [], angle_fov))
        meta["views"].append({"name": "hut_angle", "file": "demo01_autolayout_raw_hut_angle.png",
                              "proj": {"loc": [loc.x, loc.y, loc.z], "pitch": look_pitch, "yaw": look_yaw, "fov": angle_fov}})
    if ruin_vol:
        bar = next((q for q in labels if q["kind"] == "object" and abs(q["x"] - ruin_vol["x"]) < 400 and 0 < q["y"] - ruin_vol["y"] < 1200), None)
        ox, oy = (bar["x"], bar["y"] + 500) if bar else (ruin_vol["x"], ruin_vol["y"] + 1200)
        loc = unreal.Vector(ox + 300, oy, ruin_vol.get("z", 0.0) + 650)
        look_yaw = math.degrees(math.atan2(ruin_vol["y"] - oy, ruin_vol["x"] - ox - 300))
        dist = math.hypot(ruin_vol["x"] - ox - 300, ruin_vol["y"] - oy)
        look_pitch = -math.degrees(math.atan2(650 - 100, dist))
        transform = unreal.Transform(loc, unreal.Rotator(pitch=look_pitch, yaw=look_yaw, roll=0.0), unreal.Vector(1, 1, 1))
        cams.append(("ruin_angle", camera, transform, [], 60.0))
        meta["views"].append({"name": "ruin_angle", "file": "demo01_autolayout_raw_ruin_angle.png",
                              "proj": {"loc": [loc.x, loc.y, loc.z], "pitch": look_pitch, "yaw": look_yaw, "fov": 60.0}})
    with open(SAVED + "Screenshots/demo01_autolayout_labels.json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
    return cams


def game_main(mode):
    state = {"t": 0.0, "stage": 0, "handle": None, "cams": [], "shot": 0, "wait": 0.0}
    REPORT.append("%s %s %s" % (mode, TARGET_MAP, datetime.datetime.now().isoformat()))
    log_name = "Demo01LayoutVerify.txt" if mode == "verify" else "Demo01LayoutShots.txt"

    def finish():
        unreal.unregister_slate_post_tick_callback(state["handle"])
        flush_report(log_name)
        unreal.SystemLibrary.execute_console_command(game_world(), "quit")

    def tick(dt):
        try:
            state["t"] += dt
            w = game_world()
            if w is None:
                if state["t"] > 60:
                    REPORT.append("RESULT: FAIL (world %s not found)" % TARGET_MAP)
                    finish()
                return
            if state["stage"] == 0:
                building = unreal.NavigationSystemV1.is_navigation_being_built_or_locked(w)
                if state["t"] < 10 or (building and state["t"] < 240):
                    return
                REPORT.append("nav ready after %.1f s (still building=%s)" % (state["t"], building))
                game = Game(w)
                if mode == "verify":
                    verify(game)
                    push = start_ruin_push(game, game.ruin) if game.ruin else None
                    if push:
                        # Rev 5d: wait for the real push into the ruin (stage 10).
                        state.update({"game": game, "push": push, "stage": 10, "wait": state["t"] + 90.0})
                        return
                    REPORT.extend(game.lines)
                    REPORT.append("RESULT: %s" % ("PASS" if game.ok else "FAIL"))
                    finish()
                    return
                unreal.SystemLibrary.execute_console_command(w, "ShowFlag.Fog 0")
                unreal.SystemLibrary.execute_console_command(w, "ShowHUD")
                unreal.SystemLibrary.execute_console_command(w, "DisableAllScreenMessages")
                unreal.SystemLibrary.execute_console_command(w, "r.Streaming.FullyLoadUsedTextures 1")
                state["cams"] = shots(game)
                state["stage"] = 1
                state["wait"] = state["t"] + 3.0
                return
            if state["stage"] == 10:
                game, ruin, push = state["game"], state["game"].ruin, state["push"]
                bl = ruin["barrel"].get_actor_location()
                done = in_box(bl, ruin["box"]) and push["relocation"].get_active_task_count() == 0
                if not done and state["t"] < state["wait"]:
                    return
                game.check(done, "ruin push: the barrel was pushed INTO the ruin (now at (%.0f, %.0f), %.0f s)" % (bl.x, bl.y, state["t"] - (state["wait"] - 90.0)))
                # With the barrel inside, the squad still fits round it.
                spots = 0
                rc, re = ruin["box"]
                for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
                    q = unreal.Vector(rc.x + sx * (re.x - 60), rc.y + sy * (re.y - 60), ruin["inside"].z)
                    valid, _, _, partial = game.path(ruin["camp"], q, unreal.NavFilter_NoCrawl.static_class())
                    spots += 1 if valid and not partial else 0
                game.check(spots >= 3, "ruin: with the barrel inside, %d / 4 interior spots still reachable" % spots)
                REPORT.extend(game.lines)
                REPORT.append("RESULT: %s" % ("PASS" if game.ok else "FAIL"))
                finish()
                return
            if state["stage"] == 1 and state["t"] >= state["wait"]:
                if state["shot"] >= len(state["cams"]):
                    REPORT.append("RESULT: PASS (%d shots)" % len(state["cams"]))
                    finish()
                    return
                name, cam, transform, hide, view_fov = state["cams"][state["shot"]]
                for a in hide:
                    a.set_actor_hidden_in_game(True)
                cam.get_editor_property("camera_component").set_editor_property("field_of_view", view_fov)
                cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
                cam.set_actor_transform(transform, False, True)
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(cam, 0.0)
                state["stage"] = 2
                state["wait"] = state["t"] + 4.0
                return
            if state["stage"] == 2 and state["t"] >= state["wait"]:
                name, cam, transform, hide, view_fov = state["cams"][state["shot"]]
                unreal.SystemLibrary.execute_console_command(
                    w, "HighResShot 3840x2160 filename=demo01_autolayout_raw_%s.png" % name)
                REPORT.append("shot %s requested" % name)
                state["shot"] += 1
                state["stage"] = 3
                state["wait"] = state["t"] + 6.0
                state["shown_again"] = hide
                return
            if state["stage"] == 3 and state["t"] >= state["wait"]:
                for a in state.get("shown_again") or []:
                    a.set_actor_hidden_in_game(False)  # the hut roof back after the top-down hut shot
                state["stage"] = 1
        except Exception as exc:
            import traceback
            REPORT.append("EXCEPTION: " + repr(exc))
            REPORT.append(traceback.format_exc())
            REPORT.append("RESULT: FAIL")
            finish()

    state["handle"] = unreal.register_slate_post_tick_callback(tick)


# ----------------------------------------------------------------------------------------------------------------------
# Annotate (system python + Pillow)
# ----------------------------------------------------------------------------------------------------------------------

def annotate():
    from PIL import Image, ImageDraw, ImageFont
    shots_dir = SAVED + "Screenshots" + os.sep
    with open(shots_dir + "demo01_autolayout_labels.json", encoding="utf-8") as f:
        meta = json.load(f)
    colours = {"struct": (190, 210, 235), "volume": (255, 170, 30), "marker": (255, 230, 80), "bridge": (80, 220, 255), "fence": (80, 220, 255),
               "zone": (120, 170, 255), "trap": (255, 60, 60), "route": (255, 90, 200), "start": (90, 255, 120),
               "object": (230, 230, 230)}
    try:
        font = ImageFont.truetype("arialbd.ttf", 26)
        small = ImageFont.truetype("arial.ttf", 20)
    except Exception:
        font = small = ImageFont.load_default()
    for view in meta["views"]:
        raw = None
        for root, _, files in os.walk(shots_dir):
            if view["file"] in files:
                cand = os.path.join(root, view["file"])
                if raw is None or os.path.getmtime(cand) > os.path.getmtime(raw):
                    raw = cand
        if raw is None:
            print("missing", view["file"])
            continue
        img = Image.open(raw).convert("RGB")
        w, h = img.size
        proj = view.get("proj")
        s = w / view["width_cm"] if not proj else 1.0

        def px(x, y, z=0.0):
            if not proj:
                return w * 0.5 + (x - view["cx"]) * s, h * 0.5 + (y - view["cy"]) * s
            # Perspective (UE: X forward, Y right, Z up; horizontal FOV).
            cp, sp = math.cos(math.radians(proj["pitch"])), math.sin(math.radians(proj["pitch"]))
            cyw, syw = math.cos(math.radians(proj["yaw"])), math.sin(math.radians(proj["yaw"]))
            fwd = (cp * cyw, cp * syw, sp)
            right = (-syw, cyw, 0.0)
            up = (-sp * cyw, -sp * syw, cp)
            d = (x - proj["loc"][0], y - proj["loc"][1], z - proj["loc"][2])
            depth = sum(a * b for a, b in zip(d, fwd))
            if depth < 10:
                return -9999, -9999
            focal = (w * 0.5) / math.tan(math.radians(proj["fov"] * 0.5))
            return (w * 0.5 + sum(a * b for a, b in zip(d, right)) / depth * focal,
                    h * 0.5 - sum(a * b for a, b in zip(d, up)) / depth * focal)

        draw = ImageDraw.Draw(img, "RGBA")
        taken = []
        for q in meta["labels"]:
            c = colours.get(q["kind"], (255, 255, 255))
            qz = q.get("z", 0.0)
            x, y = px(q["x"], q["y"], qz)
            if proj and q["kind"] == "zone":
                continue  # perspective close-up: cold / camera zones would only clutter the door
            if proj and q["kind"] in ("volume", "struct", "bridge") and "ex" in q:
                corners = [px(q["x"] + sx * q["ex"], q["y"] + sy * q["ey"], qz) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
                if all(cx > -5000 for cx, _ in corners):
                    draw.polygon(corners, outline=c + (255,), fill=c + (40,))
                if not q["text"]:
                    continue
            elif proj:
                draw.ellipse([x - 7, y - 7, x + 7, y + 7], fill=c + (255,), outline=(0, 0, 0, 255))
            if proj:
                pass
            elif q["kind"] == "struct":
                x0, y0 = px(q["x"] - q["ex"], q["y"] - q["ey"])
                x1, y1 = px(q["x"] + q["ex"], q["y"] + q["ey"])
                draw.rectangle([x0, y0, max(x1, x0 + 2), max(y1, y0 + 2)], outline=(40, 60, 90, 255), width=2,
                               fill=(40, 60, 90, 110))
                if not q["text"]:
                    continue
            elif q["kind"] in ("volume", "zone") or "ex" in q:
                x0, y0 = px(q["x"] - q["ex"], q["y"] - q["ey"])
                x1, y1 = px(q["x"] + q["ex"], q["y"] + q["ey"])
                draw.rectangle([x0, y0, x1, y1], outline=c + (255,), width=3, fill=c + (40,))
            elif q["kind"] == "route":
                pts = [px(a, b) for a, b in q["points"]]
                draw.line(pts + pts[:1], fill=c + (255,), width=4)
            else:
                draw.ellipse([x - 7, y - 7, x + 7, y + 7], fill=c + (255,), outline=(0, 0, 0, 255))
            if not (-200 < x < w + 200 and -200 < y < h + 200):
                continue
            text = q["text"] if q["kind"] != "zone" else q["text"]
            f = font if q["kind"] in ("volume", "bridge", "fence", "start", "trap") else small
            ty = y - 34
            while any(abs(ty - t[1]) < 24 and abs(x - t[0]) < 260 for t in taken):
                ty -= 24
            taken.append((x, ty))
            tw = draw.textlength(text, font=f)
            draw.rectangle([x + 8, ty - 2, x + 14 + tw, ty + 26], fill=(0, 0, 0, 170))
            draw.text((x + 11, ty), text, fill=c + (255,), font=f)
        draw.rectangle([0, 0, w, 44], fill=(0, 0, 0, 200))
        draw.text((12, 8), "Demo 01 Frozen Lake - AutoLayout (%s). +X right. Amber = tutorial volumes, blue = cold zones / bridge / "
                  "crawl gap, red = PMC traps, pink = patrol loops. %s" % (view["name"], "perspective" if proj else "1 px = %.1f cm" % (1 / s)),
                  fill=(255, 255, 255), font=small)
        out = shots_dir + ("demo01_autolayout_top.png" if view["name"] == "top" else "demo01_autolayout_%s.png" % view["name"])
        img.save(out)
        print("wrote", out)


def main():
    if unreal is None:
        annotate()
        return
    mode = os.environ.get("CODEX_LAYOUT_MODE", "build")
    if mode in ("verify", "shots"):
        game_main(mode)
        return
    try:
        if mode == "dump":
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(TARGET_MAP)
            dump(TARGET_MAP, "%s_%s.json" % (TARGET_MAP.split("/")[-1], datetime.datetime.now().strftime("%Y%m%d_%H%M%S")))
        elif mode == "additive":
            additive()
        else:
            build()
    except Exception as exc:
        import traceback
        report("EXCEPTION: " + repr(exc))
        report(traceback.format_exc())
    flush_report("Demo01Layout5d.txt" if mode == "additive" else "Demo01Layout.txt")


main()
