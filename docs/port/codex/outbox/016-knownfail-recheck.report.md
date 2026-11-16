# 016 — knownFail / knownFlaky recheck

Tested main: `401bb22cb88ce9a0a73b0da1c47f370c1b51c5fc` on branch `codex/016-knownfail-recheck` (2026-11-16). Router: level 2, GPT-6.1 Sol, confidence 0.88.

Fresh source/binaries build: **PASS**, `Scripts/build.ps1`, exit 0, `Result: Succeeded`, 71.09 seconds. Console output and the last 15 build-log lines were captured; wrapper: `Saved/Logs/codex016-build-wrapper.log`, full build: `Saved/Logs/Build.log`.

Enumeration of every active entry in `Scripts/test_map.json` found exactly the following two entries; no configuration changes were made.

| Name | listed as | reason in test_map | runs (P/F) | first FAIL line | matches the reason? |
| --- | --- | --- | --- | --- | --- |
| RetreatFacingSmoke | knownFail (`smokeOptions.RetreatFacingSmoke.knownFail`) | 2026-11-15: FAIL 'both fire at the pack again (10.0 s after facing it)' on main 4cf88e74 with and without the 058b patch and with -PreloadNoWorldPause - not 058b, not the preload pause; investigate (board 064) | Skipped: task 014 already covered F/P/F | Not rerun here | Not reassessed here; task 014 owns the evidence |
| PmcStalemateSmoke | knownFlaky | 2026-11-10: flaky on main too (1 of 2 FAIL: a 69-72 s quiet spell / attackers short of the point). 2026-11-11 fix/pmc-onpoint-3d-detour (level-aware on-point + three-step detour, docs/systems/pmc-squad-ai.md section 13): 3 of 4 PASS; the FAIL is a ground-level street point (attackers never closer than 25.7 m, a 50.9 s quiet spell) - the commander churn under fire, not the roof bug | **F / P / P** | `Smoke FAIL: the same order re-decided within 2 s at most 6 times (once per fighter-minute; 13)` | **no** for the listed quiet-spell / point-reach failure signature; the observed order-churn assertion is consistent with its broader commander-churn explanation. Still flaky; not “passes now.” |

RetreatFacingSmoke was explicitly skipped under task 016's task-014 exception: task 014 had already produced F/P/F, and its report commit `3ffbc719` was accepted before this task. Those prior runs are not represented as tests of this newer main revision.

PmcStalemateSmoke ran three times sequentially, each using exactly:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.PmcStalemateSmoke -NoRetry -Jobs 1
```

Actual result lines (from the preserved engine logs):

```text
run 1: [2026.11.16-06.40.11:928][679]LogCodexTactics: Display: Smoke RESULT: FAIL
run 2: [2026.11.16-06.41.40:667][179]LogCodexTactics: Display: Smoke RESULT: PASS
run 3: [2026.11.16-06.47.49:414][915]LogCodexTactics: Display: Smoke RESULT: PASS
```

First FAIL lines, at most three per smoke (only one assertion failed across all three runs):

```text
run 1: [2026.11.16-06.40.11:695][679]LogCodexTactics: Display: Smoke FAIL: the same order re-decided within 2 s at most 6 times (once per fighter-minute; 13)
```

Evidence: actual logs `Saved/Logs/codex016-run1.log`, `codex016-run2.log`, `codex016-run3.log`; wrapper logs `Saved/Logs/codex016-wrapper1.log`, `codex016-wrapper2.log`, `codex016-wrapper3.log` (wrapper exit codes 1 / 0 / 0). Verdicts above use the actual `Smoke RESULT` lines. Run 3 spent several minutes at startup `Launching UnrealBuildTool ... -Mode=ValidatePlatforms` before recovering and completing; it was neither retried nor given extra arguments.