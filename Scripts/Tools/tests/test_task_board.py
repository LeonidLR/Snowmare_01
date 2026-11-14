#!/usr/bin/env python3
"""Unit and HTTP integration tests for Scripts/Tools/task_board.py and agent_chat_server.py."""
from __future__ import annotations

import datetime as dt
import http.server
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import agent_chat_server  # noqa: E402
import task_board  # noqa: E402


def _write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


class TaskBoardTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.storage = os.path.join(self.root, "storage")
        os.makedirs(self.storage, exist_ok=True)
        self.chat_file = os.path.join(self.storage, "chat.jsonl")

        # Handover move rule (006 moved from codex to gemini task 020)
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "INTERIM_LEAD.md"),
            "# Lead\n- 006 Stealth light/sound MOVED to Gemini task 020.\n",
        )

        # Inbox tasks
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "inbox", "006-stealth-light.md"),
            "# Stealth light/sound\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "inbox", "008-throwables.md"),
            "# 008: Throwables\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "inbox", "009-overwatch.md"),
            "# 009: Overwatch\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "inbox", "010-inventory.md"),
            "# 010: Inventory\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "gemini", "inbox", "030-dispatcher.md"),
            "# 030: Chat Dispatcher\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "gemini", "inbox", "031-live-task-board.md"),
            "# 031: Live Task Board\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "gemini", "inbox", "032-pending-audit.md"),
            "# 032: Pending Audit\n",
        )

        # Outbox reports
        _write_text(
            os.path.join(self.root, "docs", "port", "gemini", "outbox", "030-chat-dispatcher.report.md"),
            "# Report 030\n",
        )
        _write_text(
            os.path.join(self.root, "docs", "port", "codex", "outbox", "010-inventory.report.md"),
            "# Report 010\n",
        )

        # Mechanics register fixture
        _write_text(
            os.path.join(self.root, "docs", "port", "mechanics_register.json"),
            json.dumps(
                {
                    "groups": [
                        {
                            "id": "combat",
                            "title": "Бой",
                            "items": [
                                {
                                    "id": "throwables-system",
                                    "name": "Система гранат и метания",
                                    "status": "in_port",
                                    "doc": "docs/port/codex/inbox/008-throwables.md",
                                },
                                {
                                    "id": "psychic-psionics",
                                    "name": "Псионика",
                                    "status": "planned",
                                    "doc": "docs/port/unstarted-psionics.md",
                                },
                            ],
                        }
                    ]
                },
                ensure_ascii=False,
            ),
        )

        # Reference time: 2026-11-12T12:00:00Z
        self.now_dt = dt.datetime(2026, 11, 12, 12, 0, 0, tzinfo=dt.timezone.utc)
        self.now_ts = self.now_dt.timestamp()

        # Chat messages linked by --topic <task-id>
        msgs = [
            {
                "id": 1,
                "ts": (self.now_dt - dt.timedelta(minutes=30)).isoformat(timespec="seconds"),
                "from": "gemini",
                "to": "claude",
                "topic": "gemini-031",
                "text": "Working on live task board",
            },
            {
                "id": 2,
                "ts": (self.now_dt - dt.timedelta(minutes=15)).isoformat(timespec="seconds"),
                "from": "codex",
                "to": "designer",
                "topic": "codex-009",
                "text": "Нужно подтвердить радиус конуса перехвата?",
            },
            {
                "id": 3,
                "ts": (self.now_dt - dt.timedelta(hours=5)).isoformat(timespec="seconds"),
                "from": "gemini",
                "to": "claude",
                "topic": "gemini-030",
                "text": "030 done abc1234",
            },
        ]
        _write_text(
            self.chat_file,
            "\n".join(json.dumps(m, ensure_ascii=False) for m in msgs) + "\n",
        )

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _fake_git(self, _repo_root: str) -> dict:
        return {
            "branches": [
                {
                    "name": "codex/008-throwables",
                    "commit": "111aaaa",
                    "ts": self.now_ts - 600,  # 10 min ago -> in_progress
                    "subject": "008 grenade trajectory",
                },
                {
                    "name": "codex/009-overwatch",
                    "commit": "222bbbb",
                    "ts": self.now_ts - 1800,
                    "subject": "009 overwatch WIP",
                },
                {
                    "name": "codex/010-inventory",
                    "commit": "333cccc",
                    "ts": self.now_ts - 7200,
                    "subject": "010 inventory",
                },
                {
                    "name": "gemini/030-dispatcher",
                    "commit": "444dddd",
                    "ts": self.now_ts - 18000,
                    "subject": "030 dispatcher",
                },
                {
                    "name": "gemini/031-task-board",
                    "commit": "555eeee",
                    "ts": self.now_ts - 300,
                    "subject": "031 board scaffold",
                },
            ],
            "merged_interim": {"codex/010-inventory"},
            "merged_main": {"gemini/030-dispatcher"},
            "main_subjects": [],
        }

    def test_state_machine_handover_register_and_now_strip(self) -> None:
        board = task_board.build_task_board(
            repo_root=self.root,
            chat_file=self.chat_file,
            now_ts=self.now_ts,
            git_provider=self._fake_git,
        )
        by_id = {t["id"]: t for t in board["tasks"]}

        # Handover override: codex-006 owner becomes gemini
        self.assertEqual(by_id["codex-006"]["owner"], "gemini")
        self.assertIn("gemini-020", by_id["codex-006"]["moved_note"])

        # All 6 board lifecycle states covered cleanly
        self.assertEqual(by_id["codex-008"]["state"], "in_progress")
        self.assertEqual(by_id["codex-009"]["state"], "waiting")
        self.assertTrue(by_id["codex-009"]["asked_question"])
        self.assertEqual(by_id["codex-010"]["state"], "in_interim")
        self.assertEqual(by_id["gemini-030"]["state"], "in_main")
        self.assertEqual(by_id["gemini-031"]["state"], "in_progress")
        self.assertEqual(by_id["gemini-032"]["state"], "queued")

        # Outbox report -> done (when not merged into interim/main)
        st, _, _ = task_board.compute_task_state(
            has_report=True,
            in_interim=False,
            in_main=False,
            asked_question=False,
            blocked=False,
            last_commit_ts=self.now_ts - 10000,
            last_chat_ts=None,
            now_ts=self.now_ts,
        )
        self.assertEqual(st, "done")

        # Register matching & unmatched items marked not_started
        self.assertEqual(by_id["codex-008"]["title_ru"], "Система гранат и метания")
        self.assertEqual(by_id["codex-008"]["area"], "Бой")
        unmatched = [r for r in board["not_started_register"] if r["id"] == "psychic-psionics"]
        self.assertEqual(len(unmatched), 1)
        self.assertEqual(unmatched[0]["state"], "not_started")

        # Per-agent "Сейчас" strip
        self.assertEqual(board["now_strip"]["gemini"]["task_id"], "gemini-031")
        self.assertEqual(board["now_strip"]["gemini"]["idle_min"], 5)
        self.assertEqual(board["now_strip"]["codex"]["task_id"], "codex-008")
        self.assertEqual(board["now_strip"]["codex"]["idle_min"], 10)
        self.assertEqual(board["now_strip"]["claude"]["state"], "idle")

    def test_http_endpoints_and_security_guards(self) -> None:
        # Prepare a minimal HTML file path & bind test handler to 127.0.0.1:0
        html_path = os.path.join(self.root, "index.html")
        _write_text(html_path, "<html><body>ok</body></html>")

        class _TestHandler(agent_chat_server.AgentChatHandler):
            def log_message(self, format, *args):  # noqa: A002
                pass

        _TestHandler.storage_dir = self.storage
        _TestHandler.html_path = html_path
        _TestHandler.project_root = self.root

        httpd = http.server.HTTPServer(("127.0.0.1", 0), _TestHandler)
        port = httpd.server_address[1]
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        thread.start()

        try:
            base = f"http://127.0.0.1:{port}"

            # 1. GET /api/task-board?refresh=1 -> 200 and writes task_board.json
            with urllib.request.urlopen(f"{base}/api/task-board?refresh=1") as resp:
                self.assertEqual(resp.status, 200)
                payload = json.loads(resp.read().decode("utf-8"))
                self.assertIn("tasks", payload)
                self.assertIn("now_strip", payload)
            self.assertTrue(os.path.exists(os.path.join(self.storage, "task_board.json")))

            # 2. GET /api/port-doc on allowed inbox markdown -> 200
            with urllib.request.urlopen(
                f"{base}/api/port-doc?path=docs/port/gemini/inbox/031-live-task-board.md"
            ) as resp:
                self.assertEqual(resp.status, 200)
                doc = json.loads(resp.read().decode("utf-8"))
                self.assertIn("Live Task Board", doc["content"])

            # 3. GET /api/port-doc traversal / disallowed path -> 400
            with self.assertRaises(urllib.error.HTTPError) as ctx_bad:
                urllib.request.urlopen(f"{base}/api/port-doc?path=../Source/Secret.cpp")
            self.assertEqual(ctx_bad.exception.code, 400)

            # 4. Cross-origin Origin header -> 403 same_origin_only
            req = urllib.request.Request(
                f"{base}/api/task-board",
                headers={"Origin": "http://evil.example"},
            )
            with self.assertRaises(urllib.error.HTTPError) as ctx_origin:
                urllib.request.urlopen(req)
            self.assertEqual(ctx_origin.exception.code, 403)
        finally:
            httpd.shutdown()
            httpd.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
