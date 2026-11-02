"""The playtest bot's planned route for a GENERATED map (docs/design/levelgen/LEVEL_GENERATOR.md section 8, docs/design/COMBAT_LAB.md
section 7.2): turns the accepted layout's MANDATORY route into the level JSON "bot_route" block (the format of
Content/Data/LevelJson/L_FormationLabCombat.json, parsed by BotRouteRules::Parse), and lists the DROP nav links the builder places
through the roof / floor holes. Pure Python (no `unreal`): build_levelgen_map.py imports it, and it runs alone to patch a level JSON
without rebuilding the map:

  python Scripts/Editor/levelgen_bot_route.py <accepted layout.json> [<level json>]      (default level JSON: by the map name)
  python Scripts/Editor/levelgen_bot_route.py --test                                     (the self-test)

The route block:
  - one waypoint per route point: "at" [x, y], "z" = the floor height of its layer (ground 0, cut -300, floor n / roof = n x 300;
    the bot clicks just above that level, so a waypoint under a roof is never a click on the roof), "label";
  - a point that lies over an open hole of its own level (a roof hole for a roof point, a floor hole for a floor point) is moved out
    to the hole's rim + RIM_MARGIN_CM (a point over a hole is off the navmesh);
  - a leg that goes through a hole link ("links": {"at": i, "via": "hole_*"}) marks its end waypoint "drop": "<via>" - the squad
    drops through the builder's drop nav link (the bot reports whether the leader fell or the drop had to be scripted);
  - the last point gets a "wall" action when the route expects the formation wall layout in the zone it lies in (a click on the
    nearest ground wall within WALL_SEARCH_CM);
  - "loop": true (the timed bot runs walk it again and again; a lap = the route once).
UE-only, no Godot reference.
"""
import json
import math
import os
import sys

LEVEL_HEIGHT = 300.0      # cm per height level (schema.ts LEVEL_HEIGHT)
HOLE_HALF = 120.0         # cm: the ruin's roof / floor holes (build_levelgen_map.slabs_with_holes)
RIM_MARGIN_CM = 70.0      # a waypoint pushed off a hole stands this far outside its edge (the navmesh is eroded ~40 cm there)
DROP_START_CM = 70.0      # a drop link starts this far outside the hole's edge on the upper slab
WALL_SEARCH_CM = 800.0
WALL_MIN_LENGTH_CM = 200.0
WALL_MIN_HEIGHT_CM = 120.0

FIGHT_COVER = {"enabled": True, "search_cm": 1500, "hold_sec": 45, "min_wall_cm": 200, "min_height_cm": 120, "min_threat_cm": 400,
               "min_facing_cos": 0.5}


def layer_level(layout, layer):
    """ground 0, cut = the cut's level (-1), floor:<b>:<n> = n, roof:<b> = the building's floors (schema.ts layerLevel)."""
    if layer == "ground":
        return 0
    if layer == "cut":
        return int((layout.get("cut") or {}).get("level", -1))
    kind, _, rest = layer.partition(":")
    bid, _, n = rest.partition(":")
    b = next((x for x in layout.get("buildings", []) if x["id"] == bid), None)
    if b is None:
        return 0
    return int(b["floors"]) if kind == "roof" else int(n or 1)


def holes_on(layout, layer):
    """The open holes [(x, y, half)] in the slab a layer walks on: a roof's roof holes, a floor's floor holes."""
    kind, _, rest = layer.partition(":")
    bid, _, n = rest.partition(":")
    b = next((x for x in layout.get("buildings", []) if x["id"] == bid), None)
    if b is None:
        return []
    if kind == "roof":
        return [(float(h[0]), float(h[1]), HOLE_HALF) for h in b.get("roof_holes", [])]
    if kind == "floor":
        return [(float(h["pos"][0]), float(h["pos"][1]), HOLE_HALF) for h in b.get("floor_holes", []) if int(h.get("floor", 1)) == int(n or 1)]
    return []


def push_off_holes(point, holes):
    """A point inside a square hole (+ the rim margin) moved out along its shallowest side; else unchanged."""
    x, y = float(point[0]), float(point[1])
    for hx, hy, half in holes:
        reach = half + RIM_MARGIN_CM
        dx, dy = x - hx, y - hy
        if abs(dx) < reach and abs(dy) < reach:
            if reach - abs(dx) <= reach - abs(dy):
                x = hx + math.copysign(reach, dx if dx != 0 else -1.0)
            else:
                y = hy + math.copysign(reach, dy if dy != 0 else -1.0)
    return [round(x), round(y)]


def point_in_poly(p, poly):
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if (yi > p[1]) != (yj > p[1]) and p[0] < (xj - xi) * (p[1] - yi) / ((yj - yi) or 1e-9) + xi:
            inside = not inside
        j = i
    return inside


