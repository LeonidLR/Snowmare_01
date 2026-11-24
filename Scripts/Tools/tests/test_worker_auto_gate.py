"""Test the READY gate against a local bare origin and stub engine scripts."""

import datetime
import json
import os
from pathlib import Path
import shutil
from tempfile import TemporaryDirectory
import unittest

from test_merge_gate import git, powershell, ps_quote

TOOLS = Path(__file__).resolve().parents[1]
GATE = TOOLS / "worker_auto_gate.ps1"


class WorkerAutoGateTests(unittest.TestCase):
    def setUp(self):
        temp = TemporaryDirectory(prefix="codex-auto-gate-")
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        self.primary = root / "worker"
        self.primary.mkdir()
        (self.primary / "Scripts/Tools").mkdir(parents=True)
        shutil.copyfile(TOOLS / "agent_chat.py", self.primary / "Scripts/Tools/agent_chat.py")
        self.shared = root / "AgentChat"
        self.shared.mkdir()
        self.chat = self.shared / "chat.jsonl"
        self.chat.write_text("", encoding="utf-8")

        source = root / "source"
        (source / "Scripts/Tools").mkdir(parents=True)
        shutil.copyfile(TOOLS / "merge_gate.ps1", source / "Scripts/Tools/merge_gate.ps1")
        shutil.copyfile(TOOLS.parent / "verify_lib.ps1", source / "Scripts/verify_lib.ps1")
        (source / ".gitignore").write_text("Saved/\n", encoding="utf-8")
        (source / "Scripts/test_map.json").write_text(json.dumps({
            "rules": [{"name":"game", "globs":["Source/**"], "smokes":["FixtureSmoke"], "tests":["CodexTactics.Game"]}],
            "quickCount":1, "fallback":{"tests":[], "smokes":[]}, "smokeRegistry":["FixtureSmoke"],
        }), encoding="utf-8")
        (source / "Scripts/build.ps1").write_text(
            "if ($env:CODEX_LOCK_HELD -ne '1') { exit 4 }\nWrite-Output 'fixture build'\nexit 0\n", encoding="utf-8"
        )
        (source / "Scripts/test.ps1").write_text(
            "param([switch]$Smart,[switch]$SkipSmokes,[string]$Base)\n"
            "if (-not ($Smart -and $SkipSmokes -and $Base -eq (git rev-parse origin/main))) { exit 4 }\n"
            "Write-Output 'fixture tests'\nexit 0\n", encoding="utf-8"
        )
        (source / "Scripts/smoke_parallel.ps1").write_text(
            "param([string]$Remote,[string]$Commands,[int]$Jobs,[string]$ResultJson)\n"
            "if ($Remote -ne 'off' -or $Commands -ne 'CodexTactics.FixtureSmoke' -or $Jobs -ne 1) { exit 4 }\n"
            "$state=if($env:FIXTURE_SMOKE_FAIL){'FAIL'}else{'PASS'}\n"
            "@{results=@(@{Name='FixtureSmoke'; State=$state})} | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $ResultJson\n"
            "if($state -eq 'FAIL'){exit 1}; exit 0\n", encoding="utf-8"
        )
        git(source, "init", "-qb", "main")
        git(source, "config", "user.name", "Auto Gate Fixture")
        git(source, "config", "user.email", "autogate@example.invalid")
        git(source, "add", "--", ".gitignore", "Scripts")
        git(source, "commit", "-qm", "base")
        self.base = git(source, "rev-parse", "HEAD")
        git(source, "checkout", "-qb", "ready/code")
        (source / "Source").mkdir()
        (source / "Source/changed.cpp").write_text("fixture", encoding="utf-8")
        git(source, "add", "--", "Source")
        git(source, "commit", "-qm", "code candidate")
        self.tip = git(source, "rev-parse", "HEAD")
        git(source, "checkout", "-qb", "codex/docs", "main")
        (source / "readme.md").write_text("fixture", encoding="utf-8")
        git(source, "add", "--", "readme.md")
        git(source, "commit", "-qm", "doc candidate")
        self.docs_tip = git(source, "rev-parse", "HEAD")
        git(source, "checkout", "-q", "main")
        origin = root / "origin.git"
        git(root, "clone", "-q", "--bare", str(source), str(origin))
        self.scratch = root / "scratch"
        git(root, "clone", "-q", str(origin), str(self.scratch))

    def run_gate(self, options="", setup=""):
        command = f"""
$ProgressPreference='SilentlyContinue'
function Get-CimInstance {{ param($ClassName,$Filter) }}
function Get-Date {{
    param($Format)
    $date=[datetime]'2026-11-24T10:00:00'
    if($Format) {{ return $date.ToString($Format) }}
    return $date
}}
{setup}
& {ps_quote(GATE)} -Repo {ps_quote(self.scratch)} -WorkerRoot {ps_quote(self.primary)} -Slot1 '' -ChatRoot {ps_quote(self.shared)} {options}
exit $LASTEXITCODE
"""
        return powershell(command, cwd=self.scratch)

    def messages(self):
        return [json.loads(line) for line in self.chat.read_text(encoding="utf-8-sig").splitlines()]

    def test_dry_run_does_not_write_state_or_switch_head(self):
        result = self.run_gate("-DryRun")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"ready/code {self.tip} onto origin/main {self.base}", result.stdout)
        self.assertFalse((self.scratch / "Saved").exists())
        self.assertEqual(git(self.scratch, "branch", "--show-current"), "main")
        self.assertEqual(self.messages(), [])

    def test_code_builds_tests_smokes_once_and_leaves_main_at_base(self):
        first = self.run_gate()
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        message, = self.messages()
        self.assertEqual(message["topic"], "gate")
        self.assertIn("GATE ready/code PASS", message["text"])
        self.assertIn("build=PASS tests=PASS smokes=FixtureSmoke=PASS", message["text"])
        self.assertEqual(git(self.scratch, "branch", "--show-current"), "")
        self.assertEqual(git(self.scratch, "rev-parse", "main"), self.base)
        self.assertFalse((self.primary / "Saved/agent.lock").exists())
        self.assertFalse((self.scratch / "Saved/agent.lock").exists())
        second = self.run_gate()
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertIn("No ungated READY commits", second.stdout)
        self.assertEqual(self.messages(), [message])

    def test_ready_message_pins_tip_and_docs_skip_build(self):
        self.chat.write_text(json.dumps({
            "id":1,"topic":"gate","from":"claude","to":"all","text":f"READY codex/docs {self.docs_tip[:8]}",
        })+"\n",encoding="utf-8")
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("GATE codex/docs PASS", self.messages()[-1]["text"])
        self.assertIn("build=skip tests=PASS smokes=none", self.messages()[-1]["text"])

    def test_busy_lock_and_freeze_gate_admission(self):
        saved = self.primary / "Saved"
        saved.mkdir()
        lock = saved / "agent.lock"
        lock.write_text("another job", encoding="utf-8")
        busy = self.run_gate()
        self.assertEqual(busy.returncode, 0, busy.stdout + busy.stderr)
        self.assertIn("lock held", busy.stdout)
        self.assertEqual(lock.read_text(encoding="utf-8"), "another job")
        self.assertEqual(self.messages(), [])
        self.assertFalse((self.scratch / "Saved/agent.lock").exists())
        lock.unlink()
        (saved / "Worker").mkdir()
        frozen = saved / "Worker/FROZEN"
        frozen.write_text("freeze", encoding="utf-8")
        stamp = datetime.datetime(2026,11,24,9).timestamp()
        os.utime(frozen,(stamp,stamp))
        freeze = self.run_gate()
        self.assertEqual(freeze.returncode, 0, freeze.stdout + freeze.stderr)
        self.assertIn("worker busy or frozen", freeze.stdout)
        self.assertEqual(self.messages(), [])

    def test_night_and_ab_window_do_not_take_a_gate(self):
        for time in ("05:30", "07:30", "16:30"):
            result = self.run_gate(f"-DryRun -PreviewAt '2026-11-24T{time}:00'")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("outside 08:00-16:30", result.stdout)
        self.assertFalse((self.scratch / "Saved").exists())


if __name__ == "__main__":
    unittest.main()
