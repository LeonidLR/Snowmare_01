"""Level generator map builder (docs/design/levelgen/LEVEL_GENERATOR.md section 7): builds a NEW greybox map
/Game/Maps/Generated/L_Gen_<profile>_<seed> from an ACCEPTED generated layout Content/Data/LevelGen/accepted/<name>.json
(schema codex.levelgen_layout/1, the Training Ground generator's output) and writes its level JSON
Content/Data/LevelJson/<map>.json. Generalises Scripts/Editor/build_formation_lab.py (the same actor vocabulary: engine cubes
with the world grid material, TargetPoints, ALadderActor, PlayerStart, NavMeshBoundsVolume) for the extended format:

  - HEIGHT LEVELS (game designer 2026-11-01): level n sits at n x 300 cm; the cut floor at level -1, floors / roofs at +1 / +2,
    the tall halls' roofs at +3 = 9 m (2026-11-02);
  - the ground slab over the bounds + a margin, split round the cut's trench; the trench floor slab at -300; the retaining faces
    below ground stand OUTSIDE the trench (the layout's cut_wall walls stand on the ground above them); crossing slabs (bridges) across
    the trench at ground level; a "ramp" crossing also gets a ramp from the bridge slab's edge down ALONG the trench onto its floor
    (levelgen_ladders.cut_ramps, 2026-11-02 - the old ramp joined nothing: its top end hung over the trench, its middle ran under the
    bridge with 0.7 m of head room);
  - walls per level (a floor-n wall stands on its slab), fences (thin), building perimeters with their door gaps (already split by
    the generator);
  - buildings: floor slabs at every intermediate level (hollow buildings have none), the floor holes cut as four slabs round the
    hole, the roof slab at level `floors` with its roof holes, stairwells / external stairs as sloped cubes (ramps) between their
    levels - the stairwell opening ENDS exactly at the ramp's top (no gap the navmesh cannot cross), an external stair's ramp ends
    exactly where its flat landing starts (no step: the landing never overlaps the ramp's top) and the landing crosses the wall's
    top onto the roof; factory chimneys (set-piece "chimney") are closed cylinders, never climbable;
  - ladders: ALadderActor (Height = rungs x pitch), a plinth cube of plinth_cm under its foot so the top lands on the level
    (LADDERS.md 3.1 top tolerance 8 cm); since 2026-11-02 (levelgen_ladders.py, the game designer's bug report) the actor's yaw comes
    from the geometry (+X from `pos` towards the layout's `foot`: the older layouts carry a mirrored yaw), its rung plane stands 10 cm
    off the OUTER face of the wall behind it (the builder's walls are centred on the generator's lines), and a wall on the upper level
    that the top exit crosses (the cut's retaining wall) gets a 2 m opening; tag "ladder=<id>";
  - covers: cubes (silos: cylinders) at pos / yaw / size; PlayerStart at the spawn; the end as a TargetPoint "LevelGenEnd";
  - route TargetPoints per route point (label "<route>_<n>", tags LevelGen + LevelGenRoute + the route id + "layer=<layer>"),
    spawn / patrol / ambush TargetPoints (ambush points tagged CodexAmbushPoint for UCombatPressureSubsystem), zone labels;
  - NavMeshBoundsVolume over the bounds from a level below the cut to 3 m above the highest walkable level (+3 -> 12 m; a chimney's
    top stays outside, so it never gets a navmesh island); the saved RecastNavMesh actors are destroyed before the save
    (docs/port/TRAPS.md) - bake with Scripts/bake_navmesh.ps1 -Map <map> afterwards;
  - DROP nav links through the roof / floor holes (2026-11-02, levelgen_bot_route.drop_links): per hole a NavLinkProxy with four
    one-way point links from 70 cm outside each edge on the upper slab down to the hole's centre one level below - the squad
    (and the enemies) walk off the rim and fall 3 m instead of being teleported; only one-level drops onto a real slab;
  - the level JSON's "bot_route" (levelgen_bot_route.bot_route: the mandatory route as the playtest bot's planned route, with each
    waypoint's floor height and the drop legs marked); a rebuild KEEPS the existing level JSON's "pressure" / "cheats" blocks
    (tuned on the Training Ground combat lab page) when it was written for the same layout.
Idempotent per map (every actor tagged LevelGen is deleted and placed again); never touches another map (L_Demo01_B_FrozenFactory
is not read or written). Lighting (sun, sky, fog) placed once (tag LevelGen_Env).

Env: CODEX_LEVELGEN_LAYOUT (the accepted layout JSON; default the newest in Content/Data/LevelGen/accepted), CODEX_LEVELGEN_TARGET
(the map path; default /Game/Maps/Generated/L_Gen_<profile short>_<seed>), CODEX_LEVELGEN_FRESH=1 (delete the map first).
Report: Saved/Logs/<map leaf>Layout.txt.
Run (this project's editor closed): UnrealEditor-Cmd.exe <project>/CodexTactics.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi
Without Unreal: `python build_levelgen_map.py plan [layout.json]` prints the actor plan as JSON and writes the level JSON (no map).
"""
import datetime
import json
import math
import os
import sys

