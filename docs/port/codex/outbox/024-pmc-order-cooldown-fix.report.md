# 024 - PMC per-order failed cooldown retention

Status: implementation, build and focused unit checks complete. Parent reviewed the five source/test diffs. Required smoke checks pending parent checkpoint commit and authorization. Task is not yet complete.

## Change

Report020 identified the single-slot overwrite at PmcSquadSubsystem.cpp:1396/1413/1992 and PmcCommanderRules.cpp:287/293: an ended-short Objective or Flank failed, then the immediate retry refused FallBack and replaced the walk cooldown.

The fighter now retains a double world-clock failed-until timestamp per order kind. Each failure updates only its own entry, including the ended RpgStrike path. A decision passes all stored expiries into PickCommitted, and retries refresh that same snapshot after recording the refusal. PickCommitted excludes each order strictly before its own expiry; it becomes eligible exactly at expiry. The bounded enum-keyed map does not grow with repeated failures.

The existing FailedOrderCooldownSeconds / failed_order_cooldown_s configuration is unchanged. No tuning constants, smoke assertions, knownFlaky settings, gameplay owners outside the four PMC commander/subsystem source/header files, assets or designer config were changed.

## Regression

CodexTactics.AI.PmcCommander.FailureRetention covers Objective and Flank independently: record the walk failure, try FallBack, record its refusal later, verify both are excluded in the retry and subsequent decision, verify the walk returns at its own expiry while FallBack remains excluded, then verify FallBack returns at its later expiry. Times derive from the existing cooldown value. The existing Commitment test was minimally migrated to the expiry API.

## Checks

- git diff --check: PASS before build.
- Scripts/build.ps1: PASS; Result: Succeeded; Total execution time: 663.77 seconds; exit code 0. Log: Saved/Logs/Build.log.
- Scripts/test.ps1 -Filter CodexTactics.AI.PmcCommander: PASS; 16 tests, 0 failed; exit code 0. Includes Commitment and FailureRetention. Log: Saved/Logs/AutomationTests.log; report: Saved/TestReport.
- Four separate serial PmcStalemateSmoke -NoRetry -Jobs 1 invocations: pending parent review/authorization.
- PmcStandoffSmoke, PmcUnderFireSmoke, PmcFireLineSmoke once: pending parent review/authorization.

No commit, push, merge, branch switch, or main writes performed. Untracked assets/config left untouched.
