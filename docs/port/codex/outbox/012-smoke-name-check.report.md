# 012 - smoke name check

Tool: `Scripts/Tools/check_smoke_names.py [--repo <path>]` (exit 0 = OK, 1 = unknown names).

- Names collected from `Scripts/test_map.json`: `smokeRegistry`, keys of `smokeOptions`, `knownFlaky` / `knownFail`, and every `"smokes": [...]` list found by a recursive walk (fallback, rules, ...). Keys starting with `_` are ignored; a `CodexTactics.` prefix is stripped.
- Commands detected: regex over `Source/**/Debug/*.cpp` for `FAutoConsoleCommand*(TEXT("CodexTactics.X")...)` / `RegisterConsoleCommand(TEXT(...))`. `smoke_parallel.ps1` runs `CodexTactics.<Name>`, so the JSON short name must equal the registered name minus the prefix.
- Output: `UNKNOWN smoke: <name>  (in <section>)` per miss, then a FAIL line; otherwise one OK line.

Tests: `python -m unittest Scripts/Tools/tests/test_check_smoke_names.py` (pytest is not installed here)
```
Ran 2 tests in 0.041s
OK
```
Fixture: `Scripts/Tools/tests/fixtures/check_smoke_names/`.

Real repo run (279 distinct names checked, 331 commands found):
```
OK: every smoke name in Scripts/test_map.json has a registered console command
```
Unknown names: none (the earlier PmcCombatBugsSmoke is no longer in the map).
