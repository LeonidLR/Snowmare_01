#!/usr/bin/env python3
"""Grok (xAI) as an agent chat participant: post, read, wait, topic limit."""

import os
import sys
import tempfile
import unittest
import io
import contextlib
from types import SimpleNamespace

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat


class TestGrokParticipant(unittest.TestCase):
    def test_post_read_wait_and_topic_limit(self):
        with tempfile.TemporaryDirectory() as root:
            storage = agent_chat.get_storage_dir(root)
            # post as / to grok
            self.assertIsNotNone(agent_chat.post_message(storage, "grok", "claude", "t", "hi"))
            self.assertIsNotNone(agent_chat.post_message(storage, "claude", "grok", "t", "hello"))
            # read: grok's message is in the chat log
            with open(os.path.join(storage, "chat.jsonl"), encoding="utf-8") as f:
                self.assertIn('"from": "grok"', f.read())
            # topic limit counts grok: 10 agent messages block the 11th
            with tempfile.TemporaryDirectory() as root2:
                st2 = agent_chat.get_storage_dir(root2)
                for i in range(10):
                    self.assertIsNotNone(agent_chat.post_message(st2, "grok", "all", "lim", f"m{i}"))
                self.assertIsNone(agent_chat.post_message(st2, "grok", "all", "lim", "over"))
                agent_chat.post_message(st2, "designer", "all", "lim", "go")
                self.assertIsNotNone(agent_chat.post_message(st2, "grok", "all", "lim", "after"))

    def test_invalid_still_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            with self.assertRaises(ValueError):
                agent_chat.post_message(agent_chat.get_storage_dir(root), "nobody", "all", "t", "x")

    def test_wait_accepts_grok(self):
        with tempfile.TemporaryDirectory() as root:
            args = SimpleNamespace(root=root, sender="grok", timeout=0, topic=None, interval=0.1)
            # must not fail with "Invalid participant"
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                try:
                    agent_chat.cmd_wait(args)
                except Exception:
                    pass
            self.assertNotIn("Invalid participant", err.getvalue())


if __name__ == "__main__":
    unittest.main()
