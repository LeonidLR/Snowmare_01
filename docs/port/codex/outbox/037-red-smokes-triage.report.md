# 037 red smokes triage

Status: IN PROGRESS. Step 0 green; MilitaryZombieSmoke targeted fix passes, wider related gate has CorpsePersistSmoke FAIL. OptimizedCharacterSmoke is next. 2026-11-17.

## Scope and baseline

- Worktree: `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-agents`.
- Branch: `codex/037-red-smokes`; baseline HEAD `01efefe9b8ea60c3c2210229945f06eac0972098` (assigned latest baseline containing Claude 074/075 and Codex 036).
- Read inbox `037-red-smokes-triage.md`, Codex README, relevant HANDOFF/CLAUDE rules and build/lock protocol.
- Per this work block's instruction, used the already prepared baseline: no fetch, pull, branch switch, merge, or main-checkout writes.
- No source, smoke assertion, flag, runtime, config, map, asset, or Depot edits. This report is the only intended tracked change. Existing untracked asset/config directories were left alone.

## Router and model

| Sub-part | Level | Model used | Result |
|---|---|---|---|
| Step 0: baseline build, two single smoke runs and evidence/report | 2 | gpt-6.1-sol (delegated cheaper worker) | pass; no redo |

The parent dispatch recorded level 2 / confidence 0.39. The worker's required fresh classification also returned level 2, confidence 0.58, `route: false` because confidence was below 0.60. The classifier's Claude model name is ignored per the mailbox README; the explicit instruction to use the cheaper model for levels 1–2 governed execution. Full fresh output: `Saved/Logs/task037/step0/router.log`.

## Build

Command: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (standard defaults; no designer-priority override).

- Started `2026-11-17T05:37:18.3535997+02:00`; finished `2026-11-17T05:47:52.4713448+02:00`.
- Engine result: `Result: Succeeded`; engine execution time **614.88 seconds**; wrapper exit **0**.
- 52 build actions completed. Engine-header deprecation and compiler-version warnings were present; no build failure.
- Full evidence copied immediately after completion: `Saved/Logs/task037/step0/build-engine.log` (113024 bytes), `build-wrapper.log`, `build-result.json`.

## Single-run smoke results

Both commands used `Scripts/smoke_parallel.ps1 -Commands CodexTactics.<Name> -Jobs 1 -NoRetry -ExtraArgs '-Multiprocess' -ResultJson Saved/Logs/task037/step0/<Name>-result.json`. They ran serially, exactly once each, on the wrapper's default fixed 60 fps step and fixed seed. No retry or additional run was performed.

| Smoke | Actual engine result | Duration | Wrapper exit | Step 0 disposition |
|---|---|---|---|---|
| RetreatFacingSmoke | `Smoke RESULT: PASS` | 39.6 s (wrapper wall 41.2 s) | 0 | Green: dropped from active 037 list |
| CoverSmoke | `Smoke RESULT: PASS` | 42.5 s (wrapper wall 43.6 s) | 0 | Green: dropped from active 037 list |

- RetreatFacingSmoke started `2026-11-17T05:48:16.9069549+02:00`, finished `2026-11-17T05:48:58.7286778+02:00`. Engine PASS at preserved log line 1633, engine timestamp `2026.11.17-03.48.56:891`.
- CoverSmoke started `2026-11-17T05:49:15.9856498+02:00`, finished `2026-11-17T05:50:00.1089063+02:00`. Engine PASS at preserved log line 1821, engine timestamp `2026.11.17-03.49.57:907`.
- Each full engine log, wrapper log, result JSON and run metadata was preserved immediately upon that run's completion under `Saved/Logs/task037/step0/`, named `<Name>-engine.log`, `<Name>-wrapper.log`, `<Name>-result.json`, `<Name>-run.json`.
- Actual engine PASS markers above determine the results; a known-fail wrapper exit was not used as evidence. Both result JSON files report `State: PASS`, `jobs: 1`, and empty `rerunPassed` arrays.
- No unit tests or later triage gates were run: this block was restricted to step 0. No code fix was needed; parent commits this step as a report-only checkpoint.

## Next

Step 0 was committed as `8a3ecc24`. MilitaryZombieSmoke is now addressed below; OptimizedCharacterSmoke baseline is confirmed red but its source is untouched and its fix has not started.


## MilitaryZombieSmoke — stale montage assertion; targeted fix, related gate blocked