def nearest_wall_point(layout, p):
    """The closest point on a ground wall (>= WALL_MIN_LENGTH_CM long, >= WALL_MIN_HEIGHT_CM tall) within WALL_SEARCH_CM, or None."""
    best, best_d = None, WALL_SEARCH_CM
    for w in layout.get("walls", []):
        if int(w.get("floor", 0)) != 0 or float(w.get("height", 0)) < WALL_MIN_HEIGHT_CM or w.get("kind") == "fence":
            continue
        (ax, ay), (bx, by) = w["a"], w["b"]
        L2 = (bx - ax) ** 2 + (by - ay) ** 2
        if L2 < WALL_MIN_LENGTH_CM ** 2:
            continue
        t = max(0.0, min(1.0, ((p[0] - ax) * (bx - ax) + (p[1] - ay) * (by - ay)) / L2))
        q = (ax + t * (bx - ax), ay + t * (by - ay))
        d = math.hypot(p[0] - q[0], p[1] - q[1])
        if d < best_d:
            best, best_d = [round(q[0]), round(q[1])], d
    return best


def mandatory_route(layout):
    return next((r for r in layout.get("routes", []) if r.get("mandatory")), None)


def bot_route(layout, route=None):
    """The level JSON "bot_route" block from the layout's mandatory route (or `route`); None without one."""
    route = route or mandatory_route(layout)
    if not route or len(route.get("points", [])) < 2:
        return None
    points = route["points"]
    layers = route.get("layers") or ["ground"] * len(points)
    links = {int(l["at"]): str(l["via"]) for l in route.get("links", [])}
    wall_zones = {e["zone"] for e in route.get("expect", []) if e.get("formation") == "wall"}
    out = []
    for i, p in enumerate(points):
        layer = layers[i] if i < len(layers) else "ground"
        at = push_off_holes(p, holes_on(layout, layer))
        via = links.get(i, "")
        label = "start (spawn)" if i == 0 else ("end" if i == len(points) - 1 else "%s %d" % (route["id"], i))
        label += " - %s" % layer
        if via and not via.startswith("hole"):
            label += " via %s" % via  # a drop names its hole in "drop" (the report prints it)
        wp = {"at": at, "z": round(layer_level(layout, layer) * LEVEL_HEIGHT), "label": label}
        if via.startswith("hole"):
            wp["drop"] = via
        if i == len(points) - 1 and layer == "ground":
            zone = next((z for z in layout.get("zones", []) if z["id"] in wall_zones and point_in_poly(at, z["poly"])), None)
            wall = nearest_wall_point(layout, at) if zone else None
            if wall:
                wp.update({"action": "wall", "wall": wall, "hold_sec": 3})
                wp["label"] += " (wall layout, %s)" % zone["id"]
        out.append(wp)
    return {
        "name": "%s_%s" % (layout.get("name", "levelgen"), route["id"]),
        "comment": "Generated by Scripts/Editor/levelgen_bot_route.py from the accepted layout's route %s (docs/design/levelgen/LEVEL_GENERATOR.md "
                   "section 8): the playtest bot leads the squad along it in the timed bot runs (loop). 'z' = the waypoint's floor height; "
                   "'drop' = the leg drops through that roof / floor hole (the builder's drop nav link)." % route["id"],
        "loop": True,
        "reach_radius_cm": 250,
        "stuck_after_sec": 12,
        "stuck_move_cm": 150,
        "max_retries": 2,
        "fight_hold_sec": 20,
        "fight_cover": dict(FIGHT_COVER),
        "action_timeout_sec": 45,
        "points": out,
    }


def drop_links(layout, ground_z=0.0):
    """The one-way DROP nav links through the roof / floor holes: per hole four links (one from each side, starting DROP_START_CM
    outside its edge on the upper slab) down to the hole's centre on the slab one level below. Only one-level drops (3 m) onto a
    real slab: a hollow building's roof hole (no floor below but the ground 6-9 m down) gets none.
    -> [{"id", "hole": [x, y], "upper_z", "lower_z", "starts": [[x, y], ...]}]"""
    out = []
    for b in layout.get("buildings", []):
        floors = int(b["floors"])
        hollow = bool(b.get("hollow"))
        holes = [("roof", floors, (float(h[0]), float(h[1]))) for h in b.get("roof_holes", [])]
        holes += [("floor", int(h.get("floor", 1)), (float(h["pos"][0]), float(h["pos"][1]))) for h in b.get("floor_holes", [])]
        floor_holes = {}
        for kind, lvl, (hx, hy) in holes:
            if kind == "floor":
                floor_holes.setdefault(lvl, []).append((hx, hy))
        roof_n = 0
        floor_n = {}
        for kind, lvl, (hx, hy) in holes:
            lower = lvl - 1
            if lower < 0 or (lower > 0 and hollow):
                continue
            if any(abs(hx - fx) < 2 * HOLE_HALF and abs(hy - fy) < 2 * HOLE_HALF for fx, fy in floor_holes.get(lower, [])):
                continue  # a hole straight below: no slab to land on
            if kind == "roof":
                hid = "hole_roof_%d" % roof_n
                roof_n += 1
            else:
                hid = "hole_floor_%d" % floor_n.get(lvl, 0) if lvl == 1 else "hole_floor%d_%d" % (lvl, floor_n.get(lvl, 0))
                floor_n[lvl] = floor_n.get(lvl, 0) + 1
            r = HOLE_HALF + DROP_START_CM
            out.append({"id": "%s_%s" % (b["id"], hid), "via": hid, "building": b["id"], "hole": [hx, hy],
                        "upper_z": ground_z + lvl * LEVEL_HEIGHT, "lower_z": ground_z + lower * LEVEL_HEIGHT,
                        "starts": [[hx + r, hy], [hx - r, hy], [hx, hy + r], [hx, hy - r]]})
    return out


