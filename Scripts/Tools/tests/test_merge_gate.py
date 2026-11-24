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
        timeout=45,
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



    def test_worker_jobs_use_day_and_night_bounds_with_explicit_override(self):
        values = helper(
            "ConvertTo-Json -Compress -InputObject @("
            "(Get-GateJobs 0 ([datetime]'2026-11-24T07:59:59')),"
            "(Get-GateJobs 0 ([datetime]'2026-11-24T08:00:00')),"
            "(Get-GateJobs 0 ([datetime]'2026-11-24T16:29:59')),"
            "(Get-GateJobs 0 ([datetime]'2026-11-24T16:30:00')),"
            "(Get-GateJobs 3 ([datetime]'2026-11-24T21:30:00')))"
        )
        self.assertEqual(values, [1, 2, 2, 1, 3])


class MergeGateFixture(unittest.TestCase):
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

    def preview(self, args, setup=""):
        return powershell(
            "function Get-CimInstance { param($ClassName, $Filter) }\n" + setup
            + f"\n& {ps_quote(self.gate)} {args}\nexit $LASTEXITCODE", cwd=self.repo
        )



class MergeGateDryRun(MergeGateFixture):
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


class MergeGateExecution(MergeGateFixture):
    """Use real merges only in temporary Git fixtures. Build, automation and worker are stubs."""

    def setUp(self):
        super().setUp()
        scripts = self.repo / "Scripts"
        (self.repo / ".gitignore").write_text("Saved/\n", encoding="utf-8")
        (scripts / "agent_lock.ps1").write_text(
            "function Enter-AgentLock { param($Purpose); return $true }\nfunction Exit-AgentLock {}\n", encoding="utf-8"
        )
        (scripts / "build.ps1").write_text("Write-Host 'fixture: build'\nexit 0\n", encoding="utf-8")
        (scripts / "test.ps1").write_text(
            """param([switch]$Smart, [switch]$SkipSmokes, [string]$Base, [string]$Filter)
New-Item Saved -ItemType Directory -Force | Out-Null
if ($env:CODEX_GATE_FIXTURE_PARALLEL -eq '1') {
    $deadline = (Get-Date).AddSeconds(8)
    while (-not (Test-Path Saved/worker-started) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (-not (Test-Path Saved/worker-started)) { exit 1 }
}
Add-Content Saved/test-bases.txt $Base
if ($env:CODEX_GATE_FIXTURE_TOUCH -eq '1') {
    $designer = Get-ChildItem -LiteralPath Content -File | Where-Object { $_.Name.StartsWith('[') } | Select-Object -First 1
    [IO.File]::WriteAllText($designer.FullName, 'changed during validation')
}
if ($env:CODEX_GATE_FIXTURE_TEST_FAIL -eq '1') { exit 1 }
exit 0
""",
            encoding="utf-8",
        )
        (scripts / "Tools/worker_sync.ps1").write_text(
            r"""param([string]$RemoteRoot, [switch]$ShipHead, [switch]$ShareMachine, [int]$Jobs,
    [string]$Commands, [string]$ResultDir, [string]$ShipRef, [string]$Ref, [switch]$Build, [switch]$AllLogs, [switch]$SkipSmoke)
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Set-Location -LiteralPath $repo
New-Item Saved -ItemType Directory -Force | Out-Null
Add-Content Saved/worker-runs.txt "$ShipRef|$Build|$Jobs|$SkipSmoke|$Commands"
if ($SkipSmoke) { exit 0 }
if ($env:CODEX_GATE_FIXTURE_PARALLEL -eq '1') {
    Set-Content Saved/worker-started 'started'
    $deadline = (Get-Date).AddSeconds(8)
    while (-not (Test-Path Saved/test-bases.txt) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 50 }
    if (-not (Test-Path Saved/test-bases.txt)) { exit 2 }
}
$mode = if ($Build) { $env:CODEX_GATE_FIXTURE_BASELINE } else { $env:CODEX_GATE_FIXTURE_SMOKE }
$state = if ($mode -in @('STALE','INFRA')) { 'FAIL' } elseif ($mode) { $mode } else { 'PASS' }
New-Item $ResultDir -ItemType Directory -Force | Out-Null
$rows = @($Commands.Split(',') | ForEach-Object {
    $name = $_ -replace '^CodexTactics\.', ''
    $log = Join-Path $ResultDir "Smoke-$name.log"
    if ($mode -eq 'INFRA') { Set-Content $log 'GetLastError=4551' }
    else {
        $verdict = if ($state -eq 'KNOWN-FAIL') { 'FAIL' } else { $state }
        Set-Content $log "Smoke RESULT: $verdict"
    }
    @{ Name = $name; State = $state; Log = $log }
})
$json = Join-Path $ResultDir 'SmokeParallel-worker.json'
@{results = $rows} | ConvertTo-Json -Depth 4 | Set-Content $json
if ($mode -eq 'STALE') { (Get-Item -LiteralPath $json).LastWriteTimeUtc = [datetime]::UtcNow.AddDays(-1) }
if ($state -ne 'PASS') { exit 1 }
exit 0
""",
            encoding="utf-8",
        )
        git(self.repo, "add", "--", ".gitignore", "Scripts")
        git(self.repo, "commit", "-qm", "fixture runners")
        self.head = git(self.repo, "rev-parse", "HEAD")

    def test_worker_bundle_advertises_only_the_pinned_unpushed_commit(self):
        pinned = git(self.repo, "rev-parse", "source-change")
        git(self.repo, "update-ref", "refs/remotes/origin/main", self.head)
        bundle = self.repo / "candidate.bundle"
        script = f"""
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    {ps_quote(TOOLS / "worker_sync.ps1")}, [ref]$tokens, [ref]$errors)
$helper = $ast.Find({{
    $args[0] -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $args[0].Name -eq 'New-WorkerCommitBundle'
}}, $true)
. ([scriptblock]::Create($helper.Extent.Text))
New-WorkerCommitBundle {ps_quote(self.repo)} {ps_quote(pinned)} {ps_quote(bundle)} fixture-079
"""
        result = powershell("$ErrorActionPreference = 'Stop'\n" + script, cwd=self.repo)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ref = result.stdout.strip()
        self.assertEqual(ref, "refs/worker-sync/fixture-079")
        self.assertEqual(git(self.repo, "bundle", "list-heads", str(bundle)), f"{pinned} {ref}")
        self.assertEqual(git(self.repo, "for-each-ref", ref), "")
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)

    def test_red_smoke_names_visible_even_when_worker_exits_nonzero(self):
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_SMOKE='FAIL'")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("WalkSmoke=FAIL", result.stdout)

    def test_red_run_requires_resume_and_keeps_pre_merge_base(self):
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_TEST_FAIL='1'")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        after_merge = git(self.repo, "rev-parse", "HEAD")
        self.assertNotEqual(after_merge, self.head)
        plain = self.preview("-Branches source-change")
        self.assertNotEqual(plain.returncode, 0, plain.stdout + plain.stderr)
        self.assertNotIn("ready to push", plain.stdout)
        resumed = self.preview("-Resume")
        self.assertEqual(resumed.returncode, 0, resumed.stdout + resumed.stderr)
        self.assertIn("ready to push", resumed.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), after_merge)
        self.assertEqual((self.repo / "Saved/test-bases.txt").read_text().splitlines(), [self.head, self.head])

    def test_second_branch_conflict_leaves_head_and_index_untouched(self):
        for branch, text in (("conflict-a", "one"), ("conflict-b", "two")):
            git(self.repo, "checkout", "-qb", branch, self.head)
            file = self.repo / "collision.txt"
            file.write_text(text, encoding="utf-8")
            git(self.repo, "add", "--", file.name)
            git(self.repo, "commit", "-qm", branch)
        git(self.repo, "checkout", "-q", "main")
        result = self.preview("-Branches conflict-a,conflict-b")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("conflict-b", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)
        self.assertFalse((self.repo / ".git/MERGE_HEAD").exists())
        self.assertFalse((self.repo / "collision.txt").exists())


    def test_dry_run_fails_closed_for_unreadable_editor(self):
        result = self.preview(
            "-Branches source-change -DryRun",
            "function Get-CimInstance { [pscustomobject]@{ ProcessId=42; CommandLine=$null } }",
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Cannot read CommandLine for Unreal Editor PID 42", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)

    def test_editor_is_rechecked_before_build(self):
        setup = """
$global:fixtureEditorQueries = 0
function Get-CimInstance {
    $global:fixtureEditorQueries++
    if ($global:fixtureEditorQueries -eq 3) {
        [pscustomobject]@{ ProcessId=43; CommandLine=$null }
    }
}
"""
        result = self.preview("-Branches source-change", setup)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Build: FAIL: Cannot read CommandLine for Unreal Editor PID 43", result.stdout)
        self.assertNotIn("fixture: build", result.stdout)
        self.assertNotEqual(git(self.repo, "rev-parse", "HEAD"), self.head)

    def test_rejects_tip_in_head_or_empty_diff(self):
        already = self.preview("-Branches main -DryRun")
        self.assertEqual(already.returncode, 1, already.stdout)
        self.assertIn("already in HEAD", already.stdout)
        git(self.repo, "checkout", "-qb", "empty-diff")
        git(self.repo, "commit", "--allow-empty", "-qm", "empty")
        git(self.repo, "checkout", "-q", "main")
        empty = self.preview("-Branches empty-diff -DryRun")
        self.assertEqual(empty.returncode, 1, empty.stdout)
        self.assertIn("empty merge diff", empty.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)

    def test_later_branch_cannot_overlap_dirty_designer_file(self):
        git(self.repo, "checkout", "-qb", "touch-designer")
        self.designer.write_text("branch change", encoding="utf-8")
        git(self.repo, "add", "--", self.relative)
        git(self.repo, "commit", "-qm", "conflict with designer")
        git(self.repo, "checkout", "-q", "main")
        self.designer.write_text("dirty", encoding="utf-8")
        result = self.preview("-Branches source-change,touch-designer")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Merge overlaps a designer path", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)
        self.assertEqual(self.designer.read_text(encoding="utf-8"), "dirty")

    def test_preflight_protects_ignored_designer_asset_in_later_branch(self):
        git(self.repo, "checkout", "-qb", "ignored-asset")
        asset = self.repo / "Saved" / "designer-local-kit.json"
        asset.parent.mkdir(exist_ok=True)
        asset.write_text("branch version", encoding="utf-8")
        git(self.repo, "add", "-f", "--", "Saved/designer-local-kit.json")
        git(self.repo, "commit", "-qm", "track formerly ignored file")
        git(self.repo, "checkout", "-q", "main")
        asset.parent.mkdir(exist_ok=True)
        asset.write_text("designer version", encoding="utf-8")
        result = self.preview("-Branches source-change,ignored-asset")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("ignored local path at Saved/designer-local-kit.json", result.stdout)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.head)
        self.assertEqual(asset.read_text(encoding="utf-8"), "designer version")


    def test_actual_unexpected_conflict_aborts_second_merge(self):
        for branch, content in (("actual-a", "one"), ("unexpected-b", "two")):
            git(self.repo, "checkout", "-qb", branch, self.head)
            file = self.repo / "actual-conflict.txt"
            file.write_text(content, encoding="utf-8")
            git(self.repo, "add", "--", file.name)
            git(self.repo, "commit", "-qm", branch)
        git(self.repo, "checkout", "-q", "main")
        setup = """
$global:fixtureMerges = 0
function git {
    if ($args[0] -eq 'merge' -and $args[1] -ne '--abort') {
        $global:fixtureMerges++
        if ($global:fixtureMerges -eq 2) {
            & git.exe merge --no-ff --no-edit unexpected-b
            return
        }
    }
    & git.exe @args
}
"""
        result = self.preview("-Branches actual-a,docs-only", setup)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn(f"git reset --keep {self.head}", result.stdout)
        self.assertFalse((self.repo / ".git/MERGE_HEAD").exists())
        self.assertEqual(git(self.repo, "status", "--porcelain", "--", "actual-conflict.txt"), "")
        self.assertEqual(self.designer.read_text(encoding="utf-8"), "dirty")

    def test_known_fail_is_reachable_after_worker_exit_one(self):
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_SMOKE='KNOWN-FAIL'")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("WalkSmoke=KNOWN-FAIL", result.stdout)
        self.assertIn("ready to push", result.stdout)

    def test_stale_worker_json_cannot_pass_or_run_a_baseline(self):
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_SMOKE='STALE'")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("did not return a fresh smoke result", result.stdout)
        self.assertEqual((self.repo / "Saved/worker-runs.txt").read_text().count("\n"), 1)

    def test_launch_failure_is_not_reclassified_as_pre_existing(self):
        env = "$env:CODEX_GATE_FIXTURE_SMOKE='INFRA'; $env:CODEX_GATE_FIXTURE_BASELINE='INFRA'"
        result = self.preview("-Branches source-change -AcceptPreExisting", env)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("WalkSmoke=FAIL has no completed smoke failure", result.stdout)
        self.assertNotIn("ready to push", result.stdout)
        self.assertEqual((self.repo / "Saved/worker-runs.txt").read_text().count("\n"), 1)

    def test_baseline_is_pinned_to_saved_pre_merge_sha_and_requires_acceptance(self):
        env = "$env:CODEX_GATE_FIXTURE_SMOKE='FAIL'; $env:CODEX_GATE_FIXTURE_BASELINE='FAIL'"
        red = self.preview("-Branches source-change -Jobs 2", env)
        self.assertEqual(red.returncode, 2, red.stdout)
        self.assertIn("PRE-EXISTING", red.stdout)
        self.assertNotIn("ready to push", red.stdout)
        candidate = git(self.repo, "rev-parse", "HEAD")
        calls = (self.repo / "Saved/worker-runs.txt").read_text().splitlines()
        self.assertEqual(calls, [
            f"{candidate}|False|2|False|CodexTactics.WalkSmoke",
            f"{self.head}|True|2|False|CodexTactics.WalkSmoke",
            f"{candidate}|False|2|True|",
        ])
        green = self.preview("-Resume -AcceptPreExisting", env)
        self.assertEqual(green.returncode, 0, green.stdout)
        self.assertIn("accepted by -AcceptPreExisting", green.stdout)
        self.assertIn("ready to push", green.stdout)

    def test_designer_files_are_rehashed_and_manifest_lists_untracked_files(self):
        untracked = self.repo / "Content" / "extra local file.json"
        untracked.write_text("untracked", encoding="utf-8")
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_TOUCH='1'")
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("designer files intact 0/1", result.stdout)
        self.assertIn("smoke selector: WalkSmoke <- Source/новый файл.cpp [source]", result.stdout)
        for label in ("Pre-merge SHA:", "Build log:", "Automation log:", "Worker result directory:"):
            self.assertIn(label, result.stdout)
        backups = list((self.repo / "Saved/Backups").glob("designer-wip-*"))
        self.assertEqual(len(backups), 1)
        manifest = json.loads((backups[0] / "Manifest.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["Untracked"], ["Content/extra local file.json"])
        self.assertEqual(manifest["Tracked"][0]["Path"], self.relative)
        self.assertEqual((backups[0] / self.relative).read_text(encoding="utf-8"), "dirty")

    def test_worker_and_local_checks_overlap(self):
        result = self.preview("-Branches source-change", "$env:CODEX_GATE_FIXTURE_PARALLEL='1'")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("Worker elapsed:", result.stdout)
        self.assertIn("designer files intact 1/1", result.stdout)


if __name__ == "__main__":
    unittest.main()
