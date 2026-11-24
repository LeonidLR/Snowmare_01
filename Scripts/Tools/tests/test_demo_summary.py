"""Tests for the local demo summary CLI and JSONL v1 checks."""

from contextlib import redirect_stdout
from io import StringIO
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import demo_summary as demo


class DemoSummaryTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def recording(self, name="session.jsonl", flags=None, duration=60):
        rows = [
            {"type": "header", "v": 1, "map": "Kit", "flags": flags or {}},
            {"type": "order", "pmc_order": "Heal", "ok": True},
            {"type": "order", "pmc_order": None, "ok": False},
            {"type": "order", "pmc_order": "Unknown", "ok": True},
            {"type": "snap"},
            {"type": "mode"},
            {"type": "footer", "duration_s": duration, "orders": 3, "orders_ok": 2, "snaps": 1},
        ]
        return self.write(rows, name)

    def write(self, rows, name="session.jsonl"):
        path = self.directory / name
        path.write_text("\n".join(json.dumps(row) for row in rows) + "\n", encoding="utf-8")
        return path

    def test_metrics_use_catalog_and_count_all_order_rows(self):
        path = self.recording()
        original = path.read_bytes()
        result = demo.summarize(path)
        self.assertTrue(result.valid, result.reasons)
        self.assertEqual((result.map_name, result.duration, result.orders, result.accepted), ("Kit", 60, 3, 2))
        self.assertEqual((result.mapped, result.snapshots, result.lab), (1, 1, False))
        self.assertIn("PMC mapped=1/3 (33.3%)", result.render(path.name))
        self.assertEqual(path.read_bytes(), original)

    def test_either_cheat_tags_valid_recording_as_lab(self):
        for flag in ("immortal", "infinite_ammo"):
            with self.subTest(flag=flag):
                result = demo.summarize(self.recording(flags={flag: True}))
                self.assertTrue(result.lab)
                self.assertTrue(result.valid)

    def test_invalid_duration(self):
        for duration in (59.9, float("inf"), True, "60"):
            with self.subTest(duration=duration):
                result = demo.summarize(self.recording(duration=duration))
                self.assertFalse(result.valid)
                self.assertIn("duration", " ".join(result.reasons))

    def test_rejected_only(self):
        path = self.write([
            {"type": "header", "v": 1, "map": "Kit"},
            {"type": "order", "pmc_order": "Heal", "ok": False},
            {"type": "footer", "duration_s": 70, "orders": 1, "orders_ok": 0, "snaps": 0},
        ])
        self.assertEqual(demo.summarize(path).reasons, ["no accepted orders"])

    def test_bad_utf8_or_json_keeps_other_files_readable(self):
        bad = self.directory / "a-bad.jsonl"
        bad.write_bytes(b'{"type":"header","v":1,"map":"Broken"}\n\xff\n{\n')
        self.recording("b-good.jsonl")
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(demo.main([str(self.directory)]), 1)
        message = output.getvalue()
        self.assertIn("line 2: invalid UTF-8 JSON", message)
        self.assertIn("missing footer", message)
        self.assertLess(message.index("a-bad"), message.index("b-good"))
        self.assertIn("valid=yes", message)

    def test_extra_record_and_incorrect_footer_counter(self):
        path = self.recording()
        rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        rows[-1]["orders"] = 99
        rows.append({"type": "snap"})
        result = demo.summarize(self.write(rows))
        self.assertIn("line 8: data after footer", result.reasons)
        self.assertIn("footer orders does not match 3 observed", result.reasons)

    def test_no_match_is_normal_and_ignores_temp_files(self):
        (self.directory / "unfinished.jsonl.tmp").write_text("unfinished", encoding="utf-8")
        output = StringIO()
        with redirect_stdout(output):
            self.assertEqual(demo.main([str(self.directory)]), 0)
        self.assertIn("No .jsonl recordings", output.getvalue())


if __name__ == "__main__":
    unittest.main()
