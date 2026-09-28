# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port
Feature: Phase 0 — Project skeleton
Task: Phase 1 — next: tactical grid (gorky17_grid_manager.gd)
<!-- /STATUS -->

## Current task
Phase 0 DONE 2026-09-28: build OK (UBT, 94s), smoke test 1/1 pass, git init + LFS, committed.

## Decisions (2026-09-28)
- Name: CodexTactics, at `Documents/Unreal Projects/CodexTactics` (`Codex/unreal` symlink points here).
- Godot keeps evolving in parallel; its docs/design/data are the source of truth.
- No GAS — plain C++ components.
- Ultra Dynamic Sky/Weather available (Phase 6).
- Rewrite, not convert; logic first with parity tests.

## Done in Phase 1
- Game flow FSM: `Source/CodexTactics/*/GameFlow/`, 25 tests `CodexTactics.GameFlow.*`, all 26 tests pass (2026-09-28). Committed.

## Next (after game flow)
Phase 1 starts with the game flow FSM (replaces the placeholder `ECodexPlayMode`):
`ECodexGamePhase` (Exploration, Cutscene, Preparation, WaveCombat, WaveCleared, PostCombat, GameOver) +
`ECodexCombatMode` (RealTime, TacticalPause, TurnBased) in pure `FGameFlowStateMachine`, driven by `UGameFlowSubsystem`.
Godot reference: `Scenes/movements/main.gd` (toggle_active_pause, _enter/_exit_turn_based_combat, Space tap/hold, waves).
Then grid (`gorky17_grid_manager.gd`), LOS (`gorky17_los.gd`).

## Game flow decisions (user, 2026-09-28)
- Turn-based combat can be entered ONLY from WaveCombat/RealTime: not in Exploration, not in Preparation,
  not during TacticalPause. DEVIATION from Godot (Godot allows entry from pause and outside waves) — intentional.
- Wave rest between waves = Preparation phase with next wave index (Godot: is_wave_active + is_preparation_active both true). Same behaviour.

## Open questions
- none
