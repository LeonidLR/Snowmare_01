# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port
Feature: Phase 3 — Framework
Task: next: tactical grid / continue PORT_MATRIX in dependency order
<!-- /STATUS -->

## Mode (user, 2026-09-28)
Full port with identical behaviour; autonomous work through `docs/port/PORT_MATRIX.md`, commit each verified
system, ask only on real design forks. Godot project is read-only (may be run for reference).

## Done
- Phase 0 skeleton (commit 2b3b110).
- Game flow FSM (commit cf882c7): `GameFlow/`, 25 tests.
- Squad movement: `Characters/`, `Camera/TacticalCameraPawn`, player controller input, GameMode squad spawn,
  test map `/Game/Maps/L_MovementTest` (created by `Scripts/Editor/create_movement_test_map.py`),
  headless check `Scripts/smoke.ps1` (`CodexTactics.MovementSmoke` → PASS), 39 automation tests pass.

## How to verify
- `Scripts/build.ps1`, `Scripts/test.ps1`, `Scripts/smoke.ps1` (editor must be closed to link).
- NavMesh: `NavMeshBoundsVolume` in each level + `RuntimeGeneration=Dynamic` (no baked navmesh).
- Unreal MCP: `.mcp.json` → http://127.0.0.1:8000/mcp, server autostarts with the editor.

## Decisions (2026-09-28)
- Name CodexTactics; no GAS; Ultra Dynamic Sky/Weather in Phase 6.
- Turn-based only from WaveCombat/RealTime (deviation from Godot, see PORT_MATRIX).
- Movement: NavMesh + Detour Crowd; formation numbers from Godot code (2.8 m back, 2.6 m side); no stamina.
- Effective speeds from balance.tres: walk 2.2, run 7.25, crouch 1.25 m/s, prone x0.28.

## Next
Remaining movement extras (vault, box select/group orders, idle roam, tactical-pause orders) come with their systems.
Next systems in order: tactical grid + LOS (`gorky17_grid_manager.gd`, `gorky17_los.gd`, `gorky17_enums.gd`),
then data import (Phase 2) so balance values stop living in C++ defaults.

## Open questions
- none
