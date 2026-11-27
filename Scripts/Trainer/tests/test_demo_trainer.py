import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from nighttrainer import doctrine, fitness
from nighttrainer.adapter import ArenaAdapter
from weekend_trainer import arena_for, plan, summarize
import weekend_trainer
from nighttrainer.gate import extract_used_metrics, get_metric_value_from_round
from nighttrainer.trainer import Trainer


class DoctrineTests(unittest.TestCase):
    def setUp(self):
        self.cfg = {'weight': 1, 'terms': [
            {'metric': 'retreats_survived', 'weight': .75, 'cap': 2},
            {'metric': 'behaviour.suppress_then_move', 'weight': .25, 'cap': 2}]}

    def test_bounded_no_renormalization_and_no_negative_credit(self):
        self.assertEqual(doctrine.evaluate({'retreats_survived': 100}, self.cfg)['bonus'], .75)
        self.assertEqual(doctrine.evaluate({'retreats_survived': -2}, self.cfg)['bonus'], 0)

    def test_missing_nonfinite_boolean_unknown(self):
        for value in (None, True, float('nan'), float('inf'), '2'):
            score = doctrine.evaluate({'retreats_survived': value}, self.cfg)
            self.assertEqual(score['bonus'], 0)
            self.assertIn('retreats_survived', score['missing'])

    def test_default_behaviour_is_not_measurement(self):
        row = {'behaviour': {'suppress_then_move': 2}}
        self.assertEqual(doctrine.evaluate(row, self.cfg)['bonus'], 0)
        row['behaviour_observed'] = ['suppress_then_move']
        self.assertEqual(doctrine.evaluate(row, self.cfg)['bonus'], .25)

    def test_crash_and_no_contact_never_bonus(self):
        for extras in ({'crashed': True}, {'contact': False, 'won': 1}):
            self.assertEqual(doctrine.evaluate(dict(retreats_survived=2, **extras), self.cfg)['bonus'], 0)

    def test_opt_in_fitness_and_unchanged_crash_penalty(self):
        row = {'won': 0, 'retreats_survived': 2}
        base = fitness.round_score(row, {})
        self.assertEqual(fitness.round_score(row, {'doctrine': self.cfg}), base + .75)
        self.assertEqual(fitness.round_score(dict(row, crashed=True), {'doctrine': self.cfg}), -5)

    def test_profile_only_observed_eligible_tactics(self):
        data = {'schema': 'codex.demo_dataset/1', 'errors': [], 'sources': [], 'summary': [], 'fights': [
            {'lab': False, 'truncated': False, 'tactics': [{'tag': 'high_ground'}, {'tag': 'first_in_cover_overwatch'}]},
            {'lab': True, 'truncated': False, 'tactics': [{'tag': 'distract_and_flank'}]}]}
        p = doctrine.profile(data)
        self.assertEqual(len(p['terms']), 1)
        self.assertEqual((p['terms'][0]['metric'], p['terms'][0]['weight']), ('behaviour.suppress_then_move', 1))
        data['errors'] = [{'error': 'bad'}]
        with self.assertRaises(ValueError):
            doctrine.profile(data)

    def test_adapter_preserves_explicit_metric_presence(self):
        doc = {'schema': 'codex.pmc_arena/1', 'rounds': [{'winner': 'A', 'sides': {'A': {'retreats_survived': 1}}}]}
        adapter = ArenaAdapter({}, '.')
        row = adapter.parse_doc(doc)[0]
        self.assertEqual(row['behaviour_observed'], [])
        doc['rounds'][0]['sides']['A']['behaviour'] = {'metrics': {'suppress_then_move': {'value': 0}}}
        row = adapter.parse_doc(doc)[0]
        self.assertEqual(row['behaviour_observed'], ['suppress_then_move'])

    def test_unloaded_weights_fail_closed_and_paths_are_absolute(self):
        adapter = ArenaAdapter({'require_loaded_weights': True}, 'C:/lane')
        with self.assertRaisesRegex(ValueError, 'did not confirm'):
            adapter.parse_doc({'schema': 'codex.pmc_arena/1', 'weights': {'A': {'loaded': False}, 'B': {'loaded': True}}, 'rounds': []})
        job = {'weights_a': 'Saved/a.json', 'weights_b': 'Saved/b.json', 'opponent': 'champion', 'rounds': 1, 'seed': 7}
        adapter.cfg['arg_template'] = ['WeightsA={weights_a}', 'WeightsB={weights_b}']
        command = adapter.console_command(job, 'C:/lane/Saved/out.json')
        self.assertIn('WeightsA=C:/lane/Saved/a.json', command)
        self.assertIn('WeightsB=C:/lane/Saved/b.json', command)
        job['weights_a'] = 'Saved/has space.json'
        with self.assertRaisesRegex(ValueError, 'without spaces'):
            adapter.console_command(job, 'C:/lane/Saved/out.json')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'unrelated.json').write_text('{}')
            adapter = ArenaAdapter({'output_dir': '.', 'require_requested_output': True}, directory)
            self.assertIsNone(adapter.find_output(str(root / 'missing.json'), 0))

    def test_gate_includes_doctrine_and_rejects_unobserved_defaults(self):
        self.assertIn('behaviour.suppress_then_move', extract_used_metrics({'fitness': {'doctrine': self.cfg}}))
        self.assertIsNone(get_metric_value_from_round({'behaviour': {'suppress_then_move': 0}, 'behaviour_observed': []}, 'behaviour.suppress_then_move'))
        self.assertEqual(get_metric_value_from_round({'behaviour': {'metrics': {'suppress_then_move': {'value': 2}}}}, 'behaviour.suppress_then_move'), 2)


