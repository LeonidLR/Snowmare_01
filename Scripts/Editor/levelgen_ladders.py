"""The ladders and the cut's ways in / out of a GENERATED map (docs/design/levelgen/LEVEL_GENERATOR.md section 7, docs/design/LADDERS.md
3.1), pure Python (no `unreal`): build_levelgen_map.py imports it; alone it prints what it would do with an accepted layout, or runs its
self-test:

  python Scripts/Editor/levelgen_ladders.py <accepted layout.json>     (the ladder frames, the exit gaps, the cut ramps as JSON)
  python Scripts/Editor/levelgen_ladders.py --test                     (the self-test)

Bug fix 2026-11-02 (the game designer playing L_Gen_Depot_1: "the ladders are sunk into the walls, nobody can climb them; the cut can
be neither entered nor climbed out of"):
  - LADDER YAW from the geometry: the actor's +X points away from the wall towards the climber (LADDERS.md 3.1) = from `pos` to the
    layout's `foot` (1 m out, where a route arrives). The layouts generated before the fix carry a MIRRORED yaw (generate.ts yawToUE was
    90 - profile instead of profile + 90): every ladder faced the wall - its hang line, bottom link point and plinth inside the wall,
    its top link point in the air off the roof's edge, its brackets and grab bars sticking out of the wall;
  - THE RUNG PLANE 10 cm off the wall's OUTER face: the builder stands a wall on its centre line, so a building's 30 cm perimeter wall
    reaches 15 cm past the footprint the generator measured `pos` from - the rung plane was 5 cm INSIDE the wall. The backing wall is
    found among the walls standing on the ladder's lower level (and the trench's retaining faces for a ladder in the cut) and the ladder
    is moved out along its +X until the plane is LADDER_FACE_GAP_CM off that face;
  - A LADDER'S TOP EXIT through a wall on the upper level: the cut's 1.5 m thick, 2.2 m tall retaining wall stands right behind the cut
    ladders' top - the climber climbed out into it. Every wall on the ladder's upper level that crosses the strip from the rung plane back
    to the layout's `top` point gets an opening LADDER_EXIT_GAP_CM wide centred on the ladder;
  - THE CUT RAMP: the old ramp ran from 5 m before the crossing's middle (in the air over the trench, nothing walkable there) to 3 m
    past it, under the bridge slab with 0.7 m of head room - it joined nothing. Now it leaves the bridge slab's far edge (tucked 5 cm
    under it, like the external stair's landing) and runs down ALONG the trench onto its floor at ~23 deg, beside the trench walkway;
  - the trench's retaining faces stand OUTSIDE the trench (their inner face on the trench edge), not on its centre line (they narrowed
    the trench floor by 10 cm a side and put the cut ladders' rung plane on the face).
UE-only, no Godot reference.
"""
import json
import math
import sys

LEVEL_HEIGHT = 300.0          # cm per height level (schema.ts LEVEL_HEIGHT)
LADDER_FACE_GAP_CM = 10.0     # the rung plane stands this far off the wall's outer face (LADDERS.md 3.1, the generator's 0.1 m)
LADDER_EXIT_GAP_CM = 200.0    # the opening in an upper-level wall behind a ladder's top: 2 m (the 35 cm agent leaves 1.3 m to walk)
LADDER_WALL_SEARCH_CM = 80.0  # a wall whose outer face is at most this far behind the rung plane backs the ladder
CUT_FACE_T = 20.0             # cm, the trench's retaining faces below ground
CUT_RAMP_RUN = 700.0          # cm: the ramp's horizontal run for the 3 m drop (23 deg; walkable up to 44)
CUT_RAMP_W = 300.0            # cm: the ramp's width (the 6 m trench keeps a 1.5 m walkway on each side)
RAMP_TUCK = 5.0               # cm: the ramp's top end slides this far under the bridge slab (no gap, a ~2 cm lip)


def _unit(dx, dy):
    n = math.hypot(dx, dy)
    return (dx / n, dy / n) if n > 1e-6 else (0.0, 0.0)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def cut_rect(cut):
    """(x0, y0, x1, y1, band_x): the trench's axis-aligned rectangle; band_x = its short side is x (it runs along y)."""
    px = [p[0] for p in cut["poly"]]
    py = [p[1] for p in cut["poly"]]
    x0, x1, y0, y1 = min(px), max(px), min(py), max(py)
    return x0, y0, x1, y1, (x1 - x0) < (y1 - y0)


