#!/usr/bin/env python3
"""Tests for the agent chat topic limit (25, lead reset) and status posts."""

import os
import sys
import tempfile
import threading
import time
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat


class TestTopicLimitAndStatus(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = agent_chat.get_storage_dir(self.tmp.name)
        self.chat = os.path.join(self.dir, "chat.jsonl")

    def tearDown(self):
        self.tmp.cleanup()

    def post(self, sender, to, text, **kw):
        return agent_chat.post_message(self.dir, sender, to, "t", text, **kw)

    def test_limit_is_25(self):
        for i in range(25):
            self.assertIsNotNone(self.post("codex", "claude", f"m{i}"))
        self.assertIsNone(self.post("codex", "claude", "overflow"))

    def test_designer_and_lead_reset(self):
        for i in range(25):
            self.post("grok", "claude", f"m{i}")
        self.assertIsNone(self.post("grok", "claude", "x"))
        self.assertIsNotNone(self.post("claude", "grok", "lead reply"))
        self.assertIsNotNone(self.post("grok", "claude", "again"))
        for i in range(24):
            self.assertIsNotNone(self.post("grok", "claude", f"n{i}"))
        self.assertIsNone(self.post("grok", "claude", "over"))
        self.assertIsNotNone(self.post("designer", "all", "hi"))
        self.assertIsNotNone(self.post("grok", "claude", "ok"))

    def test_max_per_topic_override(self):
        self.post("codex", "claude", "a", max_per_topic=2)
        self.post("codex", "claude", "b", max_per_topic=2)
        self.assertIsNone(self.post("codex", "claude", "c", max_per_topic=2))

    def test_status_stored_and_not_counted(self):
        for i in range(30):
            self.assertIsNotNone(self.post("codex", "all", f"s{i}", status=True))
        msgs = agent_chat.read_all_messages(self.chat)
        self.assertTrue(all(m["kind"] == "status" for m in msgs))
        self.assertIsNotNone(self.post("codex", "claude", "real"))
        latest = agent_chat.latest_status_per_agent(msgs)
        self.assertEqual(latest["codex"]["text"], "s29")

    def test_read_default_hides_status(self):
        self.post("codex", "claude", "real")
        self.post("codex", "all", "041 1/2", status=True)
        import argparse
        import contextlib
        import io
        for with_status, expect in ((False, 1), (True, 2)):
            buf = io.StringIO()
            args = argparse.Namespace(root=self.tmp.name, since=None, sender=None, last=None, json=True, with_status=with_status)
            with contextlib.redirect_stdout(buf):
                agent_chat.cmd_read(args)
            import json
            self.assertEqual(len(json.loads(buf.getvalue())), expect)

    def test_wait_ignores_status(self):
        self.post("codex", "claude", "real")
        agent_chat.set_last_seen_id(self.dir, "claude", 1)
        self.post("codex", "claude", "progress", status=True)
        import argparse
        import contextlib
        import io
        buf = io.StringIO()
        args = argparse.Namespace(root=self.tmp.name, sender="claude", timeout=0.3, poll_interval=0.05, json=False)
        with contextlib.redirect_stdout(buf):
            agent_chat.cmd_wait(args)
        self.assertIn("NO_MESSAGES", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
