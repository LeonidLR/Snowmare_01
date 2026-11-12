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
    waypoint's floor height and the drop legs marked); a rebuild KEEPS the existing level JSON's "pressure" / "cheats" blocks;
    "drop_links": true (2026-11-03): the runtime drop nav links off the open edges (UDropLinkSubsystem, LEVEL_GENERATOR.md 8.6)
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
    name = str(layout.get("name", ""))
    if name.startswith("L_Gen_"):
        return "/Game/Maps/Generated/%s" % name
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


def slabs_with_holes(label, folder, x0, y0, x1, y1, top_z, holes, tags=None):
    """A slab with rectangular or square holes cut out.
    A hole is (x, y), (x, y, half), or an explicit rect (hx0, hy0, hx1, hy1).
    Legacy single-strip mode is preserved verbatim when tags is None and all holes are non-overlapping tuples <= 3 elements."""
    has_rects = any(len(h) >= 4 for h in holes)
    if not has_rects and tags is None:
        cuts = sorted(holes, key=lambda h: h[0])
        bounds = []
        for h in cuts:
            half = h[2] if len(h) > 2 else 120.0
            bounds.append((max(x0, h[0] - half), min(x1, h[0] + half)))
        overlapping = any(bounds[i][1] > bounds[i + 1][0] for i in range(len(bounds) - 1))
        if not overlapping:
            out = []
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

    rects = []
    for h in holes:
        if len(h) >= 4:
            rx0, ry0, rx1, ry1 = float(h[0]), float(h[1]), float(h[2]), float(h[3])
        else:
            half = float(h[2]) if len(h) > 2 else 120.0
            rx0, ry0, rx1, ry1 = float(h[0]) - half, float(h[1]) - half, float(h[0]) + half, float(h[1]) + half
        rx0, rx1 = max(x0, min(rx0, rx1)), min(x1, max(rx0, rx1))
        ry0, ry1 = max(y0, min(ry0, ry1)), min(y1, max(ry0, ry1))
        if rx1 - rx0 >= 5.0 and ry1 - ry0 >= 5.0:
            rects.append((rx0, ry0, rx1, ry1))
    if not rects:
        return [rect_slab(label, folder, x0, y0, x1, y1, top_z, tags)]

    def dedup(vals):
        s = sorted(vals)
        res = []
        for v in s:
            if not res or abs(v - res[-1]) > 0.5:
                res.append(v)
        return res

    xs = dedup([x0, x1] + [v for r in rects for v in (r[0], r[2])])
    ys = dedup([y0, y1] + [v for r in rects for v in (r[1], r[3])])
    out = []
    idx = 0
    for i in range(len(xs) - 1):
        ax, bx = xs[i], xs[i + 1]
        if bx - ax < 5.0:
            continue
        mx = (ax + bx) * 0.5
        run_y0 = None
        run_y1 = None
        for j in range(len(ys) - 1):
            ay, by = ys[j], ys[j + 1]
            if by - ay < 5.0:
                continue
            my = (ay + by) * 0.5
            blocked = any(rx0 < mx < rx1 and ry0 < my < ry1 for rx0, ry0, rx1, ry1 in rects)
            if not blocked:
                if run_y0 is None:
                    run_y0 = ay
                run_y1 = by
            else:
                if run_y0 is not None:
                    out.append(rect_slab("%s_%d" % (label, idx), folder, ax, run_y0, bx, run_y1, top_z, tags))
                    idx += 1
                    run_y0 = None
                    run_y1 = None
        if run_y0 is not None:
            out.append(rect_slab("%s_%d" % (label, idx), folder, ax, run_y0, bx, run_y1, top_z, tags))
            idx += 1
    return out


