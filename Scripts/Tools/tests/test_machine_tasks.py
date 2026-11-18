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


if __name__ == "__main__":
    unittest.main()