def cut_face_walls(cut):
    """The trench's two retaining faces below ground as wall dicts (centre line CUT_FACE_T / 2 OUTSIDE the trench edge, so the inner
    face is the edge itself), standing on the cut's level. The ends of the band (the map's edge) stay open."""
    x0, y0, x1, y1, band_x = cut_rect(cut)
    h = CUT_FACE_T / 2.0
    lvl = int(cut.get("level", -1))
    if band_x:
        segs = [((x0 - h, y0), (x0 - h, y1)), ((x1 + h, y0), (x1 + h, y1))]
    else:
        segs = [((x0, y0 - h), (x1, y0 - h)), ((x0, y1 + h), (x1, y1 + h))]
    return [{"id": "Cut_Face_%d" % n, "a": [a[0], a[1]], "b": [b[0], b[1]], "thickness": CUT_FACE_T, "height": -lvl * LEVEL_HEIGHT,
             "floor": lvl, "kind": "cut_face"} for n, (a, b) in enumerate(segs)]


def _wall_frame(w):
    a, b = w["a"], w["b"]
    length = math.hypot(b[0] - a[0], b[1] - a[1])
    return a, _unit(b[0] - a[0], b[1] - a[1]), length


def ladder_frame(ld, walls):
    """The ladder actor's real frame: {id, pos, yaw, out, shift_cm, layout_yaw, yaw_fixed, backing}. `out` (the actor's +X) points from
    `pos` to `foot`; `pos` moves along it so the rung plane is LADDER_FACE_GAP_CM off the outer face of the nearest wall behind it
    (walls = the walls standing on the ladder's lower level)."""
    pos = (float(ld["pos"][0]), float(ld["pos"][1]))
    layout_yaw = float(ld.get("yaw", 0.0))
    out = _unit(ld["foot"][0] - pos[0], ld["foot"][1] - pos[1]) if "foot" in ld else (0.0, 0.0)
    if out == (0.0, 0.0):
        out = (math.cos(math.radians(layout_yaw)), math.sin(math.radians(layout_yaw)))
    yaw = math.degrees(math.atan2(out[1], out[0])) % 360.0
    best = None
    for w in walls:
        a, wv, length = _wall_frame(w)
        if length < 1.0 or abs(_dot(wv, out)) > 0.25:
            continue  # not parallel to the rung plane
        rel = (pos[0] - a[0], pos[1] - a[1])
        t = _dot(rel, wv)
        if t < -30.0 or t > length + 30.0:
            continue  # the ladder is not in front of this wall
        s = _dot(rel, out)                       # the rung plane's distance in front of the wall's centre line
        face = s - float(w["thickness"]) / 2.0   # ... in front of its outer face (negative = the plane is inside the wall)
        if -float(w["thickness"]) - 5.0 <= face <= LADDER_WALL_SEARCH_CM and (best is None or face < best[0]):
            best = (face, w)
    shift = 0.0
    if best is not None:
        shift = LADDER_FACE_GAP_CM - best[0]
        pos = (pos[0] + out[0] * shift, pos[1] + out[1] * shift)
    delta = abs((yaw - layout_yaw + 180.0) % 360.0 - 180.0)
    return {"id": ld["id"], "pos": [round(pos[0], 1), round(pos[1], 1)], "yaw": round(yaw, 3), "out": [round(out[0], 6), round(out[1], 6)],
            "shift_cm": round(shift, 1), "layout_yaw": layout_yaw, "yaw_fixed": delta > 1.0, "backing": best[1]["id"] if best else None}


def ladder_frames(layout):
    """Every ladder's frame (ladder_frame) against the walls of its lower level (+ the trench faces for a ladder in the cut)."""
    faces = cut_face_walls(layout["cut"]) if layout.get("cut") else []
    out = []
    for ld in layout.get("ladders", []):
        lvl = int(ld.get("level_from", 0))
        walls = [w for w in layout.get("walls", []) if int(w.get("floor", 0)) == lvl] + [f for f in faces if int(f["floor"]) == lvl]
        out.append(ladder_frame(ld, walls))
    return out