def ramp(label, folder, a, b, z0, z1, width=STAIR_W, tags=None):
    """A sloped cube from a (at z0) to b (at z1): a walkable stair stand-in (<= 44 deg)."""
    run = math.hypot(b[0] - a[0], b[1] - a[1])
    rise = z1 - z0
    length = math.hypot(run, rise)
    pitch = math.degrees(math.atan2(rise, run))  # UE pitch: positive = nose UP (the local +X end rises towards b)
    return cube(label, folder, [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, (z0 + z1) / 2.0 - SLAB / 2.0], [length, width, SLAB],
                math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])), tags, pitch)


def _has_combat_building_features(layout):
    if layout.get("mantle_edges"):
        return True
    for b in layout.get("buildings", []):
        if any(b.get(k) for k in ("openings", "floors_detail", "hatches", "parapets", "mantle_edges", "partitions")):
            return True
    return False


def _slice_wall_with_openings(label_prefix, folder, a, b, z0, thickness, floor_idx, building_id, wall_kind, openings, base_tags, exit_intervals=None):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = math.hypot(dx, dy)
    if L < 2.0:
        return []
    ux, uy = dx / L, dy / L
    nx, ny = -uy, ux

    def pt(t):
        return [a[0] + ux * t, a[1] + uy * t]

    cutouts = []
    for t0, t1, lid in (exit_intervals or []):
        c0, c1 = max(0.0, float(t0)), min(L, float(t1))
        if c1 - c0 >= 2.0:
            cutouts.append((c0, c1, {"kind": "_ladder_exit", "id": lid}))

    for op in openings:
        if int(op.get("floor", 0)) != floor_idx:
            continue
        kind = op.get("kind")
        if kind not in ("window", "loophole", "door"):
            continue
        px, py = op["pos"]
        perp = abs((px - a[0]) * nx + (py - a[1]) * ny)
        if perp > 20.0:
            continue
        t = (px - a[0]) * ux + (py - a[1]) * uy
        hw = float(op.get("width", 120.0)) * 0.5
        c0, c1 = max(0.0, t - hw), min(L, t + hw)
        if c1 - c0 >= 2.0:
            cutouts.append((c0, c1, op))

    cutouts.sort(key=lambda item: item[0])
    out = []
    cur = 0.0
    for idx, (c0, c1, op) in enumerate(cutouts):
        if c0 > cur + 2.0:
            out.append(wall_cube("%s_s%d" % (label_prefix, idx), folder, pt(cur), pt(c0), z0, LEVEL_HEIGHT, thickness, list(base_tags)))
        op_kind = op.get("kind")
        if op_kind != "_ladder_exit" and c1 - c0 >= 2.0:
            sill_h = float(op.get("sill_height", 0.0 if op_kind == "door" else 90.0))
            top_h = float(op.get("top_height", 210.0))
            if op_kind == "loophole":
                top_h = max(top_h, 175.0)  # Standing EyeHeight (160 cm) passes unobstructed
            if sill_h >= 2.0:
                if op_kind == "loophole":
                    sill_tags = ["building=" + building_id, "floor=%d" % floor_idx, "loophole_sill"]
                else:
                    sill_kind_tag = "window_sill" if floor_idx == 0 else "mantle_sill"
                    sill_tags = ["building=" + building_id, "floor=%d" % floor_idx, sill_kind_tag, "CodexMantleEdge"]
                out.append(wall_cube("%s_o%d_sill" % (label_prefix, idx), folder, pt(c0), pt(c1), z0, sill_h, thickness, sill_tags))
            lintel_h = LEVEL_HEIGHT - top_h
            if lintel_h >= 2.0:
                lintel_tags = ["building=" + building_id, "floor=%d" % floor_idx, "opening_lintel"]
                if op_kind == "door":
                    lintel_tags.append("door_lintel")
                out.append(wall_cube("%s_o%d_lintel" % (label_prefix, idx), folder, pt(c0), pt(c1), z0 + top_h, lintel_h, thickness, lintel_tags))
        cur = max(cur, c1)
    if L > cur + 2.0:
        out.append(wall_cube("%s_tail" % label_prefix if cutouts else label_prefix, folder, pt(cur), pt(L), z0, LEVEL_HEIGHT, thickness, list(base_tags)))
    return out