- Source commit: `11b04070` (`test(smoke): update MilitaryZombieSmoke for retained ragdoll death`). Only MilitaryZombieSmokeCommand.cpp changed: 18 insertions / 7 deletions including comments/include. No runtime, flags, timings, maps or assets changed.
- Cause: Task 056 commit `918d95d3` deliberately makes EnemyAnimInstance skip death montage while IsRagdollActive; current `EnemyAnimInstance.cpp:171`, `EnemyCharacterHealth.cpp:226` activate/reflect that contract. Task005 specifies default immediate ragdoll, settling/freeze and retained bodies; 067 `d734045e` supplies current floor/settle semantics. Old smoke demanded Death_ montage and one-shot weight >0.9 after death. Baseline Military engine FAIL despite ragdoll activation/freeze in1.48s and pelvis body resting at floor.
- Replacement positively requires dying + kept corpse + active ragdoll + simulating/frozen mesh + floor traced under resolved pelvis body, gap in [-configured max sink,45cm), matching the existing DeathRagdollSmoke body-bottom acceptance. Stage4.5s timing and every other assertion remain. This changes the expected death mechanism, not the game or assertion limits.
- Public API/contract references: EnemyCharacter.h IsRagdollActive/IsDying/IsCorpseFrozen; CorpseSubsystem.h ResolvePelvisBone/GetPelvisBodyBottomZ/TraceCorpseFloorZ/IsKept; existing DeathRagdollSmokeCommand.cpp:357-377.
- Baseline: MilitaryZombieSmoke FAIL46.4s; OptimizedCharacterSmoke FAIL63.5s (Marksman and Hound montage assertions after ragdoll activation). Preserved as Saved/Logs/task037/death/<Smoke>-baseline-*; no Optimized source edit yet.
- Router pair: level2, confidence0.52 | gpt-6.1-sol bounded diagnosis and patch proposal; parent review/application/gates | targeted pass; related gate blocked. No local model.

| Gate | Result / evidence |
|---|---|
| Incremental build | PASS31.09s, wrapper exit0; MilitaryZombie-fix-build-* |
| Required standalone1/2/3 | PASS43.2s / PASS42.9s / PASS42.4s; Jobs1, NoRetry, Multiprocess; MilitaryZombie-fixed-N-* |
| Smart plan | actual anim-enemy rule, EnemyAI+EnemyAnim+EnemyAssets, four mapped smokes; 4023 changed/untracked paths,4022 nonverification |
| test.ps1 -Smart -Jobs1 | 18/18 unit tests PASS; MilitaryZombie41.6s, EnemyHitLayer37.5s, Marksman37.1s, EnemyDeath39.1s all PASS, no retry; MilitaryZombie-smart-* |
| DeathRagdollSmoke | PASS, standalone Jobs1/NoRetry/Multiprocess; MilitaryZombie-related-DeathRagdollSmoke-* |
| KnockdownSmoke | PASS42.5s, standalone Jobs1/NoRetry/Multiprocess; MilitaryZombie-related-KnockdownSmoke-* |
| CorpsePersistSmoke | FAIL41.5s, standalone Jobs1/NoRetry/Multiprocess; related gate is NOT green; MilitaryZombie-related-CorpsePersistSmoke-* |
| Staged source audit / whitespace | PASS, risk1.3/4; explicit single smoke path |

All evidence prefixes above are under Saved/Logs/task037/death/. Full engine/wrapper/result files are retained, including Smart unit log and copied structured Smart test report. PASS is read from actual engine RESULT markers.

CorpsePersistSmoke first failure:
```text
Smoke FAIL: hound (BP_Enemy_Hound_C): stays 30 s, frozen 1 (controller None, ticks actor 0 / mesh 0 / move 0), collision off 1, visible 1, out of the registry 1, on the ground (root 61 cm), death pose weight 0.00
```
**needs Claude: determine stale CorpsePersist expectation versus an actual corpse-pose regression; this related gate failed and the protected Corpse/ragdoll implementation was not changed or investigated further.** Military's specific target passes3/3 and Smart passes, but its broader related gate remains blocked. No retry was used to hide this failure.

Execution/approval note: a worker source-edit/build request was denied before any execution for missing explicit task authorization. Parent supplied the original user's direct delegation of NEXT tasks from Claude and obtained approval for the narrow test-only edit and gates. One later review-service Bad Request executed no test; the three recorded standalone runs are the three actual invocations.

Next: OptimizedCharacterSmoke (same suspected stale contract), after Military review. Other037 smokes remain untouched. Final report updates will preserve this related-gate failure until resolved by Claude.
