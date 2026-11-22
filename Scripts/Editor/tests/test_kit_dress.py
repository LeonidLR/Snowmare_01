"""Unit tests for the kit dressing (Scripts/Editor/kit_dress_lib.py, kit_dress.py plan; docs/design/levelgen/KIT_DRESS.md).
Pure: no editor, no kit assets (the series table only names them). `python -m unittest Scripts/Editor/tests/test_kit_dress.py`."""

import glob
import json
import math
import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
sys.path.insert(0, EDITOR_DIR)
import kit_dress_lib as K  # noqa: E402

SERIES_FILE = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "kit_series.json")
LAYOUT = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_bgen_plans_check.json")


def table():
    with open(SERIES_FILE, encoding="utf-8") as f:
        return json.load(f)


class SeriesTableTest(unittest.TestCase):
    def test_every_series_has_the_elements_the_dresser_reads(self):
        t = table()
        self.assertEqual(t["schema"], "codex.kit_series/1")
        for name, s in t["series"].items():
            for key in ("storey_cm", "module_cm", "solid", "windows", "entrance", "stair_column", "plinth", "parapet", "interior_walls", "doors", "stairs", "floors"):
                self.assertIn(key, s, "%s lacks %s" % (name, key))
            for w in s["windows"]:
                self.assertLess(w["hole"][0], w["hole"][1])
            self.assertIn("switchback", s["stairs"])
            self.assertTrue(s["stairs"]["flights"])
        for arch, name in t["archetype_default"].items():
            self.assertIn(name, t["series"], arch)

    def test_series_lookup_record_then_archetype_default(self):
        t = table()
        self.assertEqual(K.series_for({"archetype": "block"}, t)[0], "panel_1-464")
        self.assertEqual(K.series_for({"archetype": "block", "series": "brick_1-447"}, t)[0], "brick_1-447")
        # an unknown series name falls back to the archetype default; an archetype without one stays grey
        self.assertEqual(K.series_for({"archetype": "office", "series": "stalin_1-415"}, t)[0], "brick_1-447")
        self.assertEqual(K.series_for({"archetype": "warehouse"}, t), (None, None))
        self.assertEqual(K.mesh_path(t, "Facade/SM_X"), "/Game/SovietHouses/Meshes/Facade/SM_X")
        self.assertEqual(K.mesh_path(t, "/Engine/BasicShapes/Plane.Plane"), "/Engine/BasicShapes/Plane.Plane")


