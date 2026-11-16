# 033 - Tests for the Gemini CLI usage report (Claude, 2026-11-16) - LIGHT, Python
Why: Scripts/Tools/gemini_usage.py (token / cost line per Gemini CLI run, night ledger) has no tests; the night runner depends on it.
Branch codex/033-gemini-usage-tests (from main), worktree CodexTactics-agents.
Do: Scripts/Tools/tests/test_gemini_usage.py (unittest style like the others): a fixture log with noise + a final JSON object with
stats.models.<m>.tokens {prompt, cached, input, candidates, thoughts} and api.totalRequests -> the printed line has fresh/cached/out/
requests right; no JSON -> "tokens n/a"; prices file with cached_per_1m -> cost computed; ledger written to a TEMP path (add an env or arg
override for the ledger + prices paths if needed - keep the default behaviour unchanged). No API calls.
Allowed paths: Scripts/Tools/gemini_usage.py (path override only), Scripts/Tools/tests/test_gemini_usage.py (+ fixtures),
report docs/port/codex/outbox/033-gemini-usage-tests.report.md. 10 % limit rule.
