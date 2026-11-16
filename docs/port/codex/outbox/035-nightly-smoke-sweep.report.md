# 035 - Nightly smoke sweep

Frozen base: `aba810f1e5d29a6268cf8b88f58801aa9a6c94ab` on `codex/035-nightly-sweep`; registry: 284 names in original order. Includes main 034/036. No code or flag changes.

Build: PASS (exit 0; UBT 2.18 s). Resume verification after limit reset: DLL mtimes precede original 18:52:13 UTC build; engine binary unchanged (2026-10-22), Build.log identical to preserved evidence; no rebuild needed. Evidence: `Saved/Orchestrator/035/build-wrapper.log`, `build-engine.log`.

Router: level 2, confidence 0.62 | gpt-6.1-sol | pass (execution/reporting; no local model).

Progress: 192/284 initial runs; 186 final PASS; 6 final FAIL; 6 failed twice. Updated 2026-11-16T22:04:34.077220+00:00.

Initial batches <=8, script default Jobs=4 (16 CPUs), `-NoRetry -ExtraArgs -Multiprocess`; each initial FAIL rerun exactly once alone with `-Jobs 1`. Engine `Smoke RESULT` controls PASS/FAIL, including wrapper KNOWN-FAIL. Seconds are wrapper process wall times. Missing result/timeout is infrastructure FAIL, never an invented assertion. Shared locks and designer priority remain active.

Frozen registry and resumable progress: `Saved/Orchestrator/035/registry.json`, `progress.json`, `sweep.py`. Each batch has preserved wrapper output/result JSON and copied engine logs before reruns. Unrun names remain in frozen registry.

