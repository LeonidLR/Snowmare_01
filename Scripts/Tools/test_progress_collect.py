"""Tests for progress_collect.py (pytest or `python -m unittest`).

Fixtures: Scripts/Tools/fixtures/progress/ (21 real FINISH lines copied from Saved/AgentChat/chat.jsonl + expected parse).
Git tests run on throw-away repositories in a temp dir; the real repo is never touched.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

import progress_collect as pc  # noqa: E402

FIXTURES = os.path.join(TOOLS_DIR, "fixtures", "progress")


def run_git(repo: str, *args: str) -> str:
    env = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t"}
    proc = subprocess.run(["git", "-C", repo, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, check=True)
    return proc.stdout.decode("utf-8").strip()


def commit_file(repo: str, name: str, content: str, msg: str) -> str:
    with open(os.path.join(repo, name), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(content)
    run_git(repo, "add", name)
    run_git(repo, "commit", "-q", "-m", msg)
    return run_git(repo, "rev-parse", "HEAD")


def init_repo(path: str) -> None:
    run_git(path, "init", "-q", "-b", "main")
    run_git(path, "config", "core.autocrlf", "false")
    commit_file(path, "base.txt", "base\n", "base")


class FinishParsingTest(unittest.TestCase):
    def test_real_finish_lines(self) -> None:
        with open(os.path.join(FIXTURES, "finish_expected.json"), "r", encoding="utf-8") as fh:
            expected = json.load(fh)
        with open(os.path.join(FIXTURES, "finish_chat.jsonl"), "r", encoding="utf-8") as fh:
            msgs = [json.loads(line) for line in fh if line.strip()]
        self.assertGreaterEqual(len(msgs), 20)
        for msg in msgs:
            parsed = pc.parse_finish_text(msg["text"])
            self.assertIsNotNone(parsed, msg["text"][:80])
            exp = expected[str(msg["id"])]
            self.assertEqual(parsed["num"], exp["num"], msg["id"])
            self.assertEqual(parsed["hash"], exp["hash"], msg["id"])
            self.assertEqual(parsed["status"], exp["status"], msg["id"])
            self.assertTrue(parsed["hash"] and len(parsed["hash"]) >= 7)

    def test_suffix_letter_and_positional(self) -> None:
        self.assertEqual(pc.parse_finish_text("083c FINISH | status: done | last commit: f0534a91 on x | done: a")["num"], "083")
        p = pc.parse_finish_text("G007 FINISH | готово | 4def1703 | текст | не сделано | ревью | PASS")
        self.assertEqual((p["num"], p["hash"]), ("G007", "4def1703"))

    def test_no_hash_and_non_finish(self) -> None:
        p = pc.parse_finish_text("998 FINISH | status: done | last commit: none | done: ok | NOT done: none | gate: not run")
        self.assertIsNotNone(p)
        self.assertIsNone(p["hash"])
        self.assertIsNone(pc.parse_finish_text("queue empty: 089 FINISH, 091 FINISH. All night tasks"))

    def test_reviews(self) -> None:
        self.assertEqual(pc.parse_review_text("089 review: REWORK - see docs/x.md")["verdict"], "REWORK")
        self.assertEqual(pc.parse_review_text("090 review: OK, merge it")["verdict"], "OK")
        self.assertEqual(pc.parse_review_text("008 review: 6 defects - docs/y.md")["verdict"], "NOTES")
        self.assertEqual(pc.parse_review_text("OK 098")["verdict"], "OK")
        self.assertIsNone(pc.parse_review_text("random chat line"))


class MergeStatusTest(unittest.TestCase):
    def test_ancestor_patch_id_and_unmerged(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            init_repo(tmp)
            run_git(tmp, "checkout", "-q", "-b", "feature")
            picked = commit_file(tmp, "a.txt", "feature line\n", "feat: a")
            unmerged = commit_file(tmp, "b.txt", "never merged\n", "feat: b")
            run_git(tmp, "checkout", "-q", "main")
            on_main = commit_file(tmp, "c.txt", "main work\n", "main: c")
            run_git(tmp, "cherry-pick", picked)
            res = pc.merge_status(tmp, [on_main, picked, unmerged], "main")
            self.assertEqual(res[on_main], "ancestor")
            self.assertEqual(res[picked], "patch-id")  # not an ancestor: it came in by cherry-pick
            self.assertIsNone(res[unmerged])
            rc = subprocess.run(["git", "-C", tmp, "merge-base", "--is-ancestor", picked, "main"]).returncode
            self.assertNotEqual(rc, 0)

    def test_resolve_missing_commit(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            init_repo(tmp)
            head = run_git(tmp, "rev-parse", "HEAD")
            res = pc.resolve_commits(tmp, [head[:8], "deadbeef"])
            self.assertEqual(res[head[:8]], head)
            self.assertIsNone(res["deadbeef"])


PLAN = {
    "schema": "progress-plan/1", "updated": "2026-11-22", "goal_ru": "цель",
    "stages": [{"id": "S1", "title_ru": "Этап 1", "dates": ["2026-11-22", "2026-11-28"]}],
    "weeks": [{"id": "2026-11-22..28", "stage": "S1", "goal_ru": "неделя", "gates": []}],
    "metrics": [], "designer_open": [],
    "tasks": [{"id": "t-one", "title_ru": "Первая", "stage": "S1", "week": "2026-11-22..28", "track": "tools", "kind": "tool",
               "owner": "lead", "day_row": "2026-11-22:1", "match": {"branches": ["feat/planned"]}}],
}
DAY_PLAN = """# Day plan 2026-11-22

