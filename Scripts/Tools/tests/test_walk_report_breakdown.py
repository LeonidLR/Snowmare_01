#!/usr/bin/env python3
"""walk_report_breakdown: the hall-question legs (BGEN_KIT_PLANS_2026-11-21.md question 7) are split from real defects; the gate is untouched."""

import os
import sys
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import walk_report_breakdown as wrb

LAYOUT = {"buildings": [{"id": "B", "rooms": [
    {"id": "landing", "use": "landing", "bounds": {"min": [0, 0], "max": [400, 600]}},
    {"id": "hall", "use": "hall", "bounds": {"min": [0, 0], "max": [250, 250]}},
    {"id": "living", "use": "living", "bounds": {"min": [0, 0], "max": [500, 500]}},
]}]}


def leg(kind, to, ok, reason=""):
    return {"kind": kind, "from": "landing", "to": to, "pass": ok, "reason": reason}


class TestBreakdown(unittest.TestCase):
    def report(self, entries, walk=(), path_pass=True):
        return {"map": "M", "walk_legs_passed": 1, "walk_legs": 1, "entry_legs_passed": sum(e["pass"] for e in entries),
                "buildings": [{"id": "B", "pass": all(e["pass"] for e in entries) and path_pass and all(w["pass"] for w in walk),
                               "path_pass": path_pass, "walk": list(walk), "entries": entries,
                               "plan_roles": {"entries_without_unit_entry_role": [], "unit_entry_not_an_entry": [], "unresolved_entries": []}}]}

    def test_follower_stop_into_hall_is_the_question(self):
        rep = self.report([leg("entry_solo", "hall", True), leg("entry_squad", "hall", False, "follower Engineer STOPPED (no move order running)")])
        res = wrb.breakdown(rep, LAYOUT)
        self.assertEqual((res["passed"], res["passed_without_hall_legs"], res["hall_question_legs"]), (0, 1, 1))

    def test_ratio_into_hall_is_the_question(self):
        rep = self.report([leg("entry_squad", "hall", False, "squad 3.6 s > 2 x solo 1.3 s (ratio 2.88)")])
        self.assertEqual(wrb.breakdown(rep, LAYOUT)["passed_without_hall_legs"], 1)

    def test_off_navmesh_is_a_defect_even_into_a_hall(self):
        rep = self.report([leg("entry_squad", "hall", False, "start landing off navmesh")])
        res = wrb.breakdown(rep, LAYOUT)
        self.assertEqual((res["passed_without_hall_legs"], res["hall_question_legs"]), (0, 0))

    def test_big_room_and_solo_failures_stay_failures(self):
        rep = self.report([leg("entry_squad", "living", False, "follower Engineer STUCK (move active)"),
                           leg("entry_solo", "hall", False, "follower Engineer STUCK")])
        self.assertEqual(wrb.breakdown(rep, LAYOUT)["passed_without_hall_legs"], 0)

    def test_walk_and_path_failures_stay(self):
        rep = self.report([leg("entry_squad", "hall", False, "follower X STOPPED")], walk=[{"from": "s0", "to": "s1", "pass": False, "reason": "no complete navmesh path"}])
        self.assertEqual(wrb.breakdown(rep, LAYOUT)["passed_without_hall_legs"], 0)
        rep = self.report([leg("entry_squad", "hall", False, "follower X STOPPED")], path_pass=False)
        res = wrb.breakdown(rep, LAYOUT)
        self.assertEqual(res["passed_without_hall_legs"], 0)
        self.assertTrue(res["rows"][0]["other_failures"][0].startswith("path"))


if __name__ == "__main__":
    unittest.main()
