# 035 - Nightly smoke sweep

Frozen base: `aba810f1e5d29a6268cf8b88f58801aa9a6c94ab` on `codex/035-nightly-sweep`; registry: 284 names in original order. Includes main 034/036. No code or flag changes.

Build: PASS (exit 0; UBT 2.18 s). Evidence: `Saved/Orchestrator/035/build-wrapper.log`, `build-engine.log`.

Router: 2.62 | gpt-6.1-sol | pass (execution/reporting; no local model).

Progress: 8/284 initial runs; 7 final PASS; 1 final FAIL; 1 failed twice. Updated 2026-11-16T18:56:56.850138+00:00.

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

## Failed twice

- **MilitaryZombieSmoke**: [2026.11.16-18.55.15:331][916]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 0.00, ) Rerun: [2026.11.16-18.56.53:284][891]LogCodexTactics: Display: Smoke FAIL: died standing: death clip held (one-shot weight 1.00, anim_hit(DefaultSlot 1.00))
  History: `Source/CodexTacticsDev/Private/Debug/MilitaryZombieSmokeCommand.cpp`: 6b584fbe feat(characters): operatives, Military Zombie, Hound and Frostbitten Blueprints use the optimized single-slot bodies (user 2026-10-21); `Source/CodexTacticsTests/Private/Characters/MilitaryZombieAssetTest.cpp`: 6b584fbe feat(characters): operatives, Military Zombie, Hound and Frostbitten Blueprints use the optimized single-slot bodies (user 2026-10-21)

Only tracked output: this report. Ignored Saved evidence is local. Existing untracked packs/config/cache remain outside this task. No source investigation beyond failure lines and path identification.
