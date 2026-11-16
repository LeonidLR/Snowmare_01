# 035 - Nightly smoke sweep

Frozen base: `aba810f1e5d29a6268cf8b88f58801aa9a6c94ab` on `codex/035-nightly-sweep`; registry: 284 names in original order. Includes main 034/036. No code or flag changes.

Build: PASS (exit 0; UBT 2.18 s). Evidence: `Saved/Orchestrator/035/build-wrapper.log`, `build-engine.log`.

Router: level 2, confidence 0.62 | gpt-6.1-sol | pass (execution/reporting; no local model).

Progress: 56/284 initial runs; 54 final PASS; 2 final FAIL; 2 failed twice. Updated 2026-11-16T19:15:08.280289+00:00.

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

## Failed twice

- **MilitaryZombieSmoke**: [2026.11.16-18.55.15:331][916]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 0.00, ) Rerun: [2026.11.16-18.56.53:284][891]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 1.00, anim_hit(DefaultSlot 1.00))
  History: `Source/CodexTacticsDev/Private/Debug/MilitaryZombieSmokeCommand.cpp`: 6b584fbe feat(characters): operatives, Military Zombie, Hound and Frostbitten Blueprints use the optimized single-slot bodies (user 2026-10-21); `Source/CodexTactics/Private/Characters/EnemyCharacter.cpp`: 632d61ec feat(animation): wound tiers, the full-body injured clips with the raise to fire; feat(ui): a switch per HUD label
- **OptimizedCharacterSmoke**: [2026.11.16-19.00.59:179][ 51]LogCodexTactics: Display: OptimizedCharacterSmoke FAIL: [Marksman] death: both bodies still there (original 1, optimized 1), dying, optimized anim dead 1, death clip NOT playing Rerun: [2026.11.16-19.02.53:391][497]LogCodexTactics: Display: OptimizedCharacterSmoke FAIL: [Marksman] death: both bodies still there (original 1, optimized 1), dying, optimized anim dead 1, death clip NOT playing
  History: `Source/CodexTacticsDev/Private/Debug/OptimizedCharacterSmokeCommand.cpp`: 28650298 feat(ai): PMC squads on the operative body - teams, the shared order catalog, the utility commander, the self-play arena; `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp`: 5d26c121 gemini-034: fix 034a HIGH findings (fast perception scales, hover ghost cache/yaw, leave cover pending clear)

Partial sweep. Resume: run `python Saved/Orchestrator/035/sweep.py` in the agents worktree; next batch 8, 228 initial runs remaining. Machine-readable pending state is in progress.json. Latest path commits above are history references, not proven regression culprits.

Only tracked output: this report. Ignored Saved evidence is local. Existing untracked packs/config/cache remain outside this task. No source investigation beyond failure lines and path identification.