def _collinear_gaps(segs, building_id, floor_idx):
    """Find axis-aligned collinear gaps between wall segments (door openings pre-split in walls[])."""
    groups = {}
    for w in segs:
        ax, ay = w["a"]
        bx, by = w["b"]
        if ax != bx and ay != by:
            continue
        along_y = (ax == bx)
        fixed = ax if along_y else ay
        t0, t1 = (min(ay, by), max(ay, by)) if along_y else (min(ax, bx), max(ax, bx))
        prefix = w["id"].rsplit("_", 1)[0]
        groups.setdefault((prefix, along_y, fixed), []).append((t0, t1, float(w["thickness"])))
    gaps_out = []
    for (prefix, along_y, fixed), spans in groups.items():
        spans.sort()
        for i in range(len(spans) - 1):
            g0, g1 = spans[i][1], spans[i + 1][0]
            if 40.0 <= g1 - g0 <= 300.0:
                a = [fixed, g0] if along_y else [g0, fixed]
                b = [fixed, g1] if along_y else [g1, fixed]
                gaps_out.append({"prefix": prefix, "along_y": along_y, "fixed": fixed, "g0": g0, "g1": g1, "a": a, "b": b, "thickness": spans[i][2], "building": building_id, "floor": floor_idx})
    return gaps_out


def _merged_building_edges(ext_walls):
    """Merge collinear ground-split building perimeter pieces into continuous edges for upper storeys (f >= 1)."""
    groups = {}
    for w in ext_walls:
        ax, ay = w["a"]
        bx, by = w["b"]
        if ax != bx and ay != by:
            groups[("free", w["id"])] = [w]
            continue
        along_y = (ax == bx)
        fixed = ax if along_y else ay
        prefix = w["id"].rsplit("_", 1)[0]
        groups.setdefault((prefix, along_y, fixed), []).append(w)
    merged = []
    for key, ws in groups.items():
        if key[0] == "free":
            merged.extend(ws)
            continue
        prefix, along_y, fixed = key
        pts = [coord for w in ws for coord in ([w["a"][1], w["b"][1]] if along_y else [w["a"][0], w["b"][0]])]
        lo, hi = min(pts), max(pts)
        a = [fixed, lo] if along_y else [lo, fixed]
        b = [fixed, hi] if along_y else [hi, fixed]
        merged.append({"id": prefix, "a": a, "b": b, "thickness": ws[0]["thickness"], "kind": ws[0].get("kind", "building"), "building": ws[0].get("building"), "height": ws[0]["height"]})
    return merged


def _intersect_segment_to_edge(p0, p1, e0, e1, half_w):
    """If segment p0->p1 crosses axis-aligned or general edge e0->e1, return (t0, t1) cutout along e0->e1."""
    ex, ey = e1[0] - e0[0], e1[1] - e0[1]
    L = math.hypot(ex, ey)
    if L < 1.0:
        return None
    ux, uy = ex / L, ey / L
    nx, ny = -uy, ux
    d0 = (p0[0] - e0[0]) * nx + (p0[1] - e0[1]) * ny
    d1 = (p1[0] - e0[0]) * nx + (p1[1] - e0[1]) * ny
    if (d0 > 5.0 and d1 > 5.0) or (d0 < -5.0 and d1 < -5.0):
        return None
    denom = d0 - d1
    s = 0.5 if abs(denom) < 1e-6 else max(0.0, min(1.0, d0 / denom))
    ix = p0[0] + (p1[0] - p0[0]) * s
    iy = p0[1] + (p1[1] - p0[1]) * s
    t = (ix - e0[0]) * ux + (iy - e0[1]) * uy
    if -half_w <= t <= L + half_w:
        return (max(0.0, t - half_w), min(L, t + half_w), "stair_exit")
    return None


