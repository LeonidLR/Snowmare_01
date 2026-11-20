"""Unit tests for the schema /2 building features in build_levelgen_map.py (BUILDINGS_TECH_SPEC.md 2-3, ADR-0003): opening
states, blocked flights, balconies, fire escapes, snow ramps, the /2 default table (openingStates.ts) and the legacy guarantee.
Pure: no Unreal (python -m unittest discover -s Scripts/Editor/tests)."""

import contextlib
import copy
import io
import math
import os
import sys
import types
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402

V2 = builder.SCHEMA_V2


def _load(name):
    return builder.load_layout(os.path.join(ACCEPTED_DIR, "%s.json" % name))


def _plan(layout):
    with contextlib.redirect_stdout(io.StringIO()):
        return builder.plan(layout)


def _all_openings(layout):
    for b in layout["buildings"]:
        for op in b.get("openings", []):
            yield b, op
        for pt in b.get("partitions", []):
            for op in pt.get("openings", []):
                yield b, op


def _with_tag(specs, tag):
    return [s for s in specs if tag in s.get("tags", [])]


def _house(layout):
    return next(b for b in layout["buildings"] if b["id"] == "B_house")


class OpeningStateDefaultsTest(unittest.TestCase):
    """effective_opening_state: the Training Ground openingStates.ts table, ported."""

    def setUp(self):
        self.layout = _load("L_Gen_Train_1")
        self.house = _house(self.layout)
        self.ext_door = next(o for o in self.house["openings"] if o["kind"] == "door")
        self.window = next(o for o in self.house["openings"] if o["kind"] == "window")
        self.part_door = self.house["partitions"][0]["openings"][0]
        self.loophole = {"id": "lp", "kind": "loophole", "floor": 0, "pos": [0, 0]}

    def test_v1_legacy_defaults(self):
        st = lambda op: builder.effective_opening_state(self.layout, self.house, op)  # noqa: E731
        self.assertEqual(st(self.ext_door), "open")
        self.assertEqual(st(self.part_door), "open")
        self.assertEqual(st(self.window), "broken")
        self.assertEqual(st(self.loophole), "fixed")

    def test_v2_table(self):
        lay = dict(self.layout, schema=V2)
        st = lambda op, b=self.house: builder.effective_opening_state(lay, b, op)  # noqa: E731
        self.assertEqual(st(self.ext_door), "closed")
        self.assertEqual(st(self.part_door), "open")      # interior: one of a partition's openings
        self.assertEqual(st(self.window), "glazed")
        self.assertEqual(st(self.loophole), "fixed")
        # interior by its wall: a partition id, or a layout wall of kind interior
        self.assertEqual(st(dict(self.ext_door, id="x", wall=self.house["partitions"][1]["id"])), "open")
        iw = next(w for w in self.layout["walls"] if w.get("kind") == "interior")
        self.assertEqual(st(dict(self.ext_door, id="y", wall=iw["id"])), "open")
        # ruins: doors open, windows broken
        ruin = dict(self.house, set_piece="ruined_two_level")
        self.assertEqual(st(self.ext_door, ruin), "open")
        self.assertEqual(st(self.window, ruin), "broken")

    def test_explicit_state_wins_in_both_schemas(self):
        for schema in (builder.SCHEMA, V2):
            lay = dict(self.layout, schema=schema)
            self.assertEqual(builder.effective_opening_state(lay, self.house, dict(self.ext_door, state="locked")), "locked")
            self.assertEqual(builder.effective_opening_state(lay, self.house, dict(self.window, state="barred")), "barred")

    def test_passable_states(self):
        for s in ("open", "closed", "locked", "broken", "glazed"):
            self.assertTrue(builder.is_passable_state(s), s)
        for s in ("sealed", "barricaded", "barred", "boarded", "fixed"):
            self.assertFalse(builder.is_passable_state(s), s)

    def test_load_layout_accepts_v2(self):
        import json
        import tempfile
        lay = dict(self.layout, schema=V2)
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "l.json")
            with open(p, "w", encoding="utf-8") as f:
                json.dump(lay, f)
            self.assertEqual(builder.load_layout(p)["schema"], V2)
            lay["schema"] = "codex.levelgen_layout/3"
            with open(p, "w", encoding="utf-8") as f:
                json.dump(lay, f)
            with self.assertRaises(ValueError):
                builder.load_layout(p)


