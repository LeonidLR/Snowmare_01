"""Unit tests for the parapet FIRING GAPS in build_levelgen_map.py (docs/design/levelgen/FIRING_GAPS_HANDOVER.md step 6): each gap of the
layout's top-level `firing_gaps` is a span cut from its parapet and refilled with a low sill; a sill that overlaps a ladder exit cut is
clipped to what still stands; a layout without `firing_gaps` builds exactly as before."""

import copy
import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402


def _gap(gid, parapet, center, outward, width=90.0, sill=60.0, building="B_house"):
    return {"id": gid, "building": building, "parapet": parapet, "center": center, "width_cm": width, "sill_cm": sill,
            "outward": outward, "stand": [center[0] - outward[0] * 60.0, center[1] - outward[1] * 60.0], "facing_yaw": 0.0}


class FiringGapBuilderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout = builder.load_layout(os.path.join(ACCEPTED_DIR, "L_Gen_Train_1.json"))

    def _plan(self, gaps=None):
        layout = copy.deepcopy(self.layout)
        if gaps is not None:
            layout["firing_gaps"] = gaps
        return builder.plan(layout)

    @staticmethod
    def _parapet_pieces(specs, wall):
        return sorted((s for s in specs if s["label"].startswith(wall) and "firing_gap" not in s.get("tags", []) and "parapet" in s.get("tags", [])), key=lambda s: s["label"])

    def test_no_firing_gaps_key_builds_exactly_as_before(self):
        base = self._plan()
        self.assertEqual(base, self._plan([]), "an empty list = no key")
        self.assertFalse([s for s in base if "firing_gap" in s.get("tags", [])])
        # a gap of another building / of a parapet that is not there changes nothing
        self.assertEqual(base, self._plan([_gap("FG_x_1", "B_house_parapet_S", [450, -100], [-1, 0], building="B_other")]))
        self.assertEqual(base, self._plan([_gap("FG_B_house_1", "B_house_parapet_nope", [450, -100], [-1, 0])]))

    def test_one_gap_cuts_its_parapet_and_spawns_a_sill(self):
        base = self._plan()
        specs = self._plan([_gap("FG_B_house_1", "B_house_parapet_S", [450, -100], [-1, 0])])
        # the S parapet (x = 450, y -1400 .. 1200) falls into two pieces round the 90 cm gap; the other parapets are untouched
        before = self._parapet_pieces(base, "B_house_parapet_S")
        after = self._parapet_pieces(specs, "B_house_parapet_S")
        self.assertEqual([s["label"] for s in before], ["B_house_parapet_S"])
        self.assertEqual([s["label"] for s in after], ["B_house_parapet_S_l0", "B_house_parapet_S_l1"])
        for w in ("B_house_parapet_N", "B_house_parapet_W"):
            self.assertEqual(self._parapet_pieces(base, w), self._parapet_pieces(specs, w))
        # the two pieces together are the old parapet minus 90 cm
        length = lambda s: s["scale"][0] * 100.0  # noqa: E731
        self.assertAlmostEqual(sum(length(s) for s in after), length(before[0]) - 90.0, delta=0.5)
        # the sill: label, folder, tags, base z and thickness of the parapet, 60 cm high, 90 cm long, centred on the gap
        sills = [s for s in specs if "firing_gap" in s.get("tags", [])]
        self.assertEqual(len(sills), 1)
        sill, par = sills[0], before[0]
        self.assertEqual(sill["label"], "FG_B_house_1_sill")
        self.assertEqual(sill["folder"], "Buildings/B_house/Parapets")
        self.assertEqual(sill["tags"], ["parapet", "firing_gap", "CodexMantleEdge", "building=B_house", "floor=2", "gap=FG_B_house_1"])
        self.assertAlmostEqual(sill["scale"][0] * 100.0, 90.0, places=1)
        self.assertAlmostEqual(sill["scale"][1], par["scale"][1], places=3)
        self.assertAlmostEqual(sill["scale"][2] * 100.0, 60.0, places=1)
        self.assertAlmostEqual(sill["location"][0], 450.0, places=1)
        self.assertAlmostEqual(sill["location"][1], -100.0, places=1)
        base_z = par["location"][2] - par["scale"][2] * 50.0
        self.assertAlmostEqual(sill["location"][2] - 30.0, base_z, places=1, msg="the sill stands on the parapet's base z")
        self.assertAlmostEqual(sill["yaw"], par["yaw"], places=3)
        self.assertEqual(len(specs), len(base) + 2, "+1 parapet piece, +1 sill")

    def test_the_numbers_come_from_the_gap_entry(self):
        specs = self._plan([_gap("FG_B_house_1", "B_house_parapet_S", [450, -100], [-1, 0], width=100.0, sill=70.0)])
        sill = [s for s in specs if "firing_gap" in s.get("tags", [])][0]
        self.assertAlmostEqual(sill["scale"][0] * 100.0, 100.0, places=1)
        self.assertAlmostEqual(sill["scale"][2] * 100.0, 70.0, places=1)

    def test_a_gap_over_a_ladder_exit_is_clipped_to_what_stands(self):
        # LAD_B_house tops out at (1031, 1100) on the E parapet (y = 1200, from x = 1650 towards 450): the exit cut is 200 cm wide
        # round t = 619, i.e. t 519 .. 719 from the parapet's start
        base = self._plan()
        ladder_cut_pieces = self._parapet_pieces(base, "B_house_parapet_E")
        self.assertEqual([s["label"] for s in ladder_cut_pieces], ["B_house_parapet_E_l0", "B_house_parapet_E_l1"])
        # a gap at t 480 (x = 1170) spans 435 .. 525: the last 6 cm lie in the exit cut -> the sill is 84 cm (435 .. 519)
        specs = self._plan([_gap("FG_B_house_1", "B_house_parapet_E", [1170, 1200], [0, 1])])
        sills = [s for s in specs if "firing_gap" in s.get("tags", [])]
        self.assertEqual(len(sills), 1)
        self.assertAlmostEqual(sills[0]["scale"][0] * 100.0, 84.0, delta=0.2)
        self.assertAlmostEqual(sills[0]["location"][0], 1650.0 - (435.0 + 519.0) / 2.0, delta=0.2)
        # the standing parapet ends at the gap (435) and resumes after the exit cut (719): the gap and the cut merged
        pieces = self._parapet_pieces(specs, "B_house_parapet_E")
        self.assertEqual(len(pieces), 2)
        self.assertAlmostEqual(pieces[0]["scale"][0] * 100.0, 435.0, delta=0.2)
        self.assertAlmostEqual(pieces[1]["scale"][0] * 100.0, 1200.0 - 719.0, delta=0.2)
        # a gap fully inside the exit cut: no sill left (the opening is already wider)
        inside = self._plan([_gap("FG_B_house_2", "B_house_parapet_E", [1650.0 - 619.0, 1200], [0, 1])])
        self.assertFalse([s for s in inside if "firing_gap" in s.get("tags", [])])
        self.assertEqual(self._parapet_pieces(inside, "B_house_parapet_E"), ladder_cut_pieces)

    def test_span_helpers(self):
        self.assertEqual(builder._span_minus((0.0, 100.0), []), [(0.0, 100.0)])
        self.assertEqual(builder._span_minus((0.0, 100.0), [(40.0, 60.0, "x")]), [(0.0, 40.0), (60.0, 100.0)])
        self.assertEqual(builder._span_minus((0.0, 100.0), [(-10.0, 95.0, "x")]), [], "a 5 cm rest is dropped")
        walls = [{"id": "P", "a": [0, 0], "b": [1000, 0], "thickness": 20, "height": 95}]
        spans = builder.firing_gap_spans(walls, [{"id": "G", "parapet": "P", "center": [300, 0], "width_cm": 90}, {"id": "H", "parapet": "Q", "center": [0, 0], "width_cm": 90}])
        self.assertEqual(spans, {"P": [(255.0, 345.0, "G")]})

    def test_a_rotated_building_gets_its_gaps_in_its_own_frame(self):
        # footprint 1000 x 500 turned 90 degrees about its centre (500, 250): the local +X side (x = 1000) is the world +Y side
        b = {"id": "B_r", "footprint": [[0, 0], [1000, 500]], "rotation": 90, "floors": 2, "parapets": [], "stairs": [], "doors": []}
        gap = _gap("FG_B_r_1", "B_r_parapet_N", [500.0, 750.0], [0.0, 1.0], building="B_r")
        gap["facing_yaw"] = 90.0
        layout = {"buildings": [b], "walls": [], "ladders": [], "firing_gaps": [gap, _gap("FG_other_1", "O_parapet_N", [0, 0], [1, 0], building="B_o")]}
        local, centre, deg = builder._building_local(layout, b)
        self.assertEqual((centre, deg), ((500.0, 250.0), 90.0))
        self.assertEqual([g["id"] for g in local["firing_gaps"]], ["FG_B_r_1"], "only this building's gaps")
        g = local["firing_gaps"][0]
        self.assertEqual(g["center"], [1000.0, 250.0])
        self.assertEqual(g["stand"], [940.0, 250.0])
        self.assertEqual([round(v, 4) for v in g["outward"]], [1.0, 0.0])
        self.assertAlmostEqual(g["facing_yaw"], 0.0)
        self.assertEqual((g["width_cm"], g["sill_cm"]), (90.0, 60.0))


if __name__ == "__main__":
    unittest.main()
