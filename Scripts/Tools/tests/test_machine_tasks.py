#!/usr/bin/env python3
import os
import sys
import unittest

TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import machine_tasks as mt

NOW = 1_800_000_000


def P(name, cmd, pid=1, age_min=5):
    return {"name": name, "pid": pid, "start_time": NOW - age_min * 60, "command_line": cmd}


class TestMachineTasks(unittest.TestCase):
    def labels(self, procs):
        return [i["label"] for i in mt.classify_processes(procs, NOW)]

    def test_build_and_smoke(self):
        L = self.labels([
            P("UnrealBuildTool.exe", r"C:\UE\UnrealBuildTool.exe CodexTacticsEditor -Project=C:\x\CodexTactics-claude\CodexTactics.uproject"),
            P("UnrealEditor-Cmd.exe", r'"C:\x\CodexTactics-grok\CodexTactics.uproject" /Game/Maps/L -ExecCmds="CodexTactics.MovementSmoke" -nullrhi'),
            P("UnrealEditor-Cmd.exe", r"C:\x\CodexTactics\CodexTactics.uproject -run=pythonscript -script=C:\s\gen_thing.py"),
        ])
        self.assertIn("Сборка (claude)", L)
        self.assertIn("Smoke MovementSmoke (grok)", L)
        self.assertTrue(any(x.startswith("Python в редакторе: gen_thing.py") for x in L))

    def test_designer_editor(self):
        L = self.labels([P("UnrealEditor.exe", r"C:\UE\UnrealEditor.exe C:\x\CodexTactics\CodexTactics.uproject")])
        self.assertEqual(L, ["Редактор дизайнера (main)"])
        self.assertEqual(self.labels([P("UnrealEditor.exe", r"UnrealEditor.exe CodexTactics.uproject -game")]), [])

    def test_agents_and_scripts(self):
        L = self.labels([
            P("grok.exe", "grok"),
            P("claude.exe", "claude --bare -p x"),
            P("claude.exe", "claude"),
            P("powershell.exe", r"powershell -File Scripts\smoke_parallel.ps1"),
            P("python.exe", r"python Scripts\Trainer\run.py"),
            P("chrome.exe", "chrome"),
        ])
        self.assertEqual(sorted(L), sorted(["Грок", "API-агент Claude", "Параллельные smoke", "Ночной тренер"]))

    def test_running_and_merge(self):
        items = mt.classify_processes([P("grok.exe", "grok", 1, 3), P("grok.exe", "grok", 2, 10)], NOW)
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0]["count"], 2)
        self.assertEqual(items[0]["running_min"], 10)

    def test_scrub(self):
        s = mt.scrub("x sk-ant-abc123 --api-key SECRET ANTHROPIC_API_KEY=foo XAI_API_KEY bar")
        for bad in ("abc123", "SECRET", "foo", "bar"):
            self.assertNotIn(bad, s)

    def test_ubt_line(self):
        self.assertEqual(mt.parse_ubt_line("[14/15] Link [x64] UnrealEditor.dll"), (14, 15))
        self.assertIsNone(mt.parse_ubt_line("Log file: x"))

    def test_parse_commands(self):
        self.assertEqual(mt.parse_commands("powershell -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.A,B -Jobs 2"), ["A", "B"])
        self.assertEqual(mt.parse_commands("x -Commands all -Jobs 2"), ["all"])

    def test_smoke_counts(self):
        logs = [
            {"n": "Smoke-A.log", "t": 100, "r": "PASS"},
            {"n": "Smoke-B.log", "t": 100, "r": "FAIL"},
            {"n": "Smoke-B-alone.log", "t": 120, "r": "FAIL"},
            {"n": "Smoke-C.log", "t": 100, "r": ""},
            {"n": "Smoke-Old.log", "t": 10, "r": "PASS"},
        ]
        self.assertEqual(mt.smoke_counts(logs, 50), (2, 1))
        self.assertEqual(mt.smoke_counts(logs, 50, {"A"}), (1, 0))

    def test_progress_items(self):
        root = "c:/x/codextactics-claude"
        ctx = {root: {"root": root, "registry": 296,
                      "build": {"line": "[3/12] Compile x.cpp", "t": NOW},
                      "smokes": [{"n": "Smoke-A.log", "t": NOW - 60, "r": "PASS"}, {"n": "Smoke-B.log", "t": NOW - 30, "r": "FAIL"}]}}
        procs = [
            P("UnrealBuildTool.exe", r"UBT -Project=C:\x\CodexTactics-claude\CodexTactics.uproject", age_min=2),
            P("powershell.exe", r"powershell -File Scripts\smoke_parallel.ps1 -Commands A,B,C,D", age_min=4),
            P("grok.exe", "grok"),
        ]
        items = {i["kind"]: i for i in mt.classify_processes(procs, NOW, ctx)}
        b = items["Сборка"]["progress"]
        self.assertEqual((b["done"], b["total"], b["pct"], b["eta_min"]), (3, 12, 25.0, 6))
        s = items["Скрипт"]["progress"]
        self.assertEqual((s["done"], s["total"], s["state"]), (2, 4, "warn"))
        self.assertIn("1 FAIL", s["text"])
        self.assertTrue(items["Агент"]["progress"]["indeterminate"])

    def test_eta_math_and_stale_progress_ignored(self):
        self.assertIsNone(mt.median([]))
        self.assertEqual(mt.median([10, 30, 20]), 20.0)
        self.assertEqual(mt.median([10, 20, 30, 40]), 25.0)

        # Stale progress file (> 10 min = 600 s) is ignored; <= 600 s is kept
        self.assertIsNone(mt.read_progress_if_fresh(None, NOW))
        self.assertIsNone(mt.read_progress_if_fresh({"stage": "Build", "t": NOW - 601}, NOW))
        self.assertIsNotNone(mt.read_progress_if_fresh({"stage": "Build", "t": NOW - 599}, NOW))

        # Merge gate ETA: median of last 5 runs per stage, never negative, None when no history
        self.assertIsNone(mt.estimate_merge_gate_eta("Build", NOW - 30, [], NOW))
        hist = [
            {"stages": {"Build": 100, "Tests": 60, "Worker smokes": 200}},
            {"stages": {"Build": 120, "Tests": 80, "Worker smokes": 240}},
            {"stages": {"Build": 140, "Tests": 100, "Worker smokes": 260}},
        ]
        # Build median=120, elapsed=30 -> 90; Tests median=80; Worker smokes median=240 => 410s
        self.assertEqual(mt.estimate_merge_gate_eta("Build", NOW - 30, hist, NOW), 410)
        # Overtime on last stage never goes negative
        self.assertEqual(mt.estimate_merge_gate_eta("Worker smokes", NOW - 9999, hist, NOW), 0)

        # Remote build ETA from RemoteBuilds.jsonl median
        self.assertIsNone(mt.estimate_remote_build_eta(NOW - 10, [], 0, 3, NOW))
        rb_hist = [{"exit": 0, "total_s": 300}, {"exit": 0, "total_s": 400}, {"exit": 0, "total_s": 500}]
        self.assertEqual(mt.estimate_remote_build_eta(NOW - 100, rb_hist, 45, 84, NOW), 300)
        self.assertEqual(mt.estimate_remote_build_eta(NOW - 999, rb_hist, 45, 84, NOW), 0)

        # Smoke batch ETA = sum(remaining last durations) / jobs
        smoke_hist = [{"n": "A", "s": 60}, {"n": "B", "s": 120}, {"n": "C", "s": 180}]
        self.assertEqual(
            mt.estimate_smoke_batch_eta({"A", "B", "C"}, {"A"}, smoke_hist, 2, 60, 1, 3),
            150,
        )

    def test_merge_gate_and_remote_build_progress_items(self):
        root = "c:/x/codextactics"
        ctx = {
            root: {
                "root": root,
                "merge_gate": {
                    "stage": "Build",
                    "stage_index": 4,
                    "stage_count": 7,
                    "stage_started": NOW - 20,
                    "gate_started": NOW - 60,
                    "t": NOW - 5,
                },
                "merge_gate_history": [{"stages": {"Build": 80, "Tests": 40}}],
                "remote_build": {"stage": "compile", "stage_index": 2, "stage_count": 3, "started": NOW - 120, "t": NOW - 5},
                "remote_build_history": [{"exit": 0, "total_s": 480}],
                "build": {"line": "[45/84] Compile Foo.cpp", "t": NOW - 2},
            }
        }
        procs = [
            P("powershell.exe", r"powershell -File Scripts\Tools\merge_gate.ps1 -ProjectDir C:\x\CodexTactics", age_min=1),
            P("powershell.exe", r"powershell -File Scripts\Tools\build_remote.ps1 -ProjectDir C:\x\CodexTactics", age_min=2),
        ]
        items = {i["label"]: i for i in mt.classify_processes(procs, NOW, ctx)}
        mg = items["Merge gate (main)"]["progress"]
        self.assertEqual((mg["stage"], mg["done"], mg["total"], mg["eta_s"]), ("Сборка", 4, 7, 100))
        rb = items["Удалённая сборка (main)"]["progress"]
        self.assertEqual((rb["stage"], rb["done"], rb["total"], rb["eta_s"]), ("Сборка", 45, 84, 360))


if __name__ == "__main__":
    unittest.main()