| Smoke | Final | Initial seconds | First FAIL / infrastructure | Rerun | Rerun seconds |
|---|---|---:|---|---|---:|
| HitchProbeSmoke | PASS | 43.6 |  | — | — |
| VehicleSmoke | PASS | 67.6 |  | — | — |
| VehicleCombatSmoke | PASS | 65.2 |  | — | — |
| VehicleDamageSmoke | PASS | 60.7 |  | — | — |
| EnemyPrioritySmoke | PASS | 55.6 |  | — | — |
| EnemyArchetypeSmoke | PASS | 47.4 |  | — | — |
| MilitaryZombieSmoke | FAIL | 53.1 | [2026.11.16-18.55.15:331][916]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 0.00, ) | FAIL | 50.1 |
| TacticalHeatSmoke | PASS | 49.3 |  | — | — |
| MovementSmoke | PASS | 51.9 |  | — | — |
| CameraZoneSmoke | PASS | 54.8 |  | — | — |
| CameraStaySmoke | PASS | 53.7 |  | — | — |
| CameraMinZoomSmoke | PASS | 51.1 |  | — | — |
| CameraMinZoomDepotSmoke | PASS | 66.4 |  | — | — |
| QuestChainSmoke | PASS | 62.9 |  | — | — |
| CombatFlowSmoke | PASS | 56.7 |  | — | — |
| WaveCombatSmoke | PASS | 52.3 |  | — | — |
| ColdSmoke | PASS | 51.7 |  | — | — |
| StanceSmoke | PASS | 57.6 |  | — | — |
| OptimizedCharacterSmoke | FAIL | 79.5 | [2026.11.16-19.00.59:179][ 51]LogCodexTactics: Display: OptimizedCharacterSmoke FAIL: [Marksman] death: both bodies still there (original 1, optimized 1), dying, optimized anim dead 1, death clip NOT playing | FAIL | 60.4 |
| BarrelSmoke | PASS | 52.8 |  | — | — |
| RelocationSmoke | PASS | 53 |  | — | — |
| DeployableSmoke | PASS | 53.2 |  | — | — |
| LootSmoke | PASS | 51.9 |  | — | — |
| TurretSmoke | PASS | 45.9 |  | — | — |
| TargetedShotSmoke | PASS | 47.3 |  | — | — |
| MissionSmoke | PASS | 55.6 |  | — | — |
| MainMenuSmoke | PASS | 72.5 |  | — | — |
| DialogueSmoke | PASS | 59 |  | — | — |
| ActionBarSmoke | PASS | 45.6 |  | — | — |
| BannersSmoke | PASS | 47.4 |  | — | — |
| TurnBasedSmoke | PASS | 46.4 |  | — | — |
| LevelWaveSmoke | PASS | 46.2 |  | — | — |
| ExposedZonesSmoke | PASS | 53.7 |  | — | — |
| TurnBasedPushSmoke | PASS | 49.4 |  | — | — |
| TurnBasedBarricadeSmoke | PASS | 48.9 |  | — | — |
| TurnBasedDeploySmoke | PASS | 50.8 |  | — | — |
| WeaponSelectorSmoke | PASS | 48.4 |  | — | — |
| GrenadeSmoke | PASS | 48.6 |  | — | — |
| GuardSmoke | PASS | 49 |  | — | — |
| InventorySmoke | PASS | 48.4 |  | — | — |
| TransferSmoke | PASS | 55 |  | — | — |
| SaveLoadSmoke | PASS | 38.7 |  | — | — |
| SaveRoundTripSmoke | PASS | 58.3 |  | — | — |
| PauseMenuSmoke | PASS | 48.9 |  | — | — |
| RadiusRingSmoke | PASS | 56 |  | — | — |
| FlankBreachSmoke | PASS | 55.5 |  | — | — |
| SusaninSmoke | PASS | 55.4 |  | — | — |
| FloatingTextSmoke | PASS | 39.5 |  | — | — |
| SquadFireSmoke | PASS | 48.7 |  | — | — |
| RageSmoke | PASS | 50.6 |  | — | — |
| SquadControlSmoke | PASS | 51.2 |  | — | — |
| AIGrenadeSmoke | PASS | 49.8 |  | — | — |
| EnemyAISmoke | PASS | 49.2 |  | — | — |
| CutterSmoke | PASS | 49.2 |  | — | — |
| ClickRulesSmoke | PASS | 48.9 |  | — | — |
| NarrativeSmoke | PASS | 48.5 |  | — | — |
| HoldSphereSmoke | PASS | 52.3 |  | — | — |
| ProgressionSmoke | PASS | 51.7 |  | — | — |
| VictorySmoke | PASS | 54.6 |  | — | — |
| TurnBasedCameraSmoke | PASS | 49.7 |  | — | — |
| EventBusSmoke | PASS | 38.3 |  | — | — |
| SilhouetteSmoke | PASS | 43.8 |  | — | — |
| AttackModeSmoke | PASS | 43.8 |  | — | — |
| GroupSelectSmoke | PASS | 44.4 |  | — | — |
| GroupCoverSmoke | PASS | 53.2 |  | — | — |
| TurnWalkSmoke | PASS | 45.9 |  | — | — |
| TurnBlastDeathSmoke | PASS | 46.6 |  | — | — |
| EnemyHuntSmoke | PASS | 45.7 |  | — | — |
| VaultSmoke | PASS | 46.6 |  | — | — |
| DepotVaultSmoke | PASS | 46.3 |  | — | — |
| CornerExitSmoke | PASS | 57.3 |  | — | — |
| BarricadeContactSmoke | PASS | 45.4 |  | — | — |
| CombatMoveSmoke | PASS | 47.7 |  | — | — |
| EnemyDeathSmoke | PASS | 47.2 |  | — | — |
| TurnSelectSmoke | PASS | 46.5 |  | — | — |
| PanicSmoke | PASS | 49.3 |  | — | — |
| FacingSmoke | PASS | 46.2 |  | — | — |
| MarksmanSmoke | PASS | 45.7 |  | — | — |
| MarksmanStealthSmoke | PASS | 48.5 |  | — | — |
| EnemyHitLayerSmoke | PASS | 45 |  | — | — |
| RecruitDeathSmoke | PASS | 44.7 |  | — | — |
| HitFlashOverlaySmoke | PASS | 45.6 |  | — | — |
| BotMarksmanSmoke | PASS | 44.7 |  | — | — |
| BarricadeTurnContactSmoke | PASS | 46.1 |  | — | — |
| MarksmanAdvanceSmoke | PASS | 49 |  | — | — |
| BotRepairSmoke | PASS | 49.1 |  | — | — |
| PatrolSmoke | PASS | 48.2 |  | — | — |
| AmbushSmoke | PASS | 46.9 |  | — | — |
| RealTimeControlSmoke | PASS | 47.1 |  | — | — |
| PostureSmoke | PASS | 46.6 |  | — | — |
| RealtimeSelectSmoke | PASS | 49.7 |  | — | — |
| MarksmanCloseShotSmoke | PASS | 45.1 |  | — | — |
| HordeSmoke | PASS | 46.6 |  | — | — |
| CoverSmoke | PASS | 49.2 | [2026.11.16-21.28.25:356][603]LogCodexTactics: Display: Smoke FAIL: ... standing the _R stand-off from the real wall end (81 cm, stand-off 28; the clip leans 40 cm out) | PASS | 42.4 |
| AimOffsetSmoke | PASS | 44.9 |  | — | — |
| CommanderModeSmoke | PASS | 52.3 |  | — | — |
| CornerEntrySmoke | PASS | 68.6 |  | — | — |
| CornerHoldSmoke | PASS | 76 |  | — | — |
| CoverCrouchSmoke | PASS | 48.5 |  | — | — |
| CoverMoveSmoke | PASS | 51.9 |  | — | — |
| CoverReverseSmoke | PASS | 45.6 |  | — | — |
| CoverWallSmoke | PASS | 47.2 |  | — | — |
| CoverWallMedicSmoke | PASS | 45.8 |  | — | — |
| CoverHugSmoke | FAIL | 43.4 | [2026.11.16-21.31.34:143][870]LogCodexTactics: Display: Smoke FAIL: shimmy back to the edge ordered | FAIL | 39.6 |
| CoverHugMedicSmoke | PASS | 47.8 |  | — | — |
| DefendSmoke | PASS | 48.5 |  | — | — |
| EnemyTacticsSmoke | PASS | 49 |  | — | — |
| GrenadeAnimSmoke | PASS | 49.5 |  | — | — |
| LeftHandIKSmoke | PASS | 49.9 |  | — | — |
| PrepDeploySmoke | PASS | 44.9 |  | — | — |
| SightSmoke | PASS | 49.4 |  | — | — |
| TripwireSmoke | PASS | 46.5 |  | — | — |
| TurnRulesSmoke | PASS | 43.7 |  | — | — |
| BotStealthSmoke | PASS | 49 |  | — | — |
| PauseBarrelOrderSmoke | PASS | 48.2 | [2026.11.16-21.35.10:930][891]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first) | PASS | 41.3 |
| TurnBasedAimSmoke | PASS | 61.9 |  | — | — |
| KnockdownSmoke | PASS | 46 |  | — | — |
| DeathCamSmoke | PASS | 47.3 |  | — | — |
| CameraZoneModeSmoke | PASS | 45.7 |  | — | — |
| FrontendSmoke | PASS | 56.9 |  | — | — |
| SniperSmoke | PASS | 62.1 |  | — | — |
| ShotgunSmoke | PASS | 52.1 |  | — | — |
| ShotgunCoverSmoke | PASS | 48.9 |  | — | — |
| FormationJitterSmoke | FAIL | 67.3 | [2026.11.16-21.38.31:138][468]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) | FAIL | 55.1 |
| PistolSmoke | PASS | 46.3 |  | — | — |
| ProjectileSmoke | PASS | 46 |  | — | — |
| RpgSmoke | PASS | 44.7 |  | — | — |
| RpgAnimSmoke | PASS | 48.2 |  | — | — |
| RpgAiSmoke | PASS | 52 |  | — | — |
| RpgPmcSmoke | FAIL | 48 | [2026.11.16-21.41.35:234][944]LogCodexTactics: Display: Smoke FAIL: 2: Codex.Combat.LauncherAI 0 - the commander's RpgStrike order (2 orders) fires the rocket (0 strike shots; ordered rocket strike at BP_Operative_C_0) | FAIL | 39.6 |
| MortarSmoke | PASS | 47.3 |  | — | — |
| HordeIntroSmoke | PASS | 66.7 |  | — | — |
| SoloRejoinSmoke | PASS | 55.4 |  | — | — |
| CombatFormationSmoke | PASS | 49.7 |  | — | — |
| PatrolAmbushSmoke | PASS | 45.5 |  | — | — |
| VisionConeSmoke | PASS | 44.4 |  | — | — |
| GateGeneratorSmoke | PASS | 64.9 |  | — | — |
| TutorialSmoke | PASS | 153.3 |  | — | — |
| TutorialSaveSmoke | PASS | 78.7 |  | — | — |
| ControlsScreenSmoke | PASS | 37.9 |  | — | — |
| InteriorExitSmoke | PASS | 46.7 |  | — | — |
| PMCTripwireSmoke | PASS | 40.3 |  | — | — |
| CoverEventSmoke | PASS | 47.9 |  | — | — |
| WarmIdleSmoke | PASS | 50.5 |  | — | — |
| BridgeFenceSmoke | PASS | 47.9 |  | — | — |
| TrapSoloSmoke | PASS | 47.5 |  | — | — |
| PeekMinesSmoke | PASS | 49.2 |  | — | — |
| RoofCutawaySmoke | PASS | 48.3 |  | — | — |
| StealthLureSmoke | PASS | 54.8 |  | — | — |
| KnockdownCoverSmoke | PASS | 47.1 |  | — | — |
| HoundScentSmoke | PASS | 50.8 |  | — | — |
| MinimapSmoke | PASS | 49.3 |  | — | — |
| QuickFireSmoke | PASS | 46.4 |  | — | — |
| SuppressionSmoke | PASS | 50.3 | [2026.11.16-21.51.49:846][470]LogCodexTactics: Display: Smoke FAIL: A: ... while the marksman is still free (level 0.30) | PASS | 42 |
| PeekSightSmoke | PASS | 47.4 |  | — | — |
| LeanPoseSmoke | PASS | 51.3 |  | — | — |
| CoverSwitchSmoke | PASS | 53.9 |  | — | — |
| CoverWrapSmoke | PASS | 47 |  | — | — |
| CoverSwitchLakeSmoke | PASS | 44.3 |  | — | — |
| SaveCoverageSmoke | PASS | 44.8 |  | — | — |
| SaveCoverageLakeSmoke | PASS | 46.9 |  | — | — |
| TutorialAmbushLakeSmoke | PASS | 61.8 |  | — | — |
| TutorialAmbushLakeSpottedSmoke | PASS | 59 |  | — | — |
| TutorialAmbushLakeLureSpottedSmoke | PASS | 49.9 |  | — | — |
| TutorialAmbushLakeEarlyKillSmoke | PASS | 50.9 |  | — | — |
| TutorialAmbushLakeMineSmoke | PASS | 60.1 |  | — | — |
| DisarmCoverSmoke | PASS | 47.1 |  | — | — |
| LowCoverMoveSmoke | PASS | 53 |  | — | — |
| LowCoverMoveLakeSmoke | PASS | 59 |  | — | — |
| LakeQuestSmoke | PASS | 60 |  | — | — |
| LakePauseOrdersSmoke | PASS | 60.4 |  | — | — |
| HordeArrowsSmoke | PASS | 53 |  | — | — |
| BarricadeVaultBackSmoke | PASS | 50.4 |  | — | — |
| HeatFearWallSmoke | PASS | 47.8 |  | — | — |
| TurnArenaLakeSmoke | PASS | 49.6 |  | — | — |
| TurnArenaYardSmoke | PASS | 48.4 |  | — | — |
| RetreatFacingSmoke | FAIL | 48.5 | [2026.11.16-22.00.17:362][516]LogCodexTactics: Display: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0) | FAIL | 38.4 |
| EnvironmentSmoke | PASS | 46 |  | — | — |
| EnvironmentLakeSmoke | PASS | 46.7 |  | — | — |
| EnvironmentRosterSmoke | PASS | 45.8 |  | — | — |
| GateExitLakeSmoke | PASS | 60.8 |  | — | — |
| GateExitLakeNoPointsSmoke | PASS | 60.6 |  | — | — |
| FrozenFactoryStartSmoke | PASS | 46.3 |  | — | — |
| DepotCampaignStartSmoke | PASS | 54.8 |  | — | — |
| WeatherSmoke | PASS | 47 |  | — | — |
| WeatherLakeSmoke | PASS | 44.1 |  | — | — |
| LadderClimbSmoke | PASS | 56.8 |  | — | — |
| LadderEnemySmoke | PASS | 54.4 |  | — | — |
| CorpseFallSmoke | PASS | 46.9 |  | — | — |
| BlizzardWalkSmoke | PASS | 46.2 |  | — | — |
| RegroupAfterLossesSmoke | PASS | 47.6 |  | — | — |
| GateExitLakeKiaSmoke | PASS | 56.7 |  | — | — |

