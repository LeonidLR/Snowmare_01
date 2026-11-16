"""Task027: pure process/status fixtures and a virtual admission clock."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import designer_busy as db


class DesignerBusyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.main = Path(self.tmp.name) / "CodexTactics"
        self.status_dir = self.main / "Saved" / "LevelGen"
        self.status_dir.mkdir(parents=True)
        (self.main / "CodexTactics.uproject").touch()
        self.now = 10000

    def proc(self, **updates):
        data = {"pid": 42, "name": "UnrealEditor.exe", "started_epoch": self.now - 60,
                "cmd": 'UnrealEditor.exe "' + str(self.main / "CodexTactics.uproject") + '"'}
        data.update(updates)
        return data

    def reason(self, processes):
        return db.busy_reason(self.main, processes, self.now, 15)

    def status(self, **updates):
        data = {"schema": "codex.job_status/1", "job": "build_map", "state": "running", "pid": 42}
        data.update(updates)
        path = self.status_dir / "build_test.json"
        path.write_text(json.dumps(data), encoding="utf-8-sig")
        return path

    def test_young_main_editor_only(self):
        self.assertEqual(self.reason([self.proc()]), "editor starting")
        original = self.proc()["cmd"]
        for proc in [self.proc(cmd=original.replace("CodexTactics/", "CodexTactics-agents/")),
                     self.proc(cmd=original.replace("CodexTactics\\", "CodexTactics-agents\\")),
                     self.proc(cmd=original + " -GAME"), self.proc(cmd=original + " -run=pythonscript"),
                     self.proc(name="UnrealEditor-Cmd.exe"), self.proc(started_epoch=self.now - 900),
                     self.proc(cmd=original.replace('.uproject"', '.uproject.bak"'))]:
            # Only the matching platform-specific replacement actually changes the path.
            if proc["cmd"] == original and proc == self.proc():
                continue
            with self.subTest(proc=proc):
                self.assertIsNone(self.reason([proc]))

    def test_quoted_mode_flags_and_explicit_project_argument(self):
        project = str(self.main / "CodexTactics.uproject")
        for argument in ['-project="' + project + '"', '"-project=' + project + '"',
                         '-project=' + project]:
            cmd = 'UnrealEditor.exe ' + argument
            with self.subTest(argument=argument):
                self.assertEqual(self.reason([self.proc(cmd=cmd)]), "editor starting")
                for flag in ['"-game"', '"-run=pythonscript"', '-game', '-run=pythonscript']:
                    self.assertIsNone(self.reason([self.proc(cmd=cmd + ' ' + flag)]))
                self.assertIsNone(self.reason([self.proc(cmd=cmd.replace('.uproject', '.uproject.bak'))]))
                self.assertIsNone(self.reason([self.proc(cmd=cmd.replace('CodexTactics.uproject', 'Other.uproject'))]))

    def test_slashes_case_and_unknown_creation_time(self):
        proc = self.proc()
        proc["cmd"] = proc["cmd"].replace("\\", "/").upper()
        self.assertEqual(self.reason([proc]), "editor starting")
        self.assertIn("unavailable", self.reason([self.proc(started_epoch=None)]))

    def test_running_bom_status_requires_live_owner(self):
        self.status()
        self.assertEqual(self.reason([{"pid": 42}]), "levelgen build running")
        self.assertIsNone(self.reason([{"pid": 99}]))
        for updates in [{"state": "done"}, {"state": "waiting_lock"}, {"schema": "other"},
                        {"job": "record_run"}, {"pid": None}, {"state": "failed"}]:
            self.status(**updates)
            self.assertIsNone(self.reason([{"pid": 42}]))

    def test_malformed_status_and_other_checkout_ignored(self):
        path = self.status()
        for text in ["{", "null", "[]", '"running"']:
            path.write_text(text, encoding="utf-8")
            self.assertIsNone(self.reason([{"pid": 42}]))
        path.unlink()
        other = self.main.parent / "CodexTactics-agents" / "Saved" / "LevelGen"
        other.mkdir(parents=True)
        (other / "build_test.json").write_text('{"schema":"codex.job_status/1","job":"build_map","state":"running","pid":42}')
        self.assertIsNone(self.reason([{"pid": 42}]))

    def test_config_bom_invalid_defaults(self):
        path = self.main / "priority.json"
        path.write_text('{"startup_grace_minutes":2,"poll_seconds":0,"max_wait_minutes":"bad"}', encoding="utf-8-sig")
        self.assertEqual(db.load_config(path), {"startup_grace_minutes": 2, "poll_seconds": 20, "max_wait_minutes": 30})
        path.write_text("null")
        self.assertEqual(db.load_config(path), db.DEFAULTS)

    def test_main_from_git_common_dir(self):
        with patch.object(db.editor_status, "_run", return_value=str(self.main / ".git")):
            self.assertEqual(db.main_checkout(self.main.parent / "CodexTactics-agents"), self.main.resolve())

    def wait(self, probe, minutes=2, environ=None):
        now, logs, sleeps = [0], [], []
        def sleep(seconds):
            sleeps.append(seconds)
            now[0] += seconds
        result = db.wait_for_designer(probe, {"max_wait_minutes": minutes, "poll_seconds": 20},
                                      environ=environ or {}, clock=lambda: now[0], sleep=sleep, log=logs.append)
        return result, logs, sleeps, now[0]

    def test_idle_and_override_silent(self):
        self.assertEqual(self.wait(lambda: None), (False, [], [], 0))
        def forbidden():
            self.fail("override must not probe")
        self.assertEqual(self.wait(forbidden, environ={"CODEX_IGNORE_DESIGNER": "1"}), (False, [], [], 0))
        with patch.dict(db.os.environ, {"CODEX_IGNORE_DESIGNER": "1"}), patch.object(db, "load_config", side_effect=AssertionError), patch.object(sys, "argv", ["designer_busy.py"]):
            with contextlib.redirect_stdout(io.StringIO()) as output:
                db.main()
            self.assertEqual(output.getvalue(), "")

    def test_timeout_and_once_per_minute(self):
        result, logs, sleeps, elapsed = self.wait(lambda: "editor starting", minutes=2.1)
        self.assertTrue(result)
        self.assertEqual(elapsed, 126)
        self.assertEqual(sleeps[-1], 6)
        self.assertEqual(sum("waiting for" in line for line in logs), 3)
        self.assertEqual(sum("expired" in line for line in logs), 1)

    def test_unknown_detection_bounded_and_clear_warning(self):
        def unknown():
            raise RuntimeError("CIM unavailable")
        result, logs, _, elapsed = self.wait(unknown, minutes=1)
        self.assertTrue(result)
        self.assertEqual(elapsed, 60)
        self.assertIn("detection unavailable", logs[-1])
        self.assertIn("continuing", logs[-1])

    def test_busy_then_idle(self):
        probes = iter(["editor starting", None])
        result, logs, _, elapsed = self.wait(lambda: next(probes))
        self.assertFalse(result)
        self.assertEqual(elapsed, 20)
        self.assertEqual(len(logs), 1)


if __name__ == "__main__":
    unittest.main()
