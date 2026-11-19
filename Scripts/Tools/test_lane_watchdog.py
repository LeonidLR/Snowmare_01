"""Unit tests for lane_watchdog.evaluate (python -m pytest Scripts/Tools/test_lane_watchdog.py or python this file)."""
import datetime as dt
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lane_watchdog as w  # noqa: E402

NOW = dt.datetime(2026, 11, 19, 15, 0, tzinfo=dt.timezone.utc)


def ago(minutes):
    return NOW - dt.timedelta(minutes=minutes)


class EvaluateTest(unittest.TestCase):
    def test_active_recent_commit_ok(self):
        self.assertEqual(w.evaluate({"id": "a", "state": "active"}, ago(10), None, NOW)["verdict"], "ok")

    def test_active_quiet_is_stall(self):
        r = w.evaluate({"id": "a", "state": "active", "max_quiet_min": 45}, ago(60), ago(50), NOW)
        self.assertEqual(r["verdict"], "stalled")

    def test_chat_post_counts_as_activity(self):
        self.assertEqual(w.evaluate({"id": "a", "state": "active"}, ago(300), ago(5), NOW)["verdict"], "ok")

    def test_no_signal_at_all_is_stall(self):
        self.assertEqual(w.evaluate({"id": "a", "state": "active"}, None, None, NOW)["verdict"], "stalled")

    def test_blocked_without_reason_is_stall(self):
        self.assertEqual(w.evaluate({"id": "a", "state": "blocked"}, ago(1), None, NOW)["verdict"], "stalled")

    def test_waiting_with_reason_ok_until_limit(self):
        lane = {"id": "a", "state": "waiting", "reason": "worker busy", "max_wait_min": 120}
        self.assertEqual(w.evaluate(lane, ago(100), None, NOW)["verdict"], "ok")
        self.assertEqual(w.evaluate(lane, ago(130), None, NOW)["verdict"], "stalled")

    def test_done_and_paused_ignored(self):
        for state in ("done", "paused-by-designer"):
            self.assertEqual(w.evaluate({"id": "a", "state": state}, None, None, NOW)["verdict"], "ignored")

    def test_heartbeat_counts(self):
        self.assertEqual(w.evaluate({"id": "a", "state": "active"}, None, None, NOW, heartbeat_ts=ago(5))["verdict"], "ok")

    def test_window(self):
        lane = {"id": "a", "state": "active", "window": "16:55-07:45"}
        night = dt.datetime(2026, 11, 19, 23, 0)
        day = dt.datetime(2026, 11, 19, 12, 0)
        self.assertEqual(w.evaluate(lane, None, None, NOW, local_now=day)["verdict"], "ignored")
        self.assertEqual(w.evaluate(lane, None, None, NOW, local_now=night)["verdict"], "stalled")


if __name__ == "__main__":
    unittest.main()
