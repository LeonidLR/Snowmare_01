import contextlib
import copy
import csv
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apply_measured_heights as heights


def config():
    values = {"step": (30, 0, 35), "low_vault": (100, 25, 105),
              "low_mantle": (120, 75, 130), "high_mantle": (200, 130, 240),
              "wall_no_traverse": (300, 240, None)}
    return {"schema": "codex.obstacle_heights/1", "custom": {"keep": True},
            "classes": {k: {"nominal_cm": n, "tolerance_cm": 5, "min_cm": lo,
                            "max_cm": hi, "measured": False} for k, (n, lo, hi) in values.items()}}


def row(name, dz, peak):
    return {"asset_path": heights.CAV_PREFIX + f"Climbing/Test/{name}.{name}",
            "dz_up_cm": str(dz), "max_root_rise_cm": str(peak),
            "status": "ok", "root_motion_enabled": "True"}


def measurements():
    return [row("LowVault", 0, 100), row("Low_Climb", 120, 150), row("HighClimb", 200, 220)]


class ApplyMeasuredHeightsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="codex_measured_heights_")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.csv_path = self.root / "measure.csv"
        self.input = self.root / "heights.json"
        self.output = self.root / "approved.json"
        self.input.write_text(json.dumps(config()), encoding="utf-8")
        self.write_csv(measurements())

    def write_csv(self, rows):
        with self.csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(measurements()[0]))
            writer.writeheader()
            writer.writerows(rows)

    def cli(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            result = heights.main(["--csv", str(self.csv_path), "--heights", str(self.input), *args])
        return result, out.getvalue()

    def test_auto_uses_over_peak_and_climb_net_without_mutating_input(self):
        doc = config()
        before = copy.deepcopy(doc)
        p = heights.propose_heights(doc, measurements())
        by_class = {x["class"]: x for x in p}
        self.assertEqual(by_class["low_vault"]["nominal_cm"], 100)
        self.assertEqual(by_class["low_mantle"]["nominal_cm"], 120)
        self.assertEqual(by_class["high_mantle"]["nominal_cm"], 200)
        self.assertEqual(by_class["low_vault"]["measurements"][0]["metric"], "max_root_rise_cm")
        self.assertEqual(doc, before)

    def test_median_rounds_positive_half_up_to_five(self):
        rows = measurements() + [row("RunLowVault", 0, 105)]
        p = next(p for p in heights.propose_heights(config(), rows) if p["class"] == "low_vault")
        self.assertEqual(p["nominal_cm"], 105)
        self.assertEqual(p["tolerance_cm"], 5)
        self.assertEqual(p["spread_cm"], 5)
        self.assertFalse(p["blocking"])

    def test_spread_conflict_is_not_hidden_by_larger_proposed_tolerance(self):
        rows = measurements()
        rows[2] = row("HighClimb", 180, 210)
        rows.append(row("RunHighClimb", 210, 220))
        p = next(p for p in heights.propose_heights(config(), rows) if p["class"] == "high_mantle")
        self.assertEqual(p["nominal_cm"], 195)
        self.assertEqual(p["tolerance_cm"], 15)
        self.assertIn("SPREAD_EXCEEDS_TOLERANCE", p["flags"])
        self.assertTrue(p["blocking"])

    def test_missing_measured_class_blocks_apply(self):
        p = heights.propose_heights(config(), measurements()[:2])
        high = next(x for x in p if x["class"] == "high_mantle")
        self.assertIn("NO_CLIP_MEASUREMENT", high["flags"])
        with self.assertRaisesRegex(ValueError, "no JSON written"):
            heights.apply_proposals(config(), p, self.csv_path)

    def test_intentional_no_clip_classes_are_flagged_and_kept_unmeasured(self):
        doc = config()
        p = heights.propose_heights(doc, measurements())
        result = heights.apply_proposals(doc, p, self.csv_path)
        for name in ("step", "wall_no_traverse"):
            self.assertIn("NO_CLIP", next(x for x in p if x["class"] == name)["flags"])
            self.assertEqual(result["classes"][name], doc["classes"][name])
            self.assertFalse(result["classes"][name]["measured"])

    def test_provenance_and_unknown_keys_are_preserved(self):
        doc = config()
        result = heights.apply_proposals(doc, heights.propose_heights(doc, measurements()), self.csv_path)
        self.assertEqual(result["custom"], doc["custom"])
        entry = result["classes"]["low_mantle"]
        self.assertTrue(entry["measured"])
        self.assertEqual(entry["source_clips"], [measurements()[1]["asset_path"]])
        self.assertEqual(entry["source_measurements"][0]["metric"], "dz_up_cm")
        self.assertEqual(entry["measurement_basis"], "source_root_motion_proxy")
        self.assertEqual(entry["min_cm"], 75)
        self.assertFalse(doc["classes"]["low_mantle"]["measured"])

    def test_duplicate_clip_does_not_get_double_weight_in_median(self):
        p = heights.propose_heights(config(), measurements() + [measurements()[2]])
        high = next(x for x in p if x["class"] == "high_mantle")
        self.assertTrue(high["blocking"])
        self.assertIn("DUPLICATE_CLIP:HighClimb", high["flags"])

    def test_bad_measurements_are_flagged(self):
        for changes in ({"status": "error"}, {"root_motion_enabled": "False"},
                        {"dz_up_cm": "nan", "max_root_rise_cm": "nan"},
                        {"dz_up_cm": "0", "max_root_rise_cm": "0"},
                        {"dz_up_cm": "inf", "max_root_rise_cm": "inf"}):
            with self.subTest(changes=changes):
                rows = measurements()
                rows[2].update(changes)
                high = next(x for x in heights.propose_heights(config(), rows) if x["class"] == "high_mantle")
                self.assertTrue(high["blocking"])
                self.assertTrue(any(f.startswith("INVALID_CLIP:") for f in high["flags"]))

    def test_fallback_measurement_status_keeps_its_provenance(self):
        rows = measurements()
        rows[2]["status"] = "fallback"
        result = heights.apply_proposals(config(), heights.propose_heights(config(), rows), self.csv_path)
        self.assertEqual(result["classes"]["high_mantle"]["source_measurements"][0]["status"], "fallback")

    def test_explicit_metric_and_auto_peak_fallback(self):
        rows = measurements()
        rows[2] = row("HighClimb", 0, 200)
        p = next(x for x in heights.propose_heights(config(), rows) if x["class"] == "high_mantle")
        self.assertEqual(p["nominal_cm"], 200)
        p = next(x for x in heights.propose_heights(config(), measurements(), "max-rise") if x["class"] == "high_mantle")
        self.assertEqual(p["nominal_cm"], 220)
        p = next(x for x in heights.propose_heights(config(), measurements(), "dz") if x["class"] == "low_vault")
        self.assertTrue(p["blocking"])

    def test_values_outside_class_range_cannot_round_back_into_it(self):
        rows = measurements()
        rows[2] = row("HighClimb", 241, 250)
        high = next(x for x in heights.propose_heights(config(), rows) if x["class"] == "high_mantle")
        self.assertEqual(high["nominal_cm"], 240)
        self.assertIn("OUTSIDE_CLASS_RANGE", high["flags"])

    def test_foreign_first_person_clip_is_not_used(self):
        rows = measurements()
        rows[2]["asset_path"] = "/Game/CaV_Anims/Animations/FirstPerson_Anims/HighClimb.HighClimb"
        high = next(x for x in heights.propose_heights(config(), rows) if x["class"] == "high_mantle")
        self.assertIn("NO_CLIP_MEASUREMENT", high["flags"])

    def test_unknown_height_class_has_no_mapping_conflict(self):
        doc = config()
        doc["classes"]["new_class"] = {"nominal_cm": 150, "tolerance_cm": 5}
        p = next(x for x in heights.propose_heights(doc, measurements()) if x["class"] == "new_class")
        self.assertIn("NO_MAPPING", p["flags"])
        self.assertTrue(p["blocking"])

    def test_dry_run_prints_table_and_never_writes_even_with_output(self):
        before = self.input.read_bytes()
        code, text = self.cli("--output", str(self.output))
        self.assertEqual(code, 0)
        self.assertIn("before cm/tol | proposed cm/tol", text)
        self.assertIn("NO_CLIP", text)
        self.assertIn("DRY RUN: no files written", text)
        self.assertFalse(self.output.exists())
        self.assertEqual(before, self.input.read_bytes())

    def test_write_requires_explicit_destination(self):
        with self.assertRaises(SystemExit) as caught:
            self.cli("--write")
        self.assertEqual(caught.exception.code, 2)
        self.assertFalse(self.output.exists())

    def test_write_changes_only_the_explicit_json_path(self):
        before_csv, before_input = self.csv_path.read_bytes(), self.input.read_bytes()
        code, text = self.cli("--write", "--output", str(self.output))
        self.assertEqual(code, 0, text)
        self.assertIn("WROTE", text)
        self.assertEqual(before_csv, self.csv_path.read_bytes())
        self.assertEqual(before_input, self.input.read_bytes())
        self.assertEqual(set(p.name for p in self.root.iterdir()), {"measure.csv", "heights.json", "approved.json"})
        output = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertTrue(output["classes"]["high_mantle"]["measured"])

    def test_conflicts_do_not_overwrite_existing_destination(self):
        self.output.write_text("preserve", encoding="utf-8")
        self.write_csv(measurements()[:2])
        code, text = self.cli("--write", "--output", str(self.output))
        self.assertEqual(code, 2)
        self.assertIn("NO_CLIP_MEASUREMENT", text)
        self.assertEqual(self.output.read_text(encoding="utf-8"), "preserve")

    def test_csv_headers_and_bom(self):
        self.csv_path.write_text("asset_path,status\nfoo,ok\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "missing columns"):
            heights.read_measurements(self.csv_path)
        self.write_csv(measurements())
        self.csv_path.write_text(self.csv_path.read_text(encoding="utf-8"), encoding="utf-8-sig")
        self.input.write_text(json.dumps(config()), encoding="utf-8-sig")
        code, text = self.cli()
        self.assertEqual(code, 0, text)

    def test_invalid_config_has_no_output(self):
        doc = config()
        doc["classes"]["low_vault"]["tolerance_cm"] = -1
        self.input.write_text(json.dumps(doc), encoding="utf-8")
        code, text = self.cli("--write", "--output", str(self.output))
        self.assertEqual(code, 2)
        self.assertIn("Invalid nominal/tolerance", text)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
