# 094 - a smoke can be run N times for real, so a flake is measured, not guessed (TOOL) - LIGHT, PowerShell
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main newest. Branch gemini/094-smoke-repeats. Worktree CodexTactics-gemday. Starts after 091 (designer 2026-11-20 order:
089 -> 091 -> 094 -> 090). PLAN first in chat topic `gemini-094`.
Push note: the pre-push guard refuses gemini/* changes under Scripts/ (not in the gemini allowlist) - that is expected; commit locally,
post FINISH, Claude merges from the shared local commit. Do NOT edit the hook.

## Design gate (lead filled)
- Spec: none - acceptance here.
- Acceptance:
  1. `Scripts/smoke_parallel.ps1 -Commands PmcStalemateSmoke -Repeat 10 -Jobs 4` runs the smoke 10 times (today line 58
     `$Names = @($Names | Select-Object -Unique)` collapses 10 identical names into ONE run - the lead's 10-repeat PmcStalemate check on
     11-20 silently ran once). Duplicate names in -Commands keep their count too (`-Commands A,A,A` = 3 runs), no -Unique on purpose.
  2. Each run has its own log `Saved/Logs/Smoke-<Name>-r<k>.log` and its own row in the result JSON (`name`, `repeat` k, result,
     seconds); the summary prints per smoke `PASS 7/10 (FAIL r3, r6, r9)`; exit code non-zero if any run failed.
  3. `-RepeatSeeds` (switch): run k gets `-FixedSeed` replaced by a different seed (k) so repeats are not byte-identical under the fixed
     step; without it the runs keep today's clock args (repeats then measure load / timing noise only) - say in the help which is which.
     Find how the engine takes a seed (`-FixedSeed` today, Get-ClockArgs line 70-78); if no per-run seed argument exists, implement only
     the plain repeat and write the finding in the report.
  4. Retry logic: with -Repeat, NO automatic "re-run alone" of failed runs (that would hide the flake rate); without -Repeat today's
     behaviour is unchanged.
  5. Passes through the remote path: `Scripts/Tools/smoke_remote.ps1` and `Scripts/Tools/worker_sync.ps1 -Repeat N [-RepeatSeeds]`
     forward it to the worker, and the result JSON / logs come back (worker_sync fetches SmokeParallel-worker.json + the logs).
- Proved by: a Pester-free self-test the scripts already use, or a new `Scripts/Tools/tests/test_smoke_repeat.ps1` that runs the
  name-expansion / summary functions on fake results (no engine): A x3 -> 3 rows; summary text; exit code. Plus ONE real run on this PC:
  `Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke -Repeat 3 -Jobs 3` -> 3 logs, 3 rows (paste the summary).
- Designer decision needed? none.

## Never
- Change any smoke's C++ check, timeout or fixture. Change test_map.json semantics for existing callers. Touch C++, maps, Content.
- Run the worker yourself (Claude runs worker_sync). Push or merge main. Edit .git/hooks.

## Gate (all before FINISH)
the self-test + the 3-run local MovementSmoke | `powershell -File Scripts/smoke_parallel.ps1 -Remote off -Commands MovementSmoke`
(no -Repeat: output and exit code as before) | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: Scripts/smoke_parallel.ps1, Scripts/Tools/smoke_remote.ps1, Scripts/Tools/worker_sync.ps1, Scripts/Tools/tests/**,
docs/port/gemini/**, docs/port/LANES.md (one usage line).

## Report
`docs/port/gemini/outbox/094-smoke-repeats.report.md`: handover, usage examples with output, what changed (file:line), gate results.
FINISH in chat topic `gemini-094`: `094 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
