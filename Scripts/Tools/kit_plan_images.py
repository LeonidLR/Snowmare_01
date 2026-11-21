"""Floor-plan images and room tables of the Soviet Houses kit buildings.

Pure Python (numpy, scipy, Pillow, matplotlib). No Unreal. Inputs, both in Saved/KitDump:

- kit_interiors.json from Scripts/Editor/dump_kit_interiors.py: per building a ground and a
  typical upper storey, each with 2D wall segments cut at floor + 1.0 m, the slab footprint
  triangles, and the AABB of every other placed mesh in the storey band.
- kit_plans.json from Scripts/Tools/kit_plan_report.py: series guess and street / court edges.

Outputs (default Saved/KitDump of the agents worktree):

- plans/Building_NN_ground.png, plans/Building_NN_upper.png, plans/contact_sheet_upper.png
- kit_rooms.md: rooms per flat on the upper storey and the common rules.

Method:

1. Wall segments of each placed mesh are chained into closed loops and painted onto a 2.5 cm
   raster; a loop nested in an odd number of larger loops of the same mesh is a hole, so a
   merged partition mesh keeps its rooms open whatever its winding.
2. Doors are sealed: from each corner of a (possibly open) leaf box the doorway is searched
   along x / y as a wall-free run of the leaf length with wall jambs past both ends; the seal
   spans jamb to jamb and the jambs' wall thickness. Without a doorway the leaf box,
   stretched along its long side, is the seal. Window boxes (Win_*, StairGlass_*) are sealed
   too. Free cells inside the slab footprint (plus the stairwell hole) are labelled
   4-connected; pieces under MIN_ROOM_M2 are dropped.
3. Regions holding a Stair_* / Steps_* box are stair halls. A Door_Apt joins a landing
   (stair hall, else the side with more flat doors) to a flat's entry room; an entry room
   serving several flat doors is a shared corridor. Without flat doors, a street door
   (Ent_Door, Win_*_door) opens a flat. Door_Int joins rooms of one flat (union-find).
4. Room kind by area and windows: entry room = hall, < 5 m2 = bath/WC, 5..8 m2 with a window
   = kitchen (one per flat, the smallest), >= 8 m2 with a window = living; windowless rooms
   with >= 3 doors = corridor.

  python Scripts/Tools/kit_plan_images.py [--dump-dir <dir>] [--out-dir <dir>] [--only 01]
"""
import argparse
import json
import math
import os
import re
import statistics
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kit_plan_report as report  # noqa: E402

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
AGENTS_DUMP = os.path.normpath(os.path.join(REPO_ROOT, "..", "CodexTactics-agents", "Saved", "KitDump"))

FINE_CM = 2.5
ROOM_CM = 2.5
MIN_ROOM_M2 = 0.8
DOOR_STRETCH_CM = 25.0
DOOR_PROBE_CM = 50.0
JAMB_REACH_CM = 40.0
WINDOW_PROBE_CM = 60.0
SNAP_CM = 0.05
PAD_CM = 150.0

EXTERIOR_TOKENS = ("SM_SH_Wall_", "SM_SH_Rust_", "SM_SH_StairWall_", "SM_SH_StairEnt_")
INTERIOR_TOKENS = ("_Interior_F", "SM_SH_IntWall_")
WINDOW_TOKENS = ("SM_SH_Win_", "SM_SH_StairGlass_")


# ---------------------------------------------------------------- classification

def wall_category(mesh):
    """exterior | interior | window | decor for a sliced mesh name."""
    if any(token in mesh for token in WINDOW_TOKENS):
        return "window"
    if any(token in mesh for token in INTERIOR_TOKENS):
        return "interior"
    if any(token in mesh for token in EXTERIOR_TOKENS):
        return "exterior"
    return "decor"


def item_kind(mesh):
    if "SM_SH_Door_Int" in mesh:
        return "door_int"
    if "SM_SH_Door_Apt" in mesh:
        return "door_apt"
    if "SM_SH_Ent_Door" in mesh:
        return "entrance"
    if mesh.startswith("SM_SH_Win_") and ("_door_" in mesh or "_shopdoor_" in mesh):
        return "entrance"  # glazed street door of low-rise houses and shops
    if mesh.startswith("SM_SH_Stair_") or mesh.startswith("SM_SH_Steps_"):
        return "stair"
    if mesh.startswith("SM_SH_Balc_"):
        return "balcony"
    if mesh.startswith("SM_SH_Win_") or mesh.startswith("SM_SH_StairGlass_"):
        return "window"
    return None


# ---------------------------------------------------------------- geometry

def signed_area(loop):
    """Shoelace area, positive for counter-clockwise in a y-up frame."""
    total = 0.0
    count = len(loop)
    for index in range(count):
        x0, y0 = loop[index]
        x1, y1 = loop[(index + 1) % count]
        total += x0 * y1 - x1 * y0
    return total * 0.5


def _key(x, y, snap=SNAP_CM):
    return (int(round(x / snap)), int(round(y / snap)))


def chain_loops(segments, snap=SNAP_CM):
    """Closed loops and open chains from oriented segments (x0, y0, x1, y1).

    End points are matched on a snap grid (and its 8 neighbours). A segment that only
    matches reversed is used reversed, so a badly wound face does not break a loop.
    """
    starts = {}
    ends = {}
    for index, (x0, y0, x1, y1) in enumerate(segments):
        starts.setdefault(_key(x0, y0, snap), []).append(index)
        ends.setdefault(_key(x1, y1, snap), []).append(index)
    used = [False] * len(segments)

    def take(table, key):
        kx, ky = key
        for dx in (0, -1, 1):
            for dy in (0, -1, 1):
                for index in table.get((kx + dx, ky + dy), ()):
                    if not used[index]:
                        return index
        return None

    loops = []
    chains = []
    for first in range(len(segments)):
        if used[first]:
            continue
        used[first] = True
        x0, y0, x1, y1 = segments[first]
        points = [(x0, y0), (x1, y1)]
        start_key = _key(x0, y0, snap)
        closed = False
        for _guard in range(len(segments) + 1):
            tail = _key(points[-1][0], points[-1][1], snap)
            if len(points) > 2 and abs(tail[0] - start_key[0]) <= 1 and abs(tail[1] - start_key[1]) <= 1:
                closed = True
                break
            index = take(starts, tail)
            if index is not None:
                used[index] = True
                points.append((segments[index][2], segments[index][3]))
                continue
            index = take(ends, tail)
            if index is not None:
                used[index] = True
                points.append((segments[index][0], segments[index][1]))
                continue
            break
        if closed:
            loops.append(points[:-1])
        else:
            chains.append(points)
    return loops, chains


