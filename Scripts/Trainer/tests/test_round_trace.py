"""Unit tests for Scripts/Tools/pmc_round_trace.py (Task T-01 / 089).

Verifies round trace analysis, timeline extraction, open standing time,
first-shot timestamps, and hypotheses H1-H4 evaluation against fixture data.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

# Add Scripts and Scripts/Tools to path
REPO_ROOT = Path(__file__).resolve().parents[3]
SCRIPTS_DIR = REPO_ROOT / "Scripts"
TOOLS_DIR = SCRIPTS_DIR / "Tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import pmc_round_trace as prt


class TestPmcRoundTrace(unittest.TestCase):
    def setUp(self):
        self.fixtures_dir = REPO_ROOT / "Scripts" / "Trainer" / "fixtures"
        self.sample_arena = self.fixtures_dir / "sample_arena_objective_result.json"
        self.sample_track = self.fixtures_dir / "real_arena_round.track.json"

    def test_fixture_files_exist(self):
        self.assertTrue(self.sample_arena.is_file(), f"Missing {self.sample_arena}")
        self.assertTrue(self.sample_track.is_file(), f"Missing {self.sample_track}")

    def test_parse_arena_round(self):
        with open(self.sample_arena, "r", encoding="utf-8") as f:
            arena_data = json.load(f)

        r1, meta = prt.parse_arena_round(arena_data, round_index=1)
        self.assertEqual(r1.get("index"), 1)
        self.assertEqual(r1.get("scenario"), "chokepoint")
        self.assertEqual(r1.get("winner"), "A")

        r2, _ = prt.parse_arena_round(arena_data, round_index=2)
        self.assertEqual(r2.get("index"), 2)
        self.assertEqual(r2.get("scenario"), "roofs")
        self.assertEqual(r2.get("winner"), "A")

    def test_resolve_files(self):
        resolved_track = prt.resolve_track_file(self.sample_arena, 1, explicit_track=str(self.sample_track))
        self.assertEqual(resolved_track, self.sample_track)

        no_track = prt.resolve_track_file(Path("/nonexistent/file.arena.json"), 99)
        self.assertIsNone(no_track)

    def test_log_parser_synthetic(self):
        # Create synthetic log lines
        synthetic_log = (
            "[PmcArena] round 1: A vs B\n"
            "[PmcAI] team 2: spawned Operator Lind (look dark, m16) at V(X=-8425.00, Y=-1925.00, Z=105.00)\n"
            "[PmcAI] team 2: Operator Lind placed on a ambush post V(X=-8425.00, Y=-1925.00, Z=10.00) (yaw 159, score 1.22, danger 0.29, HS_05)\n"
            "[PmcAI] team 2: Operator Lind -> Ambush (score 1.54; threat none at 0 cm, hp 100%, cover 0.00)\n"
            "[PmcAI] team 2: Operator Lind opens fire (threat at 150 cm, attention off 0.84)\n"
            "[PmcAI] team 2: Operator Lind -> FallBack (score 1.27; threat seen at 150 cm, hp 100%, cover 0.00)\n"
            "[PmcAI] team 2: Operator Lind's FallBack failed (ended short) - not again for 5.0 s\n"
            "[PmcArena] round 2: A vs B\n"
            "[PmcAI] team 2: spawned Operator Other\n"
        )
        temp_log = self.fixtures_dir / "_temp_test.log"
        try:
            temp_log.write_text(synthetic_log, encoding="utf-8")
            parsed = prt.parse_log_for_round(temp_log, round_index=1)
            self.assertTrue(parsed["round_found"])
            self.assertIn("Lind", parsed["spawned"])
            self.assertEqual(parsed["spawned"]["Lind"]["team"], 2)

            self.assertIn("Lind", parsed["posts"])
            self.assertEqual(parsed["posts"]["Lind"]["role"], "ambush")
            self.assertEqual(parsed["posts"]["Lind"]["post_id"], "HS_05")
            self.assertEqual(parsed["posts"]["Lind"]["score"], 1.22)

            self.assertEqual(len(parsed["decisions"]), 2)
            self.assertEqual(parsed["decisions"][0]["order"], "Ambush")
            self.assertEqual(parsed["decisions"][0]["score"], 1.54)

            self.assertEqual(len(parsed["fire_events"]), 1)
            self.assertEqual(parsed["fire_events"][0]["dist_m"], 1.5)
            self.assertEqual(parsed["fire_events"][0]["attention_off"], 0.84)

            self.assertEqual(len(parsed["failures"]), 1)
            self.assertEqual(parsed["failures"][0]["order"], "FallBack")
        finally:
            if temp_log.exists():
                temp_log.unlink()

    def test_compute_trace_metrics_fixture(self):
        with open(self.sample_arena, "r", encoding="utf-8") as f:
            arena_data = json.load(f)
        with open(self.sample_track, "r", encoding="utf-8") as f:
            track_data = json.load(f)

        rnd, meta = prt.parse_arena_round(arena_data, 1)
        log_data = {"found": False, "round_found": False, "decisions": [], "fire_events": [], "failures": [], "posts": {}, "spawned": {}}

        metrics = prt.compute_trace_metrics(rnd, meta, track_data, log_data)
        self.assertEqual(metrics["scenario"], "chokepoint")
        self.assertEqual(metrics["def_side"], "B")
        self.assertEqual(metrics["atk_side"], "A")
        self.assertEqual(len(metrics["def_fighters"]), 3)

        # Check exposure stats
        for f in metrics["def_fighters"]:
            fid = f["id"]
            self.assertIn(fid, metrics["exposure_stats"])
            st = metrics["exposure_stats"][fid]
            self.assertIn("total_stand_s", st)
            self.assertIn("pre_contact_stand_s", st)
            self.assertIn("first_shot_t", st)

        # Check hypotheses dictionary structure
        hyp = metrics["hypotheses"]
        for h_key in ("H1", "H2", "H3", "H4"):
            self.assertIn(h_key, hyp)
            self.assertIn("verdict", hyp[h_key])
            self.assertIn("evidence", hyp[h_key])
            self.assertIn(hyp[h_key]["verdict"], ("HOLDS", "DOES NOT HOLD", "CANNOT TELL"))

    def test_hypothesis_evaluators(self):
        # H1 ground vs roof
        v1_ground, _ = prt.evaluate_h1("tactical", {"z": 10.0, "level_m": 1.5}, {}, {}, {"Op1": [{"decision": "Objective"}]})
        self.assertEqual(v1_ground, "HOLDS")

        v1_roof, _ = prt.evaluate_h1("roofs", {"z": 900.0, "level_m": 9.0}, {}, {}, {})
        self.assertEqual(v1_roof, "DOES NOT HOLD")

        # H2 posts vs no posts
        v2_posts, _ = prt.evaluate_h2(
            {"Op1": {"role": "ambush"}},
            {"Op1": [{"t": 5.0, "decision": "FallBack", "score": 1.2}]},
            {"decisions": [{"who": "Op1", "order": "FallBack", "threat_dist_m": 60.0}]},
            t_contact=10.0,
        )
        self.assertEqual(v2_posts, "HOLDS")

        v2_no_posts, _ = prt.evaluate_h2({}, {}, {}, t_contact=10.0)
        self.assertEqual(v2_no_posts, "DOES NOT HOLD")

        # H3 zero cover vs covered
        v3_zero, _ = prt.evaluate_h3({"decisions": [{"cover": 0.0}, {"cover": 0.0}], "failures": []}, {"Op1": {"pre_contact_stand_s": 5.0}}, {})
        self.assertEqual(v3_zero, "HOLDS")

        # H4 attention delay vs immediate
        v4_delay, _ = prt.evaluate_h4(
            {"fire_events": [{"dist_m": 1.5, "attention_off": 0.5}]},
            {"Op1": {"first_shot_t": 10.0}},
            t_contact=5.0,
            shots=[],
        )
        self.assertEqual(v4_delay, "HOLDS")

    def test_markdown_report_generation(self):
        with open(self.sample_arena, "r", encoding="utf-8") as f:
            arena_data = json.load(f)
        with open(self.sample_track, "r", encoding="utf-8") as f:
            track_data = json.load(f)

        rnd, meta = prt.parse_arena_round(arena_data, 1)
        log_data = {"found": False, "round_found": False, "decisions": [], "fire_events": [], "failures": [], "posts": {}, "spawned": {}}

        metrics = prt.compute_trace_metrics(rnd, meta, track_data, log_data)
        md = prt.format_markdown_report(metrics, self.sample_arena, 1)

        self.assertIn("# PMC Round Decision Trace: Round 1", md)
        self.assertIn("## 1. Round Overview & Objective Context", md)
        self.assertIn("## 2. Defender Positions & Initial Posts", md)
        self.assertIn("## 3. Order Sequence & Decision Timeline", md)
        self.assertIn("## 4. Exposure & Combat Engagement Times", md)
        self.assertIn("## 5. Audit 4.1 Hypotheses Evaluation", md)
        self.assertIn("## 6. Diagnostic Status & Stop Rule", md)

    def test_cli_execution(self):
        script_path = TOOLS_DIR / "pmc_round_trace.py"
        cmd = [
            sys.executable,
            str(script_path),
            str(self.sample_arena),
            "--round", "1",
            "--track", str(self.sample_track),
        ]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, 0, f"CLI failed: {proc.stderr}")
        self.assertIn("# PMC Round Decision Trace: Round 1", proc.stdout)


if __name__ == "__main__":
    unittest.main()
