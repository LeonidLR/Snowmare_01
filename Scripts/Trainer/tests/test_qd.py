"""G002: behaviour metrics, MAP-Elites archive, league parents, morning-report section.

Run: python -m unittest discover -s Scripts/Trainer/tests
"""
import copy
import io
import json
import os
import random
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
TRAINER = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(TRAINER))
sys.path.insert(0, TRAINER)

from nighttrainer import fitness, qd  # noqa: E402
from nighttrainer.adapter import (ArenaAdapter, BEHAVIOUR_METRICS, _metric_value, behaviour_defaults,  # noqa: E402
                                  behaviour_document, parse_behaviour)
from nighttrainer.genome import GeneSpec, load_json  # noqa: E402
from nighttrainer.report import _archive_section  # noqa: E402
from nighttrainer.trainer import Trainer, _lookup_rounds  # noqa: E402

with open(os.path.join(TRAINER, "trainer_config.json"), encoding="utf-8") as _f:
    CONFIG = json.load(_f)

AXES = {"axes": [
    {"id": "aggression", "metric": "time_in_contact_s", "bins": 4, "range": [0, 100]},
    {"id": "mobility", "metrics": ["flanks_completed", "moves"], "bins": 4, "range": [0, 10]},
    {"id": "patience", "metric": "ambush_success", "bins": 3, "range": [0, 3]},
]}


def _beh(**kw):
    out = behaviour_defaults()
    out.update(kw)
    return out


