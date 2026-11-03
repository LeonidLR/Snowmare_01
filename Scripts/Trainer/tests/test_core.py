"""Unit tests: genome / bounds, mutation / crossover / selection, fitness, adapter parsing, guards.
Run: python -m unittest discover -s Scripts/Trainer/tests
"""
import copy
import json
import os
import random
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TRAINER = os.path.dirname(HERE)
sys.path.insert(0, TRAINER)

from nighttrainer import evolution, fitness, genome, highlights, mock_arena  # noqa: E402
from nighttrainer.adapter import ArenaAdapter, ArenaResultError  # noqa: E402
from nighttrainer.runner import BuildLock, JobRunner, is_codex_editor_cmdline  # noqa: E402

FIXTURES = os.path.join(TRAINER, "fixtures")
with open(os.path.join(TRAINER, "trainer_config.json"), encoding="utf-8") as _f:
    CONFIG = json.load(_f)


def sample_weights():
    return genome.load_json(os.path.join(FIXTURES, "pmc_commander.sample.json"))


class GenomeTests(unittest.TestCase):
    def test_flatten_only_numeric_genes(self):
        genes = genome.flatten_numeric(sample_weights())
        self.assertIn("/utility/flank/weight", genes)
        self.assertIn("/skills/0/cooldown_s", genes)
        self.assertNotIn("/version", genes)            # meta key
        self.assertNotIn("/squad/use_cover_only", genes)  # bool
        self.assertNotIn("/skills/0/id", genes)        # string
        self.assertIsInstance(genes["/utility/flank/max_flankers"], int)

    def test_apply_genes_roundtrip_keeps_non_numeric(self):
        doc = sample_weights()
        genes = genome.flatten_numeric(doc)
        genes["/utility/flank/weight"] = 1.23
        out = genome.apply_genes(doc, genes)
        self.assertEqual(out["utility"]["flank"]["weight"], 1.23)
        self.assertEqual(out["skills"][1]["id"], "smoke")
        self.assertEqual(doc["utility"]["flank"]["weight"], 0.8)  # baseline untouched
        self.assertEqual(list(out.keys()), list(doc.keys()))

    def test_pointer_escaping(self):
        doc = {"a/b": {"c~d": 2.0}}
        genes = genome.flatten_numeric(doc)
        self.assertEqual(list(genes), ["/a~1b/c~0d"])
        self.assertEqual(genome.apply_genes(doc, {"/a~1b/c~0d": 3.0})["a/b"]["c~d"], 3.0)

    def test_default_bounds(self):
        self.assertEqual(genome.default_gene_bounds(2.0)["min"], 1.0)
        self.assertEqual(genome.default_gene_bounds(2.0)["max"], 3.0)
        self.assertEqual(genome.default_gene_bounds(0.0)["min"], 0.0)
        neg = genome.default_gene_bounds(-2.0)
        self.assertLess(neg["min"], neg["max"])
        self.assertGreaterEqual(genome.default_gene_bounds(3)["step"], 1)

    def test_bounds_sidecar_created_and_new_genes_join(self):
        with tempfile.TemporaryDirectory() as tmp:
            w = os.path.join(tmp, "pmc_commander.json")
            genome.write_json(w, sample_weights())
            bounds, created = genome.load_or_create_bounds(w)
            self.assertTrue(created)
            self.assertTrue(os.path.exists(os.path.join(tmp, "pmc_commander.bounds.json")))
            _, created_again = genome.load_or_create_bounds(w)
            self.assertFalse(created_again)
            bounds["genes"]["/utility/flank/weight"] = {"min": 0.1, "max": 0.2, "step": 0.01, "frozen": True}
            doc = sample_weights()
            doc["skills"].append({"id": "new_skill", "weight": 0.4})  # a new skill joins with default bounds
            specs = genome.resolve_specs(genome.flatten_numeric(doc), bounds)
            self.assertTrue(specs["/utility/flank/weight"].frozen)
            self.assertAlmostEqual(specs["/skills/2/weight"].max, 0.6)

    def test_clamp(self):
        spec = genome.GeneSpec(0.0, 1.0, 0.1)
        self.assertEqual(genome.clamp_gene(5, spec), 1.0)
        self.assertEqual(genome.clamp_gene(2.6, genome.GeneSpec(0, 3, 1, is_int=True)), 3)


