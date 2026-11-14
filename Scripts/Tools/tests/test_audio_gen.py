"""Unit tests for Scripts/Tools/audio_gen.py (no network calls, $0 cost)."""
from __future__ import annotations

import base64
import datetime as dt
import io
import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_gen  # noqa: E402


def _make_pcm_wav_bytes(duration_sec: float = 0.5, sample_rate: int = 8000) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        frames = b"\x00\x00" * int(duration_sec * sample_rate)
        w.writeframes(frames)
    return buf.getvalue()


class AudioGenTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out_dir = self.root / "Saved" / "AudioGen"
        self.ledger = self.out_dir / "spend.jsonl"
        self.catalog = self.out_dir / "catalog.json"

        self.voice_manifest = self.root / "voice_lines.json"
        self.voice_manifest.write_text(
            json.dumps(
                {
                    "version": 1,
                    "model": "gemini-3.8-flash-tts",
                    "lines": [
                        {
                            "id": "cmd_contact",
                            "speaker": "commander",
                            "voice": "Charon",
                            "style": "Urgent callout",
                            "text": "Contact!",
                        },
                        {
                            "id": "eng_restored",
                            "speaker": "engineer",
                            "voice": "Puck",
                            "style": "Relieved",
                            "text": "Generator restored!",
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )

        self.music_manifest = self.root / "music_cues.json"
        self.music_manifest.write_text(
            json.dumps(
                {
                    "version": 1,
                    "default_model": "lyria-3-clip-preview",
                    "cues": [
                        {
                            "id": "explore_calm",
                            "model": "lyria-3-clip-preview",
                            "prompt": "Cold sparse arctic ambient loop.",
                        },
                        {
                            "id": "combat",
                            "model": "lyria-3-clip-preview",
                            "prompt": "Driving low industrial combat loop.",
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_dry_run_makes_no_calls_and_writes_nothing(self) -> None:
        called = []

        def fake_transport(payload: dict, key: str) -> dict:
            called.append((payload, key))
            return {}

        res = audio_gen.run_generation(
            kind="voice",
            manifest_path=self.voice_manifest,
            out_dir=self.out_dir,
            ledger_path=self.ledger,
            catalog_path=self.catalog,
            budget_usd=40.0,
            dry_run=True,
            transport=fake_transport,
            api_key="SECRET_SHOULD_NEVER_PRINT",
        )
        self.assertTrue(res["dry_run"])
        self.assertEqual(len(called), 0)
        self.assertEqual(len(res["planned"]), 2)
        self.assertFalse(self.ledger.exists())
        self.assertFalse(self.catalog.exists())
        self.assertNotIn("SECRET_SHOULD_NEVER_PRINT", json.dumps(res))

    def test_weekly_budget_cap_blocks_old_vs_recent_correctly(self) -> None:
        now = dt.datetime.now(dt.timezone.utc)
        old_iso = (now - dt.timedelta(days=10)).isoformat(timespec="seconds")
        recent_iso = (now - dt.timedelta(days=1)).isoformat(timespec="seconds")
        audio_gen.append_spend_entry(self.ledger, {"when": old_iso, "usd": 35.0})
        audio_gen.append_spend_entry(self.ledger, {"when": recent_iso, "usd": 39.99})

        self.assertAlmostEqual(audio_gen.load_spend_total(self.ledger, window_days=7, now=now), 39.99, places=4)

        with self.assertRaises(RuntimeError) as ctx:
            audio_gen.run_generation(
                kind="music",
                manifest_path=self.music_manifest,
                out_dir=self.out_dir,
                ledger_path=self.ledger,
                catalog_path=self.catalog,
                budget_usd=40.0,
                selected_ids=["explore_calm"],
                api_key="dummy-key",
            )
        self.assertIn("Budget cap exceeded", str(ctx.exception))

    def test_voice_generation_writes_wav_ledger_and_catalog_without_leaking_key(self) -> None:
        wav_bytes = _make_pcm_wav_bytes(duration_sec=0.5)
        fake_b64 = base64.b64encode(wav_bytes).decode("ascii")
        seen_payloads = []

        def fake_transport(payload: dict, key: str) -> dict:
            seen_payloads.append(payload)
            return {
                "outputs": [
                    {
                        "content": [
                            {"type": "audio", "mime_type": "audio/wav", "data": fake_b64},
                        ]
                    }
                ],
                "usage": {"total_input_tokens": 20, "total_output_tokens": 12},
            }

        res = audio_gen.run_generation(
            kind="voice",
            manifest_path=self.voice_manifest,
            out_dir=self.out_dir,
            ledger_path=self.ledger,
            catalog_path=self.catalog,
            budget_usd=40.0,
            selected_ids=["cmd_contact"],
            transport=fake_transport,
            api_key="SUPER_SECRET_API_KEY",
        )
        self.assertEqual(len(seen_payloads), 1)
        self.assertEqual(seen_payloads[0]["model"], "gemini-3.8-flash-tts")
        self.assertEqual(seen_payloads[0]["generation_config"]["speech_config"][0]["voice"], "Charon")
        wav_out = self.out_dir / "voice" / "cmd_contact.wav"
        self.assertTrue(wav_out.exists())
        self.assertEqual(res["generated"][0]["duration_sec"], 0.5)

        catalog = json.loads(self.catalog.read_text(encoding="utf-8"))
        self.assertIn("voice/cmd_contact", catalog["items"])
        self.assertEqual(catalog["items"]["voice/cmd_contact"]["voice"], "Charon")
        self.assertEqual(catalog["items"]["voice/cmd_contact"]["text"], "Contact!")
        self.assertNotIn("SUPER_SECRET_API_KEY", self.catalog.read_text(encoding="utf-8"))
        self.assertNotIn("SUPER_SECRET_API_KEY", self.ledger.read_text(encoding="utf-8"))

        # Second run without --overwrite skips existing file cleanly.
        res2 = audio_gen.run_generation(
            kind="voice",
            manifest_path=self.voice_manifest,
            out_dir=self.out_dir,
            ledger_path=self.ledger,
            catalog_path=self.catalog,
            budget_usd=40.0,
            selected_ids=["cmd_contact"],
            transport=fake_transport,
            api_key="SUPER_SECRET_API_KEY",
        )
        self.assertEqual(res2["skipped_existing"], ["cmd_contact"])
        self.assertEqual(len(seen_payloads), 1)

    def test_music_mp3_conversion_produces_valid_wav(self) -> None:
        fake_mp3 = b"ID3FAKE_MP3_STREAM"
        fake_b64 = base64.b64encode(fake_mp3).decode("ascii")
        wav_bytes = _make_pcm_wav_bytes(duration_sec=1.0)

        def fake_transport(payload: dict, key: str) -> dict:
            return {
                "steps": [
                    {
                        "type": "model_output",
                        "content": [
                            {"type": "audio", "mime_type": "audio/mpeg", "data": fake_b64},
                        ],
                    }
                ]
            }

        def fake_converter(src: Path, dst: Path) -> None:
            self.assertEqual(src.read_bytes(), fake_mp3)
            dst.write_bytes(wav_bytes)

        res = audio_gen.run_generation(
            kind="music",
            manifest_path=self.music_manifest,
            out_dir=self.out_dir,
            ledger_path=self.ledger,
            catalog_path=self.catalog,
            budget_usd=40.0,
            selected_ids=["explore_calm"],
            transport=fake_transport,
            converter=fake_converter,
            api_key="dummy-key",
        )
        self.assertEqual(len(res["generated"]), 1)
        out_wav = self.out_dir / "music" / "explore_calm.wav"
        self.assertTrue(out_wav.exists())
        self.assertEqual(res["generated"][0]["duration_sec"], 1.0)
        self.assertAlmostEqual(res["spent_this_run_usd"], 0.04, places=4)

    def test_unknown_id_raises_value_error(self) -> None:
        with self.assertRaises(ValueError):
            audio_gen.run_generation(
                kind="voice",
                manifest_path=self.voice_manifest,
                out_dir=self.out_dir,
                ledger_path=self.ledger,
                catalog_path=self.catalog,
                budget_usd=40.0,
                selected_ids=["does_not_exist"],
                dry_run=True,
            )


if __name__ == "__main__":
    unittest.main()