try:
    import unreal
except ImportError:  # system python: the `plan` mode only
    unreal = None

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import levelgen_bot_route  # noqa: E402  (pure: the bot route block + the drop links)
import levelgen_ladders  # noqa: E402  (pure: the ladder frames, the top exit openings, the cut's faces / ramps)

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(HERE, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")
LEVELJSON_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelJson")
SCHEMA = "codex.levelgen_layout/1"
TAG = "LevelGen"
ENV_TAG = "LevelGen_Env"
ROUTE_TAG = "LevelGenRoute"
AMBUSH_TAG = "CodexAmbushPoint"
FOLDER = "LevelGen"
LEVEL_HEIGHT = 300.0   # cm per height level
PITCH = 36.4           # ladder rung pitch, cm (LADDERS.md)
MIN_RUNGS = 7
SLAB = 20.0            # cm, every floor / roof / ground slab
MARGIN = 1000.0        # cm of ground plane round the layout bounds
PARAPET_H = 100.0
STAIR_W = 150.0
STAIRWELL_HOLE = 350.0   # cm: the slab opening above a stairwell ramp's last 3.5 m (head room 0.7 x 350 - 20 = 2.25 m at its far edge)
LANDING_TUCK = 5.0       # cm: the external landing starts this far before the ramp's top end (a 3.5 cm step, under the agent's 35 cm)
CUBE = "/Engine/BasicShapes/Cube.Cube"
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"
GRID = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
START_Z = 100.0
REPORT = []


def report(line):
    REPORT.append(str(line))
    if unreal is None:
        print(line, file=sys.stderr)


def newest_accepted():
    files = [os.path.join(ACCEPTED_DIR, f) for f in os.listdir(ACCEPTED_DIR)] if os.path.isdir(ACCEPTED_DIR) else []
    files = [f for f in files if f.endswith(".json") and not f.endswith(".meta.json")]
    if not files:
        raise ValueError("no accepted layout in %s (accept one in Training Ground or `npm run levelgen -- --accept <name>`)" % ACCEPTED_DIR)
    return max(files, key=os.path.getmtime)


LAYOUT_FILE = os.environ.get("CODEX_LEVELGEN_LAYOUT") or (newest_accepted() if (unreal is not None or (len(sys.argv) < 3)) else sys.argv[2])


def load_layout(path):
    with open(path, encoding="utf-8") as f:
        layout = json.load(f)
    problems = []
    if layout.get("schema") != SCHEMA:
        problems.append("schema must be %s" % SCHEMA)
    for key in ("name", "bounds", "spawn", "walls", "buildings", "ladders", "covers", "zones", "routes", "end"):
        if key not in layout:
            problems.append("missing %s" % key)
    for ld in layout.get("ladders", []):
        if int(ld.get("rungs", 0)) < MIN_RUNGS:
            problems.append("ladder %s: >= %d rungs" % (ld.get("id"), MIN_RUNGS))
    if sum(1 for r in layout.get("routes", []) if r.get("mandatory")) != 1:
        problems.append("exactly one mandatory route")
    if problems:
        raise ValueError("%s: %s (evaluate it in Training Ground first)" % (path, "; ".join(problems)))
    return layout


def map_name_for(layout):
    short = layout["profile"].replace("ref_", "").replace("01_02_", "")
    return "/Game/Maps/Generated/L_Gen_%s_%d" % (short.capitalize(), int(layout["seed"]))


TARGET_MAP = os.environ.get("CODEX_LEVELGEN_TARGET", "")


def z_of(level):
    return level * LEVEL_HEIGHT


def cube(label, folder, center, size, yaw=0.0, tags=None, pitch=0.0):
    return {"kind": "cube", "label": label, "folder": folder, "location": [center[0], center[1], center[2]], "yaw": yaw, "pitch": pitch,
            "scale": [size[0] / 100.0, size[1] / 100.0, size[2] / 100.0], "tags": tags or []}


def wall_cube(label, folder, a, b, z0, height, thickness, tags=None):
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    return cube(label, folder, [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, z0 + height / 2.0], [length, thickness, height],
                math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])), tags)


def rect_slab(label, folder, x0, y0, x1, y1, top_z, tags=None):
    return cube(label, folder, [(x0 + x1) / 2.0, (y0 + y1) / 2.0, top_z - SLAB / 2.0], [x1 - x0, y1 - y0, SLAB], 0.0, tags)