def _hatch_rim_specs(b, h, gz, roof_z, frames):
    """3-sided visual hatch curb leaving the ladder approach/exit side clear."""
    hx, hy = h["pos"]
    sx, sy = h.get("size", [100.0, 100.0])
    hx0, hx1 = hx - sx * 0.5, hx + sx * 0.5
    hy0, hy1 = hy - sy * 0.5, hy + sy * 0.5
    skip_side = None
    lid = h.get("ladder_id")
    f = frames.get(lid) if lid else None
    if f is not None:
        ox, oy = -f["out"][0], -f["out"][1]  # exit vector points along -out (towards top)
        if abs(ox) >= abs(oy):
            skip_side = "E" if ox > 0 else "W"
        else:
            skip_side = "S" if oy > 0 else "N"
    sides = [
        ("W", [hx0 - 4.0, hy0 - 4.0], [hx0 - 4.0, hy1 + 4.0]),
        ("E", [hx1 + 4.0, hy0 - 4.0], [hx1 + 4.0, hy1 + 4.0]),
        ("N", [hx0 - 4.0, hy0 - 4.0], [hx1 + 4.0, hy0 - 4.0]),
        ("S", [hx0 - 4.0, hy1 + 4.0], [hx1 + 4.0, hy1 + 4.0]),
    ]
    out = []
    for side, p0, p1 in sides:
        if side == skip_side:
            continue
        out.append(wall_cube("%s_rim_%s" % (h["id"], side), "Buildings/%s" % b["id"], p0, p1, roof_z, 12.0, 8.0, ["hatch_rim", "building=" + b["id"]]))
    return out


def _point_in_cover(px, py, c, margin=12.0):
    cx, cy = c["pos"]
    rad = math.radians(float(c.get("yaw", 0.0)))
    dx, dy = px - cx, py - cy
    lx = dx * math.cos(rad) + dy * math.sin(rad)
    ly = -dx * math.sin(rad) + dy * math.cos(rad)
    return abs(lx) <= c["size"][0] * 0.5 + margin and abs(ly) <= c["size"][1] * 0.5 + margin


def _walls_and_buildings_legacy(layout, gz, chimneys, frames, gaps):
    specs = []
    for w in levelgen_ladders.split_walls(layout["walls"], gaps):
        if w.get("building") in chimneys:
            continue  # the chimney's cylinder stands for its four 2D walls
        lvl = int(w.get("floor", 0))
        specs.append(wall_cube(w["id"], "Walls/L%d" % lvl, w["a"], w["b"], gz + z_of(lvl), float(w["height"]), float(w["thickness"]), [w.get("kind", "wall")]))
    for b in layout["buildings"]:
        (bx0, by0), (bx1, by1) = b["footprint"]
        floors = int(b["floors"])
        hollow = bool(b.get("hollow"))
        holes_by_floor = {}
        for h in b.get("floor_holes", []):
            holes_by_floor.setdefault(int(h["floor"]), []).append(h["pos"])
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
            specs.append({"kind": "cylinder", "label": "%s_chimney" % b["id"], "folder": "Buildings/%s" % b["id"], "location": [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0],
                          "yaw": 0.0, "pitch": 0.0, "scale": [(bx1 - bx0) / 100.0, (by1 - by0) / 100.0, float(b["height"]) / 100.0], "tags": ["chimney"]})
        else:
            specs.append(cube("%s_block" % b["id"], "Buildings/%s" % b["id"], [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0], [bx1 - bx0, by1 - by0, float(b["height"])]))
        for s in b.get("stairs", []):
            z0, z1 = gz + z_of(int(s["from"])), gz + z_of(int(s["to"]))
            if s.get("kind") == "external" and s.get("mid"):
                specs.append(ramp("%s_%s" % (b["id"], s["id"]), "Buildings/%s" % b["id"], s["pos"], s["mid"], z0, z1))
                x0_, y0_, x1_, y1_ = landing_rect(s["pos"], s["mid"], s["top"])
                specs.append(rect_slab("%s_%s_landing" % (b["id"], s["id"]), "Buildings/%s" % b["id"], x0_, y0_, x1_, y1_, z1))
            else:
                specs.append(ramp("%s_%s" % (b["id"], s["id"]), "Buildings/%s" % b["id"], s["pos"], s["top"], z0, z1))
    return specs