class BehaviourParseTests(unittest.TestCase):
    def test_missing_behaviour_is_zeros(self):
        doc = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "A", "duration_s": 10, "sides": {"A": {"survivors": 1}, "B": {"survivors": 0}}}]}
        row = ArenaAdapter(CONFIG["arena"], "C:/Repo").parse_doc(doc)[0]
        self.assertEqual(row["behaviour"], behaviour_defaults())
        self.assertEqual(row["survivors"], 1.0)

    def test_partial_flat_and_rich_and_side_fallback(self):
        adapter = ArenaAdapter(CONFIG["arena"], "C:/Repo")
        flat = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "A", "duration_s": 10, "behaviour": {"time_in_contact_s": 40, "moves": True,
                                                            "order_churn": "no"},
             "sides": {"A": {}, "B": {}}}]}
        row = adapter.parse_doc(flat)[0]["behaviour"]
        self.assertEqual(row["time_in_contact_s"], 40.0)
        self.assertEqual(row["moves"], 0.0)  # bool is not a measurement
        self.assertEqual(row["order_churn"], 0.0)
        self.assertEqual(row["ambush_success"], 0.0)

        rich = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "B", "duration_s": 10, "behaviour": behaviour_document({"quiet_spells": 3, "stalemate_share": 1}),
             "sides": {"A": {}, "B": {}}}]}
        got = adapter.parse_doc(rich)[0]["behaviour"]
        self.assertEqual(got["quiet_spells"], 3.0)
        self.assertEqual(got["stalemate_share"], 1.0)
        self.assertEqual(got["flanks_completed"], 0.0)

        side = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "draw", "duration_s": 10, "sides": {
                "A": {"behaviour": {"ambush_success": {"value": 2, "unit": "count"}}}, "B": {}}}]}
        self.assertEqual(adapter.parse_doc(side)[0]["behaviour"]["ambush_success"], 2.0)

    def test_cpp_tojson_shape(self):
        """claude/tactical-p2 PmcArenaBehaviourRules::ToJson: schema, side, tactical_map, metrics.*.value.
        rounds[].behaviour is side A and wins over sides.A.behaviour."""
        values = {"time_in_contact_s": 42.0, "flanks_completed": 1.0, "time_in_chokepoint_s": 3.3,
                  "stalemate_share": 1.0}
        metrics = {}
        for key, name, unit, lo, hi, direction in BEHAVIOUR_METRICS:
            metrics[key] = {"name": name, "unit": unit, "range": [lo, hi], "direction": direction,
                            "value": values.get(key, 0.0)}
        behaviour = {"schema": "codex.pmc_arena_behaviour/1", "side": "A", "tactical_map": True, "metrics": metrics}
        doc = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "A", "duration_s": 40, "behaviour": behaviour,
             "sides": {"A": {"behaviour": dict(behaviour, side="A"), "survivors": 2},
                       "B": {"behaviour": dict(behaviour, side="B", tactical_map=True), "survivors": 1}}}]}
        row = ArenaAdapter(CONFIG["arena"], "C:/Repo").parse_doc(doc)[0]
        self.assertEqual(row["behaviour"]["time_in_contact_s"], 42.0)
        self.assertEqual(row["behaviour"]["flanks_completed"], 1.0)
        self.assertEqual(row["behaviour"]["time_in_chokepoint_s"], 3.3)
        self.assertEqual(row["behaviour"]["stalemate_share"], 1.0)
        self.assertEqual(row["behaviour"]["overwatch_kills"], 0.0)
        self.assertEqual(len(row["behaviour"]), len(BEHAVIOUR_METRICS))
        self.assertEqual(row["survivors"], 2.0)

    def test_round_object_wins_over_the_side(self):
        doc = {"schema": "codex.pmc_arena/1", "rounds": [
            {"winner": "A", "duration_s": 1, "behaviour": {"moves": 4},
             "sides": {"A": {"behaviour": {"moves": 9}}, "B": {}}}]}
        self.assertEqual(ArenaAdapter(CONFIG["arena"], "C:/Repo").parse_doc(doc)[0]["behaviour"]["moves"], 4.0)

    def test_schema_doc_names_every_metric(self):
        path = os.path.join(REPO, "docs", "port", "grok", "outbox", "G002-arena-behaviour-schema.md")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for key, name, unit, _lo, _hi, direction in BEHAVIOUR_METRICS:
            self.assertIn(key, text)
            self.assertIn(name, text)
            self.assertIn(direction, text)
            self.assertIn(unit, text)
        self.assertEqual(set(parse_behaviour(None)), {key for key, *_r in BEHAVIOUR_METRICS})

    def test_behaviour_does_not_change_the_scalar(self):
        plain = {"won": 1.0, "survivors": 2, "squad_size": 4, "hp_lost": 40, "hp_max": 400, "duration_s": 150,
                 "kills": 1}
        with_behaviour = dict(plain, behaviour=_beh(time_in_contact_s=99))
        self.assertEqual(fitness.round_score(plain, CONFIG["fitness"]),
                         fitness.round_score(with_behaviour, CONFIG["fitness"]))

    def test_non_finite_metric_is_missing(self):
        for bad in (float("nan"), float("inf"), float("-inf")):
            self.assertIsNone(_metric_value(bad))
            self.assertIsNone(_metric_value({"value": bad, "unit": "s"}))
            got = parse_behaviour({"time_in_contact_s": bad, "moves": 2})
            self.assertEqual(got["time_in_contact_s"], 0.0)
            self.assertEqual(got["moves"], 2.0)
            qd.Archive(AXES).consider("n", {"/a": 1.0}, 1.0, got, 0)


