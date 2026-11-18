# 047 - SuppressionSmoke F: the hound pack does not split round a suppress zone (GAME fix, not the smoke) - MEDIUM, C++
Designer exception 2026-11-18 (today only): you work until 10 % of your DAILY norm is left, then stop, commit, report.
You go offline ~19:00: by then commit everything (WIP is fine) with a handover at the top of the report.
Base: origin/main (newest; contains 046 once Claude merges it). Branch codex/047-suppression-split. Worktree CodexTactics-agents.
Before you switch branches there: no UnrealEditor-Cmd may run on CodexTactics-agents (Claude's 046 smokes) - wait until they end.

## Red
`CodexTactics.SuppressionSmoke` stage F (Source/CodexTacticsDev/Private/Debug/SuppressionSmokeCommand.cpp case 51, check ~791):
`F: the pack splits round it (6 left, 0 right)` - night sweep 11-18 (docs/port/reports/worker-night-2026-11-18.md line 31), still red
on fresh main 11-18 afternoon. Design: a SUPPRESS order on a spot opens a fear zone; a hound pack charging through it splits round it
(nobody crosses its core) - so the detour side must be chosen per hound, not one side for the whole pack.

Your 044 change f709796f (moved the hounds 10 m behind the zone, 3 per side) was REJECTED: it made the fixture produce the split
instead of the game. This time the smoke stays exactly as on main - no change to its spawn positions, checks or timing.

## Task
1. Diagnose with evidence (log lines): where the enemy's fear-zone detour picks its side (grep the suppression / fear zone avoidance in
   Source/CodexTactics/Private - EnemyCharacter*, the suppression subsystem / rules, the detour path cost). Why do all six hounds pick
   the same side? (one shared side for the pack, a tie broken by the same sign, the side from the zone centre vs the hound's own lateral
   offset, the slot of the pack formation...)
2. Fix it in the game: each hound picks the side from its own position relative to the line run -> zone centre (lateral offset),
   ties split alternately inside the pack, or an equivalent rule - numbers in the existing suppression JSON, no new literals.
   Pure decision logic -> a small rules function + unit test (`CodexTactics.Combat.Suppression.*`).
3. If the cause turns out bigger than ~1.5 h: stop, write the diagnosis + the next exact step, commit WIP. That is a good result too.

## Gate
build (Scripts/build.ps1 -Remote worker, exit 2 -> local), SuppressionSmoke 2/2, EnemyArchetypeSmoke, PmcArenaSmoke, HoundPack / any pack
smoke you find with grep, Scripts/test.ps1 -Smart, python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex.
Allowed paths: Source/CodexTactics/{Public,Private}/** files of the suppression / fear-zone / enemy detour logic you name in the report,
the suppression JSON under Content/Data, Source/CodexTacticsTests/Private/** (new tests), docs/port/codex/outbox/047-*.
NOT allowed: Source/CodexTacticsDev/** (the smoke), maps, Content/VFX, Config/DefaultScalability.ini.
Report docs/port/codex/outbox/047-suppression-split.report.md. FINISH in topic codex-047. Never push / merge main.
