"""Task 087 (docs/design/PMC_DEFENCE.md D2): build_levelgen_map.py carries the layout's generated PMC posts into the level JSON
"pmc_squads" (count / weights / posture / delay_s + post {id, kind, pos, yaw}, no "spawn") and keeps the per-map tuned entries on a
rebuild from the same layout. Writes only into a temp folder (never Content/Data/LevelJson)."""

import json
import os
import shutil
import sys
import tempfile
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))
ACCEPTED_DIR = os.path.join(PROJECT_DIR, "Content", "Data", "LevelGen", "accepted")

sys.path.insert(0, EDITOR_DIR)
import build_levelgen_map as builder  # noqa: E402

MAP = "/Game/Maps/Generated/L_Gen_Depot_X4"


def pmc_block():
    return {"version": 1, "seed": 87, "geometry_hash": "1f399583ed257447", "charges": [], "report": {}, "posts": [
        {"id": "PMC_01", "kind": "overwatch_roof", "posture": "defend", "count": 2, "delay_s": 0, "layer": "roof:B_steelworks",
         "pos": [-1500, 4350, 900], "yaw": 131, "source": "B_steelworks#s0", "building": "B_steelworks", "score": 0.9,
         "anchor": [-1700, 3000, 0], "anchor_layer": "ground", "route_m": 13.6},
        {"id": "PMC_02", "kind": "ambush_exit", "posture": "hunt", "count": 3, "delay_s": 120, "layer": "ground",
         "pos": [6125, 6025, 0], "yaw": 3, "source": "HS_83", "place": "CP_32", "score": 0.89,
         "anchor": [693, 6025, 0], "anchor_layer": "ground", "route_m": 54.32},
    ]}


class BuildLevelGenPmcTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = builder.load_layout(os.path.join(ACCEPTED_DIR, "gen_ref_01_02_depot_1_x4.json"))

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="pmc087_")
        self._dir = builder.LEVELJSON_DIR
        builder.LEVELJSON_DIR = self.tmp

    def tearDown(self):
        builder.LEVELJSON_DIR = self._dir
        shutil.rmtree(self.tmp, ignore_errors=True)

    def layout(self, pmc=True):
        layout = dict(self.base)
        if pmc:
            layout["pmc"] = pmc_block()
        return layout

    def read(self):
        with open(os.path.join(self.tmp, "L_Gen_Depot_X4.json"), encoding="utf-8") as f:
            return json.load(f)

    def test_posts_become_pmc_squads_with_post_instead_of_random_spawn(self):
        squads = builder.level_json(self.layout(), MAP)["pmc_squads"]
        self.assertEqual(len(squads), 2)
        first = squads[0]
        self.assertEqual(first["count"], 2)
        self.assertEqual(first["weights"], "pmc_commander.json")
        self.assertEqual(first["posture"], "defend")
        self.assertEqual(first["delay_s"], 0)
        self.assertNotIn("spawn", first)
        self.assertEqual(first["post"], {"id": "PMC_01", "kind": "overwatch_roof", "pos": [-1500, 4350, 900], "yaw": 131})
        self.assertEqual((squads[1]["posture"], squads[1]["delay_s"], squads[1]["post"]["kind"]), ("hunt", 120, "ambush_exit"))
        for s in squads:
            self.assertEqual(set(s), {"count", "weights", "posture", "delay_s", "post"})

    def test_no_pmc_block_no_pmc_squads(self):
        self.assertNotIn("pmc_squads", builder.level_json(self.layout(pmc=False), MAP))

    def test_rebuild_same_layout_keeps_tuned_entries_and_hand_squads(self):
        builder.write_level_json(self.layout(), MAP)
        data = self.read()
        # the designer tunes PMC_01, adds a hand random squad; a stale generated post is in the file too
        data["pmc_squads"][0].update({"count": 5, "delay_s": 30, "seed": 7})
        data["pmc_squads"].append({"count": 2, "spawn": "random", "posture": "patrol", "delay_s": 60})
        data["pmc_squads"].append({"count": 3, "posture": "defend", "delay_s": 0, "post": {"id": "PMC_09", "kind": "window", "pos": [0, 0, 0], "yaw": 0}})
        with open(os.path.join(self.tmp, "L_Gen_Depot_X4.json"), "w", encoding="utf-8") as f:
            json.dump(data, f)
        moved = self.layout()
        moved["pmc"]["posts"][0]["pos"] = [-1500, 4910, 900]
        builder.write_level_json(moved, MAP)
        squads = self.read()["pmc_squads"]
        self.assertEqual([s.get("post", {}).get("id") for s in squads], ["PMC_01", "PMC_02", None])
        self.assertEqual((squads[0]["count"], squads[0]["delay_s"], squads[0]["seed"]), (5, 30, 7), "tuned keys kept")
        self.assertEqual(squads[0]["post"]["pos"], [-1500, 4910, 900], "the post position refreshed")
        self.assertEqual(squads[2]["spawn"], "random", "the hand squad kept")

    def test_rebuild_without_posts_drops_generated_posts_other_layout_starts_fresh(self):
        builder.write_level_json(self.layout(), MAP)
        builder.write_level_json(self.layout(pmc=False), MAP)
        posts = [e for e in self.read().get("pmc_squads", []) if isinstance(e.get("post"), dict)]
        self.assertEqual(posts, [], "the «ЧВК» toggle off -> no generated post entries stay in the level JSON (087 lead review)")
        builder.write_level_json(self.layout(), MAP)
        data = self.read()
        data["levelgen_layout"] = "Content/Data/LevelGen/accepted/other.json"
        data["pmc_squads"][0]["count"] = 6
        with open(os.path.join(self.tmp, "L_Gen_Depot_X4.json"), "w", encoding="utf-8") as f:
            json.dump(data, f)
        builder.write_level_json(self.layout(), MAP)
        self.assertEqual(self.read()["pmc_squads"][0]["count"], 2, "another layout -> written afresh")

    def test_merge_matches_post_id_and_kind(self):
        old = [{"count": 6, "posture": "defend", "delay_s": 0, "post": {"id": "PMC_01", "kind": "window", "pos": [1, 2, 3], "yaw": 0}}]
        new = builder.pmc_squads_of(self.layout())
        merged = builder.merge_pmc_squads(old, new)
        self.assertEqual(merged[0]["count"], 2, "same id, another kind = another post: not the tuned entry")


if __name__ == "__main__":
    unittest.main()