class EvolutionTests(unittest.TestCase):
    def setUp(self):
        self.base = genome.flatten_numeric(sample_weights())
        self.specs = genome.resolve_specs(self.base, None)

    def test_mutation_stays_in_bounds_and_changes_something(self):
        rng = random.Random(1)
        for _ in range(200):
            child = evolution.mutate(self.base, self.specs, rng, rate=0.5, sigma_scale=3.0)
            self.assertNotEqual(child, self.base)
            for p, v in child.items():
                s = self.specs[p]
                self.assertTrue(s.min <= v <= s.max, p)
                if s.is_int:
                    self.assertIsInstance(v, int)

    def test_frozen_genes_never_mutate(self):
        bounds = {"genes": {p: {"frozen": True} for p in self.base if p != "/utility/attack/weight"}}
        specs = genome.resolve_specs(self.base, bounds)
        child = evolution.mutate(self.base, specs, random.Random(3), rate=1.0)
        changed = [p for p in child if child[p] != self.base[p]]
        self.assertEqual(changed, ["/utility/attack/weight"])

    def test_mutation_deterministic(self):
        a = evolution.mutate(self.base, self.specs, random.Random("x"))
        b = evolution.mutate(self.base, self.specs, random.Random("x"))
        self.assertEqual(a, b)

    def test_uniform_crossover_takes_genes_from_parents(self):
        other = {p: (v + 1 if isinstance(v, int) else v + 0.5) for p, v in self.base.items()}
        child = evolution.crossover(self.base, other, random.Random(5))
        self.assertTrue(all(child[p] in (self.base[p], other[p]) for p in child))
        self.assertTrue(any(child[p] == self.base[p] for p in child) and any(child[p] == other[p] for p in child))

    def test_tournament_and_elitism(self):
        scored = [({"/x": float(i)}, float(i)) for i in range(10)]
        self.assertEqual(evolution.tournament(scored, random.Random(0), k=50)["/x"], 9.0)
        specs = {"/x": genome.GeneSpec(0.0, 100.0, 1.0)}
        nxt = evolution.next_generation(scored, specs, random.Random(0), {"elitism": 2})
        self.assertEqual(len(nxt), 10)
        self.assertEqual([g["/x"] for g in nxt[:2]], [9.0, 8.0])

    def test_initial_population_starts_with_baseline(self):
        pop = evolution.initial_population(self.base, self.specs, 6, random.Random(2), {})
        self.assertEqual(len(pop), 6)
        self.assertEqual(pop[0], self.base)
        self.assertTrue(all(p != self.base for p in pop[1:]))