def slabs_with_holes(label, folder, x0, y0, x1, y1, top_z, holes):
    """A slab with square holes cut out: the rectangle is split into strips round each hole (holes along x, at most a few).
    A hole = (x, y) (half size 120 cm: the ruin's roof / floor holes) or (x, y, half) (a stairwell opening)."""
    out = []
    cuts = sorted(holes, key=lambda h: h[0])
    cx = x0
    for n, h in enumerate(cuts):
        hx, hy = h[0], h[1]
        hole_half = h[2] if len(h) > 2 else 120.0
        hx0, hx1 = max(x0, hx - hole_half), min(x1, hx + hole_half)
        hy0, hy1 = max(y0, hy - hole_half), min(y1, hy + hole_half)
        if hx1 <= hx0 or hy1 <= hy0:
            continue
        if hx0 > cx:
            out.append(rect_slab("%s_%d_w" % (label, n), folder, cx, y0, hx0, y1, top_z))
        if hy0 > y0:
            out.append(rect_slab("%s_%d_n" % (label, n), folder, hx0, y0, hx1, hy0, top_z))
        if hy1 < y1:
            out.append(rect_slab("%s_%d_s" % (label, n), folder, hx0, hy1, hx1, y1, top_z))
        cx = hx1
    if cx < x1:
        out.append(rect_slab("%s_e" % label, folder, cx, y0, x1, y1, top_z))
    return out


def ramp(label, folder, a, b, z0, z1, width=STAIR_W, tags=None):
    """A sloped cube from a (at z0) to b (at z1): a walkable stair stand-in (<= 44 deg)."""
    run = math.hypot(b[0] - a[0], b[1] - a[1])
    rise = z1 - z0
    length = math.hypot(run, rise)
    pitch = math.degrees(math.atan2(rise, run))  # UE pitch: positive = nose UP (the local +X end rises towards b)
    return cube(label, folder, [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, (z0 + z1) / 2.0 - SLAB / 2.0], [length, width, SLAB],
                math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])), tags, pitch)


