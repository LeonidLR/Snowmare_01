import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nighttrainer.adapter import ArenaAdapter
from nighttrainer.fitness import round_score
from nighttrainer.gate import get_metric_value_from_round, check_metric_liveness
from nighttrainer.gate_identity import config_hash, identity, validate_pass
from nighttrainer.gate import check_baseline_sufficiency
import night_trainer
import metric_campaign


class MetricIdentityTests(unittest.TestCase):
    def test_campaign_writes_real_files_and_counts_actual_scenarios(self):
        for fallback in (False, True):
            with self.subTest(fallback=fallback), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                cfg = {'arena': {'time_limit_s': 120, 'arg_template': ['Scenario=random']},
                       'fitness': {}, 'baseline_weights': 'approved.json',
                       'evaluation': {'matches': [{'engine_args': ['-Sector=0']}]}}
                config = root / 'config.json'
                config.write_text(json.dumps(cfg))
                runner = SimpleNamespace(run_jobs=lambda jobs, guard: {job['id']: [
                    {'scenario': 'skirmish' if fallback else job['scenario_requested'], 'won': .5}]
                    for job in jobs})
                with patch.object(metric_campaign, 'ROOT', root), \
                        patch.object(metric_campaign, 'freeze_sha', return_value='a' * 40), \
                        patch.object(metric_campaign, 'JobRunner', return_value=runner):
                    self.assertEqual(metric_campaign.main(['--config', str(config), '--run-id', 'probe',
                                                          '--freeze-sha', 'a' * 40]), 0)
                evidence = json.loads((root / 'Saved/Trainer/probe/evidence.json').read_text())
                self.assertEqual(len(evidence['rounds']), 12)
                self.assertFalse(evidence['g3_sufficient'])
                self.assertEqual(evidence['identity'], identity(cfg, 'a' * 40))
                self.assertEqual(evidence['counts'], {'skirmish': 12} if fallback else
                                 dict.fromkeys(metric_campaign.SCENARIOS, 2))

    def test_crashes_do_not_satisfy_baseline(self):
        self.assertFalse(check_baseline_sufficiency({'rounds': [
            {'scenario': 'tactical', 'crashed': True} for _ in range(30)]})[0])

    def test_scenario_is_passed_to_real_command(self):
        adapter = ArenaAdapter({'arg_template': ['Scenario={scenario}']}, '.')
        command = adapter.console_command({'rounds': 1, 'seed': 1, 'weights_a': 'a.json',
                                          'opponent': 'champion', 'scenario_requested': 'tactical'}, 'out.json')
        self.assertIn('Scenario=tactical', command)

    def test_damage_new_measurement_wins_over_opponent_hp_loss(self):
        cfg = {'win': 0, 'survivors': 0, 'hp_lost': 0, 'damage_dealt': 4}
        adapter = ArenaAdapter({}, '.')
        for value in (0, 25, 80):
            raw = {'schema': 'codex.pmc_arena/1', 'rounds': [
                {'winner': 'A', 'sides': {'A': {'damage_dealt': value, 'hp_max': 100}, 'B': {'hp_lost': 99}}}]}
            row = adapter.parse_doc(raw)[0]
            self.assertEqual(get_metric_value_from_round(row, 'damage_dealt'), value)
            self.assertAlmostEqual(round_score(row, cfg), 4 * value / 100)

    def test_legacy_raw_and_saved_canonical_rows(self):
        adapter = ArenaAdapter({}, '.')
        row = adapter.parse_doc({'schema': 'codex.pmc_arena/1', 'rounds': [
            {'winner': 'A', 'sides': {'A': {}, 'B': {'hp_lost': 37}}} ]})[0]
        self.assertEqual(row['damage_dealt'], 37)
        old = {'opp_hp_lost': 37}
        self.assertEqual(get_metric_value_from_round(old, 'damage_dealt'), 37)
        self.assertEqual(round_score(old, {'damage_dealt': 4}), round_score({'damage_dealt': 37}, {'damage_dealt': 4}))

    def test_invalid_damage_does_not_use_legacy_proxy(self):
        for value in (None, True, float('nan'), float('inf')):
            row = {'damage_dealt': value, 'opp_hp_lost': 100}
            self.assertIsNone(get_metric_value_from_round(row, 'damage_dealt'))
            self.assertFalse(check_metric_liveness([row], {'damage_dealt'})[0])

    def test_gate_bound_to_config_and_freeze_and_age(self):
        cfg = {'fitness': {'damage_dealt': 4}, 'seed': 7}
        sha = 'a' * 40
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'gate.json'
            doc = {'passed': True, 'created_unix': 100000, 'identity': identity(cfg, sha)}
            path.write_text(json.dumps(doc))
            self.assertTrue(validate_pass(path, cfg, sha, now=100001)['passed'])
            changed = copy.deepcopy(cfg)
            changed['fitness']['damage_dealt'] = 3
            for config, freeze, now in ((changed, sha, 100001), (cfg, 'b' * 40, 100001),
                                        (cfg, sha, 200000), (cfg, sha, 99999)):
                with self.assertRaises(ValueError):
                    validate_pass(path, config, freeze, now=now)
            del doc['identity']
            path.write_text(json.dumps(doc))
            with self.assertRaises(ValueError):
                validate_pass(path, cfg, sha, now=100001)
        self.assertEqual(config_hash(cfg), config_hash(dict(reversed(list(cfg.items())))))

    def test_resume_checks_effective_config_even_with_mock_flag(self):
        cfg = {'seed': 1}
        with tempfile.TemporaryDirectory() as folder:
            gate = Path(folder) / 'Saved/Trainer/gate_pass.json'
            gate.parent.mkdir(parents=True)
            import time
            gate.write_text(json.dumps({'passed': True, 'created_unix': time.time(),
                                        'identity': identity(cfg, 'a' * 40)}))
            args = night_trainer.parse_args(['--repo-root', folder, '--mock'])
            resumed = SimpleNamespace(mock=False, cfg={'seed': 2})
            with patch.object(night_trainer, 'Trainer', return_value=resumed), \
                    patch.object(night_trainer, 'freeze_sha', return_value='a' * 40):
                with self.assertRaisesRegex(ValueError, 'mismatch'):
                    night_trainer._run(args, cfg, 'resume')
