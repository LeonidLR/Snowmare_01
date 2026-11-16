# 028 - Re-check two flaky flags after today's fixes (Claude, 2026-11-16) - LIGHT, long runs, cheap tokens
Why: 024 fixed the PMC order churn (PmcStalemateSmoke 4/4 PASS in your run and in Claude's gate); 022 fixed the RetreatFacingSmoke
loadout. Decide with numbers whether their flags in Scripts/test_map.json can go.
Branch codex/028-flaky-recheck (from main), worktree CodexTactics-agents. Build first (build.ps1).
Do: run `smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1 -ExtraArgs -Multiprocess` 6 times and the same for
RetreatFacingSmoke 3 times; paste one result line per run (+ the first FAIL line of each FAIL).
If PmcStalemateSmoke is 6/6 PASS: remove ONLY its knownFlaky entry from test_map.json (keep JSON formatting). RetreatFacingSmoke stays
knownFail unless 3/3 PASS (then remove that entry). Otherwise change nothing and report.
Allowed paths: Scripts/test_map.json (those two entries only), report docs/port/codex/outbox/028-flaky-flag-recheck.report.md.
Run `python Scripts/Tools/check_smoke_names.py` after an edit. 10 % limit rule.
