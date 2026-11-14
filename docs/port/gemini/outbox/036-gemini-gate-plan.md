# 036 - Gemini Gate (`gemini_gate.ps1`) — Plan

## Goal
Implement `Scripts/Tools/gemini_gate.ps1` (backed by Python helper `Scripts/Tools/gemini_gate.py` and unit tests `Scripts/Tools/tests/test_gemini_gate.py`) to run pre-"done" validation checks for task `<NNN>`:
1. **Location**: must run in a worktree (not main checkout) on branch `gemini/<NNN>-*`.
2. **Allowed paths**: parse `docs/port/gemini/inbox/<NNN>-*.md` Allowed paths and fail on any changed file vs base branch outside them; always fail on `Content/Maps`, `Config`, `Content/VFX`, `.uasset`, `.umap`, Content packs, and `Content/Data/LevelJson/L_Gen_Depot_1.json`.
3. **Build + tests**: run `Scripts/build.ps1` (if `Source/**` changed and `-SkipBuild` is not set), `Scripts/test.ps1 -Smart -Base <base>` (if `Source/**` changed), `npm test` + `npx tsc --noEmit` in `Tools/TrainingGround` (if `Tools/TrainingGround/**` changed), and `python -m unittest` on `Scripts/Tools/tests/` (if `Scripts/Tools/**` changed); capture last output lines.
4. **Jev audit**: run `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`.
5. **Citation check**: verify every `path:line` and repo path referenced in `docs/port/gemini/outbox/<NNN>-*.report.md` exists (`1 <= line <= file line count`).
6. **Evidence check**: verify the report contains a `Claim | Evidence` (or `Утверждение | Доказательство`) table with at least one row and no empty/placeholder evidence cells.
7. **Optional `-SonnetReview`** (when C++ diff exists): invoke `Scripts/Tools/api_task.py --model claude-sonnet-5-5 --effort medium --label gemini-review-<NNN> --budget-usd <cap from docs/port/PLAN_WEEK_*.md>`, save review alongside the report, and fail only on `HIGH` findings without printing keys.
8. Save full gate summary and check tails to `Saved/Orchestrator/gate/<NNN>-<timestamp>.md`.

## Steps
1. Commit this plan only (`docs/port/gemini/outbox/036-gemini-gate-plan.md`).
2. Implement `Scripts/Tools/gemini_gate.py` (checks 1, 2, 5, 6, budget/base resolution, Sonnet review prep & severity scan, gate markdown formatting) and `Scripts/Tools/tests/test_gemini_gate.py` covering checks 2, 5, 6 + edge cases; run `unittest`.
3. Implement `Scripts/Tools/gemini_gate.ps1` orchestrating checks 1–7, saving to `Saved/Orchestrator/gate/<NNN>-<time>.md`; commit implementation + tests.
4. Create `docs/port/gemini/outbox/036-gemini-gate.report.md`, run `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 036`, paste gate summary + unit test output into the report, re-run gate to verify self-check passes cleanly, commit report, and post completion hash in chat (`--topic gemini-036`).

## Exact Files to Touch
- `docs/port/gemini/outbox/036-gemini-gate-plan.md` (plan)
- `Scripts/Tools/gemini_gate.py` (Python checks + gate report generator)
- `Scripts/Tools/gemini_gate.ps1` (PowerShell CLI entrypoint)
- `Scripts/Tools/tests/test_gemini_gate.py` (unit tests for checks 2, 5, 6 on isolated fixtures)
- `docs/port/gemini/outbox/036-gemini-gate.report.md` (final report)
