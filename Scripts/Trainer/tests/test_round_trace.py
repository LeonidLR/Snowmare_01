"""Unit tests for Scripts/Tools/pmc_round_trace.py (Task T-01 / 089).

Verifies round identification, log partitioning, tactical event filtering,
exposure stats extraction from arena.json, hypothesis evaluations H1-H4,
and markdown formatting on a coherent committed round fixture.
"""
from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
TOOLS_DIR = REPO_ROOT / "Scripts" / "Tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import pmc_round_trace as prt


class TestPmcRoundTrace(unittest.TestCase):
    def setUp(self):
        self.fixtures_dir = Path(__file__).resolve().parent.parent / "fixtures"
        self.arena_file = self.fixtures_dir / "trace_fixture_round_1.arena.json"
        self.track_file = self.fixtures_dir / "trace_fixture_round_1.track.json"
        self.log_file = self.fixtures_dir / "trace_fixture_round_1.log"

    def test_parse_round_identifier(self):
        # 1-based default
        r1, r0 = prt.parse_round_identifier(1, num_rounds=2)
        self.assertEqual((r1, r0), (1, 0))

        # 0-based clip notation
        r1, r0 = prt.parse_round_identifier("r0", num_rounds=2)
        self.assertEqual((r1, r0), (1, 0))
        r1, r0 = prt.parse_round_identifier("r1", num_rounds=2)
        self.assertEqual((r1, r0), (2, 1))

        # Explicit zero_based flag
        r1, r0 = prt.parse_round_identifier(1, num_rounds=2, zero_based=True)
        self.assertEqual((r1, r0), (2, 1))

        # Reject round 0 without zero-based
        with self.assertRaises(ValueError):
            prt.parse_round_identifier(0, num_rounds=2)
        with self.assertRaises(ValueError):
            prt.parse_round_identifier("0", num_rounds=2)

        # Round 0 with zero-based flag succeeds
        r1_0, r0_0 = prt.parse_round_identifier(0, num_rounds=2, zero_based=True)
        self.assertEqual((r1_0, r0_0), (1, 0))

        # Invalid format
        with self.assertRaises(ValueError):
            prt.parse_round_identifier("rX", num_rounds=2)

        # Out of range
        with self.assertRaises(ValueError):
            prt.parse_round_identifier(5, num_rounds=2)
        with self.assertRaises(ValueError):
            prt.parse_round_identifier("r2", num_rounds=2)

    def test_parse_arena_round_strict(self):
        arena_data = json.loads(self.arena_file.read_text(encoding="utf-8"))

        # Match 1-based
        rnd1, meta, idx1, idx0 = prt.parse_arena_round(arena_data, 1)
        self.assertEqual(rnd1["index"], 1)
        self.assertEqual((idx1, idx0), (1, 0))

        # Match clip notation r1 -> index 2
        rnd2, _, idx1_2, idx0_2 = prt.parse_arena_round(arena_data, "r1")
        self.assertEqual(rnd2["index"], 2)
        self.assertEqual((idx1_2, idx0_2), (2, 1))

        # Out of range must raise ValueError (no silent fallback to round 1)
        with self.assertRaises(ValueError):
            prt.parse_arena_round(arena_data, 99)

    def test_log_partitioning_and_holds_fire(self):
        lines = [
            "[PmcAI] team 2: spawned Operator Lind (look dark, m16) at V(X=-8425.00, Y=-1925.00, Z=910.00)",
            "[PmcAI] team 2: Operator Lind placed on a ambush post V(X=-8425.00, Y=-1925.00, Z=910.00) (yaw 159, score 1.22, danger 0.29, HS_05)",
            "[PmcArena] round 1: A vs B",
            "[PmcAI] team 2: Operator Lind -> Ambush (score 1.54; threat none at 0 cm, hp 100%, cover 0.00)",
            "[PmcAI] team 2: Operator Lind holds his fire (threat at 4500 cm, attention off 0.84)",
            "[PmcAI] team 2: Operator Lind opens fire (threat at 1500 cm, attention off 0.84)",
            "[PmcAI] team 2: spawned Operator Cross (look dark, m16) at V(X=-8425.00, Y=-1825.00, Z=10.00)",
            "[PmcAI] team 2: Operator Cross placed on a post V(X=-8425.00, Y=-1825.00, Z=10.00) (yaw 90, score 1.0, danger 0.1, Post_01)",
            "[PmcArena] round 2: A vs B",
            "[PmcAI] team 2: Operator Cross -> Hold (score 1.0; threat none at 0 cm, hp 100%, cover 0.00)",
        ]

        # Round 1 log parsing
        parsed_r1 = prt.parse_log_for_round(None, 1, 0, lines_override=lines)
        self.assertTrue(parsed_r1["round_found"])
        self.assertIn((2, "Lind"), parsed_r1["spawned"])
        self.assertIn((2, "Lind"), parsed_r1["posts"])
        self.assertNotIn((2, "Cross"), parsed_r1["spawned"])  # Cross belongs to round 2!
        self.assertEqual(len(parsed_r1["fire_events"]), 2)
        self.assertEqual(parsed_r1["fire_events"][0]["action"], "holds his fire")
        self.assertEqual(parsed_r1["fire_events"][0]["dist_m"], 45.0)
        self.assertEqual(parsed_r1["fire_events"][1]["action"], "opens fire")

        # Round 2 log parsing
        parsed_r2 = prt.parse_log_for_round(None, 2, 1, lines_override=lines)
        self.assertTrue(parsed_r2["round_found"])
        self.assertIn((2, "Cross"), parsed_r2["spawned"])
        self.assertIn((2, "Cross"), parsed_r2["posts"])
        self.assertNotIn((2, "Lind"), parsed_r2["spawned"])

    def test_coherent_fixture_metrics_and_value_asserts(self):
        arena_data = json.loads(self.arena_file.read_text(encoding="utf-8"))
        track_data = json.loads(self.track_file.read_text(encoding="utf-8"))
        rnd, meta, idx1, idx0 = prt.parse_arena_round(arena_data, 1)
        log_data = prt.parse_log_for_round(self.log_file, idx1, idx0)

        metrics = prt.compute_trace_metrics(rnd, meta, track_data, log_data)

        # Basic metadata
        self.assertEqual(metrics["scenario"], "roofs")
        self.assertEqual(metrics["map"], "/Game/Maps/Generated/L_Gen_Depot_1")
        self.assertEqual(metrics["seed"], 211024966)
        self.assertEqual(metrics["winner"], "B")
        self.assertEqual(metrics["def_side"], "B")
        self.assertEqual(metrics["atk_side"], "A")

        # Objective: storey level & height
        obj = metrics["objective"]
        self.assertEqual(obj["z"], 910)
        self.assertAlmostEqual(obj["height_m"], 9.1, places=1)
        self.assertEqual(obj["level"], 3)

        # Exposure stats: from arena.json open_idle_s ground truth (defender Lind gets 12.4s, NOT attacker Lind's 99.0s!)
        st_lind = metrics["exposure_stats"][("B", "Operator Lind")]
        self.assertEqual(st_lind["total_stand_s"], 12.4)
        self.assertEqual(st_lind["pre_contact_stand_s"], 8.2)
        self.assertEqual(st_lind["source"], "arena.defence.open_idle_s")
        self.assertEqual(st_lind["first_shot_t"], 10.2)
        self.assertEqual(st_lind["shots_fired"], 1)

        # Post assignment: range_cm extracted from post line
        self.assertIn(("B", "Operator Lind"), metrics["posts_assigned"])
        self.assertEqual(metrics["posts_assigned"][("B", "Operator Lind")]["range_cm"], 3200.0)

        st_cross = metrics["exposure_stats"][("B", "Operator Cross")]
        self.assertEqual(st_cross["total_stand_s"], 15.0)
        self.assertEqual(st_cross["pre_contact_stand_s"], 10.5)

        # Tactic decisions: defenders only
        self.assertIn(("B", "Operator Lind"), metrics["decisions_by_fighter"])
        lind_decisions = metrics["decisions_by_fighter"][("B", "Operator Lind")]
        self.assertEqual(len(lind_decisions), 2)
        self.assertEqual(lind_decisions[0]["decision"], "Ambush")
        self.assertEqual(lind_decisions[1]["decision"], "FallBack")
        # Attacker decision (Operator Vance -> Advance) must NOT be attributed to defenders!
        self.assertNotIn(("A", "Operator Vance"), metrics["decisions_by_fighter"])

        # Log fire events passed through to metrics with relative time t
        self.assertEqual(len(metrics["log_fire_events"]), 2)
        self.assertEqual(metrics["log_fire_events"][0]["action"], "holds his fire")
        self.assertIsNotNone(metrics["log_fire_events"][0]["t"])

        # Hypotheses H1 - H4
        hyp = metrics["hypotheses"]
        self.assertEqual(hyp["H1"]["verdict"], "DOES NOT HOLD")
        self.assertIn("roof/upper elevation", hyp["H1"]["evidence"])

        self.assertEqual(hyp["H2"]["verdict"], "HOLDS")
        self.assertIn("HS_05 for FallBack at t=3.5s", hyp["H2"]["evidence"])
        self.assertIn("45.0m", hyp["H2"]["evidence"])

        self.assertEqual(hyp["H3"]["verdict"], "HOLDS")
        self.assertIn("100% of logged defender decisions", hyp["H3"]["evidence"])

        self.assertEqual(hyp["H4"]["verdict"], "HOLDS")
        self.assertIn("holds his fire", hyp["H4"]["evidence"])

    def test_interleaved_fighters_with_same_id(self):
        fighter_id = "Operator Lind"
        track = {
            "fighters": [{"id": fighter_id, "side": side} for side in "AB"],
            "dt": 1,
            "states": [[[0, "stand", 100, "Hold"], [0, "crouch", 100, "Hold"]]],
            "events": [
                {"t": 0.1, "type": "shot", "side": "A", "from": fighter_id, "to": fighter_id},
                {"t": 0.2, "type": "tactic", "side": "B", "who": "Lind", "decision": "Hold"},
                {"t": 0.3, "type": "shot", "from": fighter_id, "to": fighter_id},
                {"t": 0.4, "type": "tactic", "side": "A", "who": fighter_id, "decision": "Advance"},
                {"t": 0.5, "type": "tactic", "who": fighter_id, "decision": "Ambush"},
                {"t": 0.6, "type": "shot", "side": "B", "from": fighter_id, "to": fighter_id},
            ],
        }
        log = prt.parse_log_for_round(None, 1, 0, lines_override=[
            "[PmcAI] team 1: spawned Operator Lind (look dark, ak) at V(X=0)",
            "[PmcAI] team 1: Operator Lind placed on a post V(X=0) (A_Post)",
            "[PmcAI] team 1: Operator Lind holds his fire (threat at 4500 cm, attention off 0.84)",
        ])
        for side, attacker, decision, shot_t, stand_s in (
            ("A", "B", "Advance", 0.1, 1.0), ("B", "A", "Hold", 0.6, 0.0),
        ):
            with self.subTest(side=side):
                rnd = {"attacker": attacker, "defence": {"open_idle_s": [{"id": fighter_id, "total_s": 99}]}}
                metrics = prt.compute_trace_metrics(rnd, {}, track, log)
                key = (side, fighter_id)
                self.assertEqual(set(metrics["decisions_by_fighter"]), {key})
                self.assertEqual([d["decision"] for d in metrics["decisions_by_fighter"][key]], [decision])
                stats = metrics["exposure_stats"][key]
                self.assertEqual((stats["shots_fired"], stats["first_shot_t"]), (1, shot_t))
                self.assertEqual(stats["total_stand_s"], stand_s)
                self.assertEqual(stats["first_shot_target"], f"{attacker}:{fighter_id}")
                self.assertEqual(set(metrics["posts_assigned"]), {key} if side == "A" else set())
                report = prt.format_markdown_report(metrics, Path("round.track.json"), 1)
                self.assertIn(f"**{side}:{fighter_id}** | {'ak' if side == 'A' else 'n/a'}", report)
                self.assertEqual(metrics["hypotheses"]["H4"]["verdict"], "HOLDS" if side == "A" else "DOES NOT HOLD")

    def test_markdown_report_formatting(self):
        arena_data = json.loads(self.arena_file.read_text(encoding="utf-8"))
        track_data = json.loads(self.track_file.read_text(encoding="utf-8"))
        rnd, meta, idx1, idx0 = prt.parse_arena_round(arena_data, 1)
        log_data = prt.parse_log_for_round(self.log_file, idx1, idx0)

        metrics = prt.compute_trace_metrics(rnd, meta, track_data, log_data)
        md = prt.format_markdown_report(metrics, self.arena_file, 1)

        self.assertIn("PMC Round Decision Trace: Round 1", md)
        self.assertIn("Storey Level: **3** | Height: **9.1 m**", md)
        self.assertIn("Operator Lind", md)
        self.assertIn("12.4 s", md)
        self.assertIn("arena.defence.open_idle_s", md)
        self.assertIn("Holds Fire", md)
        self.assertIn("Opens Fire", md)
        self.assertIn("H1 (Ground Objective Pull)", md)
        self.assertIn("H4 (Hold Fire / Attention Delay)", md)

    def test_cli_execution_on_coherent_fixture(self):
        script_path = TOOLS_DIR / "pmc_round_trace.py"
        res = subprocess.run(
            [sys.executable, str(script_path), str(self.arena_file), "--round", "1"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res.returncode, 0, f"CLI failed: {res.stderr}")
        self.assertIn("PMC Round Decision Trace: Round 1", res.stdout)
        self.assertIn("Storey Level: **3**", res.stdout)

        # Test clip notation r0
        res_r0 = subprocess.run(
            [sys.executable, str(script_path), str(self.arena_file), "--round", "r0"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res_r0.returncode, 0, f"CLI with r0 failed: {res_r0.stderr}")
        self.assertIn("PMC Round Decision Trace: Round r0", res_r0.stdout)

        # Test rejection of round 0 without --zero-based
        res_0 = subprocess.run(
            [sys.executable, str(script_path), str(self.arena_file), "--round", "0"],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(res_0.returncode, 0)
        self.assertIn("Round 0 is ambiguous", res_0.stderr)

        # Test direct track file CLI input
        res_track = subprocess.run(
            [sys.executable, str(script_path), str(self.track_file)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(res_track.returncode, 0, f"CLI with track file failed: {res_track.stderr}")
        self.assertIn("PMC Round Decision Trace: Round 1", res_track.stdout)


if __name__ == "__main__":
    unittest.main()
