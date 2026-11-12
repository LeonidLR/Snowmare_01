"""Unit & integration tests for Scripts/Tools/audio_loudness.py using generated WAV tone fixtures."""
from __future__ import annotations

import math
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_loudness  # noqa: E402


def _write_tone_wav(
    path: Path,
    duration_sec: float = 1.5,
    freq_hz: float = 440.0,
    amplitude: float = 0.05,
    leading_silence_sec: float = 0.0,
    trailing_silence_sec: float = 0.0,
    sample_rate: int = 24000,
    channels: int = 1,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lead_n = int(round(leading_silence_sec * sample_rate))
    tone_n = int(round(duration_sec * sample_rate))
    trail_n = int(round(trailing_silence_sec * sample_rate))
    max_int16 = 32767.0
    frames = bytearray()
    for _ in range(lead_n * channels):
        frames.extend(struct.pack("<h", 0))
    for i in range(tone_n):
        sample = int(round(max(-1.0, min(1.0, amplitude * math.sin(2.0 * math.pi * freq_hz * (i / sample_rate)))) * max_int16))
        packed = struct.pack("<h", sample)
        for _ in range(channels):
            frames.extend(packed)
    for _ in range(trail_n * channels):
        frames.extend(struct.pack("<h", 0))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(bytes(frames))


class AudioLoudnessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.in_dir = self.root / "audio_v1"
        self.vo_wav = self.in_dir / "voice" / "cmd_test.wav"
        self.mus_wav = self.in_dir / "music" / "explore_test_v1.wav"
        # Quiet VO fixture (-29 LUFS approx) with long 0.40s silence at both ends.
        _write_tone_wav(
            self.vo_wav,
            duration_sec=1.2,
            freq_hz=440.0,
            amplitude=0.05,
            leading_silence_sec=0.40,
            trailing_silence_sec=0.40,
            sample_rate=24000,
            channels=1,
        )
        # Hot stereo music fixture (~ -9 LUFS, > -1 dBTP peak).
        _write_tone_wav(
            self.mus_wav,
            duration_sec=1.5,
            freq_hz=220.0,
            amplitude=0.95,
            leading_silence_sec=0.0,
            trailing_silence_sec=0.0,
            sample_rate=48000,
            channels=2,
        )

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_measure_and_markdown_table_detects_lufs_peaks_and_silence(self) -> None:
        res = audio_loudness.run_loudness_pass(in_dir=self.in_dir, normalize=False)
        self.assertEqual(res["method"], "ffmpeg_ebur128")
        self.assertEqual(res["count"], 2)

        by_id = {item["id"]: item for item in res["measured"]}
        vo = by_id["cmd_test"]
        self.assertEqual(vo["kind"], "voice")
        self.assertAlmostEqual(vo["duration_sec"], 2.0, places=2)
        self.assertGreater(vo["leading_silence_sec"], 0.35)
        self.assertGreater(vo["trailing_silence_sec"], 0.35)
        self.assertIsNotNone(vo["integrated_lufs"])
        self.assertLess(vo["integrated_lufs"], -20.0)
        self.assertIn("lufs_off_target", vo["status"])
        self.assertIn("trim_edge_silence", vo["status"])

        mus = by_id["explore_test_v1"]
        self.assertEqual(mus["kind"], "music")
        self.assertIsNotNone(mus["true_peak_dbtp"])
        self.assertGreater(mus["true_peak_dbtp"], -1.0)
        self.assertIn("peak_over_-1dBTP", mus["status"])

        md = res["markdown"]
        self.assertIn("`cmd_test`", md)
        self.assertIn("`explore_test_v1`", md)
        self.assertIn("Voice (VO)", md)
        self.assertIn("Music", md)

    def test_normalize_writes_separate_copies_hits_targets_and_trims_vo(self) -> None:
        orig_vo_bytes = self.vo_wav.read_bytes()
        orig_mus_bytes = self.mus_wav.read_bytes()
        out_dir = self.root / "audio_v1_norm"

        # Refusing in-place overwrite must raise ValueError.
        with self.assertRaises(ValueError):
            audio_loudness.run_loudness_pass(in_dir=self.in_dir, out_dir=self.in_dir, normalize=True)

        res = audio_loudness.run_loudness_pass(in_dir=self.in_dir, out_dir=out_dir, normalize=True)
        self.assertEqual(len(res["normalized"]), 2)
        # Originals must stay byte-identical.
        self.assertEqual(self.vo_wav.read_bytes(), orig_vo_bytes)
        self.assertEqual(self.mus_wav.read_bytes(), orig_mus_bytes)

        norm_by_id = {item["id"]: item for item in res["normalized"]}
        vo_norm = norm_by_id["cmd_test"]
        self.assertTrue(vo_norm["trimmed"])
        self.assertEqual(vo_norm["sample_rate"], 48000)
        self.assertEqual(vo_norm["bit_depth"], 16)
        self.assertLess(vo_norm["duration_sec"], 1.35)
        self.assertLess(vo_norm["leading_silence_sec"], 0.08)
        self.assertLess(vo_norm["trailing_silence_sec"], 0.08)
        self.assertAlmostEqual(vo_norm["integrated_lufs"], -18.0, delta=1.0)
        self.assertLessEqual(vo_norm["true_peak_dbtp"], -0.9)

        mus_norm = norm_by_id["explore_test_v1"]
        self.assertEqual(mus_norm["sample_rate"], 48000)
        self.assertEqual(mus_norm["bit_depth"], 24)
        self.assertAlmostEqual(mus_norm["duration_sec"], 1.5, delta=0.05)
        self.assertAlmostEqual(mus_norm["integrated_lufs"], -23.0, delta=1.0)
        self.assertLessEqual(mus_norm["true_peak_dbtp"], -0.9)


if __name__ == "__main__":
    unittest.main()
