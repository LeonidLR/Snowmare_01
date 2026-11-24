"""Kit dressing, the pure part (no `unreal`): which Soviet-kit modules dress a generated building, where, and how much they are stretched
(docs/design/levelgen/KIT_DRESS.md). Scripts/Editor/kit_dress.py runs it in the editor; Scripts/Editor/tests/test_kit_dress.py tests it.

Input: one building of an accepted layout in its own (unrotated) frame, the builder's actor specs of that building (build_levelgen_map.py
plan(), turned back into the same frame) and a series of Content/Data/LevelGen/kit_series.json. The dressing reads everything generic
from the specs and the layout records - never a building id or a plan kind:
  - facade: per footprint side and storey, the layout's openings on that side (windows by width, ground doors = entrances, upper doors =
    balcony doors), the stairwell rooms that touch the side (a stair glass column), and the gaps the builder left in its wall pieces that
    no opening explains (ladder exits) - those stay open; solid fillers in between;
  - interior walls, sills and lintels: every builder piece tagged `interior`, tiled with the series' interior wall (stair paint on a
    stairwell room's boundary or inside it);
  - doors: partition openings (role unit_entry / room / enfilade; portals get none), leaves fitted to the opening, open / closed by the
    builder's effective state;
  - stairs: every ramp of a `stairwell` stair (labels <bid>_<stair id>[_b]): a two-ramp switchback with its `_midlanding` slab gets the kit
    switchback set, a single flight gets stacked kit step blocks;
  - parapets (also firing-gap sills): every builder piece tagged `parapet`;
  - floors / ceilings / roof: room records x the builder's slab pieces.
Output: instance dicts {mesh, mat, loc, rot [pitch, yaw, roll], scale, group, part, storey} and the labels of the builder pieces they replace
(kit_dress.py hides those: hidden in game + invisible, collision / navigation / tags unchanged). A piece nothing replaces stays visible.
Godot reference: none (UE-only).
"""
import math

STOREY = 300.0          # our storey (cm); every kit height is stretched to it
WALL_T_DEFAULT = 30.0   # exterior wall thickness when the builder's pieces do not say
JAMB_MARGIN = 3.0       # cm kept clear of a neighbour bay's hole when a filler slides behind its jamb
RECESS = 0.3            # cm: an overlapping filler sits this far behind the bays in front of it (no z-fighting)
FLOOR_CELL = 200.0      # cm: floor / ceiling finish planes (UV 0..1 per plane)
FLOOR_LIFT = 0.5        # cm above a slab top (below a slab bottom for ceilings)
STAIR_LIFT = 0.6        # cm: a kit stair's floor landing above our slab top
EDGE_TOL = 20.0         # cm: an opening / wall piece belongs to a footprint side within this distance of its line
STOREY_TOL = 50.0       # cm: an instance whose bottom lies up to this far under a storey's base counts on that storey (a balcony slab)
DOOR_ROLES_NO_LEAF = ("portal",)


# ---- the series table --------------------------------------------------------------------------------------------------------------

def series_for(building, table):
    """(name, series dict) of a building: its record's `series` when the table has it, else the table's archetype default; (None, None)
    when neither (the building stays grey box)."""
    series = table.get("series", {})
    name = building.get("series")
    if name and name in series:
        return name, series[name]
    name = table.get("archetype_default", {}).get(building.get("archetype"))
    if name and name in series:
        return name, series[name]
    return None, None


def mesh_path(table, rel):
    if not rel:
        return None
    return rel if rel.startswith("/") else table.get("kit_root", "/Game/SovietHouses/Meshes/") + rel


# ---- fitting rules (unit tested) ---------------------------------------------------------------------------------------------------

def clamp_stretch(target, size, max_dev=0.10):
    """The stretch of a `size` module towards `target`, clamped to 1 +- max_dev."""
    if size <= 0:
        return 1.0
    return max(1.0 - max_dev, min(1.0 + max_dev, target / size))


def choose_module(width, holes, max_dev=0.10):
    """The module for an opening `width` cm wide from candidate hole widths `holes`: the smallest hole that reaches the width with at
    most +max_dev stretch (so the kit hole never shows less than the gameplay opening), else the widest one. -> (index, stretch,
    jamb_each_side_cm, narrower_cm): the hole stretched by `stretch` is width + 2 x jamb (filled with wall), or `narrower_cm` short of it
    (the widest module at +max_dev is still narrower than the opening)."""
    if not holes:
        raise ValueError("no module candidates")
    order = sorted(range(len(holes)), key=lambda i: holes[i])
    pick = next((i for i in order if holes[i] * (1.0 + max_dev) >= width - 1e-6), order[-1])
    s = clamp_stretch(width, holes[pick], max_dev)
    vis = holes[pick] * s
    jamb = max(0.0, (vis - width) / 2.0)
    narrower = max(0.0, width - vis)
    return pick, s, (jamb if jamb > 0.5 else 0.0), (narrower if narrower > 0.5 else 0.0)


def tile_exact(a, b, module=300.0, min_len=2.0):
    """[a, b] in n modules of equal width (n = round(len / module), >= 1): [(start, width)]."""
    g = b - a
    if g < min_len:
        return []
    n = max(1, int(round(g / module)))
    w = g / n
    return [(a + i * w, w) for i in range(n)]


def tile_vertical(height, module_h):
    """A height in n stacked modules: (n, z stretch) with n = round(height / module_h) >= 1."""
    if height <= 0 or module_h <= 0:
        return 0, 1.0
    n = max(1, int(round(height / module_h)))
    return n, height / (n * module_h)


def fill(a, b, ext_l, ext_r, module=300.0, max_dev=0.10, mode="overlap"):
    """Solid filler modules over [a, b] (cm along the wall); a filler may slide ext_l / ext_r behind the neighbour bays' jambs.
    -> [(start, width, recess_cm)]. Exact tiling when a width within +-max_dev fits; else (mode overlap) full modules overlapping the
    neighbours and each other (alternately recessed); only a gap too small even for that is squished. mode squish: exact tiling of any
    width (contiguous, no overlap)."""
    g = b - a
    if g < 1.0:
        return []
    lo, hi = module * (1.0 - max_dev), module * (1.0 + max_dev)
    n = max(1, int(round(g / module)))
    if mode == "squish" or lo <= g / n <= hi:
        return [(a + i * g / n, g / n, 0.0) for i in range(n)]
    if g + ext_l + ext_r < module:
        return [(a - ext_l, g + ext_l + ext_r, RECESS)]
    span = max(module, g)
    extra = span - g
    xl = min(ext_l, extra / 2.0)
    xr = extra - xl
    if xr > ext_r:
        xr = ext_r
        xl = extra - xr
    start = a - xl
    n = max(1, int(math.ceil(span / module - 1e-6)))
    step = (span - module) / (n - 1) if n > 1 else 0.0
    return [(start + i * step, module, RECESS * (1.0 if i % 2 == 0 else 0.5)) for i in range(n)]


def door_plan(width, height, candidates, single_max=130.0, max_dev=0.10):
    """Leaves for a door opening: one leaf up to single_max cm, two above; the candidate (in table order) whose width fits a leaf within
    +-max_dev, else the one nearest below (jambs filled). -> {"leaves", "cand" (index), "sx", "sz", "leaf_w", "jamb", "head"}: each leaf is
    leaf_w wide (= candidate width x sx), `jamb` cm of wall each side and `head` cm over the leaves up to the opening top."""
    n = 1 if width <= single_max + 1e-6 else 2
    want = width / n
    pick = next((i for i, c in enumerate(candidates) if abs(want / c["width_cm"] - 1.0) <= max_dev + 1e-9), None)
    if pick is None:
        below = [i for i, c in enumerate(candidates) if c["width_cm"] <= want]
        pick = max(below, key=lambda i: candidates[i]["width_cm"]) if below else min(range(len(candidates)), key=lambda i: candidates[i]["width_cm"])
    c = candidates[pick]
    sx = clamp_stretch(want, c["width_cm"], max_dev)
    sz = clamp_stretch(height, c["height_cm"], max_dev)
    leaf_w = c["width_cm"] * sx
    jamb = max(0.0, (width - leaf_w * n) / 2.0)
    head = max(0.0, height - c["height_cm"] * sz)
    return {"leaves": n, "cand": pick, "sx": sx, "sz": sz, "leaf_w": leaf_w, "jamb": jamb if jamb > 0.5 else 0.0, "head": head if head > 0.5 else 0.0}


# ---- small geometry ----------------------------------------------------------------------------------------------------------------

def rot90(v):
    return (-v[1], v[0])


def unit(v):
    n = math.hypot(v[0], v[1])
    return (v[0] / n, v[1] / n) if n > 1e-9 else (1.0, 0.0)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def rot_xy(p, c, deg):
    if not deg:
        return [p[0], p[1]] + list(p[2:])
    r = math.radians(deg)
    dx, dy = p[0] - c[0], p[1] - c[1]
    return [c[0] + dx * math.cos(r) - dy * math.sin(r), c[1] + dx * math.sin(r) + dy * math.cos(r)] + list(p[2:])


