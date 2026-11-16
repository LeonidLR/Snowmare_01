# 033 - Gemini usage CLI regression tests

Branch: `codex/033-gemini-usage-tests` (from main).
Code commit: `74a887930c56decbd474a8b669dc584ff9f497af`.

## Changes
- `Scripts/Tools/gemini_usage.py`: only adds optional `GEMINI_USAGE_LEDGER` and `GEMINI_USAGE_PRICES` environment path overrides. Unset or empty overrides preserve the existing default paths.
- `Scripts/Tools/tests/test_gemini_usage.py`: three standard-library unittest cases execute the actual CLI, assert complete stdout lines and persisted ledger contents, and isolate every prices/ledger path under a TemporaryDirectory.
- `Scripts/Tools/tests/fixtures/gemini_usage_noisy.log`: noise, invalid braces, an earlier stats object, an unrelated JSON object, then final multi-model stats. Covers explicit fresh input, prompt-minus-cached fallback, candidates plus thoughts, and request aggregation.

Priced case checks cached tokens use their own price ($6.375 per run), printed rounding ($6.38), and a second run appends rather than replacing the ledger (5,100,000 tokens, $12.75). No-JSON case reports tokens n/a and creates no ledger. Tests make no API calls; the required Jev audit separately uses its configured service. No Unreal build or smoke was needed for this Python-only scope.

## Routing
Parent supplied router classification: level 2, confidence 0.86.
`2 | gpt-6.1-sol | pass` (no redo).

## Validation
`python -m unittest Scripts.Tools.tests.test_gemini_usage -v` ran once: **3 tests PASS**, four actual CLI subprocess invocations, 0.718 s. Output:

```text
test_cached_price_and_existing_ledger_accumulate (Scripts.Tools.tests.test_gemini_usage.TestGeminiUsage) ... ok
test_no_json_reports_unavailable_without_writing_ledger (Scripts.Tools.tests.test_gemini_usage.TestGeminiUsage) ... ok
test_noisy_log_reports_final_stats_and_writes_isolated_ledger (Scripts.Tools.tests.test_gemini_usage.TestGeminiUsage) ... ok

----------------------------------------------------------------------
Ran 3 tests in 0.718s

OK
```

`git diff --check` and `git diff --cached --check`: PASS.
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: staged all three code/fixture/test paths; **PASS**, risk 0.0/4, before code commit.

Existing untracked imported assets/config/cache were left untouched. No push, merge, main write, editor operation, or protected-path change. Root reviewed the limited path overrides, CLI tests and fixture and ran the required audit before the final report commit. No open implementation questions.
