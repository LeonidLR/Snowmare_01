"""Task #53: objective rounds (roles, ambush stats, objective win) in the adapter + fitness, and the single-instance lock.
Run: python -m unittest discover -s Scripts/Trainer/tests
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TRAINER = os.path.dirname(HERE)
sys.path.insert(0, TRAINER)

from nighttrainer import fitness, genome  # noqa: E402
from nighttrainer.adapter import ArenaAdapter  # noqa: E402
from nighttrainer.runner import InstanceLock  # noqa: E402

FIXTURES = os.path.join(TRAINER, "fixtures")
with open(os.path.join(TRAINER, "trainer_config.json"), encoding="utf-8") as _f:
    CONFIG = json.load(_f)


class ObjectiveAdapterTests(unittest.TestCase):
    def setUp(self):
        self.adapter = ArenaAdapter(CONFIG["arena"], "C:/Repo")
        self.doc = genome.load_json(os.path.join(FIXTURES, "sample_arena_objective_result.json"))

    def test_roles_stats_and_objective_win(self):
        rounds = self.adapter.parse_doc(self.doc)
        self.assertEqual([r["won"] for r in rounds], [1.0, 1.0, 0.0])
        self.assertEqual([r["role"] for r in rounds], ["attacker", "defender", "attacker"])
        # captured as the attacker, held to the limit as the defender, wiped out: 1, 1, 0
        self.assertEqual([r["objective_win"] for r in rounds], [1.0, 1.0, 0.0])
        self.assertEqual(rounds[1]["damage_from_concealment"], 130.0)
        self.assertEqual(rounds[1]["first_strike"], 1.0)
        self.assertEqual(rounds[0]["objective_hold_s"], 15.1)
        self.assertEqual(rounds[2]["median_engagement_m"], 12.0)
        self.assertEqual([r["scenario"] for r in rounds], ["chokepoint", "roofs", "blizzard"])
        self.assertEqual([r["strikes_unobserved"] for r in rounds], [0.0, 2.0, 0.0])
        agg = fitness.aggregate(rounds, CONFIG["fitness"])
        self.assertAlmostEqual(agg["objective_win"], 2.0 / 3.0, places=3)
        self.assertIn("damage_from_concealment", agg)

    def test_config_wires_the_objective(self):
        args = CONFIG["arena"]["arg_template"]
        self.assertIn("Objective=1", args)
        self.assertIn("Scenario=random", args)
        fields = CONFIG["arena"]["result"]["fields"]
        for key in ("role", "damage_from_concealment", "first_strike", "objective_hold_s", "median_engagement_m",
                    "strikes_unobserved"):
            self.assertEqual(fields[key], key)
        cmd = self.adapter.console_command({"rounds": 4, "seed": 7, "opponent": "champion", "weights_a": "C:/Repo/a.json",
                                            "weights_b": "C:/Repo/b.json"}, "C:/Repo/o.json")
        self.assertIn("Spawns=Random Objective=1 Scenario=random", cmd)


class ObjectiveFitnessTests(unittest.TestCase):
    cfg = CONFIG["fitness"]

    def r(self, **kw):
        base = {"won": 1.0, "survivors": 2, "squad_size": 3, "hp_lost": 100, "hp_max": 300, "duration_s": 150,
                "opp_hp_lost": 200, "kills": 1, "opp_kills": 1}
        base.update(kw)
        return base

    def test_ambush_from_concealment_and_first_strike_pay(self):
        plain = fitness.round_score(self.r(), self.cfg)
        ambush = fitness.round_score(self.r(damage_from_concealment=100, first_strike=1), self.cfg)
        self.assertAlmostEqual(ambush - plain, 1.5 + 0.75, places=4)
        # capped at one man's HP of concealed damage
        self.assertAlmostEqual(fitness.round_score(self.r(damage_from_concealment=900), self.cfg),
                               fitness.round_score(self.r(damage_from_concealment=100), self.cfg), places=4)

    def test_strikes_unobserved_pay_small_capped(self):
        plain = fitness.round_score(self.r(), self.cfg)
        self.assertAlmostEqual(fitness.round_score(self.r(strikes_unobserved=1), self.cfg) - plain, 0.25, places=4)
        self.assertAlmostEqual(fitness.round_score(self.r(strikes_unobserved=5), self.cfg) - plain, 0.5, places=4)

    def test_objective_win_pays_modestly(self):
        plain = fitness.round_score(self.r(), self.cfg)
        self.assertAlmostEqual(fitness.round_score(self.r(objective_win=1.0), self.cfg) - plain, 2.0, places=4)
        # modest: an objective win never outweighs the win itself
        self.assertLess(self.cfg["bonuses"]["objective_win"]["weight"], self.cfg["win"])

    def test_comment_keys_in_bonuses_are_skipped(self):
        cfg = dict(self.cfg, bonuses=dict(self.cfg["bonuses"], _note="text"))
        fitness.round_score(self.r(), cfg)  # no crash on a string entry


class InstanceLockTests(unittest.TestCase):
    def test_second_instance_refused_while_holder_alive(self):
        with tempfile.TemporaryDirectory() as root:
            first = InstanceLock(root, "run-a")
            self.assertTrue(first.acquire())
            path = first.path
            self.assertEqual(genome.load_json(path)["pid"], os.getpid())
            # another live process holds it -> a second instance (any other PID) is refused
            sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            try:
                with open(path, "w") as f:
                    json.dump({"pid": sleeper.pid, "run_id": "run-a"}, f)
                second = InstanceLock(root, "run-b")
                self.assertFalse(second.acquire())
                self.assertEqual(second.holder()["run_id"], "run-a")
            finally:
                sleeper.kill()
                sleeper.wait()
            # the holder died -> its lock is stale and taken over
            third = InstanceLock(root, "run-c")
            self.assertTrue(third.acquire())
            self.assertEqual(genome.load_json(path)["run_id"], "run-c")
            third.release()
            self.assertFalse(os.path.exists(path))

    def test_cli_exits_cleanly_when_locked(self):
        with tempfile.TemporaryDirectory() as root:
            sleeper = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"])
            try:
                os.makedirs(os.path.join(root, "Saved", "Trainer"))
                with open(os.path.join(root, "Saved", "Trainer", "night_trainer.lock"), "w") as f:
                    json.dump({"pid": sleeper.pid, "run_id": "busy"}, f)
                out = subprocess.run([sys.executable, os.path.join(TRAINER, "night_trainer.py"), "--run-id", "dup",
                                      "--repo-root", root, "--max-generations", "1"],
                                     capture_output=True, text=True, timeout=120)
                self.assertEqual(out.returncode, 0)
                self.assertIn("another instance is running", out.stdout)
                self.assertFalse(os.path.exists(os.path.join(root, "Saved", "Trainer", "dup")))
            finally:
                sleeper.kill()
                sleeper.wait()


if __name__ == "__main__":
    unittest.main()