class LegacyGuaranteeTest(unittest.TestCase):
    """A /1 layout, or a /2 one without the new fields whose openings are all open / broken (loopholes fixed), builds the same
    spec list as before (the /1 Depot hash itself is pinned in test_build_levelgen_buildings.test_legacy_depot_bit_identical)."""

    def test_depot_v1_equals_v2(self):
        depot = _load("gen_ref_01_02_depot_1")
        self.assertEqual(_plan(depot), _plan(dict(copy.deepcopy(depot), schema=V2)))

    def test_buildings_with_openings_v1_equals_v2_passable(self):
        for name in ("L_Gen_Train_1", "L_Gen_Train_2", "L_Gen_Train_3"):
            lay = _load(name)
            base = _plan(lay)
            v2 = copy.deepcopy(lay)
            v2["schema"] = V2
            explicit_v1 = copy.deepcopy(lay)
            for target in (v2, explicit_v1):
                for _, op in _all_openings(target):
                    op["state"] = {"door": "open", "window": "broken"}.get(op["kind"], "fixed")
            self.assertEqual(base, _plan(v2), name)
            self.assertEqual(base, _plan(explicit_v1), name)
            self.assertFalse([s for s in base if "collision" in s or "affects_navigation" in s], name)

    def test_fire_escape_ladder_adds_nothing(self):
        lay = _load("L_Gen_Train_1")
        base = _plan(lay)
        fe = copy.deepcopy(lay)
        _house(fe)["fire_escapes"] = [{"id": "FE_1", "kind": "ladder", "side": "S", "at": [440, 600], "ground": [400, 600],
                                       "stops": [], "to_roof": True, "ladder_ids": ["LAD_B_house"]}]
        specs = _plan(fe)
        self.assertEqual(base, specs)
        self.assertEqual(len([s for s in specs if s["kind"] == "ladder" and s["label"] == "LAD_B_house"]), 1)