| # | Who | Task | Done when |
|---|---|---|---|
| 1 | Lead | First | done |
| 2 | Lead | Second, no task | done |
"""


def build_world(tmp: str) -> tuple[str, str, str]:
    repo = os.path.join(tmp, "repo")
    os.makedirs(os.path.join(repo, "docs", "port", "progress"))
    init_repo(repo)
    run_git(repo, "checkout", "-q", "-b", "feat/planned")
    commit_file(repo, "p.txt", "planned\n", "feat: planned work")
    run_git(repo, "checkout", "-q", "-b", "stray/no-task")
    commit_file(repo, "s.txt", "stray\n", "feat: stray work")
    run_git(repo, "checkout", "-q", "main")
    with open(os.path.join(repo, "docs", "port", "progress", "plan.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(PLAN, fh, ensure_ascii=False)
    with open(os.path.join(repo, "docs", "port", "DAY_PLAN_2026-11-22.md"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write(DAY_PLAN)
    with open(os.path.join(repo, "docs", "port", "LANES.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump({"lanes": []}, fh)
    chat = os.path.join(tmp, "chat.jsonl")
    with open(chat, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(json.dumps({"id": 1, "ts": pc.ts_to_iso(int(time.time()) - 3600), "from": "gemini", "to": "claude", "topic": "x",
                             "text": "777 FINISH | PASS | deadbee1 | done: x | NOT done: none"}) + "\n")
    return repo, chat, os.path.join(tmp, "out")


def make_collector(repo: str, chat: str, out: str) -> pc.Collector:
    # 2026-11-22 12:00 UTC-ish; the day plan file name is picked by date, the refs are "recent" relative to wall time below
    return pc.Collector(repo=repo, out_dir=out, chat_path=chat, now=int(time.time()))


class CollectorTest(unittest.TestCase):
    def test_rebuild_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo, chat, out = build_world(tmp)
            col = make_collector(repo, chat, out)
            col.run_once(rebuild=True)
            first = open(os.path.join(out, "state.json"), "rb").read()
            col.run_once(rebuild=True)
            second = open(os.path.join(out, "state.json"), "rb").read()
            self.assertEqual(first, second)
            col.run_once(rebuild=False)  # incremental pass over the same sources changes nothing
            self.assertEqual(first, open(os.path.join(out, "state.json"), "rb").read())

    def test_unmatched_branch_and_discrepancy(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo, chat, out = build_world(tmp)
            state = make_collector(repo, chat, out).run_once(rebuild=True)
            un = {u["branch"]: u for u in state["flags"]["unmatched"]}
            self.assertIn("stray/no-task", un)
            self.assertEqual(un["stray/no-task"]["reasons"], ["no_task", "no_lane"])
            self.assertIn("feat/planned", un)  # has a plan task but no lane
            self.assertEqual(un["feat/planned"]["reasons"], ["no_lane"])
            self.assertNotIn("main", un)
            self.assertTrue(any(d["kind"] == "finish_hash_missing" and d["hash"] == "deadbee1" for d in state["flags"]["discrepancies"]))

    def test_day_rows_get_status(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo, chat, out = build_world(tmp)
            state = make_collector(repo, chat, out).run_once(rebuild=True)
            rows = {r["n"]: r for r in state["day_plan"]["rows"]}
            self.assertEqual(set(rows), {1, 2})
            self.assertEqual(rows[1]["status"], "in_progress")  # fresh commit on the matched branch
            self.assertEqual(rows[2]["status"], "planned")
            self.assertTrue(rows[2]["no_plan_task"])

    def test_html_is_self_contained(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            repo, chat, out = build_world(tmp)
            html_path = os.path.join(tmp, "p.html")
            make_collector(repo, chat, out).run_once(rebuild=True, html_path=html_path)
            text = open(html_path, "r", encoding="utf-8").read()
            self.assertIn("<title>Прогресс разработки</title>", text)
            self.assertNotIn("<script", text)
            self.assertNotIn("http://", text.replace("http://www.w3.org", ""))

    def test_lock_second_runner_exits(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            a = pc.CollectorLock(os.path.join(tmp, "c.lock"))
            b = pc.CollectorLock(os.path.join(tmp, "c.lock"))
            self.assertTrue(a.acquire())
            self.assertFalse(b.acquire())
            a.release()
            self.assertTrue(b.acquire())
            b.release()


if __name__ == "__main__":
    unittest.main()
