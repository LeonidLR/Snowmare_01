# Plan 091: Training Gate and Scenario Regression Runner (smart enemies T-10)

## 1. Goal & Context
Implement roadmap task **T-10** (`docs/design/gdd/smart-enemies-roadmap.md`), GDD section 9 (gates G1-G5), and designer decision 2026-11-19 (OQ-10: evolution paused; worker runs scenario regressions at night; day loop gets quick regression runner).

## 2. Key Deliverables
1. **`Scripts/smoke_parallel.ps1` (args pass-through)**:
   - Add parameter `-SmokeArgs` (and optional `-CmdArgs` alias) to pass arguments like `Seed=N` to the smoke command in `-ExecCmds`.
   - Support optional `args` field from `test_map.json` `smokeOptions` per the stop rule.
2. **`Scripts/Trainer/nighttrainer/gate.py` & CLI integration in `night_trainer.py --check-gate`**:
   - Evaluates:
     - **Metric liveness (G2)**: checks whether any metric used in fitness or QD descriptors is dead/constant (e.g. `overwatch_kills == 0`, `guard_share is None`, or variance 0 over a 6-round probe).
     - **Baseline sufficiency (G3)**: requires a valid baseline with $\ge 30$ rounds per scenario for the current commit / freeze.
     - **Smoke health (G1)**: verifies that listed foundation and scenario smokes (`PmcCrossroadsSmoke`, `PmcAlleySmoke`, `PmcRoofDefenceSmoke`, etc.) are green in the latest sweep or logs.
   - CLI: `python Scripts/Trainer/night_trainer.py --check-gate [--config ...] [--probe-folder ...] [--out ...]`.
   - Exits non-zero with explicit error diagnostics if any check fails. Writes `gate_result.json`.
3. **`Scripts/Trainer/run_night_trainer.ps1` guard**:
   - Refuses to start night trainer evolution unless `--check-gate` passes or a fresh, valid gate file exists.
4. **`Scripts/Tools/scenario_regression.py`**:
   - **Quick Day Mode**: `--scenario <name> --seeds <count or range>` (e.g. `--seeds 20` or `1-20`, target <15 min):
     - Executes scenario smoke across seeds via `smoke_parallel.ps1 -SmokeArgs "Seed=S"`.
     - Prints compact day loop digest: pass rate, the worst 3 seeds, and one failed seed's decision trace.
   - **Night Mode**: runs list of scenario smokes on larger seed sets:
     - Detects regressions since previous run.
     - Identifies stable seeds vs flaky seeds.
     - Writes Markdown report and JSON summary (`Saved/Reports/regression-<date>.md`, `.json`).
5. **Test Suite `Scripts/Trainer/tests/test_gate.py`**:
   - Tests gate checks against the 2026-11-19 fixture where `overwatch_kills` is 0 and `guard_share` is None (must be refused).
   - Tests metric liveness, baseline verification, smoke result validation, and argument passing.

## 3. Gates & Validation
- `python -m unittest discover -s Scripts/Trainer/tests` (100% pass).
- Dry run of `--check-gate` on committed config.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` (PASS).
- `git diff --check` (clean).
