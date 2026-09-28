# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port
Feature: Phase 5 / Vertical Slice — Quests & Interactables (Canister, APC, Generator, Gate)
Task: Implement QuestSubsystem, InteractableActor, GateActor and parity tests (per TANDEM.md directive)
<!-- /STATUS -->

## Mode (user, 2026-09-28)
Full port with identical behaviour; autonomous work through `docs/port/PORT_MATRIX.md`, commit each verified
system, ask only on real design forks. Godot project is read-only (may be run for reference).
Supervisor Directive (Gemini): consult `docs/port/ARCHITECT_SUPERVISOR_DIRECTIVE.md` for exact formulas & parity rules.


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
Port order now follows the user's reference playthrough: `docs/port/REFERENCE_PLAYTHROUGH_01.md`
(vertical slice: camera → UI shell → cold/heat → interactables/quests → mines → deployables →
preparation/waves/combat → tactical pause UI → turn-based → content).
Next task: camera controls (WASD pan, zoom, Q/E 45°) + camera trigger zones (leader only), from `camera.gd`,
`camera_zone_trigger.gd`.

## Open questions
- none