def rect_and(a, b):
    r = (max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3]))
    return r if r[2] - r[0] > 1.0 and r[3] - r[1] > 1.0 else None


def rect_minus(a, b):
    """a without b: up to four rects."""
    i = rect_and(a, b)
    if i is None:
        return [a]
    out = []
    if i[1] > a[1]:
        out.append((a[0], a[1], a[2], i[1]))
    if i[3] < a[3]:
        out.append((a[0], i[3], a[2], a[3]))
    if i[0] > a[0]:
        out.append((a[0], i[1], i[0], i[3]))
    if i[2] < a[2]:
        out.append((i[2], i[1], a[2], i[3]))
    return [r for r in out if r[2] - r[0] > 1.0 and r[3] - r[1] > 1.0]


def cells(rect, size):
    x0, y0, x1, y1 = rect
    nx, ny = max(1, int(math.ceil((x1 - x0) / size - 1e-6))), max(1, int(math.ceil((y1 - y0) / size - 1e-6)))
    w, h = (x1 - x0) / nx, (y1 - y0) / ny
    return [(x0 + i * w, y0 + j * h, x0 + (i + 1) * w, y0 + (j + 1) * h) for i in range(nx) for j in range(ny)]


def interval_union(iv):
    out = []
    for a, b in sorted(iv):
        if out and a <= out[-1][1] + 0.5:
            out[-1] = (out[-1][0], max(out[-1][1], b))
        else:
            out.append((a, b))
    return out


def interval_minus(span, cuts, min_len=1.0):
    """span (a, b) without the cut intervals -> the pieces at least min_len long."""
    a, b = span
    out, cur = [], a
    for c0, c1 in interval_union(cuts):
        if c1 <= cur or c0 >= b:
            continue
        if c0 > cur:
            out.append((cur, min(c0, b)))
        cur = max(cur, c1)
    if b > cur:
        out.append((cur, b))
    return [p for p in out if p[1] - p[0] >= min_len]


class Edge:
    """A footprint side: d along it with rot90(d) = n (outward); s = distance along d from O; [lo, hi] the dressed range (the long sides
    run over the corners, the short ones stop at the long sides' inner faces)."""

    def __init__(self, name, line_pt, n, d, length, long_side, wall_t=WALL_T_DEFAULT):
        self.name, self.O, self.n, self.d, self.L, self.long = name, line_pt, n, d, length, long_side
        self.set_wall_t(wall_t)

    def set_wall_t(self, t):
        self.t = t
        e = t / 2.0
        self.lo, self.hi = (-e, self.L + e) if self.long else (e, self.L - e)

    def s_of(self, p):
        return (p[0] - self.O[0]) * self.d[0] + (p[1] - self.O[1]) * self.d[1]

    def perp(self, p):
        return abs((p[0] - self.O[0]) * self.n[0] + (p[1] - self.O[1]) * self.n[1])

    def at(self, s, out=0.0):
        return (self.O[0] + self.d[0] * s + self.n[0] * out, self.O[1] + self.d[1] * s + self.n[1] * out)


def footprint_rect(b):
    (x0, y0), (x1, y1) = b["footprint"]
    return min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)


def edges_of(b):
    x0, y0, x1, y1 = footprint_rect(b)
    long_x = (y1 - y0) >= (x1 - x0)  # the x = const sides are the long ones
    return [Edge("x0", (x0, y0), (-1.0, 0.0), (0.0, 1.0), y1 - y0, long_x),
            Edge("x1", (x1, y1), (1.0, 0.0), (0.0, -1.0), y1 - y0, long_x),
            Edge("y0", (x1, y0), (0.0, -1.0), (-1.0, 0.0), x1 - x0, not long_x),
            Edge("y1", (x0, y1), (0.0, 1.0), (1.0, 0.0), x1 - x0, not long_x)]


def edge_of_point(edges, p, tol=EDGE_TOL):
    best = min(edges, key=lambda e: e.perp(p))
    return best if best.perp(p) <= tol and -tol <= best.s_of(p) <= best.L + tol else None


def tag_value(tags, key):
    pre = key + "="
    for t in tags:
        t = str(t)
        if t.startswith(pre):
            return t[len(pre):]
    return None


def box_piece(sp):
    """A builder cube spec as a wall-like box: {a, b, d, len, thick, z0, z1, mid} (the cube's local X = along a -> b; pitch ignored)."""
    cx, cy, cz = sp["location"]
    sx, sy, sz = sp.get("scale", [1.0, 1.0, 1.0])
    L, T, H = sx * 100.0, sy * 100.0, sz * 100.0
    r = math.radians(float(sp.get("yaw", 0.0)))
    d = (math.cos(r), math.sin(r))
    a = (cx - d[0] * L / 2.0, cy - d[1] * L / 2.0)
    b = (cx + d[0] * L / 2.0, cy + d[1] * L / 2.0)
    return {"a": a, "b": b, "d": d, "len": L, "thick": T, "z0": cz - H / 2.0, "z1": cz + H / 2.0, "mid": (cx, cy), "w": T, "h": H}


def ramp_piece(sp, slab=20.0):
    """A builder ramp (sloped cube, the top face is the walk line) -> {foot, top, z0, z1, width, run}."""
    cx, cy, cz = sp["location"]
    L, W = sp["scale"][0] * 100.0, sp["scale"][1] * 100.0
    p = math.radians(float(sp.get("pitch", 0.0)))
    r = math.radians(float(sp.get("yaw", 0.0)))
    d = (math.cos(r), math.sin(r))
    run, rise = L * math.cos(p), L * math.sin(p)
    zc = cz + slab / 2.0
    return {"foot": (cx - d[0] * run / 2.0, cy - d[1] * run / 2.0), "top": (cx + d[0] * run / 2.0, cy + d[1] * run / 2.0),
            "z0": zc - rise / 2.0, "z1": zc + rise / 2.0, "width": W, "run": run, "d": d}


def slab_rect(sp):
    b = box_piece(sp)
    cx, cy = b["mid"]
    hx, hy = sp["scale"][0] * 50.0, sp["scale"][1] * 50.0
    return (cx - hx, cy - hy, cx + hx, cy + hy), b["z1"], b["z0"]


# ---- instances ---------------------------------------------------------------------------------------------------------------------

def inst(mesh, wx, wy, anchor, local=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), part="", group="main", mat=None):
    """A kit instance: mesh whose LOCAL point `local` (unscaled cm) lands on the world point `anchor`, local +X along wx and +Y along wy
    (unit 2D vectors). A left-handed pair (wy = -rot90(wx)) is a mirror: yaw from -wx and a negative X scale."""
    sx, sy, sz = scale
    r = rot90(wx)
    mirror = abs(r[0] - wy[0]) + abs(r[1] - wy[1]) > 1e-3
    ex = (-wx[0], -wx[1]) if mirror else wx
    yaw = math.degrees(math.atan2(ex[1], ex[0]))
    px = anchor[0] - wx[0] * local[0] * sx - wy[0] * local[1] * sy
    py = anchor[1] - wx[1] * local[0] * sx - wy[1] * local[1] * sy
    pz = anchor[2] - local[2] * sz
    return {"mesh": mesh, "mat": mat, "loc": [px, py, pz], "rot": [0.0, yaw, 0.0], "scale": [-sx if mirror else sx, sy, sz],
            "group": group, "part": part}


def plane(mesh, rect, z, mat, part, group="main", down=False):
    x0, y0, x1, y1 = rect
    return {"mesh": mesh, "mat": mat, "loc": [(x0 + x1) / 2.0, (y0 + y1) / 2.0, z], "rot": [0.0, 0.0, 180.0 if down else 0.0],
            "scale": [(x1 - x0) / 100.0, (y1 - y0) / 100.0, 1.0], "group": group, "part": part}


def storey_of_z(z, gz, floors, tol=STOREY_TOL):
    """The storey of a kit instance whose bottom (or, for a tall piece, centre) is at z: 0 = ground floor .. floors = the roof (the
    builder's roof slab is tagged floor=<floors>). The roof cutaway hides every storey above the selected operative's (designer bug
    2026-11-22): instances are grouped per storey into their own ISM components tagged building=<id> / floor=<n>."""
    return max(0, min(int(floors), int(math.floor((float(z) - float(gz) + tol) / STOREY))))


def globalize(instances, c, deg):
    """Instances planned in a rotated building's own frame turned into the world (location about c, yaw + deg)."""
    if not deg:
        return instances
    out = []
    for i in instances:
        i = dict(i)
        i["loc"] = rot_xy(i["loc"], c, deg)
        i["rot"] = [i["rot"][0], i["rot"][1] + deg, i["rot"][2]]
        out.append(i)
    return out