class FitnessTests(unittest.TestCase):
    cfg = CONFIG["fitness"]

    def r(self, **kw):
        base = {"won": 1.0, "survivors": 3, "squad_size": 4, "hp_lost": 100, "hp_max": 400, "duration_s": 150}
        base.update(kw)
        return base

    def test_win_beats_loss(self):
        self.assertGreater(fitness.round_score(self.r(), self.cfg), fitness.round_score(self.r(won=0.0), self.cfg))

    def test_survivors_and_hp(self):
        self.assertGreater(fitness.round_score(self.r(survivors=4), self.cfg), fitness.round_score(self.r(), self.cfg))
        self.assertLess(fitness.round_score(self.r(hp_lost=350), self.cfg), fitness.round_score(self.r(), self.cfg))

    def test_duration_prefers_target(self):
        on = fitness.round_score(self.r(duration_s=150), self.cfg)
        self.assertGreater(on, fitness.round_score(self.r(duration_s=20), self.cfg))
        self.assertGreater(on, fitness.round_score(self.r(duration_s=299), self.cfg))

    def test_bonus_capped_and_penalties(self):
        a = fitness.round_score(self.r(flanks_succeeded=3), self.cfg)
        self.assertAlmostEqual(a, fitness.round_score(self.r(flanks_succeeded=30), self.cfg))
        self.assertGreater(a, fitness.round_score(self.r(), self.cfg))
        self.assertLess(fitness.round_score(self.r(friendly_fire=2, stuck_events=1), self.cfg),
                        fitness.round_score(self.r(), self.cfg))

    def test_crash_penalised(self):
        agg = fitness.aggregate([self.r(), fitness.crash_round()], self.cfg)
        self.assertEqual(agg["crashes"], 1)
        self.assertEqual(agg["win_rate"], 0.5)
        self.assertAlmostEqual(agg["fitness"], (fitness.round_score(self.r(), self.cfg) + self.cfg["crash_penalty"]) / 2,
                               places=3)
        self.assertEqual(fitness.aggregate([], self.cfg)["fitness"], self.cfg["crash_penalty"])


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.adapter = ArenaAdapter(CONFIG["arena"], "C:/Repo")
        self.doc = genome.load_json(os.path.join(FIXTURES, "sample_arena_result.json"))

    def test_parse_sample(self):
        rounds = self.adapter.parse_doc(self.doc)
        self.assertEqual([r["won"] for r in rounds], [1.0, 0.0, 0.5])
        self.assertEqual(rounds[0]["survivors"], 3.0)
        self.assertEqual(rounds[1]["stuck_events"], 1.0)
        self.assertEqual(rounds[2]["time_limit_s"], 300.0)
        self.assertFalse(any(r["crashed"] for r in rounds))
        agg = fitness.aggregate(rounds, CONFIG["fitness"])
        self.assertEqual(agg["rounds"], 3)

    def test_missing_rounds_padded_as_crash(self):
        rounds = self.adapter.parse_doc(self.doc, expected_rounds=5)
        self.assertEqual(len(rounds), 5)
        self.assertTrue(rounds[-1]["crashed"])

    def test_field_map_and_side_list(self):
        cfg = copy.deepcopy(CONFIG["arena"])
        cfg["result"]["fields"]["hp_lost"] = "damage.taken"
        cfg["result"]["sides_key"] = "teams"
        doc = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "B", "duration_s": 10, "teams": [{"side": "A", "survivors": 1, "damage": {"taken": 77}}]}]}
        rounds = ArenaAdapter(cfg, "C:/Repo").parse_doc(doc)
        self.assertEqual(rounds[0]["hp_lost"], 77.0)
        self.assertEqual(rounds[0]["won"], 0.0)

    def test_bad_schema_and_winner(self):
        with self.assertRaises(ArenaResultError):
            self.adapter.parse_doc({"schema": "other", "rounds": [{}]})
        with self.assertRaises(ArenaResultError):
            self.adapter.parse_doc({"schema": "codex.pmc_arena/1", "rounds": [{"winner": "Z"}]})

    def test_build_command(self):
        job = {"rounds": 4, "seed": 7, "opponent": "champion", "map": None, "time_limit_s": 300,
               "weights_a": "C:/Repo/Saved/Trainer/r/gen_0001/candidates/g0001_c00.json",
               "weights_b": "C:/Repo/Saved/Trainer/r/gen_0001/opponent_champion.json"}
        cmd = self.adapter.build_command(job, "C:/Repo/Saved/Trainer/r/gen_0001/jobs/x.arena.json", "C:/l.log")
        self.assertTrue(cmd[0].endswith("UnrealEditor-Cmd.exe"))
        self.assertIn("-game", cmd)
        execs = [a for a in cmd if a.startswith("-ExecCmds=")][0]
        self.assertIn("CodexTactics.PmcArena Map=/Game/Maps/L_FormationLabCombat Rounds=4 Seed=7", execs)
        self.assertIn("WeightsA=Saved/Trainer/r/gen_0001/candidates/g0001_c00.json", execs)
        self.assertIn("Opponent=Pmc", execs)
        self.assertNotIn(",", execs)
        bot = dict(job, opponent="bot", weights_b=None)
        self.assertIn("Opponent=Bot WeightsB=None", self.adapter.console_command(bot, "C:/Repo/o.json"))

    def test_highlight_extras_and_track_path(self):
        rounds = self.adapter.parse_doc(dict(self.doc, rounds=[dict(self.doc["rounds"][0], highlight_score=7.5,
                                                                    track="x.round_0.track.json")]),
                                        result_dir="C:/Repo/Saved/Arena")
        r = rounds[0]
        self.assertEqual(r["highlight_score"], 7.5)
        self.assertEqual(r["opp_survivors"], 0.0)
        self.assertEqual(r["round_seed"], 4242)
        self.assertEqual(os.path.normpath(r["track"]), os.path.normpath("C:/Repo/Saved/Arena/x.round_0.track.json"))

    def test_record_command(self):
        job = {"id": "h", "rounds": 4, "seed": 99, "opponent": "bot", "weights_a": "C:/Repo/a.json",
               "weights_b": None, "map": None, "time_limit_s": 300}
        cmd = self.adapter.record_command(job, "C:/Repo/o.json", "C:/l.log", "trainer_h", 41.2)
        execs = [a for a in cmd if a.startswith("-ExecCmds=")][0]
        self.assertIn("Rounds=1 Seed=99", execs)
        self.assertIn("Record=1,CodexTactics.ClipRecorder trainer_h seconds=44", execs)
        self.assertIn("-RenderOffscreen", cmd)


