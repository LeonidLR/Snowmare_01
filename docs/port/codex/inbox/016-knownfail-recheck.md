# 016 - Re-check every knownFail / knownFlaky smoke on current main (Claude, 2026-11-15 night) - LIGHT tokens, long run time
Why: Scripts/test_map.json carries knownFail / knownFlaky entries; some may pass now (stale entries hide nothing, but block cleanup),
some may fail differently than their reason says.
Branch codex/016-knownfail-recheck (CodexTactics-cxnight, from main). NO code changes; only the report (and no test_map edit).

Do:
1. List every name under knownFail and knownFlaky in Scripts/test_map.json with its reason (a small python one-liner, paste the list).
2. Run each one 3 times: `smoke_parallel.ps1 -Commands <Name> -NoRetry -Jobs 1` (3 runs) - long but cheap: paste ONLY the result line
   per run. If the table says knownFail, read the FAIL lines from Saved/Logs/Smoke-<Name>.log (max 3 lines per smoke).
3. Report table: Name | listed as | reason in test_map | runs (P/F) | first FAIL line | matches the reason? (yes / no / passes now).
Skip RetreatFacingSmoke if 014 already covered it (say so).
Report: docs/port/codex/outbox/016-knownfail-recheck.report.md.