def plan(layout):
    """The actors to place (pure dicts; cm; UE yaw = atan2(dy, dx))."""
    gz = float(layout.get("ground_z", 0.0))
    (x0, y0), (x1, y1) = layout["bounds"]["min"], layout["bounds"]["max"]
    specs = []
    cut = layout.get("cut")
    # ---- the ground: one slab, or four round the trench (the trench polygon is an axis-aligned band across the map)
    gx0, gy0, gx1, gy1 = x0 - MARGIN, y0 - MARGIN, x1 + MARGIN, y1 + MARGIN
    if cut:
        px = [p[0] for p in cut["poly"]]
        py = [p[1] for p in cut["poly"]]
        tx0, tx1, ty0, ty1 = min(px), max(px), min(py), max(py)
        band_x = (tx1 - tx0) < (ty1 - ty0)  # the band's short side is x -> it runs along y
        if band_x:
            specs.append(rect_slab("Ground_N", "Ground", tx1, gy0, gx1, gy1, gz))
            specs.append(rect_slab("Ground_S", "Ground", gx0, gy0, tx0, gy1, gz))
            specs.append(rect_slab("Ground_W", "Ground", tx0, gy0, tx1, ty0, gz))
            specs.append(rect_slab("Ground_E", "Ground", tx0, ty1, tx1, gy1, gz))
        else:
            specs.append(rect_slab("Ground_N", "Ground", gx0, ty1, gx1, gy1, gz))
            specs.append(rect_slab("Ground_S", "Ground", gx0, gy0, gx1, ty0, gz))
            specs.append(rect_slab("Ground_W", "Ground", gx0, ty0, tx0, ty1, gz))
            specs.append(rect_slab("Ground_E", "Ground", tx1, ty0, gx1, ty1, gz))
        cz = gz + z_of(int(cut.get("level", -1)))
        specs.append(rect_slab("Cut_Floor", "Cut", tx0, ty0, tx1, ty1, cz))
        # the retaining faces below ground (the layout's cut_wall walls carry the parapet above): the trench's two long edges, standing
        # OUTSIDE the trench (their inner face on its edge: the cut ladders' rung plane 10 cm off it, the full trench width walkable)
        for f in levelgen_ladders.cut_face_walls(cut):
            specs.append(wall_cube(f["id"], "Cut", f["a"], f["b"], cz, gz - cz, f["thickness"], ["cut_face"]))
        for x in cut.get("crossings", []):
            cx_ = [p[0] for p in x["poly"]]
            cy_ = [p[1] for p in x["poly"]]
            specs.append(rect_slab("Cut_%s" % x["id"], "Cut", min(cx_), min(cy_), max(cx_), max(cy_), gz, [x.get("kind", "bridge")]))
        # a "ramp" crossing: the ramp from the bridge slab's far edge down along the trench onto its floor (the V3 variant walks in here)
        for r in levelgen_ladders.cut_ramps(cut, gz):
            specs.append(ramp(r["id"], "Cut", r["a"], r["b"], r["z0"], r["z1"], r["width"], ["cut_ramp", r["crossing"]]))
            report("cut ramp %s: (%.0f, %.0f) at %.0f -> (%.0f, %.0f) at %.0f, %.0f cm wide" % (r["id"], r["a"][0], r["a"][1], r["z0"], r["b"][0], r["b"][1], r["z1"], r["width"]))
    else:
        specs.append(rect_slab("Ground", "Ground", gx0, gy0, gx1, gy1, gz))
    # ---- walls (per level; cut_wall parapets stand on the ground)
    chimneys = {b["id"] for b in layout["buildings"] if b.get("set_piece") == "chimney"}
    frames = {f["id"]: f for f in levelgen_ladders.ladder_frames(layout)}
    gaps = levelgen_ladders.exit_gaps(layout, list(frames.values()))
    for wid, cuts in sorted(gaps.items()):
        report("ladder exit opening in %s: %s" % (wid, ", ".join("%s %.0f..%.0f cm" % (lid, t0, t1) for t0, t1, lid in cuts)))
    for w in levelgen_ladders.split_walls(layout["walls"], gaps):
        if w.get("building") in chimneys:
            continue  # the chimney's cylinder stands for its four 2D walls
        lvl = int(w.get("floor", 0))
        specs.append(wall_cube(w["id"], "Walls/L%d" % lvl, w["a"], w["b"], gz + z_of(lvl), float(w["height"]), float(w["thickness"]), [w.get("kind", "wall")]))
    # ---- buildings: slabs, holes, parapets, stairs
    for b in layout["buildings"]:
        (bx0, by0), (bx1, by1) = b["footprint"]
        floors = int(b["floors"])
        hollow = bool(b.get("hollow"))
        holes_by_floor = {}
        for h in b.get("floor_holes", []):
            holes_by_floor.setdefault(int(h["floor"]), []).append(h["pos"])
        # a stairwell opening in the slab a stair arrives on: above the ramp's last 3.5 m, ENDING exactly at the ramp's top end so the
        # ramp meets the slab edge (an opening past the top end leaves a gap the navmesh does not cross); an external stair lands on
        # the roof over the wall instead
        for s in b.get("stairs", []):
            if s.get("kind") == "stairwell":
                end = s["top"]
                d = (end[0] - s["pos"][0], end[1] - s["pos"][1])
                L = math.hypot(*d) or 1.0
                half = STAIRWELL_HOLE / 2.0
                holes_by_floor.setdefault(int(s["to"]), []).append((end[0] - d[0] / L * half, end[1] - d[1] / L * half, half))
        if not hollow:
            for n in range(1, floors):
                specs.extend(slabs_with_holes("%s_floor%d" % (b["id"], n), "Buildings/%s" % b["id"], bx0, by0, bx1, by1, gz + z_of(n), holes_by_floor.get(n, [])))
        if b.get("roof_walkable") or b.get("enterable"):
            roof_holes = list(b.get("roof_holes", [])) + holes_by_floor.get(floors, [])
            specs.extend(slabs_with_holes("%s_roof" % b["id"], "Buildings/%s" % b["id"], bx0, by0, bx1, by1, gz + z_of(floors), roof_holes))
        elif b.get("set_piece") == "chimney":
            # a factory chimney (game designer 2026-11-02): a closed cylinder, a sight blocker, never climbable (no ladder, no roof)
            specs.append({"kind": "cylinder", "label": "%s_chimney" % b["id"], "folder": "Buildings/%s" % b["id"], "location": [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0],
                          "yaw": 0.0, "pitch": 0.0, "scale": [(bx1 - bx0) / 100.0, (by1 - by0) / 100.0, float(b["height"]) / 100.0], "tags": ["chimney"]})
        else:
            specs.append(cube("%s_block" % b["id"], "Buildings/%s" % b["id"], [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0], [bx1 - bx0, by1 - by0, float(b["height"])]))
        # no parapet: the roof simply ends (the 2D layer's parapet is only the simulator's boundary); an external stair lands over the edge
        for s in b.get("stairs", []):
            z0, z1 = gz + z_of(int(s["from"])), gz + z_of(int(s["to"]))
            if s.get("kind") == "external" and s.get("mid"):
                specs.append(ramp("%s_%s" % (b["id"], s["id"]), "Buildings/%s" % b["id"], s["pos"], s["mid"], z0, z1))
                x0_, y0_, x1_, y1_ = landing_rect(s["pos"], s["mid"], s["top"])
                specs.append(rect_slab("%s_%s_landing" % (b["id"], s["id"]), "Buildings/%s" % b["id"], x0_, y0_, x1_, y1_, z1))
            else:
                specs.append(ramp("%s_%s" % (b["id"], s["id"]), "Buildings/%s" % b["id"], s["pos"], s["top"], z0, z1))
    # ---- ladders with their plinths, in their real frame (levelgen_ladders.ladder_frame: the yaw from pos -> foot, 10 cm off the outer face)
    for ld in layout["ladders"]:
        f = frames[ld["id"]]
        lower = gz + z_of(int(ld.get("level_from", 0)))
        plinth = float(ld.get("plinth_cm", 0.0))
        if f["yaw_fixed"] or abs(f["shift_cm"]) > 0.5:
            report("ladder %s: yaw %.0f (layout %.0f%s), moved %.1f cm out to stand 10 cm off %s" % (
                ld["id"], f["yaw"], f["layout_yaw"], " - MIRRORED in the layout, fixed" if f["yaw_fixed"] else "", f["shift_cm"], f["backing"] or "(no wall)"))
        if plinth > 0.5:
            d = f["out"]
            # under the foot: from 5 cm in front of the rung plane out past the bottom link point (88 cm), 140 cm wide
            specs.append(cube("%s_plinth" % ld["id"], "Ladders", [f["pos"][0] + d[0] * 60.0, f["pos"][1] + d[1] * 60.0, lower + plinth / 2.0], [110.0, 140.0, plinth], f["yaw"]))
        specs.append({"kind": "ladder", "label": ld["id"], "folder": "Ladders", "location": [f["pos"][0], f["pos"][1], lower + plinth], "yaw": f["yaw"],
                      "rungs": int(ld["rungs"]), "tags": ["ladder=%s" % ld["id"], "from=%s" % ld["from"], "to=%s" % ld["to"]]})
    # ---- covers
    for c in layout["covers"]:
        size = c["size"]
        kind = "cylinder" if c.get("kind") == "silo" else "cube"
        specs.append({"kind": kind, "label": c["id"], "folder": "Covers", "location": [c["pos"][0], c["pos"][1], gz + size[2] / 2.0], "yaw": float(c.get("yaw", 0.0)), "pitch": 0.0,
                      "scale": [size[0] / 100.0, size[1] / 100.0, size[2] / 100.0], "tags": [c.get("kind", "cover")]})
    # ---- start, end, routes, spawns, ambush points, patrols, zones
    sx, sy = layout["spawn"]["pos"]
    specs.append({"kind": "player_start", "label": "LevelGen_PlayerStart", "folder": "Base", "location": [sx, sy, gz + START_Z], "yaw": float(layout["spawn"]["yaw"])})
    specs.append({"kind": "target_point", "label": "LevelGenEnd", "folder": "Base", "location": [layout["end"][0], layout["end"][1], gz + 50.0], "yaw": 0.0, "tags": ["LevelGenEnd"]})
    for r in layout["routes"]:
        layers = r.get("layers") or ["ground"] * len(r["points"])
        for n, (px, py) in enumerate(r["points"]):
            lvl = layer_level(layout, layers[n])
            specs.append({"kind": "target_point", "label": "%s_%d" % (r["id"], n), "folder": "Routes/" + r["id"], "location": [px, py, gz + z_of(lvl) + 50.0], "yaw": 0.0,
                          "tags": [ROUTE_TAG, r["id"], "layer=%s" % layers[n], "level=%d" % lvl] + (["mandatory"] if r.get("mandatory") else [])})
    for sp in layout.get("spawn_points", []):
        specs.append({"kind": "target_point", "label": sp["id"], "folder": "Spawns", "location": [sp["pos"][0], sp["pos"][1], gz + 50.0], "yaw": 0.0, "tags": ["LevelGenSpawn", sp.get("kind", "edge")]})
    for ap in layout.get("ambush_points", []):
        specs.append({"kind": "target_point", "label": ap["id"], "folder": "Ambush", "location": [ap["pos"][0], ap["pos"][1], gz + 50.0], "yaw": 0.0, "tags": [AMBUSH_TAG]})
    for pr in layout.get("patrol_routes", []):
        for n, (px, py) in enumerate(pr["points"]):
            specs.append({"kind": "target_point", "label": "%s_%d" % (pr["id"], n), "folder": "Patrols/" + pr["id"], "location": [px, py, gz + 50.0], "yaw": 0.0, "tags": ["LevelGenPatrol", pr["id"]]})
    for z in layout["zones"]:
        cx = sum(p[0] for p in z["poly"]) / len(z["poly"])
        cy = sum(p[1] for p in z["poly"]) / len(z["poly"])
        specs.append({"kind": "text", "label": "Zone_" + z["id"], "folder": "Zones", "text": "%s (%s)" % (z["id"], z.get("set_piece", z["kind"])), "location": [cx, cy, gz + 3.0], "yaw": 0.0})
    # ---- drop nav links through the roof / floor holes (one level down; levelgen_bot_route.drop_links)
    for dl in levelgen_bot_route.drop_links(layout, gz):
        hx, hy = dl["hole"]
        specs.append({"kind": "nav_link", "label": "Drop_%s" % dl["id"], "folder": "Navigation/Drops", "location": [hx, hy, dl["upper_z"]], "yaw": 0.0,
                      "links": [[[sx - hx, sy - hy, 0.0], [0.0, 0.0, dl["lower_z"] - dl["upper_z"]]] for sx, sy in dl["starts"]],
                      "tags": ["LevelGenDrop", dl["via"]]})
    # ---- navmesh bounds: from a level below the cut to 3 m above the highest walkable level (a roof on it needs the agent's
    # height above it inside the volume; a chimney's top stays outside)
    nz0, nz1 = gz + z_of(-1) - 100.0, gz + z_of(max_walkable_level(layout)) + LEVEL_HEIGHT
    specs.append({"kind": "nav_bounds", "label": "LevelGen_NavMeshBounds", "folder": "Navigation", "location": [(gx0 + gx1) / 2.0, (gy0 + gy1) / 2.0, (nz0 + nz1) / 2.0], "yaw": 0.0,
                  "scale": [(gx1 - gx0) / 200.0, (gy1 - gy0) / 200.0, (nz1 - nz0) / 200.0]})
    return specs


