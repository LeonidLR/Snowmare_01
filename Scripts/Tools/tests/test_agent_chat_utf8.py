"""Regression test for agent_chat output in legacy console environments."""

import json
import os
import subprocess
import sys
import tempfile
import unittest


SCRIPT_PATH = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "agent_chat.py")
)


class TestAgentChatUtf8(unittest.TestCase):
    def test_read_prints_russian_message_with_cp1252_environment(self):
        message_text = "Привет, мир! Проверка кодировки."
        with tempfile.TemporaryDirectory() as root:
            storage_dir = os.path.join(root, "Saved", "AgentChat")
            os.makedirs(storage_dir)
            with open(os.path.join(storage_dir, "chat.jsonl"), "w", encoding="utf-8") as chat_file:
                chat_file.write(json.dumps({
                    "id": 1,
                    "ts": "2026-11-16T12:00:00+00:00",
                    "from": "designer",
                    "to": "codex",
                    "topic": "encoding",
                    "text": message_text,
                }, ensure_ascii=False) + "\n")

            env = os.environ.copy()
            env["PYTHONIOENCODING"] = "cp1252"
            for command in (["read"], ["read", "--json"], ["wait", "--as", "codex", "--timeout", "0"]):
                with self.subTest(command=command):
                    result = subprocess.run(
                        [sys.executable, SCRIPT_PATH, "--root", root, *command],
                        capture_output=True,
                        env=env,
                        check=False,
                        timeout=15,
                    )
                    self.assertEqual(result.returncode, 0, result.stderr.decode("utf-8", errors="replace"))
                    self.assertIn(message_text, result.stdout.decode("utf-8"))


if __name__ == "__main__":
    unittest.main()
