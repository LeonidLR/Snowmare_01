#!/usr/bin/env python3
"""
Unit tests for agent chat idle tracking and idle banner API.
Requirement: Designer decision B / task chat-idle-banner.
Run: python -m unittest discover -s Scripts/Tools/tests -p "test_*.py"
"""

import datetime
import json
import os
import sys
import tempfile
import time
import unittest

# Ensure Scripts/Tools is on sys.path
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TOOLS_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat
import agent_chat_server


class TestComputeIdleSeconds(unittest.TestCase):
    def test_empty_messages(self):
        idle = agent_chat.compute_idle_seconds([], now=1000.0)
        self.assertEqual(idle, {"gemini": 0, "codex": 0, "claude": 0})

    def test_single_direct_message(self):
        # Message to gemini at T=1000
        msg_ts = datetime.datetime.fromtimestamp(1000.0, datetime.timezone.utc).isoformat()
        messages = [
            {"id": 1, "from": "designer", "to": "gemini", "text": "ping", "ts": msg_ts}
        ]

        # Evaluated at T=1300 (300s later, <= 600s)
        idle_300 = agent_chat.compute_idle_seconds(messages, now=1300.0)
        self.assertEqual(idle_300["gemini"], 300)
        self.assertEqual(idle_300["codex"], 0)
        self.assertEqual(idle_300["claude"], 0)

        # Evaluated at T=1700 (700s later, > 600s, sleeping!)
        idle_700 = agent_chat.compute_idle_seconds(messages, now=1700.0)
        self.assertEqual(idle_700["gemini"], 700)
        self.assertEqual(idle_700["codex"], 0)
        self.assertEqual(idle_700["claude"], 0)

    def test_answered_message_resets_idle(self):
        # Msg 1: designer -> gemini at T=1000
        # Msg 2: gemini -> designer at T=1400
        ts1 = datetime.datetime.fromtimestamp(1000.0, datetime.timezone.utc).isoformat()
        ts2 = datetime.datetime.fromtimestamp(1400.0, datetime.timezone.utc).isoformat()
        messages = [
            {"id": 1, "from": "designer", "to": "gemini", "text": "work?", "ts": ts1},
            {"id": 2, "from": "gemini", "to": "designer", "text": "working!", "ts": ts2},
        ]
        idle = agent_chat.compute_idle_seconds(messages, now=2000.0)
        self.assertEqual(idle["gemini"], 0)

    def test_oldest_unanswered_message_selected(self):
        # Msg 1: designer -> claude at T=1000
        # Msg 2: codex -> claude at T=1200
        ts1 = datetime.datetime.fromtimestamp(1000.0, datetime.timezone.utc).isoformat()
        ts2 = datetime.datetime.fromtimestamp(1200.0, datetime.timezone.utc).isoformat()
        messages = [
            {"id": 1, "from": "designer", "to": "claude", "text": "first", "ts": ts1},
            {"id": 2, "from": "codex", "to": "claude", "text": "second", "ts": ts2},
        ]
        # At T=1800, Claude hasn't answered. Oldest unanswered message is Msg 1 (T=1000).
        idle = agent_chat.compute_idle_seconds(messages, now=1800.0)
        self.assertEqual(idle["claude"], 800)  # 1800 - 1000 = 800s
        self.assertEqual(idle["gemini"], 0)
        self.assertEqual(idle["codex"], 0)

    def test_broadcast_message_to_all(self):
        # Msg 1: codex -> all at T=1000
        # Codex itself should not become idle from its own post!
        ts1 = datetime.datetime.fromtimestamp(1000.0, datetime.timezone.utc).isoformat()
        messages = [
            {"id": 1, "from": "codex", "to": "all", "text": "status update", "ts": ts1}
        ]
        idle = agent_chat.compute_idle_seconds(messages, now=1650.0)
        self.assertEqual(idle["codex"], 0)
        self.assertEqual(idle["gemini"], 650)
        self.assertEqual(idle["claude"], 650)

    def test_partial_replies_to_broadcast(self):
        # Msg 1: designer -> all at T=1000
        # Msg 2: gemini -> designer at T=1100
        ts1 = datetime.datetime.fromtimestamp(1000.0, datetime.timezone.utc).isoformat()
        ts2 = datetime.datetime.fromtimestamp(1100.0, datetime.timezone.utc).isoformat()
        messages = [
            {"id": 1, "from": "designer", "to": "all", "text": "wake up", "ts": ts1},
            {"id": 2, "from": "gemini", "to": "designer", "text": "awake", "ts": ts2},
        ]
        idle = agent_chat.compute_idle_seconds(messages, now=1900.0)
        # Gemini replied at T=1100, so gemini is not idle
        self.assertEqual(idle["gemini"], 0)
        # Claude and Codex never replied to Msg 1 at T=1000
        self.assertEqual(idle["claude"], 900)  # 1900 - 1000 = 900s
        self.assertEqual(idle["codex"], 900)