class FitRulesTest(unittest.TestCase):
    HOLES = [150.0, 218.0]   # panel_1-464: win, wide

    def test_window_by_width(self):
        self.assertEqual(K.choose_module(150.0, self.HOLES), (0, 1.0, 0.0, 0.0))
        i, s, jamb, narrower = K.choose_module(160.0, self.HOLES)
        self.assertEqual((i, jamb, narrower), (0, 0.0, 0.0))
        self.assertAlmostEqual(s, 160.0 / 150.0)
        # 200 cm: the 150 hole would need x 1.33 - the wide module squeezed to 0.917
        i, s, jamb, narrower = K.choose_module(200.0, self.HOLES)
        self.assertEqual(i, 1)
        self.assertAlmostEqual(s * 218.0, 200.0)
        self.assertEqual((jamb, narrower), (0.0, 0.0))

    def test_window_narrower_than_any_module_gets_jamb_fill(self):
        i, s, jamb, narrower = K.choose_module(100.0, self.HOLES)
        self.assertEqual(i, 0)
        self.assertAlmostEqual(s, 0.9)
        self.assertAlmostEqual(jamb, (135.0 - 100.0) / 2.0)
        self.assertEqual(narrower, 0.0)

    def test_window_wider_than_any_module_is_reported_narrower(self):
        i, s, jamb, narrower = K.choose_module(300.0, self.HOLES)
        self.assertEqual((i, jamb), (1, 0.0))
        self.assertAlmostEqual(s, 1.1)
        self.assertAlmostEqual(narrower, 300.0 - 218.0 * 1.1)

    def test_clamp_and_tiles(self):
        self.assertAlmostEqual(K.clamp_stretch(240.0, 205.0), 1.1)
        self.assertAlmostEqual(K.clamp_stretch(190.0, 205.0), 190.0 / 205.0)
        self.assertEqual(K.tile_exact(0.0, 600.0), [(0.0, 300.0), (300.0, 300.0)])
        self.assertEqual(K.tile_exact(0.0, 1.0), [])
        n, kz = K.tile_vertical(95.0, 54.0)
        self.assertEqual(n, 2)
        self.assertAlmostEqual(kz, 95.0 / 108.0)
        self.assertEqual(K.tile_vertical(300.0, 300.0), (1, 1.0))

    def test_filler_split(self):
        # exact within 10 %: 620 cm -> 2 x 310
        self.assertEqual(K.fill(0.0, 620.0, 0.0, 0.0), [(0.0, 310.0, 0.0), (310.0, 310.0, 0.0)])
        # 450 cm: n = 2 -> 225 each (x 0.75) is out; overlap mode -> two full 300 modules, alternately recessed, covering [a, b]
        f = K.fill(0.0, 450.0, 72.0, 72.0)
        self.assertEqual([w for _, w, _ in f], [300.0, 300.0])
        self.assertLessEqual(f[0][0], 0.0)
        self.assertGreaterEqual(f[-1][0] + f[-1][1], 450.0)
        self.assertNotEqual(f[0][2], f[1][2])
        # 50 cm between two bays whose jambs leave 72 cm each: one module slid behind both jambs (194 cm, squished, recessed)
        f = K.fill(100.0, 150.0, 72.0, 72.0)
        self.assertEqual(len(f), 1)
        self.assertAlmostEqual(f[0][0], 28.0)
        self.assertAlmostEqual(f[0][1], 194.0)
        self.assertGreater(f[0][2], 0.0)
        # squish mode: contiguous, any width
        self.assertEqual(K.fill(0.0, 450.0, 72.0, 72.0, mode="squish"), [(0.0, 225.0, 0.0), (225.0, 225.0, 0.0)])
        self.assertEqual(K.fill(5.0, 5.5, 0.0, 0.0), [])

    def test_door_leaves(self):
        room = table()["series"]["panel_1-464"]["doors"]["room"]
        flat = table()["series"]["panel_1-464"]["doors"]["unit_entry"]
        p = K.door_plan(150.0, 240.0, room)
        self.assertEqual(p["leaves"], 2)
        self.assertAlmostEqual(p["sx"], 75.0 / 80.0)
        self.assertEqual(p["jamb"], 0.0)
        self.assertAlmostEqual(p["head"], 240.0 - 205.0 * 1.1)
        # a single flat door: the framed kit door, x 0.98
        p = K.door_plan(100.0, 214.0, flat)
        self.assertEqual((p["leaves"], p["cand"]), (1, 0))
        self.assertAlmostEqual(p["sx"], 100.0 / 102.0)
        # a 150 cm flat door is a pair (> 130): the framed door (102) does not fit a 75 cm leaf, the plain leaf does
        p = K.door_plan(150.0, 240.0, flat)
        self.assertEqual((p["leaves"], p["cand"]), (2, 1))
        # 120 cm single: no candidate within 10 % -> the 80 cm leaf at x 1.1 and 16 cm jambs
        p = K.door_plan(120.0, 240.0, room)
        self.assertEqual(p["leaves"], 1)
        self.assertAlmostEqual(p["leaf_w"], 88.0)
        self.assertAlmostEqual(p["jamb"], 16.0)


