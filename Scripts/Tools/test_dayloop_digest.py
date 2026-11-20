"""Unit tests for dayloop_digest and paired_ab tools (CodexTactics Task 093)."""
from __future__ import annotations

import json
import math
import shutil
import tempfile
import unittest
from pathlib import Path

from Scripts.Tools.dayloop_digest import load_folder, med, row
from Scripts.Tools.paired_ab import (
    compare_paired_runs,
    exact_mcnemar_p,
    format_report,
    format_seed_ranges,
    parse_seed_spec,
    percentile,
)


def make_arena_round(
    seed: int,
    scenario: str = "street",
    winner: str = "A",
    end_reason: str = "elimination",
    def_kills: int = 3,
    att_survivors: int = 0,
    att_kills: int = 0,
    def_survivors: int = 2,
    on_pos: float = 0.67,
    crossfire: float = 35.0,
    idle_s: float = 1.5,
) -> dict:
    """Helper creating a round dict compliant with schema codex.pmc_arena/1."""
    return {
        "rounds": [
            {
                "seed": seed,
                "scenario": scenario,
                "winner": winner,
                "end_reason": end_reason,
                "duration_s": 45.0,
                "sides": {
                    "A": {
                        "role": "defender",
                        "squad_size": 3,
                        "survivors": def_survivors,
                        "kills": def_kills,
                        "shots": 20,
                    },
                    "B": {
                        "role": "attacker",
                        "squad_size": 3,
                        "survivors": att_survivors,
                        "kills": att_kills,
                        "shots": 15,
                    },
                },
                "defence": {
                    "on_position_before_contact": on_pos,
                    "crossfire_deg": crossfire,
                    "first_shot_gap_s": 1.2,
                    "max_exposed_run_m": 4.0,
                    "spread_m": 10.0,
                    "open_idle_s": [
                        {"side": "A", "before_contact_s": idle_s},
                    ],
                },
            }
        ]
    }