def landing_rect(pos, mid, top):
    """The flat landing of an external stair (x0, y0, x1, y1): it STARTS at the ramp's top end `mid` (LANDING_TUCK before it, so
    the ramp's last centimetres slide under it - never the 50 cm step a landing centred on `mid` leaves over the 35 deg ramp,
    above the navmesh agent's 35 cm step), runs STAIR_W on in the ramp's direction and spans from the ramp's outer edge across the
    wall's top to `top` on the roof + half a stair width."""
    ux, uy = mid[0] - pos[0], mid[1] - pos[1]
    L = math.hypot(ux, uy) or 1.0
    ux, uy = ux / L, uy / L
    vx, vy = -uy, ux
    # the side of the ramp line the roof lies on (towards `top`)
    side = 1.0 if (top[0] - mid[0]) * vx + (top[1] - mid[1]) * vy >= 0.0 else -1.0
    reach = abs((top[0] - mid[0]) * vx + (top[1] - mid[1]) * vy) + STAIR_W / 2.0
    pts = []
    for a in (-LANDING_TUCK, STAIR_W):
        for b in (-STAIR_W / 2.0 * side, reach * side):
            pts.append((mid[0] + ux * a + vx * b, mid[1] + uy * a + vy * b))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def max_walkable_level(layout):
    """The highest level anyone walks on: a walkable roof's level, an enterable building's top floor, the ground."""
    best = 0
    for b in layout["buildings"]:
        if b.get("roof_walkable"):
            best = max(best, int(b["floors"]))
        elif b.get("enterable"):
            best = max(best, int(b["floors"]) - 1)
    return best