def spec_to_local(sp, c, deg):
    """A builder spec of a rotated building turned back into the building's frame (the inverse of the builder's _globalize_specs)."""
    if not deg:
        return sp
    sp = dict(sp)
    sp["location"] = rot_xy(sp["location"], c, -deg)
    sp["yaw"] = float(sp.get("yaw", 0.0)) - deg
    return sp


# ---- stairs (unit tested) ----------------------------------------------------------------------------------------------------------

def flight_mismatch(run, run_k, shift, half_rise):
    """Max vertical gap (cm) between our ramp (0 at t = 0 .. half_rise at t = run) and a kit flight of run run_k starting at t = shift (flat
    floor landing before it, flat half landing after it), over our ramp t in [0, run]."""
    def ours(t):
        return half_rise * t / run

    def kit(t):
        return half_rise * min(1.0, max(0.0, (t - shift) / run_k))
    ts = [t for t in (0.0, shift, shift + run_k, run) if 0.0 <= t <= run]
    return max(abs(ours(t) - kit(t)) for t in ts)


def switchback_fit(run, half_rise, cfg, back_room=None, mid_far=None, max_dev=0.10):
    """The kit switchback's stretch along the flights (sy) and its shift (cm along the first flight) for our flight of `run` cm: the least
    vertical mismatch between our ramp and the kit steps + 0.3 x the cm of our mid landing the kit half landing leaves uncovered, the
    kit floor landing kept within back_room cm behind our flight foot. -> (sy, shift, mismatch_cm, landing_short_cm)."""
    run_kit = cfg["turn_y"] - cfg["floor_edge_y"]
    back_kit = cfg["floor_edge_y"] - cfg["floor_landing_y"]
    land_kit = cfg["landing_end_y"] - cfg["floor_edge_y"]
    best = None
    n = int(round(max_dev * 2 / 0.005))
    for i in range(n + 1):
        sy = 1.0 - max_dev + i * 0.005
        for shift in range(-60, 61):
            if back_room is not None and back_kit * sy - shift > back_room + 1e-6:
                continue
            mis = flight_mismatch(run, run_kit * sy, float(shift), half_rise)
            short = max(0.0, mid_far - (shift + land_kit * sy)) if mid_far is not None else 0.0
            cost = mis + 0.3 * short + 0.001 * abs(shift)
            if best is None or cost < best[0] - 1e-9:
                best = (cost, sy, float(shift), mis, short)
    if best is None:   # no room at all: the shortest landing, flush with the limit
        sy = 1.0 - max_dev
        shift = back_kit * sy - back_room
        return sy, shift, flight_mismatch(run, run_kit * sy, shift, half_rise), 0.0
    return best[1], best[2], best[3], best[4]


def switchback_placement(pos, turn, top, rise, cfg, back_room=None, mid_far=None, max_dev=0.10):
    """The kit switchback set over a two-ramp switchback: flight a pos -> turn, flight b ... -> top (both floor-landing ends on one line).
    cfg: the series' stairs.switchback (kit frame: lane centres lane_x, floor-landing edge floor_edge_y, turn turn_y, half landing to
    landing_end_y, floor landing from floor_landing_y; flight 1 rises along +y). back_room: cm free behind `pos` (against the flight) for
    the kit's floor landing (None = no limit); mid_far: cm from pos to our mid landing's far edge.
    -> {wx, wy, anchor, anchor_local, scale (sx, sy, sz), notes, mismatch, landing_short}: the kit point anchor_local lands on the xy
    point `anchor` (pos moved `shift` cm along the flight)."""
    notes = []
    v = unit((turn[0] - pos[0], turn[1] - pos[1]))
    q = (top[0] - pos[0], top[1] - pos[1])
    lat = (q[0] - v[0] * dot(q, v), q[1] - v[1] * dot(q, v))
    lane = math.hypot(*lat)
    w = unit(lat) if lane > 1.0 else rot90(v)
    lane_kit = cfg["lane_x"][1] - cfg["lane_x"][0]
    run = math.hypot(turn[0] - pos[0], turn[1] - pos[1])
    sx = clamp_stretch(lane, lane_kit, max_dev) if lane > 1.0 else 1.0
    sy, shift, mis, short = switchback_fit(run, rise / 2.0, cfg, back_room, mid_far, max_dev)
    if abs(lane - lane_kit * sx) > 2.0:
        notes.append("lanes %.0f cm apart, kit %.0f (stretched %.0f)" % (lane, lane_kit, lane_kit * sx))
    if mis > 3.0 or short > 3.0:
        notes.append("kit set x %.3f along the flights, shifted %.0f cm: steps within %.0f cm of our ramp, %.0f cm of our mid landing past the kit's" % (
            sy, shift, mis, short))
    sz = rise / cfg["rise_cm"]
    return {"wx": w, "wy": v, "anchor": (pos[0] + v[0] * shift, pos[1] + v[1] * shift), "anchor_local": (cfg["lane_x"][0], cfg["floor_edge_y"], 0.0),
            "scale": (sx, sy, sz), "notes": notes, "mismatch": mis, "landing_short": short}


def flight_pieces(foot, top, z0, z1, width, flights, max_n=4):
    """Stacked kit step blocks over a single straight flight foot (z0) -> top (z1): the kit flight and count with the least stretch
    (|ln sz| + |ln sy|). flights: the series' stairs.flights (kit frame: rising along -y, foot at (centre_x, foot_y, foot_z), `rise_cm`
    over `run_cm`, `width_cm` across). -> {"cand", "n", "scale", "pieces": [(foot point xy, foot z)], "wx", "wy", "anchor_local"}."""
    run = math.hypot(top[0] - foot[0], top[1] - foot[1])
    rise = z1 - z0
    v = unit((top[0] - foot[0], top[1] - foot[1]))
    best = None
    for ci, f in enumerate(flights):
        for n in range(1, max_n + 1):
            sz = rise / n / f["rise_cm"]
            sy = run / n / f["run_cm"]
            cost = abs(math.log(sz)) + abs(math.log(sy))
            if best is None or cost < best[0] - 1e-9:
                best = (cost, ci, n, sy, sz)
    _, ci, n, sy, sz = best
    f = flights[ci]
    wy = (-v[0], -v[1])                 # kit +y points down the flight
    wx = (wy[1], -wy[0])                # rot90(wx) = wy: no mirror
    pieces = [((foot[0] + v[0] * run / n * i, foot[1] + v[1] * run / n * i), z0 + rise / n * i) for i in range(n)]
    return {"cand": ci, "n": n, "scale": (width / f["width_cm"], sy, sz), "pieces": pieces, "wx": wx, "wy": wy,
            "anchor_local": (f["centre_x"], f["foot_y"], f["foot_z"])}


# ---- the building dresser ----------------------------------------------------------------------------------------------------------