class TestTemporaryStorageIdle(unittest.TestCase):
    def test_get_idle_dict_from_disk(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            chat_file = os.path.join(temp_dir, "chat.jsonl")
            now_ts = time.time()
            m1_time = now_ts - 720.0  # 12 minutes ago (> 600s)
            ts_str = datetime.datetime.fromtimestamp(m1_time, datetime.timezone.utc).isoformat()
            
            with open(chat_file, "w", encoding="utf-8") as f:
                f.write(json.dumps({
                    "id": 1,
                    "from": "claude",
                    "to": "codex",
                    "topic": "stealth",
                    "text": "codex please check",
                    "ts": ts_str
                }) + "\n")

            idle = agent_chat.get_idle_dict(temp_dir, now=now_ts)
            self.assertGreaterEqual(idle["codex"], 719)
            self.assertEqual(idle["claude"], 0)
            self.assertEqual(idle["gemini"], 0)


class TestServerHandlerIdle(unittest.TestCase):
    def test_handler_endpoints(self):
        import http.server
        import io
        import socketserver
        import urllib.request

        with tempfile.TemporaryDirectory() as temp_dir:
            chat_file = os.path.join(temp_dir, "chat.jsonl")
            now_ts = time.time()
            ts_str = datetime.datetime.fromtimestamp(now_ts - 700.0, datetime.timezone.utc).isoformat()
            with open(chat_file, "w", encoding="utf-8") as f:
                f.write(json.dumps({
                    "id": 1,
                    "from": "designer",
                    "to": "claude",
                    "topic": "test",
                    "text": "claude please respond",
                    "ts": ts_str
                }) + "\n")

            # Create ephemeral server on random free port
            class TempHandler(agent_chat_server.AgentChatHandler):
                pass
            TempHandler.storage_dir = temp_dir
            TempHandler.html_path = os.path.abspath(os.path.join(temp_dir, "index.html"))
            with open(TempHandler.html_path, "w", encoding="utf-8") as f:
                f.write("<html><body>Test</body></html>")

            httpd = http.server.HTTPServer(("127.0.0.1", 0), TempHandler)
            port = httpd.server_address[1]

            import threading
            t = threading.Thread(target=httpd.serve_forever)
            t.daemon = True
            t.start()

            try:
                # Test /api/messages returns idle
                req = urllib.request.Request(f"http://127.0.0.1:{port}/api/messages")
                with urllib.request.urlopen(req) as resp:
                    self.assertEqual(resp.status, 200)
                    body = json.loads(resp.read().decode("utf-8"))
                    self.assertIn("messages", body)
                    self.assertIn("idle", body)
                    self.assertGreaterEqual(body["idle"]["claude"], 699)

                # Test /api/presence returns idle
                req = urllib.request.Request(f"http://127.0.0.1:{port}/api/presence")
                with urllib.request.urlopen(req) as resp:
                    self.assertEqual(resp.status, 200)
                    body = json.loads(resp.read().decode("utf-8"))
                    self.assertIn("idle", body)
                    self.assertGreaterEqual(body["idle"]["claude"], 699)

                # Test /api/idle endpoint
                req = urllib.request.Request(f"http://127.0.0.1:{port}/api/idle")
                with urllib.request.urlopen(req) as resp:
                    self.assertEqual(resp.status, 200)
                    body = json.loads(resp.read().decode("utf-8"))
                    self.assertIn("idle", body)
                    self.assertGreaterEqual(body["idle"]["claude"], 699)

                # Test same-origin check still rejects foreign origin
                bad_req = urllib.request.Request(f"http://127.0.0.1:{port}/api/idle", headers={"Origin": "http://evil.com"})
                try:
                    with urllib.request.urlopen(bad_req) as resp:
                        self.fail("Should have failed with 403")
                except urllib.error.HTTPError as e:
                    self.assertEqual(e.code, 403)
            finally:
                httpd.shutdown()
                httpd.server_close()


if __name__ == "__main__":
    unittest.main()
