# 041 - Day sweep

Frozen base: 50e8e4a93d36a0724fd23f0853b26944fef6fa51 on codex/041-day-sweep; registry: 287 names in original order. No code or flag changes.

Build: PASS, exit 0, one build attempt for task 041. Completed 2026-11-17T12:52:36.3659921Z. Evidence: Saved/Orchestrator/041/build-result.json, build-wrapper.log, build-engine.log. Only FAIL log lines are inspected.

Router: level 2, confidence 0.77; deterministic existing sweep harness, no local model.

Progress: 192/287 initial runs; 188 final PASS; 4 final FAIL; 4 failed twice. Updated 2026-11-17T14:13:03.808653+00:00.

Initial batches <=8, script default Jobs=4 (16 CPUs), `-NoRetry -ExtraArgs -Multiprocess`; each initial FAIL rerun exactly once alone with `-Jobs 1`. Engine `Smoke RESULT` controls PASS/FAIL, including wrapper KNOWN-FAIL. Seconds are wrapper process wall times. Missing result/timeout is infrastructure FAIL, never an invented assertion. Shared locks and designer priority remain active.

Frozen registry and resumable progress: `Saved/Orchestrator/041/registry.json`, `progress.json`, `sweep.py`. Each batch has preserved wrapper output/result JSON and copied engine logs before reruns. Unrun names remain in frozen registry.

