# Weekly audit (zero Claude tokens)

Branch chore/weekly-audit. Runs every Sunday 02:00 via Task Scheduler; one-page report + health dashboard.

## What it does
1. **Static analysis**: `Scripts/build.ps1 -StaticAnalyzer Default` (UBT `-StaticAnalyzer=Default -StaticAnalyzerProjectOnly`, MSVC `/analyze`; log `Saved/Logs/StaticAnalysis.log`). Only `Source/CodexTactics*` warnings are kept, normalized to file / line / code / message, diffed against `docs/port/audit/static_baseline.json` by file + code + message (first run creates the baseline; later runs list NEW / fixed). Severity: high = null deref / uninitialised / overrun / dangling codes (`HIGH_CODES` in `Scripts/Tools/audit/audit_lib.py`), medium = other C6/C26/C28, low = C4xxx. The build takes the shared lock and refuses while this checkout's editor is open; the audit waits up to 3 h, then skips the analyzer with a note (churn + coverage still reported). A normal build follows so binaries are usable again (`-NoRestore` to skip).
2. **Churn + risk**: `git log main --since="7 days ago" --name-only` -> touches per file; the top 40 changed source files go to Jev in ONE batched `score` call (`jev_client`); no key / API failure -> churn only.
3. **Coverage map** per system (`Source/CodexTactics/{Private,Public}/<System>`): LOC, unit tests (automation macros in `CodexTacticsTests/Private/<System>`), smokes mapped by `Scripts/test_map.json` globs, changes this week. Grey score = (kLOC + changes/5) / (1 + tests/5 + smokes/10); >= 2.0 is a grey zone.
4. **Output**: `docs/port/audit/AUDIT_<date>.md` (Russian summary + "Действия на неделю" 1-3), `audit_<date>.json`, `docs/port/project-health.html` (`Scripts/Tools/build_health_dashboard.py`, self-contained). First Sunday of a month adds "architecture review due".

Action rule (deterministic): new high-severity analyzer warnings (first run: the high baseline) -> the riskiest file changed >= 3 times without unit tests (Jev risk, then changes, then LOC) -> the worst grey zone.

## Run / schedule
```
powershell -ExecutionPolicy Bypass -File Scripts/Tools/weekly_audit.ps1 [-Root <main checkout>] [-Date yyyy-MM-dd] [-SkipAnalyzer] [-AnalyzerLog <log>] [-NoRestore] [-NoJev]
powershell -ExecutionPolicy Bypass -File Scripts/Tools/register_weekly_audit.ps1 [-Time 02:00] [-Unregister]   # registers "CodexTactics Weekly Audit" (run from main)
python Scripts/Tools/audit/test_audit.py                                                                       # self-test
```
The audit output is not committed automatically; commit `docs/port/audit/` and `project-health.html` from main.