def layer_level(layout, layer):
    if layer == "ground":
        return 0
    if layer == "cut":
        return int(layout.get("cut", {}).get("level", -1))
    kind, _, rest = layer.partition(":")
    bid, _, n = rest.partition(":")
    b = next((x for x in layout["buildings"] if x["id"] == bid), None)
    if b is None:
        return 0
    return int(b["floors"]) if kind == "roof" else int(n or 1)


def level_json(layout, map_path):
    """The level JSON of the generated map (found by the map name): a combat-lab style skirmish level with the layout's pressure block."""
    leaf = map_path.rsplit("/", 1)[-1]
    data = {
        "$schema": "../Schemas/level_config.schema.json",
        "level_id": leaf,
        "level_name": "Generated %s (seed %d)" % (layout["profile"], int(layout["seed"])),
        "description": "Generated greybox (docs/design/levelgen/LEVEL_GENERATOR.md) from %s seed %d by Scripts/Editor/build_levelgen_map.py; the squad walks the mandatory route (LevelGenRouteSmoke); combat-lab style pressure. ONE small wave only (a valid wave list is required)." % (layout["profile"], int(layout["seed"])),
        "combat_start": "ambush",
        "ambush_fight": "skirmish",
        "susanin_wave": 0,
        "horde_enabled": False,
        "prep_phase_duration": 60,
        "wave_rest_duration": 20,
        "cold_rate_mult": 0,
        "intro_dialogue": False,
        "cheats": {"immortal": True, "infinite_ammo": True},
        "levelgen_layout": "Content/Data/LevelGen/accepted/%s.json" % layout["name"],
        "minimap": {"floors_cm": [z_of(n) for n in range(1, max_walkable_level(layout) + 1)]},
        "waves": [{"wave_index": 1, "name": "Wave 1", "max_simultaneous_enemies": 2,
                   "spawns": [{"enemy_type": "FROSTBITTEN", "count": 2, "spawn_lane": "ANY", "spawn_delay_sec": 1, "initial_delay_sec": 0}],
                   "wave_modifiers": {"enemy_hp_mult": 1, "enemy_damage_mult": 1, "enemy_speed_mult": 1, "cold_drain_mult": 1}}],
        "squad_loadout": {"simulation_mode": "EXPLORE_AND_COLLECT", "preset_tier": "STANDARD"},
    }
    if layout.get("pressure"):
        data["pressure"] = layout["pressure"]
    route = levelgen_bot_route.bot_route(layout)
    if route:
        data["bot_route"] = route
    return data