| Smoke | Final | Initial seconds | First FAIL / infrastructure | Rerun | Rerun seconds |
|---|---|---:|---|---|---:|
| HitchProbeSmoke | PASS | 40.2 |  | — | — |
| VehicleSmoke | PASS | 65 |  | — | — |
| VehicleCombatSmoke | PASS | 66.3 |  | — | — |
| VehicleDamageSmoke | PASS | 61.9 |  | — | — |
| EnemyPrioritySmoke | PASS | 58.4 |  | — | — |
| EnemyArchetypeSmoke | PASS | 39.5 |  | — | — |
| MilitaryZombieSmoke | PASS | 46.1 |  | — | — |
| TacticalHeatSmoke | PASS | 42.1 |  | — | — |
| MovementSmoke | PASS | 45.4 |  | — | — |
| CameraZoneSmoke | PASS | 46.1 |  | — | — |
| CameraStaySmoke | PASS | 47.6 |  | — | — |
| CameraMinZoomSmoke | PASS | 44.8 |  | — | — |
| CameraMinZoomDepotSmoke | PASS | 53 |  | — | — |
| QuestChainSmoke | PASS | 50.3 |  | — | — |
| CombatFlowSmoke | PASS | 45.5 |  | — | — |
| WaveCombatSmoke | PASS | 41.2 |  | — | — |
| ColdSmoke | PASS | 49.8 |  | — | — |
| StanceSmoke | PASS | 53.1 |  | — | — |
| OptimizedCharacterSmoke | PASS | 79.6 |  | — | — |
| BarrelSmoke | PASS | 45.3 |  | — | — |
| RelocationSmoke | PASS | 43.4 |  | — | — |
| DeployableSmoke | PASS | 43.3 |  | — | — |
| LootSmoke | PASS | 41.4 |  | — | — |
| TurretSmoke | PASS | 36.5 |  | — | — |
| TargetedShotSmoke | PASS | 42.6 |  | — | — |
| MissionSmoke | PASS | 48.6 |  | — | — |
| MissionGoalsSmoke | PASS | 43.8 |  | — | — |
| MainMenuSmoke | PASS | 70.8 |  | — | — |
| DialogueSmoke | PASS | 61.1 |  | — | — |
| ActionBarSmoke | PASS | 52 |  | — | — |
| BannersSmoke | PASS | 50.8 |  | — | — |
| TurnBasedSmoke | PASS | 44.2 |  | — | — |
| LevelWaveSmoke | PASS | 53.1 |  | — | — |
| ExposedZonesSmoke | PASS | 55.6 |  | — | — |
| TurnBasedPushSmoke | PASS | 51.9 |  | — | — |
| TurnBasedBarricadeSmoke | PASS | 53.6 |  | — | — |
| TurnBasedDeploySmoke | PASS | 65.6 |  | — | — |
| WeaponSelectorSmoke | PASS | 67.2 |  | — | — |
| GrenadeSmoke | PASS | 66 |  | — | — |
| GuardSmoke | PASS | 65.2 |  | — | — |
| InventorySmoke | PASS | 75.6 |  | — | — |
| TransferSmoke | PASS | 76.3 |  | — | — |
| SaveLoadSmoke | PASS | 46.4 |  | — | — |
| SaveRoundTripSmoke | PASS | 69.7 |  | — | — |
| PauseMenuSmoke | PASS | 54.3 |  | — | — |
| RadiusRingSmoke | PASS | 72.1 |  | — | — |
| FlankBreachSmoke | PASS | 70.9 |  | — | — |
| SusaninSmoke | PASS | 70 |  | — | — |
| FloatingTextSmoke | PASS | 72.2 |  | — | — |
| SquadFireSmoke | PASS | 71.6 |  | — | — |
| RageSmoke | PASS | 74.5 |  | — | — |
| SquadControlSmoke | PASS | 74.9 |  | — | — |
| AIGrenadeSmoke | PASS | 51.7 |  | — | — |
| EnemyAISmoke | PASS | 52.9 |  | — | — |
| CutterSmoke | PASS | 52.8 |  | — | — |
| ClickRulesSmoke | PASS | 52.6 |  | — | — |
| NarrativeSmoke | PASS | 49.2 |  | — | — |
| HoldSphereSmoke | PASS | 50.4 |  | — | — |
| ProgressionSmoke | PASS | 51.5 |  | — | — |
| VictorySmoke | PASS | 54.8 |  | — | — |
| TurnBasedCameraSmoke | PASS | 51 |  | — | — |
| EventBusSmoke | PASS | 36.6 |  | — | — |
| SilhouetteSmoke | PASS | 49.8 |  | — | — |
| AttackModeSmoke | PASS | 49.1 |  | — | — |
| GroupSelectSmoke | PASS | 45.9 |  | — | — |
| GroupCoverSmoke | PASS | 54.4 |  | — | — |
| TurnWalkSmoke | PASS | 45.9 |  | — | — |
| TurnBlastDeathSmoke | PASS | 44.9 |  | — | — |
| EnemyHuntSmoke | PASS | 43.1 |  | — | — |
| VaultSmoke | PASS | 43.8 |  | — | — |
| DepotVaultSmoke | PASS | 43.6 |  | — | — |
| CornerExitSmoke | PASS | 53.4 |  | — | — |
| BarricadeContactSmoke | PASS | 39.8 |  | — | — |
| CombatMoveSmoke | PASS | 41.7 |  | — | — |
| EnemyDeathSmoke | PASS | 41.2 |  | — | — |
| TurnSelectSmoke | PASS | 42.7 |  | — | — |
| PanicSmoke | PASS | 50.8 |  | — | — |
| FacingSmoke | PASS | 50.1 |  | — | — |
| MarksmanSmoke | PASS | 49.7 |  | — | — |
| MarksmanStealthSmoke | PASS | 55.8 |  | — | — |
| EnemyHitLayerSmoke | PASS | 53.5 |  | — | — |
| RecruitDeathSmoke | PASS | 54.7 |  | — | — |
| HitFlashOverlaySmoke | PASS | 53.4 |  | — | — |
| BotMarksmanSmoke | PASS | 52.2 |  | — | — |
| BarricadeTurnContactSmoke | PASS | 51.1 |  | — | — |
| MarksmanAdvanceSmoke | PASS | 53.1 |  | — | — |
| BotRepairSmoke | PASS | 50.2 |  | — | — |
| PatrolSmoke | PASS | 53.7 |  | — | — |
| AmbushSmoke | PASS | 48.1 |  | — | — |
| RealTimeControlSmoke | PASS | 50.7 |  | — | — |
| PostureSmoke | PASS | 50.1 |  | — | — |
| RealtimeSelectSmoke | PASS | 52.4 |  | — | — |
| MarksmanCloseShotSmoke | PASS | 48.2 |  | — | — |
| HordeSmoke | PASS | 49.1 |  | — | — |
| CoverSmoke | PASS | 51.6 |  | — | — |
| AimOffsetSmoke | PASS | 45.2 |  | — | — |
| CommanderModeSmoke | PASS | 59.6 |  | — | — |
| CornerEntrySmoke | PASS | 73.1 |  | — | — |
| CornerHoldSmoke | PASS | 97.1 |  | — | — |
| CoverCrouchSmoke | PASS | 51.5 |  | — | — |
| CoverMoveSmoke | PASS | 58.3 |  | — | — |
| CoverReverseSmoke | PASS | 46.4 |  | — | — |
| CoverWallSmoke | PASS | 55 |  | — | — |
| CoverWallMedicSmoke | PASS | 48.5 |  | — | — |
| CoverHugSmoke | PASS | 51.2 |  | — | — |
| CoverHugMedicSmoke | PASS | 50.5 |  | — | — |
| DefendSmoke | PASS | 52.3 |  | — | — |
| EnemyTacticsSmoke | PASS | 51.4 |  | — | — |
| GrenadeAnimSmoke | PASS | 51.7 |  | — | — |
| LeftHandIKSmoke | PASS | 52.6 |  | — | — |
| PrepDeploySmoke | PASS | 47.1 |  | — | — |
| SightSmoke | PASS | 51.3 |  | — | — |
| TripwireSmoke | PASS | 56.6 |  | — | — |
| TurnRulesSmoke | PASS | 53.7 |  | — | — |
| BotStealthSmoke | PASS | 59.3 |  | — | — |
| PauseBarrelOrderSmoke | FAIL | 59.7 | [2026.11.17-13.34.12:197][994]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first) | FAIL | 60.4 |
| TurnBasedAimSmoke | PASS | 65 |  | — | — |
| KnockdownSmoke | PASS | 45.7 |  | — | — |
| DeathCamSmoke | PASS | 46 |  | — | — |
| CameraZoneModeSmoke | PASS | 43.8 |  | — | — |
| FrontendSmoke | PASS | 82.2 |  | — | — |
| SniperSmoke | PASS | 83.2 |  | — | — |
| ShotgunSmoke | PASS | 63.4 |  | — | — |
| ShotgunCoverSmoke | PASS | 58.8 |  | — | — |
| FormationJitterSmoke | FAIL | 91.9 | [2026.11.17-13.38.59:426][758]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) | FAIL | 78.2 |
| PistolSmoke | PASS | 56.5 |  | — | — |
| ProjectileSmoke | PASS | 56.3 |  | — | — |
| RpgSmoke | PASS | 54.5 |  | — | — |
| RpgAnimSmoke | PASS | 76.3 |  | — | — |
| RpgAiSmoke | PASS | 72.3 |  | — | — |
| RpgPmcSmoke | PASS | 66.4 |  | — | — |
| MortarSmoke | PASS | 67.7 |  | — | — |
| HordeIntroSmoke | PASS | 121.5 |  | — | — |
| SoloRejoinSmoke | PASS | 103.8 |  | — | — |
| CombatFormationSmoke | PASS | 93.6 |  | — | — |
| PatrolAmbushSmoke | PASS | 86 |  | — | — |
| VisionConeSmoke | PASS | 64.6 |  | — | — |
| GateGeneratorSmoke | PASS | 83.5 |  | — | — |
| TutorialSmoke | FAIL | 249.4 | [2026.11.17-13.46.12:718][685]LogCodexTactics: Display: Smoke FAIL: medkit lesson: H used his medkit (healed, one medkit fewer) | FAIL | 254.3 |
| TutorialSaveSmoke | PASS | 126.8 |  | — | — |
| ControlsScreenSmoke | PASS | 62.7 |  | — | — |
| InteriorExitSmoke | PASS | 57.6 |  | — | — |
| PMCTripwireSmoke | PASS | 71.1 |  | — | — |
| CoverEventSmoke | PASS | 51.8 |  | — | — |
| WarmIdleSmoke | PASS | 81.4 |  | — | — |
| BridgeFenceSmoke | PASS | 66.7 |  | — | — |
| TrapSoloSmoke | PASS | 65.5 |  | — | — |
| PeekMinesSmoke | FAIL | 70.7 | [2026.11.17-13.58.10:132][568]LogCodexTactics: Display: Smoke FAIL: rev 5j: each role in cover at his own ring - the card done (20.0 s) | FAIL | 56.5 |
| RoofCutawaySmoke | PASS | 82.7 |  | — | — |
| StealthLureSmoke | PASS | 96.9 |  | — | — |
| KnockdownCoverSmoke | PASS | 79.8 |  | — | — |
| HoundScentSmoke | PASS | 81 |  | — | — |
| MinimapSmoke | PASS | 65.9 |  | — | — |
| QuickFireSmoke | PASS | 62.4 |  | — | — |
| SuppressionSmoke | PASS | 67.4 |  | — | — |
| PeekSightSmoke | PASS | 63.2 |  | — | — |
| LeanPoseSmoke | PASS | 63.6 |  | — | — |
| CoverSwitchSmoke | PASS | 65 |  | — | — |
| CoverWrapSmoke | PASS | 57.3 |  | — | — |
| CoverSwitchLakeSmoke | PASS | 52.2 |  | — | — |
| SaveCoverageSmoke | PASS | 49.7 |  | — | — |
| SaveCoverageLakeSmoke | PASS | 52.5 |  | — | — |
| TutorialAmbushLakeSmoke | PASS | 64.3 |  | — | — |
| TutorialAmbushLakeSpottedSmoke | PASS | 63.4 |  | — | — |
| TutorialAmbushLakeLureSpottedSmoke | PASS | 51.5 |  | — | — |
| TutorialAmbushLakeEarlyKillSmoke | PASS | 51 |  | — | — |
| TutorialAmbushLakeMineSmoke | PASS | 60.1 |  | — | — |
| DisarmCoverSmoke | PASS | 45 |  | — | — |
| LowCoverMoveSmoke | PASS | 52.3 |  | — | — |
| LowCoverMoveLakeSmoke | PASS | 51.3 |  | — | — |
| LakeQuestSmoke | PASS | 50.2 |  | — | — |
| LakePauseOrdersSmoke | PASS | 52.8 |  | — | — |
| HordeArrowsSmoke | PASS | 48.6 |  | — | — |
| BarricadeVaultBackSmoke | PASS | 45.1 |  | — | — |
| HeatFearWallSmoke | PASS | 42.2 |  | — | — |
| TurnArenaLakeSmoke | PASS | 46.3 |  | — | — |
| TurnArenaYardSmoke | PASS | 50.8 |  | — | — |
| RetreatFacingSmoke | PASS | 45.6 |  | — | — |
| EnvironmentSmoke | PASS | 42.1 |  | — | — |
| EnvironmentLakeSmoke | PASS | 46.2 |  | — | — |
| EnvironmentRosterSmoke | PASS | 43 |  | — | — |
| GateExitLakeSmoke | PASS | 65.1 |  | — | — |
| GateExitLakeNoPointsSmoke | PASS | 62.9 |  | — | — |
| FrozenFactoryStartSmoke | PASS | 43.4 |  | — | — |
| DepotCampaignStartSmoke | PASS | 61.4 |  | — | — |
| WeatherSmoke | PASS | 47.8 |  | — | — |
| WeatherLakeSmoke | PASS | 47.3 |  | — | — |
| LadderClimbSmoke | PASS | 70.7 |  | — | — |
| LadderEnemySmoke | PASS | 60.1 |  | — | — |
| CorpseFallSmoke | PASS | 51.7 |  | — | — |
| BlizzardWalkSmoke | PASS | 47.6 |  | — | — |
| RegroupAfterLossesSmoke | PASS | 47.2 |  | — | — |

