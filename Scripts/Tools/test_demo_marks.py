import json
from pathlib import Path
import tempfile
import unittest

from demo_marks import choose_round, round_marks
from demo_tactics import analyze_records
from demo_dataset import recording


class RoundMarksTest(unittest.TestCase):
    def setUp(self):
        self.rows = [dict(type='header', map='test'), dict(type='round_start', t=0, round=1),
                     dict(type='round_end', t=90, round=1), dict(type='round_start', t=100, round=3)]

    def test_choice(self):
        self.assertIsNone(choose_round([], 0))
        self.assertEqual(choose_round(self.rows, 1), 1)
        self.assertIsNone(choose_round(self.rows, 1, previous=True))
        self.assertEqual(choose_round(self.rows, 95), 1)
        self.assertEqual(choose_round(self.rows, 95, previous=True), 1)
        self.assertEqual(choose_round(self.rows, 129.99), 1)
        self.assertEqual(choose_round(self.rows, 130), 3)
        self.assertEqual(choose_round(self.rows, 101, explicit=3), 3)
        self.assertEqual(choose_round(self.rows, 140, explicit=1), 1)
        self.assertIsNone(choose_round(self.rows, 140, explicit=2))
        for contact in [dict(type=k, t=100.5) for k in ('shot', 'shot_hit', 'damage', 'sighting')] + [
                dict(type='snap', t=100.5, threats=[{'id': 'enemy'}]),
                dict(type='snap', t=100.5, ops=[{'seen_by_enemy': 1}])]:
            rows = self.rows + [contact]
            self.assertEqual(choose_round(rows, 101), 3)
            self.assertEqual(choose_round(rows, 101, previous=True), 1)

    def test_late_marks_and_consumers(self):
        rows = self.rows + [dict(type='round_mark', t=105, round=1, value='bad'),
                           dict(type='round_mark', t=110, round=1, value='good'),
                           dict(type='footer', mark='good', duration_s=120)]
        self.assertEqual(round_marks(rows), {'1': 'good', '3': ''})
        self.assertEqual(analyze_records(rows).round_marks, {'1': 'good', '3': ''})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'test.jsonl'
            path.write_text('\n'.join(json.dumps(r) for r in rows), encoding='utf-8')
            self.assertEqual([r['mark'] for r in recording(path)['fights']], ['good', ''])
        legacy = self.rows + [dict(type='footer', mark='bad')]
        self.assertEqual(round_marks(legacy), {'1': 'bad', '3': 'bad'})
        self.assertEqual(round_marks([dict(type='footer', mark='good')]), {'legacy': 'good'})


if __name__ == '__main__':
    unittest.main()
