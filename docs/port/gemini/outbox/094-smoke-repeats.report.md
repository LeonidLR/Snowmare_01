# 094 - Smoke Repeats & Flake Measurement (TOOL) - Report

## Handover
- **Done**:
  - `Scripts/smoke_parallel.ps1`:
    - Added `-Repeat <N>` (default 1) and `-RepeatSeeds` (switch) parameters.
    - Removed `Select-Object -Unique` on command names so duplicate names in `-Commands` (e.g. `-Commands A,A,A`) preserve their count.
    - Expanded task list by `$Repeat`: each run receives a `Repeat` index $1..N$ and a `Suffix` (`-r$k` if repeated, `""` if single unrepeated run).
    - Per-run logs saved to `Saved/Logs/Smoke-<Name>-r<k>.log` (or `Smoke-<Name>.log` for single runs).
    - Result JSON (`Saved/Logs/SmokeParallel-last.json`) records individual rows with `Name`, `Repeat`, `State`, `Result`, `Seconds`, `Log`.
    - Implemented per-smoke flake summary formatting: `PASS X/Y` or `PASS X/Y (FAIL r3, r6, r9)`.
    - Exit code non-zero (1) if any run fails.
    - Suppressed automatic retry phase when `-Repeat -gt 1` so flake rate is not masked; preserved retry when `-Repeat -le 1`.
  - `Scripts/Tools/smoke_remote.ps1`: Added `-Repeat` and `-RepeatSeeds` parameter pass-through to `worker_sync.ps1`.
  - `Scripts/Tools/worker_sync.ps1`: Added `-Repeat` and `-RepeatSeeds` forwarding to worker's `smoke_parallel.ps1`.
  - `Scripts/Tools/tests/test_smoke_repeat.ps1`: Pester-free unit test suite verifying task expansion, duplicate preservation, suffix assignment, summary string generation, and exit code logic (21/21 PASS).
  - `docs/port/LANES.md`: Added usage line.
- **Left**: None.
- **Needs Claude**: Merge from local branch `gemini/094-smoke-repeats` to `main`.
- **Next Step**: Proceed to task `090` (`gemini/090-tactical-export-v2`).

---

## Engine Seed Analysis & `-RepeatSeeds` Findings
- **Investigation of UE 5.8 `LaunchEngineLoop.cpp:2462-2476`**:
  ```cpp
  FApp::bUseFixedSeed = bDeterministic || FApp::IsBenchmarking() || FParse::Param(FCommandLine::Get(), TEXT("FixedSeed"));
  ...
  uint32 Seed1 = 0;
  uint32 Seed2 = 0;
  if (!FApp::bUseFixedSeed)
  {
      Seed1 = FPlatformTime::Cycles();
      Seed2 = FPlatformTime::Cycles();
  }
  FMath::RandInit(Seed1);
  FMath::SRandInit(Seed2);
  ```
- **Finding**:
  `-FixedSeed` in UE 5.8 is strictly a boolean parameter (`FParse::Param`) that hardcodes random seeds to 0 (`Seed1 = 0; Seed2 = 0;`). There is no command-line syntax for passing an integer seed (`-FixedSeed=<N>`) at engine level without modifying C++.
- **Resolution**:
  - Without `-RepeatSeeds`: Repeats keep `-FixedSeed` (measuring machine load / timing noise under identical fixed step seed 0).
  - With `-RepeatSeeds`: Run $k$ omits `-FixedSeed` so `FApp::bUseFixedSeed` remains `false` (allowing CPU cycle counter `FPlatformTime::Cycles()` to vary random rolls across runs) and appends `-Seed=$k` for commands/subsystems that parse it.

---

## Gate Results & Evidence

### 1. Pester-free Unit Test Suite (`Scripts/Tools/tests/test_smoke_repeat.ps1`)
```text
=== test_smoke_repeat.ps1 ===
  [PASS] Duplicate count preserved
  [PASS] Task 1 suffix is -r1
  [PASS] Task 2 suffix is -r2
  [PASS] Task 3 suffix is -r3
  [PASS] Single run count is 1
  [PASS] Single run suffix is empty (backward compatibility)
  [PASS] Single run repeat is 1
  [PASS] Repeat 3 yields 3 tasks
  [PASS] Repeat task 1 suffix is -r1
  [PASS] Repeat task 2 suffix is -r2
  [PASS] Repeat task 3 suffix is -r3
  [PASS] Multiple commands repeat count
  [PASS] SmokeA r1 suffix
  [PASS] SmokeA r2 suffix
  [PASS] SmokeB r1 suffix
  [PASS] SmokeB r2 suffix
  [PASS] Summary with failures format
  [PASS] Summary all pass format
  [PASS] Summary all fail format
  [PASS] Exit code 1 on failure
  [PASS] Exit code 0 on all pass

Summary: 21 passed, 0 failed.
```

