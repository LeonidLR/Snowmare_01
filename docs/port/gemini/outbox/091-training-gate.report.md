# 091 - Training Gate & Scenario Regression (T-10) - Report

## Handover
- **Done**:
  - `Scripts/smoke_parallel.ps1`: Added `-SmokeArgs` / `-Args` parameter and pass-through so individual seeds or custom arguments (`Seed=N`) reach `CodexTactics.$Name`.
  - `Scripts/Trainer/nighttrainer/gate.py`: Implemented full G1-G3 training gate validation logic:
    - G1 Smoke Health check: verifies required smoke tests are healthy (`PASS` / `PASS*` / `KNOWN_FAIL`).
    - G2 Metric Liveness check: verifies every fitness / QD metric in the 6-round probe is non-zero, non-constant, and populated with valid variance. Correctly catches dead metrics like 2026-11-19 (`overwatch_kills == 0`, `guard_share is null`).
    - G3 Baseline Sufficiency check: ensures $\ge 30$ rounds per scenario exist for the frozen baseline.
  - `Scripts/Trainer/night_trainer.py`: Added `--check-gate` CLI entry point with diagnostic logging. Outputs `gate_pass.json` (exit 0) or `gate_blocked.json` (exit 1).
  - `Scripts/Trainer/run_night_trainer.ps1`: Added refusal guard requiring a valid `gate_pass.json` less than 24 hours old before launching training runs.
  - `Scripts/Tools/scenario_regression.py`: Implemented scenario regression runner:
    - Quick Day Mode: runs 20-50 seeds on a selected scenario, produces a compact digest with pass rate, worst 3 seeds, and decision trace for a failed seed (target < 15 min).
    - Night Mode: runs comprehensive multi-scenario seed sweep, compares against previous runs (`--compare-yesterday`), isolates regressions and detects flaky seeds.
  - `Scripts/Trainer/fixtures/gate_probe_20261119_dead_metrics.json`: Historical fixture representing the 2026-11-19 6-round probe that wasted a 14.5-hour run due to unexercised metrics.
  - `Scripts/Trainer/tests/test_gate.py`: 10 comprehensive unit tests verifying G1, G2, G3 pass and block rules, probe evaluation, baseline requirements, and file generation.
  - `docs/tools/night-trainer.md`: Documented Section 8 (Training Gate & Scenario Regression Workflow).
- **Left**: None.
- **Needs Claude**: Merge from local branch `gemini/091-training-gate` to `main`.
- **Next Step**: Proceed to task `094` (`gemini/094-smoke-repeats`).

---

## Design Gate & Objectives
- **Context**: Smart Enemies Roadmap T-10, GDD Section 9 & 5a.
- **Problem Solved**: On 2026-11-19, a night run executed for 14.5 hours with dead metrics (`overwatch_kills == 0` across all rounds, `guard_share == null`), generating zero evolutionary pressure. The training gate prevents launching costly training jobs unless smoke health, metric liveness, and baseline sufficiency are mathematically verified.

---

## Evidence & Verification

### 1. Training Gate Logic & 2026-11-19 Fixture
Unit tests prove that the gate strictly flags missing and constant metrics:
```python
# test_gate.py::test_metric_liveness_blocks_20261119_dead_probe
# Fixture: gate_probe_20261119_dead_metrics.json
# Result: BLOCKED with:
#   - "Metric 'overwatch_kills' has zero sum / no signal across all 6 rounds"
#   - "Metric 'guard_share' has missing/null values across all 6 rounds"
```

### 2. Refusal Guard in `run_night_trainer.ps1`
When invoked without an existing/valid `gate_pass.json`:
```powershell
powershell -ExecutionPolicy Bypass -File "Scripts\Trainer\run_night_trainer.ps1" -RunId test-refusal
# Output:
# run_night_trainer: REFUSED - training gate pass file (...Saved\Trainer\gate_pass.json) missing or expired. Run 'night_trainer.py --check-gate' first.
# Exit code: 1
```

### 3. Unit Test Suite
```text
python -m unittest Scripts/Trainer/tests/test_gate.py
Ran 10 tests in 0.178s
OK

python -m unittest discover -s Scripts/Trainer/tests
Ran 95 tests in 15.668s
OK
```

### 4. Safety & Typesafe Audit
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
- `Scripts/smoke_parallel.ps1`: Lines 16-20 (`-SmokeArgs`, `-Args`), 138-142 (parameter pass-through to `CodexTactics.$Name $cmdArgs`).
- `Scripts/Trainer/nighttrainer/gate.py`: New module with `check_smoke_health()`, `check_metric_liveness()`, `check_baseline_sufficiency()`, and `evaluate_training_gate()`.
- `Scripts/Trainer/night_trainer.py`: Lines 174-180 (`--check-gate` argument parser), lines 259-307 (`--check-gate` handler).
- `Scripts/Trainer/run_night_trainer.ps1`: Lines 42-56 (pre-flight gate check requiring fresh `gate_pass.json`).
- `Scripts/Tools/scenario_regression.py`: New CLI tool for Day Quick Digest and Nightly Scenario Regression.
- `Scripts/Trainer/fixtures/gate_probe_20261119_dead_metrics.json`: Fixture representing dead metrics probe.
- `Scripts/Trainer/tests/test_gate.py`: 10 unit test cases for the gate.
- `docs/tools/night-trainer.md`: Section 8 documentation.

---

091 FINISH | PASS | 4dc822cd | done: gate.py, night_trainer --check-gate, run_night_trainer refusal guard, scenario_regression.py (day/night), test_gate.py, night-trainer.md | NOT done: none | needs Claude: merge from local branch gemini/091-training-gate | gate: unittest 95 pass, triage clean, git diff clean | report: docs/port/gemini/outbox/091-training-gate.report.md