def patch_level_json(layout, level_path):
    """Adds / replaces "bot_route" in an existing level JSON (the rest of the file untouched, its key order kept). -> the block."""
    with open(level_path, encoding="utf-8") as f:
        level = json.load(f)
    block = bot_route(layout)
    if block is None:
        raise ValueError("%s: no mandatory route" % layout.get("name"))
    if "bot_route" in level:
        level["bot_route"] = block
    else:
        # before "waves" like the combat lab's file (readable order); else at the end
        items = list(level.items())
        at = next((i for i, (k, _) in enumerate(items) if k == "waves"), len(items))
        items.insert(at, ("bot_route", block))
        level = dict(items)
    with open(level_path, "w", encoding="utf-8") as f:
        json.dump(level, f, indent=2)
        f.write("\n")
    return block


def _self_test():
    layout = {
        "name": "t", "buildings": [{"id": "B", "floors": 2, "hollow": False, "roof_holes": [[0, 0]], "floor_holes": [{"pos": [1000, 0], "floor": 1}]},
                                   {"id": "H", "floors": 3, "hollow": True, "roof_holes": [[5000, 0]], "floor_holes": []}],
        "walls": [{"id": "W", "a": [-500, 900], "b": [500, 900], "height": 300, "thickness": 40, "kind": "wall"}],
        "zones": [{"id": "Z_end", "poly": [[-600, 300], [600, 300], [600, 1000], [-600, 1000]]}],
        "routes": [{"id": "R", "mandatory": True, "points": [[-500, 0], [-150, 0], [-100, 0], [980, 0], [-200, 500], [0, 600]],
                    "layers": ["ground", "roof:B", "floor:B:1", "floor:B:1", "ground", "ground"],
                    "links": [{"at": 2, "via": "hole_roof_0"}, {"at": 4, "via": "hole_floor_0"}],
                    "expect": [{"zone": "Z_end", "formation": "wall"}]}],
    }
    r = bot_route(layout)
    pts = r["points"]
    assert [p["z"] for p in pts] == [0, 600, 300, 300, 0, 0], pts
    assert pts[1]["at"] == [-190, 0], pts[1]          # a roof point over the roof hole -> its rim (120 + 70)
    assert pts[2]["at"] == [-100, 0], pts[2]          # a floor point UNDER the roof hole stays (its own slab has no hole there)
    assert pts[3]["at"] == [810, 0], pts[3]           # a floor point over the floor hole -> its rim
    assert pts[2]["drop"] == "hole_roof_0" and pts[4]["drop"] == "hole_floor_0" and "drop" not in pts[1]
    assert pts[5]["action"] == "wall" and pts[5]["wall"] == [0, 900], pts[5]
    assert r["loop"] is True and r["name"] == "t_R"
    links = drop_links(layout)
    assert [l["via"] for l in links] == ["hole_roof_0", "hole_floor_0"], links   # the hollow hall's 9 m roof hole gets none
    assert links[0]["upper_z"] == 600 and links[0]["lower_z"] == 300 and links[1]["lower_z"] == 0
    assert links[0]["starts"][0] == [190.0, 0.0]
    print("levelgen_bot_route self-test: ok")


def main(argv):
    if len(argv) >= 2 and argv[1] == "--test":
        _self_test()
        return 0
    if len(argv) < 2:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        layout = json.load(f)
    if len(argv) >= 3:
        level_path = argv[2]
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        sys.path.insert(0, here)
        short = layout["profile"].replace("ref_", "").replace("01_02_", "")
        level_path = os.path.join(here, "..", "..", "Content", "Data", "LevelJson", "L_Gen_%s_%d.json" % (short.capitalize(), int(layout["seed"])))
    block = patch_level_json(layout, level_path)
    print(json.dumps({"level_json": os.path.abspath(level_path), "bot_route": block["name"], "points": len(block["points"]),
                      "drops": [p["drop"] for p in block["points"] if "drop" in p], "drop_links": [l["id"] for l in drop_links(layout)]}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