class StairsTest(unittest.TestCase):
    SB = table()["series"]["panel_1-464"]["stairs"]["switchback"]
    FL = table()["series"]["panel_1-464"]["stairs"]["flights"]

    def test_switchback_fit_keeps_the_floor_landing_inside(self):
        # the kit-plan core (BGEN kit plans 2026-11-21): 215 cm flights, 229 cm to the outer face behind, mid landing far edge at 375
        sy, shift, mis, short = K.switchback_fit(215.0, 150.0, self.SB, back_room=229.0, mid_far=375.0)
        self.assertLessEqual((self.SB["floor_edge_y"] - self.SB["floor_landing_y"]) * sy - shift, 229.0 + 1e-6)
        self.assertGreaterEqual(sy, 0.9 - 1e-9)
        self.assertLessEqual(sy, 1.1 + 1e-9)
        self.assertLess(mis, 15.0)
        self.assertLess(short, 30.0)
        # no limit behind, a matching landing: steps on the ramp within a few cm
        sy, shift, mis, short = K.switchback_fit(196.0, 140.0, self.SB, None, 326.0)
        self.assertAlmostEqual(sy, 1.0)
        self.assertEqual(shift, 0.0)
        self.assertLess(mis, 0.5)
        self.assertLess(short, 0.5)

    def test_flight_mismatch(self):
        self.assertAlmostEqual(K.flight_mismatch(200.0, 200.0, 0.0, 150.0), 0.0)
        self.assertAlmostEqual(K.flight_mismatch(200.0, 180.0, 20.0, 150.0), 15.0)

    def test_switchback_handedness_and_anchor(self):
        # flight a along -x from (0, 0) (kit +y), lane b at +y: kit +x = +y, rot90(+y) = -x = kit +y -> the kit's own hand; lane b at -y: mirrored
        for lane_y, mirrored in ((160.0, False), (-160.0, True)):
            pl = K.switchback_placement((0.0, 0.0), (-215.0, 0.0), (0.0, lane_y), 300.0, self.SB, None, None)
            self.assertEqual(pl["wy"], (-1.0, 0.0))
            self.assertAlmostEqual(pl["scale"][0], 160.0 / 150.0)
            self.assertAlmostEqual(pl["scale"][2], 300.0 / 280.0)
            i = K.inst("/m", pl["wx"], pl["wy"], (pl["anchor"][0], pl["anchor"][1], 0.0), pl["anchor_local"], pl["scale"])
            self.assertEqual(i["scale"][0] < 0, mirrored)
            # the kit's lane-2 centre (local x 225 at the floor edge) lands on our flight b's top
            kx, ky = 225.0, self.SB["floor_edge_y"]
            r = math.radians(i["rot"][1])
            sx, sy = i["scale"][0], i["scale"][1]
            wx = (kx * sx * math.cos(r) - ky * sy * math.sin(r) + i["loc"][0], kx * sx * math.sin(r) + ky * sy * math.cos(r) + i["loc"][1])
            shift = pl["anchor"][0]
            self.assertAlmostEqual(wx[0], shift, places=3)
            self.assertAlmostEqual(wx[1], lane_y, places=3)

    def test_single_flight_steps(self):
        # the old full-storey flight: 300 cm over 429 cm -> two Steps_10 blocks (rise 150 each = x 1.154, run x 1.02)
        fp = K.flight_pieces((0.0, 0.0), (429.0, 0.0), 0.0, 300.0, 150.0, self.FL)
        self.assertEqual((fp["cand"], fp["n"]), (0, 2))
        self.assertAlmostEqual(fp["scale"][2], 150.0 / 130.0)
        self.assertAlmostEqual(fp["scale"][1], 214.5 / 210.0)
        self.assertEqual(fp["pieces"][1], ((214.5, 0.0), 150.0))
        self.assertAlmostEqual(K.rot90(fp["wx"])[0], fp["wy"][0])   # no mirror


class GeometryTest(unittest.TestCase):
    def test_inst_mirror_and_anchor(self):
        i = K.inst("/m", (1.0, 0.0), (0.0, 1.0), (100.0, 50.0, 10.0), local=(10.0, 0.0, 5.0), scale=(2.0, 1.0, 1.0))
        self.assertEqual(i["loc"], [80.0, 50.0, 5.0])
        self.assertEqual(i["rot"][1], 0.0)
        m = K.inst("/m", (1.0, 0.0), (0.0, -1.0), (0.0, 0.0, 0.0))
        self.assertEqual(m["scale"][0], -1.0)
        self.assertAlmostEqual(abs(m["rot"][1]), 180.0)

    def test_rotated_building_round_trip(self):
        sp = {"location": [1100.0, 0.0, 50.0], "yaw": 30.0}
        loc = K.spec_to_local(sp, (1000.0, 0.0), 90.0)
        self.assertAlmostEqual(loc["location"][0], 1000.0)
        self.assertAlmostEqual(loc["location"][1], -100.0)
        self.assertAlmostEqual(loc["yaw"], -60.0)
        back = K.globalize([{"loc": loc["location"], "rot": [0.0, loc["yaw"], 0.0]}], (1000.0, 0.0), 90.0)[0]
        self.assertAlmostEqual(back["loc"][0], 1100.0)
        self.assertAlmostEqual(back["loc"][1], 0.0)
        self.assertAlmostEqual(back["rot"][1], 30.0)

    def test_intervals(self):
        self.assertEqual(K.interval_union([(5, 6), (0, 2), (1, 3)]), [(0, 3), (5, 6)])
        self.assertEqual(K.interval_minus((0.0, 10.0), [(2.0, 3.0), (2.5, 4.0)]), [(0.0, 2.0), (4.0, 10.0)])

    def test_box_and_ramp_pieces(self):
        b = K.box_piece({"location": [50.0, 0.0, 150.0], "yaw": 0.0, "scale": [1.0, 0.3, 3.0]})
        self.assertEqual((b["a"], b["b"], b["z0"], b["z1"], b["thick"]), ((0.0, 0.0), (100.0, 0.0), 0.0, 300.0, 30.0))
        L = math.hypot(429.0, 300.0)
        r = K.ramp_piece({"location": [214.5, 0.0, 150.0 - 10.0], "yaw": 0.0, "pitch": math.degrees(math.atan2(300.0, 429.0)), "scale": [L / 100.0, 1.5, 0.2]})
        self.assertAlmostEqual(r["foot"][0], 0.0)
        self.assertAlmostEqual(r["top"][0], 429.0)
        self.assertAlmostEqual(r["z0"], 0.0)
        self.assertAlmostEqual(r["z1"], 300.0)