def _walls_and_buildings_extended(layout, gz, chimneys, frames, gaps):
    specs = []
    b_by_id = {b["id"]: b for b in layout["buildings"]}
    split = [w for w in levelgen_ladders.split_walls(layout["walls"], gaps) if w.get("building") not in chimneys]

    # Non-building and interior partition walls
    interior_by_building_floor = {}
    ext_by_building = {}
    for w in split:
        bid = w.get("building")
        lvl = int(w.get("floor", 0))
        if not bid or bid not in b_by_id:
            specs.append(wall_cube(w["id"], "Walls/L%d" % lvl, w["a"], w["b"], gz + z_of(lvl), float(w["height"]), float(w["thickness"]), [w.get("kind", "wall")]))
        elif w.get("kind") == "interior":
            specs.append(wall_cube(w["id"], "Buildings/%s/L%d" % (bid, lvl), w["a"], w["b"], gz + z_of(lvl), float(w["height"]), float(w["thickness"]), ["interior", "building=" + bid, "floor=%d" % lvl]))
            interior_by_building_floor.setdefault((bid, lvl), []).append(w)
        else:
            ext_by_building.setdefault(bid, []).append(w)

    # Interior door lintels (210..300 cm) matching pre-split interior partition door gaps
    for (bid, lvl), segs in sorted(interior_by_building_floor.items()):
        b = b_by_id[bid]
        part_doors = [op for p in b.get("partitions", []) for op in p.get("openings", []) if op.get("kind") == "door" and int(op.get("floor", p.get("floor", 0))) == lvl]
        for idx, gap in enumerate(_collinear_gaps(segs, bid, lvl)):
            gx = 0.5 * (gap["a"][0] + gap["b"][0])
            gy = 0.5 * (gap["a"][1] + gap["b"][1])
            match = next((op for op in part_doors if math.hypot(op["pos"][0] - gx, op["pos"][1] - gy) <= 30.0), None)
            if match:
                top_h = float(match.get("top_height", 210.0))
                if LEVEL_HEIGHT - top_h >= 2.0:
                    specs.append(wall_cube("%s_ilintel_%d_%d" % (bid, lvl, idx), "Buildings/%s/L%d" % (bid, lvl), gap["a"], gap["b"], gz + z_of(lvl) + top_h, LEVEL_HEIGHT - top_h, gap["thickness"], ["interior", "building=" + bid, "floor=%d" % lvl, "door_lintel", "opening_lintel"]))

    # Building perimeters sliced by floor & openings, slabs, hatches, parapets, stairs
    for b in layout["buildings"]:
        bid = b["id"]
        (bx0, by0), (bx1, by1) = b["footprint"]
        floors = int(b["floors"])
        hollow = bool(b.get("hollow"))
        openings = b.get("openings", [])
        raw_ext = ext_by_building.get(bid, [])

        # Floor 0 perimeter (pre-split around ground doors in layout["walls"])
        f0_walls = [w for w in raw_ext if int(w.get("floor", 0)) == 0]
        for w in f0_walls:
            if not openings:
                specs.append(wall_cube(w["id"], "Buildings/%s/L0" % bid, w["a"], w["b"], gz, min(LEVEL_HEIGHT, float(w["height"])), float(w["thickness"]), [w.get("kind", "building"), "building=" + bid, "floor=0"]))
            else:
                specs.extend(_slice_wall_with_openings(w["id"], "Buildings/%s/L0" % bid, w["a"], w["b"], gz, float(w["thickness"]), 0, bid, w.get("kind", "building"), openings, [w.get("kind", "building"), "building=" + bid, "floor=0"]))

        # Ground exterior door lintels (210..300 cm)
        ext_doors_f0 = [op for op in openings if op.get("kind") == "door" and int(op.get("floor", 0)) == 0]
        for idx, gap in enumerate(_collinear_gaps(f0_walls, bid, 0)):
            gx = 0.5 * (gap["a"][0] + gap["b"][0])
            gy = 0.5 * (gap["a"][1] + gap["b"][1])
            match = next((op for op in ext_doors_f0 if math.hypot(op["pos"][0] - gx, op["pos"][1] - gy) <= 30.0), None)
            if match:
                top_h = float(match.get("top_height", 210.0))
                if LEVEL_HEIGHT - top_h >= 2.0:
                    specs.append(wall_cube("%s_dlintel_0_%d" % (bid, idx), "Buildings/%s/L0" % bid, gap["a"], gap["b"], gz + top_h, LEVEL_HEIGHT - top_h, gap["thickness"], ["building=" + bid, "floor=0", "door_lintel", "opening_lintel"]))

        # Upper storeys (f >= 1): reconstruct unified perimeter edges so upper windows never split over floor-0 doors
        if floors > 1 and raw_ext:
            raw_b_ext = [w for w in layout["walls"] if w.get("building") == bid and w.get("kind") != "interior"]
            unified_edges = _merged_building_edges(raw_b_ext)
            for f in range(1, floors):
                z_f = gz + z_of(f)
                # Apply ladder exit gaps targeting this upper floor if any exist
                dummy_walls = [dict(e, id="%s_f%d" % (e["id"], f), floor=f, height=LEVEL_HEIGHT) for e in unified_edges]
                f_gaps = levelgen_ladders.exit_gaps(dict(layout, walls=dummy_walls), list(frames.values()))
                for ew in levelgen_ladders.split_walls(dummy_walls, f_gaps):
                    specs.extend(_slice_wall_with_openings(ew["id"], "Buildings/%s/L%d" % (bid, f), ew["a"], ew["b"], z_f, float(ew["thickness"]), f, bid, ew.get("kind", "building"), openings, [ew.get("kind", "building"), "building=" + bid, "floor=%d" % f]))

        # Slabs, floor_holes, stairwell holes, floors_detail[].gaps, hatches
        holes_by_floor = {}
        for h in b.get("floor_holes", []):
            holes_by_floor.setdefault(int(h["floor"]), []).append(h["pos"])
        for s in b.get("stairs", []):
            if s.get("kind") == "stairwell":
                end = s["top"]
                d = (end[0] - s["pos"][0], end[1] - s["pos"][1])
                L = math.hypot(*d) or 1.0
                half = STAIRWELL_HOLE / 2.0
                holes_by_floor.setdefault(int(s["to"]), []).append((end[0] - d[0] / L * half, end[1] - d[1] / L * half, half))
        for fd in b.get("floors_detail", []):
            fl = int(fd["floor"])
            for g in fd.get("gaps", []):
                px = [p[0] for p in g["poly"]]
                py = [p[1] for p in g["poly"]]
                holes_by_floor.setdefault(fl, []).append((min(px), min(py), max(px), max(py)))
        for h in b.get("hatches", []):
            hx, hy = h["pos"]
            sx, sy = h.get("size", [100.0, 100.0])
            h_rect = (hx - sx * 0.5, hy - sy * 0.5, hx + sx * 0.5, hy + sy * 0.5)
            h_floor = int(h.get("floor", 0))
            for fl in range(h_floor + 1, floors + 1):
                holes_by_floor.setdefault(fl, []).append(h_rect)

        if not hollow:
            for n in range(1, floors):
                specs.extend(slabs_with_holes("%s_floor%d" % (bid, n), "Buildings/%s" % bid, bx0, by0, bx1, by1, gz + z_of(n), holes_by_floor.get(n, []), tags=["floor_slab", "building=" + bid, "floor=%d" % n]))
        if b.get("roof_walkable") or b.get("enterable"):
            roof_holes = list(b.get("roof_holes", [])) + holes_by_floor.get(floors, [])
            roof_z = gz + z_of(floors)
            specs.extend(slabs_with_holes("%s_roof" % bid, "Buildings/%s" % bid, bx0, by0, bx1, by1, roof_z, roof_holes, tags=["roof_slab", "building=" + bid, "floor=%d" % floors]))
            for h in b.get("hatches", []):
                specs.extend(_hatch_rim_specs(b, h, gz, roof_z, frames))
        elif b.get("set_piece") == "chimney":
            specs.append({"kind": "cylinder", "label": "%s_chimney" % bid, "folder": "Buildings/%s" % bid, "location": [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0],
                          "yaw": 0.0, "pitch": 0.0, "scale": [(bx1 - bx0) / 100.0, (by1 - by0) / 100.0, float(b["height"]) / 100.0], "tags": ["chimney"]})
        else:
            specs.append(cube("%s_block" % bid, "Buildings/%s" % bid, [(bx0 + bx1) / 2.0, (by0 + by1) / 2.0, gz + float(b["height"]) / 2.0], [bx1 - bx0, by1 - by0, float(b["height"])]))

        # Roof parapets: cut ladder exit_gaps + external stair arrivals before spawning
        parapets = b.get("parapets", [])
        if parapets:
            p_walls = [{"id": p["wall_id"], "a": p["start"], "b": p["end"], "floor": floors, "height": float(p.get("height_cm", PARAPET_H)), "thickness": float(p.get("thickness_cm", 20.0)), "kind": "parapet", "building": bid} for p in parapets]
            p_gaps = levelgen_ladders.exit_gaps(dict(layout, walls=p_walls), list(frames.values()))
            for s in b.get("stairs", []):
                if s.get("kind") == "external" and int(s.get("to", 0)) == floors:
                    approach = s.get("mid") or s["pos"]
                    for pw in p_walls:
                        hit = _intersect_segment_to_edge(approach, s["top"], pw["a"], pw["b"], STAIR_W * 0.5 + 15.0)
                        if hit:
                            p_gaps.setdefault(pw["id"], []).append(hit)
            for pw in levelgen_ladders.split_walls(p_walls, p_gaps):
                specs.append(wall_cube(pw["id"], "Buildings/%s/Parapets" % bid, pw["a"], pw["b"], gz + z_of(floors), float(pw["height"]), float(pw["thickness"]), ["parapet", "CodexMantleEdge", "building=" + bid, "floor=%d" % floors]))

        for s in b.get("stairs", []):
            z0, z1 = gz + z_of(int(s["from"])), gz + z_of(int(s["to"]))
            if s.get("kind") == "external" and s.get("mid"):
                specs.append(ramp("%s_%s" % (bid, s["id"]), "Buildings/%s" % bid, s["pos"], s["mid"], z0, z1))
                x0_, y0_, x1_, y1_ = landing_rect(s["pos"], s["mid"], s["top"])
                specs.append(rect_slab("%s_%s_landing" % (bid, s["id"]), "Buildings/%s" % bid, x0_, y0_, x1_, y1_, z1))
            else:
                specs.append(ramp("%s_%s" % (bid, s["id"]), "Buildings/%s" % bid, s["pos"], s["top"], z0, z1))
    return specs


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
    # ---- walls & buildings
    chimneys = {b["id"] for b in layout["buildings"] if b.get("set_piece") == "chimney"}
    frames = {f["id"]: f for f in levelgen_ladders.ladder_frames(layout)}
    gaps = levelgen_ladders.exit_gaps(layout, list(frames.values()))
    for wid, cuts in sorted(gaps.items()):
        report("ladder exit opening in %s: %s" % (wid, ", ".join("%s %.0f..%.0f cm" % (lid, t0, t1) for t0, t1, lid in cuts)))
    extended = _has_combat_building_features(layout)
    if extended:
        specs.extend(_walls_and_buildings_extended(layout, gz, chimneys, frames, gaps))
    else:
        specs.extend(_walls_and_buildings_legacy(layout, gz, chimneys, frames, gaps))
    # ---- ladders with their plinths, in their real frame (levelgen_ladders.ladder_frame: the yaw from pos -> foot, 10 cm off the outer face)
    hatch_ladders = {h.get("ladder_id") for b in layout["buildings"] for h in b.get("hatches", []) if h.get("ladder_id")}
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
        ld_spec = {"kind": "ladder", "label": ld["id"], "folder": "Ladders", "location": [f["pos"][0], f["pos"][1], lower + plinth], "yaw": f["yaw"],
                   "rungs": int(ld["rungs"]), "tags": ["ladder=%s" % ld["id"], "from=%s" % ld["from"], "to=%s" % ld["to"]]}
        if extended and f["backing"] is None:
            ld_spec["wall_bracket_count"] = -1
        if extended and (ld["id"] in hatch_ladders or (f["backing"] is None and ld.get("top"))):
            dist_top = math.hypot(ld["top"][0] - f["pos"][0], ld["top"][1] - f["pos"][1]) if ld.get("top") else 40.0
            ld_spec["top_exit_standoff"] = max(40.0, dist_top)
        specs.append(ld_spec)
    # ---- covers
    mantle_cover_pts = [
        (0.5 * (m["start"][0] + m["end"][0]), 0.5 * (m["start"][1] + m["end"][1]))
        for m in layout.get("mantle_edges", []) if m.get("kind") in ("crate", "obstacle", "cover", "wall")
    ] if extended else []
    for c in layout["covers"]:
        size = c["size"]
        kind = "cylinder" if c.get("kind") == "silo" else "cube"
        c_tags = [c.get("kind", "cover")]
        if extended and any(_point_in_cover(mx, my, c) for mx, my in mantle_cover_pts):
            c_tags.append("CodexMantleEdge")
        specs.append({"kind": kind, "label": c["id"], "folder": "Covers", "location": [c["pos"][0], c["pos"][1], gz + size[2] / 2.0], "yaw": float(c.get("yaw", 0.0)), "pitch": 0.0,
                      "scale": [size[0] / 100.0, size[1] / 100.0, size[2] / 100.0], "tags": c_tags})
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
        # falls and drops (2026-11-03): the runtime drop nav links off the roofs / bridges / into the cut (UDropLinkSubsystem)
        "drop_links": True,
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
            actor.set_editor_property("wall_bracket_count", int(spec.get("wall_bracket_count", 3)))
            actor.set_editor_property("wall_search_cm", 25.0)
            if "top_exit_standoff" in spec:
                actor.set_editor_property("top_exit_standoff", float(spec["top_exit_standoff"]))
            actor.set_editor_property("height", spec["rungs"] * PITCH)
            report("  %s: %d rungs, climbable %s, wall %s, top_exit_standoff %.0f" % (
                spec["label"], actor.get_rung_count(), actor.is_climbable(), actor.has_wall(), actor.get_editor_property("top_exit_standoff")))
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
    dirty_content = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
    report("dirty_content_packages_before_save (%d): %s" % (len(dirty_content), dirty_content))
    saved = unreal.EditorLoadingAndSavingUtils.save_map(new_world, target) if new_world else level_editor.save_current_level()
    report("saved %s: %s (save_scope=map_only)" % (target, saved))
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


if unreal is not None or __name__ == "__main__":
    main()
