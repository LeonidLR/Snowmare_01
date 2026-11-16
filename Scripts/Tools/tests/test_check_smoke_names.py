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


def run(repo: Path, *args: str) -> tuple[int, str]:
    out = io.StringIO()
    with redirect_stdout(out):
        code = check_smoke_names.main(["--repo", str(repo), *args])
    return code, out.getvalue()


class CheckSmokeNamesTests(unittest.TestCase):
    def test_all_known_exit_zero(self):
        code, output = run(FIXTURE)
        self.assertEqual(code, 0, output)
        self.assertIn("OK", output)

    def test_reverse_all_covered_exit_zero(self):
        code, output = run(FIXTURE, "--reverse")
        self.assertEqual(code, 0, output)
        self.assertNotIn("MISSING", output)

    def test_reverse_reports_registry_and_impact_separately(self):
        with tempfile.TemporaryDirectory(prefix="check-smoke-") as tmp:
            repo = Path(tmp) / "repo"
            shutil.copytree(FIXTURE, repo)
            path = repo / "Scripts" / "test_map.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["smokeRegistry"] = ["CodexTactics.FooSmoke"]
            data["rules"][0]["smokes"] = ["CodexTactics.BarSmoke"]
            path.write_text(json.dumps(data), encoding="utf-8")
            code, output = run(repo, "--reverse")
        self.assertEqual(code, 1)
        self.assertIn("MISSING smokeRegistry: BarSmoke", output)
        self.assertIn("MISSING impact-map smokes: FooSmoke", output)
        self.assertNotIn("MISSING smokeRegistry: FooSmoke", output)
        self.assertNotIn("MISSING impact-map smokes: BarSmoke", output)

    def test_reverse_impact_only_missing_does_not_fail(self):
        with tempfile.TemporaryDirectory(prefix="check-smoke-") as tmp:
            repo = Path(tmp) / "repo"
            shutil.copytree(FIXTURE, repo)
            path = repo / "Scripts" / "test_map.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["rules"] = [{"nested": [{"smokes": ["CodexTactics.FooSmoke"]}]}]
            path.write_text(json.dumps(data), encoding="utf-8")
            code, output = run(repo, "--reverse")
        self.assertEqual(code, 0, output)
        self.assertIn("MISSING impact-map smokes: BarSmoke", output)
        self.assertNotIn("MISSING smokeRegistry", output)

    def test_reverse_ignores_non_smoke_and_other_prefix_commands(self):
        with tempfile.TemporaryDirectory(prefix="check-smoke-") as tmp:
            repo = Path(tmp) / "repo"
            shutil.copytree(FIXTURE, repo)
            path = repo / "Source" / "X" / "Private" / "Debug" / "Extra.cpp"
            path.write_text(
                'static FAutoConsoleCommand Other(TEXT("Other.GhostSmoke"), TEXT("x"));\n'
                'static FAutoConsoleCommand Probe(TEXT("CodexTactics.Probe"), TEXT("x"));\n'
                'static FAutoConsoleCommand Pie(TEXT("CodexTactics.GhostSmokePIE"), TEXT("x"));\n',
                encoding="utf-8")
            code, output = run(repo, "--reverse")
        self.assertEqual(code, 0, output)
        self.assertNotIn("MISSING", output)

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