@unittest.skipUnless(os.path.exists(LAYOUT), "no gen_bgen_plans_check layout")
class DressLayoutTest(unittest.TestCase):
    """The whole dressing of the L_Gen_BgenPlans layout with this checkout's builder (and with an exported newer builder when present)."""

    @staticmethod
    def plan(layout=LAYOUT, builder=None):
        os.environ["KIT_DRESS_LAYOUT"] = layout
        if builder:
            os.environ["KIT_DRESS_BUILDER"] = builder
        else:
            os.environ.pop("KIT_DRESS_BUILDER", None)
        sys.modules.pop("kit_dress", None)
        import kit_dress  # noqa: E402
        return kit_dress.plan_all()

    def check(self, layout_path, builder=None):
        B, layout, specs, dressed, t = self.plan(layout_path, builder)
        blocks = [b for b in layout["buildings"] if b.get("archetype") == "block"]
        self.assertTrue(blocks)
        for b in blocks:
            d = dressed[b["id"]]
            self.assertEqual(d["series"], "panel_1-464")
            mine = [s for s in specs if str(s.get("folder", "")).startswith("Buildings/%s" % b["id"]) and s["kind"] == "cube"]
            visible = [s for s in mine if not s.get("kit_hidden")]
            # only slabs (and a mid landing the kit half landing does not cover) stay visible grey box
            self.assertTrue(all(any(t_ in s.get("tags", []) for t_ in ("floor_slab", "roof_slab", "mid_landing")) for s in visible),
                            [s["label"] for s in visible if not any(t_ in s.get("tags", []) for t_ in ("floor_slab", "roof_slab", "mid_landing"))][:5])
            # every exterior window opening has a kit window (or a balcony door window)
            edges = K.edges_of(b)
            wins = [o for o in b.get("openings", []) if o.get("kind") == "window" and K.edge_of_point(edges, o["pos"])]
            self.assertEqual(d["parts"].get("window", 0) + d["parts"].get("balcony_door_window", 0), len(wins))
            doors = [o for o in b.get("openings", []) if o.get("kind") == "door" and int(o.get("floor", 0)) == 0]
            self.assertEqual(d["parts"].get("facade_entrance_bay", 0), len(doors))
            # nothing far outside the footprint (porches / canopies / balconies stick out ~1.7 m)
            x0, y0, x1, y1 = K.footprint_rect(b)
            for i in d["instances"]:
                self.assertTrue(x0 - 400 <= i["loc"][0] <= x1 + 400 and y0 - 400 <= i["loc"][1] <= y1 + 400, (b["id"], i))
            stairs = [s for s in b.get("stairs", []) if s.get("kind") == "stairwell"]
            sets = d["parts"].get("stair_switchback_set", 0) + d["parts"].get("stair_flight_block", 0)
            self.assertGreaterEqual(sets, len(stairs))
        # one kit actor per building, a component per mesh / material: the ADR budget of <= 40 non-instanced actors per building holds
        for bid, d in dressed.items():
            groups = {(i["mesh"], i.get("mat"), i["scale"][0] < 0, i["group"]) for i in d["instances"]}
            self.assertLessEqual(len(groups), 60, bid)
        return dressed

    def test_this_checkout(self):
        # since the merge of claude/building-generator (2026-11-21 night) this checkout's layout has the kit block plans: switchback cores
        dressed = self.check(LAYOUT)
        self.assertTrue(all(d["parts"].get("stair_switchback_set") or d["parts"].get("stair_flight_block")
                            for b, d in dressed.items() if d["archetype"] == "block"))

    def test_exported_kit_plans(self):
        roots = sorted(glob.glob(os.path.join(PROJECT_DIR, "Saved", "KitBlocks", "builder", "*")))
        roots = [r for r in roots if os.path.exists(os.path.join(r, "Content", "Data", "LevelGen", "accepted", "gen_bgen_plans_check.json"))]
        if not roots:
            self.skipTest("no exported builder (python Scripts/Editor/kit_dress.py export claude/building-generator gen_bgen_plans_check)")
        for root in roots:
            lay = os.path.join(root, "Content", "Data", "LevelGen", "accepted", "gen_bgen_plans_check.json")
            with open(lay, encoding="utf-8") as f:
                if not any(s.get("turn") for b in json.load(f)["buildings"] for s in b.get("stairs", [])):
                    continue
            dressed = self.check(lay, root)
            self.assertTrue(any(d["parts"].get("stair_switchback_set") for d in dressed.values()))

    @classmethod
    def tearDownClass(cls):
        os.environ.pop("KIT_DRESS_LAYOUT", None)
        os.environ.pop("KIT_DRESS_BUILDER", None)


