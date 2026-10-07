"""Unit tests of the Jev AI coach's pure parts (no game, no API): ai_tuning.json merge that keeps the user's hand-set
cvars, the stealth log parsing and the stealth objective.  Run: python -m unittest Scripts/Tools/test_jev_ai_coach.py
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import jev_ai_coach as coach  # noqa: E402

LOG = """
[Stealth] bot sneaking: 3 patrol enemies, posture passive, ambush range x0.70, alarm 0.50, limit 240 s, trap 1 (seed 2)
[Stealth] bot trap laid at 31.0 s, the squad backs off
[Stealth] search started by BP_Enemy_Hound_C_1 at 52.0 s
[Stealth] search started by BP_Enemy_Marksman_C_1 at 52.0 s
[Stealth] bot sees a search at 52.2 s (BP_Enemy_Hound_C_1 20 m away)
[Stealth] bot hide at 52.2 s (stance prone, target BP_Enemy_Marksman_C_1 21 m, suspicion 0.00, search 1, cover 0)
[Stealth] detection by hearing at 61.5 s (BP_Enemy_Hound_C_1, search found the squad)
[Stealth] outcome detected at 61.5 s (sneaking 60.0 s, first detection 61.5 s, searches seen 1, trap 1, stance changes 4, decisions 300, seed 2)
[Stealth] detection by damage at 62.0 s (BP_Enemy_Marksman_C_0)
[Stealth] detection by sight at 70.0 s (BP_Enemy_Marksman_C_0)
"""


class TuningMergeTest(unittest.TestCase):
    def test_wave_editor_keys_are_the_users(self):
        doc = {"comment": coach.COACH_COMMENT + "; applied at game start",
               "cvars": {"Codex.Bot.AssaultClearRadius": "1800", "Codex.Perception.HearingScale": "0.8"}}
        # Old coach file: it owned its bot knob; the stealth key came from the Wave Editor.
        self.assertEqual(coach.user_set_keys(doc), ["Codex.Perception.HearingScale"])
        doc["coach_cvars"] = {"Codex.Bot.AssaultClearRadius": "1800", "Codex.Perception.HearingScale": "1.2"}
        # Recorded coach keys: a value changed since (0.8 != 1.2) is the user's now.
        self.assertEqual(coach.user_set_keys(doc), ["Codex.Perception.HearingScale"])

    def test_merge_keeps_user_and_unknown_keys(self):
        doc = {"cvars": {"Codex.Perception.HearingScale": "0.8", "Codex.Horde.Enabled": "0"}, "custom": {"keep": True}}
        users = coach.user_set_keys(doc)
        merged, applied, skipped = coach.merge_tuning(doc, {"Codex.Perception.HearingScale": "1.1", "Codex.Perception.SightRangeScale": "0.9"}, users)
        self.assertEqual(merged["cvars"]["Codex.Perception.HearingScale"], "0.8")  # never overwritten
        self.assertEqual(merged["cvars"]["Codex.Horde.Enabled"], "0")
        self.assertEqual(merged["cvars"]["Codex.Perception.SightRangeScale"], "0.9")
        self.assertEqual(merged["custom"], {"keep": True})
        self.assertEqual(applied, {"Codex.Perception.SightRangeScale": "0.9"})
        self.assertEqual(skipped, {"Codex.Perception.HearingScale": "1.1"})
        self.assertEqual(merged["coach_cvars"], {"Codex.Perception.SightRangeScale": "0.9"})
        # Now the coach owns SightRangeScale: a later run may change or remove it.
        again, applied2, _ = coach.merge_tuning(merged, {"Codex.Perception.SightRangeScale": None}, coach.user_set_keys(merged))
        self.assertNotIn("Codex.Perception.SightRangeScale", again["cvars"])
        self.assertEqual(applied2, {"Codex.Perception.SightRangeScale": None})

    def test_start_values_and_dpcvars(self):
        cvars = {"Codex.Perception.HearingScale": "0.8", "Codex.Perception.HearingOcclusion": "0.4"}
        values = coach.knob_values_from(cvars)
        self.assertEqual(values["hearing"], 0.8)
        self.assertEqual(values["sight_range"], 1.0)
        line = coach.dpcvars(values, cvars)
        self.assertIn("Codex.Perception.HearingScale=0.8", line)
        self.assertIn("Codex.Perception.HearingOcclusion=0.4", line)  # not a knob, still passed through
        self.assertNotIn("SightRangeScale", line)  # default and not in the file: the C++ default stays


class StealthParseTest(unittest.TestCase):
    def test_facts(self):
        f = coach.stealth_facts(LOG)
        self.assertTrue(f["stealth_bot"])
        self.assertEqual(f["stealth_outcome"], "detected")
        self.assertEqual(f["first_detection_s"], 61.5)
        self.assertEqual(f["first_detection_sense"], "hearing")
        self.assertEqual(f["detections_sight"], 0)  # after the fight started: no stealth failure
        self.assertEqual(f["detections_hearing"], 1)
        self.assertEqual(f["searches_started"], 2)
        self.assertEqual(f["searches_found_squad"], 1)
        self.assertEqual(f["traps_laid"], 1)
        self.assertEqual(f["bot_hides"], 1)

    def test_shots_of_the_strike_are_no_detection(self):
        log = ("[Stealth] bot sneaking: 3 patrol enemies\n"
               "[Stealth] bot ambush at 35.3 s (in_position, target BP_Enemy_Hound_C_1 14 m, leader cover 0, stance prone, sneaking 31.8 s)\n"
               "[Stealth] detection by hearing at 35.3 s (BP_Enemy_Marksman_C_1)\n"
               "[Stealth] outcome ambush_in_position at 35.3 s (sneaking 31.8 s, first detection none)\n")
        f = coach.stealth_facts(log)
        self.assertEqual(f["stealth_outcome"], "ambush_in_position")
        self.assertEqual(f["ambush_reason"], "in_position")
        self.assertIsNone(f["first_detection_s"])
        self.assertEqual(f["detections_hearing"], 0)

    def test_summary_and_objective(self):
        runs = []
        for outcome in ("detected", "ambush_in_position", "ambush_in_position", "ambush_forced"):
            r = coach.stealth_facts(LOG.replace("outcome detected", "outcome " + outcome))
            runs.append(r)
        s = coach.stealth_summary(runs)
        self.assertEqual(s["stealth_runs"], 4)
        self.assertEqual(s["stealth_detected_share"], 0.25)
        self.assertEqual(s["stealth_ambush_share"], 0.5)
        self.assertEqual(s["stealth_forced_share"], 0.25)
        base = {"win_rate": 0.5, "avg_waves_cleared": 1.0}
        good = {**base, **s, "stealth_detected_share": 0.5, "avg_first_detection_s": 60.0}
        never = {**base, **s, "stealth_detected_share": 0.0, "avg_first_detection_s": None}
        self.assertGreater(coach.objective({"summary": good}, "stealth"), coach.objective({"summary": never}, "stealth"))
        instant = dict(good, avg_first_detection_s=5.0)
        self.assertGreater(coach.objective({"summary": good}, "stealth"), coach.objective({"summary": instant}, "stealth"))


if __name__ == "__main__":
    unittest.main()
