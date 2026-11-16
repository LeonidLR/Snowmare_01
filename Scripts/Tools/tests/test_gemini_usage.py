"""CLI regression tests for Gemini usage reports; no Gemini/API calls."""

import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "gemini_usage.py"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "gemini_usage_noisy.log"


class TestGeminiUsage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ledger = self.root / "ledger" / "usage.json"
        self.prices = self.root / "prices.json"
        self.env = os.environ.copy()
        self.env.update(GEMINI_USAGE_LEDGER=str(self.ledger),
                        GEMINI_USAGE_PRICES=str(self.prices), PYTHONIOENCODING="utf-8")

    def run_cli(self, log=FIXTURE):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(log), "--task", "033", "--model", "gemini-test"],
            capture_output=True, text=True, encoding="utf-8", env=self.env, timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        return result.stdout

    def test_noisy_log_reports_final_stats_and_writes_isolated_ledger(self):
        self.assertEqual(self.run_cli(),
                         "tokens: fresh in 1,500,000 + cached 750,000 + out 300,000 (5 requests); "
                         "cost n/a (set the price in Scripts/gemini_prices.json) "
                         "(today: 2,550,000 tokens, $0.00)\n")
        runs = json.loads(self.ledger.read_text(encoding="utf-8"))["runs"]
        self.assertEqual(len(runs), 1)
        run = runs[0]
        self.assertEqual({key: run[key] for key in
                          ("task", "model", "in", "cached", "out", "requests", "total", "cost")},
                         {"task": "033", "model": "gemini-test", "in": 1500000,
                          "cached": 750000, "out": 300000, "requests": 5,
                          "total": 2550000, "cost": None})
        datetime.datetime.fromisoformat(run["at"])

    def test_cached_price_and_existing_ledger_accumulate(self):
        self.prices.write_text(json.dumps({"gemini-test": {
            "input_per_1m": 2, "cached_per_1m": 0.5, "output_per_1m": 10,
        }}), encoding="utf-8")
        prefix = ("tokens: fresh in 1,500,000 + cached 750,000 + out 300,000 (5 requests); "
                  "cost $6.38 (today: ")
        self.assertEqual(self.run_cli(), prefix + "2,550,000 tokens, $6.38)\n")
        self.assertEqual(self.run_cli(), prefix + "5,100,000 tokens, $12.75)\n")
        runs = json.loads(self.ledger.read_text(encoding="utf-8"))["runs"]
        self.assertEqual(len(runs), 2)
        self.assertEqual([run["cost"] for run in runs], [6.375, 6.375])

    def test_no_json_reports_unavailable_without_writing_ledger(self):
        log = self.root / "early-exit.log"
        log.write_text("Starting Gemini CLI...\nRun interrupted before response.\n", encoding="utf-8")
        self.assertEqual(self.run_cli(log),
                         "tokens n/a (no JSON stats in the log - run ended early?)\n")
        self.assertFalse(self.ledger.exists())


if __name__ == "__main__":
    unittest.main()
