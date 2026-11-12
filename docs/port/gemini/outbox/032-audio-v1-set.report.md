# 032 - Audio v1 Set & Implementation Map Report

- **Branch:** `gemini/032-audio-v1` (branched from `gemini/029-audio-tool`)
- **Date:** 2026-11-12
- **Budget Cap:** `$5.00` (`--budget-usd 5.0`)
- **Logged Cumulative Spend (`Saved/AudioGen/spend.jsonl`, 69 entries):** `$0.719220` (`voice`: `$0.039220`, `music`: `$0.680000`)

---

## 1. Male Squad Voices & Sample Delivery

- Replaced female voice `Kore` with male `Orus` for `medic_sapper` in `Content/Data/Audio/voice_lines.json`.
- All 6 speaker slots now use male voices (`gemini-3.8-flash-tts`):
  - `commander` -> `Charon` (11 lines)
  - `engineer` -> `Puck` (16 lines)
  - `medic_sapper` -> `Orus` (13 lines)
  - `pmc_op_1` -> `Fenrir` (3 lines)
  - `pmc_op_2` -> `Alnilam` (3 lines)
  - `pmc_op_3` -> `Iapetus` (3 lines)
- Posted 3 reference WAV samples (`cmd_contact.wav`, `eng_generator_restored.wav`, `med_tripwire.wav`) to chat `#278`.

## 2. Full v1 Generation (`49` Voice WAVs + `16` Music WAVs)

- Updated `Content/Data/Audio/voice_lines.json` (49 lines covering core barks, HUD squad messages with `source` `file:line`, quests, cover/stance/panic/rage, and enemy PMC operators) and `Content/Data/Audio/music_cues.json` (16 cues = 8 states from `AUDIO_PIPELINE.md` §2 x 2 variants `_v1`/`_v2`, `loop: true` on loops, `loop: false` on `victory_sting`/`defeat_sting`, `lyria-3-clip-preview`).
- Hardened `Scripts/Tools/audio_gen.py`:
  - Propagates `cue_group`, `variant`, `loop`, `source`, `category` into `Saved/AudioGen/catalog.json`.
  - Added HTTP 429 retry/backoff (`_parse_retry_after_seconds` + `max_retries=4` in `post_interaction`) and default `6.2s` pacing between live TTS requests (`DEFAULT_TTS_MIN_INTERVAL_SEC`) in `run_generation` so 10-RPM Tier 1 limits do not abort batch runs.
  - Added `test_post_interaction_retries_429_and_paces_requests` in `Scripts/Tools/tests/test_audio_gen.py`.
- Generated and validated all 49/49 voice WAVs and 16/16 music WAVs (`Saved/AudioGen/voice/*.wav`, `Saved/AudioGen/music/*.wav`, `Saved/AudioGen/catalog.json`).
- Copied all 49 voice WAVs, 16 music WAVs, and `catalog.json` to MAIN `Saved/Orchestrator/audio_v1/{voice,music}/`.

## 3. Read-Only C++ Architecture & Implementation Map

- Authored `docs/port/codex/inbox/009-audio-subsystem.md` (draft for Codex), mapping:
  - `UCodexAudioSubsystem` + `AudioRules` (`UWorldSubsystem`/`UGameInstanceSubsystem`, no hot tick).
  - Exact HUD message / bark interceptors (`UGameMessageSubsystem::OnMessagePosted`, `UCodexEventBus::OnDialogueLineDisplayed`, full `file:line` table of all 49 voice lines + PMC concealment filtering & VO arbiter priorities/cooldowns).
  - 8-state music FSM predicates (`Menu`, `Defeat`, `Victory`, `CombatHeavy`, `Combat`, `Tension`, `ExploreCold`, `ExploreCalm`) with exact subsystem sources (`file:line`), A/B cross-fade & pause rules.
  - Sound Class/Submix hierarchy (`SC_Master`, `SC_Music`, `SC_VO`, `SC_SFX`, `SC_UI`, `SM_CodexMaster`, `SA_CodexVoice3D`), `Scripts/Editor/import_audio.py` offline spec, and headless smoke/unit tests.

## 4. Verification

- `python -m unittest Scripts/Tools/tests/test_audio_gen.py -v`: `6` tests passed (`OK`).
- `Tools/TrainingGround`: `npm test` -> `331/331` passed (`0` failed); `npm run typecheck` -> exited `0`.
- Python WAV & catalog check: `voice_manifest=49 missing_wav=[] cat_missing=[]`, `music_manifest=16 missing_wav=[] cat_missing=[]`, `main_voice_wavs=49 main_music_wavs=16`.
