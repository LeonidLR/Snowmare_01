# 041 - Day sweep

Completed 287/287 on frozen main base `50e8e4a93d36a0724fd23f0853b26944fef6fa51`, branch `codex/041-day-sweep`.
Result: **281 PASS, 6 FAIL**. Initial: 281 PASS, 6 FAIL. Six single-test reruns: 0 recovered, 6 failed twice. Infrastructure failures: 0.

## Execution

- One build attempt, PASS (exit 0), completed 2026-11-17T12:52:36.3659921Z. No rebuild during the sweep.
- All 287 frozen smokeRegistry names ran in original order, in 36 batches of at most 8; initial Jobs=4, `-NoRetry -ExtraArgs -Multiprocess`. Every initial FAIL ran once more alone with `-Jobs 1`, including known reds.
- Engine Smoke RESULT determines PASS/FAIL, including any wrapper KNOWN-FAIL classification. No assertions, expectations, flags, source, or assets changed; no diagnosis or fixes. Only FAIL lines were read from logs.
- Registry count is 287 on this snapshot; the task's 284 denominator predates this registry. Scripts retained shared-lock waiting and designer priority.
- Reused Saved/Orchestrator/035/sweep.py via a copy in 041 with only Claude-authorized path/title/branch substitutions (chat #658). One invocation processes one batch (chat #660). The control wrapper initially stopped after 16 tests; it resumed from progress without repeating completed tests.
- Started 2026-11-17T12:48:01.604066+00:00; runner confirmed COMPLETE at 2026-11-17T15:30:56.922503+00:00.
- Router: level 2, confidence 0.77; existing deterministic harness, no local model.

## Failed twice

Latest merges below are the newest first-parent merges at or before the tested frozen base that touched each named smoke command file. These are file-history references, not inferred causes.

| Smoke | First FAIL line | Latest merge touching smoke file | Smoke file |
|---|---|---|---|
| PauseBarrelOrderSmoke | [2026.11.17-13.34.12:197][994]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first) | a48e75403 merge: one fire gate on every operative fire path - turn, raise, then fire (board 058c / 061, FireGateSmoke; pistol barrel axis, suppress keeps a fog-hidden target) | Source/CodexTacticsDev/Private/Debug/PauseBarrelOrderSmokeCommand.cpp |
| FormationJitterSmoke | [2026.11.17-13.38.59:426][758]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) | 3f66fba7a merge: fix/shotgun-crouch-abp - shotgun crouch and cover on the ABP, formation jitter fixed (torso follows forward, direction smoothing, look filter) (2026-11-09) | Source/CodexTacticsDev/Private/Debug/FormationJitterSmokeCommand.cpp |
| TutorialSmoke | [2026.11.17-13.46.12:718][685]LogCodexTactics: Display: Smoke FAIL: medkit lesson: H used his medkit (healed, one medkit fewer) | 7d35c3c47 Merge ui/formation-radial-menu: the formation radial menu on HOLD B (tap B still toggles solo / squad), 8 formations + Auto, icons, English labels (f73760af) | Source/CodexTacticsDev/Private/Debug/TutorialSmokeCommand.cpp |
| PeekMinesSmoke | [2026.11.17-13.58.10:132][568]LogCodexTactics: Display: Smoke FAIL: rev 5j: each role in cover at his own ring - the card done (20.0 s) | eb03c5132 Merge agents/peeksight: peek sight from the leaned-out eye point in a limited cone, role-specific A3 cover spots, the medic spots the lesson mines with his peek cone one by one (dd3f425, 063b863) | Source/CodexTacticsDev/Private/Debug/PeekMinesSmokeCommand.cpp |
| LevelGenRampSmoke | [2026.11.17-14.34.26:143][667]LogCodexTactics: Display: Smoke FAIL: STAIR_in_8 group (all three box-selected, sprint): STUCK after 17 s (top level z 300): &#124; Commander (leader) at X=-2191.981 Y=1368.172 feet 8, speed 0, still 4.1 s, order own -> X=-2879.000 Y=1400.000, path complete, 4 points, ends 0 cm from the goal at z 310 &#124; Engineer at X=-2388.436 Y=1346.603 feet 145, speed 15, still 10.0 s, order own -> X=-2780.683 Y=1229.314, path complete, 3 points, ends 0 cm from the goal at z 310 &#124; Medic-Sapper at X=-2511.048 Y=1355.094 feet 231, speed 22, still 6.4 s, order none/formation -> X=-2114.033 Y=1541.158, path complete, 3 points, ends 0 cm from the goal at z 310 | 498ffa321 merge: fix/night-regressions-1110 - 7 smokes follow the new rules (solo without crouch, RPG AI off where rifle rules are tested, Depot horde / PMC off on live-level smokes) (2026-11-10) | Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp |
| StealthModeSmoke | [2026.11.17-15.15.57:582][626]LogCodexTactics: Display: Smoke FAIL: actual fired round breaks the shooter's stealth | c3c34af4c merge: 034 stealth v2 (designer-approved 2026-11-15) | Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp |

