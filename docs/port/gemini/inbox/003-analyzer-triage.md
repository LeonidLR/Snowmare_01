# 003 - Triage of the 74 high analyzer warnings (READ-ONLY review)

Review task: READ ONLY. Do not edit any file except your report. NO fixes, NO C++ edits, no worktree needed.
Report: `docs/port/gemini/outbox/003-analyzer-triage.report.md` in the main checkout
`C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics`.

## Input
- `docs/port/audit/static_baseline.json` (`warnings`: every MSVC /analyze warning of the first weekly audit, 2026-11-10) and the top
  list in `docs/port/audit/AUDIT_2026-11-10.md`. Most high ones are C6011 "Dereferencing NULL pointer" with a path of lines.
- The source under `Source/` (read with `rg` / the file viewer).

## For EACH high warning decide
- **REAL**: a concrete state where the pointer is null at that line in the game (a level without that actor, a dead fighter, a missing
  asset, an early tick, a destroyed actor...). Give the state in one sentence + `file:line` of where it goes null.
- **FALSE POSITIVE**: why the analyzer is wrong (a `check()` / early return / guaranteed construction it cannot see) + `file:line`.
- **UNSURE**: what would decide it.
Group duplicates (same root cause in one function) into one row.

## Output (the report)
1. A table: id | file:line | verdict | the state (REAL) or the reason (FALSE) | fix idea in one line (for REAL only; Claude writes the fix).
2. The REAL ones ranked by how likely they crash during normal play (top 10 first).
3. Counts: real / false / unsure. Keep it compact; mark guesses as guesses. Unverified claims are useless - every REAL needs the
   concrete null state.
