# 028 - Flaky flag recheck

2026-11-16. Branch: `codex/028-flaky-flag-recheck`.
Tested main/HEAD: `2e8b5c5ab85558555cca4e0671a404573ddc62e8`.
Router log: level 2 (confidence 0.61) | gpt-6.1-sol | pass (exact recheck and report completed; smoke failures are findings).

Result: PmcStalemateSmoke **2/6 PASS**; RetreatFacingSmoke **0/3 PASS**.
Both existing flags remain unchanged. No C++, smoke implementation, assets, configuration, limits, timeouts, or test flags were edited.

Build first: `Scripts/build.ps1` succeeded, exit 0, total execution time 645.27 s. Standard shared locks and designer priority were respected; no bypass. Build evidence: `Saved/Logs/task028/Build.log`.

Exactly six separate PMC invocations followed by three separate retreat invocations, serially:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1 -ExtraArgs '-Multiprocess'
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1 -ExtraArgs '-Multiprocess'
```

Each invocation inherited `CODEX_AGENT=codex` and `PYTHONIOENCODING=utf-8`. No retries or extra runs. Engine RESULT determines PASS; known-fail wrapper exit 0 is not PASS.

| Run | Engine result | Wrapper state / exit | Seconds | Same-order repeat count / dynamic limit |
|---|---|---|---:|---:|
| PmcStalemateSmoke 1 | PASS | PASS / 0 | 67.3 | 2 / 9 |
| PmcStalemateSmoke 2 | FAIL | FAIL / 1 | 71.9 | 17 / 15 |
| PmcStalemateSmoke 3 | PASS | PASS / 0 | 62.3 | 0 / 14 |
| PmcStalemateSmoke 4 | FAIL | FAIL / 1 | 69.4 | 9 / 19 |
| PmcStalemateSmoke 5 | FAIL | FAIL / 1 | 66.1 | 1 / 19 |
| PmcStalemateSmoke 6 | FAIL | FAIL / 1 | 70.9 | 35 / 19 |
| RetreatFacingSmoke 1 | FAIL | KNOWN-FAIL / 0 | 42.4 | n/a |
| RetreatFacingSmoke 2 | FAIL | KNOWN-FAIL / 0 | 39.2 | n/a |
| RetreatFacingSmoke 3 | FAIL | KNOWN-FAIL / 0 | 40.1 | n/a |

First assertion FAIL per failed run (verbatim):

```text
PmcStalemateSmoke 2: Smoke FAIL: the same order re-decided within 2 s at most 15 times (once per fighter-minute; 17)
PmcStalemateSmoke 4: Smoke FAIL: the fight resumes within 45 s after every quiet spell (longest 54.4 s)
PmcStalemateSmoke 5: Smoke FAIL: the fight resumes within 45 s after every quiet spell (longest 72.5 s)
PmcStalemateSmoke 6: Smoke FAIL: the same order re-decided within 2 s at most 19 times (once per fighter-minute; 35)
RetreatFacingSmoke 1: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)
RetreatFacingSmoke 2: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)
RetreatFacingSmoke 3: Smoke FAIL: a facing order of the player is kept: no turn to the pack (65 deg off the ordered facing, 115 deg off the pack, turning to the threat 0)
```

Evidence was copied immediately after each invocation, before the next invocation overwrote the engine's default filename. For each table row, preserved files are `Saved/Logs/task028/<SmokeName>-<run>-engine.log`, `-wrapper.log`, and `-result.json`. These are full actual engine logs, full wrapper output, and wrapper JSON, respectively; the JSON's Log property still records the original engine filename.

Decision: keep only the existing PmcStalemateSmoke knownFlaky entry and RetreatFacingSmoke knownFail entry as-is, because neither removal threshold was met. No deeper diagnosis was performed. Untracked user asset/config directories were preserved.

Validation: `python Scripts/Tools/check_smoke_names.py` exited 0: `OK: every smoke name in Scripts/test_map.json has a registered console command`. No broad tests. Parent reviewed the report, confirmed flags unchanged and ran the required diff audit before the final report commit. No push, merge, main write, or local model invocation.
