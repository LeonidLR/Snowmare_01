# 014 - RetreatFacingSmoke red on main: find the culprit commit and fix (board 064)
RetreatFacingSmoke fails on main since about 4cf88e74 ("both fire at the pack again 10 s after facing it"), with and without 058b and
with -PreloadNoWorldPause. It is marked knownFail in Scripts/test_map.json.
Branch codex/014-retreat-facing (CodexTactics-cxnight).

Do:
1. Confirm it is red on current main: `smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry` (paste the FAIL lines from
   Saved/Logs/Smoke-RetreatFacingSmoke.log). NOTE: smoke_parallel honours knownFail - read the log, not only the table.
2. Bisect by builds (`git bisect` in your worktree, build.ps1 + the smoke per step; start: good = the last commit where it passed -
   find it from git log of test_map.json knownFail entry / HANDOFF change log; bad = main). Builds are slow but cost few tokens:
   paste only the result line per step.
3. Report the culprit commit + the exact change that breaks it (file:line) and why.
4. Fix on your branch IF the fix is small and inside the culprit's files (not FireArcRules / OperativeCharacterAim - those belong to
   Claude's 058c/061 work; if the cause is there, stop at the report). Remove the knownFail entry only when it passes 3 runs in a row.
Allowed paths: the culprit's files (say which in the chat before editing), Scripts/test_map.json (only the RetreatFacingSmoke knownFail
entry), RetreatFacingSmokeCommand.cpp, report.
Checks: build.ps1; the smoke 3x; `smoke_parallel.ps1 -Commands CoverSmoke,RetreatFacingSmoke,FireArcSmoke,PmcUnderFireSmoke` (check names
in test_map.json); typesafe audit.
Report: docs/port/codex/outbox/014-retreat-facing-bisect.report.md.
