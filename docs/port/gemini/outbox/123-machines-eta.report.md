# 123 — "Машины" Tab: Progress Bars & ETA Report

- **Branch**: `gemini/123-machines-eta`
- **Model**: Hydrogen
- **Status**: PASS

## Changes
1. `Scripts/Tools/merge_gate.ps1`:
   - Added `Write-GateProgress` and `Write-GateHistory` (wrapped in `try/catch` so they never affect gate logic or exit codes).
   - `Set-GateStage` writes `Saved/Logs/merge-gate-progress.json` (`stage`, `stage_index`, `stage_count`, `stage_started`, `gate_started`, `updated`), and removes it on gate completion while appending stage durations to `Saved/Logs/merge-gate-history.jsonl` (non-preview runs).
2. `Scripts/Tools/build_remote.ps1`:
   - Writes `Saved/Logs/remote-build-progress.json` (`stage`, `stage_index`, `stage_count`, `started`, `updated`, `worker`) before `worker_sync.ps1` and removes it in `finally` (wrapped in `try/catch`).
3. `Scripts/Tools/machine_tasks.py`:
   - Extended `_PS_SCRIPT` to collect `merge-gate-progress.json`, `merge-gate-history.jsonl`, `remote-build-progress.json`, `RemoteBuilds.jsonl`, and latest `SmokeParallel-*.json` durations.
   - Added pure helpers: `median`, `read_progress_if_fresh` (ignores missing or stale `> 10 min` progress files), `estimate_merge_gate_eta` (median of last 5 runs per stage), `estimate_remote_build_eta` (median of `RemoteBuilds.jsonl` or `[N/M]` linear fallback), `estimate_smoke_batch_eta` (`sum(remaining last durations) / jobs` or linear fallback). All ETAs clamped `>= 0`.
   - Each progress block now includes `{stage, done, total, started, eta_s}` where known; indeterminate bar is only used when nothing is known.
4. `Tools/AgentChat/index.html`:
   - Updated `renderMachines` to display Russian ETA strings (`"Сборка 45/84 · ~6 мин осталось"`, `"~N сек осталось"`, `"скоро"`).
5. `Scripts/Tools/tests/test_machine_tasks.py`:
   - Added `test_eta_math_and_stale_progress_ignored` and `test_merge_gate_and_remote_build_progress_items`.

## Verification
- `python -m pytest Scripts/Tools/tests/test_machine_tasks.py`: **11/11 PASS** (`0.29s`).
- PowerShell AST syntax check on `merge_gate.ps1` and `build_remote.ps1`: **0 parse errors**.
- Browser verification (`http://127.0.0.1:18766/`, dark theme, `"Машины"` tab with mock progress payload):
  - 0 JS console errors.
  - Determinate bars and Russian ETA labels (`Сборка 4/7 · ~6 мин осталось`, `Сборка 45/84 · ~6 мин осталось`, `Смоуки 12/20 · ~4 мин осталось`) and indeterminate fallback (`Грок · 2 мин`) rendered cleanly.
  - Screenshot: `docs/port/gemini/outbox/123-machines-eta.png`.

## Self-Review
- Checked `git diff` against `docs/port/gemini/inbox/123-machines-eta.md`: 0 findings.
