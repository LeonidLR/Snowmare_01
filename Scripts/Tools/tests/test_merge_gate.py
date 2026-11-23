"""Exercise merge-gate decisions without running a merge, Unreal or the worker."""

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest


TOOLS = Path(__file__).resolve().parents[1]
GATE = TOOLS / "merge_gate.ps1"
KNOWN_GOALS = "CodexTactics.LevelGen.MissionGoals.CommittedFiles"
KNOWN_PROPS = "CodexTactics.LevelGen.Props.Power.GeneratedMaps"
GOALS_FILE = "Content/Data/LevelGen/objectives/L_Gen_Depot_1_X6.objectives.json"
PROPS_FILE = "Content/Data/LevelGen/props/L_Gen_Depot_1_X6.props.json"


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def fixture_environment():
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_TERMINAL_PROMPT="0")
    return env


def powershell(script, cwd=None):
    preamble = "[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)\n"
    encoded = base64.b64encode((preamble + script).encode("utf-16le")).decode("ascii")
    return subprocess.run(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encoded],
        cwd=cwd,
        env=fixture_environment(),
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
    )


def git(cwd, *args):
    result = subprocess.run(
        ["git", *args], cwd=cwd, env=fixture_environment(), capture_output=True, encoding="utf-8", timeout=20
    )
    if result.returncode:
        raise AssertionError(f"git {args} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout.strip()


def helper(script):
    result = powershell("$ErrorActionPreference = 'Stop'\n. " + ps_quote(GATE) + "\n" + script)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return json.loads(result.stdout)


class MergeGateParsers(unittest.TestCase):
    def test_status_separates_unstaged_changes_and_rename_sources(self):
        paths = helper(
            "$nul = [char]0\n"
            "$raw = @(' M Content/[дизайнер].json','M  staged','R  New Name','Old Name') -join $nul\n"
            "ConvertTo-Json -Compress -InputObject @(ConvertFrom-GateGitStatus $raw)"
        )
        self.assertEqual(
            paths,
            [
                {"Status": " M", "Path": "Content/[дизайнер].json"},
                {"Status": "M ", "Path": "staged"},
                {"Status": "R ", "Path": "New Name"},
            ],
        )

    def test_editor_matches_only_gui_for_exact_project(self):
        found = helper(
            "$project = 'C:\\Unreal Projects\\CodexTactics\\CodexTactics.uproject'\n"
            "$gui = '\"C:\\UE 5.8\\UnrealEditor.exe\" \"C:/Unreal Projects/CodexTactics/CodexTactics.uproject\"'\n"
            "$lines = @($gui, \"$gui -Gameplay\", \"$gui -Game\", \"$gui -run=Cook\", "
            "($gui.Replace('CodexTactics/', 'CodexTactics-cx060/')), \"$gui -gameplay=1 -run=foo\")\n"
            "ConvertTo-Json -Compress -InputObject @($lines | ForEach-Object { Test-GateEditorCommandLine $_ $project })"
        )
        self.assertEqual(found, [True, True, False, False, False, False])

    def test_known_x6_failures_require_each_dirty_file_and_completed_run(self):
        log = [
            f"Test Completed. Result={{Fail}} Name={{Goals}} Path={{{KNOWN_GOALS}}}",
            f"Test Completed. Result={{Fail}} Name={{Power}} Path={{{KNOWN_PROPS}}}",
            "Test Completed. Result={Success} Name={Grid} Path={CodexTactics.Core.Grid}",
            "[2026.11.23-13.43.27:917][665]LogAutomationCommandLine: Display: ...Automation Test Queue Empty 3 tests performed.",
            "LogExit: Display: **** TestExit: Automation Test Queue Empty ****",
        ]
        parsed = helper(
            "$log = @(" + ",".join(map(ps_quote, log)) + ")\n"
            "$dirty = @(" + ",".join(map(ps_quote, [GOALS_FILE, PROPS_FILE])) + ")\n"
            "$fatal = @($log) + 'Critical: Fatal error:'\n"
            "ConvertTo-Json -Depth 5 -Compress -InputObject @("
            "(Get-GateAutomationFailures $log $dirty),"
            "(Get-GateAutomationFailures $log @($dirty[0])),"
            "(Get-GateAutomationFailures $log[0..3] $dirty),"
            "(Get-GateAutomationFailures $fatal $dirty),"
            "(Get-GateAutomationFailures $log @()))"
        )
        self.assertEqual(parsed[0], {"Known": [KNOWN_GOALS, KNOWN_PROPS], "Unexpected": [], "Complete": True})
        self.assertEqual(parsed[1]["Known"], [KNOWN_GOALS])
        self.assertEqual(parsed[1]["Unexpected"], [KNOWN_PROPS])
        self.assertFalse(parsed[2]["Complete"])
        self.assertFalse(parsed[3]["Complete"])
        self.assertEqual(parsed[4]["Unexpected"], [KNOWN_GOALS, KNOWN_PROPS])

    def test_backup_keeps_nested_path_and_hash(self):
        with TemporaryDirectory(prefix="codex-gate-backup-") as directory:
            repo = Path(directory)
            relative = "Content/[этаж]/[дизайнер].json"
            original = repo / relative
            original.parent.mkdir(parents=True)
            data = b'{"floors": 3}\r\n'
            original.write_bytes(data)
            backup = repo / "Saved" / "Backups" / "sample"
            manifest = helper(
                f"Backup-GateDesignerFiles {ps_quote(repo)} {ps_quote(backup)} @({ps_quote(relative)})\n"
                f"Get-Content -LiteralPath {ps_quote(backup / 'SHA256.json')} -Raw"
            )
            self.assertEqual((backup / relative).read_bytes(), data)
            self.assertEqual(original.read_bytes(), data)
            self.assertEqual(manifest, [{"Path": relative, "SHA256": hashlib.sha256(data).hexdigest().upper()}])

    def test_build_only_for_source_or_project(self):
        decisions = helper(
            "ConvertTo-Json -Compress -InputObject @("
            "(Test-GateNeedsBuild @('Source/Game/foo.cpp')),"
            "(Test-GateNeedsBuild @('CodexTactics.uproject')),"
            "(Test-GateNeedsBuild @('Content/Source/notes.md','docs/design.md')),"
            "(Test-GateNeedsBuild @()))"
        )
        self.assertEqual(decisions, [True, True, False, False])


class MergeGateDryRun(unittest.TestCase):
    def setUp(self):
        temp = TemporaryDirectory(prefix="codex-gate-preview-")
        self.addCleanup(temp.cleanup)
        self.repo = Path(temp.name)
        scripts = self.repo / "Scripts"
        (scripts / "Tools").mkdir(parents=True)
        self.gate = Path(shutil.copyfile(GATE, scripts / "Tools" / GATE.name))
        shutil.copyfile(TOOLS.parent / "verify_lib.ps1", scripts / "verify_lib.ps1")
        (scripts / "test_map.json").write_text(
            json.dumps(
                {
                    "rules": [{"name": "source", "globs": ["Source/**"], "smokes": ["WalkSmoke"], "tests": ["CodexTactics.Core"]}],
                    "quickCount": 1,
                    "fallback": {"tests": [], "smokes": []},
                    "smokeRegistry": ["WalkSmoke"],
                }
            ),
            encoding="utf-8",
        )
        self.relative = "Content/[дизайнер].json"
        self.designer = self.repo / self.relative
        self.designer.parent.mkdir()
        self.designer.write_text("committed", encoding="utf-8")
        (self.repo / "CodexTactics.uproject").write_text("{}", encoding="utf-8")
        git(self.repo, "init", "-q", "-b", "main")
        git(self.repo, "config", "user.name", "Merge Gate Fixture")
        git(self.repo, "config", "user.email", "merge-gate@example.invalid")
        git(self.repo, "add", "--", "Scripts", "Content", "CodexTactics.uproject")
        git(self.repo, "commit", "-qm", "fixture")
        self.head = git(self.repo, "rev-parse", "HEAD")
        git(self.repo, "checkout", "-qb", "docs-only")
        (self.repo / "notes.md").write_text("fixture", encoding="utf-8")
        git(self.repo, "add", "--", "notes.md")
        git(self.repo, "commit", "-qm", "documentation")
        git(self.repo, "checkout", "-qb", "source-change", "main")
        source = self.repo / "Source" / "новый файл.cpp"
        source.parent.mkdir()
        source.write_text("fixture", encoding="utf-8")
        git(self.repo, "add", "--", "Source")
        git(self.repo, "commit", "-qm", "source")
        git(self.repo, "checkout", "-q", "main")
        self.designer.write_text("dirty", encoding="utf-8")

    def preview(self, args):
        return powershell(f"& {ps_quote(self.gate)} {args}", cwd=self.repo)

    def test_source_dry_run_reports_actions_without_mutations(self):
        result = self.preview("-Branches 'source-change,docs-only' -DryRun")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for expected in (
            "would verify SHA256 for 1 file(s)", "would merge source-change, docs-only",
            f"backup: {self.relative}", "changed: Source/новый файл.cpp",
            "would build", "would run Smart", "CodexTactics.WalkSmoke",
        ):
            self.assertIn(expected, result.stdout)
        self.assertNotIn("ready to push", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)
        self.assertEqual(self.designer.read_text(encoding="utf-8"), "dirty")
        self.assertFalse((self.repo / "Saved").exists())
        self.assertFalse((self.repo / "Source").exists())

    def test_docs_only_plan_skips_build_and_smokes(self):
        result = self.preview("-Branches docs-only -DryRun -FullTests")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("skip: no Source/ or .uproject changes", result.stdout)
        self.assertIn("would run full automation suite", result.stdout)
        self.assertIn("none mapped", result.stdout)

    def test_dry_run_rejects_a_staged_file_without_merging(self):
        git(self.repo, "add", "--", self.relative)
        result = self.preview("-Branches source-change -DryRun")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("The index contains staged or unresolved changes", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)
        self.assertFalse((self.repo / "Saved").exists())


if __name__ == "__main__":
    unittest.main()
