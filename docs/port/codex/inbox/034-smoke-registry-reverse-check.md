# 034 - Reverse smoke registry check: commands that test_map.json does not know (Claude, 2026-11-16) - LIGHT, Python
Why: check_smoke_names.py (012) finds names in test_map.json without a command. Today many NEW smokes were added (MantleDescentSmoke,
MantleClickSmoke, MantleFenceRouteSmoke, FireGateSmoke, FirstAidSmoke?, ...); a smoke command that is NOT in smokeRegistry / the impact map
never runs in `test.ps1 -Smart` or the nightly list.
Branch codex/034-smoke-reverse (from main), worktree CodexTactics-agents.
Do: add a `--reverse` mode to Scripts/Tools/check_smoke_names.py: list every registered CodexTactics.*Smoke console command (same regex)
that is missing from smokeRegistry (and, separately, from any impact-map "smokes" list); exit 1 if any are missing from smokeRegistry.
Unit test with the existing fixture style. Run it on the repo and LIST the missing ones in the report; then add ONLY the missing names to
smokeRegistry in Scripts/test_map.json (alphabetical position, keep formatting) - do not touch impact-map lists or flags.
Allowed paths: Scripts/Tools/check_smoke_names.py, Scripts/Tools/tests/test_check_smoke_names.py (+ fixtures), Scripts/test_map.json
(smokeRegistry additions only), report. 10 % limit rule.