class ArchiveTests(unittest.TestCase):
    def test_insert_replace_and_tie(self):
        arch = qd.Archive(AXES)
        genes_a = {"/a": 1.0}
        genes_b = {"/a": 2.0}
        beh = _beh(time_in_contact_s=10, flanks_completed=1, moves=1, ambush_success=0)
        self.assertEqual(arch.consider("c0", genes_a, 1.0, beh, 0), "insert")
        self.assertEqual(arch.consider("c1", genes_b, 1.0, beh, 1), "reject")
        self.assertEqual(arch.cells["0,0,0"]["id"], "c0")
        self.assertEqual(arch.consider("c2", genes_b, 1.5, beh, 2), "replace")
        self.assertEqual(arch.cells["0,0,0"]["id"], "c2")
        self.assertEqual(arch.cells["0,0,0"]["genes"], genes_b)
        self.assertEqual(len(arch.cells), 1)

    def test_bins_and_sum_axis(self):
        arch = qd.Archive(AXES)
        self.assertEqual(arch.key_for(_beh(time_in_contact_s=0, ambush_success=0)), (0, 0, 0))
        self.assertEqual(arch.key_for(_beh(time_in_contact_s=100))[0], 3)  # right edge is the last bin
        self.assertEqual(arch.key_for(_beh(time_in_contact_s=25))[0], 1)
        self.assertEqual(arch.key_for(_beh(time_in_contact_s=-5))[0], 0)
        # mobility = flanks + moves, bins of 2.5 over [0, 10]
        self.assertEqual(arch.key_for(_beh(flanks_completed=2, moves=3))[1], 2)  # 5 -> bin 2
        self.assertEqual(arch.key_for(_beh(ambush_success=3))[2], 2)
        low = _beh(time_in_contact_s=1)
        high = _beh(time_in_contact_s=90, ambush_success=2)
        arch.consider("low", {"/a": 1}, 0.5, low, 0)
        arch.consider("high", {"/a": 2}, 0.5, high, 0)
        self.assertEqual(arch.capacity(), 4 * 4 * 3)
        self.assertEqual(len(arch.cells), 2)

    def test_selection_draws_only_from_filled_cells(self):
        arch = qd.Archive(AXES)
        self.assertEqual(arch.sample_genes(random.Random(1), 4), [])
        self.assertIsNone(qd.next_population(arch, {}, random.Random(1), {}, 4))
        arch.consider("a", {"/a": 1.0, "/b": 0.0}, 1.0, _beh(time_in_contact_s=1), 0)
        arch.consider("b", {"/a": 9.0, "/b": 9.0}, 2.0, _beh(time_in_contact_s=80), 0)
        rng = random.Random(3)
        drawn = arch.sample_genes(rng, 30)
        seen = {tuple(sorted(g.items())) for g in drawn}
        self.assertEqual(seen, {(("/a", 1.0), ("/b", 0.0)), (("/a", 9.0), ("/b", 9.0))})
        specs = {}  # no mutable genes: children equal a parent, still size-many
        kids = qd.next_population(arch, specs, random.Random(4), {"crossover_rate": 0.0}, 5)
        self.assertEqual(len(kids), 5)
        for kid in kids:
            self.assertIn(tuple(sorted(kid.items())), seen)

    def test_save_roundtrip_keeps_the_elite(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "qd_archive.json")
            arch = qd.Archive(AXES)
            arch.consider("elite", {"/w": 3}, 4.25, _beh(time_in_contact_s=12, moves=2), 1)
            arch.save(path)
            loaded = qd.Archive.load(path, AXES)
            self.assertEqual(loaded.cells["0,0,0"]["id"], "elite")
            self.assertEqual(loaded.cells["0,0,0"]["fitness"], 4.25)
            self.assertIn("highlight", loaded.cells["0,0,0"])

    def test_load_drops_a_foreign_or_non_object_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "qd_archive.json")
            arch = qd.Archive(AXES)
            arch.consider("elite", {"/w": 3}, 4.25, _beh(time_in_contact_s=12), 1)
            arch.save(path)
            foreign = copy.deepcopy(AXES)
            foreign["axes"][0] = dict(foreign["axes"][0], bins=9)
            buf = io.StringIO()
            with redirect_stdout(buf):
                dropped = qd.Archive.load(path, foreign)
            self.assertEqual(dropped.cells, {})
            self.assertIn("starting an empty archive", buf.getvalue())

            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump([1, 2, 3], f)
            buf = io.StringIO()
            with redirect_stdout(buf):
                dropped = qd.Archive.load(path, AXES)
            self.assertEqual(dropped.cells, {})
            self.assertIn("not an object", buf.getvalue())

            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump({"cells": {"0,0,0": {"id": "stale", "genes": {"/w": 1}}}}, f)
            buf = io.StringIO()
            with redirect_stdout(buf):
                dropped = qd.Archive.load(path, AXES)
            self.assertEqual(dropped.cells, {})
            self.assertIn("axes or bins", buf.getvalue())

    def test_elitism_is_prepended_unmutated(self):
        arch = qd.Archive(AXES)
        low = {"/a": 10.0}
        high = {"/a": 50.0}
        arch.consider("low", low, 1.0, _beh(time_in_contact_s=1), 0)
        arch.consider("high", high, 9.0, _beh(time_in_contact_s=80), 0)
        specs = {"/a": GeneSpec(0.0, 100.0, 5.0, False, False)}
        only = qd.next_population(arch, specs, random.Random(0), {"mutation_rate": 1.0}, 2)
        self.assertEqual(only, [high, low])
        kids = qd.next_population(arch, specs, random.Random(1),
                                  {"elitism": 2, "crossover_rate": 0.0, "mutation_rate": 1.0}, 4)
        self.assertEqual(kids[:2], [high, low])
        self.assertEqual(len(kids), 4)
        for kid in kids[2:]:
            self.assertNotEqual(kid, high)
            self.assertNotEqual(kid, low)
        plain = qd.next_population(arch, specs, random.Random(2),
                                   {"elitism": 0, "crossover_rate": 0.0, "mutation_rate": 1.0}, 3)
        self.assertEqual(len(plain), 3)
        for kid in plain:
            self.assertNotEqual(kid, high)
            self.assertNotEqual(kid, low)


