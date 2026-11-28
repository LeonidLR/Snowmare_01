import copy
import math
import unittest
from demo_notes import buildings_passed, decode, load_json, load_map, DEFAULT_MAP, markdown, side_of_building

B = {'id': 'house', 'footprint': [[0, 0], [1000, 1000]], 'height': 300}
INDEX = {'2': 'house'}


class DemoNotesTests(unittest.TestCase):
    def test_ue_cardinal_axes(self):
        for p, side in [([500, 1150], 'east'), ([500, -150], 'west'), ([1150, 500], 'north'), ([-150, 500], 'south')]:
            with self.subTest(side=side):
                self.assertEqual(side_of_building(p, B)[:2], (side, 150))

    def test_sparse_route_passes_east_150cm(self):
        facts = buildings_passed([[-500, 1150], [1500, 1150]], [B], INDEX)
        self.assertEqual(len(facts), 1)
        self.assertEqual((facts[0]['number'], facts[0]['side'], facts[0]['min_wall_distance_m']), ('2', 'east', 1.5))

    def test_inside_corner_and_endpoint_not_passes(self):
        for points in [([100, 100], [900, 900]), ([1200, 1200], [1500, 1500]), ([500, 1600], [500, 1100]), ([1001, 1100], [999, 1100])]:
            self.assertEqual(buildings_passed(points, [B], INDEX), [])

    def test_far_route_not_pass(self):
        self.assertEqual(buildings_passed([[0, 1501], [1000, 1501]], [B], INDEX), [])

    def test_rotated_building_pass_uses_real_wall_and_world_cardinal(self):
        building = {**B, 'rotation': 60}
        def world(p):
            x, y = p[0] - 500, p[1] - 500
            a = math.radians(60)
            return [500 + x * math.cos(a) - y * math.sin(a), 500 + x * math.sin(a) + y * math.cos(a)]
        route = [world([-500, 1150]), world([1500, 1150])]
        facts = buildings_passed(route, [building], INDEX)
        self.assertEqual(len(facts), 1)
        self.assertEqual(facts[0]['side'], 'south')
        self.assertAlmostEqual(facts[0]['min_wall_distance_m'], 1.5)
        report = decode(self.notes(points=route), {'buildings': [building]}, INDEX)
        self.assertEqual(report['annotations'][0]['building_footprints_crossed'], [])

    def test_threshold_and_direction_independence(self):
        for points in [[[0, 1500], [1000, 1500]], [[1000, 1500], [0, 1500]]]:
            self.assertEqual(buildings_passed(points, [B], INDEX)[0]['side'], 'east')

    def test_separate_passes_not_merged_across_distance(self):
        facts = buildings_passed([[0, 1150], [400, 1150], [400, 3000], [600, 3000], [600, 1150], [1000, 1150]], [B], INDEX)
        self.assertEqual(len(facts), 2)

    def notes(self, kind='route', points=None):
        return {'schema': 'codex.demo_notes/1', 'coordinates': 'UE_cm_X_north_Y_east', 'recording': 'real.jsonl', 'map': 'map',
                'annotations': [{'id': 'a', 'kind': kind, 't': 134, 'author': 'Designer', 'unit': 'Commander', 'label': 'Plan',
                                 'points': points or [[-500, 1150], [1500, 1150]]}]}

    def test_route_length_cover_and_open_crossing(self):
        report = decode(self.notes(points=[[0, 0], [4000, 0]]), {'buildings': [], 'covers': [{'id': 'cover', 'pos': [0, 0], 'size': [200, 100, 100], 'kind': 'crate'}]}, {})
        a = report['annotations'][0]
        self.assertEqual(a['length_m'], 40)
        self.assertEqual(a['covers_touched'][0]['id'], 'cover')
        self.assertEqual(a['open_ground_crossings'][0]['from_m'], 6.25)

    def test_rotated_cover_sparse_segment(self):
        a = decode(self.notes(points=[[-300, 600], [300, 600]]), {'buildings': [], 'covers': [{'id': 'long', 'pos': [0, 0], 'yaw': 90, 'size': [1200, 100, 100]}]}, {})['annotations'][0]
        self.assertEqual(len(a['covers_touched']), 1)

    def test_no_roof_inference_from_label_or_xy(self):
        notes = self.notes('zone', [[500, 500], [600, 500]])
        notes['annotations'][0]['label'] = 'overwatch from roof'
        layout = {'buildings': [B]}
        report = decode(notes, layout, INDEX)
        self.assertFalse(report['annotations'][0]['buildings_at_centre'][0]['roof'])
        notes['annotations'][0]['height_cm'] = 300
        self.assertTrue(decode(notes, layout, INDEX)['annotations'][0]['buildings_at_centre'][0]['roof'])

    def test_inside_crossing_reported_as_unknown_clearance(self):
        report = decode(self.notes(points=[[100, 100], [900, 900]]), {'buildings': [B]}, INDEX)
        a = report['annotations'][0]
        self.assertEqual(a['buildings_passed'], [])
        self.assertEqual(a['building_footprints_crossed'], ['house'])
        self.assertIn('floor/path clearance unknown', markdown(report))

    def test_decoder_preserves_input_and_writes_facts(self):
        notes = self.notes()
        before = copy.deepcopy(notes)
        report = decode(notes, {'buildings': [B]}, INDEX)
        self.assertEqual(notes, before)
        self.assertIn('t=02:14', markdown(report))
        self.assertIn('EAST side, 1.5 m', markdown(report))

    def test_real_map_index_complete_unique_and_west_to_east(self):
        layout = load_map(DEFAULT_MAP)
        index = load_json(DEFAULT_MAP.with_suffix('.buildings_index.json'))
        self.assertEqual(set(index.values()), {b['id'] for b in layout['buildings']})
        self.assertEqual(len(index), len(set(index.values())))
        buildings = {b['id']: b for b in layout['buildings']}
        centres = [sum(p[1] for p in buildings[index[str(i)]]['footprint']) / 2 for i in range(1, len(index) + 1)]
        self.assertEqual(centres, sorted(centres))

    def test_coordinate_mismatch_rejected(self):
        notes = self.notes()
        notes['coordinates'] = 'pixels'
        with self.assertRaises(ValueError):
            decode(notes, {}, {})

    def test_tag_printed_and_grouped_with_legacy_fallback(self):
        notes = {
            'schema': 'codex.demo_notes/1',
            'coordinates': 'UE_cm_X_north_Y_east',
            'recording': 'real.jsonl',
            'map': 'map',
            'annotations': [
                {'id': 'n1', 'kind': 'comment', 't': 10, 'author': 'Designer', 'unit': 'BP_Operative_C_0', 'label': 'Old untagged', 'points': [[0, 0]]},
                {'id': 'n2', 'kind': 'comment', 't': 20, 'author': 'Designer', 'unit': 'BP_Operative_C_1', 'label': 'Stuck on stair', 'tag': 'баг', 'points': [[100, 100]]},
                {'id': 'n3', 'kind': 'comment', 't': 30, 'author': 'Designer', 'unit': 'BP_Operative_C_2', 'label': 'Good flank', 'tag': 'хорошо', 'points': [[200, 200]]},
                {'id': 'n4', 'kind': 'comment', 't': 40, 'author': 'Designer', 'unit': 'BP_Operative_C_2', 'label': 'Second bug', 'tag': 'баг', 'points': [[300, 300]]},
            ],
        }
        report = decode(notes, {'buildings': []}, {})
        self.assertEqual([a['tag'] for a in report['annotations']], [None, 'баг', 'хорошо', 'баг'])
        self.assertEqual(report['by_tag'], {
            'хорошо': {'count': 1, 'ids': ['n3']},
            'баг': {'count': 2, 'ids': ['n2', 'n4']},
            'untagged': {'count': 1, 'ids': ['n1']},
        })
        md = markdown(report)
        self.assertIn('## хорошо (1)', md)
        self.assertIn('## баг (2)', md)
        self.assertIn('## untagged (1)', md)
        self.assertIn('[баг]', md)
        self.assertIn('[хорошо]', md)
        self.assertLess(md.index('## хорошо (1)'), md.index('## баг (2)'))
        self.assertLess(md.index('## баг (2)'), md.index('## untagged (1)'))


if __name__ == '__main__':
    unittest.main()