class HighlightTests(unittest.TestCase):
    def test_caption(self):
        row = {"won": 1.0, "flanks_succeeded": 1, "suppression_s": 12, "survivors": 3, "opp_survivors": 0,
               "duration_s": 41.2}
        self.assertEqual(highlights.caption(row, "bot"), "PMC won: flank + suppress, 3-0, 41 s")
        self.assertTrue(highlights.caption({"won": 0.0}, "bot").startswith("Bot squad won: straight fight"))
        self.assertTrue(highlights.caption({"won": 0.5}, "champion").startswith("Draw"))

    def test_session_key_spans_the_night(self):
        import datetime as d
        self.assertEqual(highlights.session_key(d.datetime(2026, 11, 7, 23, 0)),
                         highlights.session_key(d.datetime(2026, 11, 8, 7, 0)))
        self.assertNotEqual(highlights.session_key(d.datetime(2026, 11, 7, 23, 0)),
                            highlights.session_key(d.datetime(2026, 11, 8, 23, 0)))

    def test_update_keeps_top_n_and_champion_win(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = {}
            jobs = [{"id": "c1_eval_bot_0", "seed": 10, "weights_a": "a", "opponent": "bot"},
                    {"id": "champion_g0000_fresh_bot_0", "seed": 20, "weights_a": "c", "opponent": "bot"}]
            results = {"c1_eval_bot_0": [{"won": 1.0, "highlight_score": s, "survivors": 2, "opp_survivors": 0,
                                          "duration_s": 60} for s in (1, 9, 5, 7)],
                       "champion_g0000_fresh_bot_0": [{"won": 1.0, "highlight_score": 0, "survivors": 4,
                                                       "opp_survivors": 0, "duration_s": 30}, {"crashed": True}]}
            highlights.update(tmp, {"top_n": 3}, state, 0, jobs, results, "baseline", "champion_g0000_")
            self.assertEqual([e["score"] for e in state["highlights"]["top"]], [9, 7, 5])
            self.assertEqual(state["highlights"]["champion_win"]["id"], "champion_g0000_fresh_bot_0_r0")
            self.assertEqual(state["highlights"]["champion_win"]["seed"], 20)
            self.assertEqual(len(highlights.entries(state)), 4)

    def test_fake_track_consistent_with_round(self):
        doc = mock_arena.simulate(sample_weights(), "bot", None, sample_weights(), 1, 5)
        rnd = doc["rounds"][0]
        tr = mock_arena.fake_track(rnd, random.Random(1))
        dead = sum(1 for f in tr["frames"][-1] if f is None)
        alive_expected = int(rnd["sides"]["A"]["survivors"] + rnd["sides"]["B"]["survivors"])
        self.assertEqual(len(tr["fighters"]) - dead, alive_expected)
        self.assertTrue(any(e["type"] == "shot" for e in tr["events"]))


class GuardTests(unittest.TestCase):
    def test_editor_detection(self):
        ed = r'"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\x\Documents\Unreal Projects\CodexTactics\CodexTactics.uproject"'
        self.assertTrue(is_codex_editor_cmdline(ed))
        self.assertFalse(is_codex_editor_cmdline(ed + " -game -nullrhi"))
        self.assertFalse(is_codex_editor_cmdline(ed + " -run=cook"))
        self.assertFalse(is_codex_editor_cmdline(ed.replace("\\CodexTactics\\", "\\CodexTactics-trainer\\")))
        self.assertFalse(is_codex_editor_cmdline(None))

    def test_build_lock(self):
        with tempfile.TemporaryDirectory() as tmp:
            lock = BuildLock(tmp)
            self.assertIsNone(lock.holder())
            self.assertTrue(lock.acquire())
            self.assertIsNone(lock.holder())  # our own lock does not block us
            lock.release()
            self.assertFalse(os.path.exists(lock.path))
            genome.write_json(lock.path, {"agent": "user", "pid": 0, "manual": True})
            self.assertEqual(lock.holder()["agent"], "user")
            self.assertFalse(lock.acquire())
            genome.write_json(lock.path, {"agent": "claude", "pid": 99999999, "manual": False})
            self.assertIsNone(lock.holder())  # stale

    def test_cached_jobs_are_not_rerun(self):
        class NoRun(JobRunner):
            def run_one(self, job):
                raise AssertionError("should be cached")

        class OpenGuard:
            def wait_clear(self):
                return True

            def end_wave(self):
                pass

        with tempfile.TemporaryDirectory() as tmp:
            genome.write_json(os.path.join(tmp, "j1.rounds.json"), {"rounds": [{"won": 1.0}]})
            res = NoRun(None, {}, 2).run_jobs([{"id": "j1", "dir": tmp}], OpenGuard())
            self.assertEqual(res, {"j1": [{"won": 1.0}]})


if __name__ == "__main__":
    unittest.main()
