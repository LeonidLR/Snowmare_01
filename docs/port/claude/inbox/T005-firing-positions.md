# T-05 - Огневые позиции, из которых реально можно стрелять (исполнитель: субагент Sonnet, дизайн - ведущий)
Smart enemies T-05. Designer go 2026-11-19 ("Давай T-05, огневые позиции") after the day-loop digests below.
Owner on the board: claude. Executor: Sonnet subagent (lead designs + reviews). Branch `claude/firing-positions`.
Worktree `CodexTactics-agents` (CodexTactics-claude is busy with T-03). Status lines go to chat topic `claude-T005`.

Binding spec: roadmap `docs/design/gdd/smart-enemies-roadmap.md` section **T-05** + the "Common to every task" lines of its
section 6; GDD `docs/design/gdd/smart-enemies.md` section 5 (firing positions table, R-B rules), F2, hard rule 5 (section 4).
This file adds the lead's design decisions and a phase b. Where they differ, this file wins (the lead's call).

## Why now (day-loop evidence, codex-worker, main 6add8483, 30 seeds each, Depot stand-in, self-play attackers)
| scenario | on_position_before_contact p50 | open idle before contact p50 | crossfire p50 | spread p50 |
|---|---|---|---|---|
| A-00A crossroads (PmcCrossroadsSmoke) | 0.0 | 3.3 s | 11.7 deg | 4.9 m |
| A-00B alley (PmcAlleySmoke) | 0.0 | 1.7 s | 24.5 deg | 10.3 m |
X4 tactical (10 seeds): overwatch_kills 0 in every round (the old "0 shots from 203 overwatch posts").
Folders: Saved/Logs/Worker/dayloop-crossroads-1, dayloop-alley-1, x4-t04metrics-540e2ec4 (main checkout; digest with
`python Scripts/Tools/dayloop_digest.py <folder>`).

## THE baseline T-05 is judged against (scripted opponent, main 3f5cc62a, main PC local run 11-19, 30 seeds each)
| scenario | on_pos p50 | open idle before contact p50 | crossfire p50 | first-shot gap p50 | spread p50 | seeds failing >= 3 criteria |
|---|---|---|---|---|---|---|
| A-00A crossroads | 0.0 | 13.0 s | 9.1 deg | 0.7 s | 4.75 m | 25 / 30 |
| A-00B alley | 0.0 | 8.8 s | 14.2 deg | 2.65 s | 6.85 m | 26 / 30 |
(Saved/Logs/Worker/baseline-script-crossroads, baseline-script-alley on the main PC.) Phase b is accepted when on_pos p50 >= 0.67,
crossfire p50 >= 45 deg and open idle p50 <= 2 s on BOTH scenarios, 30 seeds, same command.

## Lead design decisions
1. **Data.** `FFiringPosition { Kind (Corner, LowCover, Window, RoofEdge, StairHead, ObstacleTop), StandPoint, Stance,
   FacingYaw, CoveredShare (0..1), CoverKey (actor / object id, for spacing), SourceId, bValid }` in `AI/FiringPositionRules.h`.