class StoryShowcaseDressTest(unittest.TestCase):
    """The dressed story showcase (gen_story_showcase, 2026-11-22: six khrushchevka blocks with a gable door each): every ground door - the
    court entrances and the gable door - gets a kit entrance (door bay + porch + canopy); the story-4 balconies get the kit balcony door bay +
    balcony and no decorative balcony stands beside them."""

    def test_gable_doors_and_story_balconies(self):
        path = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_story_showcase.json")
        if not os.path.exists(path):
            self.skipTest("no gen_story_showcase layout")
        B, layout, specs, dressed, t = DressLayoutTest.plan(path)
        gables = 0
        for b in layout["buildings"]:
            d = dressed[b["id"]]
            edges = K.edges_of(b)
            ground = [o for o in b.get("openings", []) if o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and K.edge_of_point(edges, o["pos"])]
            gables += sum(1 for o in ground if not K.edge_of_point(edges, o["pos"]).long)
            for part in ("facade_entrance_bay", "porch", "canopy"):
                self.assertEqual(d["parts"].get(part, 0), len(ground), (b["id"], part))
            real = b.get("balconies") or []
            if real:
                self.assertEqual(d["parts"].get("balcony", 0), len(real), b["id"])
                self.assertEqual(d["parts"].get("facade_balcony_bay", 0), len(real), b["id"])
                # (an upper balcony's escape ladder leaves an open gap in the wall beside its door bay: a REVIEW note, not checked here)
                self.assertFalse([n for n in d["notes"] if "visual only" in n], b["id"])
        self.assertEqual(gables, len(layout["buildings"]), "one gable door per house")

    @classmethod
    def tearDownClass(cls):
        os.environ.pop("KIT_DRESS_LAYOUT", None)


class StoryCamerasTest(unittest.TestCase):
    """The story showcase views (kit_dress.py story_cameras, KIT_DRESS_SHOT_SET=story) on gen_story_showcase: every building's front and
    the close-up of each story element the layout carries."""

    def test_every_story_element_gets_a_view(self):
        path = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_story_showcase.json")
        if not os.path.exists(path):
            self.skipTest("no gen_story_showcase layout")
        with open(path, encoding="utf-8") as f:
            layout = json.load(f)
        sys.modules.pop("kit_dress", None)
        import kit_dress  # noqa: E402
        cams = kit_dress.story_cameras(layout)
        names = [c[0] for c in cams]
        self.assertEqual(len(names), len(set(names)), names)
        for want in ("overview_34", "map_top_down", "S03_locked_door_door_locked", "S04_rubble_escape_stair_rubble_interior", "S04_rubble_escape_stair_rubble_cutaway", "S04_rubble_escape_fire_escape",
                     "S07_roof_outside_roof_ladder", "S09_broken_back_windows_broken_windows", "S09_broken_back_windows_door_locked",
                     "S10_snow_drift_snow_drift", "S10_snow_drift_door_sealed"):
            self.assertIn(want, names)
        for b in layout["buildings"]:
            self.assertIn("%s_front_34" % b["id"], names)
            # a khrushchevka's gable door (a ground door on a short side) gets its close-up (2026-11-22)
            edges = K.edges_of(b)
            if any(o.get("kind") == "door" and int(o.get("floor", 0)) == 0 and not (K.edge_of_point(edges, o["pos"]) or edges[0]).long
                   for o in b.get("openings", []) if K.edge_of_point(edges, o["pos"])):
                self.assertIn("%s_gable_door" % b["id"], names)
                # the gable door opens on a ground-floor service room (designer 2026-11-22): a view from the door into it
                if any(r.get("zone") == "service" and int(r.get("floor", 0)) == 0 for r in b.get("rooms", [])):
                    self.assertIn("%s_service_room_interior" % b["id"], names)
        for name, loc, pitch, yaw, fov, hide in cams:
            self.assertTrue(-90.0 <= pitch <= 30.0, (name, pitch))   # a roof ladder view looks up a little
            self.assertTrue(all(math.isfinite(v) for v in loc), name)


