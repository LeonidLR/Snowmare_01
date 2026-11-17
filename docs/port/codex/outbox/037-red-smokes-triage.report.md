# 037 red smokes triage

Status: STEP 0 COMPLETE; later triage has not started. 2026-11-17.

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

Next authorized triage pair: **MilitaryZombieSmoke + OptimizedCharacterSmoke** (check the suspected shared death-clip cause together). Neither has been run or investigated in this step 0 block. Root reviewed actual engine results, confirmed the empty source diff, and ran the required staged audit before this report-only checkpoint.