class TestPairedABDigest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.dir_path = Path(self.temp_dir)
        self.folder_a = self.dir_path / "folder_a"
        self.folder_b = self.dir_path / "folder_b"
        self.folder_a.mkdir()
        self.folder_b.mkdir()

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_mcnemar_exact_binomial(self):
        # n = 0 -> p = 1.0
        self.assertEqual(exact_mcnemar_p(0, 0), 1.0)
        # symmetric ties -> p = 1.0
        self.assertEqual(exact_mcnemar_p(1, 1), 1.0)
        self.assertEqual(exact_mcnemar_p(5, 5), 1.0)
        # b=0, c=5: 2 * (1/32) = 0.0625
        self.assertAlmostEqual(exact_mcnemar_p(0, 5), 0.0625, places=4)
        # b=6, c=35: exact binomial p approx 4.873598e-06
        p = exact_mcnemar_p(6, 35)
        self.assertTrue(p < 1e-5)
        self.assertAlmostEqual(p, 4.873598e-06, places=10)

    def test_seed_spec_parsing_and_formatting(self):
        parsed = parse_seed_spec("1-5,7, 9-12, 15")
        self.assertEqual(parsed, {1, 2, 3, 4, 5, 7, 9, 10, 11, 12, 15})
        formatted = format_seed_ranges(parsed)
        self.assertEqual(formatted, "1-5, 7, 9-12, 15")
        self.assertEqual(format_seed_ranges([]), "(empty)")

    def test_identical_folders(self):
        """1. Identical folders -> 0 discordant, p = 1, median diff 0."""
        for seed in range(1, 6):
            doc = make_arena_round(
                seed=seed,
                scenario="street",
                winner="A" if seed in (1, 3, 5) else "B",
                end_reason="elimination" if seed in (1, 3, 5) else "objective",
                def_kills=3 if seed in (1, 3, 5) else 1,
                att_survivors=0 if seed in (1, 3, 5) else 2,
                on_pos=0.67,
                crossfire=30.0,
                idle_s=1.8,
            )
            text = json.dumps(doc)
            (self.folder_a / f"seed{seed}.json").write_text(text, encoding="utf-8")
            (self.folder_b / f"seed{seed}.json").write_text(text, encoding="utf-8")

        rows_a = load_folder(self.folder_a)
        rows_b = load_folder(self.folder_b)
        self.assertEqual(len(rows_a), 5)
        self.assertEqual(len(rows_b), 5)

        res = compare_paired_runs(rows_a, rows_b, rng_seed=42, n_resamples=500)
        sc = res["scenarios"]["street"]
        part = sc["partitions"]["all"]

        # No warnings
        self.assertIsNone(sc["seed_warning"])
        self.assertEqual(len(res["warnings"]), 0)

        # 0 discordant, p = 1
        self.assertEqual(part["discordant"]["a_only"], 0)
        self.assertEqual(part["discordant"]["b_only"], 0)
        self.assertEqual(part["discordant"]["p"], 1.0)

        # Win rates identical
        self.assertEqual(part["def_wins"]["a"], 3)
        self.assertEqual(part["def_wins"]["b"], 3)
        self.assertEqual(part["def_wins"]["diff_rate"], 0.0)

        # Differences in medians and CIs are exactly 0.0
        med_diff = part["medians"]["on_pos"]["diff"]
        self.assertEqual(med_diff, 0.0)
        self.assertEqual(part["medians"]["on_pos"]["diff_ci"], (0.0, 0.0))
        self.assertEqual(part["medians"]["crossfire"]["diff_ci"], (0.0, 0.0))

    def test_handmade_5_seed_pair_exact_counts(self):
        """2. Hand-made 5-seed pair with known wins -> exact counts."""
        # Seed 1: A wins (elim), B wins (elim) -> concordant win
        # Seed 2: A wins (elim, kills 3), B loses (objective, kills 1) -> A-only win
        # Seed 3: A loses (objective, kills 1), B wins (elim, kills 3) -> B-only win
        # Seed 4: A loses (time_limit, kills 0), B loses (time_limit, kills 0) -> concordant loss
        # Seed 5: A wins (elim, kills 3), B loses (elim, att wins, kills 0) -> A-only win
        scenarios_data_a = [
            (1, "A", "elimination", 3, 0, 0.67, 30.0, 1.5),
            (2, "A", "elimination", 3, 0, 0.67, 30.0, 1.5),
            (3, "B", "objective", 1, 2, 0.33, 20.0, 3.0),
            (4, "draw", "time_limit", 0, 3, 0.00, 15.0, 4.0),
            (5, "A", "elimination", 3, 0, 1.00, 45.0, 1.0),
        ]
        scenarios_data_b = [
            (1, "A", "elimination", 3, 0, 0.67, 35.0, 1.2),
            (2, "B", "objective", 1, 2, 0.33, 20.0, 2.5),
            (3, "A", "elimination", 3, 0, 0.67, 40.0, 1.4),
            (4, "draw", "time_limit", 0, 3, 0.00, 15.0, 3.5),
            (5, "B", "elimination", 0, 3, 0.33, 25.0, 2.2),
        ]

        for s, w, end, k, surv, on_pos, cf, idle in scenarios_data_a:
            doc = make_arena_round(
                seed=s,
                winner=w,
                end_reason=end,
                def_kills=k,
                att_survivors=surv,
                on_pos=on_pos,
                crossfire=cf,
                idle_s=idle,
            )
            (self.folder_a / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")

        for s, w, end, k, surv, on_pos, cf, idle in scenarios_data_b:
            doc = make_arena_round(
                seed=s,
                winner=w,
                end_reason=end,
                def_kills=k,
                att_survivors=surv,
                on_pos=on_pos,
                crossfire=cf,
                idle_s=idle,
            )
            (self.folder_b / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")

        rows_a = load_folder(self.folder_a)
        rows_b = load_folder(self.folder_b)

        res = compare_paired_runs(rows_a, rows_b, rng_seed=42, n_resamples=500)
        part = res["scenarios"]["street"]["partitions"]["all"]

        # Exact counts
        self.assertEqual(part["n"], 5)
        # A wins: seeds 1, 2, 5 (3 of 5)
        self.assertEqual(part["def_wins"]["a"], 3)
        self.assertEqual(part["def_wins"]["a_rate"], 0.6)
        # B wins: seeds 1, 3 (2 of 5)
        self.assertEqual(part["def_wins"]["b"], 2)
        self.assertEqual(part["def_wins"]["b_rate"], 0.4)

        # Discordant counts: A-only = 2, B-only = 1
        self.assertEqual(part["discordant"]["a_only"], 2)
        self.assertEqual(part["discordant"]["b_only"], 1)
        self.assertEqual(part["discordant"]["both"], 1)
        self.assertEqual(part["discordant"]["neither"], 1)
        # McNemar p: n=3, k=1 -> 2 * (1 + 3) / 8 = 1.0
        self.assertEqual(part["discordant"]["p"], 1.0)

        # Attacker captures: A has 1 (seed 3), B has 1 (seed 2)
        self.assertEqual(part["atk_captures"]["a"], 1)
        self.assertEqual(part["atk_captures"]["b"], 1)

        # Attackers killed per round:
        # A: (3 + 3 + 1 + 0 + 3) / 5 = 10 / 5 = 2.0
        # B: (3 + 1 + 3 + 0 + 0) / 5 = 7 / 5 = 1.4
        self.assertAlmostEqual(part["att_killed_per_round"]["a_mean"], 2.0)
        self.assertAlmostEqual(part["att_killed_per_round"]["b_mean"], 1.4)
        self.assertAlmostEqual(part["att_killed_per_round"]["diff"], -0.6)

        # Share of rounds with >= 2 attackers down:
        # A: seeds 1 (3), 2 (3), 5 (3) -> 3 of 5 = 0.6
        # B: seeds 1 (3), 3 (3) -> 2 of 5 = 0.4
        self.assertEqual(part["att_down_ge2"]["a_count"], 3)
        self.assertEqual(part["att_down_ge2"]["a_share"], 0.6)
        self.assertEqual(part["att_down_ge2"]["b_count"], 2)
        self.assertEqual(part["att_down_ge2"]["b_share"], 0.4)
        self.assertAlmostEqual(part["att_down_ge2"]["diff_share"], -0.2)

    def test_different_seed_sets_warning(self):
        """3. Different seed sets -> loud WARNING, pairs only common seeds."""
        # Folder A: seeds 1, 2, 3
        for s in [1, 2, 3]:
            doc = make_arena_round(seed=s)
            (self.folder_a / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")

        # Folder B: seeds 2, 3, 4
        for s in [2, 3, 4]:
            doc = make_arena_round(seed=s)
            (self.folder_b / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")

        rows_a = load_folder(self.folder_a)
        rows_b = load_folder(self.folder_b)

        res = compare_paired_runs(rows_a, rows_b)
        sc = res["scenarios"]["street"]

        # Warning generated
        self.assertIsNotNone(sc["seed_warning"])
        self.assertTrue("WARNING: Seed sets differ" in sc["seed_warning"])
        self.assertTrue("Missing in B (1): 1" in sc["seed_warning"])
        self.assertTrue("Missing in A (1): 4" in sc["seed_warning"])

        # Paired runs only evaluated on common seeds: {2, 3}
        self.assertEqual(sc["common_seeds"], [2, 3])
        self.assertEqual(sc["partitions"]["all"]["n"], 2)

        # Full report includes warning block
        report = format_report(res)
        self.assertIn("WARNING: Seed sets differ", report)

    def test_bootstrap_deterministic(self):
        """4. Bootstrap with fixed RNG seed is deterministic."""
        for s in range(1, 11):
            doc_a = make_arena_round(seed=s, on_pos=0.1 * s, crossfire=20.0 + s * 2, idle_s=1.0 + s * 0.3)
            doc_b = make_arena_round(seed=s, on_pos=0.15 * s, crossfire=25.0 + s * 1.5, idle_s=0.8 + s * 0.2)
            (self.folder_a / f"seed{s}.json").write_text(json.dumps(doc_a), encoding="utf-8")
            (self.folder_b / f"seed{s}.json").write_text(json.dumps(doc_b), encoding="utf-8")

        rows_a = load_folder(self.folder_a)
        rows_b = load_folder(self.folder_b)

        res1 = compare_paired_runs(rows_a, rows_b, rng_seed=1337, n_resamples=500)
        res2 = compare_paired_runs(rows_a, rows_b, rng_seed=1337, n_resamples=500)

        med1 = res1["scenarios"]["street"]["partitions"]["all"]["medians"]
        med2 = res2["scenarios"]["street"]["partitions"]["all"]["medians"]

        for k in ["on_pos", "idle_max", "crossfire"]:
            self.assertEqual(med1[k]["a"], med2[k]["a"])
            self.assertEqual(med1[k]["b"], med2[k]["b"])
            self.assertEqual(med1[k]["diff"], med2[k]["diff"])
            self.assertEqual(med1[k]["a_ci"], med2[k]["a_ci"])
            self.assertEqual(med1[k]["b_ci"], med2[k]["b_ci"])
            self.assertEqual(med1[k]["diff_ci"], med2[k]["diff_ci"])

    def test_tune_held_out_split(self):
        """5. Optional --tune-seeds split correctly partitions tune vs held-out."""
        for s in range(1, 6):
            doc = make_arena_round(
                seed=s,
                winner="A" if s in (1, 2) else "B",
                on_pos=0.8 if s in (1, 2) else 0.3,
            )
            (self.folder_a / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")
            (self.folder_b / f"seed{s}.json").write_text(json.dumps(doc), encoding="utf-8")

        rows_a = load_folder(self.folder_a)
        rows_b = load_folder(self.folder_b)

        tune_seeds = parse_seed_spec("1-2")
        res = compare_paired_runs(rows_a, rows_b, tune_seeds=tune_seeds)
        parts = res["scenarios"]["street"]["partitions"]

        self.assertIn("tune", parts)
        self.assertIn("held_out", parts)
        self.assertIn("all", parts)

        self.assertEqual(parts["tune"]["n"], 2)
        self.assertEqual(parts["tune"]["def_wins"]["a"], 2)
        self.assertEqual(parts["tune"]["def_wins"]["a_rate"], 1.0)

        self.assertEqual(parts["held_out"]["n"], 3)
        self.assertEqual(parts["held_out"]["def_wins"]["a"], 0)
        self.assertEqual(parts["held_out"]["def_wins"]["a_rate"], 0.0)

        self.assertEqual(parts["all"]["n"], 5)
        self.assertEqual(parts["all"]["def_wins"]["a"], 2)


if __name__ == "__main__":
    unittest.main()
