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

Modes (env CODEX_LAYOUT_MODE):
  build (default)  editor commandlet: duplicate the source map to the target if the target does not exist yet
                   (CODEX_LAYOUT_FRESH=1 deletes the target first), place the layout, save, dump before / after.
  dump             editor commandlet: dump the target (or CODEX_LAYOUT_TARGET) to Saved/LayoutDumps.
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
    def __init__(self):
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
            if TAG in [str(t) for t in a.tags]:
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
        actor.set_folder_path(FOLDER + "/" + section)
        actor.set_editor_property("tags", [TAG])
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
            actor.set_editor_property("tags", [TAG] + list(tags))
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
    if want_marker("A1_solo"):
        p.marker("A1_solo", bridge_x0 - 180, bridge_y, 120, anchor="near end of the bridge")
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
        if want_volume("VOL_A1_HUT"):
            p.volume("VOL_A1_HUT", hc.x, hc.y, (max(100.0, he.x - 90), max(100.0, he.y - 80), 150), any_operative=True,
                     anchor=hut_anchor + ", over the floor")
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
            v = (step.get(key) or {}).get("volume")
            if v:
                ids.add(v)
    beats = [b["id"] for b in data.get("beats", [])]
    return sorted(ids), beats


class Game:
    def __init__(self, world):
        self.world = world
        self.ok = True
        self.lines = []

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
             ("VOL_A1_BEACON2", centre("VOL_A1_BEACON2")), ("bridge far end", b_e), ("VOL_A1_HUT", centre("VOL_A1_HUT")),
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
        if TAG not in [str(t) for t in a.tags]:
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
            labels.append({"text": text, "kind": kind, "x": l.x, "y": l.y, "ex": e.x, "ey": e.y,
                           "auto": TAG in [str(t) for t in a.tags]})
            continue
        if cls.startswith("StaticMeshActor") and TAG in [str(t) for t in a.tags]:
            o, e = a.get_actor_bounds(False)
            label = a.get_actor_label()
            names = {"LowFence_North": "LOW FENCE (closed line)", "VaultRidge_A3": "vault ridge 0.8 m",
                     "HighGround_Platform": "high ground 2 m", "Base_Roof": "PMC base (graybox)",
                     "Camp_Hut1_Roof": "camp hut 1", "Overlook_LowCover": "overlook cover",
                     "Container_A4_North": "patrol choke 3 m", "Rock_A3_GapNorth": "field exit gap",
                     "Cover_A3_1_Low": "cover field"}
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
            labels.append({"text": text, "kind": kind, "x": l.x, "y": l.y})
    xs = [q["x"] for q in labels]
    ys = [q["y"] for q in labels]
    views = [("top", min(xs) - 1500, max(xs) + 1500, min(ys) - 1500, max(ys) + 1500)]
    # Close-ups: the crossing (start -> hut), the camp + approach, the overlook + compound.
    vol = {q["text"]: q for q in labels if q["kind"] == "volume"}

    def span(names, margin=2500):
        pts = [vol[n] for n in names if n in vol]
        return (min(q["x"] for q in pts) - margin, max(q["x"] for q in pts) + margin,
                min(q["y"] for q in pts) - margin, max(q["y"] for q in pts) + margin) if pts else None

    for name, names in (("crossing", ["VOL_A0_BEACON1", "VOL_A1_BEACON2", "VOL_A1_HUT"]),
                        ("approach", ["VOL_A1_BEACON3", "VOL_A2_CAMP", "VOL_A3_APPROACH", "VOL_A3_FIELD"]),
                        ("compound", ["VOL_A3_FIELD", "VOL_A4_OVERLOOK", "VOL_A5_BASE", "VOL_A6_HIGH"])):
        s = span(names)
        if s:
            views.append((name,) + s)
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
        cams.append((name, camera, transform))
        meta["views"].append({"name": name, "cx": cx, "cy": cy, "width_cm": width, "file": "demo01_autolayout_raw_%s.png" % name})
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
            if state["stage"] == 1 and state["t"] >= state["wait"]:
                if state["shot"] >= len(state["cams"]):
                    REPORT.append("RESULT: PASS (%d shots)" % len(state["cams"]))
                    finish()
                    return
                name, cam, transform = state["cams"][state["shot"]]
                cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
                cam.set_actor_transform(transform, False, True)
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(cam, 0.0)
                state["stage"] = 2
                state["wait"] = state["t"] + 4.0
                return
            if state["stage"] == 2 and state["t"] >= state["wait"]:
                name, cam, transform = state["cams"][state["shot"]]
                unreal.SystemLibrary.execute_console_command(
                    w, "HighResShot 3840x2160 filename=demo01_autolayout_raw_%s.png" % name)
                REPORT.append("shot %s requested" % name)
                state["shot"] += 1
                state["stage"] = 1
                state["wait"] = state["t"] + 6.0
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
        s = w / view["width_cm"]

        def px(x, y):
            return w * 0.5 + (x - view["cx"]) * s, h * 0.5 + (y - view["cy"]) * s

        draw = ImageDraw.Draw(img, "RGBA")
        taken = []
        for q in meta["labels"]:
            c = colours.get(q["kind"], (255, 255, 255))
            x, y = px(q["x"], q["y"])
            if q["kind"] == "struct":
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
                  "crawl gap, red = PMC traps, pink = patrol loops. 1 px = %.1f cm" % (view["name"], 1 / s),
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
        else:
            build()
    except Exception as exc:
        import traceback
        report("EXCEPTION: " + repr(exc))
        report(traceback.format_exc())
    flush_report("Demo01Layout.txt")


main()