class ReportTests(unittest.TestCase):
    def test_section_lists_cells_and_ga_says_off(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = {"search": {"mode": "qd"}, "qd": dict(AXES, archive_file="qd_archive.json")}
            arch = qd.Archive(cfg["qd"])
            arch.consider("g0000_c01", {"/w": 1}, 3.5, _beh(time_in_contact_s=80, flanks_completed=3, moves=4,
                                                           ambush_success=2, suppress_then_move=2), 0)
            arch.save(os.path.join(tmp, "qd_archive.json"))

            class Bag:
                pass

            bag = Bag()
            bag.cfg, bag.run_dir = cfg, tmp
            page = _archive_section(bag)
            self.assertIn("Cells filled", page)
            self.assertIn("<b>1</b>", page)
            self.assertIn("g0000_c01", page)
            self.assertIn("time_in_contact_s=80.00", page)
            self.assertTrue("aggressive flanker" in page or "patient ambusher" in page or "suppress-and-push" in page
                            or "mixed" in page or "holder" in page)
            bag.cfg = {"search": {"mode": "ga"}, "qd": {}}
            off = _archive_section(bag)
            self.assertIn("MAP-Elites is off", off)
            self.assertIn("Search mode: ga", off)
            self.assertNotIn("Cells filled", off)
            bag.cfg = {"qd": {}}
            missing = _archive_section(bag)
            self.assertIn("Search mode: ga", missing)
            self.assertIn("MAP-Elites is off", missing)
            self.assertEqual(qd.search_mode_of({}), "ga")
            self.assertEqual(qd.search_mode_of({"search": {"mode": "QD"}}), "qd")


def _repo(tmp, mode):
    os.makedirs(os.path.join(tmp, "fixtures"))
    shutil.copy(os.path.join(TRAINER, "fixtures", "pmc_commander.sample.json"), os.path.join(tmp, "fixtures"))
    cfg = copy.deepcopy(CONFIG)
    cfg["baseline_weights"] = "missing/pmc_commander.json"
    cfg["mock"]["baseline_weights"] = "fixtures/pmc_commander.sample.json"
    cfg["mock"]["in_process"] = True
    cfg["mock"]["tracks"] = False
    cfg["mock"]["crash_rate"] = 0.0
    cfg["evolution"]["population"] = 6
    cfg["evaluation"]["rounds_per_job"] = 2
    cfg["highlights"]["auto_clips"] = False
    cfg["search"]["mode"] = mode
    return cfg


class LoopModeTests(unittest.TestCase):
    def test_qd_run_fills_more_than_one_cell_and_then_plays_the_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Trainer(_repo(tmp, "qd"), "qd", tmp, mock=True, log=lambda m: None, max_generations=2, parallel=2,
                        poll_s=0.01)
            self.assertEqual(t.run(), "finished")
            arch = load_json(os.path.join(t.run_dir, "qd_archive.json"))
            self.assertGreaterEqual(arch["filled"], 2)
            self.assertEqual(arch["schema"], "codex.trainer_qd_archive/1")
            with open(os.path.join(t.run_dir, "report.html"), encoding="utf-8") as f:
                page = f.read()
            self.assertIn("Cells filled", page)
            self.assertIn("Highlight", page)
            gen0 = os.path.join(t.run_dir, "gen_0000", "jobs")
            gen1 = os.path.join(t.run_dir, "gen_0001", "jobs")
            self.assertTrue(any("bot" in name for name in os.listdir(gen0)))
            self.assertTrue(any("archive" in name for name in os.listdir(gen1)))
            self.assertTrue(os.path.isdir(os.path.join(t.run_dir, "gen_0001", "league")))
            snap0 = load_json(os.path.join(t.run_dir, "gen_0000", "qd_archive.in.json"))
            snap1 = load_json(os.path.join(t.run_dir, "gen_0001", "qd_archive.in.json"))
            self.assertEqual(snap0["cells"], {})
            self.assertGreaterEqual(snap1["filled"], 1)
            for cell in snap1["cells"].values():
                self.assertEqual(cell["generation"], 0)

    def test_ga_mode_keeps_the_scalar_loop(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Trainer(_repo(tmp, "ga"), "ga", tmp, mock=True, log=lambda m: None, max_generations=1, parallel=2,
                        poll_s=0.01)
            self.assertEqual(t.run(), "finished")
            self.assertFalse(os.path.exists(os.path.join(t.run_dir, "qd_archive.json")))
            self.assertFalse(os.path.exists(os.path.join(t.run_dir, "gen_0000", "qd_archive.in.json")))
            names = os.listdir(os.path.join(t.run_dir, "gen_0000", "jobs"))
            self.assertTrue(any("champion" in name for name in names))
            self.assertFalse(any("archive" in name or name.startswith("bot") for name in names))
            with open(os.path.join(t.run_dir, "report.html"), encoding="utf-8") as f:
                self.assertIn("MAP-Elites is off", f.read())

    def test_league_reads_the_entry_snapshot_not_the_live_archive(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Trainer(_repo(tmp, "qd"), "snap", tmp, mock=True, log=lambda m: None, max_generations=1,
                        parallel=1, poll_s=0.01)
            pointer = "/utility/attack/weight"
            entered = qd.Archive(t.cfg.get("qd", {}))
            entered.consider("entered", {pointer: 1.25}, 2.0, _beh(time_in_contact_s=10), 0)
            entered.save(t.archive_path())
            gen_dir = os.path.join(t.run_dir, "gen_0003")
            t._snapshot_archive(gen_dir)
            later = qd.Archive(t.cfg.get("qd", {}))
            later.consider("later", {pointer: 7.5}, 9.0, _beh(time_in_contact_s=90), 3)
            later.save(t.archive_path())
            t._snapshot_archive(gen_dir)
            frozen = load_json(os.path.join(gen_dir, "qd_archive.in.json"))
            self.assertEqual([c["id"] for c in frozen["cells"].values()], ["entered"])
            plan = t._league_plan(3, "eval", gen_dir, t.baseline_path)
            row = next(p for p in plan if p["opponent"] == "archive")
            weights = load_json(row["weights_b"])
            self.assertEqual(weights["utility"]["attack"]["weight"], 1.25)
            self.assertEqual(load_json(t.archive_path())["cells"][list(later.cells)[0]]["id"], "later")


class RealDescriptorCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture = os.path.join(TRAINER, "fixtures", "pmc_v6_qd_40_candidates.json")
        if not os.path.exists(fixture):
            raise FileNotFoundError(f"Fixture missing: {fixture}")
        cls.candidates = load_json(fixture)

    def test_forty_candidates_loaded(self):
        self.assertEqual(len(self.candidates), 40)

    def test_validate_bin_edges(self):
        # Valid: 4 bins, 3 strictly increasing edges
        arch = qd.Archive({"axes": [{"id": "a", "bins": 4, "bin_edges": [10.0, 20.0, 30.0]}]})
        self.assertEqual(len(arch.axes), 1)

        # Invalid length (wants bins - 1 = 2 edges, got 1)
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "bad_len", "bins": 3, "bin_edges": [10.0]}]})

        # Non-increasing (10 >= 10)
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "non_inc", "bins": 3, "bin_edges": [10.0, 10.0]}]})

        # Decreasing (20 > 10)
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "dec", "bins": 3, "bin_edges": [20.0, 10.0]}]})

        # Non-finite edges (nan, inf, -inf)
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "nan_edge", "bins": 3, "bin_edges": [10.0, float("nan")]}]})
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "inf_edge", "bins": 3, "bin_edges": [10.0, float("inf")]}]})
        with self.assertRaises(ValueError):
            qd.Archive({"axes": [{"id": "neginf_edge", "bins": 3, "bin_edges": [float("-inf"), 10.0]}]})

    def test_descriptor_histogram(self):
        axes = [
            {"id": "aggression", "metric": "time_in_contact_s", "bins": 4, "bin_edges": [30.0, 45.0, 60.0]},
            {"id": "mobility", "metrics": ["flanks_completed", "moves"], "bins": 4, "bin_edges": [16.0, 24.0, 32.0]},
        ]
        behaviours = [
            {"time_in_contact_s": 25.0, "flanks_completed": 0, "moves": 10},
            {"time_in_contact_s": 50.0, "flanks_completed": 1, "moves": 20},
            {"time_in_contact_s": 70.0, "flanks_completed": 2, "moves": 35},
        ]
        hist = qd.descriptor_histogram(behaviours, axes)
        self.assertIn("aggression", hist)
        self.assertEqual(hist["aggression"]["bins"], [1, 0, 1, 1])
        self.assertEqual(hist["aggression"]["min"], 25.0)
        self.assertEqual(hist["aggression"]["max"], 70.0)
        self.assertIn("mobility", hist)
        self.assertEqual(hist["mobility"]["bins"], [1, 1, 0, 1])

    def test_old_binning_reproduces_two_of_fortyeight_cells(self):
        """Old binning from pmc-v6-qd: mobility [0,12] saturates (avg 19 -> bin 3), aggression [0,180] in bins 0-1, patience [0,3] in bin 0."""
        old_axes = [
            {"id": "aggression", "metric": "time_in_contact_s", "bins": 4, "range": [0, 180]},
            {"id": "mobility", "metrics": ["flanks_completed", "moves"], "bins": 4, "range": [0, 12]},
            {"id": "patience", "metric": "ambush_success", "bins": 3, "range": [0, 3]}
        ]
        arch = qd.Archive({"axes": old_axes})
        for c in self.candidates:
            arch.consider(c["id"], {}, c["fitness"], c["behaviour"], c.get("gen", 0))
        self.assertEqual(arch.capacity(), 48)
        self.assertEqual(len(arch.cells), 2)
        self.assertEqual(set(arch.cells.keys()), {"0,3,0", "1,3,0"})

    def test_new_binning_fills_at_least_eight_cells(self):
        """New binning from trainer_config.json: bin_edges / normalized ranges reflect real metrics and fill >= 8 cells (actual 20)."""
        cfg_axes = CONFIG["qd"]["axes"]
        arch = qd.Archive({"axes": cfg_axes})
        for c in self.candidates:
            arch.consider(c["id"], {}, c["fitness"], c["behaviour"], c.get("gen", 0))
        self.assertEqual(arch.capacity(), 48)
        self.assertGreaterEqual(len(arch.cells), 8)
        self.assertEqual(len(arch.cells), 20)
        seen_agg = {c["key"][0] for c in arch.cells.values()}
        seen_mob = {c["key"][1] for c in arch.cells.values()}
        seen_pat = {c["key"][2] for c in arch.cells.values()}
        self.assertEqual(seen_agg, {0, 1, 2, 3})
        self.assertEqual(seen_mob, {0, 1, 2, 3})
        self.assertEqual(seen_pat, {0, 1, 2})

    def test_empty_neighbors_computation(self):
        axes = [
            {"id": "x", "metric": "time_in_contact_s", "bins": 3, "range": [0, 30]},
            {"id": "y", "metric": "moves", "bins": 3, "range": [0, 30]},
        ]
        arch = qd.Archive({"axes": axes})
        self.assertEqual(arch.capacity(), 9)
        arch.consider("c", {}, 1.0, _beh(time_in_contact_s=15, moves=15), 0)
        self.assertEqual(arch.empty_neighbors((1, 1)), 8)
        arch.consider("corner", {}, 1.0, _beh(time_in_contact_s=5, moves=5), 0)
        self.assertEqual(arch.empty_neighbors((0, 0)), 2)

    def test_bot_jobs_ask_one_round_and_avoid_padded_crashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Trainer(_repo(tmp, "qd"), "bot_rounds", tmp, mock=True, log=lambda m: None, max_generations=1,
                        parallel=1, poll_s=0.01)
            plan = t._league_plan(0, "eval", os.path.join(tmp, "gen_0000"), t.baseline_path)
            bot_match = next(p for p in plan if p["opponent"] == "bot")
            self.assertEqual(bot_match.get("rounds"), 1)

            jobs = t.make_jobs("cand_1", t.baseline_path, 0, "eval", os.path.join(tmp, "gen_0000"), plan)
            bot_job = next(j for j in jobs if j["opponent"] == "bot")
            self.assertEqual(bot_job["rounds"], 1)

            # 1-round bot doc: parsed with job's rounds (1), 0 crash penalty
            one_round_doc = {
                "schema": "codex.pmc_arena/1",
                "opponent": "bot",
                "rounds": [{"winner": "A", "duration_s": 30, "sides": {"A": {"survivors": 3}, "B": {"survivors": 0}}}]
            }
            parsed = t.adapter.parse_doc(one_round_doc, expected_rounds=int(bot_job["rounds"]))
            self.assertEqual(len(parsed), 1)
            self.assertFalse(parsed[0].get("crashed", False))
            score_1 = fitness.round_score(parsed[0], CONFIG["fitness"])
            self.assertGreater(score_1, 0.0)

            # Real 0-round bot doc (crashed arena before completing round): parsed with job's rounds (1), gets exactly 1 crash penalty
            zero_round_doc = {
                "schema": "codex.pmc_arena/1",
                "opponent": "bot",
                "rounds": []
            }
            parsed_zero = t.adapter.parse_doc(zero_round_doc, expected_rounds=int(bot_job["rounds"]))
            self.assertEqual(len(parsed_zero), 1)
            self.assertTrue(parsed_zero[0].get("crashed", False))
            score_0 = fitness.round_score(parsed_zero[0], CONFIG["fitness"])
            self.assertEqual(score_0, float(CONFIG["fitness"].get("crash_penalty", -5.0)))

    def test_padded_crash_rounds_not_aliased(self):
        doc = {"schema": "codex.pmc_arena/1", "opponent": "bot", "rounds": []}
        adapter = ArenaAdapter(CONFIG["arena"], "C:/Repo")
        rounds = adapter.parse_doc(doc, expected_rounds=3)
        self.assertEqual(len(rounds), 3)
        self.assertIsNot(rounds[0], rounds[1])
        self.assertIsNot(rounds[1], rounds[2])
        rounds[0]["won"] = 1.0
        self.assertEqual(rounds[1]["won"], 0.0)

    def test_rounds_per_job_less_than_one_raises(self):
        with self.assertRaises(ValueError):
            _lookup_rounds({"rounds_per_job": 0}, 4)
        with self.assertRaises(ValueError):
            _lookup_rounds({"rounds_per_job": -1}, 4)
        with self.assertRaises(ValueError):
            _lookup_rounds({}, 0)
        with tempfile.TemporaryDirectory() as tmp:
            bad_cfg = _repo(tmp, "ga")
            bad_cfg["evaluation"]["rounds_per_job"] = 0
            with self.assertRaises(ValueError):
                Trainer(bad_cfg, "bad_rounds", tmp, mock=True, log=lambda m: None)

    def test_ga_mode_skips_qd_validation_and_histogram(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = _repo(tmp, "ga")
            cfg["search"] = {"mode": "ga"}
            cfg["qd"]["axes"] = [{"id": "bad", "bins": 4, "bin_edges": [10.0]}]  # invalid len(bin_edges)
            t = Trainer(cfg, "ga_skip", tmp, mock=True, log=lambda m: None, max_generations=1, parallel=1, poll_s=0.01)
            self.assertEqual(t.search_mode(), "ga")


if __name__ == "__main__":
    unittest.main()
