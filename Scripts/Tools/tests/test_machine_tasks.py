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


if __name__ == "__main__":
    unittest.main()
