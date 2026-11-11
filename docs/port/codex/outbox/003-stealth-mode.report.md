# 003 - Stealth mode: final interim verification

**Status:** verified on `CodexTactics-interim`, branch `interim/2026-11-12`. Main remains frozen; no merge or push. Runtime baseline: Claude checkpoint `90a6bf64`. The runtime/material follow-up, expanded integration coverage, and pure rules are recorded at `4d15a54f`, `561a29ed`, and `8ff5d194`; the approved animation-copy commit is `1843f002`.

## Delivered behavior

- Action-bar toggle and hotkey **7** apply to the selected eligible operatives. Entry crouches; exit remains crouched. Death, critical injury, knockdown, panic, rage, carrying, traversal, and mounting prevent or end stealth. Firearms and accepted sprint break stealth; grenade throws do not count as firearm shots.
- Runtime grey translucent material instances are applied and restored without saving material assets. Late-created weapon parts and loaded rounds are enrolled; pistol and RPG swaps restore their original material overrides.
- Native eight-direction slow/fast locomotion, wall movement/idle/lean, turns, and peek use the existing cover, corner, and sight systems. Peek reveals only enemies visible through current line of sight; exploration uses the existing 12-second last-known ghost and combat memory is unchanged. Hound scent and existing fire behavior remain unchanged.
- The 124 retargeted copies (62 male, 62 female) were explicitly approved. Source-pack SHA-256 remained unchanged; no animation source asset was modified.

## Final verification

- Corrected full-header build: **PASS**, 384.38 s. Final smoke-only build: **PASS**, 28.15 s. An earlier cold full build (2,123.52 s) failed on newly added test code (`LeaveCover` reason argument, local `Entry` shadowing, plus stale in-build header diagnostics); these issues were corrected before the passing builds.
- Expanded male `StealthModeSmoke`: **PASS**, 113.1 s (`Saved/Logs/StealthExpanded-results.json`). Expanded rendered smoke: **PASS** (`Saved/Logs/StealthExpanded-rendered.log`).
- Female full scenario: **PASS** (`Saved/Logs/StealthFemale.log`). It applies the existing optimized female mesh to the Medic in memory and reinitializes animation. The committed `L_MovementTest` roster is still three male meshes; do not describe its default roster as female. Reproduction needs `-Extra '-Multiprocess -UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation'`.
- **44/44** `Characters.StealthMode`, animation, `AI.Perception`, and `Combat.Sight` tests: **PASS** (`Saved/Logs/StealthExpanded-unit.log`). `CheckMap`: **PASS**.
- Six expanded cases pass: late pistol/RPG parts and restore; four real wall legs (left/right, forward/reverse) with phase/yaw and idle checks; controlled movement-component velocities 120/300/480 sampled through `NativeUpdateAnimation` (not a 1,200 cm navigation run); patrol hearing at 200 cm without stealth / with stealth and 100 cm with stealth, plus wall occlusion; female copy paths, skeleton, speed/phase measurements and 0.000 cm root drift; real knockdown recovery/no re-entry and lethal health loss with stealth state, animation weight, and materials cleaned up.
- Three 1280x720 game-camera frames were visually inspected and copied with matching SHA-256 into the only allowed main-worktree outputs: `Saved/Orchestrator/stealth/stealth.png`, `wall.png`, and `peek_revealed_enemy.png`. No main source or asset files were changed.
- Earlier required regression baseline, not rerun in this final expanded pass: `InjuredLocomotionSmoke` 76.6 s, `ProneCrawlSmoke` 67.6 s, `CoverSwitchSmoke` 59.9 s, `FormationLabSmoke` 201.3 s, `SoloStartSmoke` 56.6 s, and original `StealthModeSmoke` 57.4 s; all six had passed. Gemini review `1a05804e`: **PASS**, minor recommendations.

## Scope notes and routing record

- Female verification is a smoke fixture using the configured existing optimized body in memory; the committed movement-map roster was not edited. Fast gait verifies controlled animation sampling and does not claim a long-distance navigation measurement.
- Default stealth exit remains crouched. The authorized uncrouch copy is present but is not played on toggle exit. No grenade-throw firearm break is expected.
- Router record: `3 | Codex main implementation/testing | PASS after test-fixture corrections`; `2 | gpt-6-luna API lookup | PARTIAL REDO, corrected and primary-verified (protected patrol perception method was initially described as public)`; this final documentation part: `1 | gpt-6-luna | REDO: parent corrected date and stale queue/banner status; final reviewed`.
- The next queue items are Claude's relayed tasks 004 and 005, followed by C5/C6. Neither is claimed complete here. Designer playtest is ready in the interim branch only.

## Reproduction

From the interim worktree, with CODEX_AGENT=codex:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.StealthModeSmoke -Jobs 1 -NoRetry -ExtraArgs '-Multiprocess'
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command 'CodexTactics.StealthModeSmoke female' -Extra '-Multiprocess -UseFixedTimeStep -FPS=60 -FixedSeed -SquadStart=formation' -Log StealthFemale.log -TimeoutSeconds 420
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter 'CodexTactics.Characters.StealthMode+CodexTactics.Animation+CodexTactics.AI.Perception+CodexTactics.Combat.Sight'
```

Rendered evidence used the same console command with `shots`, `-RenderOffscreen -ResX=1280 -ResY=720 -SquadStart=formation`; full command line is in Saved/Logs/StealthExpanded-rendered.log.
