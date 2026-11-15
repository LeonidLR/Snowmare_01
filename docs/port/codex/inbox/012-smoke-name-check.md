# 012 - Smoke registry name check (Claude, 2026-11-15 night) - LIGHT, Python only
Why: a smoke name in Scripts/test_map.json that has no matching console command silently times out (it happened with
"PmcCombatBugsSmoke", a file name, not a command). Catch that before a run.
Branch codex/012-smoke-name-check in CodexTactics-cxnight (`git switch -c codex/012-smoke-name-check main`).

Do: `Scripts/Tools/check_smoke_names.py` - reads every smoke name in Scripts/test_map.json (smokeRegistry, smokeOptions keys, every
"smokes": [...] list in the impact map, knownFail / knownFlaky), scans Source/**/Debug/*.cpp for the registered console command names
(find how they are registered first - grep one known smoke, e.g. VaultSmoke), prints every name with no command and exits 1 if any.
Add a short unit test under Scripts/Tools/tests/ (same style as the existing ones there) with a tiny fixture JSON + fake .cpp.
Then run it on the real repo and LIST the unknown names in the report - do NOT edit test_map.json (Claude decides).
Allowed paths: Scripts/Tools/check_smoke_names.py, Scripts/Tools/tests/test_check_smoke_names.py (+ fixtures dir next to it), report.
Checks: `python -m pytest Scripts/Tools/tests/test_check_smoke_names.py -q` (or the runner the other tests use), the real run output.
Router level expected 2 -> cheaper model.
Report: docs/port/codex/outbox/012-smoke-name-check.report.md.
