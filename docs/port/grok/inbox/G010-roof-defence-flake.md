# G010 - PmcRoofDefenceSmoke passes only 6 of 10 runs on main after G009 (guard share / spread)
Dispatched by the lead 2026-11-19 (roadmap 19c: "Grok after G009: the guard regression, if G009 does not fix it").
Branch `grok/G010-roof-defence-flake`. Worktree `CodexTactics-grok`. Base: origin/main newest (540e2ec4 or later: G009 + T-04 merged).
Limit rule: at 10 % of your limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Report: `docs/port/grok/outbox/G010-roof-defence-flake.report.md`; FINISH line in chat topic `grok-G010` (TASK_TEMPLATE format).
Model: grok-4.7.

## Evidence (lead, codex-worker, main be5f1ece = G009 merged, 10 repeats of the same configuration)
`Scripts/Tools/worker_seed_batch.ps1 -Preset RoofDefence10` (PmcRoofDefenceSmoke, L_Gen_Depot_1, L_PmcArenaDepot.json, fixed seed):

| run | result | spread m | guard share | winner / end |
|---|---|---|---|---|
| 01 | FAIL | 4.7 | 0.55 | A elimination |
| 02 | PASS | 7.2 | 0.72 | B elimination |
| 03 | PASS | 10.2 | 0.95 | B objective |
| 04 | PASS | 7.1 | 0.88 | B elimination |
| 05 | FAIL | 3.6 | 0.64 | B elimination |
| 06 | PASS | 9.7 | 0.93 | B elimination |
| 07 | FAIL | 7.9 | 0.41 | A elimination |
| 08 | PASS | 13.5 | 0.95 | A elimination |
| 09 | PASS | 12.7 | 0.95 | A elimination |
| 10 | FAIL | 10.1 | 0.55 | A elimination |

Thresholds (G008 plan): mean spread >= 5 m, guard share > 0.6, shared_cover_pairs = 0. Before G009 it was red on main
(guard 0.48 / 0.29, spread 4.8 / 3.0). The same seed gives different rounds (-Multiprocess timing), so the smoke is a coin flip.

## Task
1. Diagnose why the guard share drops (0.41-0.55 in runs 01 / 07 / 10) and why spread collapses (3.6-4.7 in 01 / 05): which order
   takes the access guard away from the roof access (the round's decision events / the track file), and whether it is a
   real behaviour gap (the defenders leave the access while the attackers are coming) or a measurement artefact (the guard
   dies early and the share counts dead time; the round ends early, etc.).
2. Fix the cause in the PMC defence rules (G008 / G008b / G009 code). If it is a measurement artefact, fix the metric with a
   reason in the commit; never just lower a threshold.
3. Prove it: run PmcRoofDefenceSmoke 10 times on codex-worker (the lead runs it for you: put the exact ref + command in the
   report, or build with `Scripts/build.ps1 -Remote worker` and smoke with `Scripts/smoke_parallel.ps1 -Remote on`); target
   10/10 PASS (at least 9/10 with the failing run explained). PmcArenaTacticalSmoke, PmcStalemateSmoke, TacticalPmcSmoke,
   PmcArenaSmoke stay PASS.

## Never
Besides the common lines (roadmap section 6): do not touch the designer's uncommitted X6 files, Content/Maps, DefaultScalability.ini,
Content/VFX; no change to attacker scoring; no threshold lowered without a measured reason.

## Allowed paths
`Source/CodexTactics/{Public,Private}/AI/Pmc*`, `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp`, `Source/CodexTacticsTests/Private/AI/`,
`Content/Data/AI/pmc_commander.json` (comment every changed number), `docs/port/grok/`.