## Failed twice

- **MilitaryZombieSmoke**: [2026.11.16-18.55.15:331][916]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 0.00, ) Rerun: [2026.11.16-18.56.53:284][891]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 1.00, anim_hit(DefaultSlot 1.00))
  History: `Source/CodexTacticsDev/Private/Debug/MilitaryZombieSmokeCommand.cpp`: 6b584fbe feat(characters): operatives, Military Zombie, Hound and Frostbitten Blueprints use the optimized single-slot bodies (user 2026-10-21); `Source/CodexTactics/Private/Characters/EnemyCharacter.cpp`: 632d61ec feat(animation): wound tiers, the full-body injured clips with the raise to fire; feat(ui): a switch per HUD label
- **OptimizedCharacterSmoke**: [2026.11.16-19.00.59:179][ 51]LogCodexTactics: Display: OptimizedCharacterSmoke FAIL: [Marksman] death: both bodies still there (original 1, optimized 1), dying, optimized anim dead 1, death clip NOT playing Rerun: [2026.11.16-19.02.53:391][497]LogCodexTactics: Display: OptimizedCharacterSmoke FAIL: [Marksman] death: both bodies still there (original 1, optimized 1), dying, optimized anim dead 1, death clip NOT playing
  History: `Source/CodexTacticsDev/Private/Debug/OptimizedCharacterSmokeCommand.cpp`: 28650298 feat(ai): PMC squads on the operative body - teams, the shared order catalog, the utility commander, the self-play arena; `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp`: 5d26c121 gemini-034: fix 034a HIGH findings (fast perception scales, hover ghost cache/yaw, leave cover pending clear)
