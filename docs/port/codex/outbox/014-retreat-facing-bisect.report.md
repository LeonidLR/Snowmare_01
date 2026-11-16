# 014 - RetreatFacingSmoke recheck / bisect boundary

Branch: `codex/014-retreat-facing`; base/current main tested: `b3d28df6` (includes fire gate `a48e7540` and task 018).

## Result

**needs Claude: establish a reliable good revision / stabilize the intermittent return-to-fire failure before a meaningful bisect.** No culprit commit established and no gameplay fix claimed. The narrow recheck is complete; the broader historical investigation is handed back under the designer's scope rule.

- Current-main build PASS (2.07 s).
- Three serial fresh-process runs, `-NoRetry -Jobs 1`: **FAIL / PASS / FAIL**. The wrapper exits 0 for the failures because they are knownFail; actual log assertions were checked.
- Both failures: `Smoke FAIL: both fire at the pack again (10.0 s after facing it)`; then `Smoke RESULT: FAIL`.
- Run logs: `Saved/Logs/codex014-run1.log`, `codex014-run2.log`, `codex014-run3.log`; wrapper logs alongside them.
- Additional boundary probe: `b93db436` (immediately before preload `4cf88e74`), build PASS (373.77 s), smoke **FAIL** with the same assertion; `Saved/Logs/codex014-prepreload-run1.log`. Thus the immediate pre-preload revision cannot be used as the good endpoint. This does not identify a culprit or exclude every preload effect.
- The knownFail addition is `0175ae00`; its note already records failure with/without 058b and without world pause. The old documented full-pass checkpoint is the 2026-10-22 play-test (`40782ae1` / `88d1299f`), not a recently verified good endpoint. Smoke changes since then include `160f050b` and `617a874b`.
- `RetreatFacingSmokeCommand.cpp:290` is the failed assertion location, **not a demonstrated root cause**. Single-run binary bisection would misclassify current main itself (run 2 passed). A historical bisect needs a repeatable classification rule and verified good endpoint across the older gameplay changes.
- `Scripts/test_map.json` unchanged: 3 consecutive passes were not achieved. No protected files edited. No git bisect state left active; returned to task branch and rebuilding current-main binaries before next task.
- Additional regression batch and focused unit tests not run: no code changed. Required boundary audit recorded before report commit.

Routing: `2 | gpt-6.1-sol | pass (three-run recheck)`; bisect subpart classified level 4 (.65), stopped at the missing reliable good boundary rather than expanding into deep gameplay investigation.