class Grid:
    """Raster frame: cell size in cm, origin at the world min corner, rows along +y."""

    def __init__(self, min_x, min_y, max_x, max_y, cell):
        self.cell = float(cell)
        self.min_x = float(min_x)
        self.min_y = float(min_y)
        self.width = int(math.ceil((max_x - min_x) / cell)) + 1
        self.height = int(math.ceil((max_y - min_y) / cell)) + 1

    def px(self, x, y):
        return ((x - self.min_x) / self.cell, (y - self.min_y) / self.cell)

    def world(self, col, row):
        return (self.min_x + (col + 0.5) * self.cell, self.min_y + (row + 0.5) * self.cell)

    def cell_of(self, x, y):
        col = int(math.floor((x - self.min_x) / self.cell))
        row = int(math.floor((y - self.min_y) / self.cell))
        return col, row

    def empty(self):
        return np.zeros((self.height, self.width), dtype=bool)


def _draw():
    from PIL import Image, ImageDraw
    return Image, ImageDraw


def point_in_loop(x, y, loop):
    inside = False
    count = len(loop)
    for index in range(count):
        x0, y0 = loop[index]
        x1, y1 = loop[(index + 1) % count]
        if (y0 > y) != (y1 > y):
            t = (y - y0) / (y1 - y0)
            if x < x0 + (x1 - x0) * t:
                inside = not inside
    return inside


def loop_box(loop):
    xs = [p[0] for p in loop]
    ys = [p[1] for p in loop]
    return min(xs), min(ys), max(xs), max(ys)


def nesting_depths(ordered):
    """How many larger loops contain each loop (loops sorted by falling |area|).

    Even depth = solid, odd = hole. Independent of winding, so mis-wound faces and
    overlapping boxes of a merged mesh still fill as walls.
    """
    boxes = [loop_box(loop) for loop in ordered]
    depths = []
    for index, loop in enumerate(ordered):
        bx0, by0, bx1, by1 = boxes[index]
        # A point just inside the loop's first edge, off the shared boundaries.
        probe_x = (loop[0][0] + loop[1][0]) * 0.5
        probe_y = (loop[0][1] + loop[1][1]) * 0.5
        depth = 0
        for other in range(index):
            ox0, oy0, ox1, oy1 = boxes[other]
            if bx0 < ox0 - 0.5 or by0 < oy0 - 0.5 or bx1 > ox1 + 0.5 or by1 > oy1 + 0.5:
                continue
            if point_in_loop(probe_x, probe_y, ordered[other]):
                depth += 1
        depths.append(depth)
    return depths


def paint_loops(mask, grid, loops, chains=()):
    """OR one mesh's cross-section into mask. A loop nested in an odd number of larger
    loops is a hole (a room inside a merged partition mesh); open chains become lines."""
    Image, ImageDraw = _draw()
    points = [p for loop in loops for p in loop] + [p for chain in chains for p in chain]
    if not points:
        return mask
    cols = [grid.px(x, y)[0] for x, y in points]
    rows = [grid.px(x, y)[1] for x, y in points]
    c0 = max(0, int(math.floor(min(cols))) - 2)
    r0 = max(0, int(math.floor(min(rows))) - 2)
    c1 = min(grid.width, int(math.ceil(max(cols))) + 3)
    r1 = min(grid.height, int(math.ceil(max(rows))) + 3)
    if c1 <= c0 or r1 <= r0:
        return mask
    image = Image.new("L", (c1 - c0, r1 - r0), 0)
    draw = ImageDraw.Draw(image)
    ordered = [loop for loop in sorted(loops, key=lambda loop: -abs(signed_area(loop))) if len(loop) >= 3]
    depths = nesting_depths(ordered)
    for loop, depth in zip(ordered, depths):
        poly = [(grid.px(x, y)[0] - c0, grid.px(x, y)[1] - r0) for x, y in loop]
        solid = depth % 2 == 0
        draw.polygon(poly, fill=255 if solid else 0, outline=255)
    for chain in chains:
        line = [(grid.px(x, y)[0] - c0, grid.px(x, y)[1] - r0) for x, y in chain]
        if len(line) >= 2:
            draw.line(line, fill=255, width=1)
    mask[r0:r1, c0:c1] |= np.asarray(image, dtype=np.uint8) > 0
    return mask


def paint_triangles(mask, grid, triangles):
    Image, ImageDraw = _draw()
    image = Image.new("L", (grid.width, grid.height), 0)
    draw = ImageDraw.Draw(image)
    for tri in triangles:
        draw.polygon([grid.px(p[0], p[1]) for p in tri], fill=255, outline=255)
    mask |= np.asarray(image, dtype=np.uint8) > 0
    return mask


def paint_rect(mask, grid, low, high):
    c0, r0 = grid.cell_of(low[0], low[1])
    c1, r1 = grid.cell_of(high[0], high[1])
    c0, r0 = max(0, c0), max(0, r0)
    c1, r1 = min(grid.width - 1, c1), min(grid.height - 1, r1)
    if c1 >= c0 and r1 >= r0:
        mask[r0:r1 + 1, c0:c1 + 1] = True
    return mask


