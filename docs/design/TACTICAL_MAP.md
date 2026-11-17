# Tactical map - bots read the terrain (designer 2026-11-17: "a new level of bot training = reading topography, understanding space and
# the dangerous places, and how to behave in them - work on the macro level")

Status: design v1 (Claude, 2026-11-17). Implements TASK_BOARD F-4 «б» and week plan A4. Owner of the contract: Claude.

## 1. Source: the level generator's danger analysis (already exists)
Training Ground computes per accepted map an `analysis` block (docs/port/LEVELGEN_LAYERS_REPORT.md §5-6): `chokepoints[]` (kind:
narrow / clutter / canyon / single route / drop ambush / window fire / hidden enemy, score 0-1, line / poly, width, detour),
`dropAmbushSpots[]`, `windowsCovering[]`, `overwatchRoofs[]` (access stairs / ladder / none, spots with look direction), `hidingSpots[]`
(reveal distances, stealth-only, threatens which line). Thresholds: chokepoints.json (ladder-only roof factor 0.65, designer 11-17).
The game does not read it yet.

## 2. Runtime file + subsystem (C++)
- `Content/Data/LevelGen/tactical/<Map>.tactical.json` (`codex.tactical_map/1`), exported like the objectives file
  (Tools/TrainingGround: «Принять» / «Собрать в Unreal» hook + `scripts/export-tactical-map.ts` for existing maps), WORLD space (the same
  layout -> world mapping as `missionGoals.ts`), floor / layer per item, `geometry_hash` of the layout.
- `UTacticalMapSubsystem` (world subsystem, pure rules in `TacticalMapRules`): loads it at BeginPlay (no file = empty map, everything
  keeps working), queries:
  `GetZonesNear(point, radius)`, `DangerAt(point, side)` (0-1 + kinds), `BestAmbushSpots(route segment, n)`, `OverwatchSpots(target area)`,
  `WindowPositions(covering area)`, `DropSpotsAbove(area)`, `IsChokepoint(path)`, `SafeDetour(from, to)`.
  Debug: `Codex.TacticalMap.Draw 1` draws zones / spots; `Codex.TacticalMap.Dump`.

## 3. Behaviour in dangerous places (the macro rules)
| Place | PMC (enemy) side | Player squad side (bot / formation hints) |
|---|---|---|
| Chokepoint / single route / canyon | ambush at its EXIT (hiding spot facing the exit), one marksman on overwatch over it; never stand IN it | cross fast one at a time with overwatch; prefer a detour when it is short (detour_m) |
| Window fire | riflemen take window positions covering the route; leave after being suppressed | cross outside the fire sector, or suppress the window first |
| Overwatch roof | marksmen on the best spots (look direction from the analysis); ladder-only roofs: an enemy on PART of them (probability from the 0.65 factor, seeded) | watch the roof line; do not stop in its long rays |
| Drop ambush | jumpers (cutters / hounds) placed on the roof / wall top above a drop spot, drop when the squad passes the land point | do not hug the wall under a drop edge; watch up |
| Hiding spot | ambushers / PMC in stealth wait there (reveal <= 12 m); stealth-only spots for stealth PMC | slow down / peek near spots with short reveal |
PMC commander: these become CANDIDATE POSITIONS for its existing orders (Ambush, TakeCover, Overwatch, Flank, FallBack, Suppress): the
utility scoring gets terrain terms (danger of my position, cover quality from the analysis, line to the squad's route) with weights in
pmc_commander.json - so the trainer tunes them. Spawning / placement of PMC and level enemies (F-4b): by the same spots.

## 4. Training at the macro level (week plan A2-A5)
Arena scenarios on X2 / X4 / X6 (+ Depot): the squad's route and the PMC's start come from the objectives + analysis. Behaviour metrics per
round (arena result JSON, consumed by the trainer): time in contact, flanks completed, suppress-then-move, ambush success at chokepoint
exits, overwatch kills, time PMC stood inside a chokepoint (bad), window positions used, quiet spells, order churn, stalemate.
Quality-diversity archive (MAP-Elites) over 2-3 of these behaviour axes; league opponents (playtest bot squad + PMC).

## 5. Work split
| Part | Who | Depends |
|---|---|---|
| P1 runtime file export (TS) + UTacticalMapSubsystem + queries + debug draw + smoke `TacticalMapSmoke` | Claude agent (Opus) | - |
| P2 PMC commander terrain terms + placement on spots (ambush at exits, marksmen on roofs, part of ladder roofs, jumpers above drop spots) | Antigravity (knows the PMC code from 077) | P1 |
| P3 arena scenarios on generated maps + behaviour metrics in the arena result | Claude agent / Antigravity | P1 |
| P4 trainer: metrics ingestion, MAP-Elites archive, league, morning API critic | Grok (Python) | P3 result schema (draft it from §4 now) |
| P5 player-squad hints (bot squad uses the same map) | later | P1 |
