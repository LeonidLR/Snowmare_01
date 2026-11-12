# Queue 029 Report — Audio Pipeline v1 (Gemini TTS + Lyria + Training Ground Review)

**To:** `claude` (for designer review & Codex audio subsystem handoff)  
**Branch:** `gemini/029-audio-tool`  
**Status:** Ready for designer audition & review (generation stopped after 1 live test).

---

## 1. Current Google API Models, Endpoints, Pricing & Auth (Verified from Official Docs)

All details were checked directly against Google AI Studio official documentation (`https://aistudio.google.com/docs/*.md`):

- **Speech Generation (TTS):** <https://aistudio.google.com/docs/speech-generation.md>
- **Music Generation (Lyria):** <https://aistudio.google.com/docs/music-generation.md>
- **Pricing:** <https://aistudio.google.com/docs/pricing.md>
- **Interactions API Overview:** <https://aistudio.google.com/docs/interactions-overview.md>

### Auth & Endpoint
- **Both** Gemini TTS (`gemini-3.8-flash-tts`) and Lyria 3 (`lyria-3-clip-preview` / `lyria-3.5`) run on the **Interactions REST API** using **`GEMINI_API_KEY` directly** (`x-goog-api-key` header) — **no Vertex AI setup required**.
- **Endpoint:** `POST https://generativelanguage.googleapis.com/v1beta/interactions` with `Api-Revision: 2026-05-20`.

### Models & Pricing (Standard Paid Tier, checked 2026-11)
| Track | Model ID | Output Format | Official Price |
|---|---|---|---|
| TTS (Primary) | `gemini-3.8-flash-tts` | `audio/wav` (24 kHz PCM mono) | `$0.50 / 1M` text input tokens, `$9.00 / 1M` audio output tokens (~25 tok/s ≈ `$0.00225 / 10s`) through 2026-12-31 |
| TTS (Lite) | `gemini-3.8-flash-lite-tts` | `audio/wav` | `$0.25 / 1M` text in, `$6.00 / 1M` audio out |
| Music (Clip/Loop) | `lyria-3-clip-preview` | `audio/mpeg` (MP3 → converted to 44.1 kHz PCM WAV via `ffmpeg`) | **`$0.04 / request`** (30s clip, Paid tier) |
| Music (Full Song) | `lyria-3.5` | `audio/wav` or MP3 | **`$0.08 / song`** (Paid tier) |

---

## 2. Manifests v1 (`Content/Data/Audio/`)

1. **`Content/Data/Audio/voice_lines.json`** — 15 existing HUD barks (5 per character), each tied to its exact `Source/CodexTactics/` origin:
   - `commander` (`voice: "Charon"`): `cmd_contact`, `cmd_ambush`, `cmd_wave_advancing`, `cmd_autonomy_on`, `cmd_generator_starts`
   - `engineer` (`voice: "Puck"`): `eng_generator_restored`, `eng_fortifications_dismantled`, `eng_generator_dry`, `eng_draining_diesel`, `eng_found_canister`
   - `medic_sapper` (`voice: "Kore"`): `med_tripwire`, `med_apc_tank_needs_canister`, `med_reloading`, `med_grenade_out`, `med_in_cover`
2. **`Content/Data/Audio/music_cues.json`** — 3 initial state cues aligned with `docs/design/AUDIO_PIPELINE.md`:
   - `explore_calm` (`state: "explore"`, sparse cold arctic ambient loop)
   - `tension` (`state: "tension"`, stealth / enemy sighted pulse)
   - `combat` (`state: "combat"`, low industrial percussion + sub-bass defense loop)

---

## 3. Generation Tool (`Scripts/Tools/audio_gen.py`)

- Subcommands: `voice` and `music`, with `--id` filter, `--dry-run` (prints plan + estimated cost, `$0` spent), `--overwrite` (default skips already-generated WAVs), `--budget-usd` (default `40.0` over a 7-day rolling window in `Saved/AudioGen/spend.jsonl`).
- Loads `GEMINI_API_KEY` into process memory only (with `HKCU\Environment` fallback on Windows); never prints, logs, or stores the key in metadata/errors.
- Outputs standard RIFF PCM `.wav` files to `Saved/AudioGen/<kind>/<id>.wav` (converting `audio/mpeg` from Lyria via `ffmpeg` when needed) and maintains `Saved/AudioGen/catalog.json` + `Saved/AudioGen/spend.jsonl`.

---

## 4. Live API Test Run (1 Run, Stopped for Designer Listening)

Ran **1** test generation (`--budget-usd 1.0`) — 3 voice lines (1 per character) + 1 music clip:

| ID | Model | Voice / State | Duration | Actual Cost (USD) | Output File |
|---|---|---|---|---|---|
| `cmd_contact` | `gemini-3.8-flash-tts` | `commander` / `Charon` | `1.08s` | `$0.000244` | `Saved/AudioGen/voice/cmd_contact.wav` |
| `eng_generator_restored` | `gemini-3.8-flash-tts` | `engineer` / `Puck` | `4.84s` | `$0.001094` | `Saved/AudioGen/voice/eng_generator_restored.wav` |
| `med_tripwire` | `gemini-3.8-flash-tts` | `medic_sapper` / `Kore` | `4.28s` | `$0.000969` | `Saved/AudioGen/voice/med_tripwire.wav` |
| `explore_calm` | `lyria-3-clip-preview` | `explore` (converted MP3→WAV) | `30.772s` | `$0.040000` | `Saved/AudioGen/music/explore_calm.wav` |

- **Total live spend recorded in `Saved/AudioGen/spend.jsonl`:** **`$0.042307`** (all 4 files verified as valid PCM WAVs via Python `wave`; further generation intentionally stopped until the designer listens).

---

## 5. Training Ground `"Аудио"` Review Panel & Dev-Server API

- **UI:** `Tools/TrainingGround/src/components/AudioReviewPanel.tsx` + tab `"Аудио"` in `Tools/TrainingGround/src/App.tsx`.
  - Lists manifest lines & cues with kind/status filters, shows duration/cost/prompt metadata, provides HTML5 `<audio controls>` playback for generated WAVs, and saves `Approve` / `Reject` / `Pending` + designer notes.
- **API & Security:** `Tools/TrainingGround/server/audioApi.ts` registered in `Tools/TrainingGround/vite.config.ts`:
  - `GET /api/audio/list`
  - `GET /api/audio/file?kind=<voice|music>&id=<bareId>` (supports `Range` seeking; validates `kind` and `AUDIO_ID_RE`, resolves strictly inside `Saved/AudioGen/<kind>/`)
  - `POST /api/audio/review` (same-origin/local check, rejects traversal/non-manifest IDs, atomically writes `Content/Data/Audio/audio_approvals.json`)
- **Shared validation helpers:** `Tools/TrainingGround/src/utils/audioReview.ts`.

---

## 6. Verification Scope (Verified Results Only)

- `python -m unittest Scripts/Tools/tests/test_audio_gen.py -v`: **5/5 PASS**
- `python Scripts/Tools/audio_gen.py voice --dry-run` & `music --dry-run`: **$0 spent**, valid estimates printed
- `Tools/TrainingGround`:
  - `npm run typecheck`: **PASS** (`tsc && tsc -p tsconfig.node.json`)
  - `npm test`: **331/331 PASS** (includes `src/utils/audioReview.test.ts` 3/3 PASS)
  - `node --test "server/audioApi.test.ts"`: **3/3 PASS**
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`: **PASS** (0 boundary breaches, risk 0.1/4)
