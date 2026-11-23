"""Unit tests for the builder's legacy stairwell gaps (build_levelgen_map.stairwell_gap_rect, 2026-11-23): the PmcTraversalSmoke class (b)
failures on L_Gen_Depot_X4 (docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md section 4). The pre-2026-11-18 layout's floor-1 gap
reaches 40 cm past the flight's top in an 80 cm band: the arrival slab must be whole right after the top across the full flight width."""

import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402

BLOCK_STAIR = {"id": "B_x1_8_t7_block_stair_0", "pos": [-7500, -7200], "from": 0, "to": 1, "kind": "stairwell", "top": [-7500, -6771]}
LEGACY_GAP = (-7540.0, -7240.0, -7460.0, -6731.0)  # the accepted X4 B_x1_8_t7_block_gap_stair_f1


def _covered(specs, label_prefix, x, y, top_z):
    """Is (x, y) under a slab piece of `label_prefix` whose top is top_z (axis-aligned cubes)?"""
    for s in specs:
        if not s["label"].startswith(label_prefix) or abs(s.get("yaw", 0.0)) > 1e-6:
            continue
        cx, cy, cz = s["location"]
        sx, sy, sz = (v * 100.0 for v in s["scale"])
        if abs(cz + sz / 2.0 - top_z) < 0.5 and abs(x - cx) <= sx / 2.0 and abs(y - cy) <= sy / 2.0:
            return True
    return False


class BuildLevelGenStairGapTest(unittest.TestCase):
    def test_exact_gap_is_the_flight_footprint(self):
        self.assertEqual(builder.exact_stairwell_gap_rect(BLOCK_STAIR), (-7580.0, -7200, -7420.0, -6771))

    def test_legacy_gap_becomes_exact_a_current_one_stays(self):
        exact = builder.exact_stairwell_gap_rect(BLOCK_STAIR)
        self.assertEqual(builder.stairwell_gap_rect("B", "g", LEGACY_GAP, [BLOCK_STAIR]), exact)
        near = (exact[0] + 1.0, exact[1], exact[2], exact[3] - 1.0)  # within STAIRWELL_GAP_TOL: cut as it is (bit-identical rebuilds)
        self.assertEqual(builder.stairwell_gap_rect("B", "g", near, [BLOCK_STAIR]), near)
        other = (-9000.0, -7240.0, -8920.0, -6731.0)  # not over the flight
        self.assertEqual(builder.stairwell_gap_rect("B", "g", other, [BLOCK_STAIR]), other)
        self.assertEqual(builder.stairwell_gap_rect("B", "g", LEGACY_GAP, []), LEGACY_GAP)

    def test_x4_block_arrival_slab_is_whole_after_the_top(self):
        layout = builder.load_layout(os.path.join(ACCEPTED_DIR, "gen_ref_01_02_depot_1_x4.json"))
        specs = builder.plan(layout)
        for bid, (px, ty) in {"B_x1_8_t7_block": (-7500, -6771), "B_x1_16_t7_block": (-4700, -11401), "B_x2_25_t8_block": (-7400, -15819)}.items():
            prefix = "%s_floor1" % bid
            for dx in (-70, -40, 0, 40, 70):  # across the 150 cm flight
                self.assertTrue(_covered(specs, prefix, px + dx, ty + 10, 300.0), "%s: slab right after the top at dx %d" % (bid, dx))
                self.assertTrue(_covered(specs, prefix, px + dx, ty + 35, 300.0), "%s: slab 35 cm after the top at dx %d" % (bid, dx))
                self.assertFalse(_covered(specs, prefix, px + dx, ty - 10, 300.0), "%s: open over the flight's top end at dx %d" % (bid, dx))

    def test_x4_reports_the_hall_flight_inside_the_office(self):
        layout = builder.load_layout(os.path.join(ACCEPTED_DIR, "gen_ref_01_02_depot_1_x4.json"))
        lines = builder.stairs_in_other_buildings(layout)
        self.assertEqual(len(lines), 1)
        self.assertIn("B_hall_n_stair_0", lines[0])
        self.assertIn("B_office_w", lines[0])
        lab = builder.load_layout(os.path.join(ACCEPTED_DIR, "gen_story_showcase.json"))
        self.assertEqual(builder.stairs_in_other_buildings(lab), [])


if __name__ == "__main__":
    unittest.main()
