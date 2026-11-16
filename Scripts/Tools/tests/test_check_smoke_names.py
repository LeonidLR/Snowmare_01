"""Tests for check_smoke_names.py against a tiny fixture repo (read-only; unknown-name cases use a temp copy)."""
from __future__ import annotations

import io
import json
from contextlib import redirect_stdout
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import check_smoke_names

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "check_smoke_names"


def run(repo: Path) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out):
        code = check_smoke_names.main(["--repo", str(repo)])
    return code, out.getvalue()


class CheckSmokeNamesTests(unittest.TestCase):
    def test_all_known_exit_zero(self):
        code, output = run(FIXTURE)
        self.assertEqual(code, 0, output)
        self.assertIn("OK", output)

    def test_unknown_name_exit_one_and_printed(self):
        with tempfile.TemporaryDirectory(prefix="check-smoke-") as tmp:
            repo = Path(tmp) / "repo"
            shutil.copytree(FIXTURE, repo)
            path = repo / "Scripts" / "test_map.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["rules"][0]["smokes"].append("GhostSmoke")
            path.write_text(json.dumps(data), encoding="utf-8")
            code, output = run(repo)
        self.assertEqual(code, 1)
        self.assertIn("GhostSmoke", output)
        self.assertNotIn("FooSmoke", output)


if __name__ == "__main__":
    unittest.main()
