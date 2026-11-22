"""Unit tests for training gate and scenario regression (smart enemies T-10 / Task 091).

Verifies G1-G5 training gate checks:
- Metric liveness refuses runs with dead metrics (2026-11-19 fixture with overwatch_kills=0 and guard_share=None).
- Baseline sufficiency requires >= 30 rounds per scenario (F7/G3).
- Smoke health validates foundation/scenario smoke statuses.
- Scenario regression runner runs day and night modes and compares with previous runs.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
TRAINER_DIR = REPO_ROOT / "Scripts" / "Trainer"
TOOLS_DIR = REPO_ROOT / "Scripts" / "Tools"
if str(TRAINER_DIR) not in sys.path:
    sys.path.insert(0, str(TRAINER_DIR))
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from nighttrainer.gate import (
    check_baseline_sufficiency,
    check_metric_liveness,
    check_smoke_health,
    evaluate_training_gate,
    extract_used_metrics,
)
import scenario_regression as sr


class TestTrainingGate(unittest.TestCase):
    def setUp(self):
        self.fixtures_dir = TRAINER_DIR / "fixtures"
        self.dead_metrics_fixture = self.fixtures_dir / "gate_probe_20261119_dead_metrics.json"
        self.config_path = TRAINER_DIR / "trainer_config.json"

        with open(self.config_path, "r", encoding="utf-8-sig") as f:
            self.config = json.load(f)

    def test_fixture_exists(self):
        self.assertTrue(self.dead_metrics_fixture.is_file(), f"Missing fixture {self.dead_metrics_fixture}")

    def test_extract_used_metrics(self):
        metrics = extract_used_metrics(self.config)
        self.assertIn("kills", metrics)
        self.assertIn("damage_dealt", metrics)
        self.assertIn("time_in_contact_s", metrics)
        self.assertIn("ambush_success", metrics)

    def test_gate_refuses_dead_metrics_20261119(self):
        """The 2026-11-19 probe fixture with overwatch_kills 0 and guard_share None must be refused."""
        with open(self.dead_metrics_fixture, "r", encoding="utf-8") as f:
            probe_data = json.load(f)

        rounds = probe_data.get("rounds", [])
        self.assertEqual(len(rounds), 6, "Probe must contain exactly 6 rounds")

        # Test metric liveness directly
        used_metrics = {"overwatch_kills", "guard_share", "kills", "damage_dealt"}
        passed, dead_metrics = check_metric_liveness(rounds, used_metrics)

        self.assertFalse(passed, "Metric liveness check must fail on 2026-11-19 dead metrics")
        dead_str = " ".join(dead_metrics)
        self.assertIn("overwatch_kills", dead_str)
        self.assertIn("guard_share", dead_str)

        # Full gate evaluation
        gate_res = evaluate_training_gate(self.config, probe_data=probe_data, baseline_data=None, sweep_data=None)
        self.assertFalse(gate_res["passed"])
        self.assertFalse(gate_res["checks"]["G2_metric_liveness"]["passed"])

    def test_gate_passes_healthy_probe(self):
        """Healthy probe matching codex.pmc_arena_behaviour/1 schema must pass all liveness checks."""
        used_metrics = extract_used_metrics(self.config)
        healthy_rounds = []
        for i in range(6):
            # Vary values across rounds so variance > 0
            v = float(i + 1)
            behaviour_metrics = {
                "time_in_contact_s": {"value": 25.0 + v * 3.0, "unit": "s"},
                "flanks_completed": {"value": 1.0 + (i % 2), "unit": "count"},
                "moves": {"value": 4.0 + v, "unit": "count"},
                "ambush_success": {"value": 0.15 + (v * 0.05), "unit": "ratio"},
                "flanks_succeeded": {"value": 1.0 + (i % 2), "unit": "count"},
                "suppression_s": {"value": 5.0 + v * 2.0, "unit": "s"},
                "ambushes": {"value": 1.0 + (i % 2), "unit": "count"},
                "retreats_survived": {"value": float(i % 2), "unit": "count"},
                "heals": {"value": float(i % 3), "unit": "count"},
                "damage_from_concealment": {"value": 20.0 + v * 5.0, "unit": "hp"},
                "first_strike": {"value": float(i % 2), "unit": "flag"},
                "objective_win": {"value": float((i + 1) % 2), "unit": "flag"},
                "strikes_unobserved": {"value": float(i % 2), "unit": "count"},
                "friendly_fire": {"value": 0.0, "unit": "count"},
                "stuck_events": {"value": 0.0, "unit": "count"},
            }
            round_obj = {
                "index": i + 1,
                "scenario": "tactical",
                "duration_s": 80.0 + v * 5.0,
                "defence": {
                    "overwatch_kills": 1 if i % 2 == 0 else 2,
                    "guard_share": 0.6 + (v * 0.03),
                    "spread_m": 20.0 + v,
                    "shared_cover_pairs": i,
                    "objective_share": 0.5 + (v * 0.05),
                },
                "sides": {
                    "A": {
                        "role": "attacker",
                        "kills": 1 + (i % 3),
                        "damage_dealt": 100.0 + v * 20.0,
                        "hp_lost": 40.0 + v * 10.0,
                        "survivors": 2 + (i % 2),
                        "behaviour": {
                            "schema": "codex.pmc_arena_behaviour/1",
                            "metrics": behaviour_metrics,
                        },
                    },
                    "B": {"role": "defender", "kills": 1, "damage_dealt": 50.0, "hp_lost": 150.0, "survivors": 1},
                },
            }
            healthy_rounds.append(round_obj)

        probe_data = {"schema": "codex.pmc_arena/1", "rounds": healthy_rounds}
        baseline_data = {
            "rounds": [{"scenario": "crossroads"} for _ in range(35)] + [{"scenario": "alley"} for _ in range(35)]
        }
        sweep_data = {
            "results": [
                {"Name": "PmcCrossroadsSmoke", "State": "PASS"},
                {"Name": "PmcAlleySmoke", "State": "PASS"},
                {"Name": "PmcRoofDefenceSmoke", "State": "PASS"},
                {"Name": "PmcStalemateSmoke", "State": "KNOWN-FAIL"},
            ]
        }

        gate_res = evaluate_training_gate(
            self.config, probe_data=probe_data, baseline_data=baseline_data, sweep_data=sweep_data
        )
        self.assertTrue(gate_res["passed"], f"Healthy probe must pass all gate checks: {gate_res['reasons']}")
        self.assertTrue(gate_res["checks"]["G2_metric_liveness"]["passed"])
        self.assertTrue(gate_res["checks"]["G1_smoke_health"]["passed"])
        self.assertTrue(gate_res["checks"]["G3_baseline_sufficiency"]["passed"])

    def test_penalties_zero_not_flagged_as_dead(self):
        """Zero values for penalty metrics (friendly_fire, stuck_events) must not be flagged as dead."""
        used_metrics = {"friendly_fire", "stuck_events", "kills"}
        penalty_metrics = {"friendly_fire", "stuck_events"}
        rounds = [
            {"sides": {"A": {"kills": 1, "behaviour": {"metrics": {"friendly_fire": {"value": 0.0}, "stuck_events": {"value": 0.0}}}}}},
            {"sides": {"A": {"kills": 2, "behaviour": {"metrics": {"friendly_fire": {"value": 0.0}, "stuck_events": {"value": 0.0}}}}}},
            {"sides": {"A": {"kills": 3, "behaviour": {"metrics": {"friendly_fire": {"value": 0.0}, "stuck_events": {"value": 0.0}}}}}},
        ]
        passed, dead = check_metric_liveness(rounds, used_metrics, penalty_metrics=penalty_metrics)
        self.assertTrue(passed, f"Zero penalties must be accepted, got: {dead}")

    def test_dead_behaviour_metric_detected(self):
        """If a behaviour metric is constant 0.0, it must be flagged as dead."""
        used_metrics = {"time_in_contact_s", "kills"}
        rounds = [
            {"sides": {"A": {"kills": 1, "behaviour": {"metrics": {"time_in_contact_s": {"value": 0.0}}}}}},
            {"sides": {"A": {"kills": 2, "behaviour": {"metrics": {"time_in_contact_s": {"value": 0.0}}}}}},
            {"sides": {"A": {"kills": 3, "behaviour": {"metrics": {"time_in_contact_s": {"value": 0.0}}}}}},
        ]
        passed, dead = check_metric_liveness(rounds, used_metrics)
        self.assertFalse(passed)
        self.assertTrue(any("time_in_contact_s" in d for d in dead))

    def test_no_fake_proxies(self):
        """gate.py must not fall back to duration_s when time_in_contact_s is missing."""
        from nighttrainer.gate import get_metric_value_from_round
        round_without_contact = {
            "duration_s": 120.0,
            "sides": {"A": {"kills": 2}},
        }
        val = get_metric_value_from_round(round_without_contact, "time_in_contact_s")
        self.assertIsNone(val, f"time_in_contact_s must be None when absent, not duration_s proxy ({val})")

    def test_smoke_health_known_fail_variants(self):
        """Both KNOWN_FAIL and KNOWN-FAIL (written by smoke_parallel.ps1) must be accepted."""
        sweep_hyphen = {
            "results": [
                {"Name": "PmcCrossroadsSmoke", "State": "PASS"},
                {"Name": "PmcAlleySmoke", "State": "PASS"},
                {"Name": "PmcRoofDefenceSmoke", "State": "PASS"},
                {"Name": "PmcStalemateSmoke", "State": "KNOWN-FAIL"},
            ]
        }
        passed_hyphen, _ = check_smoke_health(sweep_hyphen)
        self.assertTrue(passed_hyphen, "KNOWN-FAIL must be accepted in smoke health")

        sweep_underscore = {
            "results": [
                {"Name": "PmcCrossroadsSmoke", "State": "PASS"},
                {"Name": "PmcAlleySmoke", "State": "PASS"},
                {"Name": "PmcRoofDefenceSmoke", "State": "PASS"},
                {"Name": "PmcStalemateSmoke", "State": "KNOWN_FAIL"},
            ]
        }
        passed_underscore, _ = check_smoke_health(sweep_underscore)
        self.assertTrue(passed_underscore, "KNOWN_FAIL must be accepted in smoke health")

    def test_baseline_sufficiency(self):
        # 1. Missing baseline
        pass_none, issues_none = check_baseline_sufficiency(None)
        self.assertFalse(pass_none)

        # 2. Insufficient rounds (< 30)
        insufficient = {
            "rounds": [{"scenario": "crossroads"} for _ in range(12)] + [{"scenario": "alley"} for _ in range(40)]
        }
        pass_insuf, issues_insuf = check_baseline_sufficiency(insufficient, min_rounds_per_scenario=30)
        self.assertFalse(pass_insuf)
        self.assertTrue(any("crossroads" in i for i in issues_insuf))

        # 3. Sufficient rounds (>= 30 per scenario)
        sufficient = {
            "rounds": [{"scenario": "crossroads"} for _ in range(35)] + [{"scenario": "alley"} for _ in range(30)]
        }
        pass_suf, issues_suf = check_baseline_sufficiency(sufficient, min_rounds_per_scenario=30)
        self.assertTrue(pass_suf, f"Sufficient baseline should pass, issues: {issues_suf}")

    def test_smoke_health(self):
        # Sweep with failure
        sweep_fail = {
            "results": [
                {"Name": "PmcCrossroadsSmoke", "State": "PASS"},
                {"Name": "PmcAlleySmoke", "State": "FAIL"},
                {"Name": "PmcRoofDefenceSmoke", "State": "PASS"},
                {"Name": "PmcStalemateSmoke", "State": "PASS"},
            ]
        }
        pass_fail, issues_fail = check_smoke_health(sweep_fail)
        self.assertFalse(pass_fail)
        self.assertTrue(any("PmcAlleySmoke" in i for i in issues_fail))

        # Sweep all green
        sweep_pass = {
            "results": [
                {"Name": "PmcCrossroadsSmoke", "State": "PASS"},
                {"Name": "PmcAlleySmoke", "State": "PASS"},
                {"Name": "PmcRoofDefenceSmoke", "State": "PASS"},
                {"Name": "PmcStalemateSmoke", "State": "PASS"},
            ]
        }
        pass_ok, issues_ok = check_smoke_health(sweep_pass)
        self.assertTrue(pass_ok, f"All-green sweep should pass, issues: {issues_ok}")

    def test_night_trainer_check_gate_cli(self):
        """CLI invocation of night_trainer.py --check-gate exits 1 when gate is blocked."""
        trainer_script = TRAINER_DIR / "night_trainer.py"
        cmd = [
            sys.executable,
            str(trainer_script),
            "--check-gate",
            "--config", str(self.config_path),
            "--gate-probe", str(self.dead_metrics_fixture),
        ]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(proc.returncode, 1, "night_trainer.py --check-gate must exit 1 when dead metrics exist")
        self.assertIn("TRAINING GATE: BLOCKED", proc.stdout)

    def test_stale_gate_pass_deleted_on_blocked(self):
        """When gate is blocked, any stale gate_pass.json must be deleted to prevent false starts."""
        import tempfile
        import shutil
        from night_trainer import _check_gate

        temp_dir = tempfile.mkdtemp(prefix="test_gate_hygiene_")
        try:
            saved_trainer = Path(temp_dir) / "Saved" / "Trainer"
            saved_trainer.mkdir(parents=True, exist_ok=True)
            stale_pass = saved_trainer / "gate_pass.json"
            stale_pass.write_text(json.dumps({"passed": True, "comment": "stale"}), encoding="utf-8")
            self.assertTrue(stale_pass.is_file())

            class DummyArgs:
                gate_probe = str(self.dead_metrics_fixture)
                gate_baseline = None
                gate_sweep = None
                gate_out = None
                repo_root = temp_dir

            code = _check_gate(DummyArgs(), self.config)
            self.assertEqual(code, 1, "_check_gate must return 1 on blocked gate")
            self.assertFalse(stale_pass.is_file(), "stale gate_pass.json must be deleted when gate is blocked")
            blocked_file = saved_trainer / "gate_blocked.json"
            self.assertTrue(blocked_file.is_file(), "gate_blocked.json must be written on blocked gate")
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)


class TestScenarioRegression(unittest.TestCase):
    def test_seed_spec_parsing(self):
        self.assertEqual(sr.parse_seed_spec("5"), [1, 2, 3, 4, 5])
        self.assertEqual(sr.parse_seed_spec("1-4, 8, 10-12"), [1, 2, 3, 4, 8, 10, 11, 12])
        self.assertEqual(sr.parse_seed_spec("15"), list(range(1, 16)))

    def test_mock_scenario_regression_day_digest(self):
        # Run 10 seeds in mock mode, seed 3 and 7 failing
        res = sr.run_scenario_regression("crossroads", [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], mock=True, mock_fail_seeds={3, 7})
        self.assertEqual(res["total_seeds"], 10)
        self.assertEqual(res["passed_count"], 8)
        self.assertEqual(res["failed_count"], 2)
        self.assertEqual(res["pass_rate"], 80.0)
        self.assertEqual(res["failed_seeds"], [3, 7])

        digest = sr.format_day_digest(res)
        self.assertIn("# Day Loop Digest: crossroads (PmcCrossroadsSmoke)", digest)
        self.assertIn("8 / 10 (80.0%)", digest)
        self.assertIn("3, 7", digest)
        self.assertIn("Decision Trace (Failed Seed 3):", digest)

    def test_regression_comparison(self):
        today = {"results": [{"seed": 1, "passed": True}, {"seed": 2, "passed": False}, {"seed": 3, "passed": True}]}
        yesterday = {"seeds": {1: True, 2: True, 3: False}}
        cmp = sr.compare_with_yesterday(today, yesterday)
        self.assertEqual(cmp["regressions"], [2])
        self.assertEqual(cmp["fixes"], [3])
        self.assertEqual(cmp["stable_seeds"], [1])

    def test_regression_comparison_with_results_format(self):
        """Today and yesterday both having 'results' lists as written by scenario_regression.py."""
        today = {"results": [{"seed": 1, "passed": True}, {"seed": 2, "passed": False}, {"seed": 3, "passed": True}]}
        yesterday = {"results": [{"seed": 1, "passed": True}, {"seed": 2, "passed": True}, {"seed": 3, "passed": False}]}
        cmp = sr.compare_with_yesterday(today, yesterday)
        self.assertEqual(cmp["regressions"], [2])
        self.assertEqual(cmp["fixes"], [3])
        self.assertEqual(cmp["stable_seeds"], [1])

    def test_regression_comparison_multi_scenario(self):
        """Multi-scenario nightly report comparison."""
        today = {
            "scenarios": [
                {"scenario": "crossroads", "results": [{"seed": 1, "passed": False}, {"seed": 2, "passed": True}]},
                {"scenario": "alley", "results": [{"seed": 1, "passed": True}]},
            ]
        }
        yesterday = {
            "scenarios": [
                {"scenario": "crossroads", "results": [{"seed": 1, "passed": True}, {"seed": 2, "passed": True}]},
                {"scenario": "alley", "results": [{"seed": 1, "passed": True}]},
            ]
        }
        cmp = sr.compare_with_yesterday(today, yesterday, scenario="crossroads")
        self.assertEqual(cmp["regressions"], [1])
        self.assertEqual(cmp["stable_seeds"], [2])


if __name__ == "__main__":
    unittest.main()
