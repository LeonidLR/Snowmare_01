# Port Matrix: Godot → Unreal Engine 5.8

Godot paths are relative to `Documents/Codex/godot-test-01/`.
Status: ⬜ not started · 🟨 in progress · ✅ done (build + tests pass) · ➖ not ported (Godot-only tooling)

## Phases

| # | Phase | Status |
|---|---|---|
| 0 | Project skeleton, modules, plugins, build/test scripts | ✅ |
| 1 | Pure logic: game flow FSM, Gorky grid, LOS, AP economy, damage, cold, panic/rage | ✅ (panic inert in Godot) |
| 2 | Data: USTRUCT/DataAsset types + JSON → DataAsset importer | 🟨 weapons, balance, dialogues, levels ✅; camera user-tuned |
| 3 | Framework: EventBus, operative character, Enhanced Input, camera, squad formation | 🟨 all but EventBus ✅ |
| 4 | Combat: exploration→combat transition, turn queue, commands, HUD, enemy AI | ✅ flow, pause, waves, turn-based, enemy AI, squad fire, rage, AI grenades, hold sphere, wave victory ✅ |
| 5 | World systems: mines, heat sources, loot, interactables, quests, dialogue, save | ✅ |
| 6 | Content: asset import (Nanite), animation, VFX, Stage 01 "Bunker Gate", UDS/UDW | 🟨 operative model + animations ✅ |

## Systems

