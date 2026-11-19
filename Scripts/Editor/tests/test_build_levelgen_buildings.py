"""Unit tests for build_levelgen_map.py combat-building extensions (028) and legacy Depot parity."""

import hashlib
import json
import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402
import levelgen_bot_route  # noqa: E402


class BuildLevelGenBuildingsTest(unittest.TestCase):
    def _load(self, name):
        return builder.load_layout(os.path.join(ACCEPTED_DIR, f"{name}.json"))

    def test_legacy_depot_bit_identical(self):
        depot = self._load("gen_ref_01_02_depot_1")
        specs = builder.plan(depot)
        self.assertEqual(builder.map_name_for(depot), "/Game/Maps/Generated/L_Gen_Depot_1")
        self.assertEqual(len(specs), 545)
        sha = hashlib.sha256(json.dumps(specs, sort_keys=True).encode()).hexdigest()[:16]
        # 2026-11-15 mantling slice 1: the designer's obstacle height rework (Scripts/Tools/apply_depot_height_classes.py) - was 9a37162a0b7bc284
        self.assertEqual(sha, "a93f43a8e33e0250")

    def test_train_1_openings_partitions_no_doubles_and_parapets(self):
        layout = self._load("L_Gen_Train_1")
        self.assertEqual(builder.map_name_for(layout), "/Game/Maps/Generated/L_Gen_Train_1")
        specs = builder.plan(layout)

        window_sills = [s for s in specs if "window_sill" in s.get("tags", [])]
        mantle_sills = [s for s in specs if "mantle_sill" in s.get("tags", [])]
        lintels = [s for s in specs if "opening_lintel" in s.get("tags", [])]
        parapets = [s for s in specs if "parapet" in s.get("tags", [])]
        interiors = [s for s in specs if "interior" in s.get("tags", []) and "opening_lintel" not in s.get("tags", [])]

        # B_house + B_garage: ground windows get vaultable window_sill, floor 1 windows get mantle_sill
        self.assertGreater(len(window_sills), 0)
        self.assertGreater(len(mantle_sills), 0)
        # house 2 doors + 17 windows, garage 2 doors + 2 windows, 6 partition doors (2026-11-19 S2 window clearances: the arena
        # regenerated without the windows onto the stair core / within a partition end's jamb - was 2 + 23, 2 + 3, 6 = 36)
        self.assertEqual(len(lintels), (2 + 17) + (2 + 2) + 6)
        # Window sills: measured low_vault target 95 cm (MANTLING.md 11, 58a7e34e) + CodexMantleEdge tag
        for s in window_sills:
            self.assertAlmostEqual(s["scale"][2] * 100.0, 95.0, places=1)
            self.assertIn("CodexMantleEdge", s["tags"])
        # No parapet may carry "Vault" tag
        for p in parapets:
            self.assertNotIn("Vault", p["tags"])
            self.assertIn("CodexMantleEdge", p["tags"])
        # Interior walls count matches layout interior walls (no double-spawning from b["partitions"])
        layout_interior_count = sum(1 for w in layout["walls"] if w.get("kind") == "interior")
        self.assertEqual(len(interiors), layout_interior_count)
        # Parapets cut around LAD_B_house exit on East edge -> >= 5 parapet pieces instead of 4 uncut sides
        self.assertGreaterEqual(len(parapets), 5)

    def test_train_2_hatches_loopholes_and_ladder_standoff(self):
        layout = self._load("L_Gen_Train_2")
        specs = builder.plan(layout)

        loophole_sills = [s for s in specs if "loophole_sill" in s.get("tags", [])]
        self.assertEqual(len(loophole_sills), 31)  # 22 checkpoint + 7 bunker + 2 tower
        for s in loophole_sills:
            self.assertAlmostEqual(s["scale"][2] * 100.0, 120.0, places=1)
        # Loophole lintels preserve exact JSON top_height (150 on B_bunker f=0 -> bottom Z 150.0, no hidden clamp)
        bunker_lintels = [
            s for s in specs
            if "opening_lintel" in s.get("tags", [])
            and "building=B_bunker" in s.get("tags", [])
            and "door_lintel" not in s.get("tags", [])
        ]
        self.assertEqual(len(bunker_lintels), 7)  # B_bunker has 7 loopholes + 1 door
        for s in bunker_lintels:
            bottom_z = s["location"][2] - s["scale"][2] * 50.0
            self.assertAlmostEqual(bottom_z, 150.0, places=1)

        # Free-standing tower hatch ladder: wall_bracket_count=-1, top_standoff=110, top_exit_standoff=110, 3-sided hatch rim
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertEqual(ladders["LAD_B_tower_hatch"].get("wall_bracket_count"), -1)
        self.assertAlmostEqual(ladders["LAD_B_tower_hatch"].get("top_standoff", 0.0), 110.0, places=1)
        self.assertAlmostEqual(ladders["LAD_B_tower_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
        self.assertNotIn("top_standoff", ladders["LAD_B_checkpoint"])
        self.assertNotIn("top_exit_standoff", ladders["LAD_B_checkpoint"])

        # Hatches NEVER produce DROP NavLinkProxy actors
        drops = levelgen_bot_route.drop_links(layout, 0.0)
        self.assertEqual(len(drops), 0)
        rims = [s for s in specs if "hatch_rim" in s.get("tags", [])]
        self.assertEqual(len(rims), 3)  # 3 non-exit sides

        # B_tower roof slab at X[-1400..-600], Y[-2250..-1450] must NOT cover hatch [-1270..-1170]x[-1680..-1580]
        tower_roof = [s for s in specs if "roof_slab" in s.get("tags", []) and "building=B_tower" in s.get("tags", [])]
        self.assertGreater(len(tower_roof), 1)
        for slab in tower_roof:
            sx, sy = slab["scale"][0] * 100.0, slab["scale"][1] * 100.0
            lx, ly = slab["location"][0], slab["location"][1]
            x0, x1 = lx - sx * 0.5, lx + sx * 0.5
            y0, y1 = ly - sy * 0.5, ly + sy * 0.5
            overlap_x = min(x1, -1170.0) - max(x0, -1270.0)
            overlap_y = min(y1, -1580.0) - max(y0, -1680.0)
            self.assertFalse(overlap_x > 1.0 and overlap_y > 1.0, f"Slab {slab['label']} covers B_tower hatch!")

        # Hatch rims sit strictly outside the hatch hole and skip the East exit side (x = -1170)
        for r in rims:
            self.assertFalse(r["label"].endswith("_rim_E"), f"Exit side rim must be omitted: {r['label']}")

    def test_train_3_catwalk_gaps_stair_parapet_cut_and_hatch(self):
        layout = self._load("L_Gen_Train_3")
        specs = builder.plan(layout)

        # B_catwalk roof parapet S ([550, 1150]..[550, 2750]) must leave the entire STAIR_catwalk_ext landing [2595..2750] clear
        cat_s_parapets = [s for s in specs if s["label"].startswith("B_catwalk_parapet_S")]
        self.assertGreaterEqual(len(cat_s_parapets), 1)
        for p in cat_s_parapets:
            ly = p["location"][1]
            sy = p["scale"][1] * 100.0
            y0, y1 = ly - sy * 0.5, ly + sy * 0.5
            self.assertFalse(y0 < 2750.0 and y1 > 2595.0, f"Parapet {p['label']} [{y0}..{y1}] overlaps STAIR_catwalk_ext landing [2595..2750]!")

        # B_warehouse hatch [-...] -> 3-sided rim, 110 standoff, roof cutout, floor 1 cutout? (hatch floor=1 -> only roof floor=2 cut)
        ladders = {s["label"]: s for s in specs if s["kind"] == "ladder"}
        self.assertAlmostEqual(ladders["LAD_B_warehouse_hatch"].get("top_standoff", 0.0), 110.0, places=1)
        self.assertAlmostEqual(ladders["LAD_B_warehouse_hatch"].get("top_exit_standoff", 0.0), 110.0, places=1)
        self.assertEqual(ladders["LAD_B_warehouse_hatch"].get("wall_bracket_count"), -1)

        # Covers with mantle_edges get CodexMantleEdge
        mantle_covers = [s for s in specs if s["folder"] == "Covers" and "CodexMantleEdge" in s.get("tags", [])]
        self.assertGreater(len(mantle_covers), 0)

    def test_player_start_follows_objectives_start(self):
        """Designer bug 2026-11-16: the start dragged in Training Ground (objectives.start, source manual) drives the PlayerStart;
        a layout without objectives keeps the old spawn. In-memory copies only: no accepted file is written."""
        depot = self._load("gen_ref_01_02_depot_1")
        self.assertNotIn("objectives", depot)
        gz = float(depot.get("ground_z", 0.0))

        def player_start(layout):
            starts = [s for s in builder.plan(layout) if s["kind"] == "player_start"]
            self.assertEqual(len(starts), 1)
            return starts[0]

        old = player_start(depot)
        self.assertEqual(old["location"][:2], depot["spawn"]["pos"])
        self.assertEqual(old["yaw"], float(depot["spawn"]["yaw"]))
        self.assertAlmostEqual(old["location"][2], gz + builder.START_Z)
        moved = json.loads(json.dumps(depot))
        moved["objectives"] = {"version": 1, "seed": 0, "order": "free", "goals": [],
                               "start": {"pos": [2477, -6184], "yaw": 180, "layer": "ground", "source": "manual"}}
        ps = player_start(moved)
        self.assertEqual(ps["location"][:2], [2477, -6184])
        self.assertEqual(ps["yaw"], 180.0)
        self.assertAlmostEqual(ps["location"][2], gz + builder.START_Z)
        self.assertEqual(builder.start_of(moved), (2477, -6184, 180.0, 0))
        # a start without a yaw keeps the spawn's facing; an empty objectives block falls back to the spawn
        moved["objectives"]["start"].pop("yaw")
        self.assertEqual(builder.start_of(moved)[2], float(depot["spawn"]["yaw"]))
        moved["objectives"] = None
        self.assertEqual(player_start(moved)["location"], old["location"])

    # ---- rotated buildings (designer 2026-11-16 item 4) -------------------------------------------------------------------------

    @staticmethod
    def _rotate_building(layout, bid, deg, ndigits=None):
        """The layout with building `bid` turned by deg about its footprint centre the way Training Ground stores it (rotation.ts):
        "rotation" set, its building-frame fields (building_coord_fields.json) unchanged, every world point of it, its walls and its
        ladders turned. ndigits=2: every turned point rounded to 0.01 cm like the Training Ground's saved files."""
        out = json.loads(json.dumps(layout))
        b = next(x for x in out["buildings"] if x["id"] == bid)
        (x0, y0), (x1, y1) = b["footprint"]
        c = ((x0 + x1) / 2.0, (y0 + y1) / 2.0)
        P = lambda q: [round(v, ndigits) if ndigits is not None else v for v in builder._rot_xy(q, c, deg)]  # noqa: E731
        b["rotation"] = deg
        # building fields per building_coord_fields.json: world ones turn, building-frame ones stay
        builder.map_building_coords(b, {
            "point": lambda q, frame, path: P(q) if frame == "world" else q,
            "rect": lambda r, frame, path: r,
            "direction": lambda v, frame, path: builder._rot_xy(v, (0.0, 0.0), deg),
            "yaw": lambda y, frame, path: y + deg,
        })
        for w in out["walls"]:
            if w.get("building") == bid:
                w["a"], w["b"] = P(w["a"]), P(w["b"])
        for ld in out["ladders"]:
            if bid in ld["from"] + ld["to"]:
                ld["pos"], ld["foot"], ld["top"], ld["yaw"] = P(ld["pos"]), P(ld["foot"]), P(ld["top"]), ld["yaw"] + deg
        return out, c

    def _assert_rotated_plan(self, layout, bid, deg, ndigits=None):
        """The rotated building plans the same actors as its unrotated twin, turned. ndigits: the twin's points rounded (0.01 cm noise
        after turning back -> locations / scales compared to 0.05 cm instead of 1e-3)."""
        flat = builder.plan(layout)
        turned, c = self._rotate_building(layout, bid, deg, ndigits)
        tol = 0.05 if ndigits is not None else 0.0005
        rot = builder.plan(turned)
        wall_ids = tuple(w["id"] for w in layout["walls"] if w.get("building") == bid)  # legacy: its walls go to Walls/L<n>
        mine = lambda s: s.get("folder", "") == "Buildings/%s" % bid or s.get("folder", "").startswith("Buildings/%s/" % bid) or "building=%s" % bid in s.get("tags", []) or s["label"].startswith(wall_ids)  # noqa: E731
        flat_b = {s["label"]: s for s in flat if mine(s)}
        rot_b = {s["label"]: s for s in rot if mine(s)}
        self.assertGreaterEqual(len(flat_b), 1)
        self.assertEqual(sorted(flat_b), sorted(rot_b), "the same actors, only turned")
        for label, s in flat_b.items():
            r = rot_b[label]
            want = builder._rot_xy(s["location"], c, deg)
            for k in range(3):
                self.assertAlmostEqual(r["location"][k], want[k], delta=tol, msg="%s location" % label)
            dyaw = (r["yaw"] - s["yaw"] - deg + 180.0) % 360.0 - 180.0
            self.assertAlmostEqual(dyaw, 0.0, delta=tol, msg="%s: actor yaw = layout rotation" % label)
            if ndigits is None:
                self.assertEqual(r["scale"] if "scale" in r else None, s["scale"] if "scale" in s else None, label)
            else:
                self.assertEqual("scale" in r, "scale" in s, label)
                for k in range(len(s.get("scale", []))):
                    self.assertAlmostEqual(r["scale"][k], s["scale"][k], delta=tol / 100.0, msg="%s scale" % label)
        # everything else stays where it was (its ladders and the drop links through its holes move with it: world points)
        other = lambda s: not mine(s) and s["kind"] not in ("ladder", "nav_link") and not s["label"].endswith("_plinth")  # noqa: E731
        others_flat = sorted(json.dumps(s, sort_keys=True) for s in flat if other(s))
        others_rot = sorted(json.dumps(s, sort_keys=True) for s in rot if other(s))
        self.assertEqual(others_flat, others_rot)
        return rot

    def test_rotated_building_legacy_actors_turned_with_it(self):
        depot = self._load("gen_ref_01_02_depot_1")
        self.assertTrue(all(builder.building_rotation(b) == 0.0 for b in depot["buildings"]), "old files load as 0")
        for bid in ("B_warehouse", "B_ruin", "B_chimney_0"):
            self._assert_rotated_plan(depot, bid, 30.0)
        self._assert_rotated_plan(depot, "B_hall_n", -45.0)

    def test_rotated_building_extended_actors_turned_with_it(self):
        train = self._load("L_Gen_Train_1")
        rot = self._assert_rotated_plan(train, "B_house", 30.0)
        # its ladder (placed from the world frame) turns with the house: yaw = the unrotated yaw + 30
        flat_ld = next(s for s in builder.plan(train) if s["kind"] == "ladder" and s["label"] == "LAD_B_house")
        rot_ld = next(s for s in rot if s["kind"] == "ladder" and s["label"] == "LAD_B_house")
        self.assertAlmostEqual((rot_ld["yaw"] - flat_ld["yaw"] - 30.0 + 180.0) % 360.0 - 180.0, 0.0, places=2)

    def test_rotated_building_keeps_lintels_with_tg_rounding(self):
        """S2 R3: a building turned with the Training Ground's 0.01 cm rounding keeps every door lintel, window sill / lintel and
        opening-state actor of its unrotated twin (lost 8 -> 0 before: the exact-equality axis test in _collinear_gaps /
        _merged_building_edges dropped every rounded wall), at the turned positions."""
        train = self._load("L_Gen_Train_1")
        kinds = ("door_lintel", "opening_lintel", "window_sill", "mantle_sill")
        flat = builder.plan(train)
        mine = lambda s: "building=B_house" in s.get("tags", [])  # noqa: E731
        flat_counts = {k: sum(1 for s in flat if mine(s) and k in s["tags"]) for k in kinds}
        self.assertGreater(flat_counts["door_lintel"], 0)
        self.assertGreater(flat_counts["window_sill"], 0)
        for deg in (90.0, 30.0, 17.3):
            turned, _ = self._rotate_building(train, "B_house", deg, ndigits=2)
            rot = builder.plan(turned)
            self.assertEqual({k: sum(1 for s in rot if mine(s) and k in s["tags"]) for k in kinds}, flat_counts, "deg %.1f" % deg)
            self._assert_rotated_plan(train, "B_house", deg, ndigits=2)

    # ---- S2 R3: interior walls from partitions[] ----------------------------------------------------------------------------------

    @staticmethod
    def _interior(specs, bid):
        return [s for s in specs if "interior" in s.get("tags", []) and "building=%s" % bid in s.get("tags", [])]

    def test_partition_driven_interior_walls(self):
        """A layout that opts in ("interior_walls": "partitions") builds each partition cut round its openings: the walls[] interior
        cubes are gone, every partition door has a lintel as wide as the opening, the rest of the map is unchanged. Without the
        opt-in the legacy path stays (Train_1 lintel count and the depot sha above)."""
        train = self._load("L_Gen_Train_1")
        house = next(b for b in train["buildings"] if b["id"] == "B_house")
        self.assertFalse(builder.partition_driven(train, house))
        legacy = builder.plan(train)
        opted = builder.plan(dict(train, interior_walls="partitions"))
        part_doors = [op for b in train["buildings"] for pt in b.get("partitions", []) for op in pt.get("openings", []) if op.get("kind") == "door"]
        self.assertGreater(len(part_doors), 0)
        wall_ids = {w["id"] for w in train["walls"] if w.get("kind") == "interior"}
        self.assertFalse([s["label"] for s in opted if s["label"] in wall_ids or "_ilintel_" in s["label"] or "_idoor_" in s["label"]])
        interiors = [s for s in opted if "interior" in s.get("tags", [])]
        lintels = [s for s in interiors if "door_lintel" in s["tags"]]
        self.assertEqual(len(lintels), len(part_doors))
        self.assertEqual(sorted(round(s["scale"][0] * 100.0, 3) for s in lintels), sorted(float(op.get("width", 120.0)) for op in part_doors))
        self.assertEqual(sum("opening_lintel" in s.get("tags", []) for s in opted), sum("opening_lintel" in s.get("tags", []) for s in legacy))
        rest = lambda specs: sorted(json.dumps(s, sort_keys=True) for s in specs if "interior" not in s.get("tags", []))  # noqa: E731
        self.assertEqual(rest(opted), rest(legacy))

    def test_partition_driven_sealed_door_rotated_with_tg_rounding(self):
        """The failure R3 closes: a sealed interior door of a building turned with the 0.01 cm rounding keeps its wall fill and
        lintel (one record per wall, no gap matching), at the turned position; a "plan" marker alone opts the building in."""
        train = self._load("L_Gen_Train_1")
        house = next(b for b in train["buildings"] if b["id"] == "B_house")
        house["plan"] = {"v": 1, "kind": "bands", "seed": 1, "core_wall": "B_house_N", "street_wall": "B_house_S"}
        door = next(op for pt in house["partitions"] for op in pt.get("openings", []) if op.get("kind") == "door")
        door["state"] = "sealed"
        flat = builder.plan(train)
        self.assertEqual(sum("door_state=sealed" in s["tags"] for s in self._interior(flat, "B_house")), 1)
        counts = lambda specs: (len(self._interior(specs, "B_house")), sum("door_lintel" in s["tags"] for s in self._interior(specs, "B_house")),  # noqa: E731
                                sum("door_state=sealed" in s["tags"] for s in self._interior(specs, "B_house")))
        for deg in (17.3, 90.0):
            turned, _ = self._rotate_building(train, "B_house", deg, ndigits=2)
            self.assertEqual(counts(builder.plan(turned)), counts(flat), "deg %.1f" % deg)
            self._assert_rotated_plan(train, "B_house", deg, ndigits=2)

    # ---- S2 R4: one table of building coordinate fields (building_coord_fields.json) ----------------------------------------------

    FULL_BUILDING = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "TestFixtures", "building_all_coord_fields.json")

    @staticmethod
    def _values_at(node, path):
        """The values at a table path ("partitions[].openings[].pos") in document order (absent ones skipped)."""
        cur = [node]
        for head in path.split("."):
            many = head.endswith("[]")
            key = head[:-2] if many else head
            nxt = []
            for n in cur:
                v = n.get(key) if isinstance(n, dict) else None
                if v is not None:
                    nxt.extend(v if many else [v])
            cur = nxt
        return cur

    @staticmethod
    def _points_of(kind, v):
        return {"point": lambda: [v], "point_list": lambda: v, "rect": lambda: v, "bounds": lambda: [v["min"], v["max"]]}.get(kind, lambda: [])()

    def test_coord_table_covers_every_pair_of_the_full_building(self):
        """Every number pair (Vec2 / Vec2[]) on the full building (one entry per field; the same data as the TS fixture
        buildingCoordFields.fixture.ts) is a building_coord_fields.json row or a declared size, and every row resolves to one value."""
        with open(self.FULL_BUILDING, encoding="utf-8") as f:
            full = json.load(f)
        rows = builder.BUILDING_COORD_FIELDS
        covered = {r["path"] for r in rows} | {r["path"] for r in builder.BUILDING_COORD_TABLE["not_coordinates"]}
        pair = lambda v: isinstance(v, list) and len(v) == 2 and all(isinstance(x, (int, float)) and not isinstance(x, bool) for x in v)  # noqa: E731
        missing = []

        def walk(v, path):
            if path in covered:
                return
            if pair(v) or (isinstance(v, list) and v and all(pair(x) for x in v)):
                missing.append(path)
            elif isinstance(v, list):
                for el in v:
                    walk(el, path + "[]")
            elif isinstance(v, dict):
                for k, x in v.items():
                    walk(x, "%s.%s" % (path, k) if path else k)

        walk(full, "")
        self.assertEqual(missing, [], "coordinate-looking fields missing from building_coord_fields.json")
        for r in rows:
            self.assertEqual(len(self._values_at(full, r["path"])), 1, r["path"])

    def test_building_local_follows_coord_table(self):
        """_building_local turns every world-frame table field of the full building back about its footprint centre (points, yaws,
        directions) and keeps every building-frame field (footprint, room bounds, balconies, fire escapes, snow ramps, breach panels -
        before R4 the snow ramp foot and the fire-escape points were turned too, so a rotated building's ramp stood off its window)."""
        with open(self.FULL_BUILDING, encoding="utf-8") as f:
            full = json.load(f)
        deg = float(full["rotation"])
        self.assertNotEqual(deg, 0.0)
        local, c, d = builder._building_local({"walls": [], "ladders": [], "buildings": [full]}, full)
        lb = local["buildings"][0]
        self.assertEqual(d, deg)
        self.assertNotIn("rotation", lb)
        self.assertEqual(full["rotation"], deg, "the input building is not modified")
        for r in builder.BUILDING_COORD_FIELDS:
            v0, v1 = self._values_at(full, r["path"])[0], self._values_at(lb, r["path"])[0]
            if r["frame"] == "building":
                self.assertEqual(v1, v0, "%s: building frame - kept" % r["path"])
            elif r["kind"] == "yaw":
                self.assertAlmostEqual(v1, v0 - deg, msg=r["path"])
            elif r["kind"] == "direction":
                want = builder._rot_xy(v0, (0.0, 0.0), -deg)
                self.assertAlmostEqual(v1[0], want[0], msg=r["path"])
                self.assertAlmostEqual(v1[1], want[1], msg=r["path"])
            else:
                p0, p1 = self._points_of(r["kind"], v0), self._points_of(r["kind"], v1)
                self.assertEqual(len(p1), len(p0), r["path"])
                for a, b in zip(p0, p1):
                    want = builder._rot_xy(a, c, -deg)
                    self.assertAlmostEqual(b[0], want[0], places=3, msg=r["path"])
                    self.assertAlmostEqual(b[1], want[1], places=3, msg=r["path"])
                    self.assertGreater(abs(b[0] - a[0]) + abs(b[1] - a[1]), 1.0, "%s: turned back" % r["path"])

    def test_axis_tolerance_groups_rounded_segments(self):
        """_collinear_gaps / _merged_building_edges take a wall within 0.05 cm of an axis as axis-aligned and group pieces whose
        fixed coordinate differs by rounding noise; exact layouts group as before."""
        segs = [{"id": "W_n_0", "a": [0.0, 100.004], "b": [400.0, 99.998], "thickness": 30.0},
                {"id": "W_n_1", "a": [520.0, 100.003], "b": [900.0, 99.996], "thickness": 30.0}]
        gaps = builder._collinear_gaps(segs, "B", 0)
        self.assertEqual(len(gaps), 1)
        self.assertAlmostEqual(gaps[0]["g0"], 400.0)
        self.assertAlmostEqual(gaps[0]["g1"], 520.0)
        merged = builder._merged_building_edges([dict(w, height=300.0) for w in segs])
        self.assertEqual(len(merged), 1)
        self.assertAlmostEqual(merged[0]["a"][0], 0.0)
        self.assertAlmostEqual(merged[0]["b"][0], 900.0)
        # a real diagonal stays free
        self.assertEqual(builder._collinear_gaps([{"id": "D_0", "a": [0.0, 0.0], "b": [100.0, 1.0], "thickness": 30.0}], "B", 0), [])

    # ---- T-02 builder gaps ---------------------------------------------------------------------------------------------------------

    def test_mantle_edge_wall_gets_mantle_tag(self):
        """T-02 gap 2: a free wall carrying a layout mantle edge (the bridge obstacle W_obstacle, kind 'wall') is tagged CodexMantleEdge
        (MantleRules refuses a plain 'wall'); a full-height wall without an edge is not."""
        specs = builder.plan(self._load("gen_lab_bridge"))
        tagged = {s["label"] for s in specs if s.get("folder", "").startswith("Walls") and "CodexMantleEdge" in s.get("tags", [])}
        self.assertIn("W_obstacle", tagged)
        self.assertFalse([l for l in tagged if l.startswith("W_") and l != "W_obstacle"], tagged)
        edges = [{"start": [0.0, 30.0], "end": [100.0, 30.0], "kind": "obstacle"}]
        wall = {"a": [0.0, 0.0], "b": [100.0, 0.0], "thickness": 20.0}
        self.assertEqual(builder._wall_mantle_tags(wall, edges), [], "30 cm off a 20 cm wall (reach 25): out")
        self.assertEqual(builder._wall_mantle_tags(dict(wall, thickness=30.0), edges), ["CodexMantleEdge"])
        self.assertEqual(builder._wall_mantle_tags(wall, [{"start": [50.0, -50.0], "end": [50.0, 50.0], "kind": "obstacle"}]), [], "crossing, not along")

    def test_ambush_point_layer_lifts_it_to_the_roof(self):
        """T-02 gap 3: an ambush point with layer 'roof:<bid>' stands on that roof (z_of(floors) + 50) and carries a layer= tag; ground
        points stay at gz + 50 without one."""
        alley = self._load("gen_lab_alley")
        specs = {s["label"]: s for s in builder.plan(alley) if "CodexAmbushPoint" in s.get("tags", [])}
        gz = float(alley.get("ground_z", 0.0))
        self.assertAlmostEqual(specs["AMB_roof"]["location"][2], gz + builder.z_of(2) + 50.0)
        self.assertIn("layer=roof:B_tall", specs["AMB_roof"]["tags"])
        self.assertAlmostEqual(specs["AMB_corner"]["location"][2], gz + 50.0)
        self.assertEqual(specs["AMB_corner"]["tags"], [builder.AMBUSH_TAG])


if __name__ == "__main__":
    unittest.main()