class WeekendTests(unittest.TestCase):
    def setUp(self):
        self.cfg = json.loads((Path(__file__).resolve().parents[1] / 'weekend_trainer.json').read_text())

    def test_204_paired_seeds_balanced_and_alternating(self):
        p = plan(self.cfg)
        self.assertEqual(len(p), 204)
        self.assertEqual(len({r['seed'] for r in p}), 204)
        self.assertTrue(all(sum(r['sector'] == s for r in p) == 34 for s in range(1, 7)))
        self.assertEqual(p, plan(self.cfg))
        self.assertNotEqual(p[0]['arms'], p[1]['arms'])

    def test_short_counts_only_with_explicit_trial(self):
        self.cfg['rounds_per_arm'] = 3
        with self.assertRaises(ValueError):
            plan(self.cfg)
        self.assertEqual(len(plan(self.cfg, 3)), 3)
        with self.assertRaises(ValueError):
            plan(self.cfg, 0)

    def test_captures_are_attacker_captures_not_defender_timeouts(self):
        rows = [dict(arm='candidate', pair=i, won=1, kills=2, role=role, end_reason=end)
                for i, (role, end) in enumerate([('attacker', 'objective'), ('defender', 'time_limit')])]
        rows += [dict(r, arm='baseline', won=.5) for r in rows]
        result = summarize(rows)
        self.assertEqual(result['arms']['candidate']['captures'], 1)
        self.assertEqual(result['arms']['baseline']['win_rate'], 0)
        self.assertEqual((result['complete_pairs'], result['candidate_only_wins']), (2, 2))

    def test_crashes_not_wins_or_valid_pairs(self):
        result = summarize([{'arm': 'candidate', 'pair': 0, 'crashed': True},
                            {'arm': 'baseline', 'pair': 0, 'won': 1}])
        self.assertEqual((result['complete_pairs'], result['arms']['candidate']['crashes']), (0, 1))

    def test_commands_use_frozen_sector_geometry_and_no_interactive_rounds(self):
        base = {'arena': {'engine_args': ['-LevelJson=old.json', '-nullrhi']}}
        original = copy.deepcopy(base)
        cfg = arena_for(base, self.cfg, Path('C:/lane/Saved/sector_1.json'))
        self.assertIn('-Sector=0', cfg['engine_args'])
        self.assertIn('-LevelJson=C:/lane/Saved/sector_1.json', cfg['engine_args'])
        self.assertIn('Time={time_limit_s}', cfg['arg_template'])
        self.assertEqual(base, original)

    def test_training_jobs_keep_sector_arguments(self):
        trainer = Trainer.__new__(Trainer)
        trainer.run_seed = 7
        trainer.cfg = {'evaluation': {'matches': [{'opponent': 'champion', 'jobs': 1, 'rounds': 1,
                                                  'engine_args': ['-LevelJson=C:/lane/Saved/sector_2.json']}]},
                       'arena': {'map': self.cfg['map'], 'time_limit_s': 120}}
        p = trainer._ga_plan('C:/lane/champion.json')
        jobs = trainer.make_jobs('candidate', 'C:/lane/weights.json', 0, 'eval', 'C:/lane/jobs', p)
        self.assertEqual(jobs[0]['engine_args'], ['-LevelJson=C:/lane/Saved/sector_2.json'])
        cfg = arena_for({'arena': {'engine_args': []}}, self.cfg, Path('C:/lane/Saved/sector_2.json'))
        cfg['engine_args'] = []
        command = ArenaAdapter(cfg, 'C:/lane').build_command(jobs[0], 'C:/lane/Saved/out.json', 'C:/lane/Saved/out.log')
        self.assertIn('-LevelJson=C:/lane/Saved/sector_2.json', command)

    def test_three_pair_mock_resume_and_input_freeze(self):
        # Full script integration without starting an engine or touching game content.
        real_root = weekend_trainer.ROOT
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'level.json').write_text('{"pmc_squads": [{"count":3}]}')
            (root / 'sectors.json').write_text(json.dumps({'sectors': [{'id': n, 'box_cm': {'min': [0, 0], 'max': [1000, 1000]}} for n in range(1, 7)]}))
            cfg = dict(self.cfg, level='level.json', sector_definitions='sectors.json')
            config = root / 'weekend.json'
            config.write_text(json.dumps(cfg))
            trainer_config = real_root / 'Scripts/Trainer/trainer_config.json'
            weights = real_root / 'Scripts/Trainer/fixtures/pmc_commander.sample.json'
            args = SimpleNamespace(run_id='test', config=str(config), trainer_config=str(trainer_config),
                                   champion=str(weights), candidate=str(weights), mock=True, trial=3, hours=1, plan_only=False)
            with patch.object(weekend_trainer, 'ROOT', root):
                self.assertEqual(weekend_trainer.run(args), 0)
                report_path = root / 'Saved/Trainer/test/morning.json'
                original = json.loads(report_path.read_text())
                self.assertEqual(original['complete_pairs'], 3)
                self.assertEqual(len(original['examples']), 3)
                self.assertEqual(weekend_trainer.run(args), 0)
                self.assertEqual(json.loads(report_path.read_text()), original)
                cfg['seed'] += 1
                config.write_text(json.dumps(cfg))
                with self.assertRaisesRegex(ValueError, 'resume inputs differ'):
                    weekend_trainer.run(args)


if __name__ == '__main__':
    unittest.main()