def exit_gaps(layout, frames):
    """{wall id: [(t0, t1, ladder id), ...]}: the openings (along the wall from its `a`) where a ladder's top exit crosses a wall standing
    on the ladder's upper level: the strip from the rung plane back to the layout's `top` point (+ the wall's thickness)."""
    by_id = {f["id"]: f for f in frames}
    gaps = {}
    for ld in layout.get("ladders", []):
        f = by_id[ld["id"]]
        pos, out = f["pos"], f["out"]
        depth = _dot((pos[0] - ld["top"][0], pos[1] - ld["top"][1]), out) if "top" in ld else 150.0
        depth = max(depth, 100.0)
        lvl = int(ld.get("level_to", 0))
        for w in layout.get("walls", []):
            if int(w.get("floor", 0)) != lvl:
                continue
            a, wv, length = _wall_frame(w)
            if length < 1.0 or abs(_dot(wv, out)) > 0.25:
                continue
            rel = (pos[0] - a[0], pos[1] - a[1])
            t = _dot(rel, wv)
            s = _dot(rel, out)  # > 0: the wall's centre line is behind the rung plane
            half = float(w["thickness"]) / 2.0
            if t < -LADDER_EXIT_GAP_CM / 2.0 or t > length + LADDER_EXIT_GAP_CM / 2.0 or s + half < 0.0 or s - half > depth:
                continue
            gaps.setdefault(w["id"], []).append((t - LADDER_EXIT_GAP_CM / 2.0, t + LADDER_EXIT_GAP_CM / 2.0, ld["id"]))
    return gaps


def split_walls(walls, gaps, min_piece_cm=10.0):
    """The walls with the openings cut out: a wall with gaps becomes its pieces (`<id>_l<n>`); the rest unchanged."""
    out = []
    for w in walls:
        cuts = sorted(gaps.get(w["id"], []))
        if not cuts:
            out.append(w)
            continue
        a, wv, length = _wall_frame(w)
        t = 0.0
        pieces = []
        for t0, t1, _ in cuts:
            if t0 - t >= min_piece_cm:
                pieces.append((t, min(t0, length)))
            t = max(t, t1)
        if length - t >= min_piece_cm:
            pieces.append((t, length))
        for n, (p0, p1) in enumerate(pieces):
            if p1 - p0 < min_piece_cm:
                continue
            piece = dict(w)
            piece["id"] = "%s_l%d" % (w["id"], n)
            piece["a"] = [round(a[0] + wv[0] * p0, 1), round(a[1] + wv[1] * p0, 1)]
            piece["b"] = [round(a[0] + wv[0] * p1, 1), round(a[1] + wv[1] * p1, 1)]
            out.append(piece)
    return out


def cut_ramps(cut, ground_z):
    """The ramps into the trench, one per crossing of kind "ramp": {id, crossing, a, b, z0, z1, width} - from the crossing (bridge) slab's
    far edge along the band (RAMP_TUCK under the slab, at ground level) down onto the trench floor CUT_RAMP_RUN further on; or towards
    the band's start when the far side has no room."""
    x0, y0, x1, y1, band_x = cut_rect(cut)
    cz = ground_z + int(cut.get("level", -1)) * LEVEL_HEIGHT
    out = []
    for x in cut.get("crossings", []):
        if x.get("kind") != "ramp":
            continue
        cx = [p[0] for p in x["poly"]]
        cy = [p[1] for p in x["poly"]]
        if band_x:
            lat, lo, hi, end_lo, end_hi = (x0 + x1) / 2.0, min(cy), max(cy), y0, y1
        else:
            lat, lo, hi, end_lo, end_hi = (y0 + y1) / 2.0, min(cx), max(cx), x0, x1
        if hi + CUT_RAMP_RUN <= end_hi - 100.0:
            s0, s1 = hi - RAMP_TUCK, hi + CUT_RAMP_RUN
        else:
            s0, s1 = lo + RAMP_TUCK, lo - CUT_RAMP_RUN
        a = [lat, s0] if band_x else [s0, lat]
        b = [lat, s1] if band_x else [s1, lat]
        out.append({"id": "Cut_%s_ramp" % x["id"], "crossing": x["id"], "a": a, "b": b, "z0": ground_z, "z1": cz, "width": CUT_RAMP_W})
    return out


def summary(layout):
    frames = ladder_frames(layout)
    gaps = exit_gaps(layout, frames)
    return {"ladders": frames, "exit_gaps": {k: [[round(t0, 1), round(t1, 1), lid] for t0, t1, lid in v] for k, v in gaps.items()},
            "cut_ramps": cut_ramps(layout["cut"], float(layout.get("ground_z", 0.0))) if layout.get("cut") else []}


