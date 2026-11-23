"""KIT PILOT (game designer look test 2026-11-20, docs/design/levelgen/KIT_PILOT_2026-11-20.md): ONE generated residential block dressed
with the modules of the Soviet building asset pack (Content/SovietHouses, untracked) instead of the grey boxes, on its own test map
/Game/Maps/KitPilot/L_KitPilot. A look test only: the generator and Scripts/Editor/build_levelgen_map.py are NOT changed - this script
loads the builder's source as a module (without its main()), plans the accepted layout's actors with it and post-processes the specs of
one building (default B_x of gen_bgen_plans_check.json, the designer test map L_Gen_BgenPlans):

  - the building's own grey-box actors (facade walls with their sills / lintels / door leaves, partitions, parapets, stair ramps) STAY as
    the collision + navmesh proxies, exactly as the generator builds them, but are HIDDEN (hidden in game + component invisible): cover,
    sight lines, mantle edges (tags), door states and the walking ramps are the generator's, bit for bit; the window glass panes (visual
    only) are dropped - the kit window has glass;
  - over them the kit visuals (no collision, CanEverAffectNavigation false): facade bays per opening (window / balcony / entrance bay with
    porch + canopy + steel door), kit windows in every window opening, stairwell glass bays on the core wall, solid fillers between bays,
    plinth, parapet, drain pipes, radiators under the windows, interior walls from IntWall modules (stairwell walls in stair paint),
    flat doors (SM_SH_Door_Apt) in the unit_entry openings, double interior door leaves (SM_SH_Door_Int, open) in the room doors,
    the kit switchback stair (SM_SH_Stair_mid_28_9) stacked per storey over the stair core, floor finishes (parquet / tile planes),
    a bitumen roof with vents and an antenna, two visual balconies on the street side (on the layout's balcony slots);
  - everything else of the layout (ground, other walls, covers, routes, spawns) is the builder's grey box; the other buildings are left
    out. Lighting / PlayerStart / NavMeshBoundsVolume / level JSON exactly as build_levelgen_map.py makes them (level JSON
    Content/Data/LevelJson/L_KitPilot.json points to the ORIGINAL accepted layout, so CodexTactics.LevelGenBuildingsSmoke only=B_x reads
    the same building; its environment is locked Clear 13:00 for the pictures).

Storey: OUR 3 m and grid (designer decision); kit modules are 300 cm wide x 280 cm (stretched z x 300/280 = 1.0714), interior walls 300
tall (z 1.0). Nothing is scaled more than ~10 % except where the layout's sizes leave no choice (all listed in the report: filler squish,
interior wall pieces, door jambs / lintels, the 54 cm strip over an entrance, the stairwell paint walls 260 -> 300).

Modes (env KIT_PILOT_MODE):
  build (default, editor commandlet):  UnrealEditor-Cmd.exe <uproject> -run=pythonscript "-Script=<this file>" -unattended -nullrhi
      -> the map, Content/Data/LevelJson/L_KitPilot.json, Saved/KitPilot/kit_pilot_report.json, Saved/Logs/KitPilotLayout.txt.
      Then bake: Scripts/bake_navmesh.ps1 -Map /Game/Maps/KitPilot/L_KitPilot
  shots (in game, rendered; copy this file to a path WITHOUT spaces first - `py <file>` from -ExecCmds cannot take one):
      UnrealEditor.exe <uproject> /Game/Maps/KitPilot/L_KitPilot -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosound -unattended
        -ExecCmds="py C:\\Temp\\kit_pilot_building.py"      (with KIT_PILOT_MODE=shots in the environment)
      -> HighResShot pictures from the map's own CameraActor (tag KitPilotCam), copied to Saved/KitPilot/shots/.
  plan (system python): `python kit_pilot_building.py plan` prints the spec counts and writes Saved/KitPilot/kit_pilot_plan.json.
Env: KIT_PILOT_LAYOUT (accepted layout), KIT_PILOT_BUILDING (building id), KIT_PILOT_TARGET (map path, must be under /Game/Maps/KitPilot/).
Never touches another map. Godot reference: none (UE-only).
"""
import datetime
import json
import math
import os
import shutil
import sys
import types

try:
    import unreal
except ImportError:  # system python: the `plan` mode only
    unreal = None


def _project_dir():
    if unreal is not None:
        return os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    return os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


PROJECT_DIR = _project_dir()
MODE = os.environ.get("KIT_PILOT_MODE", "build")
LAYOUT_FILE = os.environ.get("KIT_PILOT_LAYOUT") or os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_bgen_plans_check.json")
BUILDING_ID = os.environ.get("KIT_PILOT_BUILDING", "B_x")
TARGET = os.environ.get("KIT_PILOT_TARGET", "/Game/Maps/KitPilot/L_KitPilot")
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "KitPilot")
SHOTS_DIR = os.path.join(OUT_DIR, "shots")
KIT_ROOT = "/Game/SovietHouses/Meshes/"
KIT_MATERIALS = "/Game/SovietHouses/Materials"
PLANE = "/Engine/BasicShapes/Plane.Plane"
FILLER_MODE = os.environ.get("KIT_PILOT_FILLER", "overlap")   # overlap (<= 10 % where it fits; seams) | squish (contiguous, any width)
SHOT_PREFIX = os.environ.get("KIT_PILOT_SHOT_PREFIX", "kitpilot_")
SHOT_ONLY = [s for s in os.environ.get("KIT_PILOT_SHOT_ONLY", "").split(",") if s]
CAM_TAG = "KitPilotCam"
KIT_TAG = "KitPilot"

STOREY = 300.0                 # our storey (cm)
KIT_STOREY = 280.0             # the kit's panel storey
SZ = STOREY / KIT_STOREY       # 1.0714: every 28-series module's height stretch
WALL_T = 30.0                  # our exterior walls (the generator's 30 cm, centred on the footprint line) = the kit's facade depth
MODULE_W = 300.0               # the kit's facade / interior module width
FILL_MAX = 330.0               # +10 %
FILL_MIN = 270.0               # -10 %
JAMB_MARGIN = 3.0              # cm kept clear of a neighbour bay's cut-out when a filler slides behind its jamb
RECESS = 0.3                   # cm: an overlapping filler sits this far behind the bays in front of it (no z-fighting; 1 cm drew a dark seam)
FLOOR_CELL = 200.0             # cm: floor finish planes (UV 0..1 per plane)
FLOOR_LIFT = 0.5               # cm above the slab top
STAIR_LIFT = 0.6               # cm: the kit stair's floor landing above our slab top (no z-fighting)
STAIR_SX = 1.1                 # kit stair 280 cm across the two flights -> our 2 x 150 cm lanes + gap (310)
ENTRY_SX = 1.1                 # entrance module: its 130 cm door hole -> 143 cm in our 150 cm door
DOOR_APT_S = 1.1               # flat door 102 x 214 -> 112 x 235 (the layout's unit_entry doors are 150 x 240)
DOOR_INT_SZ = 1.1              # interior leaf 205 -> 225.5 tall