def write_level_json(layout, map_path):
    os.makedirs(LEVELJSON_DIR, exist_ok=True)
    leaf = map_path.rsplit("/", 1)[-1]
    path = os.path.join(LEVELJSON_DIR, leaf + ".json")
    data = level_json(layout, map_path)
    # The combat lab page tunes "pressure" / "cheats" per map: a rebuild from the SAME layout keeps them.
    if os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                old = json.load(f)
            if old.get("levelgen_layout") == data["levelgen_layout"]:
                for key in ("pressure", "cheats"):
                    if key in old:
                        data[key] = old[key]
                report("level JSON: kept its pressure / cheats (same layout)")
        except (OSError, ValueError) as exc:
            report("REVIEW: the old level JSON %s unreadable (%r): written afresh" % (path, exc))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    report("level JSON -> %s" % path)
    return path


class Placer:
    def __init__(self):
        self.actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.cube = unreal.load_asset(CUBE)
        self.cylinder = unreal.load_asset(CYLINDER)
        self.grid = unreal.load_asset(GRID)
        self.placed = 0

    def spawn(self, cls, spec, tags=None):
        loc = unreal.Vector(*spec["location"])
        rot = unreal.Rotator(pitch=spec.get("pitch", 0.0), yaw=spec.get("yaw", 0.0), roll=0.0)
        actor = self.actors.spawn_actor_from_class(cls, loc, rot)
        actor.set_actor_label(spec["label"])
        actor.set_folder_path(FOLDER + "/" + spec["folder"])
        actor.set_editor_property("tags", list(tags or [TAG]))
        if "scale" in spec:
            actor.set_actor_scale3d(unreal.Vector(*spec["scale"]))
        self.placed += 1
        return actor

    def place(self, spec):
        kind = spec["kind"]
        tags = [TAG] + [str(t) for t in spec.get("tags", [])]
        if kind in ("cube", "cylinder"):
            actor = self.spawn(unreal.StaticMeshActor, spec, tags=tags)
            comp = actor.static_mesh_component
            comp.set_static_mesh(self.cube if kind == "cube" else self.cylinder)
            comp.set_material(0, self.grid)
        elif kind == "player_start":
            self.spawn(unreal.PlayerStart, spec)
        elif kind == "nav_bounds":
            self.spawn(unreal.NavMeshBoundsVolume, spec)
        elif kind == "target_point":
            self.spawn(unreal.TargetPoint, spec, tags=tags)
        elif kind == "text":
            actor = self.spawn(unreal.TextRenderActor, spec)
            actor.set_actor_rotation(unreal.Rotator(pitch=90.0, yaw=0.0, roll=0.0), False)
            try:
                comp = actor.get_component_by_class(unreal.TextRenderComponent)
                comp.set_text(spec["text"])
                comp.set_editor_property("world_size", 60.0)
                comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
            except Exception as exc:
                report("REVIEW: zone label %s: %r" % (spec["label"], exc))
        elif kind == "nav_link":
            actor = self.spawn(unreal.NavLinkProxy, spec, tags=tags)
            links = []
            for left, right in spec["links"]:
                link = unreal.NavigationLink()
                link.set_editor_property("left", unreal.Vector(*left))
                link.set_editor_property("right", unreal.Vector(*right))
                link.set_editor_property("direction", unreal.NavLinkDirection.LEFT_TO_RIGHT)
                links.append(link)
            actor.set_editor_property("point_links", links)
            report("  %s: %d one-way drop links, %.0f cm down" % (spec["label"], len(links), -spec["links"][0][1][2]))
        elif kind == "ladder":
            actor = self.spawn(unreal.LadderActor, spec, tags=tags)
            actor.set_editor_property("height", spec["rungs"] * PITCH)
            actor.set_editor_property("wall_bracket_count", 3)
            actor.set_editor_property("wall_search_cm", 25.0)
            report("  %s: %d rungs, climbable %s, wall %s" % (spec["label"], actor.get_rung_count(), actor.is_climbable(), actor.has_wall()))
        else:
            report("REVIEW: unknown spec kind %s" % kind)


