"""
Unit tests for morning_qa_report.py (#41 Auto-QA)
Run via: python -m unittest Scripts/Tools/tests/test_morning_qa_report.py
"""

import os
import sys
import tempfile
import unittest
import json

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
import morning_qa_report as qa  # noqa: E402


class MorningQAReportTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = self.temp_dir.name
        self.reporter = qa.MorningQAReporter(root_dir=self.root, report_date="2026-11-12")

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_fingerprint_normalization(self):
        # Different memory addresses or line numbers should normalize to the same fingerprint
        fp1 = self.reporter.make_fingerprint("ASSERT", "Assertion failed: Ptr != nullptr at line 42")
        fp2 = self.reporter.make_fingerprint("ASSERT", "Assertion failed: Ptr != nullptr at line 185")
        self.assertEqual(fp1, fp2)

        # Different error messages should yield different fingerprints
        fp3 = self.reporter.make_fingerprint("ASSERT", "Assertion failed: bHasWeapon at line 42")
        self.assertNotEqual(fp1, fp3)

    def test_parse_unreal_log_incidents(self):
        sample_log = """
[2026.11.11-13.04.22:058][352]LogCodexTactics: [EnvTrace] start cover on
[2026.11.11-13.05.05:120][614]LogStreaming: Display: FlushAsyncLoading(608): 3 QueuedPackages [0.245842s / 2.342111s] took 2.34 s
[2026.11.11-13.05.07:462][614]LogSkeletalMesh: Display: Waiting for skinned assets to be ready 0/1 (SKM_PmcOperator)
[2026.11.11-13.05.14:000][615]LogOutputDevice: Error: === Critical error: ===
Fatal error: [File:D:/Build/UnrealEngine/Engine/Source/Runtime/Core/Private/Misc/AssertionMacros.cpp] [Line: 482] 
Assertion failed: Target != nullptr [File:Source/CodexTactics/Private/Combat/WeaponRules.cpp] [Line: 120]
[2026.11.11-13.06.00:100][800]LogCodexTactics: [Bot] stuck in doorway after 4.5s
[2026.11.11-13.06.30:000][950]LogCodexTactics: MON_FIRE_ARC: residual angle 38.5 deg exceeds prone limit 15.0 deg
"""
        log_path = os.path.join(self.root, "test_session.log")
        with open(log_path, "w", encoding="utf-8") as f:
            f.write(sample_log)

        self.reporter.parse_unreal_log(log_path)
        self.assertEqual(self.reporter.total_logs_scanned, 1)

        # Check clusters
        categories = [c.category for c in self.reporter.clusters.values()]
        self.assertIn("CRASH_ASSERT", categories)
        self.assertIn("PERF_HITCH", categories)
        self.assertIn("BOT_STALL", categories)
        self.assertIn("MON_FIRE_ARC", categories)

        # Check severity of assertion
        assert_cluster = next(c for c in self.reporter.clusters.values() if c.category == "CRASH_ASSERT")
        self.assertEqual(assert_cluster.severity, "BLOCKER")

    def test_deduplication(self):
        # 5 identical hitch lines should fold into 1 cluster with count = 5
        log_lines = "\n".join([
            "LogStreaming: Display: FlushAsyncLoading(100): 1 QueuedPackages took 1.20 s" for _ in range(5)
        ])
        log_path = os.path.join(self.root, "hitch.log")
        with open(log_path, "w", encoding="utf-8") as f:
            f.write(log_lines)

        self.reporter.parse_unreal_log(log_path)
        perf_clusters = [c for c in self.reporter.clusters.values() if c.category == "PERF_HITCH"]
        self.assertEqual(len(perf_clusters), 1)
        self.assertEqual(perf_clusters[0].count, 5)

    def test_deduplication_keeps_max_severity_both_orders(self):
        # Same fingerprint with different severities must keep max severity (HIGH) regardless of log order
        med_line = "LogStreaming: Display: FlushAsyncLoading(100): 1 QueuedPackages took 0.50 s\n"
        high_line = "LogStreaming: Display: FlushAsyncLoading(100): 1 QueuedPackages took 3.00 s\n"
        for order_name, lines in [("med_then_high", [med_line, high_line]), ("high_then_med", [high_line, med_line])]:
            with self.subTest(order=order_name):
                reporter = qa.MorningQAReporter(root_dir=self.root, report_date="2026-11-12")
                for idx, content in enumerate(lines):
                    p = os.path.join(self.root, f"{order_name}_{idx}.log")
                    with open(p, "w", encoding="utf-8") as f:
                        f.write(content)
                    reporter.parse_unreal_log(p)
                perf_clusters = [c for c in reporter.clusters.values() if c.category == "PERF_HITCH"]
                self.assertEqual(len(perf_clusters), 1)
                self.assertEqual(perf_clusters[0].count, 2)
                self.assertEqual(perf_clusters[0].severity, "HIGH")

    def test_parse_telemetry_jsonl(self):
        records = [
            {"run_id": "run_01", "map": "L_Gen_Depot_1", "outcome": {"status": "SUCCESS", "score": 0.85}},
            {"run_id": "run_02", "map": "L_Gen_Depot_1", "outcome": {"status": "FAILED", "reason": "SquadWiped"}},
            {"run_id": "run_03", "map": "L_FormationLabCombat", "outcome": {"status": "TIMEOUT", "time": 300.0}},
        ]
        jsonl_path = os.path.join(self.root, "runs.jsonl")
        with open(jsonl_path, "w", encoding="utf-8") as f:
            for r in records:
                f.write(json.dumps(r) + "\n")

        self.reporter.parse_telemetry_jsonl(jsonl_path)
        telemetry_clusters = [c for c in self.reporter.clusters.values() if c.category == "BOT_TELEMETRY"]
        self.assertEqual(len(telemetry_clusters), 2)  # FAILED and TIMEOUT

    def test_generate_report_markdown(self):
        # Add sample issue
        self.reporter.record_issue("CRASH_ASSERT", "Assertion failed: Valid(Pawn)", "Sample error", "test.log", "BLOCKER")
        out_path = os.path.join(self.root, "Saved", "Orchestrator", "qa", "morning-2026-11-12.md")
        self.reporter.write_report(out_path)

        self.assertTrue(os.path.exists(out_path))
        with open(out_path, "r", encoding="utf-8") as f:
            text = f.read()

        self.assertIn("# Automated Morning QA Digest — 2026-11-12", text)
        self.assertIn("## 1. Proposed QA Tasks for TASK_BOARD.md", text)
        self.assertIn("QA-1112-01", text)
        self.assertIn("BLOCKER", text)
        self.assertIn("## 3. C++ Passive Runtime Sensors Backlog (Codex Follow-Ups)", text)
        self.assertIn("MON_STUCK", text)


if __name__ == "__main__":
    unittest.main()
