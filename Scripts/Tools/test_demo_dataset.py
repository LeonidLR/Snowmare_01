import json
from pathlib import Path
import tempfile
import unittest

from demo_dataset import build, fights, recording, summarize


HEADER = {'type': 'header', 'map': 'test', 'session': 'fixture', 'flags': {}}


class DatasetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def demo(self, records):
        path = self.root / 'real.jsonl'
        path.write_text('\n'.join(json.dumps(r) for r in [HEADER, *records]), encoding='utf-8')
        return path

    def test_aborted_footer_does_not_erase_round_victory(self):
        p = self.demo([{'type': 'round_start', 'round': 1, 't': 2},
                       {'type': 'round_end', 'round': 1, 't': 12, 'duration_s': 10, 'result': 'victory', 'kills_player': 3},
                       {'type': 'round_start', 'round': 2, 't': 20},
                       {'type': 'footer', 'duration_s': 40, 'result': 'ABORTED'}])
        a, b = recording(p)['fights']
        self.assertEqual((a['outcome'], a['duration_s'], a['kills_player']), ('victory', 10, 3))
        self.assertEqual((b['outcome'], b['duration_s'], b['kills_player']), ('unknown', 20, None))

    def test_no_cross_round_cover_detection_or_preamble(self):
        p = self.demo([{'type': 'snap', 't': 0, 'ops': [{'id': 'a', 'p': [0, 0, 0], 'cov': False, 'thr_n': 1}]},
                       {'type': 'round_start', 'round': 1, 't': 1},
                       {'type': 'snap', 't': 2, 'ops': [{'id': 'a', 'p': [0, 0, 0], 'cov': False, 'thr_n': 1}]},
                       {'type': 'round_end', 'round': 1, 't': 3, 'result': 'defeat'},
                       {'type': 'round_start', 'round': 2, 't': 3},
                       {'type': 'snap', 't': 4, 'ops': [{'id': 'a', 'p': [0, 0, 0], 'cov': True}]},
                       {'type': 'footer', 'duration_s': 5}])
        self.assertFalse(any(t['tag'] == 'fallback_to_cover_after_contact' for r in recording(p)['fights'] for t in r['tactics']))

    def test_repeated_round_ids_stay_separate(self):
        records = [HEADER]
        for t in (0, 20):
            records += [{'type': 'round_start', 'round': 1, 't': t}, {'type': 'round_end', 'round': 1, 't': t + 10, 'result': 'draw'}]
        self.assertEqual(len(list(fights(records))), 2)

    def test_legacy_incomplete_and_kills_are_unknown(self):
        row = recording(self.demo([{'type': 'footer', 'duration_s': 99, 'result': 'ABORTED'}]))['fights'][0]
        self.assertEqual((row['outcome'], row['duration_s'], row['kill_events'], row['kills_player']), ('unknown', 99, [], None))

    def test_missing_footer_keeps_observed_duration(self):
        row = recording(self.demo([{'type': 'round_start', 'round': 1, 't': 2}, {'type': 'snap', 't': 9}]))['fights'][0]
        self.assertEqual((row['duration_s'], row['outcome']), (7, 'unknown'))

    def test_notes_are_decoded_intent_not_performed_actions(self):
        p = self.demo([{'type': 'round_start', 'round': 1, 't': 0}, {'type': 'round_end', 'round': 1, 't': 10, 'result': 'victory'}])
        notes = {'schema': 'codex.demo_notes/1', 'coordinates': 'UE_cm_X_north_Y_east', 'recording': p.name, 'map': 'test',
                 'annotations': [{'id': 'a', 'kind': 'route', 't': 5, 'round': 1, 'author': 'Designer', 'unit': 'a',
                                  'label': 'go here', 'points': [[-500, 1150], [1500, 1150]]}]}
        p.with_suffix('.notes.json').write_text(json.dumps(notes), encoding='utf-8')
        layout, index = self.root / 'layout.json', self.root / 'index.json'
        layout.write_text(json.dumps({'buildings': [{'id': 'b', 'footprint': [[0, 0], [1000, 1000]]}]}), encoding='utf-8')
        index.write_text('{"2":"b"}', encoding='utf-8')
        row = recording(p, {'test': (layout, index)})['fights'][0]
        self.assertEqual(row['tactics'], [])
        self.assertEqual(row['notes'][0]['intent'], 'should_have')
        self.assertEqual(row['notes'][0]['decoded']['buildings_passed'][0]['side'], 'east')

    def test_summary_deduplicates_actors_and_excludes_unknown_lab_truncated(self):
        rows = [{'recording': 'a', 'outcome': o, 'completed': o != 'unknown', 'lab': lab, 'truncated': trunc,
                 'tactics': [{'tag': 'high_ground'}, {'tag': 'high_ground'}]}
                for o, lab, trunc in [('victory', False, False), ('defeat', False, False), ('draw', False, False),
                                     ('unknown', False, False), ('victory', True, False), ('victory', False, True)]]
        s = next(s for s in summarize(rows) if s['tag'] == 'high_ground')
        self.assertEqual((s['detections'], s['fights'], s['recordings'], s['completed_eligible']), (12, 6, 1, 3))
        self.assertAlmostEqual(s['win_rate'], 1/3)

    def test_bad_file_reported_and_good_file_survives(self):
        self.demo([{'type': 'footer', 'duration_s': 2}])
        (self.root / 'bad.jsonl').write_text('{broken', encoding='utf-8')
        data = build(self.root)
        self.assertEqual((len(data['sources']), len(data['errors']), len(data['fights'])), (1, 1, 1))

    def test_deterministic_and_source_read_only(self):
        p = self.demo([{'type': 'footer', 'duration_s': 2}])
        before = p.read_bytes()
        self.assertEqual(build(self.root), build(self.root))
        self.assertEqual(p.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