def ensure_lighting(actors):
    have = [a for a in actors.get_all_level_actors() if ENV_TAG in [str(t) for t in a.tags]]
    if have:
        report("lighting: %d LevelGen_Env actors kept" % len(have))
        return
    p = Placer()
    for cls, spec in [
        (unreal.DirectionalLight, {"label": "Gen_Sun", "folder": "Lighting", "location": [0, 0, 1000], "yaw": -30.0}),
        (unreal.SkyLight, {"label": "Gen_SkyLight", "folder": "Lighting", "location": [0, 0, 800], "yaw": 0.0}),
        (unreal.SkyAtmosphere, {"label": "Gen_SkyAtmosphere", "folder": "Lighting", "location": [0, 0, 0], "yaw": 0.0}),
        (unreal.ExponentialHeightFog, {"label": "Gen_HeightFog", "folder": "Lighting", "location": [0, 0, 0], "yaw": 0.0}),
    ]:
        actor = p.spawn(cls, spec, tags=[ENV_TAG])
        if cls is unreal.DirectionalLight:
            actor.set_actor_rotation(unreal.Rotator(pitch=-50.0, yaw=-30.0, roll=0.0), False)
        if cls is unreal.SkyLight:
            try:
                actor.light_component.set_editor_property("real_time_capture", True)
            except Exception as exc:
                report("REVIEW: sky light: %r" % exc)
    report("lighting: placed %d LevelGen_Env actors" % p.placed)


def build():
    layout = load_layout(LAYOUT_FILE)
    target = TARGET_MAP or map_name_for(layout)
    specs = plan(layout)
    lib = unreal.EditorAssetLibrary
    level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if os.environ.get("CODEX_LEVELGEN_FRESH") == "1" and lib.does_asset_exist(target):
        report("fresh: deleting %s -> %s" % (target, lib.delete_asset(target)))
    new_world = None
    if lib.does_asset_exist(target):
        if not level_editor.load_level(target):
            report("ERROR: could not load %s" % target)
            return
        report("loaded %s" % target)
    else:
        new_world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        report("new blank map for %s" % target)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    old = [a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    for a in old:
        actors.destroy_actor(a)
    report("removed %d actors of the previous run (tag %s)" % (len(old), TAG))
    ensure_lighting(actors)
    p = Placer()
    for spec in specs:
        p.place(spec)
    report("placed %d actors from %s (%d walls, %d buildings, %d ladders, %d covers, %d routes)" % (
        p.placed, os.path.basename(LAYOUT_FILE), len(layout["walls"]), len(layout["buildings"]), len(layout["ladders"]), len(layout["covers"]), len(layout["routes"])))
    navmeshes = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in navmeshes:
        actors.destroy_actor(a)
    report("destroyed %d RecastNavMesh actors - bake with Scripts/bake_navmesh.ps1 -Map %s" % (len(navmeshes), target.rsplit("/", 1)[-1]))
    saved = unreal.EditorLoadingAndSavingUtils.save_map(new_world, target) if new_world else level_editor.save_current_level()
    report("saved %s: %s" % (target, saved))
    write_level_json(layout, target)


def flush_report(target):
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    os.makedirs(saved + "Logs", exist_ok=True)
    leaf = target.rsplit("/", 1)[-1]
    with open(saved + "Logs/" + (leaf[2:] if leaf.startswith("L_") else leaf) + "Layout.txt", "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def main():
    if unreal is None:
        if len(sys.argv) >= 2 and sys.argv[1] == "plan":
            layout = load_layout(LAYOUT_FILE)
            target = TARGET_MAP or map_name_for(layout)
            specs = plan(layout)
            write_level_json(layout, target)
            print(json.dumps({"map": target, "actors": len(specs), "kinds": {k: sum(1 for s in specs if s["kind"] == k) for k in sorted({s["kind"] for s in specs})}}, indent=1))
            return
        print(__doc__)
        return
    target = TARGET_MAP
    try:
        layout = load_layout(LAYOUT_FILE)
        target = target or map_name_for(layout)
        report("build_levelgen_map.py layout=%s target=%s (%s)" % (LAYOUT_FILE, target, datetime.datetime.now().isoformat()))
        build()
    except Exception as exc:
        report("ERROR: %r" % exc)
    flush_report(target or "/Game/Maps/Generated/L_Gen_Unknown")


main()
