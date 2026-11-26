import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_tactics as dt


class DemoTacticsTest(unittest.TestCase):
    def test_detects_all_core_and_doctrine_tactics(self) -> None:
        records = [
            {"type": "header", "v": 1, "session": "unit", "map": "L_Gen_Depot_1_X6"},
            {"type": "shot_hit", "t": 10.0, "who": "PMC_0", "whom": "Op_0", "dmg": 15.0, "dist": 1800.0},
            {"type": "cover_enter", "t": 12.0, "op": "Op_0", "height": "Low"},
            {"type": "stealth_on", "t": 13.0, "op": "Op_1"},
            {"type": "shot_hit", "t": 13.5, "who": "Op_0", "whom": "PMC_0", "dmg": 30.0, "dist": 1414.0},
            {
                "type": "snap",
                "t": 14.0,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1000, 0, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, -100, 0], "yaw": 0.0, "st": 2, "mv": True, "in_cover": False, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0]}],
            },
            {
                "type": "snap",
                "t": 17.5,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1800, 1000, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, -100, 0], "yaw": 0.0, "st": 2, "mv": True, "in_cover": False, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0]}],
            },
            {
                "type": "snap",
                "t": 20.5,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1000, 2000, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, -100, 0], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0]}],
            },
            {"type": "cover_enter", "t": 21.0, "op": "Op_2", "height": "Low"},
            {"type": "kill", "t": 22.0, "killer": "Op_1", "victim": "PMC_0", "weapon": "m16", "dist": 1005.0, "was_victim_aware": False, "victim_covered_by_ally": False},
            {"type": "footer", "duration_s": 65.0},
        ]
        rep = dt.analyze_records(records)
        tags = {t.tag for t in rep.tactics}
        self.assertIn("fallback_to_cover_after_contact", tags)
        self.assertIn("high_ground", tags)
        self.assertIn("distract_and_flank", tags)
        self.assertIn("flank_into_rear", tags)
        self.assertIn("prone_then_crawl_to_cover", tags)
        self.assertIn("first_in_cover_overwatch", tags)
        self.assertIn("unwatched_flank_seconds", tags)
        self.assertIn("scan_pitch_range", tags)
        self.assertAlmostEqual(rep.scan_pitch_range, 35.0)
        self.assertEqual(len(rep.kills), 1)
        self.assertEqual(rep.kills[0].killer, "Op_1")
        self.assertFalse(rep.kills[0].was_victim_aware)


if __name__ == "__main__":
    unittest.main()
