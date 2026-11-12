import contextlib
import copy
import io
import json
import pathlib
import threading
import unittest
from unittest.mock import patch

import jev_client
import model_router as router

ROOT = pathlib.Path(__file__).resolve().parents[2]
TASK = "Implement the requested gameplay feature with automated coverage"
CODEX_IDS = {
    "1": "gpt-6-luna",
    "2": "gpt-6.1-sol",
    "3": "gpt-6.1-terra",
    "4": "gpt-6-astra",
}


def answers(level="3", confidence=0.8, followup=0.0):
    return ({"level": {"choice": level, "probabilities": {level: confidence}},
             "followup": {"noul": followup}}, None)


class RouterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with (ROOT / ".claude" / "model_router.json").open(encoding="utf-8") as f:
            cls.config = json.load(f)
        with (ROOT / "Scripts" / "Tools" / "model_routes.json").open(encoding="utf-8") as f:
            cls.routes = json.load(f)

    def classify(self, response, text=TASK):
        with patch.object(jev_client, "ask", return_value=response) as ask:
            result = router.classify(text, self.config, timeout=0.05)
        ask.assert_called_once()
        return result

    def test_four_levels_map_to_allowed_codex_models(self):
        codex = self.routes["providers"]["codex"]
        self.assertEqual(set(codex["levels"]), set(CODEX_IDS))
        self.assertEqual(set(codex["models"].values()), set(CODEX_IDS.values()))
        self.assertFalse(codex["local_first_for_level_1"])
        for level, expected_id in CODEX_IDS.items():
            with self.subTest(level=level):
                result = self.classify(answers(level))
                self.assertTrue(result["route"])
                self.assertEqual(result["level"], level)
                self.assertEqual(codex["models"][codex["levels"][result["level"]]], expected_id)
                # The shared classifier retains the Claude-facing config; Codex maps the level separately.
                self.assertEqual(result["model"], self.config["levels"][level]["model_name"])
                self.assertEqual(result["agent"], self.config["levels"][level]["agent"])

    def test_claude_config_remains_claude(self):
        self.assertEqual(self.config["provider"], "claude-code-subagents")
        self.assertEqual(set(self.config["levels"]), set(CODEX_IDS))
        for level, model in {"1": "haiku", "2": "sonnet", "3": "opus", "4": "fable"}.items():
            self.assertEqual(self.config["levels"][level]["model"], model)
            self.assertTrue(self.config["levels"][level]["agent"].startswith("ue-l" + level + "-"))

    def test_followup_is_handled_without_routing(self):
        with patch.object(jev_client, "ask") as ask:
            result = router.classify("ок", self.config)
        ask.assert_not_called()
        self.assertTrue(result["followup"])
        self.assertFalse(result["route"])
        result = self.classify(answers(followup=0.9))
        self.assertTrue(result["followup"])
        self.assertFalse(result["route"])

    def test_low_confidence_and_unknown_level_do_not_route(self):
        low = self.classify(answers(confidence=0.59))
        self.assertFalse(low["route"])
        self.assertEqual(low["level"], "3")
        self.assertIn("confidence", low["reason"])
        boundary = self.classify(answers(confidence=self.config["confidence_threshold"]))
        self.assertTrue(boundary["route"])
        unknown = self.classify(answers(level="9"))
        self.assertFalse(unknown["route"])
        self.assertIsNone(unknown["level"])

    def test_jev_error_and_exception_do_not_route(self):
        failed = self.classify((None, "unavailable"))
        self.assertTrue(failed["error"])
        self.assertFalse(failed["route"])
        # The current router's worker reports uncaught exceptions as a timeout; suppress the thread traceback.
        with patch.object(jev_client, "ask", side_effect=RuntimeError("failed")), \
             patch.object(threading, "excepthook"):
            failed = router.classify(TASK, self.config, timeout=0.01)
        self.assertTrue(failed["error"])
        self.assertFalse(failed["route"])

    def test_timeout_does_not_route(self):
        release = threading.Event()
        try:
            with patch.object(jev_client, "ask", side_effect=lambda *a, **k: (release.wait(2), None)):
                result = router.classify(TASK, self.config, timeout=0.01)
            self.assertTrue(result["error"])
            self.assertIn("timeout", result["reason"])
            self.assertFalse(result["route"])
        finally:
            release.set()

    def test_disabled_hook_does_not_query_or_write(self):
        config = dict(self.config, enabled=False)
        with patch.object(router, "load_config", return_value=config), \
             patch.object(jev_client, "ask") as ask, \
             patch.object(router, "save_stats") as save, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(router.main(["--hook"]), 0)
        ask.assert_not_called()
        save.assert_not_called()
        self.assertEqual(output.getvalue(), "")

    def test_enabled_claude_hook_emits_claude_agent(self):
        config = copy.deepcopy(self.config)
        config["enabled"] = True
        stdin = io.TextIOWrapper(io.BytesIO(json.dumps({"prompt": TASK}).encode("utf-8")), encoding="utf-8")
        with patch.object(router, "load_config", return_value=config), \
             patch.object(router, "load_stats", return_value={"routed": {"1": 0, "2": 0, "3": 0, "4": 0},
                 "self_low_confidence": 0, "self_followup": 0, "errors": 0, "tokens_saved_estimate": 0,
                 "minutes_saved_estimate": 0.0}), \
             patch.object(router, "save_stats") as save, \
             patch.object(jev_client, "ask", return_value=answers("2")), \
             patch("sys.stdin", stdin), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(router.main(["--hook"]), 0)
        note = json.loads(output.getvalue())["hookSpecificOutput"]["additionalContext"]
        self.assertIn(config["levels"]["2"]["agent"], note)
        self.assertIn(config["levels"]["2"]["model_name"], note)
        save.assert_called_once()

    def test_mocked_routing_never_invokes_ollama_or_network(self):
        with patch("subprocess.Popen", side_effect=AssertionError("unexpected process")) as process, \
             patch("urllib.request.urlopen", side_effect=AssertionError("unexpected network")) as network, \
             patch.object(jev_client, "ask", return_value=answers("1")):
            result = router.classify(TASK, self.config, timeout=0.05)
        self.assertTrue(result["route"])
        process.assert_not_called()
        network.assert_not_called()
        self.assertFalse(self.routes["providers"]["codex"]["local_first_for_level_1"])


if __name__ == "__main__":
    unittest.main()