class OpeningStateSpecsTest(unittest.TestCase):
    def _house_with(self, door_state=None, window_state=None, schema=builder.SCHEMA):
        lay = copy.deepcopy(_load("L_Gen_Train_1"))
        lay["schema"] = schema
        h = _house(lay)
        for op in h["openings"]:
            if op["kind"] == "door" and door_state:
                op["state"] = door_state
            if op["kind"] == "window" and window_state:
                op["state"] = window_state
        n_doors = sum(1 for op in h["openings"] if op["kind"] == "door")
        n_windows = sum(1 for op in h["openings"] if op["kind"] == "window")
        return lay, _plan(lay), n_doors, n_windows

    def test_closed_and_locked_leaf_free_for_navigation(self):
        for state in ("closed", "locked"):
            _, specs, n_doors, _ = self._house_with(door_state=state)
            leaves = _with_tag(specs, "door_state=%s" % state)
            self.assertEqual(len(leaves), n_doors, state)
            for s in leaves:
                self.assertIn("door_leaf", s["tags"])
                self.assertIs(s["affects_navigation"], False)
                self.assertEqual(s["collision"]["pawn"], "ignore")
                self.assertAlmostEqual(s["scale"][1] * 100.0, builder.DOOR_LEAF_T)
                self.assertAlmostEqual(s["scale"][2] * 100.0, 210.0)      # sill 0 .. top 210
                self.assertAlmostEqual(s["location"][2], 105.0)
            self.assertEqual(bool(_with_tag(leaves, "door_lock_plate")), state == "locked")

    def test_barricaded_leaf_and_box_block(self):
        _, specs, n_doors, _ = self._house_with(door_state="barricaded")
        tagged = _with_tag(specs, "door_state=barricaded")
        self.assertEqual(len(tagged), 2 * n_doors)
        self.assertEqual(len(_with_tag(tagged, "door_barricade")), n_doors)
        for s in tagged:
            self.assertNotIn("affects_navigation", s)                  # blocking: affects navigation
            self.assertEqual(s["collision"], builder.COLLISION_BLOCK_ALL)
        for s in _with_tag(tagged, "door_barricade"):
            self.assertAlmostEqual(s["scale"][1] * 100.0, 30.0 + builder.BARRICADE_EXTRA_T)

    def test_sealed_wall_fill(self):
        _, specs, n_doors, _ = self._house_with(door_state="sealed")
        fills = _with_tag(specs, "door_state=sealed")
        self.assertEqual(len(fills), n_doors)
        for s in fills:
            self.assertIn("opening_fill", s["tags"])
            self.assertAlmostEqual(s["scale"][1] * 100.0, 30.0)         # the wall's thickness
            self.assertEqual(s["collision"], builder.COLLISION_BLOCK_ALL)

    def test_window_states(self):
        expect = {"glazed": ("window_glass", builder.COLLISION_VISUAL_GLASS, False),
                  "barred": ("window_bars", builder.COLLISION_PAWN_ONLY, None),
                  "boarded": ("window_boards", builder.COLLISION_BLOCK_ALL, None),
                  "fixed": ("window_glass", builder.COLLISION_PAWN_ONLY, None)}
        for state, (piece, coll, nav) in expect.items():
            _, specs, _, n_windows = self._house_with(window_state=state)
            tagged = _with_tag(specs, "window_state=%s" % state)
            self.assertEqual(len(tagged), n_windows, state)
            for s in tagged:
                self.assertIn(piece, s["tags"])
                self.assertEqual(s["collision"], coll, state)
                self.assertEqual(s.get("affects_navigation"), nav, state)
                self.assertAlmostEqual(s["scale"][2] * 100.0, 210.0 - 95.0)  # sill 95 .. top 210
        _, specs, _, _ = self._house_with(window_state="broken")
        self.assertFalse([s for s in specs if any(t.startswith("window_state=") for t in s.get("tags", []))])
        self.assertTrue(_with_tag(self._house_with(window_state="fixed")[1], "window_fixed"))

    def test_v2_defaults(self):
        lay, specs, n_ext_doors, n_windows = self._house_with(schema=V2)
        self.assertEqual(len(_with_tag(specs, "door_state=closed")), n_ext_doors + sum(
            1 for op in next(b for b in lay["buildings"] if b["id"] == "B_garage")["openings"] if op["kind"] == "door"))
        self.assertFalse(_with_tag(specs, "door_state=open"))
        # partition doors stay open: no leaf on an interior floor-0 / floor-1 door lintel position
        self.assertFalse([s for s in _with_tag(specs, "door_leaf") if "_idoor_" in s["label"]])
        self.assertGreaterEqual(len(_with_tag(specs, "window_state=glazed")), n_windows)

    def test_partition_door_state(self):
        lay = copy.deepcopy(_load("L_Gen_Train_1"))
        for pt in _house(lay)["partitions"]:
            for op in pt["openings"]:
                op["state"] = "locked"
        leaves = [s for s in _with_tag(_plan(lay), "door_state=locked") if "_idoor_" in s["label"]]
        self.assertEqual(len(leaves), sum(len(pt["openings"]) for pt in _house(lay)["partitions"]))

    def test_loophole_gets_nothing(self):
        op = {"id": "lp", "kind": "loophole", "sill_height": 120, "top_height": 150}
        self.assertEqual(builder.opening_state_specs("x", "f", [0, 0], [30, 0], 0.0, 30.0, 0, "B", op, "fixed"), [])