## Failed twice

- **PauseBarrelOrderSmoke**: [2026.11.17-13.34.12:197][994]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first) Rerun: [2026.11.17-13.36.12:444][140]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first)
  History: `Source/CodexTacticsDev/Private/Debug/PauseBarrelOrderSmokeCommand.cpp`: adae4e5b3 test(combat): FireGateSmoke (wounded crouched / in cover, quick fire + suppress from cover, object shot behind him) and the targeted-shot smokes wait for the gate (058c / 061)
- **FormationJitterSmoke**: [2026.11.17-13.38.59:426][758]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) Rerun: [2026.11.17-13.40.19:933][533]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6)
  History: `Source/CodexTacticsDev/Private/Debug/FormationJitterSmokeCommand.cpp`: c81585ae5 fix(animation): D-1 crouch and cover on the ABP, formation followers stop twitching; `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp`: 0e9c49e7d feat(formation): the leader takes the corner himself - no follower watch or place at his corner (F-2, option B)
- **TutorialSmoke**: [2026.11.17-13.46.12:718][685]LogCodexTactics: Display: Smoke FAIL: medkit lesson: H used his medkit (healed, one medkit fewer) Rerun: [2026.11.17-13.54.58:642][656]LogCodexTactics: Display: Smoke FAIL: medkit lesson: H used his medkit (healed, one medkit fewer)
  History: `Source/CodexTacticsDev/Private/Debug/TutorialSmokeCommand.cpp`: f73760af4 feat(ui): the formation radial menu on hold B (formation stage 7); `Source/CodexTactics/Private/Tutorial/TutorialMarkerActor.cpp`: 483126dbf feat(tutorial,ui): hologram markers + lock_in_place at the hut door
- **PeekMinesSmoke**: [2026.11.17-13.58.10:132][568]LogCodexTactics: Display: Smoke FAIL: rev 5j: each role in cover at his own ring - the card done (20.0 s) Rerun: [2026.11.17-14.00.44:152][490]LogCodexTactics: Display: Smoke FAIL: rev 5j: each role in cover at his own ring - the card done (20.0 s)
  History: `Source/CodexTacticsDev/Private/Debug/PeekMinesSmokeCommand.cpp`: 063b863c8 feat(tutorial): role-specific A3 cover spots; the Medic-Sapper's peek spots the mines physically, one by one

Completed sweep: all 192 frozen registry names ran once initially; every initial FAIL ran exactly once alone. No pending runs. Machine-readable state and preserved logs remain in Saved/Orchestrator/041. Latest path commits above are history references, not proven regression culprits.

Only tracked output: this report. Ignored Saved evidence is local. Existing untracked packs/config/cache remain outside this task. No source investigation beyond failure lines and path identification.