- **CoverHugSmoke**: [2026.11.16-21.31.34:143][870]LogCodexTactics: Display: Smoke FAIL: shimmy back to the edge ordered Rerun: [2026.11.16-21.32.15:593][820]LogCodexTactics: Display: Smoke FAIL: shimmy back to the edge ordered
  History: `Source/CodexTacticsDev/Private/Debug/CoverHugSmokeCommand.cpp`: d1a6958a refactor(build): move the dev console commands into the CodexTacticsDev module; `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp`: 571894f4 fix(combat): one fire gate on every operative fire path - turn, raise, then the round (058c / 061)
- **FormationJitterSmoke**: [2026.11.16-21.38.31:138][468]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) Rerun: [2026.11.16-21.40.18:154][376]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6)
  History: `Source/CodexTacticsDev/Private/Debug/FormationJitterSmokeCommand.cpp`: c81585ae fix(animation): D-1 crouch and cover on the ABP, formation followers stop twitching; `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp`: f65f3325 gemini-034: fix stealth look-direction lock and stopping snap-back (bug #324)
- **RpgPmcSmoke**: [2026.11.16-21.41.35:234][944]LogCodexTactics: Display: Smoke FAIL: 2: Codex.Combat.LauncherAI 0 - the commander's RpgStrike order (2 orders) fires the rocket (0 strike shots; ordered rocket strike at BP_Operative_C_0) Rerun: [2026.11.16-21.43.23:352][879]LogCodexTactics: Display: Smoke FAIL: 2: Codex.Combat.LauncherAI 0 - the commander's RpgStrike order (2 orders) fires the rocket (0 strike shots; ordered rocket strike at BP_Operative_C_0)
  History: `Source/CodexTacticsDev/Private/Debug/PmcCombatBugsSmokeCommand.cpp`: 64eeac93 test(smokes): fix the 7 night-bar failures of 2026-11-10 (smoke-side, no game regression); `Source/CodexTacticsDev/Private/Debug/RpgPmcSmokeCommand.cpp`: 00eb12cc feat(weapons): the AI fires the RPG-7 - shared launcher rule, blast suppression, PMC RPG gunner; `Source/CodexTacticsDev/Private/Debug/UnderFireManoeuvreSmokeCommand.cpp`: 64eeac93 test(smokes): fix the 7 night-bar failures of 2026-11-10 (smoke-side, no game regression); `Source/CodexTactics/Private/Characters/OperativeCharacterLauncherAI.cpp`: 00eb12cc feat(weapons): the AI fires the RPG-7 - shared launcher rule, blast suppression, PMC RPG gunner
- **RetreatFacingSmoke**: [2026.11.16-22.00.17:362][516]LogCodexTactics: Display: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0) Rerun: [2026.11.16-22.02.01:395][409]LogCodexTactics: Display: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)
  History: `Source/CodexTacticsDev/Private/Debug/RetreatFacingSmokeCommand.cpp`: 77804b34 wip(codex): save retreat smoke rifle fixture before limit stop; `Source/CodexTactics/Private/Characters/OperativeCharacter.cpp`: 90b60f9f fix(mantle): batch 2 - wall guard, settled exit, walk-off descent, face double click, hover ghost

Partial sweep. Resume: run `python Saved/Orchestrator/035/sweep.py` in the agents worktree; next batch 25, 92 initial runs remaining. Machine-readable pending state is in progress.json. Latest path commits above are history references, not proven regression culprits.

Only tracked output: this report. Ignored Saved evidence is local. Existing untracked packs/config/cache remain outside this task. No source investigation beyond failure lines and path identification.
