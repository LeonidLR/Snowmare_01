# Port Matrix: Godot → Unreal Engine 5.8

Godot paths are relative to `Documents/Codex/godot-test-01/`.
Status: ⬜ not started · 🟨 in progress · ✅ done (build + tests pass) · ➖ not ported (Godot-only tooling)

## Phases

| # | Phase | Status |
|---|---|---|
| 0 | Project skeleton, modules, plugins, build/test scripts | ✅ |
| 1 | Pure logic: game flow FSM, Chebyshev grid, 8-way LOS, AP economy, damage, cold, panic/rage | 🟨 |
| 2 | Data: USTRUCT/DataAsset types + JSON → DataAsset importer | ⬜ |
| 3 | Framework: EventBus, operative character, Enhanced Input, camera, squad formation | ⬜ |
| 4 | Combat: exploration→combat transition, turn queue, commands, HUD, enemy AI | ⬜ |
| 5 | World systems: mines, heat sources, loot, interactables, quests, dialogue, save | ⬜ |
| 6 | Content: asset import (Nanite), animation, VFX, Stage 01 "Bunker Gate", UDS/UDW | ⬜ |

## Systems

| Godot reference | UE target | Phase | Status | Tests |
|---|---|---|---|---|
| `Scenes/movements/main.gd` | `ACodexTacticsGameMode` | 0 | ✅ skeleton | `CodexTactics.Core.Smoke` |
| `Scenes/movements/game_state.gd` | `ACodexTacticsGameState` (exposes flow to UI) | 0 | ✅ | `CodexTactics.Core.Smoke` |
| `Scenes/movements/main.gd` mode flags: preparation, waves, tactical pause, turn-based enter/exit | `FGameFlowStateMachine` + `UGameFlowSubsystem` | 1 | ✅ logic (Space tap/hold input → Phase 3, encounter selection → Phase 4, config from balance → Phase 2) | `CodexTactics.GameFlow.*` (25) |
| `Scripts/tactics/gorky17_enums.gd` | `Combat/TacticalTypes.h` (UENUMs) | 1 | ⬜ | |
| `Scripts/tactics/gorky17_grid_manager.gd` | `UTacticalGridComponent` / `UGridManagerSubsystem` | 1 | ⬜ | |
| `Scripts/tactics/gorky17_los.gd` | `FTacticalLineOfSight` | 1 | ⬜ | |
| `Scripts/tactics/turn_based_combat_manager.gd` | `UTacticalCombatSubsystem` | 1/4 | ⬜ | |
| `Scripts/tactics/tactical_exposed_zones_manager.gd` | `UTacticalExposedZonesSubsystem` | 4 | ⬜ | |
| `Scripts/tactics/tactical_encounter_selector.gd` | `UTacticalEncounterSelector` | 4 | ⬜ | |
| `Scripts/tactics/tactical_grid_overlay.gd` | `ATacticalGridOverlay` (ISM/decals) | 4 | ⬜ | |
| `Scripts/tactics/tactical_hold_sphere.gd` | `ATacticalHoldSphere` | 4 | ⬜ | |
| `Scripts/tactics/gorky17_combat_hud.gd` | UMG `WBP_CombatHUD` + `UCombatHUDWidget` | 4 | ⬜ | |
| `Scripts/components/combat_component.gd` | `UCombatComponent` | 1/4 | ⬜ | |
| `Scripts/components/movement_component.gd` | `UOperativeMovementComponent` / CMC config | 3 | ⬜ | |
| `Scripts/components/panic_component.gd` | `UPanicComponent` | 1 | ⬜ | |
| `Scripts/components/rage_component.gd` | `URageComponent` | 1 | ⬜ | |
| `Scripts/components/allegiance_component.gd` | `UAllegianceComponent` | 1 | ⬜ | |
| `Scripts/components/cold_animation_controller.gd` | AnimBP layer | 6 | ⬜ | |
| `Scripts/components/locomotion_controller.gd`, `Scripts/locomotion_v2/**` | AnimBP + `UOperativeAnimInstance` | 6 | ⬜ | |
| `Scripts/events/event_bus.gd` | `UEventBusSubsystem` | 3 | ⬜ | |
| `Scripts/managers/save_manager.gd` | `USaveGame` + `USaveSubsystem` | 5 | ⬜ | |
| `Scenes/movements/player.gd` | `AOperativeCharacter` + `ACodexTacticsPlayerController` | 3 | ⬜ | |
| `Scenes/movements/follower.gd` | `AOperativeSquadAIController` (formation) | 3 | ⬜ | |
| `Scenes/movements/camera.gd`, `camera_zone_trigger.gd` | `ATacticalPlayerCameraManager`, `ACameraZoneVolume` | 3 | ⬜ | |
| `Scenes/movements/warm_zone.gd` | `AHeatSourceActor` + `UColdSurvivalComponent` | 1/5 | ⬜ | |
| `Scenes/movements/combat_manager.gd`, `combat_trigger.gd`, `combat_wave_controller.gd` | `UTacticalCombatSubsystem`, `ACombatTriggerVolume`, `UWaveController` | 4 | ⬜ | |
| `Scenes/movements/enemy_base.gd` + `enemy_*.gd` (6 types) | `AEnemyCharacterBase` + `UEnemyDataAsset` per type | 4 | ⬜ | |
| `Scenes/movements/enemy_spawn_point.gd`, `mission_start_point.gd` | `AEnemySpawnPoint`, `AMissionStartPoint` | 4 | ⬜ | |
| `Scenes/movements/deployables/**` | `ADeployableBase` subclasses (incl. `AStealthMineActor`) | 5 | ⬜ | |
| `Scenes/movements/loot_crate.gd`, `interactable.gd`, `gate.gd` | `ALootCrate`, `IInteractable`, `AGateActor` | 5 | ⬜ | |
| `Scenes/movements/quest_manager.gd`, `narrative_element.gd`, `dialogue_trigger.gd`, `recruit_susanin.gd` | `UQuestSubsystem`, dialogue data | 5 | ⬜ | |
| `Scenes/movements/autosave_trigger.gd`, `relocatable_object.gd` | `AAutosaveTrigger`, `ARelocatableObject` | 5 | ⬜ | |
| `Scenes/weapons/**`, `resources/weapons/*.tres` | `UWeaponDataAsset` | 2 | ⬜ | |
| `resources/characters/**`, `resources/enemies/**` | `UOperativeDataAsset`, `UEnemyDataAsset` | 2 | ⬜ | |
| `data/configs/levels/*.json` | `ULevelConfigAsset` via importer | 2 | ⬜ | |
| `Scenes/ui/**` (inventory, pause, dialogue, profile) | UMG widgets | 5 | ⬜ | |
| `*.gdshader` (silhouette, rings, AoE) | Materials | 6 | ⬜ | |
| `tools/**`, `Scripts/editor/**`, `Scenes/tools/**`, `scratch/**` | — | — | ➖ | |

## Intentional deviations from Godot

| Where | Godot | UE | Decided |
|---|---|---|---|
| Turn-based entry | Allowed whenever enemies are within 15 m, incl. exploration, preparation and tactical pause | Only from WaveCombat/RealTime | User, 2026-09-28 |
| Wave rest | `is_wave_active` and `is_preparation_active` both true | `Preparation` phase with next wave index | User, 2026-09-28 — same behaviour |