| Godot reference | UE target | Phase | Status | Tests |
|---|---|---|---|---|
| `Scenes/movements/main.gd` | `ACodexTacticsGameMode` | 0 | ✅ skeleton | `CodexTactics.Core.Smoke` |
| `Scenes/movements/game_state.gd` | `ACodexTacticsGameState` (exposes flow to UI) | 0 | ✅ | `CodexTactics.Core.Smoke` |
| `Scenes/movements/main.gd` mode flags: preparation, waves, tactical pause, turn-based enter/exit | `FGameFlowStateMachine` + `UGameFlowSubsystem` | 1 | ✅ logic (Space tap/hold input → Phase 3, encounter selection → Phase 4, config from balance → Phase 2) | `CodexTactics.GameFlow.*` (25) |
| `Scripts/tactics/gorky17_enums.gd` | `Tactics/Gorky17Types.h` (UENUMs, arc zones) | 1 | ✅ (Gemini) | `CodexTactics.Tactics.*` |
| `Scripts/tactics/gorky17_grid_manager.gd` | `Tactics/UGorkyGridManager` (cells, occupancy, AP BFS, A*) | 1 | ✅ (Gemini) | `CodexTactics.Tactics.*` |
| `Scripts/tactics/gorky17_los.gd` | `GorkyLineOfSight::HasLineOfSight` | 1 | ✅ | `CodexTactics.Tactics.LineOfSight` |
| `Scripts/tactics/turn_based_combat_manager.gd` | see `UTurnBasedCombatSubsystem` below | 1/4 | ➖ | |
| `Scripts/tactics/tactical_exposed_zones_manager.gd` | `FExposedZones` in `UTurnBasedCombatSubsystem` | 4 | ✅ | ExposedZonesTest, ExposedZonesSmoke |
| `Scripts/tactics/tactical_encounter_selector.gd` | — (not used by the Godot game) | 4 | ➖ | |
| `Scripts/tactics/tactical_grid_overlay.gd` | `ATurnGridOverlayActor` | 4 | ✅ | TurnBasedSmoke |
| `Scripts/tactics/tactical_hold_sphere.gd` | `AHoldSphereActor`, controller cease fire, HUD `DrawSpaceCharge` | 4 | ✅ | HoldSphereSmoke, `HudShot hold` |
| `Scripts/tactics/turn_based_combat_manager.gd` (core loop), `tactical_grid_overlay.gd` | `UTurnBasedCombatSubsystem`, `ATurnGridOverlayActor` | 4 | ✅ (exposed zones, barrel / turret / barricade relocation, deployables on the grid, weapon switch, grenades, stasis look; companion drone: no-op in Godot) | `CodexTactics.TurnBasedSmoke`, `ExposedZonesSmoke`, `TurnBasedPushSmoke`, `TurnBasedBarricadeSmoke`, `TurnBasedDeploySmoke`, `WeaponSelectorSmoke`, `GrenadeSmoke`, `Combat.Grenade.Rules`, `Characters.Arsenal.InitAndSwitch`, `Tactics.ExposedZones.*` (3), `HudShot turnbased` |
| `Scripts/tactics/gorky17_combat_hud.gd` | `UTurnBasedHudWidget` | 4 | ✅ (hold-charge bar = HUD `DrawSpaceCharge`; Godot log_message only prints to the console — the lines go to the feed as «GORKY 17») | `TurnBasedSmoke`, `HudShot turnbased` |
| `Scripts/components/combat_component.gd` | — (player.gd creates it but nothing calls it; firing lives in player.gd → `AOperativeCharacter`) | 1/4 | ➖ | |
| `Scripts/components/movement_component.gd` | not ported: its stamina is unused by `player.gd` (user: no stamina) | — | ➖ | |
| `Scripts/components/panic_component.gd` | — (inert: enable_realtime_panic = false, turn-based never uses it) | 1 | ➖ | |
| `enemy_cutter.gd`, `resources/enemies/anims/*.tres` | `AEnemyCharacter` cutter jump, `import_enemy_anim_configs.py` -> DA_EnemyAnim_* | 3 | ✅ | CutterSmoke |
| `enemy_base.gd` behaviour, `enemy_frost_spitter.gd`, `enemy_frost_brute.gd`, affinities of all enemy scripts | `EnemyAIRules`, `AEnemyCharacter::Tick` / `FindTarget` / `TickSpitter` / `AttackObject` | 3 | ✅ (cryo drone later) | `CodexTactics.Characters.EnemyAI.Rules`, EnemyAISmoke |
| `player.gd` `_evaluate_ai_grenade_opportunity`, `execute_ai_grenade_throw`, `_auto_switch_on_empty` | `AIGrenadeRules`, `AOperativeCharacter::TryAIGrenadeThrow` / `AutoSwitchOnEmpty` | 4 | ✅ | `CodexTactics.Combat.Grenade.AIRules`, AIGrenadeSmoke |
| overhead Label3D (`enemy_base.gd`, `barricade.gd`, `turret.gd`, `interactable.gd` generator) | `FOverheadLabel`, `GetOverheadLabel`, HUD `DrawWorldLabels` | 5 | ✅ | `HudShot labels` |
| `Scripts/components/allegiance_component.gd` | — (only used by Godot tests) | 1 | ➖ | |
| `Scripts/components/locomotion_controller.gd` (stand / crouch / prone locomotion) | `UOperativeAnimInstance` native blend + `ABP_Operative` | 6 | 🟨 baseline (fire, reload, hit, death, grenade states → AnimBP graph) | `CodexTactics.StanceSmoke`, `HudShot close walk` |
| `Scripts/components/cold_animation_controller.gd` | AnimBP layer | 6 | ⬜ | |
| `Scripts/components/locomotion_controller.gd`, `Scripts/locomotion_v2/**` | AnimBP + `UOperativeAnimInstance` | 6 | ⬜ | |
| `Scripts/events/event_bus.gd` | per-subsystem delegates (OnGameFlowChanged, OnWaveStarted, OnMessagePosted, ...) | 3 | ➖ | |
| `Scripts/managers/save_manager.gd` | `USaveGameSubsystem` (JSON, Godot keys) + `SaveGameRules` | 5 | ✅ | `CodexTactics.Core.SaveGameRules.*`, SaveLoadSmoke, PauseMenuSmoke |
| `main.gd` radius_ring | `URadiusRingSubsystem` + `ARadiusRingActor` | 5 | ✅ | RadiusRingSmoke |
| `Scenes/movements/player.gd` — movement: click/double-click orders, speeds, stances, sprint rules | `AOperativeCharacter`, `OperativeMovementRules`, `AOperativeAIController` (NavMesh + Detour Crowd), `ACodexTacticsPlayerController` | 3 | ✅ (vault, phasing, box select, group orders, idle roam, pause orders, panic/rage refusal → later) | `CodexTactics.Movement.*` (5), `CodexTactics.MovementSmoke` |
| `Scenes/movements/player.gd` — formation, `follower.gd` | `USquadSubsystem`, `SquadFormation` (slots, column, swap, wander, catch-up) | 3 | ✅ | `CodexTactics.Formation.*` (8), `CodexTactics.MovementSmoke` |
| `main.gd` `_select_squad_member_by_index`, stance keys | `ACodexTacticsPlayerController` (1–3, Z/C/X, Alt = squad) | 3 | ✅ | manual |
| `resources/balance.tres`, `resources/game_balance_config.tres` (GameBalanceConfig) | `UGodotBalanceAsset` DA_Balance / DA_GameBalanceConfig via `Scripts/Editor/import_balance.py` | 2 | 🟨 (turn-based, operatives, cold, enemies wired; deployables need none; camera kept user-tuned) | `CodexTactics.Data.BalanceImportParity` |
| `resources/weapons/*.tres`, `weapon_data.gd` | `UWeaponDataAsset` assets via `Scripts/Editor/import_weapons.py` | 2 | ✅ (switching weapons → later) | `CodexTactics.Data.WeaponImportParity` |
| `Scenes/movements/player.gd` — shooting, reload, cold misfire / aim | `AOperativeCharacter` combat part (Gemini) + `ColdSurvivalComponent` | 4 | ✅ | `CodexTactics.Combat.Squad.*` |
| `Scenes/movements/camera.gd` | `ATacticalCameraPawn` + `TacticalCameraRules` (WASD/edge pan, wheel zoom, Q/E + RMB rotate, MMB pan, turn-based deadzone) | 3 | ✅ (turn-based shake, smooth focus, dramatic action shot, entry zoom) | `CodexTactics.Camera.*` (6), TurnBasedSmoke, TurnBasedCameraSmoke |
| `camera_zone_trigger.gd` | `ACameraZoneVolume` (leader-only, followers hold, environment cold multiplier) | 3 | ✅ (perimeter patrol → with idle roam) | `CodexTactics.Camera.ZoneActivationRules`, `CodexTactics.CameraZoneSmoke` |
| `main.gd` PauseBanner / CombatBanner / CutscenePanel, `_end_cutscene_and_start_pause` squad reset | `UPhaseBannersWidget`, `UMissionSubsystem` | 6 | ✅ (cutscene video → content) | `CodexTactics.BannersSmoke`, `HudShot cutscene / prep` |
| `main.gd` TacticalBar (`_create/_update_tactical_command_bar`, `_cycle_leader_stance`, `_on_relocate_slot_clicked`) | `UActionBarWidget`, controller `CycleLeaderStance` / `ToggleRelocateSelectMode` | 6 | ✅ (transfer, inventory, guard, weapon selector) | `CodexTactics.ActionBarSmoke`, InventorySmoke, TransferSmoke, GuardSmoke, WeaponSelectorSmoke |
| `Scenes/ui/dialogue/bottom_dialogue_dialog.gd`, `resources/dialogue_*.tres`, `main.gd play_dialogue` | `UDialogueSubsystem`, `UDialogueWidget`, `DialogueRules`, `UDialogueSequenceAsset` + `import_dialogues.py` | 6 | ✅ | `CodexTactics.Dialogue.*` (2), `CodexTactics.DialogueSmoke`, `HudShot dialogue` |
| `main.gd` StartMenu, `_on_start_game/combat/exploration_pressed`, `game_state.gd` last_selected_mode / is_quick_restart | `UMainMenuWidget`, `UMissionSessionSubsystem`, `UMissionSubsystem::StartMission` | 6 | ✅ (dialogue on start → dialogue step) | `CodexTactics.Mission.StartModeAndMenu`, `CodexTactics.MainMenuSmoke` |
| `main.gd` `update_objective`, `_trigger_game_over`, `_on_restart_pressed`, Ctrl + X, GameOverPanel | `UMissionSubsystem`, `MissionRules`, `UMissionFailedWidget`, HUD objective banner | 6 | ✅ | `CodexTactics.Mission.*` (2), `CodexTactics.MissionSmoke`, `HudShot failed` |
| `main.gd` `_on_quest_message` | `UGameMessageSubsystem` | 3 | ✅ logic (HUD panel → UI step) | via CameraZoneSmoke |
| `quest_manager.gd`, `interactable.gd` (quest part), `gate.gd`, `warm_zone.gd` | `FQuestChainState`, `UQuestSubsystem`, `AInteractableActor`, `UInteractionSubsystem`, `AGateActor`, `UHeatSourceComponent` | 5 | ✅ | `CodexTactics.Quests.*` (8), `CodexTactics.QuestChainSmoke` |
| `main.gd` Ctrl + click branch, `player.gd` `shoot_at_*`, `calculate_mine_shot_hit_chance`, `set_manual_priority_target`, planned shots | `ACodexTacticsPlayerController::IssueTargetedShot`, `AOperativeCharacter::ShootAtObject` / priority target, `TargetedShotRules` | 5 | ✅ (untrapped crate: pierced only — user decision) | `CodexTactics.Combat.TargetedShot.*` (2), `CodexTactics.TargetedShotSmoke` |
| `player.gd` / `turret.gd` `_spawn_muzzle_tracer`, `main.gd` `_spawn_waypoint_marker`, `_highlight_target_feedback` | `UCombatFeedbackSubsystem`, `ACombatFeedbackActor`, `M_CombatFeedback` | 5 | ✅ | `TargetedShotSmoke`, `HudShot shoot` (visual) |
| `main.gd` KEY_SPACE tap / hold, pause orders (`planned_move_pos`) | `FSpaceInputTracker`, `ACodexTacticsPlayerController`, `USquadSubsystem::PlanMove` | 4 | ✅ input + planning (markers, hold sphere, command bar → UI step) | `CodexTactics.Combat.*` (5), `CodexTactics.CombatFlowSmoke` |
| `main.gd` `_trigger_menu_for_object` / `_open_action_menu` / `_on_action_confirmed` + `interactable.gd` barrel | `UInteractionSubsystem` menu flow, `UActionMenuWidget` (UMG), `AInteractableActor::BuildActionMenu`, `ABarrelActor` | 4 | ✅ quest objects + barrels (loot dialog next) | `CodexTactics.Barrel.*`, `CodexTactics.BarrelSmoke`, `QuestChainSmoke` |
| `deployables/barricade.gd`, `mine.gd`, `interactable.gd` traps, `main.gd` deploy / dismantle / trap | `ADeployableActor`, `ABarricadeActor`, `AProximityMineActor`, `DeployableRules`, traps on `AInteractableActor`, F placement in `URelocationSubsystem` | 4 | ✅ barricades, mines, turrets, traps, supply, loot crates | `CodexTactics.Deployable.*`, `CodexTactics.DeployableSmoke` |
| `main.gd` relocation (`_start_relocate_for_node`, `_handle_relocate_click`, `active_relocate_tasks`) | `URelocationSubsystem`, `ARelocationGhostActor`, `RelocationRules` | 4 | ✅ push/carry, ghost, pause planning, combat drop | `CodexTactics.Relocation.*`, `CodexTactics.RelocationSmoke` |
| `Scenes/movements/warm_zone.gd` + `player.gd _process_cold_system` | `UHeatSourceComponent` + `ColdRules` + `UColdSurvivalComponent` | 1/5 | ✅ real-time (turn-based per-round cold comes with the grid combat) | `CodexTactics.Cold.*`, `CodexTactics.ColdSmoke` |
| `Scenes/movements/combat_wave_controller.gd` | `UWaveSubsystem` (Gemini) | 4 | ✅ | `CodexTactics.WaveCombatSmoke` |
| `Scenes/movements/combat_manager.gd`, `combat_trigger.gd` | — (CombatManager is never instanced; main.gd disables the gate CombatTrigger; flow = `UGameFlowSubsystem`) | 4 | ➖ | |
| `Scenes/movements/enemy_base.gd` + `enemy_*.gd` | `AEnemyCharacter`, `AEnemyAIController`, `UEnemyArchetypeAsset` (Gemini) | 4 | ✅ (AI parity, cutter pounce; cryo drone unused by any level) | `CodexTactics.Combat.Enemy*`, `Characters.EnemyAI.Rules`, `WaveCombatSmoke`, EnemyAISmoke, CutterSmoke |
| `Scenes/movements/enemy_spawn_point.gd`, `mission_start_point.gd` | `AEnemySpawnPoint` (lane, type filter, dynamic breach); the mission start point is the level's `APlayerStart` (game mode squad spawn) | 4 | ✅ | FlankBreachSmoke |
| `Scenes/movements/deployables/turret.gd`, `interactable.gd` generator damage / repair | `ATurretActor`, generator part of `AInteractableActor` | 5 | ✅ (enemies attacking objects → enemy AI; tracers → VFX) | `CodexTactics.TurretSmoke` |
| `Scenes/movements/loot_crate.gd`, `Scenes/ui/inventory/loot_dialog.gd`, `main.gd` loot handlers | `ALootCrateActor`, `LootRules`, `ULootDialogWidget`, `UInteractionSubsystem` loot API | 5 | ✅ (consumable use → inventory drawer) | `CodexTactics.Loot.*`, `CodexTactics.LootSmoke` |
| `Scenes/movements/quest_manager.gd` | `UQuestSubsystem`, `QuestChain` | 5 | ✅ | QuestChainSmoke |
| `Scenes/movements/narrative_element.gd`, `dialogue_trigger.gd` | `ANarrativeElementActor`, `ADialogueTriggerVolume` | 5 | ✅ | NarrativeSmoke |
| `Scripts/components/rage_component.gd` | `URageComponent`, `RageRules` | 4 | ✅ (aura ring later) | `CodexTactics.Characters.Rage.Rules`, RageSmoke |
| `player.gd` `_find_shoot_target`, `_shoot_at_target`, `get_elevation_advantage`, `is_target_in_dead_zone` | `SquadFireRules`, `AOperativeCharacter::FindShootTarget` / `ShootAtTarget` | 4 | ✅ (rage / panic / AI grenade later) | `CodexTactics.Combat.SquadFire.Rules`, SquadFireSmoke |
| `player.gd` / `enemy_base.gd` / `mine.gd` `_spawn_floating_combat_text`, `_spawn_heal_feedback`, player `take_damage` | `UFloatingTextSubsystem`, HUD `DrawFloatingTexts`, `AOperativeCharacter::TakeHit` | 5 | ✅ (texts of unported features pending) | FloatingTextSmoke |
| `Scenes/movements/recruit_susanin.gd`, `main.gd` Susanin rescue event | `URecruitSubsystem`, `EOperativeRole::Recruit`, `AOperativeCharacter::bRecruited` | 5 | ✅ | SusaninSmoke |
| `Scenes/movements/autosave_trigger.gd`, `relocatable_object.gd` | autosave: not ported (Godot's only trigger has no collision shape, HANDOFF §9); relocatable = `AInteractableActor::bCanBeRelocated` | 5 | ✅ | RelocationSmoke |
| `Scenes/weapons/**`, `resources/weapons/*.tres` | `UWeaponDataAsset` DA_Weapon_* via `Scripts/Editor/import_weapons.py` | 2 | ✅ | `CodexTactics.Combat.Data*` |
| `resources/enemies/anims/*.tres`, `resources/characters/anims/*.tres` | enemies: DA_EnemyAnim_* via `Scripts/Editor/import_enemy_anim_configs.py` (cutter pounce numbers); characters: animation clip mapping → Phase 6 AnimBP (user-owned) | 2/6 | 🟨 (characters with the AnimBP) | CutterSmoke |
| `data/configs/levels/*.json`, `main.gd _load_active_level_config`, `_get_enemy_spawn_pos` | `ULevelConfigAsset` DA_Level_* via `Scripts/Editor/import_levels.py`; game mode `LevelConfig` → `UWaveSubsystem::SetLevelConfig` + `LevelFlowRules::ApplyLevel` (prep / rest / wave count); `AEnemySpawnPoint` lanes | 2 | ✅ | `CodexTactics.Core.LevelFlow.*` (2), `Combat.Enemy.WaveModifiers`, `LevelWaveSmoke` |
| `Scenes/ui/**` (inventory, pause, dialogue, profile) | UMG widgets (loot, inventory drawer, transfer, pause, save / load, dialogue, profile) | 5 | ✅ | |
| `main.gd` register_enemy_kill, _on_wave_cleared, _on_next_wave_pressed, _start_post_combat_sequence, _auto_recover_all_deployables; VictoryPanel | `KillStatsRules`, `UWaveVictorySubsystem`, `UVictoryPanelWidget` | 4 | ✅ | `CodexTactics.Combat.KillStats.Rules`, VictorySmoke, `HudShot victory` |
| `player.gd` level / EXP / stat points, `Scenes/ui/profile/profile_dialog.gd`, `enemy_base.gd` kill EXP | `ProgressionRules`, `AOperativeCharacter::AddExp` / `IncreaseStat`, `UProfileDialogWidget` | 5 | ✅ | `CodexTactics.Characters.Progression.Rules`, ProgressionSmoke, `HudShot profile` |
| `*.gdshader` (silhouette, rings, AoE) | Materials | 6 | ⬜ | |
| `tools/**`, `Scripts/editor/**`, `Scenes/tools/**`, `scratch/**` | — | — | ➖ | |

## Intentional deviations from Godot

| Where | Godot | UE | Decided |
|---|---|---|---|
| Turn-based entry | Allowed whenever enemies are within 15 m, incl. exploration, preparation and tactical pause | Only from WaveCombat/RealTime | User, 2026-09-28 |
| Wave rest | `is_wave_active` and `is_preparation_active` both true | `Preparation` phase with next wave index | User, 2026-09-28 — same behaviour |
| Pathing | Straight line to target, wall sliding, whisker steering, leader breadcrumbs, collider phasing | NavMesh pathfinding + Detour Crowd avoidance | User, 2026-09-28 |
| Stamina | Described in GDD / unused `movement_component.gd`; not active in `player.gd` | No stamina; sprint limited by cold and wounds only | User, 2026-09-28 |
