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
        healthy_rounds = [
            {"defence": {"overwatch_kills": 1, "guard_share": 0.8}, "sides": {"A": {"kills": 2, "damage_dealt": 150}}},
            {"defence": {"overwatch_kills": 0, "guard_share": 0.6}, "sides": {"A": {"kills": 1, "damage_dealt": 80}}},
            {"defence": {"overwatch_kills": 2, "guard_share": 0.7}, "sides": {"A": {"kills": 3, "damage_dealt": 220}}},
        ]
        used = {"overwatch_kills", "guard_share", "kills", "damage_dealt"}
        passed, issues = check_metric_liveness(healthy_rounds, used)
        self.assertTrue(passed, f"Healthy probe should pass, issues: {issues}")

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


if __name__ == "__main__":
    unittest.main()
