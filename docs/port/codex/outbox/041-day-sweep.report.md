# 041 - Day sweep

Frozen base: 50e8e4a93d36a0724fd23f0853b26944fef6fa51 on codex/041-day-sweep; registry: 287 names in original order. No code or flag changes.

Build: PASS, exit 0, one build attempt for task 041. Completed 2026-11-17T12:52:36.3659921Z. Evidence: Saved/Orchestrator/041/build-result.json, build-wrapper.log, build-engine.log. Only FAIL log lines are inspected.

Router: level 2, confidence 0.77; deterministic existing sweep harness, no local model.

Progress: 72/287 initial runs; 72 final PASS; 0 final FAIL; 0 failed twice. Updated 2026-11-17T13:22:51.482151+00:00.

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

## Failed twice

None among completed rows.

Completed sweep: all 72 frozen registry names ran once initially; every initial FAIL ran exactly once alone. No pending runs. Machine-readable state and preserved logs remain in Saved/Orchestrator/041. Latest path commits above are history references, not proven regression culprits.

Only tracked output: this report. Ignored Saved evidence is local. Existing untracked packs/config/cache remain outside this task. No source investigation beyond failure lines and path identification.
