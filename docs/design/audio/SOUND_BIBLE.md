# Sound Bible - Operation: Cold Silence (v0.1, 2026-11-12, owner: Claude as audio director; the designer approves by ear)

Structure adapted from the sound-bible template of Claude-Code-Game-Studios (Copyright (c) 2026 Donchitos, MIT License).
Fields marked TBD are filled with the designer on Monday 2026-11-16.

## 1. Audio vision
- **Sonic identity:** cold, sparse, close. Wind and snow are always there; people speak little and quietly; when a shot comes it is
  sudden and dry, and the silence after it is part of the sound.
- **Pillars:** (1) *The cold is a character* - wind / blizzard intensity follows the weather system. (2) *Silence is information* -
  quiet means safe-ish, a missing sound (wind drop, birds gone) can mean danger. (3) *Every important sound has a picture* - nothing
  the player must know is audio-only (see 7).
- **References:** TBD with the designer (what to take / what to avoid).

## 2. Music direction
- Style (designer 2026-11-12: "darker and more tense"): dark cold ambient - low drones, metal creaks, wind, rare dissonant hits, no melody or bright tones - for exploration; heavy, oppressive low percussion + pulse + industrial noise for combat, no heroics; no
  epic orchestra.
- Adaptive states (from game state, cross-fades on the Quartz bar grid, min 8 s per state):

| Game state | Music behaviour | Transition |
|---|---|---|
| Exploration (calm) | explore_calm / explore_cold (blizzard) layers | slow 4-6 s fade |
| Stealth / tension (enemy seen, not detected) | tension layer added on top, percussion off | 2 s fade in, on the next bar |
| Combat | combat stems; combat_heavy for the horde | hard cut on the next beat + sting |
| Victory / defeat | victory_sting / defeat_sting, then silence | sting over a fade-out |
| Menu | menu theme | - |
- Rules: loops seamless; silence allowed for 20-60 s after combat; never two themes at full level; dialogue ducks music.

## 3. Sound effects
- Palette: player actions (shots, reloads, footsteps by surface, cover / vault / ladder foley), enemy actions (PMC shots and barks,
  frostbitten groans, hound breathing / barks, horde mass), UI (clicks, order confirm, pause), environment (wind layers, creaks,
  metal, fire), feedback (hit, critical, mine warning whistle).
- Priority (high -> low): 1 player damage / critical warnings (mine whistle, low HP, detection), 2 player actions, 3 enemy actions,
  4 UI, 5 environment / ambient.
- Rules: 3-5 variations + pitch / volume randomisation for anything repeated; 3D attenuation for all world sounds; dialogue ducking;
  response time < 50 ms for player actions.

## 4. Mixing (Sound Classes / Submixes)
| Bus | Content | Target (integrated) |
|---|---|---|
| Master | all, limiter on | -16 LUFS short-term peak -1 dBTP |
| Music | adaptive music | about -23 LUFS |
| SFX | weapons, foley, impacts | -18 to -20 LUFS |
| Dialogue / VO | barks, radio | -18 LUFS, always intelligible |
| UI | interface | -24 LUFS |
| Ambient | wind, snow, room tones | -26 LUFS |
Rules: dialogue > SFX > music; explosions duck everything briefly; per-bus player sliders; check on speakers and headphones.
Claude measures levels with a script (LUFS / true peak) and normalises generated files before import.

## 5. Technical specifications
| Type | Format | Sample rate | Bit depth | Notes |
|---|---|---|---|---|
| Music | WAV in, streamed | 48 kHz | 24 | stereo, loop points |
| SFX | WAV | 48 kHz | 16/24 | mono for 3D |
| Ambient | WAV | 48 kHz | 24 | stereo beds + mono spots |
| Dialogue | WAV | 48 kHz | 16 | mono |
Naming: `[category]_[subcategory]_[name]_[variation]` (e.g. `vo_cmd_contact_01`, `sfx_wpn_m16_shot_03`, `mus_explore_calm_a`).
Folders: /Game/Audio/{Music,VO,SFX,Ambient,UI,MetaSounds,Classes}. Memory budget: TBD after the first import.

## 6. Unreal implementation (UE 5.8)
- MetaSound Sources for: wind (blizzard intensity parameter), footsteps (surface switch + random variation), weapons (random layers
  + tail by environment), adaptive music (stems / layers driven by a tension parameter).
- Sound Classes + Submixes per bus (4); Sound Concurrency (max voices per weapon type / per barks speaker); Attenuation presets
  (weapon_near/far, voice, footstep, ambient spot); Quartz clock for music transitions; Sound Mix modifiers for ducking.
- UCodexAudioSubsystem drives the music state and barks from the existing game events (spec: docs/port/codex/inbox/009 draft by
  Gemini 032 item 3).

## 7. Accessibility (BLOCKING rule)
Every gameplay-critical sound has a visual cue or a subtitle - otherwise the feature is BLOCKED, not "advisory": mine warning
(red circle), detection (marker), incoming fire (direction hint), barks (subtitle with the speaker - our HUD text already does this).
Options: per-bus volume, mono, "reduce sudden loud sounds".
