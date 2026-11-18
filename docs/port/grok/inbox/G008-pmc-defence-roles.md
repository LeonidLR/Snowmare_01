<!-- Grok rules: docs/port/grok/README.md; audit --agent grok. Designer decision 2026-11-18: after G007, before G005. -->
# G008 - PMC defence P1: defender roles (access guard, sectors, spacing), Objective as an area - L3, C++
Base: main (after G007 is merged; rebase on main first). Branch grok/G008-pmc-defence. Worktree CodexTactics-grok.
Spec: docs/design/PMC_DEFENCE.md sections Diagnosis, D1 and D4 (only `defender_spread_m` + `guard_near_access_share` in this task).
Designer clip notes: docs/port/reports/designer-tactics-notes-2026-11-18.md (notes 1, 1b, 2 cover part).
Problem: in arena objective rounds all PMC defenders walk into the capture circle and bunch in one roof corner (Objective 17-23 % of
orders); nobody guards the roof access; approaches are not covered.
Do:
1. PLAN first (docs/port/grok/outbox/G008-pmc-defence.plan.md, post it, wait for Claude's OK): where roles are assigned in
   PmcCommanderRules / PmcSquadSubsystem, how you read the roof access from TacticalMapSubsystem (`overwatch_roofs[].access[]`,
   `access_kind`, `ladder_tops`), the sector split, which new weights / numbers go to pmc_commander.json.
2. Implement D1: roles at round start (1 access guard, rest sector shooters; re-assign on death), access-guard post (inside access:
   face the hatch / stair head within guard_dist_m; outside ladder: roof edge above the ladder top, fire sector covers ladder + street),
   sector shooters on edge spots in their sector, shifting along the edge (existing Relocate with a sector constraint), defender
   Objective = stay inside the defended area (score 0 while inside), cover spacing (min_spacing_m, spacing_penalty).
   All numbers in pmc_commander.json; pure rules in *Rules.h/.cpp with unit tests (CodexTactics.AI.PmcDefence.*).
3. Arena metrics `defender_spread_m`, `guard_near_access_share` in the arena result JSON (append to the G002 schema doc).
4. Smoke PmcRoofDefenceSmoke (registry test_map.json): Depot_1 roofs scenario, defenders: mean spread >= 5 m, a defender within
   guard_dist_m of the access > 60 % of the round, no two defenders in one cover object. Plus an outside-ladder roof case on X4 if the
   tactical map has one (else note it).
Allowed paths: Source/CodexTactics/{Public,Private}/AI/Pmc*, the PMC part of the arena result writer, Content/Data config
pmc_commander.json, Source/CodexTacticsTests/Private/AI/Pmc*, Source/CodexTacticsDev/Private/Debug/PmcRoofDefenceSmokeCommand.cpp,
test_map.json registry entry, docs/port/grok/outbox/G008-*, docs/port/arena schema doc. TacticalMapSubsystem: read-only (ask in chat).
Gate: PmcRoofDefenceSmoke 3/3, PmcArenaTacticalSmoke, PmcArena* smokes, TacticalMapSmoke, unit tests, test.ps1 -Smart, audit --agent grok.
Report: docs/port/grok/outbox/G008-pmc-defence.report.md (numbers before / after: spread, guard share, Objective share). FINISH line.
</content>
</invoke>
<invoke name="Bash">
<parameter name="command">cd "C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics"; cat docs/port/grok/README.md | head -30