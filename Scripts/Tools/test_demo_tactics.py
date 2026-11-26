import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import demo_tactics as dt


class DemoTacticsTest(unittest.TestCase):
    def core_records(self):
        records = [
            {"type": "header", "v": 1, "session": "unit", "map": "L_Gen_Depot_1_X6"},
            {"type": "shot_hit", "t": 10.0, "who": "PMC_0", "whom": "Op_0", "dmg": 15.0, "dist": 1800.0},
            {"type": "cover_enter", "t": 12.0, "op": "Op_0", "height": "Low"},
            {"type": "stealth_on", "t": 13.0, "op": "Op_1"},
            {"type": "shot_hit", "t": 13.5, "who": "Op_0", "whom": "PMC_0", "dmg": 30.0, "dist": 1414.0},
            {"type": "order", "t": 13.9, "actor": "Op_1", "kind": "Cover", "ok": True},
            {
                "type": "snap",
                "t": 14.0,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1000, 0, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, -100, 0], "yaw": 0.0, "st": 2, "mv": True, "in_cover": False, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0], "yaw": -90}],
            },
            {
                "type": "snap",
                "t": 17.5,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1800, 1000, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, -100, 0], "yaw": 0.0, "st": 2, "mv": True, "in_cover": False, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0], "yaw": -90}],
            },
            {"type": "shot_hit", "t": 18.5, "who": "Op_0", "whom": "PMC_0"},
            {
                "type": "snap",
                "t": 20.5,
                "ops": [
                    {"id": "Op_0", "alive": 1, "p": [0, 0, 350], "yaw": 0.0, "st": 1, "mv": False, "in_cover": True, "stealth": False, "height": 350.0, "under_fire": 1.0, "scan_pitch": -15.0, "thr_n": 1},
                    {"id": "Op_1", "alive": 1, "p": [1000, 2000, 0], "yaw": 0.0, "st": 1, "mv": True, "in_cover": False, "stealth": True, "height": 0.0, "seen_by_enemy": 0, "scan_pitch": 20.0, "thr_n": 1},
                    {"id": "Op_2", "alive": 1, "p": [0, 0, 0], "yaw": 0.0, "st": 2, "mv": False, "in_cover": True, "stealth": False, "height": 0.0, "scan_pitch": 0.0, "thr_n": 0},
                ],
                "threats": [{"id": "PMC_0", "arch": "PMC", "p": [1000, 1000, 0], "yaw": -90}],
            },
            {"type": "cover_enter", "t": 21.0, "op": "Op_2", "height": "Low"},
            {"type": "kill", "t": 22.0, "killer": "Op_1", "victim": "PMC_0", "weapon": "m16", "dist": 1005.0, "was_victim_aware": False, "victim_covered_by_ally": False},
            {"type": "footer", "duration_s": 65.0},
        ]
        return records

    def test_detects_all_core_and_doctrine_tactics(self) -> None:
        rep = dt.analyze_records(self.core_records())
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

    def tags(self, records):
        return {t.tag for t in dt.analyze_records(records).tactics}

    def snap(self, t, **fields):
        op = dict(id="A", p=[0, 0, 400], yaw=0, thr_n=1, mv=True, st=2, cov=False)
        op.update(fields)
        return dict(type="snap", t=t, ops=[op], threats=[dict(id="E", p=[100, 0, 400])])

    def test_same_roof_is_not_high_ground(self):
        self.assertNotIn("high_ground", self.tags([self.snap(0, height=400)]))

    def test_missing_stealth_and_no_kills_are_not_flank_evidence(self):
        a = self.snap(0, p=[100, 0, 0])
        b = self.snap(2, p=[-100, 0, 0])
        a["threats"] = b["threats"] = [dict(id="E", p=[0, 0, 0])]
        self.assertNotIn("distract_and_flank", self.tags([a, b]))

    def test_already_in_cover_does_not_mean_fallback(self):
        self.assertNotIn("fallback_to_cover_after_contact", self.tags([self.snap(0, cov=True), self.snap(2, cov=True)]))

    def test_standing_interrupts_prone_crawl(self):
        self.assertNotIn("prone_then_crawl_to_cover", self.tags([
            self.snap(0), self.snap(1, st=0), self.snap(2, cov=True, p=[100, 0, 400], st=0)]))

    def test_cover_order_required_for_overwatch(self):
        r = self.snap(2, cov=True)
        r["ops"].append(dict(id="B", p=[10, 0, 400], mv=True))
        self.assertNotIn("first_in_cover_overwatch", self.tags([
            dict(type="shot_hit", t=1, who="A", whom="E"), r]))

    def test_round_boundaries_reset_crawl(self):
        self.assertNotIn("prone_then_crawl_to_cover", self.tags([
            self.snap(0), dict(type="round_end", t=1), dict(type="round_start", t=2, round=2),
            self.snap(3, cov=True, p=[100, 0, 400])]))

    def test_event_file_order_does_not_change_analysis(self):
        records = [self.snap(2, cov=True), self.snap(0), dict(type="damage", t=1, who="E", whom="A")]
        self.assertEqual(self.tags(records), self.tags(list(reversed(records))))
        self.assertIn("fallback_to_cover_after_contact", self.tags(records))

    def test_lure_requires_target_advance_and_open_ground(self):
        records = [dict(type="order", t=0, kind="Move", actor="A", target=dict(id="E"),
                        facts=dict(thr_d=100, thr_d_new=500)),
                   dict(type="kill", t=4, killer="B", victim="E", dist=3000)]
        self.assertNotIn("lure_then_snipe", self.tags(records))

    def test_intermittent_unwatched_gaps_do_not_accumulate(self):
        records = [self.snap(0), self.snap(3), self.snap(4, mv=False), self.snap(7)]
        self.assertNotIn("unwatched_flank_seconds", self.tags(records))

    def test_unrelated_victim_kill_cannot_complete_flank(self):
        records = self.core_records()
        for record in records:
            if record.get("type") == "kill":
                record["victim"] = "Unrelated"
        self.assertNotIn("distract_and_flank", self.tags(records))

    def test_late_kill_cannot_retroactively_complete_flank(self):
        records = self.core_records()
        for record in records:
            if record.get("type") == "kill":
                record["t"] = 100
        self.assertNotIn("distract_and_flank", self.tags(records))

    def test_legacy_missing_stealth_never_means_unseen(self):
        records = self.core_records()
        for record in records:
            for op in record.get("ops", []):
                op.pop("stealth", None)
                op.pop("seen_by_enemy", None)
        self.assertNotIn("distract_and_flank", self.tags(records))

    def test_distractor_must_engage_same_target(self):
        records = self.core_records()
        for record in records:
            if record.get("type") in ("shot_hit", "damage"):
                record["whom"] = "Elsewhere"
                record["who"] = "Op_0"
        self.assertNotIn("distract_and_flank", self.tags(records))

    def test_lure_requires_enemy_to_follow_retreat(self):
        records = [dict(type="snap", t=0, threats=[dict(id="E", p=[1000, 0, 0])]),
                   dict(type="order", t=1, actor="A", kind="Move", dest=[0, 0, 0], target=dict(id="E"),
                        facts=dict(thr_d=100, thr_d_new=500)),
                   dict(type="snap", t=2, threats=[dict(id="E", p=[500, 0, 0], in_cover=False)]),
                   dict(type="kill", t=3, killer="B", victim="E", dist=3000)]
        self.assertIn("lure_then_snipe", self.tags(records))
        records[2]["threats"][0]["p"] = [1500, 0, 0]
        self.assertNotIn("lure_then_snipe", self.tags(records))

    def test_fast_rear_flank_does_not_require_stealth(self):
        records = self.core_records()
        for record in records:
            for op in record.get("ops", []):
                op["stealth"] = False
        self.assertIn("flank_into_rear", self.tags(records))

    def test_approach_unseen_requires_explicit_zero_observers(self):
        records = [dict(type="shot_hit", t=0, who="A", whom="E"),
                   dict(type="snap", t=0, ops=[dict(id="A", p=[0, 0, 400]),
                        dict(id="B", p=[0, 0, 0], stealth=True, seen_by_enemy=0)],
                        threats=[dict(id="E", p=[1000, 0, 0])]),
                   dict(type="snap", t=2, ops=[dict(id="A", p=[0, 0, 400]),
                        dict(id="B", p=[600, 0, 0], stealth=True, seen_by_enemy=0)],
                        threats=[dict(id="E", p=[1000, 0, 0])])]
        self.assertIn("approach_unseen", self.tags(records))
        records[2]["ops"][1].pop("seen_by_enemy")
        self.assertNotIn("approach_unseen", self.tags(records))

    def test_active_look_direction_covers_sectors(self):
        records = []
        for t in (0, 3, 6, 9):
            ops = [dict(id=str(i), p=[t*100, i, 0], yaw=0, look_yaw=i*90, mv=True) for i in range(4)]
            records.append(dict(type="snap", t=t, ops=ops))
        self.assertNotIn("unwatched_flank_seconds", self.tags(records))

    def test_round_end_at_kill_timestamp_keeps_final_kill(self):
        records = self.core_records()
        records.insert(0, dict(type="round_end", t=22, round=1))
        report = dt.analyze_records(records)
        self.assertEqual(len(report.kills), 1)
        self.assertIn("distract_and_flank", {t.tag for t in report.tactics})


if __name__ == "__main__":
    unittest.main()
