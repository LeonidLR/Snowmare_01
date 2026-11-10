"""fix/pmc-arena-stalemate (2026-11-10): a round without contact pays nothing, the adapter reads `contact`, the new commander
genes (order commitment, push, RPG) are part of the genome, and the fight-flow stats of a track.
Run: python -m unittest discover -s Scripts/Trainer/tests
"""
import copy
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TRAINER = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(TRAINER))
sys.path.insert(0, TRAINER)

from nighttrainer import fitness, genome, stalemate_stats  # noqa: E402
from nighttrainer.adapter import ArenaAdapter  # noqa: E402

FIXTURES = os.path.join(TRAINER, "fixtures")
with open(os.path.join(TRAINER, "trainer_config.json"), encoding="utf-8") as _f:
    CONFIG = json.load(_f)


class NoContactTests(unittest.TestCase):
    cfg = CONFIG["fitness"]

    def test_no_contact_round_scores_flat(self):
        hiding = {"won": 0.0, "survivors": 3, "squad_size": 3, "hp_lost": 0, "hp_max": 300, "duration_s": 240,
                  "time_limit_s": 240, "objective_win": 1.0, "contact": False}
        self.assertEqual(fitness.round_score(hiding, self.cfg), self.cfg["no_contact_score"])
        self.assertEqual(fitness.round_score(dict(hiding, won=0.5), self.cfg), self.cfg["no_contact_score"])
        self.assertLessEqual(self.cfg["no_contact_score"], 0.0)  # no reward for hiding

    def test_capture_without_contact_still_a_win(self):
        r = {"won": 1.0, "survivors": 3, "squad_size": 3, "hp_lost": 0, "hp_max": 300, "duration_s": 60, "contact": False}
        self.assertGreater(fitness.round_score(r, self.cfg), 5.0)

    def test_adapter_reads_contact(self):
        doc = genome.load_json(os.path.join(FIXTURES, "sample_arena_objective_result.json"))
        doc = copy.deepcopy(doc)
        doc["rounds"][0]["contact"] = False
        rows = ArenaAdapter(CONFIG["arena"], "C:/Repo").parse_doc(doc)
        self.assertIs(rows[0]["contact"], False)
        self.assertNotIn("contact", rows[1])  # older results: no key, treated as contact


class GenomeTests(unittest.TestCase):
    def test_new_commander_genes_are_tunable(self):
        weights = genome.load_json(os.path.join(REPO, "Content", "Data", "AI", "pmc_commander.json"))
        genes = genome.flatten_numeric(weights)
        for key in ("/rpg_strike", "/rpg_gunner_share", "/order_min_hold_s", "/failed_order_cooldown_s", "/push"):
            self.assertIn(key, genes)
        with open(os.path.join(REPO, "Content", "Data", "AI", "pmc_commander.bounds.json"), encoding="utf-8") as f:
            bounds = json.load(f)["genes"]
        for key in ("/rpg_strike", "/rpg_gunner_share", "/order_min_hold_s", "/failed_order_cooldown_s", "/push"):
            self.assertIn(key, bounds)
            self.assertFalse(bounds[key].get("frozen", False), key)
        # the designer's rules stay fixed: push after 30 s quiet, re-path after 5 s without progress
        for key in ("/push_after_s", "/repath_after_s"):
            self.assertTrue(bounds[key].get("frozen"), key)
        self.assertGreaterEqual(bounds["/push"]["min"], 1.0)  # evolution can't switch the push off


class StatsTests(unittest.TestCase):
    def test_round_stats_of_a_real_track(self):
        with open(os.path.join(FIXTURES, "real_arena_round.track.json"), encoding="utf-8") as f:
            track = json.load(f)
        s = stalemate_stats.round_stats(track)
        self.assertEqual(s["shots"], 172)
        self.assertTrue(s["contact"])
        self.assertAlmostEqual(s["shots_per_min"], 172 / (23.5 / 60.0), places=1)
        self.assertGreater(s["orders_per_fighter_min"], 0.0)
        self.assertLessEqual(s["longest_quiet_s"], 23.5)

    def test_quiet_spell_and_attacker_distance(self):
        track = {"dt": 1.0, "units": "m", "duration_s": 100,
                 "fighters": [{"id": "a", "side": "A"}, {"id": "b", "side": "B"}],
                 "frames": [[[10 - min(t, 10), 0], [20, 0]] for t in range(100)],
                 "objective": {"x": 0, "y": 0, "radius": 6, "attacker": "A"},
                 "events": [{"t": 5, "type": "shot"}, {"t": 10, "type": "shot"}, {"t": 70, "type": "shot"},
                            {"t": 1, "type": "tactic", "who": "a", "side": "A", "decision": "Flank"},
                            {"t": 1.5, "type": "tactic", "who": "a", "side": "A", "decision": "Flank"}]}
        s = stalemate_stats.round_stats(track)
        self.assertEqual(s["first_contact_s"], 5.0)
        self.assertEqual(s["longest_quiet_s"], 60.0)
        self.assertEqual(s["quiet_tail_s"], 30.0)
        self.assertTrue(s["attacker_reached_point"])
        self.assertEqual(s["quick_redecisions"], 1)
        self.assertEqual(s["quick_redecisions_same_order"], 1)
        self.assertEqual(stalemate_stats.round_stats(dict(track, events=[]))["contact"], False)


if __name__ == "__main__":
    unittest.main()
