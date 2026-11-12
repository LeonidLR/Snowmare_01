# 036 — Gemini Pre-Done Quality Gate (`Scripts/Tools/gemini_gate.ps1`)

## Summary

Implemented the automated pre-"done" quality gate specified in `docs/port/gemini/inbox/036-gemini-gate.md`, `GEMINI.md`, and `docs/port/gemini/TASK_TEMPLATE.md` on isolated worktree branch `gemini/036-gate`.

- Entry point: `Scripts/Tools/gemini_gate.ps1` (`-Task <NNN>`, optional `-Worktree`, `-SkipBuild`, `-SonnetReview`, `-Base`)
- Verification engine & Markdown reporter: `Scripts/Tools/gemini_gate.py`
- Unit test suite: `Scripts/Tools/tests/test_gemini_gate.py` (7 test methods covering checks 1, 2, 5, 6, 7)

## Implemented Checks

1. **Location (`Scripts/Tools/gemini_gate.py:132-164`)**: Fails if run from the main checkout (`CodexTactics` or directory `.git`) or on protected/mismatched branches (`main`, `master`, `HEAD`, `interim/*`, or non-`gemini/<NNN>-*`).
2. **Allowed paths (`Scripts/Tools/gemini_gate.py:178-417`)**: Parses `Allowed paths` sections or inline lists from `docs/port/gemini/inbox/<NNN>-*.md` (supporting brace expansion, globs, and parenthetical notes), always fails on `Content/Maps`, `Config`, `Content/VFX`, `.uasset`, `.umap`, non-`Data` Content packs/junctions, and `Content/Data/LevelJson/L_Gen_Depot_1.json`. Also skips Python `__pycache__` / `.pyc` artifacts and untouched NTFS reparse-point junctions in `Scripts/Tools/gemini_gate.py:420-463`.
3. **Build + tests (`Scripts/Tools/gemini_gate.ps1:87-143`)**: Auto-detects changed areas and runs `Scripts/build.ps1` (unless `-SkipBuild`) + `Scripts/test.ps1 -Smart -Base <base>` for C++, `npm test` + `npx tsc --noEmit` for `Tools/TrainingGround`, and `python -m unittest discover -s Scripts/Tools/tests -p "test_*.py" -v` for `Scripts/Tools`, embedding tail output lines.
4. **Jev audit (`Scripts/Tools/gemini_gate.ps1:163-183`)**: Runs `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` and captures exit status + tail lines.
5. **Citation check (`Scripts/Tools/gemini_gate.py:491-571`)**: Scans outbox report(s) for `path:line` and repo file references, verifies every referenced path exists in the worktree and `1 <= line <= EOF`, and lists broken citations as `FAIL`.
6. **Evidence check (`Scripts/Tools/gemini_gate.py:599-647`)**: Verifies presence of a `Claim | Evidence` (or `Утверждение | Доказательство`) Markdown table with at least one row and no empty or placeholder evidence cells.
7. **Optional Sonnet review (`Scripts/Tools/gemini_gate.py:650-748`)**: When `-SonnetReview` is passed on C++ diffs under `Source/`, invokes `Scripts/Tools/api_task.py` (`--model claude-sonnet-5-5 --effort medium`) using the latest weekly budget cap from `docs/port/PLAN_WEEK_2026-11-16.md`, writes `<NNN>-sonnet-review.md` beside the report, and fails only if HIGH severity findings are present (never prints keys).
8. **Gate output log (`Scripts/Tools/gemini_gate.ps1:194-205`, `Scripts/Tools/gemini_gate.py:751-853`)**: Writes BOM-free UTF-8 intermediate JSON, formats the Markdown report, prints it to stdout, saves a timestamped copy under `Saved/Orchestrator/gate/`, and exits non-zero on any `FAIL`.

## Claim | Evidence

| Claim | Evidence |
|---|---|
| Worktree + `gemini/<NNN>-*` Location check rejects main checkout and wrong branches | `Scripts/Tools/gemini_gate.py:132-164`; `test_location_worktree_and_branch` PASS in `unittest` output below |
| Allowed paths parser enforces inbox allowlist and always blocks forbidden assets/dirs/packs | `Scripts/Tools/gemini_gate.py:178-417`; 3 unit tests (`test_allowed_paths_*`) PASS in `unittest` output below |
| Build + tests and Jev audit run and capture command tails without PowerShell ErrorRecord noise | `Scripts/Tools/gemini_gate.ps1:38-49`, `Scripts/Tools/gemini_gate.ps1:87-183`; live gate run on task `036` ran 9 unittests (`OK`) and `typesafe_triage.py --audit-diff --agent gemini` (`exit 0`) |
| Citation check catches missing files and out-of-range line numbers; Evidence check requires populated table | `Scripts/Tools/gemini_gate.py:491-647`; `test_citation_check_valid_and_broken` and `test_evidence_check_tables` PASS; dry-run without report returned `Citation check FAIL` + `Evidence check FAIL` |
| Sonnet review parses latest weekly budget cap and fails only on HIGH findings without printing keys | `Scripts/Tools/gemini_gate.py:650-748`; `test_budget_and_sonnet_severity` PASS (`docs/port/PLAN_WEEK_2026-11-16.md:1-32` -> cap `64.61`) |
| Final gate verification on `gemini/036-gate` passes all active checks | `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 036` exited `0` (`PASS`; 36 citations verified; 9 unittests OK; Jev exit 0) |

## Verification Logs

### Python unit tests
Command: `python -m unittest discover -s Scripts/Tools/tests -p "test_*.py" -v`
Run result: `Ran 9 tests in 0.315s — OK` (`7` `GeminiGateTests` + `2` `TaskBoardTests`).

### Gate output (`Scripts/Tools/gemini_gate.ps1 -Task 036`)
```markdown
# Gemini Gate `036` - **PASS** (2026-11-12 15:16:59)
- Branch: `gemini/036-gate` vs `main`

| Check | Status | Details |
|---|---|---|
| Location | **PASS** | Worktree `CodexTactics-gemc5`, branch `gemini/036-gate` |
| Allowed paths | **PASS** | 5 file(s) checked (no Allowed paths header; non-Source tooling/outbox scope) |
| Build + tests | **PASS** | Python unittest PASS |
| Jev audit | **PASS** | PASS: Clean workspace: No changes to audit. |
| Citation check | **PASS** | 36 citation(s) verified |
| Evidence check | **PASS** | Evidence table verified (1 report file(s)) |
| Sonnet review | **SKIP** | Not requested (pass -SonnetReview to enable) |
```