class StoreyCutawayDressTest(unittest.TestCase):
    """The roof cutaway's storey rule (designer bug 2026-11-22: "squad on the ground floor -> storeys above and the roof invisible"): every
    kit instance has a storey, an ISM component never spans two storeys, and its tags name the building and the storey (building=<id>,
    floor=<n> - the same tags as the builder's grey pieces, read by URoofCutawaySubsystem)."""

    def test_storey_of_z(self):
        self.assertEqual(K.storey_of_z(0.0, 0.0, 3), 0)
        self.assertEqual(K.storey_of_z(-45.0, 0.0, 3), 0)       # the plinth under the ground
        self.assertEqual(K.storey_of_z(249.0, 0.0, 3), 0)       # a canopy / lintel strip of the ground floor
        self.assertEqual(K.storey_of_z(280.0, 0.0, 3), 1)       # a balcony slab 20 cm under storey 1
        self.assertEqual(K.storey_of_z(300.0, 0.0, 3), 1)
        self.assertEqual(K.storey_of_z(600.0, 0.0, 3), 2)
        self.assertEqual(K.storey_of_z(900.0, 0.0, 3), 3)       # the roof
        self.assertEqual(K.storey_of_z(1500.0, 0.0, 3), 3)      # never above the roof
        self.assertEqual(K.storey_of_z(1300.0, 1000.0, 3), 1)   # a raised ground

    def test_showcase_instances_per_storey(self):
        path = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted", "gen_story_showcase.json")
        if not os.path.exists(path):
            self.skipTest("no gen_story_showcase layout")
        B, layout, specs, dressed, t = DressLayoutTest.plan(path)
        sys.modules.pop("kit_dress", None)
        import kit_dress  # noqa: E402
        gz = float(layout.get("ground_z", 0.0))
        for b in layout["buildings"]:
            d = dressed[b["id"]]
            floors = int(b["floors"])
            seen = set()
            for i in d["instances"]:
                st = i["storey"]
                self.assertTrue(0 <= st <= floors, (b["id"], i))
                seen.add(st)
                if i["group"] == "roof":
                    self.assertEqual(st, floors, (b["id"], i["part"]))
                elif i["part"] == "ceiling_plane":
                    # the ceiling over storey f hides with the slab of f + 1 (the ceiling over the operative's head)
                    self.assertEqual(st, int(math.floor((i["loc"][2] - gz) / K.STOREY)) + 1, (b["id"], i["loc"]))
                elif i["part"] in ("floor_plane", "window", "facade_solid", "facade_window_bay"):
                    self.assertEqual(st, int(math.floor((i["loc"][2] - gz + 1.0) / K.STOREY)), (b["id"], i["part"], i["loc"]))
            self.assertEqual(seen, set(range(floors + 1)), b["id"])   # every storey and the roof
            keys = {kit_dress.ism_key(i) for i in d["instances"]}
            self.assertLessEqual(len(keys), 60 * (floors + 1), b["id"])
            for key in keys:
                tags = kit_dress.ism_tags(b["id"], key, "x")
                self.assertIn("building=%s" % b["id"], tags)
                self.assertIn("floor=%d" % key[4], tags)
                self.assertEqual("kit_roof" in tags, key[3] == "roof")

    @classmethod
    def tearDownClass(cls):
        os.environ.pop("KIT_DRESS_LAYOUT", None)


if __name__ == "__main__":
    unittest.main()
