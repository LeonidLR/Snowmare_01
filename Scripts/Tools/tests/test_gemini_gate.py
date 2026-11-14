#!/usr/bin/env python3
"""Unit tests for Scripts/Tools/gemini_gate.py (checks 2, 5, 6 and helpers)."""
from __future__ import annotations

import os
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import gemini_gate  # noqa: E402


def _write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)


class GeminiGateTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        _write_text(
            os.path.join(self.root, "Source", "CodexTactics", "Public", "Example.h"),
            "line 1\nline 2\nline 3\nline 4\n",
        )
        _write_text(
            os.path.join(self.root, "Scripts", "Tools", "tool.py"),
            "print('ok')\nprint('two')\n",
        )

    def tearDown(self) -> None:
        self._tmp.cleanup()

    # --- Check 1: Location ---
    def test_location_worktree_and_branch(self) -> None:
        wt = os.path.join(self.root, "CodexTactics-gemc5")
        os.makedirs(wt, exist_ok=True)
        _write_text(os.path.join(wt, ".git"), "gitdir: ../.git/worktrees/gemc5\n")
        ok = gemini_gate.check_location(wt, "036", branch_override="gemini/036-gate")
        self.assertTrue(ok.passed)

        wrong_branch = gemini_gate.check_location(wt, "036", branch_override="main")
        self.assertFalse(wrong_branch.passed)

        wrong_task = gemini_gate.check_location(wt, "034", branch_override="gemini/036-gate")
        self.assertFalse(wrong_task.passed)

        main_dir = os.path.join(self.root, "CodexTactics")
        os.makedirs(os.path.join(main_dir, ".git"), exist_ok=True)
        main_res = gemini_gate.check_location(main_dir, "036", branch_override="gemini/036-gate")
        self.assertFalse(main_res.passed)

    # --- Check 2: Allowed paths ---
    def test_allowed_paths_template_section_and_braces(self) -> None:
        task_text = (
            "# 099 - Demo\n"
            "## Allowed paths\n"
            "- `Source/CodexTactics/Public/Example.{h,cpp}`\n"
            "- `Scripts/Tools/**`\n"
        )
        ok = gemini_gate.check_allowed_paths(
            [
                "Source/CodexTactics/Public/Example.h",
                "Scripts/Tools/gemini_gate.py",
                "docs/port/gemini/outbox/099-demo.report.md",
            ],
            task_text,
            "099",
        )
        self.assertTrue(ok.passed, ok.details)

        bad = gemini_gate.check_allowed_paths(
            [
                "Source/CodexTactics/Public/Other.h",
            ],
            task_text,
            "099",
        )
        self.assertFalse(bad.passed)
        self.assertIn("Other.h", "\n".join(bad.details))

    def test_allowed_paths_inline_and_forbidden_always_fail(self) -> None:
        task_text = (
            "Allowed paths: Source/CodexTactics/**/Characters/Operative*Stealth*, "
            "Core/CodexTacticsPlayerController*.{h,cpp} (stealth only), Content/Maps/L_Test.umap.\n"
        )
        ok = gemini_gate.check_allowed_paths(
            [
                "Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp",
                "Source/CodexTactics/Public/Core/CodexTacticsPlayerController.h",
            ],
            task_text,
            "034",
        )
        self.assertTrue(ok.passed, ok.details)

        for forbidden_file in (
            "Content/Maps/L_Gen_Depot_1.umap",
            "Config/DefaultEngine.ini",
            "Content/VFX/M_Hit.uasset",
            "Content/Data/LevelJson/L_Gen_Depot_1.json",
            "Content/StealthLocomotion/Clip.uasset",
            "Content/StealthLocomotion/SomeFile.txt",
        ):
            res = gemini_gate.check_allowed_paths([forbidden_file], task_text, "034")
            self.assertFalse(res.passed, f"Expected FAIL for {forbidden_file}")

    def test_allowed_paths_missing_header_blocks_source(self) -> None:
        res_ok = gemini_gate.check_allowed_paths(["Scripts/Tools/gemini_gate.py"], "# 036\nNo header here\n", "036")
        self.assertTrue(res_ok.passed, res_ok.details)
        res_bad = gemini_gate.check_allowed_paths(["Source/CodexTactics/Foo.cpp"], "# 036\nNo header here\n", "036")
        self.assertFalse(res_bad.passed)

    # --- Check 5: Citation check ---
    def test_citation_check_valid_and_broken(self) -> None:
        rep_ok = os.path.join(self.root, "docs", "port", "gemini", "outbox", "036-good.report.md")
        _write_text(
            rep_ok,
            "# Report\n"
            "- Checked `Source/CodexTactics/Public/Example.h:1-4` and Scripts/Tools/tool.py:2.\n"
            "- Note Windows path C:\\Temp\\ignored.log:9999 does not trigger.\n"
            "| Claim | Evidence |\n|---|---|\n| Ok | `Source/CodexTactics/Public/Example.h:2` |\n",
        )
        res_ok = gemini_gate.check_citations(self.root, [rep_ok])
        self.assertTrue(res_ok.passed, res_ok.details)

        rep_bad_line = os.path.join(self.root, "docs", "port", "gemini", "outbox", "036-badline.report.md")
        _write_text(
            rep_bad_line,
            "See `Source/CodexTactics/Public/Example.h:99`.\n",
        )
        res_bad_line = gemini_gate.check_citations(self.root, [rep_bad_line])
        self.assertFalse(res_bad_line.passed)
        self.assertIn("out of range", "\n".join(res_bad_line.details))

        rep_missing_file = os.path.join(self.root, "docs", "port", "gemini", "outbox", "036-missing.report.md")
        _write_text(
            rep_missing_file,
            "Look at `Source/CodexTactics/Private/DoesNotExist.cpp`.\n",
        )
        res_missing = gemini_gate.check_citations(self.root, [rep_missing_file])
        self.assertFalse(res_missing.passed)
        self.assertIn("DoesNotExist.cpp", "\n".join(res_missing.details))

    # --- Check 6: Evidence check ---
    def test_evidence_check_tables(self) -> None:
        rep_en = os.path.join(self.root, "rep_en.md")
        _write_text(rep_en, "# Done\n| Claim | Evidence |\n|---|---|\n| Tests pass | 5/5 PASS abc123 |\n")
        self.assertTrue(gemini_gate.check_evidence([rep_en]).passed)

        rep_ru = os.path.join(self.root, "rep_ru.md")
        _write_text(rep_ru, "# Готово\n| Утверждение | Доказательство |\n|---|---|\n| Сборка зелёная | build.ps1 exit 0 |\n")
        self.assertTrue(gemini_gate.check_evidence([rep_ru]).passed)

        rep_empty_cell = os.path.join(self.root, "rep_empty.md")
        _write_text(rep_empty_cell, "| Claim | Evidence |\n|---|---|\n| Build |   |\n")
        self.assertFalse(gemini_gate.check_evidence([rep_empty_cell]).passed)

        rep_tbd = os.path.join(self.root, "rep_tbd.md")
        _write_text(rep_tbd, "| Утверждение | Доказательство |\n|---|---|\n| Тесты | TBD |\n")
        self.assertFalse(gemini_gate.check_evidence([rep_tbd]).passed)

        rep_no_table = os.path.join(self.root, "rep_notable.md")
        _write_text(rep_no_table, "# Report without table\nEverything looks good.\n")
        self.assertFalse(gemini_gate.check_evidence([rep_no_table]).passed)

    # --- Check 7 helpers ---
    def test_budget_and_sonnet_severity(self) -> None:
        _write_text(
            os.path.join(self.root, "docs", "port", "PLAN_WEEK_2026-11-16.md"),
            "week of 11-16: start 14.61 $ -> cap 64.61 $\n",
        )
        self.assertAlmostEqual(gemini_gate.read_weekly_budget_cap(self.root), 64.61)

        no_high, _ = gemini_gate.has_high_sonnet_findings("No HIGH findings.\n- [LOW] Minor style nit.\n")
        self.assertFalse(no_high)

        has_high, list_high = gemini_gate.has_high_sonnet_findings("- [HIGH] Null pointer deref on CoverSlot.\n")
        self.assertTrue(has_high)
        self.assertEqual(len(list_high), 1)

        has_md_high, _ = gemini_gate.has_high_sonnet_findings("1. **HIGH \u2013 `CoverGhostActor.cpp` hover ghost rebuilt every tick.**\n")
        self.assertTrue(has_md_high)

        self.assertTrue(gemini_gate.has_verify_marker("2. **HIGH (verify)"))
        self.assertTrue(gemini_gate.has_verify_marker("- [MEDIUM] [verify] check caller"))
        self.assertFalse(gemini_gate.has_verify_marker("No HIGH findings. Looks clean."))

    def test_tiered_opus_selection_rules(self) -> None:
        m1, _ = gemini_gate.select_review_tier(["Source/CodexTactics/Private/UI/ActionBarWidget.cpp"], 80)
        self.assertEqual(m1, gemini_gate.SONNET_MODEL)

        m2, reason2 = gemini_gate.select_review_tier(["Source/CodexTactics/Private/Characters/SquadSubsystem.cpp"], 20)
        self.assertEqual(m2, gemini_gate.OPUS_MODEL)
        self.assertIn("shared core", reason2)

        m3, reason3 = gemini_gate.select_review_tier(["Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp"], 550)
        self.assertEqual(m3, gemini_gate.OPUS_MODEL)
        self.assertIn("large diff", reason3)

        m4, _ = gemini_gate.select_review_tier(["Source/CodexTactics/Private/UI/ActionBarWidget.cpp"], 20, force_opus=True)
        self.assertEqual(m4, gemini_gate.OPUS_MODEL)

        self.assertEqual(gemini_gate.touches_shared_core(["Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp"]), [])
        self.assertEqual(
            gemini_gate.count_diff_changed_lines("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 +1,2 @@\n+a\n+b\n-c\n unchanged\n"),
            3,
        )


if __name__ == "__main__":
    unittest.main()