## Full results

Seconds are wrapper process wall times. Every row has preserved evidence under Saved/Orchestrator/041.

| Smoke | Final | Initial seconds | First FAIL | Rerun | Rerun seconds | Initial evidence | Rerun evidence |
|---|---|---:|---|---|---:|---|---|
| HitchProbeSmoke | PASS | 40.2 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| VehicleSmoke | PASS | 65 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| VehicleCombatSmoke | PASS | 66.3 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| VehicleDamageSmoke | PASS | 61.9 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| EnemyPrioritySmoke | PASS | 58.4 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| EnemyArchetypeSmoke | PASS | 39.5 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| MilitaryZombieSmoke | PASS | 46.1 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| TacticalHeatSmoke | PASS | 42.1 |  | — | — | Saved\Orchestrator\041\batch-01\initial | — |
| MovementSmoke | PASS | 45.4 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| CameraZoneSmoke | PASS | 46.1 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| CameraStaySmoke | PASS | 47.6 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| CameraMinZoomSmoke | PASS | 44.8 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| CameraMinZoomDepotSmoke | PASS | 53 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| QuestChainSmoke | PASS | 50.3 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| CombatFlowSmoke | PASS | 45.5 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| WaveCombatSmoke | PASS | 41.2 |  | — | — | Saved\Orchestrator\041\batch-02\initial | — |
| ColdSmoke | PASS | 49.8 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| StanceSmoke | PASS | 53.1 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| OptimizedCharacterSmoke | PASS | 79.6 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| BarrelSmoke | PASS | 45.3 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| RelocationSmoke | PASS | 43.4 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| DeployableSmoke | PASS | 43.3 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| LootSmoke | PASS | 41.4 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| TurretSmoke | PASS | 36.5 |  | — | — | Saved\Orchestrator\041\batch-03\initial | — |
| TargetedShotSmoke | PASS | 42.6 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| MissionSmoke | PASS | 48.6 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| MissionGoalsSmoke | PASS | 43.8 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| MainMenuSmoke | PASS | 70.8 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| DialogueSmoke | PASS | 61.1 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| ActionBarSmoke | PASS | 52 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| BannersSmoke | PASS | 50.8 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| TurnBasedSmoke | PASS | 44.2 |  | — | — | Saved\Orchestrator\041\batch-04\initial | — |
| LevelWaveSmoke | PASS | 53.1 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| ExposedZonesSmoke | PASS | 55.6 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| TurnBasedPushSmoke | PASS | 51.9 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| TurnBasedBarricadeSmoke | PASS | 53.6 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| TurnBasedDeploySmoke | PASS | 65.6 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| WeaponSelectorSmoke | PASS | 67.2 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| GrenadeSmoke | PASS | 66 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| GuardSmoke | PASS | 65.2 |  | — | — | Saved\Orchestrator\041\batch-05\initial | — |
| InventorySmoke | PASS | 75.6 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| TransferSmoke | PASS | 76.3 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| SaveLoadSmoke | PASS | 46.4 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| SaveRoundTripSmoke | PASS | 69.7 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| PauseMenuSmoke | PASS | 54.3 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| RadiusRingSmoke | PASS | 72.1 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| FlankBreachSmoke | PASS | 70.9 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| SusaninSmoke | PASS | 70 |  | — | — | Saved\Orchestrator\041\batch-06\initial | — |
| FloatingTextSmoke | PASS | 72.2 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| SquadFireSmoke | PASS | 71.6 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| RageSmoke | PASS | 74.5 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| SquadControlSmoke | PASS | 74.9 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| AIGrenadeSmoke | PASS | 51.7 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| EnemyAISmoke | PASS | 52.9 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| CutterSmoke | PASS | 52.8 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| ClickRulesSmoke | PASS | 52.6 |  | — | — | Saved\Orchestrator\041\batch-07\initial | — |
| NarrativeSmoke | PASS | 49.2 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| HoldSphereSmoke | PASS | 50.4 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| ProgressionSmoke | PASS | 51.5 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| VictorySmoke | PASS | 54.8 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| TurnBasedCameraSmoke | PASS | 51 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| EventBusSmoke | PASS | 36.6 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| SilhouetteSmoke | PASS | 49.8 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| AttackModeSmoke | PASS | 49.1 |  | — | — | Saved\Orchestrator\041\batch-08\initial | — |
| GroupSelectSmoke | PASS | 45.9 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| GroupCoverSmoke | PASS | 54.4 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| TurnWalkSmoke | PASS | 45.9 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| TurnBlastDeathSmoke | PASS | 44.9 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| EnemyHuntSmoke | PASS | 43.1 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| VaultSmoke | PASS | 43.8 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| DepotVaultSmoke | PASS | 43.6 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| CornerExitSmoke | PASS | 53.4 |  | — | — | Saved\Orchestrator\041\batch-09\initial | — |
| BarricadeContactSmoke | PASS | 39.8 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| CombatMoveSmoke | PASS | 41.7 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| EnemyDeathSmoke | PASS | 41.2 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| TurnSelectSmoke | PASS | 42.7 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| PanicSmoke | PASS | 50.8 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| FacingSmoke | PASS | 50.1 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| MarksmanSmoke | PASS | 49.7 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| MarksmanStealthSmoke | PASS | 55.8 |  | — | — | Saved\Orchestrator\041\batch-10\initial | — |
| EnemyHitLayerSmoke | PASS | 53.5 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| RecruitDeathSmoke | PASS | 54.7 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| HitFlashOverlaySmoke | PASS | 53.4 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| BotMarksmanSmoke | PASS | 52.2 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| BarricadeTurnContactSmoke | PASS | 51.1 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| MarksmanAdvanceSmoke | PASS | 53.1 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| BotRepairSmoke | PASS | 50.2 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| PatrolSmoke | PASS | 53.7 |  | — | — | Saved\Orchestrator\041\batch-11\initial | — |
| AmbushSmoke | PASS | 48.1 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| RealTimeControlSmoke | PASS | 50.7 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| PostureSmoke | PASS | 50.1 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| RealtimeSelectSmoke | PASS | 52.4 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| MarksmanCloseShotSmoke | PASS | 48.2 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| HordeSmoke | PASS | 49.1 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| CoverSmoke | PASS | 51.6 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| AimOffsetSmoke | PASS | 45.2 |  | — | — | Saved\Orchestrator\041\batch-12\initial | — |
| CommanderModeSmoke | PASS | 59.6 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CornerEntrySmoke | PASS | 73.1 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CornerHoldSmoke | PASS | 97.1 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverCrouchSmoke | PASS | 51.5 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverMoveSmoke | PASS | 58.3 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverReverseSmoke | PASS | 46.4 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverWallSmoke | PASS | 55 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverWallMedicSmoke | PASS | 48.5 |  | — | — | Saved\Orchestrator\041\batch-13\initial | — |
| CoverHugSmoke | PASS | 51.2 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| CoverHugMedicSmoke | PASS | 50.5 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| DefendSmoke | PASS | 52.3 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| EnemyTacticsSmoke | PASS | 51.4 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| GrenadeAnimSmoke | PASS | 51.7 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| LeftHandIKSmoke | PASS | 52.6 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| PrepDeploySmoke | PASS | 47.1 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| SightSmoke | PASS | 51.3 |  | — | — | Saved\Orchestrator\041\batch-14\initial | — |
| TripwireSmoke | PASS | 56.6 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| TurnRulesSmoke | PASS | 53.7 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| BotStealthSmoke | PASS | 59.3 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| PauseBarrelOrderSmoke | FAIL | 59.7 | [2026.11.17-13.34.12:197][994]LogCodexTactics: Display: Smoke FAIL: real time: the Ctrl + click shot at barrel 5 is taken at once (fired, or turning / raising first) | FAIL | 60.4 | Saved\Orchestrator\041\batch-15\initial | Saved\Orchestrator\041\batch-15\rerun-PauseBarrelOrderSmoke |
| TurnBasedAimSmoke | PASS | 65 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| KnockdownSmoke | PASS | 45.7 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| DeathCamSmoke | PASS | 46 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| CameraZoneModeSmoke | PASS | 43.8 |  | — | — | Saved\Orchestrator\041\batch-15\initial | — |
| FrontendSmoke | PASS | 82.2 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| SniperSmoke | PASS | 83.2 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| ShotgunSmoke | PASS | 63.4 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| ShotgunCoverSmoke | PASS | 58.8 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| FormationJitterSmoke | FAIL | 91.9 | [2026.11.17-13.38.59:426][758]LogCodexTactics: Display: Smoke FAIL: the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6) | FAIL | 78.2 | Saved\Orchestrator\041\batch-16\initial | Saved\Orchestrator\041\batch-16\rerun-FormationJitterSmoke |
| PistolSmoke | PASS | 56.5 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| ProjectileSmoke | PASS | 56.3 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| RpgSmoke | PASS | 54.5 |  | — | — | Saved\Orchestrator\041\batch-16\initial | — |
| RpgAnimSmoke | PASS | 76.3 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| RpgAiSmoke | PASS | 72.3 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| RpgPmcSmoke | PASS | 66.4 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| MortarSmoke | PASS | 67.7 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| HordeIntroSmoke | PASS | 121.5 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| SoloRejoinSmoke | PASS | 103.8 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| CombatFormationSmoke | PASS | 93.6 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| PatrolAmbushSmoke | PASS | 86 |  | — | — | Saved\Orchestrator\041\batch-17\initial | — |
| VisionConeSmoke | PASS | 64.6 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| GateGeneratorSmoke | PASS | 83.5 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| TutorialSmoke | FAIL | 249.4 | [2026.11.17-13.46.12:718][685]LogCodexTactics: Display: Smoke FAIL: medkit lesson: H used his medkit (healed, one medkit fewer) | FAIL | 254.3 | Saved\Orchestrator\041\batch-18\initial | Saved\Orchestrator\041\batch-18\rerun-TutorialSmoke |
| TutorialSaveSmoke | PASS | 126.8 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| ControlsScreenSmoke | PASS | 62.7 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| InteriorExitSmoke | PASS | 57.6 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| PMCTripwireSmoke | PASS | 71.1 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| CoverEventSmoke | PASS | 51.8 |  | — | — | Saved\Orchestrator\041\batch-18\initial | — |
| WarmIdleSmoke | PASS | 81.4 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| BridgeFenceSmoke | PASS | 66.7 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| TrapSoloSmoke | PASS | 65.5 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| PeekMinesSmoke | FAIL | 70.7 | [2026.11.17-13.58.10:132][568]LogCodexTactics: Display: Smoke FAIL: rev 5j: each role in cover at his own ring - the card done (20.0 s) | FAIL | 56.5 | Saved\Orchestrator\041\batch-19\initial | Saved\Orchestrator\041\batch-19\rerun-PeekMinesSmoke |
| RoofCutawaySmoke | PASS | 82.7 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| StealthLureSmoke | PASS | 96.9 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| KnockdownCoverSmoke | PASS | 79.8 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| HoundScentSmoke | PASS | 81 |  | — | — | Saved\Orchestrator\041\batch-19\initial | — |
| MinimapSmoke | PASS | 65.9 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| QuickFireSmoke | PASS | 62.4 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| SuppressionSmoke | PASS | 67.4 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| PeekSightSmoke | PASS | 63.2 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| LeanPoseSmoke | PASS | 63.6 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| CoverSwitchSmoke | PASS | 65 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| CoverWrapSmoke | PASS | 57.3 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| CoverSwitchLakeSmoke | PASS | 52.2 |  | — | — | Saved\Orchestrator\041\batch-20\initial | — |
| SaveCoverageSmoke | PASS | 49.7 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| SaveCoverageLakeSmoke | PASS | 52.5 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| TutorialAmbushLakeSmoke | PASS | 64.3 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| TutorialAmbushLakeSpottedSmoke | PASS | 63.4 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| TutorialAmbushLakeLureSpottedSmoke | PASS | 51.5 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| TutorialAmbushLakeEarlyKillSmoke | PASS | 51 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| TutorialAmbushLakeMineSmoke | PASS | 60.1 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| DisarmCoverSmoke | PASS | 45 |  | — | — | Saved\Orchestrator\041\batch-21\initial | — |
| LowCoverMoveSmoke | PASS | 52.3 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| LowCoverMoveLakeSmoke | PASS | 51.3 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| LakeQuestSmoke | PASS | 50.2 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| LakePauseOrdersSmoke | PASS | 52.8 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| HordeArrowsSmoke | PASS | 48.6 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| BarricadeVaultBackSmoke | PASS | 45.1 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| HeatFearWallSmoke | PASS | 42.2 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| TurnArenaLakeSmoke | PASS | 46.3 |  | — | — | Saved\Orchestrator\041\batch-22\initial | — |
| TurnArenaYardSmoke | PASS | 50.8 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| RetreatFacingSmoke | PASS | 45.6 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| EnvironmentSmoke | PASS | 42.1 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| EnvironmentLakeSmoke | PASS | 46.2 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| EnvironmentRosterSmoke | PASS | 43 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| GateExitLakeSmoke | PASS | 65.1 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| GateExitLakeNoPointsSmoke | PASS | 62.9 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| FrozenFactoryStartSmoke | PASS | 43.4 |  | — | — | Saved\Orchestrator\041\batch-23\initial | — |
| DepotCampaignStartSmoke | PASS | 61.4 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| WeatherSmoke | PASS | 47.8 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| WeatherLakeSmoke | PASS | 47.3 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| LadderClimbSmoke | PASS | 70.7 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| LadderEnemySmoke | PASS | 60.1 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| CorpseFallSmoke | PASS | 51.7 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| BlizzardWalkSmoke | PASS | 47.6 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| RegroupAfterLossesSmoke | PASS | 47.2 |  | — | — | Saved\Orchestrator\041\batch-24\initial | — |
| GateExitLakeKiaSmoke | PASS | 55.7 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| GateExitLakeKiaNoPointsSmoke | PASS | 56.2 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| GroupCoverPrioritySmoke | PASS | 41.9 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| LadderQueueSmoke | PASS | 44.6 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| SnowTracksSmoke | PASS | 88.8 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| SnowIndoorSmoke | PASS | 90.7 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| StartLightingSmoke | PASS | 56.6 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| StartSnowSmoke | PASS | 75 |  | — | — | Saved\Orchestrator\041\batch-25\initial | — |
| FormationLabSmoke | PASS | 334.9 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationMenuSmoke | PASS | 42.7 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationCustomSmoke | PASS | 79.3 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationShapesSmoke | PASS | 44.2 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationWallGroupSmoke | PASS | 123.9 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationSlideSmoke | PASS | 103.2 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationBypassSmoke | PASS | 84 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationFarSideSmoke | PASS | 57.4 |  | — | — | Saved\Orchestrator\041\batch-26\initial | — |
| FormationCornerPeekSmoke | PASS | 57.9 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FormationWallSwitchSmoke | PASS | 112.4 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FormationSquadPathPreviewSmoke | PASS | 56.8 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FormationLeaderSwapSlideSmoke | PASS | 110.6 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FormationSquadPeekSmoke | PASS | 98 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FormationWallHugSmoke | PASS | 71.2 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| FallenGearSmoke | PASS | 55.2 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| ClimateSmoke | PASS | 53.1 |  | — | — | Saved\Orchestrator\041\batch-27\initial | — |
| ClimateArcticSmoke | PASS | 62.6 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| RainSmoke | PASS | 82.8 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| StartFlickerSmoke | PASS | 127.1 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| SquadCornerPeekSmoke | PASS | 81.3 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| FormationLabCombatSmoke | PASS | 87.6 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| CutterWallSmoke | PASS | 65.2 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| CombatLabTowerSmoke | PASS | 69.5 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| CombatLabAmbushSmoke | PASS | 64.6 |  | — | — | Saved\Orchestrator\041\batch-28\initial | — |
| SquadPeekNextWallSmoke | PASS | 61.4 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| CameraFreePanSmoke | PASS | 56.2 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| TowerVisionSmoke | PASS | 52.1 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| VisionHeightSmoke | PASS | 52.5 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| WallLeadershipSmoke | PASS | 137.3 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| CombatLabAmbushRelocateSmoke | PASS | 171.4 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| SquadHaltStanceSmoke | PASS | 155.7 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| LevelGenRouteSmoke | PASS | 223.8 |  | — | — | Saved\Orchestrator\041\batch-29\initial | — |
| LevelGenLaddersSmoke | PASS | 82.4 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| LevelGenOpeningsSmoke | PASS | 56.3 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| LevelGenRampSmoke | FAIL | 83.6 | [2026.11.17-14.34.26:143][667]LogCodexTactics: Display: Smoke FAIL: STAIR_in_8 group (all three box-selected, sprint): STUCK after 17 s (top level z 300): &#124; Commander (leader) at X=-2191.981 Y=1368.172 feet 8, speed 0, still 4.1 s, order own -> X=-2879.000 Y=1400.000, path complete, 4 points, ends 0 cm from the goal at z 310 &#124; Engineer at X=-2388.436 Y=1346.603 feet 145, speed 15, still 10.0 s, order own -> X=-2780.683 Y=1229.314, path complete, 3 points, ends 0 cm from the goal at z 310 &#124; Medic-Sapper at X=-2511.048 Y=1355.094 feet 231, speed 22, still 6.4 s, order none/formation -> X=-2114.033 Y=1541.158, path complete, 3 points, ends 0 cm from the goal at z 310 | FAIL | 75.9 | Saved\Orchestrator\041\batch-30\initial | Saved\Orchestrator\041\batch-30\rerun-LevelGenRampSmoke |
| RoofCutawayFocusSmoke | PASS | 52.7 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| FallDropSmoke | PASS | 62.5 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| DropSoftlockSmoke | PASS | 76 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| DropLinkReachSmoke | PASS | 45.5 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| WallSectorsSmoke | PASS | 46.8 |  | — | — | Saved\Orchestrator\041\batch-30\initial | — |
| GroupCoverNearbySmoke | PASS | 50.3 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| WallLeadershipSlideSmoke | PASS | 57.7 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| GroupCornerPeekSmoke | PASS | 54.4 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| RunCornerPeekSmoke | PASS | 65.9 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| TwoCornerHoldSmoke | PASS | 126.7 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| WallMoveNearestLeadsSmoke | PASS | 141.1 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| PmcTeamSmoke | PASS | 103.2 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| PmcCommanderSmoke | PASS | 124 |  | — | — | Saved\Orchestrator\041\batch-31\initial | — |
| PmcArenaSmoke | PASS | 80.2 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| PmcArenaObjectiveSmoke | PASS | 58.6 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| PmcStalemateSmoke | PASS | 79.4 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| PmcEncounterSmoke | PASS | 91.8 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| PmcEncounterHuntSmoke | PASS | 82.9 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| PmcStandoffSmoke | PASS | 58.5 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| SightConditionsSmoke | PASS | 54 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| ConePerfSmoke | PASS | 80.2 |  | — | — | Saved\Orchestrator\041\batch-32\initial | — |
| WeaponReachSmoke | PASS | 62.6 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| PmcDeathPoseSmoke | PASS | 59.3 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| FireArcSmoke | PASS | 59.8 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| PmcUnderFireSmoke | PASS | 55.9 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| UnderFireManoeuvreSmoke | PASS | 66.5 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| SoloStanceSmoke | PASS | 57.7 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| LeaderSwitchMoveSmoke | PASS | 57.3 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| LeaderSwitchLadderSmoke | PASS | 58.5 |  | — | — | Saved\Orchestrator\041\batch-33\initial | — |
| PmcFireLineSmoke | PASS | 63.8 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| CornerStandoffSmoke | PASS | 60.1 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| HordeFirefightsSmoke | PASS | 86.4 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| DeathRagdollSmoke | PASS | 55.9 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| CorpsePersistSmoke | PASS | 61.8 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| CorpseCostSmoke | PASS | 147.1 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| ProneCrawlSmoke | PASS | 73.2 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| InjuredLocomotionSmoke | PASS | 88.8 |  | — | — | Saved\Orchestrator\041\batch-34\initial | — |
| StealthModeSmoke | FAIL | 69.9 | [2026.11.17-15.15.57:582][626]LogCodexTactics: Display: Smoke FAIL: actual fired round breaks the shooter's stealth | FAIL | 78.8 | Saved\Orchestrator\041\batch-35\initial | Saved\Orchestrator\041\batch-35\rerun-StealthModeSmoke |
| StealthDetectionSmoke | PASS | 46.5 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| SoloStartSmoke | PASS | 53.5 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| FastRegroupSmoke | PASS | 53.1 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| InjuredLocomotionDepotSmoke | PASS | 70.6 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| WoundedFollowerSmoke | PASS | 86.4 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| LookHoldSmoke | PASS | 56.3 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| MantleSmoke | PASS | 60 |  | — | — | Saved\Orchestrator\041\batch-35\initial | — |
| MantleAlignSmoke | PASS | 605.5 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| MantleDescentSmoke | PASS | 604.6 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| MantleClickSmoke | PASS | 603.5 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| MantleFenceRouteSmoke | PASS | 76 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| FireGateSmoke | PASS | 89 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| FirstAidSmoke | PASS | 89.4 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |
| CornerPickSmoke | PASS | 90.7 |  | — | — | Saved\Orchestrator\041\batch-36\initial | — |

## Evidence and boundaries

- Build: Saved/Orchestrator/041/build-result.json, build-wrapper.log, build-engine.log.
- Frozen run and progress: Saved/Orchestrator/041/run.json, registry.json, progress.json; batch-01 through batch-36 preserve initial and rerun evidence.
- Runner provenance: Saved/Orchestrator/041/runner-configuration.json; completion: watch-result.json; merge metadata: failed-merge-metadata.json.
- Scripts/test_map.json SHA256: 14347e7e33744294760f12be52e049fcbf54ae0d65dd68312a51b4f88edc1e35.
- Original 035 runner, registry, progress, and report hashes verified unchanged against 035-preservation-original.json.
- Progress chat lines delivered at 64 (#663), 128 (#665), 192 (#671), 256 (#675).
- Source, Scripts, Config and Content tracked diffs are empty. Existing untracked assets/config/cache files were left untouched. Only this report is committed for 041; no push or merge into main.
- Partial report checkpoints: 40037d38 (72 tests), 4185b415 (192 tests).