# Kit modules by role (local frames read from Saved/KitDump/kit_buildings.json + the probe of their vertices: facade modules x 0..300 along
# the wall, y -30..0 with the OUTER face at y = 0 (local +Y = outward), z 0..280; holes: win 75..225 x 85..225, balc door 35..110 + window
# 130..260, StairEnt door 85..215 x 0..205, StairWall_28_w glass 90..210 x 90..210; interior walls x 0..300, y -10..10, z 0..300).
M = {
    "win_wall": "Facade/SM_SH_Wall_Facade_win_28",
    "wide_wall": "Facade/SM_SH_Wall_Facade_wide_28",
    "solid": "Facade/SM_SH_Wall_Facade_solid_28",
    "balc_wall": "Facade/SM_SH_Wall_Facade_balc_28",
    "ent_wall": "Facade/SM_SH_StairEnt_Facade_23",
    "strip": "Facade/SM_SH_StairWall_Facade_14_x",
    "stair_wall": "Facade/SM_SH_StairWall_Facade_28_w",
    "plinth": "Facade/SM_SH_Plinth_Con_9",
    "parapet": "Facade/SM_SH_Parapet_Facade_10",
    "win": "Openings/SM_SH_Win_PVC_win_28",
    "win_wide": "Openings/SM_SH_Win_PVC_wide_28",
    "win_balc": "Openings/SM_SH_Win_PVC_balc_28",
    "stair_glass": "Openings/SM_SH_StairGlass_PVC",
    "ent_door": "Openings/SM_SH_Ent_Door_Steel",
    "door_apt": "Openings/SM_SH_Door_Apt",
    "door_int": "Openings/SM_SH_Door_Int",
    "int_wall": "Interior/SM_SH_IntWall_apt_30",
    "stair_int_wall": "Interior/SM_SH_IntWall_stair_26",
    "stair": "Interior/SM_SH_Stair_mid_28_9",
    "balcony": "Balconies/SM_SH_Balc_Panel",
    "porch": "Props/SM_SH_Ent_Porch",
    "canopy": "Props/SM_SH_Ent_Canopy",
    "drain": "Props/SM_SH_Drain_70",
    "radiator": "Props/SM_SH_Radiator",
    "vent": "Roof/SM_SH_Roof_Vent",
    "antenna": "Roof/SM_SH_Roof_Antenna",
}
JAMB = {"win": 75.0, "wide": 41.0, "balc": (35.0, 40.0), "entry": 85.0 * ENTRY_SX, "stairglass": 90.0}
FLOOR_MAT = {"room": "MI_SH_Parquet", "lobby": "MI_SH_Tile", "corridor": "MI_SH_Tile", "stairwell": "MI_SH_Tile"}
FLOOR_MAT_DEFAULT = "MI_SH_Lino"
ROOF_MAT = "MI_SH_Bitumen"

REPORT = []
STATS = {}
COUNTS = {}


def report(line):
    REPORT.append(str(line))
    if unreal is None:
        print(line, file=sys.stderr)


# ---- the generator's builder as a library (its main() is not run) ------------------------------------------------------------------

def load_builder():
    path = os.path.join(PROJECT_DIR, "Scripts", "Editor", "build_levelgen_map.py")
    with open(path, encoding="utf-8") as f:
        src = f.read()
    tail = '\nif unreal is not None or __name__ == "__main__":\n    main()'
    if tail not in src:
        raise RuntimeError("build_levelgen_map.py: its main() guard changed - update kit_pilot_building.load_builder")
    os.environ["CODEX_LEVELGEN_LAYOUT"] = LAYOUT_FILE
    os.environ["CODEX_LEVELGEN_TARGET"] = TARGET
    sys.path.insert(0, os.path.dirname(path))
    mod = types.ModuleType("build_levelgen_map_kitpilot")
    mod.__file__ = path
    exec(compile(src.replace(tail, "\n"), path, "exec"), mod.__dict__)
    return mod


def pilot_layout(layout, bid):
    """The layout with only building `bid` (and nothing that stands on / leads onto the others)."""
    drop = {b["id"] for b in layout["buildings"]} - {bid}

    def on_dropped(v):
        s = str(v or "")
        return any(s == "roof:%s" % d or s.startswith("floor:%s:" % d) for d in drop)

    out = dict(layout)
    out["buildings"] = [b for b in layout["buildings"] if b["id"] == bid]
    if not out["buildings"]:
        raise ValueError("building %s not in %s" % (bid, LAYOUT_FILE))
    out["walls"] = [w for w in layout["walls"] if w.get("building") not in drop]
    out["ladders"] = [ld for ld in layout.get("ladders", []) if not (on_dropped(ld.get("from")) or on_dropped(ld.get("to")))]
    out["mantle_edges"] = [m for m in layout.get("mantle_edges", []) or [] if m.get("building") not in drop]
    out["firing_gaps"] = [g for g in layout.get("firing_gaps", []) or [] if g.get("building") not in drop]
    out["ambush_points"] = [a for a in layout.get("ambush_points", []) or [] if not on_dropped(a.get("layer"))]
    return out


# ---- small geometry -------------------------------------------------------------------------------------------------------------

def rot90(v):
    return (-v[1], v[0])


def vadd(*vs):
    return tuple(sum(c) for c in zip(*vs))


def vmul(v, k):
    return tuple(c * k for c in v)


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


# ---- kit specs ------------------------------------------------------------------------------------------------------------------

def kit(label, folder, key, wx, wy, anchor, local=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), tags=(), part=""):
    """A kit visual: mesh M[key] whose LOCAL point `local` (unscaled cm) lands on the world point `anchor`, local +X along wx and +Y along
    wy (unit 2D vectors). A left-handed pair (wy = -rot90(wx)) is a mirror: yaw from -wx and a negative X scale."""
    sx, sy, sz = scale
    r = rot90(wx)
    mirror = abs(r[0] - wy[0]) + abs(r[1] - wy[1]) > 1e-3
    ex = (-wx[0], -wx[1]) if mirror else wx
    yaw = math.degrees(math.atan2(ex[1], ex[0]))
    px = anchor[0] - wx[0] * local[0] * sx - wy[0] * local[1] * sy
    py = anchor[1] - wx[1] * local[0] * sx - wy[1] * local[1] * sy
    pz = anchor[2] - local[2] * sz
    path = M[key] if "/" in M.get(key, "") else key
    name = path.rsplit("/", 1)[-1]
    STATS.setdefault(name, []).append((abs(sx), sy, sz))
    COUNTS[part or key] = COUNTS.get(part or key, 0) + 1
    return {"kind": "kit", "label": label, "folder": folder, "mesh": KIT_ROOT + path if not path.startswith("/") else path,
            "location": [px, py, pz], "yaw": yaw, "pitch": 0.0, "scale": [-sx if mirror else sx, sy, sz],
            "tags": [KIT_TAG, "kit_visual"] + list(tags)}


def kit_plane(label, folder, rect, z, material, tags=()):
    x0, y0, x1, y1 = rect
    COUNTS["floor_plane"] = COUNTS.get("floor_plane", 0) + 1
    return {"kind": "kit_plane", "label": label, "folder": folder, "location": [(x0 + x1) / 2.0, (y0 + y1) / 2.0, z], "yaw": 0.0, "pitch": 0.0,
            "scale": [(x1 - x0) / 100.0, (y1 - y0) / 100.0, 1.0], "material": material, "tags": [KIT_TAG, "kit_visual", "kit_floor_finish"] + list(tags)}


class Edge:
    """A footprint side: d along it with rot90(d) = n (outward); s = distance along d from O; [lo, hi] the dressed range (the long sides
    run over the corners, the short ones stop at the long sides' inner faces)."""

    def __init__(self, name, line_pt, n, d, length, long_side):
        self.name, self.O, self.n, self.d, self.L, self.long = name, line_pt, n, d, length, long_side
        e = WALL_T / 2.0
        self.lo, self.hi = (-e, length + e) if long_side else (e, length - e)

    def s_of(self, p):
        return (p[0] - self.O[0]) * self.d[0] + (p[1] - self.O[1]) * self.d[1]

    def perp(self, p):
        return abs((p[0] - self.O[0]) * self.n[0] + (p[1] - self.O[1]) * self.n[1])

    def at(self, s, out=0.0):
        """The point at s on the footprint line, `out` cm outward."""
        return (self.O[0] + self.d[0] * s + self.n[0] * out, self.O[1] + self.d[1] * s + self.n[1] * out)


def edges_of(b):
    (x0, y0), (x1, y1) = b["footprint"]
    x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
    long_x = (y1 - y0) >= (x1 - x0)  # the x = const sides are the long ones
    return [Edge("x0", (x0, y0), (-1.0, 0.0), (0.0, 1.0), y1 - y0, long_x),
            Edge("x1", (x1, y1), (1.0, 0.0), (0.0, -1.0), y1 - y0, long_x),
            Edge("y0", (x1, y0), (0.0, -1.0), (-1.0, 0.0), x1 - x0, not long_x),
            Edge("y1", (x0, y1), (0.0, 1.0), (1.0, 0.0), x1 - x0, not long_x)]


def edge_of_point(edges, p, tol=20.0):
    best = min(edges, key=lambda e: e.perp(p))
    return best if best.perp(p) <= tol and -1.0 <= best.s_of(p) <= best.L + 1.0 else None


