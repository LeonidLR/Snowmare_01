# 094 - Smoke Repeats Plan (Flake Measurement)

## Goal
Enable headless smokes to run $N$ times via `Scripts/smoke_parallel.ps1 -Commands <Name> -Repeat <N>` (and `-Commands A,A,A` duplicate preservation), generating per-run logs `Saved/Logs/Smoke-<Name>-r<k>.log`, individual JSON rows, formatted flake summary `PASS 7/10 (FAIL r3, r6, r9)`, non-zero exit on failure, retry suppression under `-Repeat`, and remote pass-through via `smoke_remote.ps1` and `worker_sync.ps1`.

---

## 1. Findings on Engine Seed Passing & `-RepeatSeeds`
- **UE 5.8 Engine Investigation**:
  In `Runtime/Launch/Private/LaunchEngineLoop.cpp:2462-2476`:
  `FApp::bUseFixedSeed = bDeterministic || FApp::IsBenchmarking() || FParse::Param(FCommandLine::Get(), TEXT("FixedSeed"));`
  When true, `Seed1 = 0; Seed2 = 0;`.
  When false, `Seed1 = FPlatformTime::Cycles(); Seed2 = FPlatformTime::Cycles();`.
  UE 5.8 does **not** support `-FixedSeed=<int>` or a global command line seed value parameter without modifying C++ engine code.
- **Implementation Strategy**:
  - Without `-RepeatSeeds`: runs keep today's clock args with `-FixedSeed` (measuring machine load / timing noise under identical fixed step seed 0).
  - With `-RepeatSeeds`: run $k$ omits `-FixedSeed` (allowing `FPlatformTime::Cycles()` to vary random rolls across runs) and appends `-Seed=$k` for commands/subsystems that parse `Seed=`.

---

## 2. Parameter & Flow Design
1. **Command Expansion & Duplicates**:
   - Remove `$Names = @($Names | Select-Object -Unique)` so duplicate command arguments (`-Commands A,A,A`) are preserved.
   - For each command in `$Commands`, expand by `$Repeat` times.
   - Assign `Repeat` index $1..N$ and `Suffix` (`-r$k` if repeated, `""` if single unrepeated run).
2. **Execution & Logging**:
   - `Start-SmokeProcess`: writes `Saved/Logs/Smoke-<Name>-r<k>.log` (or `Saved/Logs/Smoke-<Name>.log` for single runs).
   - `Invoke-Batch`: receives task descriptors and logs progress with repeat tags.
   - Result JSON (`SmokeParallel-last.json`): each run records `Name`, `Repeat`, `State`, `Result`, `Seconds`, `Log`.
3. **Summary & Exit Code**:
   - Summary per repeated smoke: `PASS X/Y` or `PASS X/Y (FAIL r3, r6, r9)`.
   - Exit code 1 if any run fails (unless `knownFail`).
4. **Retry Suppression**:
   - When `-Repeat -gt 1`: skip the automatic "re-run alone" phase so true flakiness rate is not hidden.
   - Without `-Repeat`: existing retry behavior is 100% preserved.
5. **Remote Pass-through**:
   - `Scripts/Tools/smoke_remote.ps1`: accept `-Repeat` and `-RepeatSeeds`, forward to `worker_sync.ps1`.
   - `Scripts/Tools/worker_sync.ps1`: accept `-Repeat` and `-RepeatSeeds`, forward to worker's `smoke_parallel.ps1`, archive and fetch all logs.
6. **Documentation**:
   - Add usage entry to `docs/port/LANES.md`.

---

## 3. Verification Plan
1. **Pester-Free Self-Test**:
   - Create `Scripts/Tools/tests/test_smoke_repeat.ps1` testing:
     - Argument expansion (`-Commands A,A` -> 2 runs; `-Commands A -Repeat 3` -> 3 runs).
     - Suffix and log path generation (`-r1`, `-r2`, `-r3` vs unrepeated `""`).
     - Summary string generation (`PASS 7/10 (FAIL r3, r6, r9)`).
     - Result JSON structure.
2. **Real Local Execution**:
   - `powershell -File Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke -Repeat 3 -Jobs 3`
   - Verify 3 separate logs (`Smoke-MovementSmoke-r1.log` .. `r3.log`), 3 JSON rows, and summary output.
3. **Backward Compatibility Check**:
   - `powershell -File Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke`
   - Confirm unrepeated run produces standard `Smoke-MovementSmoke.log` and unchanged exit code/output.
4. **Gates**:
   - `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
   - `git diff --check`
