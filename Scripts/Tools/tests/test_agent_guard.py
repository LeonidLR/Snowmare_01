import contextlib
import copy
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import agent_guard
import model_router


class AgentGuardTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.cli = self.root / "Scripts/Tools/agent_guard.py"
        self.cli.parent.mkdir(parents=True)
        shutil.copyfile(TOOLS / "agent_guard.py", self.cli)
        (self.root / "Saved/Lead").mkdir(parents=True)

    def run_hook(self, tool="Agent", **parameters):
        payload = {"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": parameters}
        return subprocess.run(
            [sys.executable, str(self.cli)], input=json.dumps(payload),
            capture_output=True, encoding="utf-8", check=False,
        )

    def test_denies_agents_without_team_flag_even_with_fable_flag(self):
        (self.root / agent_guard.FABLE_FLAG).touch()
        result = self.run_hook(model="fable", subagent_type="Explore")
        self.assertEqual(result.returncode, 2)
        self.assertIn(agent_guard.NO_SUBAGENTS, result.stderr)

    def test_open_gate_allows_ordinary_agent_and_blocks_premium_variants(self):
        (self.root / agent_guard.AGENT_FLAG).touch()
        self.assertEqual(self.run_hook(model="sonnet", subagent_type="Explore").returncode, 0)
        for model, agent in (
            ("fable", "Explore"),
            ("claude-fable-5-2", "Explore"),
            ("opus", "ue-l4-architect"),
        ):
            with self.subTest(model=model, agent=agent):
                result = self.run_hook(model=model, subagent_type=agent)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Saved/Lead/ALLOW_FABLE", result.stderr)
        (self.root / agent_guard.FABLE_FLAG).touch()
        self.assertEqual(self.run_hook(model="fable", subagent_type="ue-l4-architect").returncode, 0)

    def test_unrelated_tool_is_unaffected(self):
        self.assertEqual(self.run_hook(tool="Bash", command="echo ok").returncode, 0)

    def test_invalid_json_is_blocked(self):
        result = subprocess.run([sys.executable, str(self.cli)], input="{",
                                capture_output=True, encoding="utf-8", check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("cannot read hook input", result.stderr)

    def test_installer_joins_existing_agent_matcher_without_touching_other_settings(self):
        local = self.root / ".claude/settings.local.json"
        local.parent.mkdir()
        old = {
            "permissions": {"allow": ["Read"]},
            "hooks": {
                "UserPromptSubmit": [{"hooks": [{"type": "command", "command": "python model_router.py --hook"}]}],
                "PreToolUse": [
                    {"matcher": "Agent", "hooks": [{"type": "command", "command": "python agent_gate.py"}]},
                    {"matcher": "Bash", "hooks": [{"type": "command", "command": "python agent_gate.py"}]},
                ],
            },
        }
        local.write_text(json.dumps(old), encoding="utf-8")
        expected = copy.deepcopy(old)
        expected["hooks"]["PreToolUse"][0]["hooks"].append(agent_guard.HOOK)
        for _ in range(2):
            subprocess.run([sys.executable, str(self.cli), "--install-local"], check=True)
            self.assertEqual(json.loads(local.read_text(encoding="utf-8")), expected)


class RouterPolicyTests(unittest.TestCase):
    PROMPT = "Design a network architecture across replicated game worlds"

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.flags = self.root / "Saved/Lead"
        self.flags.mkdir(parents=True)
        self.config = model_router.load_config()

    def fake_jev(self, level):
        return types.SimpleNamespace(
            ask=lambda *args, **kwargs: ({"level": {"choice": level, "confidence": 0.99}}, None),
            noul=lambda *args: 0.0,
        )

    def classify(self, level):
        with patch.object(model_router, "ROOT", self.root), patch.dict(sys.modules, {"jev_client": self.fake_jev(level)}):
            return model_router.classify(self.PROMPT, self.config)

    def test_l4_never_recommends_an_agent_even_with_both_flags(self):
        (self.root / agent_guard.AGENT_FLAG).touch()
        (self.root / agent_guard.FABLE_FLAG).touch()
        result = self.classify("4")
        self.assertEqual(result["reason"], "do it yourself (Lead) or Codex")
        self.assertFalse(result["route"])
        self.assertIsNone(result["model"])
        self.assertIsNone(result["agent"])

    def test_l2_uses_general_flag_and_never_recommends_misconfigured_fable(self):
        denied = self.classify("2")
        self.assertEqual(denied["policy"], agent_guard.NO_SUBAGENTS)
        self.assertFalse(denied["route"])
        (self.root / agent_guard.AGENT_FLAG).touch()
        self.assertTrue(self.classify("2")["route"])
        self.config["levels"]["2"]["model"] = "fable"
        self.assertFalse(self.classify("2")["route"])

    def test_hook_emits_self_or_codex_and_the_guard_status_as_distinct_lines(self):
        stdin = io.TextIOWrapper(io.BytesIO(json.dumps({"prompt": self.PROMPT}).encode()))
        stdout = io.StringIO()
        with patch.object(model_router, "ROOT", self.root), patch.object(model_router, "save_stats"), \
                patch.dict(sys.modules, {"jev_client": self.fake_jev("4")}), patch("sys.stdin", stdin), contextlib.redirect_stdout(stdout):
            self.assertEqual(model_router.hook(), 0)
        note = json.loads(stdout.getvalue())["hookSpecificOutput"]["additionalContext"]
        self.assertEqual(note.splitlines(), ["[model-router] do it yourself (Lead) or Codex", agent_guard.NO_SUBAGENTS])


if __name__ == "__main__":
    unittest.main()