def tile_exact(a, b):
    """[a, b] in n modules of equal width (n = round(len / 300)): [(start, width)]."""
    g = b - a
    if g < 2.0:
        return []
    n = max(1, int(round(g / MODULE_W)))
    w = g / n
    return [(a + i * w, w) for i in range(n)]


def fill(a, b, ext_l, ext_r):
    """Solid filler modules over [a, b] (cm along the wall); a filler may slide ext_l / ext_r behind the neighbour bays' jambs (or into a
    perpendicular wall). -> [(start, width, recess_cm)]. Exact tiling when a width within +-10 % fits; else 300 cm modules overlapping
    the neighbours (and each other, alternately recessed); only a gap too small even for that is squished."""
    g = b - a
    if g < 1.0:
        return []
    n = max(1, int(round(g / MODULE_W)))
    if FILLER_MODE == "squish" or FILL_MIN <= g / n <= FILL_MAX:
        if not FILL_MIN <= g / n <= FILL_MAX:
            report("  SQUISH (filler mode squish): gap %.0f cm -> %d module(s) x %.2f" % (g, n, g / n / MODULE_W))
        return [(a + i * g / n, g / n, 0.0) for i in range(n)]
    if g + ext_l + ext_r < MODULE_W:
        if g + ext_l + ext_r < FILL_MIN:
            report("  SQUISH: filler gap %.0f cm (+%.0f / +%.0f behind the neighbours) -> one %.0f cm module (x %.2f)" % (
                g, ext_l, ext_r, g + ext_l + ext_r, (g + ext_l + ext_r) / MODULE_W))
        return [(a - ext_l, g + ext_l + ext_r, RECESS)]
    span = max(MODULE_W, g)
    extra = span - g
    xl = min(ext_l, extra / 2.0)
    xr = extra - xl
    if xr > ext_r:
        xr = ext_r
        xl = extra - xr
    start = a - xl
    n = max(1, int(math.ceil(span / MODULE_W - 1e-6)))
    step = (span - MODULE_W) / (n - 1) if n > 1 else 0.0
    return [(start + i * step, MODULE_W, RECESS * (1.0 if i % 2 == 0 else 0.5)) for i in range(n)]


# ---- the building's dressing ------------------------------------------------------------------------------------------------------

