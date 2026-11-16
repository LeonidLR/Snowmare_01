# 024 - PMC per-order failed cooldown retention

Status: implementation and all requested checks complete; PASS. Routing: router2 | gpt6.1sol (score 0.56). Source/test checkpoint: `2f2ccb9e`, committed by parent after diff review and audit PASS. The smoke phase used that checkpoint with no further source edits. Final report awaits parent audit/commit.

## Change

Report020 identified the single-slot overwrite at PmcSquadSubsystem.cpp:1396/1413/1992 and PmcCommanderRules.cpp:287/293: an ended-short Objective or Flank failed, then the immediate retry refused FallBack and replaced the walk cooldown.

The fighter now retains a double world-clock failed-until timestamp per order kind. Each failure updates only its own entry, including the ended RpgStrike path. A decision passes all stored expiries into PickCommitted, and retries refresh that snapshot after recording the refusal. PickCommitted excludes each order strictly before its own expiry; it becomes eligible exactly at expiry. The bounded enum-keyed map does not grow with repeated failures.

The existing FailedOrderCooldownSeconds / failed_order_cooldown_s configuration is unchanged; actual smoke failure messages still print 5 seconds. No tuning constants, smoke assertions, dynamic repeat limits or knownFlaky settings changed. Only four PMC commander/subsystem source/header files and the focused test changed in the implementation.

## Regression and build

CodexTactics.AI.PmcCommander.FailureRetention covers Objective and Flank independently: record the walk failure, try FallBack, record its refusal later, verify both exclusions in the retry and subsequent decision, verify the walk returns exactly at its own expiry while FallBack remains excluded, then verify FallBack returns at its later expiry. Times derive from the existing cooldown value. The existing Commitment test was minimally migrated to the expiry API.

- Scripts/build.ps1: PASS; Result: Succeeded; total execution time 663.77 seconds; exit code 0. Log: Saved/Logs/Build.log.
- Scripts/test.ps1 -Filter CodexTactics.AI.PmcCommander: PASS; 16 tests, 0 failed; exit code 0. Includes Commitment and FailureRetention. Log: Saved/Logs/AutomationTests.log; report: Saved/TestReport.
- git diff --check: PASS before build and after the unit phase.

## Four serial stalemate checks

Executed four separate standard invocations, each exactly `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1`. Default fixed step: -UseFixedTimeStep -FPS=60 -FixedSeed. Each invocation exited 0, ran once, and used no retry. Actual RESULT and FAIL lines were checked in each preserved engine log: four RESULT PASS, zero Smoke FAIL lines.

| Run | Wrapper result | Round duration, outcome | Same repeats / printed dynamic limit | Longest quiet s | Orders/fighter-min | Failed orders |
|---|---|---|---|---|---|---|
| 1 | PASS 155.5s | 50.8s, B/elimination | 0 / 6 | 22.4 | 12.99 | 10 |
| 2 | PASS 73.1s | 53.4s, A/elimination | 1 / 6 | 12.2 | 10.11 | 17 |
| 3 | PASS 107.6s | 180.0s, A/time_limit | 6 / 19 | 32.4 | 10.33 | 23 |
| 4 | PASS 108.6s | 89.8s, A/elimination | 9 / 9 | 30.8 | 10.02 | 23 |

The printed dynamic limits are retained verbatim. The floor remains 6 for 3v3, and longer rounds allow more under the existing formula; run 3 prints 19 at its internally unrounded time-limit boundary although exported duration rounds to 180.0 seconds. No threshold was widened. All four rounds made contact, reached the point-area assertion, and had zero early preempts.

## Companion checks and exact result lines

Executed one serial batch: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands PmcStandoffSmoke,PmcUnderFireSmoke,PmcFireLineSmoke -NoRetry -Jobs 1`. Three checks, zero failed, no retries, exit code 0. Each actual engine log contains RESULT PASS and no Smoke FAIL lines. Wrapper decimal separators below are normalized to dots only.

```text
run1 PmcStalemateSmoke PASS 155.5s
run1 Smoke RESULT: PASS
run2 PmcStalemateSmoke PASS 73.1s
run2 Smoke RESULT: PASS
run3 PmcStalemateSmoke PASS 107.6s
run3 Smoke RESULT: PASS
run4 PmcStalemateSmoke PASS 108.6s
run4 Smoke RESULT: PASS
PmcStandoffSmoke PASS 74.5s; actual Smoke RESULT: PASS
PmcUnderFireSmoke PASS 72.1s; actual Smoke RESULT: PASS
PmcFireLineSmoke PASS 68.4s; actual Smoke RESULT: PASS
```

## Preserved evidence and outcome

Each stalemate run has Saved/Logs/codex024-runN.log (actual engine log), codex024-runN-wrapper.log, codex024-runN.json (wrapper result), codex024-runN.track.json, and codex024-runN.arena.json (round summary). Original arena files remain:

- Run 1: Saved/Arena/20261116-142615/round_1.track.json and Saved/Arena/20261116-142615.json.
- Run 2: Saved/Arena/20261116-142757/round_1.track.json and Saved/Arena/20261116-142757.json.
- Run 3: Saved/Arena/20261116-142932/round_1.track.json and Saved/Arena/20261116-142932.json.
- Run 4: Saved/Arena/20261116-143147/round_1.track.json and Saved/Arena/20261116-143147.json.

Companion evidence: Saved/Logs/codex024-companions-wrapper.log, codex024-companions.json, and codex024-PmcStandoffSmoke.log / codex024-PmcUnderFireSmoke.log / codex024-PmcFireLineSmoke.log.

Required checks: build PASS; commander unit group 16/16 PASS; stalemate 4/4 PASS; companion smokes 3/3 PASS. PmcStalemateSmoke remains knownFlaky unchanged for Claude to assess; four passes do not establish permanent removal of all other churn causes. No residual failed assertion requires a needsClaude implementation escalation in this task. Run 4 meets its existing repeat limit exactly (9/9).

No additional source changes, commits, push, merge, branch switch, main writes, broad tests, or repeated build/unit runs in the smoke phase. Untracked assets/config left untouched.