2. **Pure rules** `FiringPositionRules` (no UWorld): `ScoreF2(inputs)` exactly as GDD F2 (fire * protect * escape *
   (1 - w_danger * danger) * spacing * role_fit), `CrossfireOk(H1, H2, K, MinDeg)` (angle >= crossfire_min_deg 45 and
   neither in the other's line of fire), `CoveredShare(hits, samples)`, `PickCrossfirePair / PickN` (greedy best F2,
   then the best partner that satisfies crossfire, then spacing). Numbers in a new JSON block `firing_positions` in
   `Content/Data/AI/pmc_commander.json` (comment every number; frozen, not trainer genes): `crossfire_min_deg` 45,
   `area_samples` 9 (3x3 grid over the area, chest height 120 cm), `min_covered_share` 0.34, `rim_step_max_cm` 80,
   muzzle heights per stance (read them from the operative's existing stance data if they exist; else stand 150 / crouch 100 / kneel 110 / prone 30).
3. **Subsystem** `UFiringPositionSubsystem` (UWorldSubsystem). Candidates: cover slots (reuse the slot generation behind
   `UPmcSquadSubsystem::FindCoverNear` through a read helper - do not copy it), tactical-map overwatch roof spots and window
   stand points (TacticalMap read helpers). ObstacleTop: only a seam (enum value + TODO); mantle edge data lives on the
   unmerged building-generator branch.
4. **Validation** = line traces from the muzzle at the firing stance to each area sample (Visibility / the channel the
   weapons use for hit tests - check WeaponReach / FireArc code and use the SAME channel and ignore rules as a real shot).
   Cached per (position, area key) for the round; re-validated only on a geometry event (opening state change, destruction)
   if such an event exists on the event bus, else per round. No per-tick traces; budget: HitchProbeSmoke stays green.
5. **Roof edge.** Diagnose first (task step 1): on one X4 roof (B_hall_n or the roof G008b used) log the muzzle trace of
   the kneeling and standing shot from the current spot: hit actor, hit distance, parapet height. Then the stand point
   steps toward the rim in 10 cm steps up to `rim_step_max_cm` until the trace clears; the step must stay on the roof
   walk surface (navmesh / the roof's floor test). No clear point = the position is dropped and logged as a generator
   fault (`[FiringPos] roof <id>: parapet blocks every stance`) - NEVER shoot through geometry.
6. **Query** `PositionsCovering(AreaCenter, AreaRadiusCm, BandMinCm, BandMaxCm, N, ExcludeCoverKeys)` -> top N valid
   positions by F2 with spacing applied, plus a debug dump (console `CodexTactics.FiringPositions <radius>` on the current map).

## Phases (commit + push after each; report after each)
- **a (spec T-05).** Data, rules, subsystem, validation, roof-edge step, query, unit tests `CodexTactics.AI.FiringPosition.*`,
  `FiringPositionSmoke` on L_Gen_Depot_X4 (100 % of returned positions fire: re-trace each; a marksman placed on a
  returned roof-edge position fires at a target standing in its area). L_Lab_Alley exists only on the worker / bgen
  branch: skip it in the smoke, say so in the report.
- **b (lead addition: use it, so the day-loop numbers can move).** Objective defenders BEFORE contact take firing
  positions instead of the current sector spots when the query returns enough: kill zone K = the objective / the point
  of the attackers' expected approach the G008 defence already computes; hammers = the first two sector shooters get
  `PickCrossfirePair` positions on K; others keep their G008 role (the access guard stays). Behind a JSON switch
  `firing_positions.defence_use` (default true). Call sites only in `PmcSquadSubsystem.*` (the defence post assignment).
  Log `[PmcAI] team N: <name> firing position <kind> ... covers <share> crossfire <deg>`.
- Stop rule: phase a ~1 day of work. If phase b fights the G008 defence state machine (posts re-stamped every refresh,
  walks cancelled), stop, commit what works, report the conflict.

## Gate (build and smoke on codex-worker, never locally unless the worker fails twice)
`$env:CODEX_AGENT='claude'`; `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Remote worker`;
`powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Remote on -Commands FiringPositionSmoke,TacticalPmcSmoke,PmcArenaTacticalSmoke,PmcFireLineSmoke,HitchProbeSmoke,PmcRoofDefenceSmoke,PmcStalemateSmoke`;
unit tests `CodexTactics.AI.FiringPosition` + `CodexTactics.AI.Pmc` (report how/where they ran);
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`. After phase b the LEAD runs the day loop
(crossroads + alley x30) and compares with the table above - put the exact ref in the report.

## Never
Besides the common lines: no shot through an obstacle to move a metric; do not widen `floor_tolerance_cm` / `SpotRadiusCm`;
no per-tick traces for all positions; no attacker scoring change; never touch the designer's uncommitted X6 files,
Content/Maps, Config/DefaultScalability.ini, Content/VFX; never push or merge main.

## Allowed paths
`Source/CodexTactics/{Public,Private}/AI/FiringPosition*` (new), `PmcSquadSubsystem.*` (call sites + a read helper for
cover slots), `Source/CodexTactics/{Public,Private}/LevelGen/TacticalMap*` (read helpers), `Content/Data/AI/pmc_commander.json`
(new block only), `Content/Data/LevelGen/tactical_runtime.json`, `Source/CodexTacticsDev/Private/Debug/`, `Source/CodexTacticsTests/`,
`Scripts/test_map.json`, `docs/port/claude/`.

Report: `docs/port/claude/outbox/T05-firing-positions.report.md` (diagnosis numbers, what was built, gate results, the
roof kinds that cannot fire = generator findings, open points for T-08).