def facade_specs(B, layout, b, gz, stair_rooms):
    bid = b["id"]
    floors = int(b["floors"])
    edges = edges_of(b)
    street_wall = (b.get("plan") or {}).get("street_wall")
    street_edge = None
    if street_wall:
        w = next((w for w in layout["walls"] if str(w.get("id", "")).startswith(street_wall)), None)
        if w:
            street_edge = edge_of_point(edges, ((w["a"][0] + w["b"][0]) / 2.0, (w["a"][1] + w["b"][1]) / 2.0))
    openings = list(b.get("openings", [])) + B.balcony_door_openings(b)
    # "a couple of balconies on the street side": the two of the layout's balcony slots (S2.3 marks them; nothing is built there yet)
    # farthest from the wall ends (a slot next to a corner leaves a 20 cm filler)
    slots = []
    for o in openings:
        e = edge_of_point(edges, o["pos"]) if o.get("slot") == "balcony" else None
        if e is not None:
            s = e.s_of(o["pos"])
            slots.append((-min(s, e.L - s), o["id"]))
    balcony_ids = {i for _, i in sorted(slots)[:2]}
    out = []
    for e in edges:
        for f in range(floors):
            z0 = gz + STOREY * f
            folder = "Kit/%s/Facade/%s/L%d" % (bid, e.name, f)
            tags = ["kit_building=" + bid, "kit_floor=%d" % f, "kit_edge=" + e.name]
            bays = []   # (s0, s1, jamb_l, jamb_r, kind, opening)
            for op in openings:
                if int(op.get("floor", 0)) != f or op.get("kind") not in ("door", "window"):
                    continue
                if edge_of_point(edges, op["pos"]) is not e:
                    continue
                c = e.s_of(op["pos"])
                w = float(op.get("width", 120.0))
                if op["kind"] == "door":
                    half = MODULE_W * ENTRY_SX / 2.0
                    bays.append((c - half, c + half, JAMB["entry"], JAMB["entry"], "entry", op))
                elif op["id"] in balcony_ids:
                    bays.append((c - 195.0, c + 105.0, JAMB["balc"][0], JAMB["balc"][1], "balc", op))
                elif w > 180.0:
                    bays.append((c - 150.0, c + 150.0, JAMB["wide"], JAMB["wide"], "wide", op))
                else:
                    bays.append((c - 150.0, c + 150.0, JAMB["win"], JAMB["win"], "win", op))
            # a stair window in the middle of each stairwell's span on this wall (visual only: the layout has no opening there)
            for r in stair_rooms.get(f, []):
                (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
                corners = [(rx0, ry0), (rx1, ry0), (rx1, ry1), (rx0, ry1)]
                on = [p for p in corners if e.perp(p) <= 1.0]
                if len(on) == 2:
                    sa, sb = sorted(e.s_of(p) for p in on)
                    c = (sa + sb) / 2.0
                    if not any(b0 < c + 150.0 and b1 > c - 150.0 for b0, b1, _, _, _, _ in bays):
                        bays.append((c - 150.0, c + 150.0, JAMB["stairglass"], JAMB["stairglass"], "stairglass", r))
            bays.sort(key=lambda x: x[0])
            for i in range(1, len(bays)):
                if bays[i][0] < bays[i - 1][1] - 0.5:
                    report("REVIEW: %s %s L%d: bays overlap at s %.0f (openings closer than a module)" % (bid, e.name, f, bays[i][0]))
            for k, (s0, s1, jl, jr, kind, op) in enumerate(bays):
                lab = "%s_kit_%s_L%d_b%d_%s" % (bid, e.name, f, k, kind)
                anchor = e.at(s0, WALL_T / 2.0) + (z0,)
                if kind in ("win", "wide"):
                    out.append(kit(lab + "_wall", folder, "win_wall" if kind == "win" else "wide_wall", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags, part="facade_window_bay"))
                    out.append(kit(lab + "_window", folder, "win" if kind == "win" else "win_wide", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags + ["kit_window"], part="window"))
                elif kind == "balc":
                    out.append(kit(lab + "_wall", folder, "balc_wall", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags, part="facade_balcony_bay"))
                    out.append(kit(lab + "_window", folder, "win_balc", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags + ["kit_window"], part="balcony_door_window"))
                    out.append(kit(lab + "_balcony", folder, "balcony", e.d, e.n, anchor, scale=(1.0, 1.0, 1.0), tags=tags + ["kit_balcony"], part="balcony"))
                    report("balcony (visual only, not reachable) at %s on %s floor %d" % (op["id"], e.name, f))
                elif kind == "entry":
                    s3 = (ENTRY_SX, 1.0, SZ)
                    out.append(kit(lab + "_wall", folder, "ent_wall", e.d, e.n, anchor, scale=s3, tags=tags, part="facade_entrance_bay"))
                    out.append(kit(lab + "_door", folder, "ent_door", e.d, e.n, anchor, scale=s3, tags=tags + ["kit_door"], part="entrance_door"))
                    strip_z = z0 + 230.0 * SZ
                    out.append(kit(lab + "_strip", folder, "strip", e.d, e.n, (anchor[0], anchor[1], strip_z),
                                   scale=(ENTRY_SX, 1.0, (z0 + STOREY - strip_z) / 140.0), tags=tags, part="entrance_top_strip"))
                    if f == 0:
                        out.append(kit(lab + "_porch", folder, "porch", e.d, e.n, (anchor[0], anchor[1], z0 + 10.0), scale=(ENTRY_SX, 1.0, 1.0), tags=tags, part="porch"))
                        out.append(kit(lab + "_canopy", folder, "canopy", e.d, e.n, (anchor[0], anchor[1], z0 + 222.0 * SZ), scale=(ENTRY_SX, 1.0, 1.0), tags=tags, part="canopy"))
                elif kind == "stairglass":
                    out.append(kit(lab + "_wall", folder, "stair_wall", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags, part="facade_stairwell_bay"))
                    out.append(kit(lab + "_glass", folder, "stair_glass", e.d, e.n, anchor, scale=(1.0, 1.0, SZ), tags=tags + ["kit_window"], part="stairwell_glass"))
                if kind in ("win", "wide", "balc"):
                    # a radiator under the window on the inner face (local +Y into the room)
                    c = s0 + (195.0 if kind == "balc" else 150.0)
                    inner = (-e.n[0], -e.n[1])
                    p = e.at(c, -WALL_T / 2.0)
                    out.append(kit(lab + "_radiator", folder, "radiator", (inner[1], -inner[0]), inner, (p[0], p[1], z0), local=(44.0, 4.0, 0.0), tags=tags, part="radiator"))
            # solid fillers between the bays (a wall end may hide 30 cm in the long side it meets)
            end_ext = 0.0 if e.long else WALL_T
            cur, cur_jamb = e.lo, end_ext
            for k, (s0, s1, jl, jr, kind, op) in enumerate(bays + [(e.hi, e.hi, end_ext, end_ext, "end", None)]):
                ext_l = max(0.0, cur_jamb - (JAMB_MARGIN if cur > e.lo else 0.0))
                ext_r = max(0.0, jl - (JAMB_MARGIN if kind != "end" else 0.0))
                for n_, (fs, fw, recess) in enumerate(fill(cur, s0, ext_l, ext_r)):
                    p = e.at(fs, WALL_T / 2.0 - recess)
                    out.append(kit("%s_kit_%s_L%d_f%d_%d" % (bid, e.name, f, k, n_), folder, "solid", e.d, e.n, (p[0], p[1], z0),
                                   scale=(fw / MODULE_W, 1.0, SZ), tags=tags, part="facade_solid"))
                cur, cur_jamb = s1, jr
        # plinth (ground storey; not under the entrance doors / porches)
        cuts = []
        for op in openings:
            if int(op.get("floor", 0)) == 0 and op.get("kind") == "door" and edge_of_point(edges, op["pos"]) is e:
                c = e.s_of(op["pos"])
                cuts.append((c - (MODULE_W * ENTRY_SX / 2.0 - 30.0 * ENTRY_SX), c + (MODULE_W * ENTRY_SX / 2.0 - 30.0 * ENTRY_SX)))
        cuts.sort()
        lo, hi = (e.lo - 5.0, e.hi + 5.0) if e.long else (e.lo, e.hi)
        segs, cur = [], lo
        for c0, c1 in cuts:
            segs.append((cur, c0))
            cur = c1
        segs.append((cur, hi))
        for k, (a, bb) in enumerate(segs):
            for n_, (ps, pw) in enumerate(tile_exact(a, bb)):
                p = e.at(ps, WALL_T / 2.0 + 1.0)
                out.append(kit("%s_kit_%s_plinth_%d_%d" % (bid, e.name, k, n_), "Kit/%s/Plinth" % bid, "plinth", e.d, e.n, (p[0], p[1], gz - 45.0),
                               scale=(pw / MODULE_W, 1.0, 1.0), tags=["kit_building=" + bid, "kit_floor=0"], part="plinth"))
        # parapet on the roof edge (our parapet proxy is 95 cm: the 104 cm kit parapet x 0.913)
        par = next((p for p in b.get("parapets", []) if edge_of_point(edges, ((p["start"][0] + p["end"][0]) / 2.0, (p["start"][1] + p["end"][1]) / 2.0)) is e), None)
        if par is not None:
            ph = float(par.get("height_cm", 95.0))
            lo, hi = (e.lo - 5.0, e.hi + 5.0) if e.long else (e.lo + 5.0, e.hi - 5.0)
            for n_, (ps, pw) in enumerate(tile_exact(lo, hi)):
                p = e.at(ps, WALL_T / 2.0)
                out.append(kit("%s_kit_%s_parapet_%d" % (bid, e.name, n_), "Kit/%s/Roof" % bid, "parapet", e.d, e.n, (p[0], p[1], gz + STOREY * floors),
                               scale=(pw / MODULE_W, 1.0, ph / 104.0), tags=["kit_building=" + bid, "kit_roof"], part="parapet"))
    # drain pipes on the long sides: both ends + the middle
    for e in edges:
        if not e.long:
            continue
        for k, s in enumerate((e.lo + 35.0, e.L / 2.0 - 50.0, e.hi - 35.0)):
            p = e.at(s, WALL_T / 2.0 + 0.5)
            out.append(kit("%s_kit_%s_drain_%d" % (bid, e.name, k), "Kit/%s/Props" % bid, "drain", e.d, e.n, (p[0], p[1], gz), tags=["kit_building=" + bid], part="drain"))
    report("street side: %s (%s); balconies on %s" % (street_edge.name if street_edge else "?", street_wall, ", ".join(sorted(balcony_ids)) or "-"))
    return out


def partition_specs(B, layout, b, gz, stair_rooms):
    bid = b["id"]
    out = []

    def is_stair_wall(pt, f):
        (ax, ay), (bx, by) = pt["start"], pt["end"]
        for r in stair_rooms.get(f, []):
            (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
            if abs(ax - bx) < 1.0 and (abs(ax - rx0) < 1.0 or abs(ax - rx1) < 1.0) and min(ay, by) >= ry0 - 1.0 and max(ay, by) <= ry1 + 1.0:
                return True
            if abs(ay - by) < 1.0 and (abs(ay - ry0) < 1.0 or abs(ay - ry1) < 1.0) and min(ax, bx) >= rx0 - 1.0 and max(ax, bx) <= rx1 + 1.0:
                return True
        return False

    for pt in b.get("partitions", []) or []:
        f = int(pt.get("floor", 0))
        z0 = gz + STOREY * f
        a, e_ = pt["start"], pt["end"]
        L = math.hypot(e_[0] - a[0], e_[1] - a[1])
        if L < 2.0:
            continue
        d = ((e_[0] - a[0]) / L, (e_[1] - a[1]) / L)
        nn = rot90(d)
        stairy = is_stair_wall(pt, f)
        key = "stair_int_wall" if stairy else "int_wall"
        kz = (STOREY / 260.0) if stairy else 1.0
        folder = "Kit/%s/Interior/L%d" % (bid, f)
        tags = ["kit_building=" + bid, "kit_floor=%d" % f, "kit_interior"]
        pt_at = lambda s, _a=a, _d=d: (_a[0] + _d[0] * s, _a[1] + _d[1] * s)  # noqa: E731
        gaps = []
        for op in pt.get("openings", []) or []:
            if op.get("kind") != "door":
                continue
            s = (op["pos"][0] - a[0]) * d[0] + (op["pos"][1] - a[1]) * d[1]
            w = float(op.get("width", 120.0))
            gaps.append((max(0.0, s - w / 2.0), min(L, s + w / 2.0), op))
        gaps.sort(key=lambda g: g[0])
        cur = 0.0
        for k, (g0, g1, op) in enumerate(gaps + [(L, L, None)]):
            for n_, (ws, ww) in enumerate(tile_exact(cur, g0)):
                p = pt_at(ws)
                out.append(kit("%s_kit_%s_w%d_%d" % (bid, pt["id"], k, n_), folder, key, d, nn, (p[0], p[1], z0), scale=(ww / MODULE_W, 1.0, kz), tags=tags,
                               part="interior_stairwell_wall" if stairy else "interior_wall"))
            cur = max(cur, g1)
            if op is None:
                continue
            w = g1 - g0
            c = (g0 + g1) / 2.0
            role = op.get("role", "")
            state = B.effective_opening_state(layout, b, dict(op, floor=f))
            lab = "%s_kit_%s_d%d" % (bid, pt["id"], k)
            top_vis = float(op.get("top_height", 240.0))
            if role == "unit_entry":
                dw, top_vis = 102.0 * DOOR_APT_S, 214.0 * DOOR_APT_S
                p = pt_at(c)
                out.append(kit(lab + "_flatdoor", folder, "door_apt", d, nn, (p[0], p[1], z0), local=(45.0, 3.0, 0.0), scale=(DOOR_APT_S, 1.0, DOOR_APT_S),
                               tags=tags + ["kit_door", "door_state=%s" % state], part="flat_door"))
                jw = (w - dw) / 2.0
                if jw > 1.0:
                    for side, js in (("l", g0), ("r", g1 - jw)):
                        p = pt_at(js)
                        out.append(kit(lab + "_jamb" + side, folder, key, d, nn, (p[0], p[1], z0), scale=(jw / MODULE_W, 1.0, top_vis / 300.0), tags=tags, part="door_jamb_fill"))
            elif role != "portal":
                n_leaves = 2 if w >= 140.0 else 1
                lw = w / n_leaves
                open_ = state in ("open", None)
                for li in range(n_leaves):
                    hinge_s = g0 + 1.0 if li == 0 else g1 - 1.0
                    p = pt_at(hinge_s)
                    along = d if li == 0 else (-d[0], -d[1])
                    wx = nn if open_ else along      # an open leaf swings 90 deg to the +nn side
                    out.append(kit("%s_leaf%d" % (lab, li), folder, "door_int", wx, rot90(wx), (p[0], p[1], z0), local=(0.0, 2.5, 0.0),
                                   scale=((lw - 2.0) / 80.0, 1.0, DOOR_INT_SZ), tags=tags + ["kit_door", "door_state=%s" % state], part="interior_door_leaf"))
                top_vis = 205.0 * DOOR_INT_SZ
            lh = STOREY - top_vis
            if lh > 1.0:
                p = pt_at(g0)
                out.append(kit(lab + "_lintel", folder, key, d, nn, (p[0], p[1], z0 + top_vis), scale=(w / MODULE_W, 1.0, lh / 300.0), tags=tags, part="door_lintel"))
    return out


def stair_specs(b, gz, stair_rooms):
    """The kit switchback stacked per storey over each stair core (the kit building's own assembly): lane 1 (rising) on the storey-0
    flight's lane, lane 2 towards the core's other flight; returns (specs, footprint rects)."""
    bid = b["id"]
    floors = int(b["floors"])
    out, rects = [], []
    stairs = [s for s in b.get("stairs", []) if s.get("kind") == "stairwell"]
    for r in stair_rooms.get(0, []):
        (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
        inside = [s for s in stairs if rx0 - 1 <= s["pos"][0] <= rx1 + 1 and ry0 - 1 <= s["pos"][1] <= ry1 + 1]
        first = next((s for s in inside if int(s["from"]) == 0), None)
        if first is None:
            continue
        nxt = next((s for s in inside if int(s["from"]) == 1), None)
        S0 = first["pos"]
        T0 = first["top"]
        L = math.hypot(T0[0] - S0[0], T0[1] - S0[1]) or 1.0
        v = ((T0[0] - S0[0]) / L, (T0[1] - S0[1]) / L)
        if nxt is not None:
            q = (nxt["pos"][0] - S0[0], nxt["pos"][1] - S0[1])
            k = q[0] * v[0] + q[1] * v[1]
            w = (q[0] - v[0] * k, q[1] - v[1] * k)
        else:
            w = (((rx0 + rx1) / 2.0) - S0[0], ((ry0 + ry1) / 2.0) - S0[1])
            k = w[0] * v[0] + w[1] * v[1]
            w = (w[0] - v[0] * k, w[1] - v[1] * k)
        wl = math.hypot(*w) or 1.0
        w = (w[0] / wl, w[1] / wl)
        for f in range(floors):
            if not any(int(s["from"]) == f for s in inside):
                continue
            out.append(kit("%s_kit_stair_%s_L%d" % (bid, first["id"], f), "Kit/%s/Stairs" % bid, "stair", w, v, (S0[0], S0[1], gz + STOREY * f + STAIR_LIFT),
                           local=(80.0, -370.0, 0.0), scale=(STAIR_SX, 1.0, SZ), tags=["kit_building=" + bid, "kit_floor=%d" % f, "kit_stair"], part="stair_flight_set"))
        pts = []
        for lx in (10.0, 290.0):
            for ly in (-590.0, -30.0):
                pts.append((S0[0] + w[0] * (lx - 80.0) * STAIR_SX + v[0] * (ly + 370.0), S0[1] + w[1] * (lx - 80.0) * STAIR_SX + v[1] * (ly + 370.0)))
        rects.append((min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)))
        report("stair core %s: kit switchback (half-storey flights) stacked x%d over our full-storey ramps %s - see the doc: the walk line and the visual differ" % (
            r["id"], floors, ", ".join(s["id"] for s in inside)))
    return out, rects


def finish_specs(b, gz, slab_specs, stair_rects):
    """Floor finishes (planes, UV 0..1 per cell) on every storey's rooms (minus holes: on the upper storeys only where our slab is) and the
    bitumen roof, plus roof vents / antenna."""
    bid = b["id"]
    floors = int(b["floors"])
    out = []
    slabs = {}
    for sp in slab_specs:
        lvl = next((int(t.split("=")[1]) for t in sp.get("tags", []) if str(t).startswith("floor=")), None)
        if lvl is None:
            continue
        cx, cy, _ = sp["location"]
        hx, hy = sp["scale"][0] * 50.0, sp["scale"][1] * 50.0
        slabs.setdefault(lvl, []).append((cx - hx, cy - hy, cx + hx, cy + hy))
    for room in b.get("rooms", []):
        f = int(room["floor"])
        (rx0, ry0), (rx1, ry1) = room["bounds"]["min"], room["bounds"]["max"]
        base = [(rx0, ry0, rx1, ry1)]
        if f > 0:
            base = [i for r in base for s in slabs.get(f, []) for i in [rect_and(r, s)] if i]
        if room.get("kind") == "stairwell":
            for sr in stair_rects:
                base = [x for r in base for x in rect_minus(r, sr)]
        mat = FLOOR_MAT.get(room.get("kind"), FLOOR_MAT_DEFAULT)
        k = 0
        for r in base:
            for c in cells(r, FLOOR_CELL):
                out.append(kit_plane("%s_kit_floor_%s_%d" % (bid, room["id"], k), "Kit/%s/Floors/L%d" % (bid, f), c, gz + STOREY * f + FLOOR_LIFT, mat,
                                     ["kit_building=" + bid, "kit_floor=%d" % f]))
                k += 1
    k = 0
    for r in slabs.get(floors, []):
        for c in cells(r, FLOOR_CELL * 1.5):
            out.append(kit_plane("%s_kit_roof_%d" % (bid, k), "Kit/%s/Roof" % bid, c, gz + STOREY * floors + FLOOR_LIFT, ROOF_MAT, ["kit_building=" + bid, "kit_roof"]))
            k += 1
    (x0, y0), (x1, y1) = b["footprint"]
    x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
    rz = gz + STOREY * floors + FLOOR_LIFT
    holes = [r for r in stair_rects]
    long_y = (y1 - y0) >= (x1 - x0)
    for k, t in enumerate((0.12, 0.5, 0.88)):
        p = (x0 + (x1 - x0) * 0.3, y0 + (y1 - y0) * t) if long_y else (x0 + (x1 - x0) * t, y0 + (y1 - y0) * 0.3)
        if any(h[0] - 100 <= p[0] <= h[2] + 100 and h[1] - 100 <= p[1] <= h[3] + 100 for h in holes):
            continue
        out.append(kit("%s_kit_roof_vent_%d" % (bid, k), "Kit/%s/Roof" % bid, "vent", (1.0, 0.0), (0.0, 1.0), (p[0], p[1], rz), tags=["kit_building=" + bid, "kit_roof"], part="roof_vent"))
    p = (x0 + (x1 - x0) * 0.25, y0 + (y1 - y0) * 0.65) if long_y else (x0 + (x1 - x0) * 0.65, y0 + (y1 - y0) * 0.25)
    out.append(kit("%s_kit_roof_antenna" % bid, "Kit/%s/Roof" % bid, "antenna", (1.0, 0.0), (0.0, 1.0), (p[0], p[1], rz), tags=["kit_building=" + bid, "kit_roof"], part="roof_antenna"))
    return out


def kitify(B, layout, specs):
    """The builder's specs with the building's grey boxes hidden (kept as proxies) / its glass dropped, plus the kit visuals."""
    b = layout["buildings"][0]
    bid = b["id"]
    gz = float(layout.get("ground_z", 0.0))
    stair_ids = {"%s_%s" % (bid, s["id"]) for s in b.get("stairs", [])}
    out, slab_specs = [], []
    hidden = dropped = 0
    for sp in specs:
        folder = str(sp.get("folder", ""))
        mine = folder == "Buildings/%s" % bid or folder.startswith("Buildings/%s/" % bid)
        tags = [str(t) for t in sp.get("tags", [])]
        if not mine or sp.get("kind") not in ("cube", "cylinder"):
            out.append(sp)
            continue
        if "window_glass" in tags:
            dropped += 1
            continue
        if "floor_slab" in tags or "roof_slab" in tags:
            slab_specs.append(sp)
            out.append(sp)
            continue
        sp = dict(sp, kit_hidden=True)
        if sp["label"] in stair_ids:
            sp["tags"] = list(sp.get("tags", [])) + ["kit_hidden_ramp"]
        hidden += 1
        out.append(sp)
    report("%s: %d grey-box pieces kept as hidden proxies (walls, sills, lintels, leaves, partitions, parapets, %d ramps), %d glass panes dropped, %d slabs kept" % (
        bid, hidden, len(stair_ids), dropped, len(slab_specs)))
    stair_rooms = {}
    for r in b.get("rooms", []):
        if r.get("kind") == "stairwell":
            stair_rooms.setdefault(int(r["floor"]), []).append(r)
    kits = []
    kits += facade_specs(B, layout, b, gz, stair_rooms)
    kits += partition_specs(B, layout, b, gz, stair_rooms)
    st, stair_rects = stair_specs(b, gz, stair_rooms)
    kits += st
    kits += finish_specs(b, gz, slab_specs, stair_rects)
    kits.append({"kind": "kit_camera", "label": "KitPilot_Camera", "folder": "Kit/Camera", "location": [0.0, 0.0, 1000.0], "yaw": 0.0, "pitch": -30.0, "tags": [CAM_TAG]})
    report("%s: %d kit visuals" % (bid, len(kits)))
    return out + kits


def stats_summary():
    rows = []
    for name, ss in sorted(STATS.items()):
        xs = [s[0] for s in ss]
        zs = [s[2] for s in ss]
        far = sum(1 for s in ss if abs(s[0] - 1.0) > 0.105 or abs(s[2] - 1.0) > 0.105)
        rows.append({"mesh": name, "count": len(ss), "sx_min": round(min(xs), 3), "sx_max": round(max(xs), 3), "sz_min": round(min(zs), 3), "sz_max": round(max(zs), 3),
                     "beyond_10pct": far})
    return rows


def plan_all():
    B = load_builder()
    with open(LAYOUT_FILE, encoding="utf-8") as f:
        layout = json.load(f)
    pilot = pilot_layout(layout, BUILDING_ID)
    specs = B.plan(pilot)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    return B, pilot, kitify(B, pilot, specs)


def write_outputs(pilot, specs, extra=None):
    os.makedirs(OUT_DIR, exist_ok=True)
    data = {"map": TARGET, "layout": LAYOUT_FILE, "building": BUILDING_ID, "time": datetime.datetime.now().isoformat(),
            "kinds": {k: sum(1 for s in specs if s["kind"] == k) for k in sorted({s["kind"] for s in specs})},
            "hidden_proxies": sum(1 for s in specs if s.get("kit_hidden")), "parts": dict(sorted(COUNTS.items())), "stretch": stats_summary(),
            "report": REPORT}
    if extra:
        data.update(extra)
    with open(os.path.join(OUT_DIR, "kit_pilot_report.json"), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    return data


# ---- Unreal: the map -------------------------------------------------------------------------------------------------------------

def build():
    if not TARGET.startswith("/Game/Maps/KitPilot/"):
        raise ValueError("KIT_PILOT_TARGET must be under /Game/Maps/KitPilot/ (got %s)" % TARGET)
    B, pilot, specs = plan_all()
    lib = unreal.EditorAssetLibrary
    if lib.does_asset_exist(TARGET):
        report("deleting the previous pilot map %s -> %s" % (TARGET, lib.delete_asset(TARGET)))
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    B.ensure_lighting(actors)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    placer = KitPlacer(B)
    for sp in specs:
        placer.place(sp)
    REPORT.extend("builder: " + line for line in B.REPORT)
    del B.REPORT[:]
    nav = [a for a in actors.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for a in nav:
        actors.destroy_actor(a)
    report("placed %d actors (%d kit visuals, %d hidden proxies, %d kit visuals with collision left on); destroyed %d RecastNavMesh" % (
        placer.inner.placed + placer.kits, placer.kits, placer.hidden, placer.colliding, len(nav)))
    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, TARGET)
    report("saved %s: %s" % (TARGET, saved))
    path = B.write_level_json(pilot, TARGET)
    with open(path, encoding="utf-8") as f:
        lj = json.load(f)
    lj["level_name"] = "Kit pilot %s (%s)" % (BUILDING_ID, os.path.basename(LAYOUT_FILE))
    lj["description"] = "Designer look test 2026-11-20 (Scripts/Editor/kit_pilot_building.py): %s dressed with the Soviet kit; proxies = the generator's grey boxes (hidden)." % BUILDING_ID
    # no snow cover: the snow layer also lands on the interior floors (seen in the first pictures)
    lj["environment"] = {"_comment": "kit pilot pictures: locked Clear 13:00, no snow cover", "locked": True, "start_hour": 13, "weather": "Clear", "snow_cover": 0.0}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(lj, f, indent=2)
        f.write("\n")
    REPORT.extend("builder: " + line for line in B.REPORT)
    write_outputs(pilot, specs, {"saved": bool(saved), "level_json": path, "kit_with_collision": placer.colliding})


class KitPlacer:
    def __init__(self, B):
        self.B = B
        self.inner = B.Placer()
        self.actors = self.inner.actors
        self.meshes = {}
        self.materials = None
        self.kits = self.hidden = self.colliding = 0
        last = {}
        orig = self.inner.spawn

        def spawn(cls, spec, tags=None):
            a = orig(cls, spec, tags)
            last["a"] = a
            return a
        self.inner.spawn = spawn
        self.last = last

    def mesh(self, path):
        if path not in self.meshes:
            m = unreal.load_asset(path)
            if m is None:
                report("ERROR: kit mesh %s missing" % path)
            self.meshes[path] = m
        return self.meshes[path]

    def material(self, name):
        if self.materials is None:
            self.materials = {}
            for p in unreal.EditorAssetLibrary.list_assets(KIT_MATERIALS, recursive=True, include_folder=False):
                self.materials[p.rsplit("/", 1)[-1].split(".")[0]] = p
        p = self.materials.get(name)
        return unreal.load_asset(p) if p else None

    def visual_only(self, comp, label):
        comp.set_collision_profile_name("NoCollision")
        try:
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        except Exception:
            pass
        if hasattr(comp, "set_can_ever_affect_navigation"):
            comp.set_can_ever_affect_navigation(False)
        else:
            comp.set_editor_property("can_ever_affect_navigation", False)
        comp.set_editor_property("generate_overlap_events", False)
        body = comp.get_editor_property("body_instance")
        if str(body.get_editor_property("collision_profile_name")) != "NoCollision" or comp.get_editor_property("can_ever_affect_navigation"):
            self.colliding += 1
            report("REVIEW: %s: collision / navigation not switched off" % label)

    def spawn_mesh(self, spec, mesh, material=None):
        loc = unreal.Vector(*spec["location"])
        rot = unreal.Rotator(pitch=spec.get("pitch", 0.0), yaw=spec.get("yaw", 0.0), roll=0.0)
        a = self.actors.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
        a.set_actor_label(spec["label"])
        a.set_folder_path(self.B.FOLDER + "/" + spec["folder"])
        a.set_editor_property("tags", [self.B.TAG] + [str(t) for t in spec.get("tags", [])])
        a.set_actor_scale3d(unreal.Vector(*spec["scale"]))
        comp = a.static_mesh_component
        comp.set_static_mesh(mesh)
        if material is not None:
            comp.set_material(0, material)
        self.visual_only(comp, spec["label"])
        self.kits += 1
        return a

    def place(self, spec):
        kind = spec["kind"]
        if kind == "kit":
            m = self.mesh(spec["mesh"])
            if m is not None:
                self.spawn_mesh(spec, m)
            return
        if kind == "kit_plane":
            self.spawn_mesh(spec, self.mesh(PLANE), self.material(spec["material"]))
            return
        if kind == "kit_camera":
            a = self.actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(*spec["location"]), unreal.Rotator(pitch=spec["pitch"], yaw=spec["yaw"], roll=0.0))
            a.set_actor_label(spec["label"])
            a.set_folder_path(self.B.FOLDER + "/" + spec["folder"])
            a.set_editor_property("tags", [self.B.TAG] + spec["tags"])
            a.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            return
        self.last.pop("a", None)
        self.inner.place(spec)
        a = self.last.get("a")
        if spec.get("kit_hidden") and a is not None:
            a.set_actor_hidden_in_game(True)
            comp = a.static_mesh_component
            comp.set_editor_property("visible", False)   # invisible in the editor too; collision + navigation unchanged
            a.set_editor_property("tags", list(a.tags) + [unreal.Name("KitPilotProxy")])
            self.hidden += 1


# ---- Unreal: rendered pictures in game ----------------------------------------------------------------------------------------------

def look(loc, at):
    dx, dy, dz = at[0] - loc[0], at[1] - loc[1], at[2] - loc[2]
    return math.degrees(math.atan2(dz, math.hypot(dx, dy))), math.degrees(math.atan2(dy, dx))


def cameras(layout, b):
    """The designer's views: (name, location, pitch, yaw, fov, hide roof)."""
    gz = float(layout.get("ground_z", 0.0))
    floors = int(b["floors"])
    edges = edges_of(b)
    street_wall = (b.get("plan") or {}).get("street_wall")
    street = None
    if street_wall:
        w = next((w for w in layout["walls"] if str(w.get("id", "")).startswith(street_wall)), None)
        if w:
            street = edge_of_point(edges, ((w["a"][0] + w["b"][0]) / 2.0, (w["a"][1] + w["b"][1]) / 2.0))
    street = street or max(edges, key=lambda e: e.L)
    court = next(e for e in edges if e.n == (-street.n[0], -street.n[1]))
    H = STOREY * floors
    cams = []

    def facade_view(name, e, side):
        c = e.at(e.L / 2.0)
        loc = vadd(c, vmul(e.n, 0.75 * e.L), vmul(e.d, side * 0.55 * e.L)) + (gz + 900.0,)
        pitch, yaw = look(loc, c + (gz + H * 0.45,))
        cams.append((name, loc, pitch, yaw, 50.0, False))

    facade_view("street_facade_34", street, -1.0)
    facade_view("court_facade_34", court, -1.0)
    ops = [o for o in b.get("openings", []) if o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and edge_of_point(edges, o["pos"]) is street]
    if ops:
        p = ops[0]["pos"]
        loc = vadd(tuple(p), vmul(street.n, 650.0), vmul(street.d, -260.0)) + (gz + 230.0,)
        pitch, yaw = look(loc, tuple(p) + (gz + 160.0,))
        cams.append(("entrance_closeup", loc, pitch, yaw, 55.0, False))
    wins = [o for o in b.get("openings", []) if o.get("kind") == "window" and int(o.get("floor", 0)) == min(1, floors - 1) and edge_of_point(edges, o["pos"]) is street and not o.get("slot")]
    if wins:
        # the window nearest the street side's start corner: the kit window, the squished corner filler and the plinth / parapet ends
        p = min(wins, key=lambda o: street.s_of(o["pos"]))["pos"]
        zc = gz + STOREY * min(1, floors - 1) + 150.0
        loc = vadd(tuple(p), vmul(street.n, 650.0), vmul(street.d, 260.0)) + (zc + 20.0,)
        pitch, yaw = look(loc, vadd(tuple(p), vmul(street.d, -90.0)) + (zc - 40.0,))
        cams.append(("window_closeup", loc, pitch, yaw, 50.0, False))
    stair_rooms = [r for r in b.get("rooms", []) if r.get("kind") == "stairwell" and int(r["floor"]) == min(1, floors - 1)]
    stairs = [s for s in b.get("stairs", []) if s.get("kind") == "stairwell" and int(s["from"]) == 0]
    if stair_rooms and stairs:
        r = stair_rooms[0]
        (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
        s0 = next((s for s in stairs if rx0 <= s["pos"][0] <= rx1 and ry0 <= s["pos"][1] <= ry1), stairs[0])
        v = (s0["top"][0] - s0["pos"][0], s0["top"][1] - s0["pos"][1])
        vl = math.hypot(*v) or 1.0
        v = (v[0] / vl, v[1] / vl)
        cx, cy = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
        # on the ground floor at the core's far end, under the upper slab, looking back at the flights
        far = (cx + v[0] * ((rx1 - rx0) / 2.0 - 30.0), cy + v[1] * ((ry1 - ry0) / 2.0 - 30.0))
        loc = far + (gz + 175.0,)
        at = (s0["pos"][0] + v[0] * 120.0, s0["pos"][1] + v[1] * 120.0, gz + 160.0)
        pitch, yaw = look(loc, at)
        cams.append(("stairwell_interior", loc, pitch, yaw, 85.0, False))
        # the same core from above with the roof off (the flights of both storeys)
        loc = (cx - v[1] * 60.0 + v[0] * 350.0, cy + v[0] * 60.0 + v[1] * 350.0, gz + STOREY * floors + 650.0)
        pitch, yaw = look(loc, (cx, cy - v[1] * 0.0, gz + STOREY))
        cams.append(("stairwell_above_roof_off", loc, pitch, yaw, 70.0, True))
    flats = [r for r in b.get("rooms", []) if r.get("kind") == "room" and int(r["floor"]) == 0]
    for r in flats:
        (rx0, ry0), (rx1, ry1) = r["bounds"]["min"], r["bounds"]["max"]
        wins = [o for o in b.get("openings", []) if o.get("kind") == "window" and int(o.get("floor", 0)) == 0 and rx0 - 1 <= o["pos"][0] <= rx1 + 1 and ry0 - 1 <= o["pos"][1] <= ry1 + 1]
        if not wins:
            continue
        p = wins[0]["pos"]
        cx, cy = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
        loc = (cx + (cx - p[0]) * 0.75, cy + (cy - p[1]) * 0.75, gz + 170.0)
        pitch, yaw = look(loc, (p[0], p[1], gz + 120.0))
        cams.append(("flat_interior", loc, pitch, yaw, 85.0, False))
        break
    (x0, y0), (x1, y1) = b["footprint"]
    cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
    long_y = abs(y1 - y0) >= abs(x1 - x0)
    span = max(abs(y1 - y0), abs(x1 - x0)) + 900.0
    fov = 40.0
    h = span / (2.0 * math.tan(math.radians(fov / 2.0))) + gz + H   # UE's FOV is horizontal: the long side fills the width
    yaw = 0.0 if long_y else 90.0     # pitch -90, yaw 0: image up = +X, image right = +Y (the long side across the picture)
    cams.append(("top_down", (cx, cy, h), -89.9, yaw, fov, False))
    cams.append(("top_down_roof_off", (cx, cy, h), -89.9, yaw, fov, True))
    return cams


def game_world():
    return unreal.find_object(None, "%s.%s" % (TARGET, TARGET.rsplit("/", 1)[-1]))


def proxy_checks(w, layout, b):
    """In game: the hidden proxies still block sight and the navmesh, the openings stay open (sight trace + nav raycast through the street
    wall's solid end, its first window and its first entrance)."""
    gz = float(layout.get("ground_z", 0.0))
    edges = edges_of(b)
    street_wall = (b.get("plan") or {}).get("street_wall") or ""
    wall = next((x for x in layout["walls"] if str(x.get("id", "")).startswith(street_wall)), None)
    e = edge_of_point(edges, ((wall["a"][0] + wall["b"][0]) / 2.0, (wall["a"][1] + wall["b"][1]) / 2.0)) if wall else max(edges, key=lambda x: x.L)
    ops = [o for o in b.get("openings", []) if edge_of_point(edges, o["pos"]) is e and int(o.get("floor", 0)) == 0]
    win = min((o for o in ops if o.get("kind") == "window"), key=lambda o: e.s_of(o["pos"]), default=None)
    door = next((o for o in ops if o.get("kind") == "door"), None)
    # (name, point, sight blocked?, navmesh blocked?); a closed entrance's leaf blocks sight, not the pawn (ADR-0003 'before R1')
    probes = [("solid wall end", e.at(20.0), True, True)]
    if win:
        probes.append(("window %s" % win["id"], tuple(win["pos"]), False, None))
    if door:
        probes.append(("entrance %s (%s)" % (door["id"], door.get("state") or "default closed"), tuple(door["pos"]), door.get("state") not in ("open",), False))
    ok = True
    for name, p, want_block, want_nav in probes:
        a2, b2 = vadd(p, vmul(e.n, 150.0)), vadd(p, vmul(e.n, -150.0))
        z = gz + (150.0 if "window" in name else 120.0)
        hit = unreal.SystemLibrary.line_trace_single(w, unreal.Vector(a2[0], a2[1], z), unreal.Vector(b2[0], b2[1], z), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                                    False, [], unreal.DrawDebugTrace.NONE, True)
        sight_blocked = hit is not None and bool(hit.to_tuple()[0])
        nav_txt = "-"
        if want_nav is not None:
            try:
                res = unreal.NavigationSystemV1.navigation_raycast(w, unreal.Vector(a2[0], a2[1], gz + 5.0), unreal.Vector(b2[0], b2[1], gz + 5.0))
                nav_blocked = bool(res[0] if isinstance(res, tuple) else res)
                nav_txt = "blocked" if nav_blocked else "open"
                ok = ok and nav_blocked == want_nav
            except Exception as exc:
                nav_txt = "n/a (%r)" % exc
        good = sight_blocked == want_block
        ok = ok and good
        REPORT.append("proxy check %-40s sight %-7s nav %-7s expected sight %s nav %s -> %s" % (
            name, "blocked" if sight_blocked else "open", nav_txt, "blocked" if want_block else "open",
            "-" if want_nav is None else ("blocked" if want_nav else "open"), "ok" if good else "FAIL"))
    REPORT.append("proxy checks: %s" % ("PASS" if ok else "FAIL"))


def shots():
    with open(LAYOUT_FILE, encoding="utf-8") as f:
        layout = json.load(f)
    b = next(x for x in layout["buildings"] if x["id"] == BUILDING_ID)
    cams = [c for c in cameras(layout, b) if not SHOT_ONLY or c[0] in SHOT_ONLY]
    state = {"t": 0.0, "stage": 0, "shot": 0, "wait": 0.0, "handle": None, "hidden": []}
    REPORT.append("shots %s %s (prefix %s)" % (TARGET, datetime.datetime.now().isoformat(), SHOT_PREFIX))
    names = ["%s%s.png" % (SHOT_PREFIX, c[0]) for c in cams]

    def finish():
        unreal.unregister_slate_post_tick_callback(state["handle"])
        os.makedirs(SHOTS_DIR, exist_ok=True)
        found = {}
        for root, _, files in os.walk(os.path.join(PROJECT_DIR, "Saved", "Screenshots")):
            for fn in files:
                if fn in names:
                    full = os.path.join(root, fn)
                    if fn not in found or os.path.getmtime(full) > os.path.getmtime(found[fn]):
                        found[fn] = full
        for fn, full in sorted(found.items()):
            shutil.copy2(full, os.path.join(SHOTS_DIR, fn))
        REPORT.append("copied %d / %d pictures to %s" % (len(found), len(names), SHOTS_DIR))
        REPORT.append("RESULT: %s" % ("PASS" if len(found) == len(names) else "FAIL"))
        with open(os.path.join(OUT_DIR, "kit_pilot_shots.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(REPORT) + "\n")
        unreal.SystemLibrary.execute_console_command(game_world(), "quit")

    def tick(dt):
        try:
            state["t"] += dt
            w = game_world()
            if w is None:
                if state["t"] > 90:
                    REPORT.append("world %s not found" % TARGET)
                    finish()
                return
            if state["stage"] == 0:
                if state["t"] < 12.0:
                    return
                for cmd in ("ShowHUD", "DisableAllScreenMessages", "r.Streaming.FullyLoadUsedTextures 1", "Codex.Pressure.Enabled 0", "Codex.Pmc.Encounters 0"):
                    unreal.SystemLibrary.execute_console_command(w, cmd)
                cam = next((a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor) if CAM_TAG in [str(t) for t in a.tags]), None)
                if cam is None:
                    REPORT.append("no CameraActor tagged %s in the map" % CAM_TAG)
                    finish()
                    return
                state["cam"] = cam
                proxy_checks(w, layout, b)
                cam.get_editor_property("camera_component").set_editor_property("constrain_aspect_ratio", False)
                roof = []
                for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.StaticMeshActor):
                    tags = [str(t) for t in a.tags]
                    if "kit_roof" in tags or "roof_slab" in tags:
                        roof.append(a)
                state["roof"] = roof
                state["stage"] = 1
                state["wait"] = state["t"] + 4.0
                return
            if state["stage"] == 1 and state["t"] >= state["wait"]:
                for a in state["hidden"]:
                    a.set_actor_hidden_in_game(False)
                state["hidden"] = []
                if state["shot"] >= len(cams):
                    finish()
                    return
                name, loc, pitch, yaw, fov, hide_roof = cams[state["shot"]]
                if hide_roof:
                    for a in state["roof"]:
                        a.set_actor_hidden_in_game(True)
                    state["hidden"] = list(state["roof"])
                cam = state["cam"]
                comp = cam.get_editor_property("camera_component")
                comp.set_editor_property("field_of_view", fov)
                try:  # the interiors get +1.5 EV (no light inside but the windows)
                    pps = comp.get_editor_property("post_process_settings")
                    pps.set_editor_property("override_auto_exposure_bias", True)
                    pps.set_editor_property("auto_exposure_bias", 1.5 if "interior" in name else 0.0)
                    comp.set_editor_property("post_process_settings", pps)
                    comp.set_editor_property("post_process_blend_weight", 1.0)
                except Exception as exc:
                    REPORT.append("exposure bias not set: %r" % exc)
                cam.set_actor_location_and_rotation(unreal.Vector(*loc), unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.0), False, True)
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(cam, 0.0)
                state["stage"] = 2
                state["wait"] = state["t"] + 3.0
                return
            if state["stage"] == 2 and state["t"] >= state["wait"]:
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                if pc:
                    pc.set_view_target_with_blend(state["cam"], 0.0)
                name = cams[state["shot"]][0]
                unreal.SystemLibrary.execute_console_command(w, "HighResShot 1920x1080 filename=%s%s.png" % (SHOT_PREFIX, name))
                REPORT.append("shot %s: loc %s pitch %.1f yaw %.1f fov %.0f" % (name, [round(v) for v in cams[state["shot"]][1]], cams[state["shot"]][2], cams[state["shot"]][3], cams[state["shot"]][4]))
                state["shot"] += 1
                state["stage"] = 1
                state["wait"] = state["t"] + 5.0
        except Exception as exc:
            import traceback
            REPORT.append("EXCEPTION: %r\n%s" % (exc, traceback.format_exc()))
            finish()

    state["handle"] = unreal.register_slate_post_tick_callback(tick)


def flush_log():
    saved = os.path.join(PROJECT_DIR, "Saved", "Logs")
    os.makedirs(saved, exist_ok=True)
    with open(os.path.join(saved, "KitPilotLayout.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")


def main():
    if unreal is None:
        if len(sys.argv) >= 2 and sys.argv[1] == "plan":
            _, pilot, specs = plan_all()
            data = write_outputs(pilot, specs)
            with open(os.path.join(OUT_DIR, "kit_pilot_plan.json"), "w", encoding="utf-8") as f:
                json.dump(specs, f, indent=0)
            print(json.dumps({k: data[k] for k in ("kinds", "hidden_proxies", "parts", "stretch")}, indent=1))
            return
        print(__doc__)
        return
    if MODE == "shots":
        shots()
        return
    try:
        report("kit_pilot_building.py build %s <- %s (%s) %s" % (TARGET, os.path.basename(LAYOUT_FILE), BUILDING_ID, datetime.datetime.now().isoformat()))
        build()
    except Exception as exc:
        import traceback
        report("ERROR: %r\n%s" % (exc, traceback.format_exc()))
    flush_log()


if unreal is not None or __name__ == "__main__":
    main()
