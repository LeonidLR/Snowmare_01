# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port
Feature: Phase 0 — Project skeleton
Task: Phase 1 — game flow FSM (awaiting answers on turn-based scope)
<!-- /STATUS -->

## Current task
Phase 0 DONE 2026-09-28: build OK (UBT, 94s), smoke test 1/1 pass, git init + LFS, committed.

## Decisions (2026-09-28)
- Name: CodexTactics, at `Documents/Unreal Projects/CodexTactics` (`Codex/unreal` symlink points here).
- Godot keeps evolving in parallel; its docs/design/data are the source of truth.
- No GAS — plain C++ components.
- Ultra Dynamic Sky/Weather available (Phase 6).
- Rewrite, not convert; logic first with parity tests.

## Next
Phase 1 starts with the game flow FSM (replaces the placeholder `ECodexPlayMode`):
`ECodexGamePhase` (Exploration, Cutscene, Preparation, WaveCombat, WaveCleared, PostCombat, GameOver) +
`ECodexCombatMode` (RealTime, TacticalPause, TurnBased) in pure `FGameFlowStateMachine`, driven by `UGameFlowSubsystem`.
Godot reference: `Scenes/movements/main.gd` (toggle_active_pause, _enter/_exit_turn_based_combat, Space tap/hold, waves).
Then grid (`gorky17_grid_manager.gd`), LOS (`gorky17_los.gd`).

## Open questions
- Turn-based entry in Godot only checks enemies within 15 m (not wave state) — allowed in Exploration/Preparation by design?
- Wave rest: Godot sets is_wave_active + is_preparation_active together; UE models it as Preparation(next wave) — OK?