class BlockedFlightTest(unittest.TestCase):
    def _check(self, lay, bid, stair):
        base = _plan(lay)
        blocked = copy.deepcopy(lay)
        s = next(st for b in blocked["buildings"] if b["id"] == bid for st in b["stairs"] if st["id"] == stair)
        s["blocked"] = "rubble"
        specs = _plan(blocked)
        label = "%s_%s" % (bid, stair)
        old_ramp = next(x for x in base if x["label"] == label)
        new_ramp = next(x for x in specs if x["label"] == label)
        for k in ("location", "scale", "yaw", "pitch"):
            self.assertEqual(old_ramp[k], new_ramp[k], k)                # the ramp stays
        self.assertIn("stair_blocked=rubble", new_ramp["tags"])
        extra = [x for x in specs if x["label"].startswith(label + "_") and x["label"] not in {y["label"] for y in base}]
        kinds = sorted((x["kind"], next(t for t in x["tags"] if t in ("flight_block", "nav_null", "rubble_flight"))) for x in extra)
        self.assertEqual(kinds, [("cube", "flight_block"), ("cube", "rubble_flight"), ("nav_modifier", "nav_null")])
        nav = next(x for x in extra if x["kind"] == "nav_modifier")
        self.assertEqual(nav["area"], "NavArea_Null")
        box = next(x for x in extra if "flight_block" in x["tags"])
        self.assertEqual(box["collision"], builder.COLLISION_BLOCK_ALL)
        self.assertAlmostEqual(nav["location"][0], box["location"][0])
        self.assertAlmostEqual(nav["yaw"], new_ramp["yaw"])
        self.assertEqual(len(specs), len(base) + 3)

    def test_blocked_flight_extended(self):
        self._check(_load("L_Gen_Train_1"), "B_house", "B_house_stair_0")

    def test_blocked_flight_legacy_path(self):
        self._check(_load("gen_ref_01_02_depot_1"), "B_hall_n", "B_hall_n_stair_0")


class BalconyAndSnowRampTest(unittest.TestCase):
    def test_balcony_slab_rails_and_door(self):
        lay = copy.deepcopy(_load("L_Gen_Train_1"))
        lay["schema"] = V2
        h = _house(lay)
        # hangs on the x = 450 facade at floor 1; its door is not in openings -> synthesised at y = 100
        h["balconies"] = [{"id": "BAL_1", "floor": 1, "rect": [[300, 0], [450, 200]], "door_id": "B_house_bal_door_1", "rail_cm": 95}]
        specs = _plan(lay)
        slab = next(s for s in specs if s["label"] == "BAL_1_slab")
        self.assertIn("balcony_slab", slab["tags"])
        self.assertAlmostEqual(slab["location"][2] + slab["scale"][2] * 50.0, builder.z_of(1))   # top at floor 1
        rails = _with_tag(specs, "balcony_rail")
        self.assertEqual(sorted(r["label"] for r in rails), ["BAL_1_rail_N", "BAL_1_rail_S", "BAL_1_rail_W"])  # not on the facade
        for r in rails:
            self.assertIn("CodexMantleEdge", r["tags"])
            self.assertAlmostEqual(r["scale"][2] * 100.0, 95.0)
            self.assertAlmostEqual(r["location"][2], builder.z_of(1) + 47.5)
        # the door is cut in the floor-1 facade (a door lintel there) and gets its /2 state (exterior -> closed leaf)
        lintels = [s for s in _with_tag(specs, "door_lintel") if "floor=1" in s["tags"] and abs(s["location"][1] - 100.0) < 1.0]
        self.assertEqual(len(lintels), 1)
        self.assertTrue([s for s in _with_tag(specs, "opening=B_house_bal_door_1") if "door_state=closed" in s["tags"]])

    def test_balcony_door_in_openings_not_doubled(self):
        b = {"id": "B", "footprint": [[0, 0], [1000, 1000]], "openings": [{"id": "D", "kind": "door", "floor": 1, "pos": [0, 500]}],
             "balconies": [{"id": "BAL", "floor": 1, "rect": [[-150, 400], [0, 600]], "door_id": "D", "rail_cm": 95}]}
        self.assertEqual(builder.balcony_door_openings(b), [])
        self.assertEqual(builder.balcony_wall_side(b["balconies"][0], b["footprint"]), "E")

    def test_snow_ramp_to_window_sill(self):
        lay = copy.deepcopy(_load("L_Gen_Train_1"))
        lay["schema"] = V2
        h = _house(lay)
        h["snow_ramps"] = [{"id": "SR_1", "from": [50, -867], "to_opening": "B_house_win_south_f0_1", "width_cm": 160}]
        specs = _plan(lay)
        r = next(s for s in specs if "snow_ramp=SR_1" in s["tags"])
        self.assertAlmostEqual(r["scale"][1] * 100.0, 160.0)
        # the top surface's far end (local +X) at the sill (95 cm) at the wall's outer face (x = 450 - 15)
        length = r["scale"][0] * 100.0
        p = math.radians(r["pitch"])
        top_end_z = r["location"][2] + builder.SLAB / 2.0 + math.sin(p) * length / 2.0
        top_end_x = r["location"][0] + math.cos(p) * length / 2.0
        self.assertAlmostEqual(top_end_z, 95.0, places=3)
        self.assertAlmostEqual(top_end_x, 435.0, places=3)
        self.assertLess(r["pitch"], builder.SNOW_RAMP_MAX_DEG)

    def test_snow_ramp_missing_opening_skipped(self):
        lay = copy.deepcopy(_load("L_Gen_Train_1"))
        _house(lay)["snow_ramps"] = [{"id": "SR_X", "from": [50, -867], "to_opening": "nope", "width_cm": 160}]
        self.assertFalse([s for s in _plan(lay) if "snow_ramp=SR_X" in s.get("tags", [])])


