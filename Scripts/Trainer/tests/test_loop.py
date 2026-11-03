"""End-to-end mock loop: outputs, resume determinism, STOP / crash handling, champion promotion rule."""
import copy
import json
import os
import shutil
import glob
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TRAINER = os.path.dirname(HERE)
sys.path.insert(0, TRAINER)

from nighttrainer.genome import load_json  # noqa: E402
from nighttrainer.trainer import Trainer  # noqa: E402

with open(os.path.join(TRAINER, "trainer_config.json"), encoding="utf-8") as _f:
    CONFIG = json.load(_f)


def make_repo(tmp):
    os.makedirs(os.path.join(tmp, "fixtures"))
    shutil.copy(os.path.join(TRAINER, "fixtures", "pmc_commander.sample.json"), os.path.join(tmp, "fixtures"))
    cfg = copy.deepcopy(CONFIG)
    cfg["baseline_weights"] = "missing/pmc_commander.json"
    cfg["mock"]["baseline_weights"] = "fixtures/pmc_commander.sample.json"
    cfg["mock"]["in_process"] = True
    cfg["evolution"]["population"] = 6
    cfg["evaluation"]["rounds_per_job"] = 3
    cfg["highlights"]["auto_clips"] = False
    cfg["mock"]["tracks"] = False  # fake tracks cost ~40 ms per round; only the highlight tests need them
    return cfg


def trainer(cfg, tmp, run_id, gens, **kw):
    return Trainer(cfg, run_id, tmp, mock=True, log=lambda m: None, max_generations=gens, parallel=2, poll_s=0.01, **kw)


def signature(run_dir):
    st = load_json(os.path.join(run_dir, "state.json"))
    return [(h["generation"], h["best"], h["mean"], h["best_id"], h["champion"]) for h in st["history"]], st["population"]


class LoopTests(unittest.TestCase):
    def test_outputs_and_resume_equal_continuous_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            self.assertEqual(trainer(cfg, tmp, "cont", 3).run(), "finished")
            run_dir = os.path.join(tmp, "Saved", "Trainer", "cont")
            for name in ("leaderboard.json", "leaderboard.csv", "champion_weights.json", "champion_meta.json",
                         "report.html", "state.json", "status.json", "gen_0002/generation.json"):
                self.assertTrue(os.path.exists(os.path.join(run_dir, name)), name)
            self.assertTrue(os.path.exists(os.path.join(tmp, "fixtures", "pmc_commander.sample.bounds.json")))
            with open(os.path.join(run_dir, "report.html"), encoding="utf-8") as f:
                html = f.read()
            self.assertIn("<svg", html)
            self.assertIn("Champion weights vs baseline", html)
            champ = load_json(os.path.join(run_dir, "champion_weights.json"))
            self.assertEqual(champ["skills"][0]["id"], "grenade")  # a full drop-in weights file

            trainer(cfg, tmp, "split", 2).run()
            self.assertEqual(load_json(os.path.join(tmp, "Saved/Trainer/split/state.json"))["generation_done"], 1)
            trainer(cfg, tmp, "split", 3).run()  # resume
            self.assertEqual(signature(run_dir), signature(os.path.join(tmp, "Saved", "Trainer", "split")))

    def test_stop_file_stops_and_resume_finishes(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            run_dir = os.path.join(tmp, "Saved", "Trainer", "stop")
            os.makedirs(run_dir)
            open(os.path.join(run_dir, "STOP"), "w").close()
            self.assertEqual(trainer(cfg, tmp, "stop", 2).run(), "stopped")
            self.assertEqual(load_json(os.path.join(run_dir, "state.json"))["generation_done"], -1)
            os.remove(os.path.join(run_dir, "STOP"))
            self.assertEqual(trainer(cfg, tmp, "stop", 2).run(), "finished")
            self.assertEqual(load_json(os.path.join(run_dir, "state.json"))["generation_done"], 1)

    def test_subprocess_mock_with_crashes_is_not_fatal(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            cfg["mock"]["in_process"] = False
            cfg["mock"]["crash_rate"] = 0.9
            cfg["evolution"]["population"] = 3
            cfg["evolution"]["fresh_top_k"] = 1
            cfg["arena"]["retries"] = 0
            self.assertEqual(trainer(cfg, tmp, "crash", 1).run(), "finished")
            gen = load_json(os.path.join(tmp, "Saved/Trainer/crash/gen_0000/generation.json"))
            crashes = sum(c["stats"]["all"]["crashes"] for c in gen["candidates"])
            rounds = sum(c["stats"]["all"]["rounds"] for c in gen["candidates"])
            self.assertEqual(rounds, 3 * 2 * 3)
            self.assertGreater(crashes, 0)

    def test_promotion_needs_fresh_seed_win(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            cfg["evolution"]["promotion_margin"] = 1e9  # impossible margin -> the baseline stays champion
            trainer(cfg, tmp, "nopromo", 2).run()
            st = load_json(os.path.join(tmp, "Saved/Trainer/nopromo/state.json"))
            self.assertEqual(st["champion"]["id"], "baseline")
            self.assertEqual(st["champions"], [])
            self.assertIsNotNone(st["baseline_stats"])

    def test_highlights_tracks_and_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            cfg["mock"]["tracks"] = True
            trainer(cfg, tmp, "hl", 2).run()
            run_dir = os.path.join(tmp, "Saved", "Trainer", "hl")
            st = load_json(os.path.join(run_dir, "state.json"))
            hl = st["highlights"]
            self.assertEqual(len(hl["top"]), 3)
            self.assertGreaterEqual(hl["top"][0]["score"], hl["top"][-1]["score"])
            kept = glob.glob(os.path.join(run_dir, "highlights", "tracks", "*.track.json"))
            self.assertGreaterEqual(len(kept), 3)
            self.assertEqual(glob.glob(os.path.join(run_dir, "gen_0000", "jobs", "*.track.json")), [])  # pruned
            tr = load_json(kept[0])
            self.assertEqual(tr["schema"], "codex.pmc_arena_track/1")
            self.assertEqual(len(tr["fighters"]), len(tr["frames"][0]))
            with open(os.path.join(run_dir, "report.html"), encoding="utf-8") as f:
                page = f.read()
            self.assertIn("id='rp-canvas'", page)
            self.assertIn("2D replay", page)
            self.assertNotIn("</script></script>", page)

    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg not on PATH")
    def test_mock_clips(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = make_repo(tmp)
            cfg["highlights"]["top_n"] = 1
            cfg["mock"]["tracks"] = True
            t = trainer(cfg, tmp, "clips", 1)
            t.run()
            self.assertGreaterEqual(t.make_highlights(), 1)
            clips = glob.glob(os.path.join(tmp, "Saved", "Trainer", "clips", "highlights", "*.mp4"))
            self.assertTrue(clips and all(os.path.getsize(c) < 25e6 for c in clips))
            with open(os.path.join(tmp, "Saved", "Trainer", "clips", "report.html"), encoding="utf-8") as f:
                self.assertIn("<video", f.read())


if __name__ == "__main__":
    unittest.main()
