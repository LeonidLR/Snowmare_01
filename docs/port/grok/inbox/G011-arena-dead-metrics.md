# G011 - the arena counts overwatch kills and post walks for real (TEST / GAME fix) - LIGHT, C++
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main newest (>= b9507af7). Branch grok/G011-arena-dead-metrics. Worktree CodexTactics-grok.
Before switching branches in the worktree: no UnrealEditor-Cmd / build may run on it (the lead may be gating
claude/squad-holder-yield there right now - ask in chat topic `grok-G011` before you switch, or wait for "worktree free").
Router: L2 (ue-l2-routine, conf 0.69). Use the build-fast model.

## Design gate (lead filled)
- Spec: smart-enemies roadmap docs/design/gdd/smart-enemies-roadmap.md (T-05 proof "overwatch_kills > 0 on X4", T-10 gate) +
  the stand-in report docs/port/claude/outbox/WORKER_LEAD_2026-11-20.report.md: "Dead arena metrics: owKills and postMoves are 0 in
  all 750 rows - fix before the T-10 gate relies on them."
- Acceptance: in a PMC arena round where a defender on an overwatch / tactical post kills an attacker, the round JSON
  (Saved/Arena/*.json, schema codex.pmc_arena/1) has overwatch_kills >= 1 for that side; when a squad member walks to a post,
  post_moves >= 1. Rounds without such events stay 0.
- Proved by: (1) a unit test on the pure counting rule (PmcArenaBehaviourRules or a new small rules function: given a kill note
  with the killer's post kind -> counts / does not count); (2) PmcArenaTacticalSmoke + TacticalPmcSmoke still PASS and their logs /
  JSON show a non-zero value in at least one of 10 seeded runs (report the 10 values).
- Designer decision needed? none.

## Evidence so far
- Writers: Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:2306 (`Team.Stats.PostMoves += bTaken ? 1 : 0;`),
  Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:2049 (`++...Behaviour[Note->Side].OverwatchKills`), JSON at
  PmcArenaCommand.cpp:2480 (post_moves) and 3648 / 3664 (overwatch). Read side: PmcArenaBehaviourRules.cpp:18.
- Hypotheses to check (do not assume): the 2049 branch never matches (wrong note kind / side index / post flag); PostMoves is
  counted on a stats object that is not the one exported (copy vs reference, reset at round start after the walk); bTaken is
  always false for the order the arena issues.

## Task
1. Diagnose with evidence: log lines from one seeded round (`PmcArenaTacticalSmoke`) showing why each counter stays 0.
2. Fix the counting, not the metric's meaning. Pure logic -> rules function + unit test (CodexTactics.AI.PmcArena.* naming).
3. Stop rule: > ~2 h -> diagnosis + exact next step, commit WIP.

## Never
- Change / weaken a smoke or test check, its timing or fixture. Change AI behaviour (only counting). Touch maps, Content/VFX,
  Config/DefaultScalability.ini, Content/Data/AI/pmc_*.json values. Push or merge main.

## Gate (all before FINISH)
build: `Scripts/build.ps1 -Remote worker` (exit 2 -> local) | `Scripts/test.ps1 -Filter CodexTactics.AI.Pmc` | smokes:
PmcArenaTacticalSmoke, TacticalPmcSmoke, PmcArenaSmoke, PmcArenaObjectiveSmoke via `Scripts/smoke_parallel.ps1` |
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` | `git diff --check`.
Allowed paths: Source/CodexTactics/**/AI/Pmc*, Source/CodexTacticsDev/Private/Debug/PmcArena*, Source/CodexTacticsTests/**/AI/*,
docs/port/grok/**.

## Report
`docs/port/grok/outbox/G011-arena-dead-metrics.report.md` (in Russian, like G010): handover, cause with log lines, file:line
changes, gate results with log paths, the 10 values. FINISH in chat topic `grok-G011`:
`G011 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
