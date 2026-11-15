"""Offline integration tests: git writes/commits/checkouts ONLY inside TemporaryDirectory."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import branch_guard

VALID = "```text\nResult: Succeeded\n12 tests, 0 failed\n```\n"
REPORT = "docs/port/gemini/outbox/task.report.md"


class BranchGuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="branch-guard-")
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name).resolve()
        # Neutralize inherited GIT_DIR/WORK_TREE and user hooks/signing/filter config.
        env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull,
                   GIT_TERMINAL_PROMPT="0", PYTHONDONTWRITEBYTECODE="1")
        self.environment = patch.dict(os.environ, env, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.git("init", "--initial-branch=main")
        self.git("config", "user.name", "Branch Guard Test")
        self.git("config", "user.email", "branch-guard@example.invalid")
        hooks = self.repo / "unused-hooks"
        hooks.mkdir()
        self.git("config", "core.hooksPath", str(hooks))
        self.git("config", "commit.gpgsign", "false")
        self.write("README.md", "base\n")
        self.write("CLAUDE.md", "protected baseline\n")
        self.write("docs/port/gemini/outbox/old.report.md", VALID)
        self.commit("baseline")
        self.git("checkout", "-b", "gemini/test")

    def git(self, *args):
        result = subprocess.run(["git", "-C", str(self.repo), *args], capture_output=True,
                                text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def write(self, name, content):
        path = self.repo / name
        self.assertTrue(path.resolve().is_relative_to(self.repo))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def commit(self, message="test change"):
        self.git("add", "--all")
        self.git("commit", "-m", message)

    def check(self):
        return branch_guard.check_branch("gemini/test", repo=self.repo)

    def report(self, text=VALID):
        self.write(REPORT, text)

    def test_valid_report_pass_and_no_worktree_mutations(self):
        self.report()
        self.write("Source/Foo.cpp", "int answer = 42;\n")
        self.commit()
        before = self.git("status", "--porcelain"), self.git("rev-parse", "HEAD")
        result = self.check()
        self.assertEqual(result["status"], "PASS", result)
        self.assertEqual(result["cpp_lines"], 1)
        self.assertEqual(before, (self.git("status", "--porcelain"), self.git("rev-parse", "HEAD")))

    def test_all_protected_paths_and_prefixes(self):
        paths = ["Scripts/Tools/typesafe_triage.py", "Scripts/Tools/branch_guard.py",
                 "Scripts/agent_lock.ps1", ".githooks/pre-push", "Scripts/pre_push_guard.py",
                 "Config/DefaultScalability.ini", "Content/VFX/Test.fx", "Content/Maps/Test.umap",
                 "Content/Data/LevelJson/L_Gen_Depot_1.json", "Scripts/Tools/local_task.py",
                 "CLAUDE.md", ".claude/agents/a.md"]
        for name in paths:
            self.write(name, "changed\n")
        self.report()
        self.commit()
        result = self.check()
        self.assertEqual(set(result["protected_paths"]), set(paths))
        self.assertEqual(result["status"], "FAIL")

    def test_protected_delete_and_rename_are_not_hidden(self):
        self.git("mv", "CLAUDE.md", "renamed.md")
        self.report()
        self.commit()
        self.assertIn("CLAUDE.md", self.check()["protected_paths"])

    def test_directory_root_and_case_variants(self):
        for name in (".githooks", ".claude", "Content/Maps", "content/VFX/a", ".git/hooks/pre-push"):
            self.assertTrue(branch_guard.is_protected(name), name)
        self.assertFalse(branch_guard.is_protected("docs/Content/Maps/notes.md"))

    def test_unchanged_inherited_report_is_not_evidence(self):
        self.write("Source/A.cpp", "a\n")
        self.commit()
        self.assertEqual(self.check()["status"], "FAIL")
        self.assertEqual(self.check()["reports"], [])

    def test_deleted_report_is_not_evidence(self):
        self.git("rm", "docs/port/gemini/outbox/old.report.md")
        self.commit()
        self.assertEqual(self.check()["status"], "FAIL")

    def test_report_read_from_branch_not_worktree(self):
        self.report("unverified claim\n")
        self.commit()
        self.report(VALID)  # uncommitted good text cannot rescue branch evidence
        self.assertEqual(self.check()["status"], "FAIL")

    def test_outputs_must_share_one_report(self):
        self.report("Result: Succeeded\n")
        self.write("docs/port/gemini/outbox/other.report.md", "12 tests, 0 failed\n")
        self.commit()
        self.assertEqual(self.check()["status"], "FAIL")

    def test_bad_output_and_prose_are_not_test_evidence(self):
        for text in ("Build PASS, tests PASS", "Result: Succeeded\n0 tests, 0 failed",
                     "Result: Succeeded\n12 tests, 1 failed", "Exit code 10\n12 tests, 0 failed",
                     "- Result: Succeeded\n- 12 tests, 0 failed"):
            self.assertFalse(branch_guard.evidence(text)["complete"], text)

    def test_exit_zero_and_log_prefix_output(self):
        self.report("[2026.11.15-12.00.00][  0]LogPython: Exit code 0\n"
                    "LogAutomationController: 4 tests, 0 failed\n")
        self.commit()
        self.assertEqual(self.check()["status"], "PASS")

    def test_smoke_claim_without_output_warns_not_fails(self):
        self.report(VALID + "All smokes PASS.\n")
        self.commit()
        result = self.check()
        self.assertEqual(result["status"], "WARN")
        self.assertTrue(any("smoke" in line for line in result["warnings"]))

    def test_smoke_output_accepted_and_not_run_does_not_warn(self):
        for extra in ("LogCodexTactics: Smoke CoverSmoke: PASS\n", "Smokes not run.\n"):
            self.assertFalse(branch_guard.evidence(VALID + extra)["smoke_claim_without_output"])

    def test_cpp_size_boundary_and_non_cpp_ignored(self):
        self.report()
        self.write("Source/Size.cpp", "line\n" * 800)
        self.write("docs/large.md", "text\n" * 900)
        self.commit()
        self.assertEqual(self.check()["status"], "PASS")
        self.write("Source/Size.cpp", "line\n" * 801)
        self.commit("cross threshold")
        result = self.check()
        self.assertEqual(result["cpp_lines"], 801)
        self.assertEqual(result["status"], "WARN")

    def test_cpp_deletions_count(self):
        self.write("Source/Delete.hpp", "line\n" * 801)
        self.commit("file creation")
        self.git("branch", "-f", "main", "HEAD")
        self.git("rm", "Source/Delete.hpp")
        self.report()
        self.commit("file deletion")
        self.assertEqual(self.check()["cpp_lines"], 801)

    def test_base_advance_does_not_charge_agent_with_main_changes(self):
        self.report()
        self.commit()
        self.git("checkout", "main")
        self.write("CLAUDE.md", "main-only change\n")
        self.commit("lead change")
        self.assertEqual(self.check()["status"], "PASS")
        self.assertEqual(self.git("branch", "--show-current"), "main")

    def test_invalid_refs_fail_closed(self):
        for branch in ("missing", "--help"):
            self.assertEqual(branch_guard.check_branch(branch, repo=self.repo)["status"], "FAIL")
        self.assertEqual(branch_guard.check_branch("gemini/test", "missing", self.repo)["status"], "FAIL")

    def test_cli_json_exit_codes_and_base_option(self):
        self.report()
        self.commit()
        command = [sys.executable, str(TOOLS / "branch_guard.py"), "gemini/test", "--base", "main", "--json"]
        completed = subprocess.run(command, cwd=self.repo, capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["status"], "PASS")
        self.write("Scripts/agent_lock.ps1", "blocked\n")
        self.commit()
        completed = subprocess.run(command, cwd=self.repo, capture_output=True, text=True, encoding="utf-8")
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(json.loads(completed.stdout)["status"], "FAIL")


if __name__ == "__main__":
    unittest.main()
