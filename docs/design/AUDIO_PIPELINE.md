# Audio pipeline v1 - music, voices, first sounds (designer 2026-11-12, start week of 2026-11-16)

Why: the designer cannot judge the game without sound. Source: Google Cloud bonus credit (~$300) - **Lyria** (music) and **Gemini TTS**
(voices). Lyria makes music, not sound effects: SFX need a separate source (see 4).

## 1. Generation tool (Gemini - Python)
`Scripts/Tools/audio_gen.py` (+ tests): `music` (Lyria) and `voice` (Gemini TTS) commands; prompts come from manifests
`Content/Data/Audio/music_cues.json` and `Content/Data/Audio/voice_lines.json`; output WAV into `Saved/AudioGen/<kind>/<id>.wav` + a
manifest entry (prompt, model, seed, duration, cost). Credentials from the designer's user environment only (never printed / logged);
a spend ledger with a hard cap like `api_task.py` (default 40 $/week of the 300 $), dry-run mode. Model ids / prices are checked
against Google's docs at implementation time (do not hard-code from memory). An editor-Python importer puts approved WAVs into
`/Game/Audio/{Music,VO,SFX}` (new folders only, editor closed).

## 2. Music (Lyria) - first cues
explore_calm, explore_cold (blizzard), tension (enemy sighted / stealth), combat, combat_heavy (horde), victory_sting, defeat_sting,
menu. Several variants each; the designer listens on a Training Ground page (play, approve / reject, notes) before import.

## 3. Voices (Gemini TTS) - first lines
Today the HUD already shows the barks as text ("Behind cover!", "Suppressive fire!", "Hugging the wall, holding the corner!", ...).
v1 = voice these existing barks per speaker: Commander, Engineer, Medic-Sapper (our three), PMC operators (2-3 voices), the radio /
HQ later. One fixed voice per character, a short style prompt each (tired, cold, calm under fire...). Text source: the existing bark
tables (Codex lists the bark ids + texts into voice_lines.json).

## 4. Sound effects (not Lyria)
Gunshots (M16, shotgun, pistol, RPG-7), footsteps on snow / concrete, wind / blizzard loop, impacts, explosions, door / crate, UI
clicks. Options to decide: free / CC0 libraries, the designer's own packs / Fab, or another generator. Until then: placeholder SFX
from the engine content if available.

## 5. Game integration (Codex - C++)
`UCodexAudioSubsystem`: music state machine (explore / tension / combat from the sight / pressure / stealth / horde state, cross-fade,
min time per state), barks hooked to the existing bark / HUD message events (per-speaker cooldowns, no overlap, priority, 3D
attenuation for barks, 2D for music), mixer (Sound Classes: Music / VO / SFX / UI) + volume keys in data; smokes: state changes ->
the right cue; barks play once per event within cooldowns.

## Open questions for the designer (defaults in brackets)
1. Voice language: [English for v1 - matches the HUD text; Russian later as a second set].
2. Music style references: [sparse cold ambient for explore, low percussion + drones for combat; no epic orchestra].
3. Weekly spend cap: [40 $ / week of the 300 $].
4. SFX source: [free CC0 libraries first].