class Dresser:
    """Dresses ONE building (its own frame). layout / b: the (local) layout and building; specs: the builder's specs of this building
    (local frame); series / table: kit_series.json; state_of(opening) -> the effective opening state; extra_openings: the builder's
    synthesised openings (balcony doors); filler_mode overlap | squish."""

    def __init__(self, layout, b, specs, series_name, series, table, state_of, extra_openings=(), filler_mode="overlap"):
        self.layout, self.b, self.specs = layout, b, specs
        self.sname, self.S, self.T = series_name, series, table
        self.state_of = state_of
        self.extra = list(extra_openings)
        self.filler_mode = filler_mode
        self.fixed_bays = (b.get("plan") or {}).get("template") in ("block_1_464", "block_1_464_short")
        self.bid = b["id"]
        self.gz = float(layout.get("ground_z", 0.0))
        self.floors = int(b["floors"])
        self.max_dev = float(table.get("stretch_max", 0.10))
        self.sz = STOREY / float(series.get("storey_cm", 280.0))
        self.module = float(series.get("module_cm", 300.0))
        self.out = []
        self.consumed = set()
        self.notes = []
        self.counts = {}
        self.stretch = {}
        self.plane_mesh = table.get("plane_mesh", "/Engine/BasicShapes/Plane.Plane")
        self.edges = edges_of(b)
        # balcony door id -> balcony id: a planned building's story-4 door is a real opening (role balcony) without the builder's `balcony` field
        self.balcony_of_door = {x["door_id"]: x["id"] for x in (b.get("balconies") or []) if x.get("door_id")}
        self.stair_rects = []
        self.stair_rooms = {}
        for r in b.get("rooms", []) or []:
            if r.get("kind") == "stairwell":
                self.stair_rooms.setdefault(int(r["floor"]), []).append(r)

    # -- helpers
    def m(self, rel):
        return mesh_path(self.T, rel)

    def storey(self, z, group="main"):
        """Storey of an instance at z: the roof group is the roof storey (floors), the rest by height (storey_of_z)."""
        return self.floors if group == "roof" else storey_of_z(z, self.gz, self.floors)

    def add(self, mesh_rel, wx, wy, anchor, local=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), part="", group="main", mat=None, storey=None):
        mp = self.m(mesh_rel)
        if not mp:
            return None
        i = inst(mp, wx, wy, anchor, local, scale, part, group, mat)
        i["storey"] = storey if storey is not None else self.storey(i["loc"][2], group)
        self.out.append(i)
        self.counts[part] = self.counts.get(part, 0) + 1
        name = mp.rsplit("/", 1)[-1]
        self.stretch.setdefault(name, []).append((abs(scale[0]), scale[1], scale[2]))
        return i

    def add_plane(self, rect, z, mat, part, group="main", down=False, storey=None):
        i = plane(self.plane_mesh, rect, z, mat, part, group, down)
        i["storey"] = storey if storey is not None else self.storey(z, group)
        self.out.append(i)
        self.counts[part] = self.counts.get(part, 0) + 1

    def note(self, line):
        self.notes.append("%s: %s" % (self.bid, line))

    def tags(self, sp):
        return [str(t) for t in sp.get("tags", [])]

    def floor_of(self, sp):
        v = tag_value(self.tags(sp), "floor")
        if v is not None:
            try:
                return int(v)
            except ValueError:
                pass
        z = box_piece(sp)["z0"] - self.gz
        return int(math.floor(z / STOREY + 1e-3))

    # -- run
    def dress(self):
        self.classify()
        self.facade()
        self.interior()
        self.doors()
        self.stairs()
        self.parapets()
        self.finishes()
        return self

    def classify(self):
        """Sort the building's builder pieces: exterior facade pieces per (edge, floor), interior pieces, parapets, ramps, slabs, state
        actors per opening id."""
        self.ext = {}
        self.interior_pieces = []
        self.parapet_pieces = []
        self.slabs = []
        self.ramps = {}
        self.state_actors = {}
        self.mid_landings = {}
        stair_labels = {}
        for s in self.b.get("stairs", []) or []:
            base = "%s_%s" % (self.bid, s["id"])
            stair_labels[base] = (s, "a")
            stair_labels[base + "_b"] = (s, "b")
        for sp in self.specs:
            if sp.get("kind") not in ("cube", "cylinder"):
                continue
            tags = self.tags(sp)
            lab = sp["label"]
            if lab in stair_labels:
                s, fl = stair_labels[lab]
                self.ramps.setdefault(s["id"], {})[fl] = sp
                continue
            if "mid_landing" in tags or lab.endswith("_midlanding"):
                sid = tag_value(tags, "stair") or lab[len(self.bid) + 1:-len("_midlanding")]
                self.mid_landings[sid] = sp
                continue
            if "floor_slab" in tags or "roof_slab" in tags:
                self.slabs.append(sp)
                continue
            op = tag_value(tags, "opening")
            if op is not None and any(t in tags for t in ("door_leaf", "window_glass", "window_bars", "window_boards", "door_barricade", "opening_fill")):
                self.state_actors.setdefault(op, []).append(sp)
                continue
            if "parapet" in tags:
                self.parapet_pieces.append(sp)
                continue
            if "interior" in tags:
                self.interior_pieces.append(sp)
                continue
            if any(t in tags for t in ("balcony_slab", "balcony_rail", "hatch_rim", "snow_ramp", "flight_block", "rubble_flight", "stair_exit")):
                continue
            if sp.get("kind") != "cube" or abs(float(sp.get("pitch", 0.0))) > 0.5:
                continue
            bp = box_piece(sp)
            e = edge_of_point(self.edges, bp["mid"])
            if e is None or abs(dot(bp["d"], e.d)) < 0.99:
                continue
            f = self.floor_of(sp)
            s0, s1 = sorted((e.s_of(bp["a"]), e.s_of(bp["b"])))
            self.ext.setdefault((e.name, f), []).append({"s0": s0, "s1": s1, "z0": bp["z0"], "z1": bp["z1"], "thick": bp["thick"], "label": lab, "tags": tags})
        # the wall thickness per edge from its full-height pieces
        for e in self.edges:
            ts = [p["thick"] for (en, _), ps in self.ext.items() if en == e.name for p in ps if p["z1"] - p["z0"] > 200.0]
            if ts:
                e.set_wall_t(max(set(ts), key=ts.count))

    # -- facade ----------------------------------------------------------------------------------------------------------------
    def street_edge(self):
        sw = (self.b.get("plan") or {}).get("street_wall")
        if sw:
            w = next((w for w in self.layout.get("walls", []) if str(w.get("id", "")).startswith(sw)), None)
            if w:
                return edge_of_point(self.edges, ((w["a"][0] + w["b"][0]) / 2.0, (w["a"][1] + w["b"][1]) / 2.0))
        return None

    def ext_openings(self):
        """Exterior openings per (edge name, floor): (opening, s centre, width)."""
        out = {}
        for op in list(self.b.get("openings", []) or []) + self.extra:
            if op.get("kind") not in ("door", "window", "loophole"):
                continue
            e = edge_of_point(self.edges, op["pos"])
            if e is None:
                continue
            out.setdefault((e.name, int(op.get("floor", 0))), []).append((op, e.s_of(op["pos"]), float(op.get("width", 120.0))))
        return out

    def facade(self):
        S = self.S
        ops = self.ext_openings()
        street = self.street_edge()
        self.street = street
        nslot = int(S.get("slot_balconies", 0) or 0)
        if self.b.get("balconies"):
            # a story built real balconies on this building (story 4: balcony + fire escape on one slot column): no decorative ones beside them
            # (2026-11-22: a visual balcony next to the escape overlapped its bay, and balconies stand only where an escape is)
            nslot = 0
        slots = []
        for (en, f), lst in ops.items():
            for op, s, w in lst:
                if op.get("slot") == "balcony" and op.get("kind") == "window" and f >= 1:
                    e = next(x for x in self.edges if x.name == en)
                    slots.append((0 if street is not None and en == street.name else 1, -min(s, e.L - s), op["id"]))
        self.slot_balconies = {i for _, _, i in sorted(slots)[:nslot]}
        for e in self.edges:
            columns = self.columns_on(e, ops)
            entry_ranges = []
            for f in range(self.floors):
                z0 = self.gz + STOREY * f
                bays = self.bays_of(e, f, ops.get((e.name, f), []), columns)
                for b0, b1, jl, jr, kind, data in bays:
                    if kind == "entry" and f == 0:
                        entry_ranges.append((b0, b1, data))
                self.emit_floor(e, f, z0, bays, columns)
                for p in self.ext.get((e.name, f), []):
                    if not self.keep_piece(p, bays):
                        self.consumed.add(p["label"])
            for col in columns:
                self.emit_column(e, col)
            self.plinth(e, entry_ranges)
            self.cornice(e)
            self.drains(e, ops)

    def keep_piece(self, p, bays):
        """A facade piece stays visible when it frames a kept opening (a loophole) - everything else is replaced by the kit facade."""
        for b0, b1, _, _, kind, _ in bays:
            if kind == "keep" and p["s0"] >= b0 - 1.0 and p["s1"] <= b1 + 1.0:
                return True
        return False

    def columns_on(self, e, ops):
        """Stair glass columns on side e: one per stairwell room span touching it (on any storey), aligned on a ground entrance in that
        span; dropped when a window / upper door of the layout falls into it (the layout's openings win)."""
        spans = []
        for f, rooms in sorted(self.stair_rooms.items()):
            for r in rooms:
                (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
                on = [p for p in [(rx0, ry0), (rx1, ry0), (rx1, ry1), (rx0, ry1)] if e.perp(p) <= e.t / 2.0 + 2.0]
                if len(on) == 2:
                    sa, sb = sorted(e.s_of(p) for p in on)
                    if not any(abs(sa - x[0]) < 5.0 and abs(sb - x[1]) < 5.0 for x in spans):
                        spans.append((sa, sb))
        sc = self.S.get("stair_column")
        if not sc:
            return []
        ent = self.S.get("entrance") or {}
        hole = ent.get("hole") or [85, 215]
        cols = []
        for sa, sb in spans:
            entry = next(((op, s, w) for op, s, w in ops.get((e.name, 0), []) if op.get("kind") == "door" and sa - 1.0 <= s <= sb + 1.0), None)
            if entry:
                sx = clamp_stretch(entry[2], hole[1] - hole[0], self.max_dev)
                c = entry[1] - ((hole[0] + hole[1]) / 2.0 - self.module / 2.0) * sx
            else:
                sx, c = 1.0, (sa + sb) / 2.0
            c0, c1 = c - self.module / 2.0 * sx, c + self.module / 2.0 * sx
            clash = [op["id"] for (en, f), lst in ops.items() if en == e.name for op, s, w in lst
                     if not (entry and op is entry[0]) and s + w / 2.0 > c0 and s - w / 2.0 < c1]
            if clash:
                self.note("stair column on %s at s %.0f dropped: the layout has openings there (%s)" % (e.name, c, ", ".join(clash[:4])))
                continue
            if c0 < e.lo - 1.0 or c1 > e.hi + 1.0:
                self.note("stair column on %s at s %.0f dropped: past the wall end" % (e.name, c))
                continue
            cols.append({"c0": c0, "c1": c1, "sx": sx, "entry": entry})
        return cols

    def bays_of(self, e, f, ops, columns):
        """The bays of side e on storey f: (s0, s1, jamb_l, jamb_r, kind, data) sorted; kinds win / balc / entry / column / open / keep."""
        S = self.S
        bays = []
        wins = S.get("windows") or []
        ent = S.get("entrance") or {}
        bd = S.get("balcony_door") or {}
        mod = self.module
        paired_rooms = {
            op["links"][0] for op, _, _ in ops if op.get("links")
            and (op.get("role") == "balcony" or op.get("slot") == "balcony")
        } if self.fixed_bays and f > 0 else set()
        for op, c, w in ops:
            kind = op.get("kind")
            if kind == "loophole":
                bays.append((c - w / 2.0, c + w / 2.0, 0.0, 0.0, "keep", op))
                continue
            if self.fixed_bays and f > 0 and (op.get("role") == "balcony"
                    or op.get("slot") == "balcony"
                    or (kind == "window" and (op.get("links") or [None])[0] in paired_rooms)):
                bays.append((c - w / 2.0, c + w / 2.0, 0.0, 0.0, "fixed_opening", {"op": op, "c": c, "w": w}))
                continue
            if kind == "door" and f == 0 and ent:
                hole = ent["hole"]
                hw = hole[1] - hole[0]
                sx = w / hw if self.fixed_bays else clamp_stretch(w, hw, self.max_dev)
                start = c - (hole[0] + hole[1]) / 2.0 * sx
                bays.append((start, start + mod * sx, hole[0] * sx, (mod - hole[1]) * sx, "entry", {"op": op, "sx": sx}))
                continue
            if (kind == "door" or op.get("id") in self.slot_balconies) and bd:
                part = bd["door_hole"] if kind == "door" else bd["window_hole"]
                start = c - (part[0] + part[1]) / 2.0
                hw = part[1] - part[0]
                if kind == "door" and abs(w - hw) > hw * self.max_dev + 1.0:
                    self.note("balcony door %s %.0f cm, kit door %.0f cm (the kit wall narrows it)" % (op.get("id"), w, hw))
                bays.append((start, start + mod, bd["door_hole"][0], mod - bd["window_hole"][1], "balc",
                             {"op": op, "real": bool(op.get("balcony") or self.balcony_of_door.get(op.get("id"))), "visual": kind != "door"}))
                continue
            if kind == "door":
                self.note("door %s on storey %d of %s: no balcony_door modules in series %s - left grey" % (op.get("id"), f, e.name, self.sname))
                bays.append((c - w / 2.0, c + w / 2.0, 0.0, 0.0, "keep", op))
                continue
            if not wins:
                bays.append((c - w / 2.0, c + w / 2.0, 0.0, 0.0, "keep", op))
                continue
            idx, s, jamb, narrower = choose_module(w, [x["hole"][1] - x["hole"][0] for x in wins], self.max_dev)
            win = wins[idx]
            hc = (win["hole"][0] + win["hole"][1]) / 2.0
            start = c - hc * s
            if narrower:
                self.note("window %s %.0f cm: widest kit window %s shows %.0f cm less" % (op.get("id"), w, win["id"], narrower))
            hz = win.get("hole_z") or [85, 225]
            sill, top = float(op.get("sill_height", 90.0)), float(op.get("top_height", 210.0))
            if abs(hz[0] * self.sz - sill) > 15.0 or abs(hz[1] * self.sz - top) > 15.0:
                self.note("window %s: sill / top %.0f / %.0f, kit hole %.0f / %.0f" % (op.get("id"), sill, top, hz[0] * self.sz, hz[1] * self.sz))
            bays.append((start, start + mod * s, win["hole"][0] * s, (mod - win["hole"][1]) * s, "win",
                         {"op": op, "win": win, "s": s, "jamb": jamb, "w": w, "c": c}))
        for col in columns:
            if f == 0 and col["entry"] is not None:
                continue     # the entrance bay is the column's base
            bays.append((col["c0"], col["c1"], 0.0, 0.0, "column", col))
        # gaps in the builder's wall that no opening explains (ladder exits): left open
        pieces = self.ext.get((e.name, f), [])
        if pieces:
            covered = [(p["s0"], p["s1"]) for p in pieces]
            known = [(c - w / 2.0 - 2.0, c + w / 2.0 + 2.0) for _, c, w in ops]
            for g0, g1 in interval_minus((0.0, e.L), covered + known, min_len=20.0):
                bays.append((g0, g1, 0.0, 0.0, "open", None))
                self.note("open gap in the %s wall, storey %d, s %.0f..%.0f (no opening record: a ladder exit?) left open" % (e.name, f, g0, g1))
        bays.sort(key=lambda x: x[0])
        for i in range(1, len(bays)):
            if bays[i][0] < bays[i - 1][1] - 0.5:
                self.note("REVIEW: %s storey %d: bays overlap at s %.0f (openings closer than a module)" % (e.name, f, bays[i][0]))
        return bays

    def emit_floor(self, e, f, z0, bays, columns):
        S = self.S
        sz = self.sz
        mod = self.module
        t = e.t
        for k, (s0, s1, jl, jr, kind, data) in enumerate(bays):
            anchor = e.at(s0, t / 2.0) + (z0,)
            if kind == "fixed_opening":
                self.emit_fixed_opening(e, z0, s0, data)
            elif kind == "win":
                op = data["op"]
                win, s = data["win"], data["s"]
                state = self.state_of(op)
                self.add(win["wall"], e.d, e.n, anchor, scale=(s, 1.0, sz), part="facade_window_bay")
                if state in ("glazed", "fixed", None):
                    if data["jamb"]:
                        gs = data["w"] / (win["hole"][1] - win["hole"][0])
                        gc = e.at(data["c"] - (win["hole"][0] + win["hole"][1]) / 2.0 * gs, t / 2.0) + (z0,)
                        self.add(win["glass"], e.d, e.n, gc, scale=(gs, 1.0, sz), part="window")
                    else:
                        self.add(win["glass"], e.d, e.n, anchor, scale=(s, 1.0, sz), part="window")
                    self.consume_state(op, ("window_glass",))
                if data["jamb"]:
                    hz = win.get("hole_z") or [85, 225]
                    for js in (s0 + win["hole"][0] * s, s0 + win["hole"][1] * s - data["jamb"]):
                        p = e.at(js, t / 2.0)
                        self.add(S["solid"], e.d, e.n, (p[0], p[1], z0 + hz[0] * sz), scale=(data["jamb"] / mod, 1.0, (hz[1] - hz[0]) * sz / float(S.get("storey_cm", 280))), part="window_jamb_fill")
                self.radiator(e, data["c"], z0)
            elif kind == "balc":
                op = data["op"]
                bd = S["balcony_door"]
                self.add(bd["wall"], e.d, e.n, anchor, scale=(1.0, 1.0, sz), part="facade_balcony_bay")
                self.add(bd["glass"], e.d, e.n, anchor, scale=(1.0, 1.0, sz), part="balcony_door_window")
                self.consume_state(op, ("window_glass", "door_leaf"))
                if not data["visual"] and self.state_of(op) in ("closed", "locked"):
                    # doors R2 (designer 2026-11-22): a balcony / fire-escape door that can be shut gets a separate leaf in the door hole -
                    # the openable door lifts it onto its hinge (the glass unit stays static)
                    self.door_leaf_in_hole(e, s0 + bd["door_hole"][0], bd["door_hole"][1] - bd["door_hole"][0], z0, bd.get("leaf"), "balcony_door_leaf")
                if data["real"] or data["visual"]:
                    self.balcony(e, s0, z0, op, data["real"])
                self.radiator(e, s0 + (bd["window_hole"][0] + bd["window_hole"][1]) / 2.0, z0)
            elif kind == "entry":
                op = data["op"]
                ent = S["entrance"]
                sx = data["sx"]
                state = self.state_of(op)
                self.add(ent["wall"], e.d, e.n, anchor, scale=(sx, 1.0, sz), part="facade_entrance_bay")
                if state in ("closed", "locked"):
                    if ent.get("leaf"):
                        # doors R2 (designer 2026-11-22): the frame stays static (the bay's hole), a separate leaf fills the hole - the
                        # openable door lifts it onto its hinge (the combined frame + leaf module SM_SH_Ent_Door_* is no longer placed)
                        if ent.get("frame"):
                            self.add(ent["frame"], e.d, e.n, anchor, scale=(sx, 1.0, sz), part="entrance_frame")
                        hole = ent["hole"]
                        self.door_leaf_in_hole(e, s0 + hole[0] * sx, (hole[1] - hole[0]) * sx, z0, ent["leaf"], "entrance_leaf")
                    else:
                        self.add(ent["door"], e.d, e.n, anchor, scale=(sx, 1.0, sz), part="entrance_door")
                    self.consume_state(op, ("door_leaf",))
                col = next((c for c in columns if c["entry"] is not None and c["entry"][0] is op), None)
                if col is None:
                    sc = S.get("stair_column") or {}
                    strip_z = z0 + float(ent.get("height_cm", 230)) * sz
                    rest = z0 + STOREY - strip_z
                    if sc.get("strip") and rest > 1.0:
                        self.add(sc["strip"], e.d, e.n, (anchor[0], anchor[1], strip_z), scale=(sx, 1.0, rest / float(sc.get("strip_height_cm", 140))), part="entrance_top_strip")
                if f == 0:
                    if ent.get("porch"):
                        self.add(ent["porch"], e.d, e.n, (anchor[0], anchor[1], z0 + float(ent.get("porch_z_cm", 10))), scale=(sx, 1.0, 1.0), part="porch")
                    if ent.get("canopy"):
                        self.add(ent["canopy"], e.d, e.n, (anchor[0], anchor[1], z0 + float(ent.get("canopy_z_cm", 222)) * sz), scale=(sx, 1.0, 1.0), part="canopy")
        # solid fillers between the bays (a short side's end may hide in the long side it meets)
        end_ext = 0.0 if e.long else t
        cur, cur_jamb = e.lo, end_ext
        for k, (s0, s1, jl, jr, kind, data) in enumerate(bays + [(e.hi, e.hi, end_ext, end_ext, "end", None)]):
            if kind in ("open", "keep", "column"):
                jl = 0.0
            ext_l = max(0.0, cur_jamb - (JAMB_MARGIN if cur > e.lo else 0.0))
            ext_r = max(0.0, jl - (JAMB_MARGIN if kind != "end" else 0.0))
            if self.fixed_bays:
                self.emit_fixed_fill(e, cur, s0, z0)
            else:
                for fs, fw, recess in fill(cur, s0, ext_l, ext_r, mod, self.max_dev, self.filler_mode):
                    p = e.at(fs, t / 2.0 - recess)
                    self.add(S["solid"], e.d, e.n, (p[0], p[1], z0), scale=(fw / mod, 1.0, sz), part="facade_solid")
            cur = max(cur, s1)
            cur_jamb = 0.0 if kind in ("open", "keep", "column") else jr

    def facade_trim(self, e, start, width, z, height):
        # Engine cube is 100 cm per side. This is a cut panel, with the kit
        # material and exact dimensions, rather than a stretched facade module.
        if width <= 0.5 or height <= 0.5:
            return
        p = e.at(start + width / 2.0, 0.0)
        self.add("/Engine/BasicShapes/Cube.Cube", e.d, e.n, (p[0], p[1], z + height / 2.0),
                 scale=(width / 100.0, e.t / 100.0, height / 100.0), part="facade_trim", mat=self.S["fixed_bay_trim"]["material"])

    def emit_fixed_fill(self, e, start, end, z):
        # Keep the long-side corner caps out of the 75 cm bay trim widths.
        if start < 0.0:
            self.facade_trim(e, start, min(end, 0.0) - start, z, STOREY)
            start = 0.0
        if end > e.L:
            cap_start = max(start, e.L)
            self.facade_trim(e, cap_start, end - cap_start, z, STOREY)
            end = e.L
        while end - start >= self.module - 0.5:
            p = e.at(start, e.t / 2.0)
            self.add(self.S["solid"], e.d, e.n, (p[0], p[1], z), scale=(1.0, 1.0, self.sz), part="facade_solid")
            start += self.module
        if end - start <= 0.5:
            return
        count = math.ceil((end - start) / self.S["fixed_bay_trim"]["width_cm"])
        width = (end - start) / count
        for i in range(count):
            self.facade_trim(e, start + i * width, width, z, STOREY)

    def emit_fixed_opening(self, e, z, start, data):
        op, c, width = data["op"], data["c"], data["w"]
        sill = float(op.get("sill_height", 0.0))
        head = float(op.get("top_height", 210.0))
        self.facade_trim(e, start, width, z, sill)
        self.facade_trim(e, start, width, z + head, STOREY - head)
        state = self.state_of(op)
        if op.get("kind") == "door":
            bd = self.S["balcony_door"]
            if state in ("closed", "locked"):
                self.door_leaf_in_hole(e, start, width, z, bd.get("leaf"), "balcony_door_leaf")
                self.consume_state(op, ("door_leaf",))
            if op.get("balcony") or op.get("id") in self.balcony_of_door:
                self.balcony(e, start, z, op, True)
            return
        if state in ("glazed", "fixed", None):
            win = self.S["windows"][0]
            lo, hi = win["hole"]
            low_z, high_z = win["hole_z"]
            xs, zs = width / (hi - lo), (head - sill) / (high_z - low_z)
            anchor = e.at(c - (lo + hi) * xs / 2.0, e.t / 2.0) + (z + sill - low_z * zs,)
            self.add(win["glass"], e.d, e.n, anchor, scale=(xs, 1.0, zs), part="window")
            self.consume_state(op, ("window_glass",))
        self.radiator(e, c, z)

    def door_leaf_in_hole(self, e, s_hinge, width, z0, leaf, part):
        """A separate door leaf in a facade hole: leaf = {"mesh", "width_cm", "height_cm", "mat", "inset_cm"} (mesh pivot at its hinge corner,
        +X along the leaf, like the interior leaves); hinged at s_hinge, `width` cm wide (scaled), height_cm x storey scale, inset_cm in
        from the outer face. Doors R2: the openable door lifts the instance onto its hinge."""
        if not leaf or not leaf.get("mesh"):
            self.note("%s: no leaf mesh in series %s - the doorway stays without a leaf" % (part, self.sname))
            return None
        sx = width / float(leaf.get("width_cm", 80.0))
        sz = float(leaf.get("height_target_cm", leaf.get("height_cm", 205.0))) * self.sz / float(leaf.get("height_cm", 205.0))
        p = e.at(s_hinge, e.t / 2.0 - float(leaf.get("inset_cm", 9.5)))
        return self.add(leaf["mesh"], e.d, e.n, (p[0], p[1], z0), local=tuple(leaf.get("local", (0, 0, 0))), scale=(sx, 1.0, sz), part=part,
                        mat=leaf.get("mat"))

    def consume_state(self, op, kinds):
        for sp in self.state_actors.get(op.get("id"), []):
            if any(k in self.tags(sp) for k in kinds):
                self.consumed.add(sp["label"])

    def radiator(self, e, c, z0):
        rd = self.S.get("radiator")
        if not rd:
            return
        inner = (-e.n[0], -e.n[1])
        p = e.at(c, -e.t / 2.0)
        self.add(rd["mesh"], (inner[1], -inner[0]), inner, (p[0], p[1], z0), local=tuple(rd.get("local", (44, 4, 0))), part="radiator")

    def balcony(self, e, s0, z0, op, real):
        bd = self.S["balcony_door"]
        bx = bd.get("balcony_x") or [11, 289]
        bid = op.get("balcony") or self.balcony_of_door.get(op.get("id"))
        bal = next((x for x in self.b.get("balconies", []) or [] if x.get("id") == bid), None) if real else None
        anchor = e.at(s0, e.t / 2.0) + (z0,)
        scale = (1.0, 1.0, 1.0)
        if bal is not None:
            (ax, ay), (bx_, by_) = bal["rect"]
            corners = [(ax, ay), (bx_, ay), (bx_, by_), (ax, by_)]
            ss = [e.s_of(p) for p in corners]
            depth = max(e.perp(p) for p in corners) - e.t / 2.0
            sx = (max(ss) - min(ss)) / float(bx[1] - bx[0])
            sy = depth / float(bd.get("balcony_depth_cm", 102)) if depth > 10 else 1.0
            scale = (sx, sy, 1.0)
            anchor = e.at(min(ss) - bx[0] * sx, e.t / 2.0) + (z0,)
            for sp in self.specs:
                if tag_value(self.tags(sp), "balcony") == bal["id"]:
                    self.consumed.add(sp["label"])
        else:
            self.note("balcony (visual only, not reachable) at %s on %s" % (op.get("id"), e.name))
        self.add(bd["balcony"], e.d, e.n, anchor, scale=scale, part="balcony")

    def emit_column(self, e, col):
        sc = self.S["stair_column"]
        sz = self.sz
        top = self.gz + STOREY * self.floors
        if col["entry"] is not None:
            z = self.gz + float(self.S["entrance"].get("height_cm", 230)) * sz
        else:
            z = self.gz
        hm = float(sc.get("height_cm", 280)) * sz
        anchor = e.at(col["c0"], e.t / 2.0)
        while top - z >= hm - 1.0:
            st = self.storey(z + hm / 2.0)
            self.add(sc["wall"], e.d, e.n, (anchor[0], anchor[1], z), scale=(col["sx"], 1.0, sz), part="facade_stairwell_bay", storey=st)
            if sc.get("glass"):
                self.add(sc["glass"], e.d, e.n, (anchor[0], anchor[1], z), scale=(col["sx"], 1.0, sz), part="stairwell_glass", storey=st)
            z += hm
        if top - z > 1.0 and sc.get("strip"):
            self.add(sc["strip"], e.d, e.n, (anchor[0], anchor[1], z), scale=(col["sx"], 1.0, (top - z) / float(sc.get("strip_height_cm", 140))), part="stairwell_top_strip")

    def plinth(self, e, entry_ranges):
        pl = self.S.get("plinth")
        if not pl:
            return
        cuts = [(b0 + 30.0 * d["sx"], b1 - 30.0 * d["sx"]) for b0, b1, d in entry_ranges]
        lo, hi = (e.lo - 5.0, e.hi + 5.0) if e.long else (e.lo, e.hi)
        for a, b in interval_minus((lo, hi), cuts, min_len=2.0):
            for ps, pw in tile_exact(a, b, self.module):
                p = e.at(ps, e.t / 2.0 + 1.0)
                self.add(pl["mesh"], e.d, e.n, (p[0], p[1], self.gz - float(pl.get("height_cm", 90)) + float(pl.get("visible_cm", 45))),
                         scale=(pw / self.module, 1.0, 1.0), part="plinth")

    def cornice(self, e):
        cn = self.S.get("cornice")
        if not cn:
            return
        lo, hi = (e.lo - 5.0, e.hi + 5.0) if e.long else (e.lo, e.hi)
        z = self.gz + STOREY * self.floors - float(cn.get("z_cm", 0.0))
        for ps, pw in tile_exact(lo, hi, self.module):
            p = e.at(ps, e.t / 2.0)
            self.add(cn["mesh"], e.d, e.n, (p[0], p[1], z), scale=(pw / self.module, 1.0, 1.0), part="cornice", group="roof")

    def drains(self, e, ops):
        dr = self.S.get("drain")
        if not dr or not e.long:
            return
        busy = [(s - w / 2.0 - 40.0, s + w / 2.0 + 40.0) for (en, f), lst in ops.items() if en == e.name for _, s, w in lst]
        # one pipe length per storey (stretched to the storey): the roof cutaway hides the storeys above the operative's, a pipe
        # spanning storeys would stand over the cut
        n, sz = self.floors, STOREY / float(dr.get("height_cm", 698))
        for s in (e.lo + 35.0, e.L / 2.0, e.hi - 35.0):
            if any(a <= s <= b for a, b in busy):
                alts = [x for x in (s - 150.0, s + 150.0, s - 300.0, s + 300.0) if e.lo + 20 <= x <= e.hi - 20 and not any(a <= x <= b for a, b in busy)]
                if not alts:
                    continue
                s = alts[0]
            p = e.at(s, e.t / 2.0 + 0.5)
            for i in range(n):
                self.add(dr["mesh"], e.d, e.n, (p[0], p[1], self.gz + i * STOREY), scale=(1.0, 1.0, sz), part="drain", storey=i)

    # -- interior --------------------------------------------------------------------------------------------------------------
    def in_stairwell(self, p, f, margin=2.0):
        for r in self.stair_rooms.get(f, []):
            (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
            if rx0 - margin <= p[0] <= rx1 + margin and ry0 - margin <= p[1] <= ry1 + margin:
                return True
        return False

    def interior(self):
        walls = self.S.get("interior_walls") or []
        if not walls:
            return
        sw = self.S.get("stair_wall")
        for sp in self.interior_pieces:
            if sp.get("kind") != "cube" or abs(float(sp.get("pitch", 0.0))) > 0.5:
                continue
            bp = box_piece(sp)
            if bp["len"] < 2.0:
                continue
            f = self.floor_of(sp)
            stairy = sw is not None and self.in_stairwell(bp["a"], f) and self.in_stairwell(bp["b"], f)
            if stairy:
                wm = sw
            else:
                wm = min(walls, key=lambda x: abs(float(x.get("thickness_cm", 20)) - bp["thick"]))
            nn = rot90(bp["d"])
            h = bp["z1"] - bp["z0"]
            kz = h / float(wm.get("height_cm", 300))
            for ws, ww in tile_exact(0.0, bp["len"], self.module):
                p = (bp["a"][0] + bp["d"][0] * ws, bp["a"][1] + bp["d"][1] * ws)
                self.add(wm["mesh"], bp["d"], nn, (p[0], p[1], bp["z0"]), scale=(ww / self.module, 1.0, kz),
                         part="interior_stairwell_wall" if stairy else "interior_wall")
            self.consumed.add(sp["label"])

    def doors(self):
        D = self.S.get("doors") or {}
        if not D:
            return
        walls = self.S.get("interior_walls") or []
        for pt in self.b.get("partitions", []) or []:
            a, e_ = pt["start"], pt["end"]
            L = math.hypot(e_[0] - a[0], e_[1] - a[1])
            if L < 2.0:
                continue
            d = ((e_[0] - a[0]) / L, (e_[1] - a[1]) / L)
            nn = rot90(d)
            f = int(pt.get("floor", 0))
            z0 = self.gz + STOREY * f
            thick = float(pt.get("thickness", 20.0))
            wm = min(walls, key=lambda x: abs(float(x.get("thickness_cm", 20)) - thick)) if walls else None
            for op in pt.get("openings", []) or []:
                if op.get("kind") != "door":
                    continue
                role = op.get("role", "room")
                if role in DOOR_ROLES_NO_LEAF:
                    continue
                cands = D.get(role) or D.get("room") or []
                if not cands:
                    continue
                state = self.state_of(dict(op, floor=f))
                if state in ("barricaded", "sealed"):
                    continue    # the builder's blocking pieces stay visible
                w = float(op.get("width", 120.0))
                top = float(op.get("top_height", 210.0))
                s = (op["pos"][0] - a[0]) * d[0] + (op["pos"][1] - a[1]) * d[1]
                g0, g1 = s - w / 2.0, s + w / 2.0
                at = lambda t, _a=a, _d=d: (_a[0] + _d[0] * t, _a[1] + _d[1] * t)  # noqa: E731
                closed = state in ("closed", "locked")
                plan = door_plan(w, top, cands, float(D.get("single_max_cm", 130)), self.max_dev)
                c = cands[plan["cand"]]
                if c.get("framed") and plan["leaves"] == 1 and closed:
                    p = at(s)
                    self.add(c["mesh"], d, nn, (p[0], p[1], z0), local=tuple(c.get("local", (0, 0, 0))), scale=(plan["sx"], 1.0, plan["sz"]), part="flat_door")
                elif c.get("framed"):
                    # a framed door cannot swing: an open one (or a pair) takes the plain leaf candidate
                    alt = [x for x in cands if not x.get("framed")]
                    if not alt:
                        continue
                    plan = door_plan(w, top, alt, float(D.get("single_max_cm", 130)), self.max_dev)
                    c = alt[plan["cand"]]
                    self.leaves(at, d, nn, z0, g0, g1, plan, c, closed)
                else:
                    self.leaves(at, d, nn, z0, g0, g1, plan, c, closed)
                if wm is not None:
                    if plan["jamb"]:
                        for js in (g0, g1 - plan["jamb"]):
                            p = at(js)
                            self.add(wm["mesh"], d, nn, (p[0], p[1], z0), scale=(plan["jamb"] / self.module, 1.0, top / float(wm.get("height_cm", 300))), part="door_jamb_fill")
                    if plan["head"]:
                        p = at(g0)
                        hz = c["height_cm"] * plan["sz"]
                        self.add(wm["mesh"], d, nn, (p[0], p[1], z0 + hz), scale=(w / self.module, 1.0, plan["head"] / float(wm.get("height_cm", 300))), part="door_head_fill")
                for sp in self.state_actors.get(op.get("id"), []):
                    if "door_leaf" in self.tags(sp):
                        self.consumed.add(sp["label"])

    def leaves(self, at, d, nn, z0, g0, g1, plan, c, closed):
        n = plan["leaves"]
        for li in range(n):
            hinge = g0 + plan["jamb"] if li == 0 else g1 - plan["jamb"]
            p = at(hinge)
            along = d if li == 0 else (-d[0], -d[1])
            wx = along if closed else nn
            self.add(c["mesh"], wx, rot90(wx), (p[0], p[1], z0), local=tuple(c.get("local", (0, 0, 0))), scale=(plan["sx"], 1.0, plan["sz"]),
                     part="door_leaf_closed" if closed else "door_leaf_open")

    # -- stairs ----------------------------------------------------------------------------------------------------------------
    def back_room(self, pos, v):
        """cm free behind pos against v up to the building's outer face (minus 1 cm): None when the ray leaves through no wall within 6 m."""
        best = None
        for e in self.edges:
            k = dot((-v[0], -v[1]), e.n)
            if k < 0.7:
                continue
            dist = e.perp(pos) / k
            if dist <= 600.0:
                best = dist + e.t / 2.0 - 1.0 if best is None else min(best, dist + e.t / 2.0 - 1.0)
        return best

    def stairs(self):
        st = self.S.get("stairs") or {}
        for s in self.b.get("stairs", []) or []:
            if s.get("kind") != "stairwell":
                continue
            r = self.ramps.get(s["id"], {})
            if not r:
                continue
            if s.get("blocked"):
                self.note("stair %s blocked (%s): the builder's flight left grey" % (s["id"], s["blocked"]))
                continue
            if "a" in r and "b" in r and st.get("switchback"):
                ra, rb = ramp_piece(r["a"]), ramp_piece(r["b"])
                cfg = st["switchback"]
                mid_far = None
                ml = self.mid_landings.get(s["id"])
                v = unit((ra["top"][0] - ra["foot"][0], ra["top"][1] - ra["foot"][1]))
                if ml is not None:
                    rect, _, _ = slab_rect(ml)
                    mid_far = max(dot((x - ra["foot"][0], y - ra["foot"][1]), v) for x in (rect[0], rect[2]) for y in (rect[1], rect[3]))
                pl = switchback_placement(ra["foot"], ra["top"], rb["top"], rb["z1"] - ra["z0"], cfg, self.back_room(ra["foot"], v), mid_far, self.max_dev)
                for n_ in pl["notes"]:
                    self.note("stair %s: %s" % (s["id"], n_))
                self.add(cfg["mesh"], pl["wx"], pl["wy"], (pl["anchor"][0], pl["anchor"][1], ra["z0"] + STAIR_LIFT), local=pl["anchor_local"], scale=pl["scale"],
                         part="stair_switchback_set")
                self.consumed.update((r["a"]["label"], r["b"]["label"]))
                # our mid landing slab stays visible where the kit half landing does not reach (no invisible floor under a turning operative)
                if ml is not None and pl["landing_short"] <= 3.0:
                    self.consumed.add(ml["label"])
            elif st.get("flights"):
                for key in sorted(r):
                    rp = ramp_piece(r[key])
                    fp = flight_pieces(rp["foot"], rp["top"], rp["z0"], rp["z1"], rp["width"], st["flights"])
                    f = st["flights"][fp["cand"]]
                    for (pxy, pz) in fp["pieces"]:
                        self.add(f["mesh"], fp["wx"], fp["wy"], (pxy[0], pxy[1], pz + STAIR_LIFT), local=fp["anchor_local"], scale=fp["scale"], part="stair_flight_block")
                    self.consumed.add(r[key]["label"])
                    self.stair_rects.append(self.flight_rect(rp))
                continue
            self.stair_rects.append(self.core_rect(s, r))

    def flight_rect(self, rp):
        d = unit((rp["top"][0] - rp["foot"][0], rp["top"][1] - rp["foot"][1]))
        n = rot90(d)
        hw = rp["width"] / 2.0 + 5.0
        pts = [(p[0] + n[0] * k, p[1] + n[1] * k) for p in (rp["foot"], rp["top"]) for k in (-hw, hw)]
        return (min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts))

    def core_rect(self, s, r):
        pts = []
        for sp in r.values():
            rp = ramp_piece(sp)
            x = self.flight_rect(rp)
            pts += [(x[0], x[1]), (x[2], x[3])]
        ml = self.mid_landings.get(s["id"])
        if ml is not None:
            rect, _, _ = slab_rect(ml)
            pts += [(rect[0], rect[1]), (rect[2], rect[3])]
        return (min(x for x, _ in pts), min(y for _, y in pts), max(x for x, _ in pts), max(y for _, y in pts))

    # -- parapets / finishes ---------------------------------------------------------------------------------------------------
    def parapets(self):
        pp = self.S.get("parapet")
        if not pp:
            return
        cx = sum(footprint_rect(self.b)[i] for i in (0, 2)) / 2.0
        cy = sum(footprint_rect(self.b)[i] for i in (1, 3)) / 2.0
        for sp in self.parapet_pieces:
            bp = box_piece(sp)
            if bp["len"] < 2.0:
                continue
            e = edge_of_point(self.edges, bp["mid"], tol=40.0)
            if e is not None and abs(dot(bp["d"], e.d)) > 0.99:
                s0, s1 = sorted((e.s_of(bp["a"]), e.s_of(bp["b"])))
                # a piece ending at a corner: the long sides run over it, the short ones stop at the long sides' kit parapet
                if s0 <= 25.0:
                    s0 = e.lo - 5.0 if e.long else max(s0, e.lo + 5.0)
                if s1 >= e.L - 25.0:
                    s1 = e.hi + 5.0 if e.long else min(s1, e.hi - 5.0)
                d, n = e.d, e.n
                start = lambda s, _e=e: _e.at(s, _e.t / 2.0)  # noqa: E731
            else:
                d = bp["d"]
                n = rot90(d)
                if dot((bp["mid"][0] - cx, bp["mid"][1] - cy), n) < 0:
                    d, n = (-d[0], -d[1]), (-n[0], -n[1])
                a = bp["a"] if dot(bp["d"], d) > 0 else bp["b"]
                s0, s1 = 0.0, bp["len"]
                start = lambda s, _a=a, _d=d, _n=n, _t=bp["thick"]: (_a[0] + _d[0] * s + _n[0] * _t / 2.0, _a[1] + _d[1] * s + _n[1] * _t / 2.0)  # noqa: E731
            h = bp["z1"] - bp["z0"]
            nv, kz = tile_vertical(h, float(pp.get("height_cm", 100)))
            for ps, pw in tile_exact(s0, s1, self.module):
                p = start(ps)
                for i in range(nv):
                    self.add(pp["mesh"], d, n, (p[0], p[1], bp["z0"] + i * float(pp.get("height_cm", 100)) * kz), scale=(pw / self.module, 1.0, kz), part="parapet", group="roof")
            self.consumed.add(sp["label"])

    def finishes(self):
        F = self.S.get("floors") or {}
        slabs = {}
        bottoms = {}
        for sp in self.slabs:
            lvl = tag_value(self.tags(sp), "floor")
            if lvl is None:
                continue
            rect, top, bot = slab_rect(sp)
            slabs.setdefault(int(lvl), []).append(rect)
            bottoms.setdefault(int(lvl), []).append((rect, bot))
        for room in self.b.get("rooms", []) or []:
            f = int(room["floor"])
            (rx0, ry0), (rx1, ry1) = room["bounds"]["min"], room["bounds"]["max"]
            base = [(rx0, ry0, rx1, ry1)]
            if f > 0:
                base = [i for r in base for s in slabs.get(f, []) for i in [rect_and(r, s)] if i]
            if room.get("kind") == "stairwell":
                for sr in self.stair_rects:
                    base = [x for r in base for x in rect_minus(r, sr)]
            mat = F.get(room.get("kind"), F.get("default"))
            if mat:
                for r in base:
                    for c in cells(r, FLOOR_CELL):
                        self.add_plane(c, self.gz + STOREY * f + FLOOR_LIFT, mat, "floor_plane")
            cm = self.S.get("ceiling")
            if cm:
                for rect, bot in bottoms.get(f + 1, []):
                    i = rect_and((rx0, ry0, rx1, ry1), rect)
                    if i:
                        for c in cells(i, FLOOR_CELL):
                            self.add_plane(c, bot - FLOOR_LIFT, cm, "ceiling_plane", down=True, storey=min(f + 1, self.floors))
        roof = self.S.get("roof") or {}
        rz = self.gz + STOREY * self.floors + FLOOR_LIFT
        if roof.get("material"):
            for r in slabs.get(self.floors, []):
                for c in cells(r, FLOOR_CELL * 1.5):
                    self.add_plane(c, rz, roof["material"], "roof_plane", group="roof")
        x0, y0, x1, y1 = footprint_rect(self.b)
        long_y = (y1 - y0) >= (x1 - x0)
        roof_rects = slabs.get(self.floors, [])
        for pr in roof.get("props", []) or []:
            u, t = pr["at"]
            p = (x0 + (x1 - x0) * u, y0 + (y1 - y0) * t) if long_y else (x0 + (x1 - x0) * t, y0 + (y1 - y0) * u)
            if not any(r[0] + 100 <= p[0] <= r[2] - 100 and r[1] + 100 <= p[1] <= r[3] - 100 for r in roof_rects):
                continue
            self.add(pr["mesh"], (1.0, 0.0), (0.0, 1.0), (p[0], p[1], rz), part="roof_prop", group="roof")

    def stats(self):
        rows = []
        for name, ss in sorted(self.stretch.items()):
            xs, zs = [s[0] for s in ss], [s[2] for s in ss]
            far = sum(1 for s in ss if abs(s[0] - 1.0) > self.max_dev + 0.005 or abs(s[2] - 1.0) > self.max_dev + 0.005)
            rows.append({"mesh": name, "count": len(ss), "sx": [round(min(xs), 3), round(max(xs), 3)], "sz": [round(min(zs), 3), round(max(zs), 3)], "beyond_max": far})
        return rows
