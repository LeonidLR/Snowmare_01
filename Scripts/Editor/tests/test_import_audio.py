"""Unit tests for import_audio.py dry-run planner (Task 045)."""

import json
import os
import shutil
import sys
import tempfile
import unittest

EDITOR_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROJECT_DIR = os.path.abspath(os.path.join(EDITOR_DIR, "..", ".."))

sys.path.insert(0, EDITOR_DIR)
import import_audio  # noqa: E402


class AudioImporterDryRunTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.music_manifest = os.path.join(PROJECT_DIR, "Content", "Data", "Audio", "music_cues.json")
        cls.voice_manifest = os.path.join(PROJECT_DIR, "Content", "Data", "Audio", "voice_lines.json")
        cls.assertTrue(os.path.isfile(cls.music_manifest), "Missing music manifest fixture")
        cls.assertTrue(os.path.isfile(cls.voice_manifest), "Missing voice manifest fixture")

    def setUp(self):
        self.temp_dir = tempfile.mkdtemp(prefix="audio_import_test_")

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_music_cues_planning(self):
        """Verifies that all 22 music cues plan destinations strictly under /Game/Audio/Music/**."""
        plan = import_audio.build_audio_import_plan(
            audio_dir=self.temp_dir,
            music_manifest_path=self.music_manifest,
            voice_manifest_path=self.voice_manifest,
        )
        music_items = [i for i in plan if i.kind == "music"]
        self.assertEqual(len(music_items), 22)

        for item in music_items:
            self.assertTrue(
                item.destination_path.startswith("/Game/Audio/Music/"),
                f"Music cue destination '{item.destination_path}' must start with '/Game/Audio/Music/'",
            )
            self.assertTrue(item.asset_name.startswith("SW_"))
            self.assertEqual(item.asset_class, "SoundWave")
            self.assertIn("cue_group", item.metadata)
            self.assertIn("loop", item.metadata)

    def test_voice_lines_planning(self):
        """Verifies that all 49 voice lines plan destinations strictly under /Game/Audio/Voice/**."""
        plan = import_audio.build_audio_import_plan(
            audio_dir=self.temp_dir,
            music_manifest_path=self.music_manifest,
            voice_manifest_path=self.voice_manifest,
        )
        voice_items = [i for i in plan if i.kind == "voice"]
        self.assertEqual(len(voice_items), 49)

        expected_speakers = {"commander", "engineer", "medic_sapper", "pmc_op_1", "pmc_op_2", "pmc_op_3"}
        found_speakers = set()

        for item in voice_items:
            self.assertTrue(
                item.destination_path.startswith("/Game/Audio/Voice/"),
                f"Voice line destination '{item.destination_path}' must start with '/Game/Audio/Voice/'",
            )
            speaker = item.metadata.get("speaker")
            found_speakers.add(speaker)
            self.assertTrue(item.destination_path.endswith(f"/{speaker}"))
            self.assertTrue(item.asset_name.startswith("SW_"))
            self.assertEqual(item.asset_class, "SoundWave")

        self.assertEqual(found_speakers, expected_speakers)

    def test_destination_guard_rejects_foreign_paths(self):
        """Security guard: destinations outside /Game/Audio/** MUST trigger ValueError."""
        # Valid cases
        import_audio.validate_destination_path("/Game/Audio")
        import_audio.validate_destination_path("/Game/Audio/Music")
        import_audio.validate_destination_path("/Game/Audio/Voice/commander")
        import_audio.validate_destination_path("/Game/Audio/SFX/weapons")

        # Invalid cases
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Game/Characters/Audio")
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Game/Maps/Audio")
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Engine/Audio")
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Game/UI/Audio")
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Game/Audio/../Maps/Audio")
        with self.assertRaises(ValueError):
            import_audio.validate_destination_path("/Game/Audio/Music/..")

    def test_plan_validation_clean(self):
        """Verifies that real manifests produce zero duplicate packages and zero naming errors."""
        plan = import_audio.build_audio_import_plan(
            audio_dir=self.temp_dir,
            music_manifest_path=self.music_manifest,
            voice_manifest_path=self.voice_manifest,
        )
        errors = import_audio.validate_plan(plan)
        self.assertEqual(errors, [])

    def test_fixture_file_discovery_and_dryrun_json(self):
        """Verifies that matching audio files in audio-dir are discovered and status set to FOUND."""
        # Create dummy audio files
        dummy_music_dir = os.path.join(self.temp_dir, "music", "explore_calm")
        os.makedirs(dummy_music_dir, exist_ok=True)
        dummy_music_file = os.path.join(dummy_music_dir, "explore_calm_v1.wav")
        with open(dummy_music_file, "wb") as f:
            f.write(b"RIFFdummyWAVE")

        dummy_voice_dir = os.path.join(self.temp_dir, "voice", "commander")
        os.makedirs(dummy_voice_dir, exist_ok=True)
        dummy_voice_file = os.path.join(dummy_voice_dir, "cmd_contact.ogg")
        with open(dummy_voice_file, "wb") as f:
            f.write(b"OggSdummy")

        plan = import_audio.build_audio_import_plan(
            audio_dir=self.temp_dir,
            music_manifest_path=self.music_manifest,
            voice_manifest_path=self.voice_manifest,
        )

        music_found = [i for i in plan if i.id == "explore_calm_v1"][0]
        self.assertEqual(music_found.status, "FOUND")
        self.assertEqual(os.path.normpath(music_found.source_file), os.path.normpath(dummy_music_file))

        voice_found = [i for i in plan if i.id == "cmd_contact"][0]
        self.assertEqual(voice_found.status, "FOUND")
        self.assertEqual(os.path.normpath(voice_found.source_file), os.path.normpath(dummy_voice_file))

        # Check summary serialization
        out_json = os.path.join(self.temp_dir, "test_plan.json")
        ret = import_audio.main(
            [
                "--dry-run",
                "--audio-dir",
                self.temp_dir,
                "--music-manifest",
                self.music_manifest,
                "--voice-manifest",
                self.voice_manifest,
                "--output-json",
                out_json,
            ]
        )
        self.assertEqual(ret, 0)
        self.assertTrue(os.path.isfile(out_json))

        with open(out_json, "r", encoding="utf-8") as f:
            summary = json.load(f)

        self.assertEqual(summary["counts"]["total_items"], 71)
        self.assertEqual(summary["counts"]["music_cues"], 22)
        self.assertEqual(summary["counts"]["voice_lines"], 49)
        self.assertEqual(summary["counts"]["found_files"], 2)
        self.assertEqual(summary["counts"]["missing_files"], 69)

    def test_live_import_strictly_blocked(self):
        """Verifies that passing --live raises an error and returns exit code 1."""
        ret = import_audio.main(["--live"])
        self.assertEqual(ret, 1)


if __name__ == "__main__":
    unittest.main()
