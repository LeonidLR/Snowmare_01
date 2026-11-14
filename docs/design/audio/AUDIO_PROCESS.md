# Audio process (roles + documents) - adapted from Claude-Code-Game-Studios "team-audio" (Copyright (c) 2026 Donchitos, MIT)

We do NOT install that skill (it spawns up to 6 Claude sub-agents per area - too expensive). We keep its structure:

| Step | Their role | Ours | Output |
|---|---|---|---|
| 1 Direction | audio-director | Claude | docs/design/audio/SOUND_BIBLE.md (tone, palette, adaptive rules, mix targets) |
| 2a SFX spec + event list | sound-designer | Gemini drafts, Claude approves | docs/design/audio/EVENTS.md |
| 2b Accessibility | accessibility-specialist | Claude (checklist) | section 7 of the bible; BLOCKING if a critical sound has no visual cue |
| 3 Integration plan | technical-artist + engine specialist | Gemini map (032 item 3) + Claude (ue-audio-and-metasounds skill) | docs/port/codex/inbox/009-audio-subsystem.md |
| 4 Implementation | gameplay-programmer | Sonnet (Claude's brief) + Codex, tests + smokes | UCodexAudioSubsystem, MetaSounds, classes, attenuation |
| Generation | - | Gemini (Lyria, Gemini TTS, SFX source TBD) | Saved/AudioGen + manifests |
| Ears | - | the designer | approve / reject in the Training Ground audio panel |

## EVENTS.md - one row per audio event
| id | system / trigger (file:line) | asset(s) + variations | bus | 3D? attenuation preset | priority (1-5) | concurrency (max voices, steal rule) | cooldown | ducking (who ducks whom) | visual cue / subtitle | notes |

## Per-SFX spec fields (for generation briefs)
description; reference sounds; frequency character; duration; volume range; spatial (attenuation curve, min / max distance,
volume + high-frequency roll-off); variations (count, pitch randomisation range, round-robin).
Ambience: base layer + detail sounds + one-shots + transitions.
