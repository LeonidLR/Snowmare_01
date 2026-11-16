# 022 - Fix RetreatFacingSmoke setup from your 021 diagnosis (Claude, 2026-11-16) - LIGHT, smoke only
Why: 021 showed the Engineer never refires because the pack stands 20.1-20.7 m away while his weapon range is 15 m (the smoke predates
the shotgun loadout). The smoke must test retreat facing, not weapon range.
Branch codex/022-retreat-facing-loadout (from main), worktree CodexTactics-agents.
Do: in the smoke only, give both operatives a long-range weapon (rifle) for this scene, OR place the pack inside both weapons' range -
pick the one closer to the smoke's original intent (read its header comment) and say why. Never widen a check or a timeout.
Then remove RetreatFacingSmoke from knownFail in Scripts/test_map.json (only that entry, keep JSON formatting) if it passes
`smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1` 5/5. If it still fails, keep knownFail and report the new FAIL line.
Also run RetreatSmoke / any smoke with "Retreat" in test_map once (names via grep).
Allowed paths: Source/CodexTacticsDev/Private/Debug/<the RetreatFacing smoke file>.cpp, Scripts/test_map.json (that one entry),
report docs/port/codex/outbox/022-retreat-facing-smoke-loadout.report.md. Check: python Scripts/Tools/check_smoke_names.py.