def _self_test():
    # A building face (a 30 cm perimeter wall on the footprint line x = 4000) with the ladder 10 cm off the FOOTPRINT, the generator's
    # mirrored yaw (0 instead of 180): the frame faces away from the wall (-X) and stands 10 cm off the wall's outer face (3985).
    walls = [{"id": "B_S", "a": [4000, -8450], "b": [4000, -7250], "thickness": 30, "height": 300, "floor": 0},
             {"id": "far", "a": [3000, -8450], "b": [3000, -7250], "thickness": 30, "height": 300, "floor": 0}]
    f = ladder_frame({"id": "L", "pos": [3990, -7810], "foot": [3890, -7810], "top": [4100, -7810], "yaw": 0}, walls)
    assert f["yaw"] == 180.0 and f["yaw_fixed"] and f["backing"] == "B_S", f
    assert f["pos"] == [3975.0, -7810.0] and f["shift_cm"] == 15.0, f
    # Already right (yaw, 10 cm off the face): unchanged.
    f = ladder_frame({"id": "L", "pos": [3975, -7810], "foot": [3875, -7810], "yaw": 180}, walls)
    assert f["pos"] == [3975.0, -7810.0] and not f["yaw_fixed"] and abs(f["shift_cm"]) < 1e-6, f
    # A free-standing ladder (no wall within reach): only the yaw.
    f = ladder_frame({"id": "L", "pos": [0, 0], "foot": [0, 100], "yaw": 0}, walls)
    assert f["backing"] is None and f["pos"] == [0.0, 0.0] and f["yaw"] == 90.0, f
    # The cut: a band along y, x -317..283 at level -1; a ladder on the south face (x = -317) climbing out to the south ground over the
    # 1.5 m thick cut wall (-467..-317): the faces stand outside the trench, the ladder keeps its 10 cm, the wall gets a 2 m opening.
    layout = {
        "ground_z": 0,
        "cut": {"poly": [[283, -8850], [283, 8850], [-317, 8850], [-317, -8850]], "level": -1,
                "crossings": [{"id": "X_c", "kind": "ramp", "poly": [[433, -3562], [433, -3062], [-467, -3062], [-467, -3562]]},
                              {"id": "X_b", "kind": "bridge", "poly": [[433, 7769], [433, 8169], [-467, 8169], [-467, 7769]]}]},
        "walls": [{"id": "CW_S", "a": [-392, -3062], "b": [-392, 7769], "thickness": 150, "height": 220, "kind": "cut_wall"},
                  {"id": "CW_N", "a": [358, -3062], "b": [358, 7769], "thickness": 150, "height": 220, "kind": "cut_wall"}],
        "ladders": [{"id": "LC", "pos": [-307, 2700], "foot": [-207, 2700], "top": [-567, 2700], "yaw": 180, "level_from": -1, "level_to": 0}],
    }
    faces = cut_face_walls(layout["cut"])
    assert faces[0]["a"] == [-327.0, -8850] and faces[1]["a"] == [293.0, -8850] and faces[0]["height"] == 300.0, faces
    frames = ladder_frames(layout)
    assert frames[0]["yaw"] == 0.0 and frames[0]["pos"] == [-307.0, 2700.0] and frames[0]["backing"] == "Cut_Face_0", frames
    gaps = exit_gaps(layout, frames)
    assert list(gaps) == ["CW_S"], gaps                      # the north wall is in front of the ladder, never cut
    t0, t1, lid = gaps["CW_S"][0]
    assert lid == "LC" and abs(t0 - (2700 + 3062 - 100)) < 1e-6 and abs(t1 - (2700 + 3062 + 100)) < 1e-6, gaps
    pieces = split_walls(layout["walls"], gaps)
    assert [p["id"] for p in pieces] == ["CW_S_l0", "CW_S_l1", "CW_N"], pieces
    assert pieces[0]["b"] == [-392.0, 2600.0] and pieces[1]["a"] == [-392.0, 2800.0], pieces
    ramps = cut_ramps(layout["cut"], 0.0)
    assert len(ramps) == 1 and ramps[0]["a"] == [-17.0, -3067.0] and ramps[0]["b"] == [-17.0, -3062.0 + CUT_RAMP_RUN], ramps
    assert ramps[0]["z0"] == 0.0 and ramps[0]["z1"] == -300.0
    # A ramp crossing near the band's far end runs the other way (still from the slab's edge).
    near_end = dict(layout["cut"], crossings=[{"id": "X_e", "kind": "ramp", "poly": [[433, 8000], [433, 8500], [-467, 8500], [-467, 8000]]}])
    r = cut_ramps(near_end, 0.0)[0]
    assert r["a"] == [-17.0, 8005.0] and r["b"] == [-17.0, 8000.0 - CUT_RAMP_RUN], r
    print("levelgen_ladders self-test: ok")


def main(argv):
    if len(argv) >= 2 and argv[1] == "--test":
        _self_test()
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        layout = json.load(f)
    print(json.dumps(summary(layout), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
