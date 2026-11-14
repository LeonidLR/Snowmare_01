# Task 052: In-Game Audio Step 1 - Subsystem Event Subscriptions & Rules

- **Branch:** `gemini/052-audio-hookup` (branched from `main` `664d78b27f54c0cda71a1651d8f6f8ce3bdf6ca1`)
- **Status:** Complete (PASS)
- **Governing Specs:**
  - `docs/design/audio/EVENTS.md` (§1 Adaptive Music States & §2 Voice Lines)
  - `docs/design/audio/SOUND_BIBLE.md`
  - `Content/Data/Audio/music_cues.json` (22 cues across 8 states)
  - `Content/Data/Audio/voice_lines.json` (49 voice lines across 6 speakers)

---

## 1. Architecture & Implementation Summary

Per directive #370, implemented In-Game Audio Step 1 with strictly decoupled pure evaluation rules, zero asset import requirements, and automatic silent fallback for unimported audio assets.

### 1.1 Types (`Source/CodexTactics/Public/Audio/CodexAudioTypes.h`)
- `ECodexMusicState`:
  - `Menu`: Frontend UI map (`UCodexFrontendSubsystem::IsFrontendWorld`).
  - `ExploreCalm`: Default baseline peaceful exploration (`Clear`, `Overcast`, `Snowfall`).
  - `ExploreCold`: Sub-zero blizzard survival exploration (`Blizzard` weather).
  - `Tension`: Threat proximity, enemy visual suspicion, or combat pressure outside wave combat.
  - `Combat`: Standard active wave firefight (`WaveCombat` phase).
  - `CombatHeavy`: High-threat escalated combat (live horde release, `UHordeSubsystem`).
  - `Victory`: Wave cleared (`WaveCleared`) or mission completed (`PostCombat`).
  - `Defeat`: Squad wiped / mission failure (`GameOver`).
- `FCodexMusicCueDef`: Struct mirroring `Content/Data/Audio/music_cues.json`.
- `FCodexVoiceLineDef`: Struct mirroring `Content/Data/Audio/voice_lines.json`.

### 1.2 Evaluation Rules (`Source/CodexTactics/Public/Audio/CodexAudioRules.h`, `Private/Audio/CodexAudioRules.cpp`)
- `EvaluateMusicState`: Pure evaluation function resolving world context to `ECodexMusicState` per `EVENTS.md` §1 hierarchy.
- `SelectMusicCueForState` & `GetAvailableMusicCuesForState`: Deterministic variant selection across all 22 registered cues.
- `MapEventToVoiceLine`: Maps both explicit bark IDs, `vo_` event IDs, and generic gameplay events (`OnSoldierDowned`, `OnMineSpotted`, `OnRageStarted`, `OnGeneratorStarted`, `OnHordeReleased`, etc.) to the 49 voice lines.
- `CanPlayVoiceLine`: Enforces phrase-level and speaker-level cooldown windows (5s to 20s).
- `DoesSoundAssetExist`: Uses non-intrusive package query (`FPackageName::DoesPackageExist`) to avoid engine load warnings when sound assets are not yet imported.
- `LoadMusicCuesFromJson` & `LoadVoiceLinesFromJson`: Full JSON parsers verifying schema compliance against `music_cues.json` and `voice_lines.json`.

### 1.3 Subsystem Integration (`Source/CodexTactics/Public/Audio/CodexAudioSubsystem.h`, `Private/Audio/CodexAudioSubsystem.cpp`)
- `UCodexAudioSubsystem : public UGameInstanceSubsystem`:
  - Cross-level persistence supporting frontend menu music as well as in-game missions.
  - Subscribed to `UCodexEventBus` dynamic delegates:
    - `OnDialogueLineDisplayed`
    - `OnWeatherChanged`
    - `OnSoldierDowned`
    - `OnMineSpotted`
    - `OnRageStarted`
    - `OnGeneratorStateChanged`
    - `OnSoldierPanicked`
    - `OnGameplayEvent`
  - Lifecycle hook `FWorldDelegates::OnPostWorldInitialization`:
    - Automatically discovers world-level subsystems (`UGameFlowSubsystem`, `UHordeSubsystem`) and binds to `OnGameFlowChanged` and `OnHordeReleased`.
  - Silent No-Op Policy:
    - When an asset package is missing on disk, logs `Verbose` to `LogCodexTactics` and safely returns without attempting invalid loads or crashing.

---

## 2. Test Evidence

### 2.1 Audio Unit Test Suite (`Source/CodexTacticsTests/Private/Audio/CodexAudioRulesTest.cpp`)
- `CodexTactics.Audio.MusicStateTransitions`: PASS (verifies all 8 states in priority sequence).
- `CodexTactics.Audio.MusicCueSelection`: PASS (verifies all 22 cue selections and variant wrapping).
- `CodexTactics.Audio.VoiceLineMapping`: PASS (verifies event-to-bark resolution).
- `CodexTactics.Audio.VoiceLineCooldown`: PASS (verifies cooldown gating).
- `CodexTactics.Audio.JsonDataValidation`: PASS (loads and parses 22 cues and 49 voice lines from `Content/Data/Audio/`).
- `CodexTactics.Audio.MissingAssetSilentNoOp`: PASS (confirms package existence check and path sanitation).

### 2.2 Verification Summary
- **Build (`powershell .\Scripts\build.ps1`):** SUCCESS (11.92s).
- **Audio Unit Tests (`test.ps1 -Filter "CodexTactics.Audio"`):** 6/6 PASS.
- **Smart Test Gate (`test.ps1 -Smart`):**
  - Unit tests: 15/15 PASS.
  - Smokes: 5/5 PASS:
    - `CombatFlowSmoke`: PASS (66.4s)
    - `EventBusSmoke`: PASS (52.3s)
    - `MissionSmoke`: PASS (77.3s)
    - `MovementSmoke`: PASS (60.4s)
    - `StanceSmoke`: PASS (71.4s)
- **Triage Audit (`typesafe_triage.py --audit-diff --agent gemini`):** PASS (0 boundary breaches, C++ strictly confined to `Audio/`).
