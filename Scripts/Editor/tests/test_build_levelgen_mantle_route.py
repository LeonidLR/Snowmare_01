"""Mantle legs in the bot route (levelgen_bot_route.py, T-02 gap 4): a route link via a mantle edge id marks the end waypoint
"mantle" (+ "mantle_at", the crossing point on the edge). Pure: no Unreal (python -m unittest discover -s Scripts/Editor/tests)."""

import json
import os
import sys
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import levelgen_bot_route as bot  # noqa: E402


def _bridge():
    with open(os.path.join(ACCEPTED_DIR, "gen_lab_bridge.json"), encoding="utf-8") as f:
        return json.load(f)


class MantleBotRouteTest(unittest.TestCase):
    def test_bridge_v3_has_one_mantle_leg_with_the_edge_id(self):
        layout = _bridge()
        v3 = next(r for r in layout["routes"] if r["id"] == "V3_mantle")
        block = bot.bot_route(layout, v3)
        marked = [(i, p) for i, p in enumerate(block["points"]) if "mantle" in p]
        self.assertEqual(len(marked), 1, marked)
        i, wp = marked[0]
        self.assertEqual(i, 4)
        self.assertEqual(wp["mantle"], "me_W_obstacle_s")
        self.assertIn("via me_W_obstacle_s", wp["label"])
        self.assertNotIn("drop", wp)
        self.assertEqual(block["name"], "gen_lab_bridge_V3_mantle")
        # the crossing point lies on the edge (a segment of the layout's mantle_edges), between the leg's two points
        edge = bot.find_mantle_edge(layout, "me_W_obstacle_s")
        self.assertIsNotNone(edge)
        ax, ay = edge["start"]
        bx, by = edge["end"]
        cx, cy = wp["mantle_at"]
        length = ((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5
        distance = abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) / length
        self.assertLess(distance, 1.5, "the crossing point is on the edge's line (rounded to a cm)")
        self.assertTrue(min(ax, bx) - 1 <= cx <= max(ax, bx) + 1 and min(ay, by) - 1 <= cy <= max(ay, by) + 1, wp["mantle_at"])

    def test_the_mandatory_route_and_other_variants_have_no_mantle_leg(self):
        layout = _bridge()
        for r in layout["routes"]:
            if r["id"] == "V3_mantle":
                continue
            block = bot.bot_route(layout, r)
            self.assertFalse(any("mantle" in p for p in block["points"]), r["id"])

    def test_a_leg_that_misses_the_edge_has_no_crossing_point(self):
        layout = _bridge()
        v3 = json.loads(json.dumps(next(r for r in layout["routes"] if r["id"] == "V3_mantle")))
        v3["links"] = [{"at": 6, "via": "me_W_obstacle_s"}]  # a leg along the trench rim, nowhere near the obstacle
        wp = bot.bot_route(layout, v3)["points"][6]
        self.assertEqual(wp["mantle"], "me_W_obstacle_s")
        self.assertNotIn("mantle_at", wp)

    def test_variants_hold_only_the_non_mandatory_mantle_routes(self):
        layout = _bridge()
        variants = bot.bot_route_variants(layout)
        self.assertEqual(list(variants), ["V3_mantle"])
        v3 = next(r for r in layout["routes"] if r["id"] == "V3_mantle")
        self.assertEqual(variants["V3_mantle"], bot.bot_route(layout, v3))
        self.assertEqual(variants["V3_mantle"]["points"][4]["mantle"], "me_W_obstacle_s")

    def test_apply_keeps_the_level_keys_and_places_the_variants_after_bot_route(self):
        layout = _bridge()
        level = {"a": 1, "waves": [], "z": 2}
        out, block = bot.apply_bot_route(level, layout)
        self.assertEqual(list(out), ["a", "bot_route", "bot_route_variants", "waves", "z"])
        self.assertEqual(block, out["bot_route"])
        self.assertEqual(list(out["bot_route_variants"]), ["V3_mantle"])
        # a second pass replaces in place; a layout without a mantle variant drops the stale key
        again, _ = bot.apply_bot_route(dict(out), layout)
        self.assertEqual(list(again), list(out))
        plain = json.loads(json.dumps(layout))
        plain["routes"] = [r for r in plain["routes"] if r["id"] != "V3_mantle"]
        cleaned, _ = bot.apply_bot_route(dict(out), plain)
        self.assertNotIn("bot_route_variants", cleaned)
        self.assertEqual(list(cleaned), ["a", "bot_route", "waves", "z"])

    def test_self_test(self):
        bot._self_test()


if __name__ == "__main__":
    unittest.main()
