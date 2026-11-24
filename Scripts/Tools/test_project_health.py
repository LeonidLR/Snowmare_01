#!/usr/bin/env python3
"""Unit tests for Scripts/Tools/project_health.py (task 109 / Codex 082 must-fixes)."""

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent.parent
sys.path.insert(0, str(TOOLS_DIR))

import project_health  # noqa: E402


class ProjectHealthTests(unittest.TestCase):
    def _make_fake_repo(self, tmp: Path, smoke_names: list[str] | None = None) -> Path:
        (tmp / "Scripts").mkdir(parents=True, exist_ok=True)
        (tmp / "Saved" / "Health").mkdir(parents=True, exist_ok=True)
        (tmp / "CodexTactics.uproject").write_text("{}", encoding="utf-8")
        tm = {"smokeRegistry": smoke_names or ["SmokeA", "SmokeB", "SmokeC"]}
        (tmp / "Scripts" / "test_map.json").write_text(json.dumps(tm), encoding="utf-8")
        return tmp

    def test_smoke_json_with_utf8_bom_and_fail(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._make_fake_repo(Path(td))
            sjson = repo / "Saved" / "Health" / "smoke_bom.json"
            payload = {
                "jobs": 2,
                "wallSeconds": 12.3,
                "results": [
                    {"Name": "SmokeA", "State": "PASS", "Seconds": 3.0},
                    {"Name": "SmokeB", "State": "KNOWN-FAIL", "Seconds": 4.0},
                    {"Name": "SmokeC", "State": "FAIL", "Seconds": 5.3},
                ],
            }
            # Write with UTF-8 BOM (PowerShell 5.1 Set-Content -Encoding UTF8 behavior)
            sjson.write_bytes(b"\xef\xbb\xbf" + json.dumps(payload).encode("utf-8"))

            res = project_health.collect_latest_smoke_results(repo, str(sjson), smoke_exit=1, max_age_minutes=60)
            self.assertTrue(res["available"])
            self.assertTrue(res["fresh"])
            self.assertEqual(res["total"], 3)
            self.assertEqual(res["expected_total"], 3)
            self.assertEqual(res["pass"], 1)
            self.assertEqual(res["known_fail"], 1)
            self.assertEqual(res["fail"], 1)

    def test_no_tests_matched_and_engine_exit_1_never_fakes_420(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._make_fake_repo(Path(td))
            tlog = repo / "Saved" / "Health" / "tests_fail.log"
            tlog.write_text("No tests matched filter.\nEngine exit code 1\n", encoding="utf-8")

            res = project_health.collect_latest_test_results(repo, str(tlog), test_exit=1, max_age_minutes=60)
            self.assertFalse(res["available"])
            self.assertEqual(res["total"], 0)
            self.assertEqual(res["passed"], 0)
            self.assertIn("no tests matched", res["error"])

    def test_stale_artifacts_are_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._make_fake_repo(Path(td))
            tlog = repo / "Saved" / "Health" / "old_tests.log"
            tlog.write_text("10 tests, 0 failed\n", encoding="utf-8")
            old_time = time.time() - 7200  # 2 hours ago
            os.utime(tlog, (old_time, old_time))

            res = project_health.collect_latest_test_results(repo, str(tlog), test_exit=0, max_age_minutes=30)
            self.assertFalse(res["available"])
            self.assertFalse(res["fresh"])
            self.assertIn("stale", res["error"])

    def test_strict_cli_exits_nonzero_on_bom_fail_or_unavailable(self):
        with tempfile.TemporaryDirectory() as td:
            repo = self._make_fake_repo(Path(td))
            tlog = repo / "Saved" / "Health" / "t.log"
            tlog.write_text("No tests matched\n", encoding="utf-8")
            sjson = repo / "Saved" / "Health" / "s.json"
            sjson.write_bytes(
                b"\xef\xbb\xbf"
                + json.dumps({"wallSeconds": 1.0, "results": [{"Name": "SmokeA", "State": "FAIL"}]}).encode("utf-8")
            )
            out_json = repo / "Saved" / "Health" / "out.json"
            debt_md = repo / "Saved" / "Health" / "DEBT.md"

            proc = subprocess.run(
                [
                    sys.executable,
                    str(TOOLS_DIR / "project_health.py"),
                    "--repo",
                    str(repo),
                    "--test-log",
                    str(tlog),
                    "--smoke-json",
                    str(sjson),
                    "--test-exit",
                    "1",
                    "--smoke-exit",
                    "1",
                    "--max-age-minutes",
                    "60",
                    "--strict",
                    "--out-json",
                    str(out_json),
                    "--debt-md",
                    str(debt_md),
                    "--chat-summary",
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 1)
            self.assertIn("status CRITICAL", proc.stdout)
            self.assertIn("tests UNAVAILABLE", proc.stdout)
            self.assertIn("1 fail", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
