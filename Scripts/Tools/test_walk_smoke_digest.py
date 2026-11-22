"""Tests for walk_smoke_digest.py (task 098). Run: python -m pytest Scripts/Tools/test_walk_smoke_digest.py -q"""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import walk_smoke_digest as w  # noqa: E402

FIX = Path(__file__).parent / "fixtures" / "walk_smoke"
BEFORE = FIX / "kit_before.log"
AFTER = FIX / "kit_after.log"
AFTER_JSON = FIX / "kit_after.json"


class ClassifyTest(unittest.TestCase):
    def test_every_class(self):
        self.assertEqual(w.classify("squad 2.8 s > 2 x solo 1.2 s (ratio 2.36)"), "ratio")
        self.assertEqual(w.classify("follower Medic-Sapper STUCK (move active) after 6.0 s"), "follower_stuck")
        self.assertEqual(w.classify("leader Commander STOPPED (no move order running) after 10.2 s"), "leader_stopped")
        self.assertEqual(w.classify("no complete navmesh path"), "refused")
        self.assertEqual(w.classify("start X off navmesh"), "refused")
        self.assertEqual(w.classify("TIMED OUT after 23 s"), "other")
        self.assertEqual(w.classify("follower Engineer STOPPED (no move order running)"), "other")

    def test_storey(self):
        self.assertEqual(w.storey_of("S01_open_r_f1_c9", "S01_open_r_f1_c12"), "f1")
        self.assertEqual(w.storey_of("storey_0", "S04_stair_0"), "f0")
        self.assertEqual(w.storey_of("S07_stair_1", "roof"), "st1")
        self.assertEqual(w.storey_of("door_1", "roof"), "roof")


class LogParseTest(unittest.TestCase):
    def setUp(self):
        self.runs = w.parse_log(BEFORE)

    def test_header_from_result_line(self):
        self.assertEqual(len(self.runs), 1)
        r = self.runs[0]
        self.assertEqual(r["map"], "L_Gen_StoryShowcase_Kit")
        h = r["header"]
        self.assertEqual((h["buildings_passed"], h["buildings"]), (0, 6))
        self.assertEqual((h["walk_passed"], h["walk"]), (19, 21))
        self.assertEqual((h["entry_passed"], h["entry"]), (102, 120))
        self.assertEqual(h["max_ratio"], 3.29)
        self.assertEqual(h["wall_s"], 657)

    def test_class_counts(self):
        cls = w.by_class(self.runs[0]["fails"])
        counts = {c: cls[c]["count"] for c in w.CLASSES}
        self.assertEqual(counts, {"ratio": 2, "follower_stuck": 1, "leader_stopped": 1, "refused": 1, "other": 1})
        self.assertEqual(cls["follower_stuck"]["stories"], {"S01_open": ["f1"]})
        self.assertEqual(cls["refused"]["stories"], {"B_y": ["st0"]})

    def test_ok_leg_is_not_a_fail(self):
        self.assertEqual(self.runs[0]["legs"], 8)
        self.assertEqual(len(self.runs[0]["fails"]), 6)

    def test_stuck_meshes(self):
        self.assertEqual(dict(self.runs[0]["stuck"]), {"StaticMeshActor_258": 3, "StaticMeshActor_1200": 1})


class ClusterTest(unittest.TestCase):
    def test_cluster_rule_150cm(self):
        f = lambda x, z=0.0, s="A", c="ratio": {"pos": (x, 0.0, z), "story": s, "cause": c}
        cl = w.clusters([f(0), f(149, s="B", c="other"), f(1000), f(0, z=300)])
        self.assertEqual(len(cl), 1)
        self.assertEqual(cl[0]["count"], 2)
        self.assertEqual(cl[0]["stories"], ["A", "B"])
        self.assertEqual(cl[0]["causes"], {"ratio": 1, "other": 1})
        self.assertEqual(w.clusters([f(0), f(151)]), [])

    def test_fixture_cluster(self):
        cl = w.digest(w.parse_log(BEFORE)[0])["clusters"]
        self.assertEqual(len(cl), 1)
        self.assertEqual(cl[0]["count"], 2)
        self.assertEqual(cl[0]["stories"], ["S01_open"])


class JsonAndDeltaTest(unittest.TestCase):
    def test_json_report(self):
        r = w.parse_json(AFTER_JSON)[0]
        self.assertEqual(r["map"], "L_Gen_StoryShowcase_Kit")
        self.assertEqual([f["cause"] for f in r["fails"]], ["ratio", "follower_stuck"])
        self.assertEqual(r["header"]["entry_passed"], 118)

    def test_delta_before_after(self):
        ds = w.build([BEFORE, AFTER])
        self.assertNotIn("delta", ds[0])
        dl = ds[1]["delta"]
        self.assertEqual(dl["fail_total"], -5)
        self.assertEqual(dl["classes"], {"ratio": -2, "follower_stuck": 0, "leader_stopped": -1, "refused": -1, "other": -1})

    def test_delta_log_vs_json_same_map(self):
        ds = w.build([BEFORE, AFTER_JSON])
        self.assertIn("delta", ds[1])  # same map in json -> delta too


class OutputTest(unittest.TestCase):
    def test_text_lines_limit_and_content(self):
        text = w.render(w.build([BEFORE])[0])
        self.assertLessEqual(len(text.splitlines()), 25)
        self.assertIn("buildings 0/6", text)
        self.assertIn("2 fails at", text)
        self.assertIn("StaticMeshActor_258 x3", text)

    def test_json_cli(self):
        import io
        import contextlib
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = w.main([str(BEFORE), str(AFTER), "--json"])
        self.assertEqual(rc, 0)
        data = json.loads(buf.getvalue())
        self.assertEqual(len(data), 2)
        self.assertIn("delta", data[1])

    def test_missing_file(self):
        self.assertEqual(w.main([str(FIX / "nope.log")]), 2)


if __name__ == "__main__":
    unittest.main()
