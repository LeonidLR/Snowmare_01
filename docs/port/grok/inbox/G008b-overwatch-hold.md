# G008b - PMC overwatch posts are held until the attackers are in the post's range (GAME fix) - MEDIUM, C++
Limit rule: at 10 % of your limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main (newest). Branch grok/G008b-overwatch-hold. Worktree CodexTactics-grok.
The worker (codex-worker) is FROZEN for the night trainer until 08:00: build and smoke LOCALLY (worker_sync / build_remote refuse anyway).

## Design gate
- Spec: docs/design/PMC_DEFENCE.md phase P1b (overwatch hold) + D1 (defender roles, G008 merged 80f7d55b).
- Acceptance: a PMC defender placed on an overwatch roof post stays on it (does not walk off to Objective / TakeCover / FallBack /
  Advance / Flank) until a known attacker is within the post's range (`overwatch_roofs[].range_m`, or the weapon reach if smaller) or
  `ambush_max_hold_s` after that in-range contact passed, or he is hit / suppressed / the post becomes untenable (existing FallBack
  reasons); he fires from the post at attackers in range.
- Proved by: the arena metric `overwatch_kills` > 0 and shots-from-post > 0 on L_Gen_Depot_X4 tactical (PmcArenaCommand, scenario
  tactical, 2-3 seeds); PmcRoofDefenceSmoke still PASS (spread >= 5 m, guard near access > 60 %); PmcStalemate / PmcArenaTactical PASS.
- Designer decision needed: none (P1b approved 2026-11-18).

## Evidence so far
docs/port/reports/worker-overwatch-kills.md (worker-ops 2026-11-18, trainer pmc-v6-qd, 552 side-rounds): 203 side-rounds start a defender
on an overwatch post; shots fired within 3 m of the post: 0; time on post p50 17.2 s; first non-Ambush order p50 10.5 s; these men's first
shot anywhere p50 36.8 s. Cause: at t = 0 the team already knows a threat 120-155 m away (post range_m 110) -> Team.bEverContact = true
(PmcSquadSubsystem.cpp:736 / :355) -> Ambush score gated on Step(!S.bEverContact) (PmcCommanderRules.cpp:204) = 0 -> the next decision
walks him off the roof. ShouldHoldFire (:868) is not the cause. The metric itself is correct (IsAtOverwatchSpot, PmcArenaCommand.cpp:1728 / :1765).

## Task
1. Diagnose: confirm the chain above on one local X4 tactical arena run (log lines), then
2. Fix in the commander: a posted overwatch defender's Ambush / hold is gated on contact WITHIN THE POST'S RANGE (new view field, e.g.
   bContactInPostRange, from the squad subsystem), not on bEverContact; `ambush_max_hold_s` counts from that in-range contact; keep the
   existing FallBack / hit / suppression exits. Numbers in pmc_commander.json (+ bounds file entry for any new gene), no new literals.
   Pure scoring change -> unit test in the existing PmcCommander rules tests.
3. Stop rule: if it grows beyond ~2 h, write the diagnosis + next exact step and commit WIP.

## Never
- Change / weaken a smoke or metric check, its timing or its fixture; raise a budget.
- Push or merge main; touch maps, Content/VFX, Config/DefaultScalability.ini, the designer's uncommitted files.

## Gate
build `Scripts/build.ps1` (local) | arena X4 tactical 2-3 seeds before -> after (overwatch_kills, shots from post, time on post p50) |
smokes PmcRoofDefenceSmoke, PmcStalemateSmoke, PmcArenaTacticalSmoke, PmcArenaSmoke via `Scripts/smoke_parallel.ps1 -Jobs 3` |
`Scripts/test.ps1 -Smart` | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok`.
Allowed paths: Source/CodexTactics/{Public,Private}/AI/PmcCommander*, Source/CodexTactics/{Public,Private}/AI/PmcSquadSubsystem*,
Source/CodexTactics/{Public,Private}/AI/PmcTactical*, Content/Data/AI/pmc_commander*.json, Source/CodexTacticsTests/Private/**,
docs/port/grok/outbox/G008b-*.

## Report
docs/port/grok/outbox/G008b-overwatch-hold.report.md: handover, cause with evidence, change (file:line), before -> after numbers, gate.
FINISH in chat topic grok-G008b: `G008b FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
