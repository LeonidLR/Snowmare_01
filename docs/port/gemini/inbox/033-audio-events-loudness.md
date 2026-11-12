# 033 - Audio: events list + loudness pass (Claude, 2026-11-12) - branch gemini/033-audio-events from main; report to claude

Read docs/design/audio/SOUND_BIBLE.md and docs/design/audio/AUDIO_PROCESS.md first.
1. **docs/design/audio/EVENTS.md** in the AUDIO_PROCESS table format: one row per audio event that exists or is planned - every bark
   you voiced (with its source file:line), the 8 music states, and the SFX the game needs (weapons per type, footsteps per surface,
   reload, cover / vault / ladder, mine whistle, explosions, crates / doors / generator, wind / blizzard layers, hounds / frostbitten /
   horde, UI). Fill bus, 3D + attenuation preset, priority, concurrency, cooldown, ducking, visual cue (BLOCKING if a critical event
   has none - list them separately). No guessing of code: file:line only where it exists.
2. **Loudness pass (Python):** Scripts/Tools/audio_loudness.py - measure integrated LUFS, true peak, duration, silence at the ends for
   every WAV in Saved/Orchestrator/audio_v1/ (ffmpeg ebur128 or pyloudnorm - say which; no new installs without asking the designer);
   report table vs the bible targets (VO -18, music -23, peak -1 dBTP); `--normalize --out <dir>` writes normalised copies (never
   overwrite the originals); trim leading / trailing silence for VO. Tests on generated tone fixtures.
3. Training Ground audio panel: load audio_v1/catalog.json, play / approve / reject + note per file (approvals JSON next to the
   catalog), filter by speaker / cue. Same security rules as before.