class _Struct:
    """A stand-in unreal struct: set_editor_property / get_editor_property over a dict."""

    def __init__(self, **props):
        self.props = dict(props)

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props[name]


class PlacerFlagsTest(unittest.TestCase):
    """Placer.apply_flags with a stand-in unreal module: the spec flags reach the component."""

    def _fake(self):
        return types.SimpleNamespace(
            CollisionResponseType=types.SimpleNamespace(ECR_BLOCK="BLOCK", ECR_IGNORE="IGNORE", ECR_OVERLAP="OVERLAP"),
            ResponseChannel=_Struct)

    def _comp(self, calls):
        body = _Struct(collision_responses=_Struct(response_array=[]), collision_profile_name="BlockAll")
        comp = _Struct(body_instance=body)
        comp.set_can_ever_affect_navigation = lambda v: calls.append(("nav", v))
        # the setter that does NOT survive a save + reload in UE 5.8 must never be used for the responses
        comp.set_collision_response_to_channel = lambda ch, r: calls.append(("SETTER", ch, r))
        return comp, body

    def test_apply_flags(self):
        calls = []
        comp, body = self._comp(calls)
        saved = builder.unreal
        builder.unreal = self._fake()
        try:
            placer = builder.Placer.__new__(builder.Placer)
            placer.apply_flags(comp, {"label": "leaf", "affects_navigation": False, "collision": builder.COLLISION_LEAF})
            self.assertEqual(calls, [("nav", False)])
            self.assertIs(comp.get_editor_property("body_instance"), body)
            self.assertEqual(body.get_editor_property("collision_profile_name"), "Custom")
            # the mesh's default collision would overwrite the Custom responses (bake: FinishAllCompilation; game: registration)
            self.assertIs(comp.get_editor_property("use_default_collision"), False)
            array = body.get_editor_property("collision_responses").get_editor_property("response_array")
            self.assertEqual([(e.get_editor_property("channel"), e.get_editor_property("response")) for e in array],
                             [("Pawn", "IGNORE"), ("Visibility", "BLOCK")])
            calls.clear()
            comp2, body2 = self._comp(calls)
            placer.apply_flags(comp2, {"label": "wall"})                # a legacy spec: untouched
            self.assertEqual(calls, [])
            self.assertEqual(body2.get_editor_property("collision_profile_name"), "BlockAll")
            self.assertNotIn("use_default_collision", comp2.props)
        finally:
            builder.unreal = saved

    def test_nav_property_fallback(self):
        comp = _Struct()
        saved = builder.unreal
        builder.unreal = self._fake()
        try:
            placer = builder.Placer.__new__(builder.Placer)
            placer.apply_flags(comp, {"label": "leaf", "affects_navigation": False})
            self.assertIs(comp.get_editor_property("can_ever_affect_navigation"), False)
        finally:
            builder.unreal = saved


if __name__ == "__main__":
    unittest.main()