def max_pool(mask, factor):
    if factor == 1:
        return mask.copy()
    height = int(math.ceil(mask.shape[0] / factor)) * factor
    width = int(math.ceil(mask.shape[1] / factor)) * factor
    padded = np.zeros((height, width), dtype=bool)
    padded[:mask.shape[0], :mask.shape[1]] = mask
    return padded.reshape(height // factor, factor, width // factor, factor).any(axis=(1, 3))


def long_axis(low, high):
    """0 when the box is longer along x, else 1."""
    return 0 if (high[0] - low[0]) >= (high[1] - low[1]) else 1


def door_blocker(low, high, stretch=DOOR_STRETCH_CM):
    """2D box that seals a doorway: the leaf AABB stretched along its long side."""
    axis = long_axis(low, high)
    lo = [low[0], low[1]]
    hi = [high[0], high[1]]
    lo[axis] -= stretch
    hi[axis] += stretch
    return lo, hi


def wall_in_band(walls, grid, x, y, across_axis, half_cm):
    """True when any wall cell lies within half_cm of (x, y) along across_axis."""
    steps = int(half_cm / grid.cell)
    for step in range(-steps, steps + 1):
        px = x + (step * grid.cell if across_axis == 0 else 0.0)
        py = y + (step * grid.cell if across_axis == 1 else 0.0)
        col, row = grid.cell_of(px, py)
        if 0 <= row < walls.shape[0] and 0 <= col < walls.shape[1] and walls[row, col]:
            return True
    return False


def leaf_length(low, high):
    return min(110.0, max(60.0, math.hypot(high[0] - low[0], high[1] - low[1]) - 5.0))


def find_doorway(walls, grid, low, high, band_cm=15.0):
    """Doorway a (possibly open) door leaf hangs in: (x, y, axis, sign, length) or None.

    From each corner of the leaf box, along +-x and +-y for the leaf length, the opening
    must be wall-free across +-band_cm and have wall jambs just past both ends.
    """
    length = leaf_length(low, high)
    corners = [(low[0], low[1]), (high[0], low[1]), (low[0], high[1]), (high[0], high[1])]
    best = None
    for cx, cy in corners:
        for axis in (0, 1):
            across = 1 - axis
            for sign in (-1.0, 1.0):
                def at(t):
                    return (cx + sign * t, cy) if axis == 0 else (cx, cy + sign * t)
                jamb_a = any(wall_in_band(walls, grid, *at(-t), across, band_cm) for t in (5.0, 10.0, 15.0))
                jamb_b = any(wall_in_band(walls, grid, *at(length + t), across, band_cm)
                             for t in (5.0, 10.0, 15.0, 20.0))
                if not (jamb_a and jamb_b):
                    continue
                samples = [at(t) for t in np.arange(10.0, length - 9.0, 5.0)]
                if not samples:
                    continue
                free = sum(1 for x, y in samples if not wall_in_band(walls, grid, x, y, across, band_cm))
                score = free / float(len(samples))
                if score >= 0.7 and (best is None or score > best[0]):
                    offsets = []
                    for t in (-15.0, -10.0, -5.0, length + 5.0, length + 10.0, length + 15.0, length + 20.0):
                        offsets.extend(wall_offsets(walls, grid, *at(t), across, JAMB_REACH_CM))
                    best = (score, (cx, cy, axis, sign, length, min(offsets), max(offsets)))
    return None if best is None else best[1]


def wall_offsets(walls, grid, x, y, across_axis, half_cm):
    """Offsets (cm) along across_axis from (x, y) of wall cells within half_cm."""
    steps = int(half_cm / grid.cell)
    found = []
    for step in range(-steps, steps + 1):
        offset = step * grid.cell
        px = x + (offset if across_axis == 0 else 0.0)
        py = y + (offset if across_axis == 1 else 0.0)
        col, row = grid.cell_of(px, py)
        if 0 <= row < walls.shape[0] and 0 <= col < walls.shape[1] and walls[row, col]:
            found.append(offset)
    return found


def doorway_seal(doorway, stretch=DOOR_STRETCH_CM):
    """2D box over a found doorway: along it jamb to jamb, across it the jambs' wall thickness."""
    x, y, axis, sign, length, across_lo, across_hi = doorway
    t0, t1 = -stretch, length + stretch
    lo, hi = across_lo - 2.5, across_hi + 2.5
    if axis == 0:
        xs = sorted((x + sign * t0, x + sign * t1))
        return [xs[0], y + lo], [xs[1], y + hi]
    ys = sorted((y + sign * t0, y + sign * t1))
    return [x + lo, ys[0]], [x + hi, ys[1]]


def door_geometry(walls, grid, item):
    """(seal_low, seal_high, probe_low, probe_high) for a door item.

    The probe box is thin across the doorway so two_sides() looks into both rooms.
    Without a found doorway the leaf box itself is used.
    """
    low, high = item["min"], item["max"]
    doorway = find_doorway(walls, grid, low, high)
    if doorway is None:
        seal_low, seal_high = door_blocker(low, high)
        return seal_low, seal_high, low, high
    seal_low, seal_high = doorway_seal(doorway)
    x, y, axis, sign, length, across_lo, across_hi = doorway
    mid = length * 0.5
    centre = (across_lo + across_hi) * 0.5  # middle of the wall, not the leaf's face
    if axis == 0:
        mx = x + sign * mid
        return seal_low, seal_high, [mx - mid, y + centre - 1.0], [mx + mid, y + centre + 1.0]
    my = y + sign * mid
    return seal_low, seal_high, [x + centre - 1.0, my - mid], [x + centre + 1.0, my + mid]


def label_regions(free):
    from scipy import ndimage
    labels, count = ndimage.label(free)
    return labels, count


# ---------------------------------------------------------------- analysis

class UnionFind:
    def __init__(self):
        self.parent = {}

    def find(self, item):
        self.parent.setdefault(item, item)
        while self.parent[item] != item:
            self.parent[item] = self.parent[self.parent[item]]
            item = self.parent[item]
        return item

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[max(ra, rb)] = min(ra, rb)


def probe_label(labels, grid, x, y, radius=2):
    """Most common non-zero label in a small window around (x, y)."""
    col, row = grid.cell_of(x, y)
    best = {}
    for dr in range(-radius, radius + 1):
        for dc in range(-radius, radius + 1):
            r, c = row + dr, col + dc
            if 0 <= r < labels.shape[0] and 0 <= c < labels.shape[1]:
                value = int(labels[r, c])
                if value:
                    best[value] = best.get(value, 0) + 1
    if not best:
        return 0
    return max(best, key=lambda value: (best[value], -value))


def two_sides(labels, grid, low, high, distance):
    """Labels on both sides of a thin box across its short axis."""
    axis = 1 - long_axis(low, high)
    cx = (low[0] + high[0]) * 0.5
    cy = (low[1] + high[1]) * 0.5
    sides = []
    for sign in (-1.0, 1.0):
        x = cx + (sign * distance if axis == 0 else 0.0)
        y = cy + (sign * distance if axis == 1 else 0.0)
        sides.append(probe_label(labels, grid, x, y))
    return sides


def storey_items(storey):
    """Items of the storey by kind, filtered to the storey's own floor."""
    floor_z = storey["floor_z"]
    cut_z = storey["cut_z"]
    out = {"door_int": [], "door_apt": [], "entrance": [], "stair": [], "balcony": [], "window": []}
    for item in storey.get("items", []):
        kind = item_kind(item["mesh"])
        if kind is None:
            continue
        low, high = item["min"], item["max"]
        if kind in ("door_int", "door_apt", "entrance"):
            # Entrances may sit below the ground storey floor, at porch level.
            lowest = floor_z - (130.0 if kind == "entrance" else 30.0)
            if not (lowest <= low[2] <= floor_z + 60.0):
                continue
        elif kind == "stair":
            if not (high[2] > floor_z + 10.0 and low[2] < cut_z):
                continue
        elif kind == "balcony":
            if not (floor_z - 40.0 <= low[2] <= floor_z + 60.0):
                continue
        elif kind == "window":
            # Windows of this storey, high-sill ones (above the cut) included.
            if not (high[2] > cut_z - 50.0 and low[2] < floor_z + 200.0):
                continue
        out[kind].append(item)
    return out


def storey_bounds(storey):
    xs, ys = [], []
    for wall in storey.get("walls", []):
        for x0, y0, x1, y1 in wall["segments"]:
            xs.extend((x0, x1))
            ys.extend((y0, y1))
    for tri in storey.get("footprint_triangles", []):
        for x, y in tri:
            xs.append(x)
            ys.append(y)
    if not xs:
        return None
    return min(xs) - PAD_CM, min(ys) - PAD_CM, max(xs) + PAD_CM, max(ys) + PAD_CM


def edge_sides(plan):
    """{edge: side} from kit_plans rows that carry both, plus the footprint box in cm."""
    labels = {}
    if not plan:
        return labels, None
    rows = list(plan.get("entrances") or []) + list(plan.get("stairwells") or [])
    for side_rows in (plan.get("windows_by_facade") or {}).values():
        rows.extend(side_rows)
    for row in rows:
        if row.get("edge") and row.get("side"):
            labels.setdefault(row["edge"], row["side"])
    footprint = plan.get("footprint_m") or {}
    box = None
    if footprint.get("min_cm") and footprint.get("max_cm"):
        box = (footprint["min_cm"][0], footprint["min_cm"][1], footprint["max_cm"][0], footprint["max_cm"][1])
    if box and len(labels) < 4:
        guessed = report.classify_edges(box, {})
        for edge, side in guessed.items():
            labels.setdefault(edge, side if side == "end" else "unknown")
    return labels, box


def classify_room(room):
    """Room kind from area, windows and doors. Entry rooms are halls."""
    area = room["area_m2"]
    windows = room["windows"]
    if room.get("stair"):
        return "stair"
    if room.get("shared"):
        return "shared corridor"
    if room.get("entry"):
        if not windows:
            return "hall"
        # A windowed entry room is a hall merged with a room through a doorless opening.
        return "living (entry)" if area >= 12.0 else "hall+kitchen (open)"
    if not windows and room["doors"] >= 3:
        return "corridor"
    if area < 5.0:
        return "small (window)" if windows else "bath/WC"
    if area < 8.0:
        return "kitchen" if windows else "store/corridor"
    return "living" if windows else "hall/corridor"


def analyse_storey(storey, sides=None, footprint_box=None, build_fine=True):
    """Wall masks, room labels, flats and room facts of one storey. Pure; no drawing."""
    bounds = storey_bounds(storey)
    if bounds is None:
        return None
    sides = sides or {}
    fine = Grid(*bounds, cell=FINE_CM)
    walls_ext = fine.empty()
    walls_int = fine.empty()
    windows_fine = fine.empty()
    for wall in storey.get("walls", []):
        category = wall_category(wall["mesh"])
        if category == "decor":
            continue
        loops, chains = chain_loops([tuple(seg) for seg in wall["segments"]])
        target = {"exterior": walls_ext, "interior": walls_int, "window": windows_fine}[category]
        paint_loops(target, fine, loops, chains)
    factor = int(round(ROOM_CM / FINE_CM))
    room = Grid(*bounds, cell=ROOM_CM)
    blocked = max_pool(walls_ext | walls_int | windows_fine, factor)[:room.height, :room.width]
    if blocked.shape != (room.height, room.width):
        grown = np.zeros((room.height, room.width), dtype=bool)
        grown[:blocked.shape[0], :blocked.shape[1]] = blocked
        blocked = grown
    items = storey_items(storey)
    solid = walls_ext | walls_int
    doors = {}
    for kind in ("door_int", "door_apt", "entrance"):
        doors[kind] = []
        for item in items[kind]:
            seal_low, seal_high, probe_low, probe_high = door_geometry(solid, fine, item)
            paint_rect(blocked, room, seal_low, seal_high)
            doors[kind].append({"item": item, "probe": (probe_low, probe_high),
                                "found": probe_low is not item["min"]})
    for item in items["window"]:
        paint_rect(blocked, room, item["min"], item["max"])
    inside = paint_triangles(room.empty(), room, storey.get("footprint_triangles", []))
    for item in items["stair"]:  # the slab has a hole over the stairwell; porch Steps_ stay outside
        if item["mesh"].startswith("SM_SH_Stair_"):
            paint_rect(inside, room, item["min"], item["max"])
    free = inside & ~blocked
    labels, count = label_regions(free)
    cell_m2 = (ROOM_CM / 100.0) ** 2
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    keep = np.zeros(count + 1, dtype=bool)
    keep[1:] = sizes[1:] * cell_m2 >= MIN_ROOM_M2
    labels = np.where(keep[labels], labels, 0)

    rooms = {}
    rows_cols = np.nonzero(labels)
    for value in np.unique(labels[rows_cols]) if rows_cols[0].size else []:
        value = int(value)
        rr, cc = np.nonzero(labels == value)
        cx, cy = room.world(float(cc.mean()), float(rr.mean()))
        x0, y0 = room.world(float(cc.min()), float(rr.min()))
        x1, y1 = room.world(float(cc.max()), float(rr.max()))
        rooms[value] = {
            "label": value,
            "area_m2": round(sizes[value] * cell_m2, 2),
            "center": (cx, cy),
            "bbox": (x0, y0, x1, y1),
            "windows": [],
            "window_sides": [],
            "doors": 0,
            "balcony": False,
            "stair": False,
            "entry": False,
        }

    for item in items["stair"]:
        low, high = item["min"], item["max"]
        c0, r0 = room.cell_of(low[0], low[1])
        c1, r1 = room.cell_of(high[0], high[1])
        patch = labels[max(0, r0):max(0, r1 + 1), max(0, c0):max(0, c1 + 1)]
        values, counts = np.unique(patch[patch > 0], return_counts=True)
        if values.size:
            rooms[int(values[np.argmax(counts)])]["stair"] = True

    finder = UnionFind()
    for value in rooms:
        finder.find(value)
    int_pairs = []
    for door in doors["door_int"]:
        a, b = two_sides(labels, room, door["probe"][0], door["probe"][1], DOOR_PROBE_CM)
        for value in (a, b):
            if value in rooms:
                rooms[value]["doors"] += 1
        if a in rooms and b in rooms and a != b:
            int_pairs.append((a, b))

    apt_pairs = []
    for door in doors["door_apt"]:
        a, b = two_sides(labels, room, door["probe"][0], door["probe"][1], DOOR_PROBE_CM)
        apt_pairs.append((a, b, door["item"]))
    apt_count = {}
    for a, b, _item in apt_pairs:
        for value in (a, b):
            if value:
                apt_count[value] = apt_count.get(value, 0) + 1
    landings = {}
    entries = []
    for a, b, item in apt_pairs:
        if a not in rooms or b not in rooms or a == b:
            continue
        if rooms[a]["stair"] != rooms[b]["stair"]:
            landing, entry = (a, b) if rooms[a]["stair"] else (b, a)
        else:
            landing, entry = (a, b) if apt_count.get(a, 0) >= apt_count.get(b, 0) else (b, a)
        landings.setdefault(landing, []).append(entry)
        entries.append(entry)
    if not apt_pairs:
        # Low-rise houses without flat doors: each building entrance opens a flat.
        for door in doors["entrance"]:
            a, b = two_sides(labels, room, door["probe"][0], door["probe"][1], DOOR_PROBE_CM)
            outer = {value for value in (a, b) if value == 0 or rooms.get(value, {}).get("stair")}
            inner = [value for value in (a, b) if value not in outer]
            inside_room = inner[0] if outer and len(inner) == 1 else None
            if inside_room in rooms and not rooms[inside_room]["stair"]:
                entries.append(inside_room)
    # An entry room that itself serves several flat doors is a shared corridor, not a flat.
    shared = {entry for entry in entries if len(landings.get(entry, ())) >= 2}
    entries = [entry for entry in dict.fromkeys(entries) if entry not in shared]
    for entry in entries:
        rooms[entry]["entry"] = True
    for value in shared:
        rooms[value]["shared"] = True
    for a, b in int_pairs:
        if not any(rooms[v]["stair"] or rooms[v].get("shared") for v in (a, b)):
            finder.union(a, b)

    for item in items["window"]:
        low, high = item["min"], item["max"]
        a, b = two_sides(labels, room, low, high, WINDOW_PROBE_CM)
        cx = (low[0] + high[0]) * 0.5
        cy = (low[1] + high[1]) * 0.5
        side = "unknown"
        if footprint_box:
            side = sides.get(report.nearest_edge(cx, cy, footprint_box), "unknown")
        for value in {a, b}:
            if value in rooms:
                rooms[value]["windows"].append(item["mesh"])
                rooms[value]["window_sides"].append(side)
                if "_balc_" in item["mesh"]:
                    rooms[value]["balcony"] = True

    balcony_rooms = []
    for item in items["balcony"]:
        low, high = item["min"], item["max"]
        bx = (low[0] + high[0]) * 0.5
        by = (low[1] + high[1]) * 0.5
        best = None
        for window in items["window"] + items["entrance"]:
            wx = (window["min"][0] + window["max"][0]) * 0.5
            wy = (window["min"][1] + window["max"][1]) * 0.5
            # A balcony door (Win_*_balc_*) wins over a plain window next to the balcony.
            distance = math.hypot(wx - bx, wy - by) + (0.0 if "_balc_" in window["mesh"] else 100.0)
            if distance < 250.0 and (best is None or distance < best[0]):
                best = (distance, window)
        if best is not None:
            a, b = two_sides(labels, room, best[1]["min"], best[1]["max"], WINDOW_PROBE_CM)
            for value in (a, b):
                if value in rooms:
                    rooms[value]["balcony"] = True
                    balcony_rooms.append(value)

    flats = {}
    for entry in entries:
        root = finder.find(entry)
        flats.setdefault(root, {"entries": [], "rooms": set()})["entries"].append(entry)
    for value in rooms:
        root = finder.find(value)
        if root in flats:
            flats[root]["rooms"].add(value)
    for value, info in rooms.items():
        info["kind"] = classify_room(info)
        info["flat"] = None
    flat_rows = []
    ordered = sorted(flats.values(), key=lambda flat: (rooms[min(flat["rooms"])]["center"][0],
                                                       rooms[min(flat["rooms"])]["center"][1]))
    landing_of_entry = {}
    for landing, entry_list in landings.items():
        for entry in entry_list:
            landing_of_entry[entry] = landing
    for number, flat in enumerate(ordered, start=1):
        members = sorted(flat["rooms"], key=lambda value: -rooms[value]["area_m2"])
        for value in members:
            rooms[value]["flat"] = number
        if len(members) == 1 and rooms[members[0]]["windows"]:
            rooms[members[0]]["kind"] = "living (single room)"
        # One kitchen per flat: the smallest windowed 5-8 m2 room; larger ones are small living rooms.
        kitchens = sorted((value for value in members if rooms[value]["kind"] == "kitchen"),
                          key=lambda value: rooms[value]["area_m2"])
        for value in kitchens[1:]:
            rooms[value]["kind"] = "living"
        landing = landing_of_entry.get(flat["entries"][0])
        living = [value for value in members if rooms[value]["kind"].startswith("living")]
        flat_rows.append({
            "flat": number,
            "landing": landing,
            "corridor_unit": bool(landing in rooms and rooms[landing].get("shared")),
            "entries": len(flat["entries"]),
            "rooms": members,
            "living_rooms": len(living),
            "area_m2": round(sum(rooms[value]["area_m2"] for value in members), 2),
            "balcony": any(rooms[value]["balcony"] for value in members),
        })
    return {
        "fine": fine,
        "room_grid": room,
        "walls_ext": walls_ext,
        "walls_int": walls_int,
        "windows_fine": windows_fine,
        "inside": inside,
        "labels": labels,
        "rooms": rooms,
        "flats": flat_rows,
        "landings": {landing: len(entry_list) for landing, entry_list in landings.items()},
        "items": items,
        "doors": doors,
        "apt_doors": len(items["door_apt"]),
    }


# ---------------------------------------------------------------- drawing

PASTELS = ["#f6d8ae", "#c9e4ca", "#cde0f5", "#f3c9d9", "#e3d5f2", "#fbeeb0", "#c7eee9", "#f2d0c4",
           "#d9e8b8", "#d6d6f5", "#f5e1c8", "#c4dcef"]
KIND_SHORT = {"living": "L", "living (entry)": "L*", "hall+kitchen (open)": "HK", "kitchen": "K", "bath/WC": "B", "hall": "H",
              "corridor": "C", "store/corridor": "S", "hall/corridor": "C", "small (window)": "s", "stair": "St"}


def colour_image(result):
    """RGBA image (room raster) with flats in pastel, stairs grey, other rooms light."""
    labels = result["labels"]
    rgba = np.zeros(labels.shape + (4,), dtype=float)
    from matplotlib.colors import to_rgba
    for value, info in result["rooms"].items():
        if info["stair"]:
            colour = to_rgba("#d0d0d0")
        elif info["flat"] is not None:
            colour = to_rgba(PASTELS[(info["flat"] - 1) % len(PASTELS)])
        else:
            colour = to_rgba("#f4f4f4")
        rgba[labels == value] = colour
    return rgba


def to_m(grid_origin, x, y):
    return (x - grid_origin[0]) / 100.0, (y - grid_origin[1]) / 100.0


def draw_storey(ax, result, origin, detail=True):
    import matplotlib.patches as patches
    fine = result["fine"]
    room = result["room_grid"]

    def extent(grid, shape):
        x0, y0 = to_m(origin, grid.min_x, grid.min_y)
        return (x0, x0 + shape[1] * grid.cell / 100.0, y0 + shape[0] * grid.cell / 100.0, y0)

    ax.imshow(colour_image(result), extent=extent(room, result["labels"].shape), interpolation="nearest", zorder=1)
    walls = result["walls_int"] | result["walls_ext"]
    if detail:
        from scipy import ndimage
        walls = walls | ndimage.binary_dilation(result["walls_ext"], iterations=1)
    wall_rgba = np.zeros(walls.shape + (4,), dtype=float)
    wall_rgba[walls] = (0.0, 0.0, 0.0, 1.0)
    ax.imshow(wall_rgba, extent=extent(fine, walls.shape), interpolation="nearest", zorder=3)
    items = result["items"]
    for item in items["stair"]:
        x0, y0 = to_m(origin, item["min"][0], item["min"][1])
        x1, y1 = to_m(origin, item["max"][0], item["max"][1])
        ax.add_patch(patches.Rectangle((x0, y0), x1 - x0, y1 - y0, facecolor="#bdbdbd", edgecolor="#707070",
                                       hatch="////" if detail else None, linewidth=0.6, zorder=2, alpha=0.9))
    for item in items["balcony"]:
        x0, y0 = to_m(origin, item["min"][0], item["min"][1])
        x1, y1 = to_m(origin, item["max"][0], item["max"][1])
        ax.add_patch(patches.Rectangle((x0, y0), x1 - x0, y1 - y0, facecolor="none", edgecolor="#5a4a8a",
                                       linewidth=1.2 if detail else 0.6, zorder=4))
    for item in items["window"]:
        low, high = item["min"], item["max"]
        x0, y0 = to_m(origin, low[0], low[1])
        x1, y1 = to_m(origin, high[0], high[1])
        if long_axis(low, high) == 0:
            ym = (y0 + y1) * 0.5
            ax.plot([x0, x1], [ym, ym], color="#00b8d4", linewidth=2.5 if detail else 1.0, zorder=5,
                    solid_capstyle="butt")
        else:
            xm = (x0 + x1) * 0.5
            ax.plot([xm, xm], [y0, y1], color="#00b8d4", linewidth=2.5 if detail else 1.0, zorder=5,
                    solid_capstyle="butt")
    door_colours = {"door_int": "#1e9e3a", "door_apt": "#d32f2f", "entrance": "#1f4fd8"}
    for kind, colour in door_colours.items():
        for door in result["doors"][kind]:
            low, high = door["probe"]
            x0, y0 = to_m(origin, low[0], low[1])
            x1, y1 = to_m(origin, high[0], high[1])
            pad = 0.05 if kind == "door_int" else 0.08
            ax.add_patch(patches.Rectangle((x0 - pad, y0 - pad), x1 - x0 + 2 * pad, y1 - y0 + 2 * pad,
                                           facecolor=colour, edgecolor=colour, linewidth=0.5, zorder=6))
    if detail:
        for info in result["rooms"].values():
            if info["area_m2"] < 1.5:
                continue
            x, y = to_m(origin, info["center"][0], info["center"][1])
            short = KIND_SHORT.get(info["kind"], "?")
            text = "%s %.1f" % (short, info["area_m2"])
            if info["flat"] is not None:
                text = "F%d %s" % (info["flat"], text)
            if info["balcony"]:
                text += " b"
            ax.text(x, y, text, fontsize=6.5, ha="center", va="center", zorder=7,
                    bbox=dict(boxstyle="round,pad=0.15", facecolor="white", edgecolor="none", alpha=0.7))


def finish_axes(ax, result, origin, detail=True):
    fine = result["fine"]
    x0, y0 = to_m(origin, fine.min_x, fine.min_y)
    x1 = x0 + fine.width * fine.cell / 100.0
    y1 = y0 + fine.height * fine.cell / 100.0
    ax.set_xlim(x0, x1)
    ax.set_ylim(y1, y0)  # UE top view: +X right, +Y down
    ax.set_aspect("equal")
    if detail:
        ax.set_xticks(np.arange(math.ceil(x0), math.floor(x1) + 1, 1.0), minor=True)
        ax.set_yticks(np.arange(math.ceil(y0), math.floor(y1) + 1, 1.0), minor=True)
        ax.set_xticks(np.arange(math.ceil(x0 / 5) * 5, x1, 5.0))
        ax.set_yticks(np.arange(math.ceil(y0 / 5) * 5, y1, 5.0))
        ax.grid(which="minor", color="#000000", alpha=0.07, linewidth=0.5, zorder=0)
        ax.grid(which="major", color="#000000", alpha=0.15, linewidth=0.6, zorder=0)
        ax.tick_params(labelsize=7)
        ax.set_xlabel("x, m (UE +X)", fontsize=7)
        ax.set_ylabel("y, m (UE +Y down)", fontsize=7)
        bar_x = x0 + 0.5
        bar_y = y1 - 0.5
        ax.plot([bar_x, bar_x + 5.0], [bar_y, bar_y], color="black", linewidth=3, zorder=8)
        ax.text(bar_x + 2.5, bar_y - 0.25, "5 m", ha="center", va="bottom", fontsize=7, zorder=8)
    else:
        ax.set_xticks([])
        ax.set_yticks([])


def legend_handles():
    import matplotlib.patches as patches
    from matplotlib.lines import Line2D
    return [
        Line2D([0], [0], color="black", linewidth=4, label="wall (cut at +1.0 m)"),
        patches.Patch(facecolor="#1e9e3a", label="interior door"),
        patches.Patch(facecolor="#d32f2f", label="flat entry door"),
        patches.Patch(facecolor="#1f4fd8", label="building entrance"),
        patches.Patch(facecolor="#bdbdbd", hatch="////", edgecolor="#707070", label="stairs"),
        Line2D([0], [0], color="#00b8d4", linewidth=3, label="window"),
        patches.Patch(facecolor="none", edgecolor="#5a4a8a", label="balcony"),
        patches.Patch(facecolor=PASTELS[0], label="flat (Fn), L living K kitchen B bath H hall C corridor, b balcony"),
    ]


def render_storey_png(result, title, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fine = result["fine"]
    width_m = fine.width * fine.cell / 100.0
    height_m = fine.height * fine.cell / 100.0
    scale = min(0.42, 30.0 / max(width_m, height_m))  # inches per metre, image <= ~3000 px
    fig = plt.figure(figsize=(width_m * scale + 1.2, height_m * scale + 2.2), dpi=100)
    ax = fig.add_axes([0.06, 0.16, 0.92, 0.74])
    origin = (result["fine"].min_x + PAD_CM, result["fine"].min_y + PAD_CM)
    draw_storey(ax, result, origin, detail=True)
    finish_axes(ax, result, origin, detail=True)
    ax.set_title(title, fontsize=11)
    fig.legend(handles=legend_handles(), loc="lower center", ncol=4, fontsize=7, frameon=False)
    fig.savefig(path, dpi=100)
    plt.close(fig)


def render_contact_sheet(entries, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    columns = 4
    rows = int(math.ceil(len(entries) / float(columns))) or 1
    fig, axes = plt.subplots(rows, columns, figsize=(columns * 5.0, rows * 3.4), dpi=110)
    axes = np.atleast_2d(axes)
    for index in range(rows * columns):
        ax = axes[index // columns][index % columns]
        if index >= len(entries):
            ax.axis("off")
            continue
        title, result = entries[index]
        origin = (result["fine"].min_x + PAD_CM, result["fine"].min_y + PAD_CM)
        draw_storey(ax, result, origin, detail=False)
        finish_axes(ax, result, origin, detail=False)
        ax.set_title(title, fontsize=8)
    fig.suptitle("Soviet kit - typical upper storey of each building (ground if single-storey), cut at +1.0 m",
                 fontsize=11)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(path)
    plt.close(fig)


# ---------------------------------------------------------------- tables

def side_summary(sides):
    sides = [side for side in sides if side and side != "unknown"]
    if not sides:
        return "-"
    counts = {}
    for side in sides:
        counts[side] = counts.get(side, 0) + 1
    return "/".join(sorted(counts, key=lambda side: -counts[side]))


def building_rows(building_id, series, kind, result):
    """Rows for the markdown table: one per flat with its rooms."""
    rows = []
    for flat in result["flats"]:
        parts = []
        for value in flat["rooms"]:
            info = result["rooms"][value]
            text = "%s %.1f" % (info["kind"], info["area_m2"])
            facing = side_summary(info["window_sides"])
            if facing != "-":
                text += " (%s)" % facing
            if info["balcony"]:
                text += " +balcony"
            parts.append(text)
        rows.append({
            "building": building_id,
            "series": series,
            "storey": kind,
            "flat": flat["flat"],
            "landing": flat["landing"],
            "area_m2": flat["area_m2"],
            "living_rooms": flat["living_rooms"],
            "rooms": len(flat["rooms"]),
            "balcony": flat["balcony"],
            "detail": "; ".join(parts),
        })
    return rows


def common_rules(per_building):
    """Short summary over all analysed upper storeys."""
    flat_areas, living_counts, kinds, halls, corridor_flats = [], [], {}, 0, 0
    flats_per_landing = []
    facing = {}
    balcony_kinds = {}
    flats_total = 0
    corridor_units = []
    corridor_buildings = set()
    for building_id, _series, result in per_building:
        for landing, count in result["landings"].items():
            if not result["rooms"].get(landing, {}).get("shared"):
                flats_per_landing.append(count)
        for flat in result["flats"]:
            if flat["corridor_unit"]:
                corridor_units.append(flat["area_m2"])
                corridor_buildings.add(building_id)
                continue
            flats_total += 1
            flat_areas.append(flat["area_m2"])
            living_counts.append(flat["living_rooms"])
            kinds_in_flat = [result["rooms"][value]["kind"] for value in flat["rooms"]]
            if any(kind in ("hall",) for kind in kinds_in_flat):
                halls += 1
            if any(kind in ("corridor", "hall/corridor", "store/corridor") for kind in kinds_in_flat):
                corridor_flats += 1
            for value in flat["rooms"]:
                info = result["rooms"][value]
                kinds.setdefault(info["kind"], []).append(info["area_m2"])
                for side in set(info["window_sides"]):
                    if side in ("street", "court", "end"):
                        facing.setdefault(info["kind"], {}).setdefault(side, 0)
                        facing[info["kind"]][side] += 1
                if info["balcony"]:
                    balcony_kinds[info["kind"]] = balcony_kinds.get(info["kind"], 0) + 1

    def spread(values):
        if not values:
            return "n/a"
        ordered = sorted(values)
        return "median %.1f, range %.1f-%.1f" % (statistics.median(ordered), ordered[0], ordered[-1])

    lines = []
    if not flats_total:
        return ["- No flats found (no Door_Apt joined to a room)."]
    lines.append("- Sectional flats (door on a stair landing, or own street entrance): %d on %d storeys. "
                 "Flat area (net floor, m2): %s." % (flats_total, len(per_building), spread(flat_areas)))
    if corridor_units:
        lines.append("- Corridor (dormitory) units off a shared corridor: %d in buildings %s, area %s; "
                     "each is one windowed room, the corridor is reached from the stair hall." % (
                         len(corridor_units), ", ".join(sorted(corridor_buildings)), spread(corridor_units)))
    if flats_per_landing:
        counts = {}
        for value in flats_per_landing:
            counts[value] = counts.get(value, 0) + 1
        listed = ", ".join("%d per landing x%d" % (key, counts[key]) for key in sorted(counts))
        lines.append("- Flats per landing: %s (median %s)." % (listed, statistics.median(flats_per_landing)))
    counts = {}
    for value in living_counts:
        counts[value] = counts.get(value, 0) + 1
    lines.append("- Living rooms per flat: %s." % ", ".join(
        "%d-room x%d" % (key, counts[key]) for key in sorted(counts)))
    for kind in ("living", "kitchen", "bath/WC", "hall", "corridor"):
        if kind in kinds:
            lines.append("- %s: %d rooms, area %s." % (kind, len(kinds[kind]), spread(kinds[kind])))
    lines.append("- Entry hall (windowless room behind the flat door): %d of %d flats; corridor or inner hall: %d." % (
        halls, flats_total, corridor_flats))
    for kind in ("living", "kitchen", "small (window)"):
        if kind in facing:
            listed = ", ".join("%s %d" % (side, facing[kind][side]) for side in sorted(facing[kind]))
            lines.append("- %s windows face: %s." % (kind, listed))
    if balcony_kinds:
        listed = ", ".join("%s %d" % (kind, count) for kind, count in sorted(balcony_kinds.items(), key=lambda x: -x[1]))
        lines.append("- Balcony opens from: %s." % listed)
    return lines


def render_rooms_md(per_building, skipped):
    lines = [
        "# Soviet kit - rooms per flat (typical upper storey)",
        "",
        "Generated by `Scripts/Tools/kit_plan_images.py` from `kit_interiors.json` (walls cut at floor + 1.0 m)",
        "and `kit_plans.json` (series guess, street / court edges). Areas are net floor m2 on a 2.5 cm raster.",
        "Kinds: hall = room behind the flat door; bath/WC < 5 m2 without window; kitchen 5-8 m2 with window;",
        "living >= 8 m2 with window; corridor = windowless with 3+ doors. Facing = facade side of the room's windows.",
        "",
        "## Common rules",
        "",
    ]
    lines.extend(common_rules(per_building))
    lines.append("")
    for building_id, series, result in per_building:
        lines.append("## Building %s - %s" % (building_id, series))
        lines.append("")
        landing = ", ".join(str(count) for count in sorted(result["landings"].values())) or "none"
        corridor = sum(1 for flat in result["flats"] if flat["corridor_unit"])
        lines.append("Flat doors on this storey: %d. Doors per landing / shared corridor: %s. "
                     "Flats resolved: %d (of them corridor units: %d)." % (
                         result["apt_doors"], landing, len(result["flats"]), corridor))
        lines.append("")
        rows = building_rows(building_id, series, "upper", result)
        if not rows:
            lines.append("No flats resolved on this storey.")
            lines.append("")
            continue
        lines.append("| flat | m2 | living | rooms | balcony | rooms (kind m2, facing) |")
        lines.append("| --- | --- | --- | --- | --- | --- |")
        for row in rows:
            lines.append("| F%d | %.1f | %d | %d | %s | %s |" % (
                row["flat"], row["area_m2"], row["living_rooms"], row["rooms"],
                "yes" if row["balcony"] else "no", row["detail"]))
        lines.append("")
    if skipped:
        lines.append("## Not in the table")
        lines.append("")
        for text in skipped:
            lines.append("- %s" % text)
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


# ---------------------------------------------------------------- main

def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Draw Soviet kit floor plans and room tables.")
    parser.add_argument("--dump-dir", default=AGENTS_DUMP, help="folder with kit_interiors.json and kit_plans.json")
    parser.add_argument("--out-dir", default=None, help="folder for kit_rooms.md; PNGs go to <out>/plans")
    parser.add_argument("--only", default=None, help="one building id, e.g. 01")
    args = parser.parse_args(argv)
    dump_dir = os.path.abspath(args.dump_dir)
    out_dir = os.path.abspath(args.out_dir or dump_dir)
    plans_dir = os.path.join(out_dir, "plans")
    os.makedirs(plans_dir, exist_ok=True)
    interiors = load_json(os.path.join(dump_dir, "kit_interiors.json"))
    plans_path = os.path.join(dump_dir, "kit_plans.json")
    plans = {plan["id"]: plan for plan in load_json(plans_path)["buildings"]} if os.path.isfile(plans_path) else {}

    per_building = []
    sheet = []
    skipped = []
    for building in interiors.get("buildings", []):
        building_id = str(building["id"])
        if args.only and building_id != "%02d" % int(args.only):
            continue
        plan = plans.get(building_id)
        series = (plan or {}).get("series_guess", "unknown")
        sides, box = edge_sides(plan)
        upper = None
        ground = None
        for storey in building.get("storeys", []):
            result = analyse_storey(storey, sides, box)
            if result is None:
                continue
            title = "Building %s - %s - %s (floor z %.0f cm, cut +%.0f cm)" % (
                building_id, series, storey["kind"], storey["floor_z"], storey["cut_z"] - storey["floor_z"])
            path = os.path.join(plans_dir, "Building_%s_%s.png" % (building_id, storey["kind"]))
            render_storey_png(result, title, path)
            print("plan -> %s (rooms %d, flats %d)" % (path, len(result["rooms"]), len(result["flats"])))
            if storey["kind"] == "upper":
                upper = result
            else:
                ground = result
        if upper is not None:
            per_building.append((building_id, series, upper))
            sheet.append(("%s - %s" % (building_id, series), upper))
        elif ground is not None:
            skipped.append("Building %s (%s): single storey, ground plan only." % (building_id, series))
            sheet.append(("%s - %s (ground)" % (building_id, series), ground))
            per_building.append((building_id, series + " (ground, single storey)", ground))
        else:
            skipped.append("Building %s: no storey data (%s)." % (building_id, building.get("error")))
    if sheet:
        sheet_path = os.path.join(plans_dir, "contact_sheet_upper.png")
        render_contact_sheet(sheet, sheet_path)
        print("contact sheet -> %s" % sheet_path)
    md_path = os.path.join(out_dir, "kit_rooms.md")
    with open(md_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(render_rooms_md(per_building, skipped))
    print("rooms -> %s" % md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
