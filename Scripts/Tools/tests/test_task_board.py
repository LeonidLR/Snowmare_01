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
                                    "id": "grenade-system",
                                    "name": "Система гранат и метания",
                                    "status": "in_port",
                                    "doc": "docs/port/codex/inbox/008-throwables.md",
                                },
                                {
                                    "id": "overwatch-system",
                                    "name": "Overwatch register name",
                                    "status": "planned",
                                    "doc": "docs/port/reactive-fire.md",
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
        # The interim branch is gone (2026-11-16): a branch merged only into interim shows as done (review), not a separate column.
        self.assertEqual(by_id["codex-010"]["state"], "done")
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
        self.assertEqual(by_id["codex-009"]["area"], "Бой")
        self.assertEqual(by_id["codex-009"]["title_ru"], "")
        unmatched = [r for r in board["not_started_register"] if r["id"] == "psychic-psionics"]
        self.assertEqual(len(unmatched), 1)
        self.assertEqual(unmatched[0]["state"], "not_started")

        # Per-agent "Сейчас" strip
        self.assertEqual(board["now_strip"]["gemini"]["task_id"], "gemini-031")
        self.assertEqual(board["now_strip"]["gemini"]["idle_min"], 5)
        self.assertNotIn("codex", board["now_strip"])
        self.assertEqual(by_id["codex-008"]["idle_min"], 10)
        self.assertEqual(board["now_strip"]["claude"]["state"], "idle")

    def test_grok_tasks_and_generic_moves(self) -> None:
        def w(owner: str, box: str, name: str, text: str) -> None:
            _write_text(os.path.join(self.root, "docs", "port", owner, box, name), text)

        w("grok", "inbox", "G001-ramp-stall.md", "# G001: Ramp\n")
        w("grok", "inbox", "G002-metrics.md", "# G002: Metrics\n")
        w("grok", "outbox", "G001-ramp-stall.report.md", "# R\n")
        w("gemini", "inbox", "081-ramp-fight.md", "# 081\n")
        w("gemini", "inbox", "080-jitter.md", "# 080\n")
        w("codex", "inbox", "043-ramp-fight.md", "# 043\n")
        _write_text(
            os.path.join(self.root, "docs", "port", "RESOURCES.md"),
            "- gemini 081 -> codex 043\n- gemini 080 -> claude (review)\n",
        )

        def fake_git(_root: str) -> dict:
            return {
                "branches": [{"name": "grok/G002-metrics", "commit": "abc1234", "ts": self.now_ts - 60, "subject": "x"}],
                "merged_interim": set(),
                "merged_main": set(),
                "main_subjects": [],
            }

        board = task_board.build_task_board(
            repo_root=self.root, chat_file=self.chat_file, now_ts=self.now_ts, git_provider=fake_git
        )
        by_id = {t["id"]: t for t in board["tasks"]}
        self.assertEqual(by_id["grok-G001"]["owner"], "grok")
        self.assertEqual(by_id["grok-G001"]["state"], "done")
        self.assertTrue(by_id["grok-G001"]["report_file"].endswith("G001-ramp-stall.report.md"))
        self.assertEqual(by_id["grok-G002"]["branch"], "grok/G002-metrics")
        self.assertEqual(by_id["grok-G002"]["state"], "in_progress")
        # 081 moved to codex 043: shown once, as the codex task.
        self.assertNotIn("gemini-081", by_id)
        self.assertEqual(by_id["codex-043"]["owner"], "codex")
        # 080 moved to claude without a target file: the gemini task stays, owned by claude.
        self.assertEqual(by_id["gemini-080"]["owner"], "claude")

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

    def test_progress_parsing_and_chat_in_progress_window(self) -> None:
        now = 1_700_000_000
        ts1 = "2026-11-25T13:00:00+00:00"
        ts2 = "2026-11-25T13:01:00+00:00"
        ts3 = "2026-11-25T13:02:00+00:00"
        ts4 = "2026-11-25T13:05:00+00:00"

        # 1. PLAN with numbered steps -> 0/3 total
        p_plan = task_board.parse_progress_from_messages(
            [
                {"ts": ts1, "text": "124 START"},
                {"ts": ts2, "text": "124 PLAN | model: Hydrogen | 1) parser 2) UI 3) tests"},
            ]
        )
        self.assertEqual(p_plan["progress_done"], 0)
        self.assertEqual(p_plan["progress_total"], 3)
        self.assertEqual(p_plan["progress_step"], "")
        self.assertEqual(p_plan["progress_ts"], task_board.parse_iso_timestamp(ts2))

        # 2. PROGRESS overrides PLAN, malformed PROGRESS is ignored
        p_prog = task_board.parse_progress_from_messages(
            [
                {"ts": ts1, "text": "124 PLAN | 1) a 2) b 3) c 4) d"},
                {"ts": ts2, "text": "124 PROGRESS 2/4 | A/B 300 seeds"},
                {"ts": ts3, "text": "124 PROGRESS bad/line | ignore me"},
                {"ts": ts4, "text": "124 PROGRESS 9/2 | invalid done > total"},
            ]
        )
        self.assertEqual(p_prog["progress_done"], 2)
        self.assertEqual(p_prog["progress_total"], 4)
        self.assertEqual(p_prog["progress_step"], "A/B 300 seeds")
        self.assertEqual(p_prog["progress_ts"], task_board.parse_iso_timestamp(ts2))

        # 3. FINISH sets progress_done == progress_total
        p_fin = task_board.parse_progress_from_messages(
            [
                {"ts": ts2, "text": "124 PROGRESS 2/5 | building"},
                {"ts": ts4, "text": "124 FINISH | PASS | abcdef1 | done"},
            ]
        )
        self.assertEqual(p_fin["progress_done"], 5)
        self.assertEqual(p_fin["progress_total"], 5)
        self.assertEqual(p_fin["progress_ts"], task_board.parse_iso_timestamp(ts4))

        # 4. START/PLAN/PROGRESS in last 2h moves a branchless task to in_progress
        st_recent, _, _ = task_board.compute_task_state(
            has_report=False,
            in_interim=False,
            in_main=False,
            asked_question=False,
            blocked=False,
            last_commit_ts=None,
            last_chat_ts=now - 1800,
            now_ts=now,
        )
        self.assertEqual(st_recent, "in_progress")

        st_stale, _, _ = task_board.compute_task_state(
            has_report=False,
            in_interim=False,
            in_main=False,
            asked_question=False,
            blocked=False,
            last_commit_ts=None,
            last_chat_ts=now - 9000,
            now_ts=now,
        )
        self.assertEqual(st_stale, "queued")


if __name__ == "__main__":
    unittest.main()