### 2. Real Run on this PC (`MovementSmoke -Repeat 3 -Jobs 3`)
```text
powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke -Repeat 3 -Jobs 3

[smoke_parallel] 3 smokes (repeat 3), 3 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  MovementSmoke              (r1) PASS      94,5s
  MovementSmoke              (r2) PASS      92,7s
  MovementSmoke              (r3) PASS      91,0s

Smoke                      Summary
MovementSmoke              PASS 3/3

Smoke                      Repeat   Result       Seconds
MovementSmoke              r1       PASS            94,5
MovementSmoke              r2       PASS            92,7
MovementSmoke              r3       PASS            91,0
[smoke_parallel] 3 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 97 s, sum of checks 278 s
```
Created logs in `Saved/Logs/`:
- `Smoke-MovementSmoke-r1.log` (219,783 bytes)
- `Smoke-MovementSmoke-r2.log` (219,538 bytes)
- `Smoke-MovementSmoke-r3.log` (218,768 bytes)

Result JSON (`Saved/Logs/SmokeParallel-last.json`):
```json
{
    "results":  [
                    {
                        "Name":  "MovementSmoke",
                        "Repeat":  1,
                        "State":  "PASS",
                        "Result":  "PASS",
                        "Seconds":  94.5,
                        "Log":  "...\\Saved\\Logs\\Smoke-MovementSmoke-r1.log"
                    },
                    {
                        "Name":  "MovementSmoke",
                        "Repeat":  2,
                        "State":  "PASS",
                        "Result":  "PASS",
                        "Seconds":  92.7,
                        "Log":  "...\\Saved\\Logs\\Smoke-MovementSmoke-r2.log"
                    },
                    {
                        "Name":  "MovementSmoke",
                        "Repeat":  3,
                        "State":  "PASS",
                        "Result":  "PASS",
                        "Seconds":  91,
                        "Log":  "...\\Saved\\Logs\\Smoke-MovementSmoke-r3.log"
                    }
                ],
    "wallSeconds":  96.8,
    "jobs":  3,
    "rerunPassed":  []
}
```

### 3. Unrepeated Backward Compatibility Run (`MovementSmoke`)
```text
powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke

[smoke_parallel] 1 smokes, 4 at a time, fixed step (-UseFixedTimeStep -FPS=60 -FixedSeed)
  MovementSmoke              PASS      49,6s

Smoke                      Result       Seconds
MovementSmoke              PASS            49,6
[smoke_parallel] needed a re-run: none
[smoke_parallel] 1 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 51 s, sum of checks 50 s
```
Confirmed `Saved/Logs/Smoke-MovementSmoke.log` updated with standard suffix-less naming and exit code 0.

### 4. Safety & Boundary Audit
```text
python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini
• Agent Audited   : GEMINI
• Boundary Breach : PASS
• Risk Rating     : 0.0 / 4 (Jev)

git diff --check
(clean, no trailing whitespace or EOF issues)
```

---

## File Changes & Touched Locations
- `Scripts/smoke_parallel.ps1`:
  - Lines 38-42: Added `[int]$Repeat = 1` and `[switch]$RepeatSeeds`.
  - Lines 48-50: Remote invocation forwarding `-Repeat` and `-RepeatSeeds`.
  - Lines 60-95: Duplicate preservation, `$Repeat` expansion, per-run `$tasks` generation with `-r$k` suffix.
  - Lines 107-117: `Get-ClockArgs` supporting `$RepeatSeeds` (`-Seed=$RepeatIndex`).
  - Lines 119-145: `Start-SmokeProcess` taking task descriptor and repeat index.
  - Lines 150-192: `Invoke-Batch` logging repeat tags (`(r1) PASS`).
  - Lines 200-245: Exclusive/shared task processing, retry suppression under `$isRepeatedRun`.
  - Lines 250-295: Summary formatting (`PASS X/Y (FAIL r...)`), repeat column table, and failure detection.
- `Scripts/Tools/smoke_remote.ps1`: Lines 26-28 (parameters), line 78 (forwarding to `worker_sync.ps1`).
- `Scripts/Tools/worker_sync.ps1`: Lines 31-33 (parameters), lines 403-405 (forwarding to `smoke_parallel.ps1`).
- `Scripts/Tools/tests/test_smoke_repeat.ps1`: New Pester-free unit test suite.
- `docs/port/LANES.md`: Line 23 (usage entry).

---

094 FINISH | PASS | 78681788 | done: smoke_parallel.ps1 -Repeat/-RepeatSeeds, duplicate preservation, -r<k> logs, flake summary PASS X/Y, retry suppression, smoke_remote.ps1 & worker_sync.ps1 forwarding, test_smoke_repeat.ps1, LANES.md | NOT done: none | needs Claude: merge from local branch gemini/094-smoke-repeats | gate: test_smoke_repeat 21 pass, MovementSmoke x3 pass, MovementSmoke x1 pass, triage clean, git diff clean | report: docs/port/gemini/outbox/094-smoke-repeats.report.md
