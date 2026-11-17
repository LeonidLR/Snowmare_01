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

## 6. P1 implemented (Claude, 2026-11-17, branch `claude/tactical-map-p1`) - the contract for P2 / P3 / P4
**Export (Training Ground).** `Tools/TrainingGround/src/levelgen/tacticalMap.ts` (`tacticalMapFor`, `writeTacticalMapFile`; tests
`tacticalMap.test.ts`) turns the layout's `analysis` into `Content/Data/LevelGen/tactical/<Map>.tactical.json`. The layout's own analysis is
used when its `geometry_hash` is current, else it is computed exactly as «Принять» does (`analyzeLayout`, the config's default conditions -
clear / day). «Принять» and «Собрать в Unreal» write it next to the objectives file (`src/levelgen/node/runtimeFiles.ts` from
`server/levelgenApi.ts`; a failure never blocks them). Existing maps: `node scripts/export-tactical-map.ts [<layout.json> <Map> ...]`
(a layout without `objectives` first gets the automatic goals, like `export-mission-goals.ts`). Committed: `L_Gen_Test_X2` (26 places),
`L_Gen_Depot_X4` (46 places / 7 drop spots / 163 windows / 22 roofs / 150 hiding spots / 7 ladder-only roofs), `L_Gen_Depot_1_X6` (55 places).

**File `codex.tactical_map/1`** (world cm; x, y as on the plan; z = `ground_z` + layer level x 300 - the missionGoals.ts mapping):
`map`, `layout`, `geometry_hash`, `analysis_version`, `analysis_source` (layout | computed), `conditions`, `ground_z`, `thresholds` (the
analysis thresholds + `ladder_only_factor` 0.65), and the lists (one compact item per line):
- `chokepoints[]`: id, kind, kind_name, tags, score, layer, `pos` xyz (narrowest point), `line` xyz[], `poly` xy[] (on pos.z), length_m,
  width_m, lane_m, route, sources, `alternative` (none = only path / detour / n/a), `detour_m`, ids of its drop_spots / windows / overwatch /
  hiding_spots, reveal_m, reveal_stealth_m.
- `drop_spots[]`: id, surface, kind (roof / wall_top), `layer` (roof:B / wall:W), `edge` xyz (on the roof / wall top), `land` xyz (street),
  drop_cm, gap_cm, access, `ladder_only`, jumpers, line, places.
- `windows[]`: id, building, floor, layer (ground / floor:B:n), kind, `pos` xyz (the window, floor z), `stand` xyz (60 cm inside), `yaw`
  (outward look), covered_m, min_dist_m, places.
- `overwatch_roofs[]`: building, layer, level, z, height_cm, range_m, access[] (stair:/ladder:/hatch:), `access_kind`, `ladder_only`,
  `spots[]` {pos xyz, facing_yaw, covered_m, longest_los_m}, places.
- `hiding_spots[]`: id, layer, pos xyz, facing_yaw, reveal_normal_m, reveal_stealth_m, stealth_only, blind, rush_m, cover, cover_height_cm,
  `threatens` {line, route?, pos xyz}, place.
- `ladder_only_roofs[]`: every walkable roof reached by ladder(s) and no stair - building, layer, z, center xyz, poly, ladders, ladder_tops
  xyz, overwatch (is an overwatch roof), drop_spots.

**Runtime (C++).** `UTacticalMapSubsystem` (`Source/CodexTactics/{Public,Private}/LevelGen/TacticalMapSubsystem.*`, world subsystem, Game /
PIE) loads the file at BeginPlay; no file = an empty map (danger 0, every list empty, no warnings). Pure rules + structs:
`LevelGen/TacticalMapRules.*` (tests `CodexTactics.LevelGen.TacticalMap.*`). Query settings (radii / weights / smoke tolerances):
`Content/Data/LevelGen/tactical_runtime.json` (`codex.tactical_runtime/1`). Queries (all world cm; "same floor" = |dz| <= 150):
- `GetZonesNear(point, radius)` -> place indices within radius of their outline, nearest first (then score).
- `DangerAt(point, side)` -> {Danger 0..1, ZoneTerm, ZoneId, Kinds}. Place term = score inside the outline, fading to 0 over 3 m outside.
  Side Squad adds threats (max per kind): drop landing reach (x 0.65 for ladder-only roofs) 0.6, window sector +-75 deg within 60 m (full to
  28 m) 0.35, any roof spot within its range 0.1, a hiding spot's threatened point within 3 m 0.4 (stealth-only x 0.5). Danger =
  1 - prod(1 - term). Side Pmc = the place term only. No runtime line traces.
- `BestAmbushSpots(start, end, n)` -> hiding spots whose threatened point is within 8 m of the segment; score = 0.5 x short reveal + 0.3 x
  position along the segment (the EXIT wins) + 0.2 x its place's score, x 0.5 stealth-only. Best first.
- `OverwatchSpots(target, radius, n)` -> (roof, spot) pairs whose range reaches the target; 0.5 x nearness + 0.5 x coverage.
- `WindowPositions(target, radius)` -> windows whose sector and range reach the target area; nearest first (use `stand` + `yaw`).
- `DropSpotsAbove(center, radius)` -> drop spots landing within radius (same floor); nearest first (the jumper goes on `edge`).
- `IsChokepoint(path, &index)` / `ChokepointsOnPath(path)` -> places (score >= min_score) the polyline runs through, in path order.
- `SafeDetour(from, to)` -> the highest-scored place on the straight way + its data: `bOnlyWay` (alternative none), `bTakeDetour`
  (a detour <= 40 m), `DetourM`. No new path search (simple version).
- `PickLadderOnlyRoofs(seed, factor = file's 0.65)` -> indices of `ladder_only_roofs` that get an enemy: each roof independently when
  `SeededRoll(seed, building id) < factor` (FNV-1a + murmur finaliser; deterministic, order independent, ~65 % on average).
Debug: `Codex.TacticalMap.Draw 1` (places red by score, drop arrows orange, windows cyan, roof spots magenta, hiding spots green / thin line =
stealth-only, ladder-only roofs yellow, within 90 m of the view); `Codex.TacticalMap.Dump`. Smoke `CodexTactics.TacticalMapSmoke` on
`L_Gen_Depot_X4` (registered in `Scripts/test_map.json`, rule `tactical-map-2026-11-17`).

**For P2 (PMC placement / commander terms).** Ambush at a chokepoint EXIT = `BestAmbushSpots(squad route segment)` and stand on the spot's
`pos` facing `facing_yaw`; marksmen = `OverwatchSpots` (`spots[].pos` + `facing_yaw`); riflemen at windows = `WindowPositions` -> `stand` +
`yaw`; jumpers = `DropSpotsAbove(route point)` -> `edge`; ladder-only roofs = `PickLadderOnlyRoofs(level seed)` -> `center` or a
`ladder_tops` point; never stand IN a chokepoint = `DangerAt(p, Pmc)` as a cost term (weights belong in pmc_commander.json).
**For P3 / P4 (arena metrics).** "Time PMC stood inside a chokepoint" = `GetZonesNear(p, 0)` non-empty; "ambush success at chokepoint exits"
= kills from a `BestAmbushSpots` spot; "window positions used" = a PMC within ~1 m of a window `stand`; "overwatch kills" = kills from an
`OverwatchSpots` spot. **Not done in P1:** no runtime line of sight (window / roof terms are geometric), no path search for the detour, only
the clear / day analysis is exported, floors / roofs are not analysed as squad positions (the analysis is ground only).
**Verified (2026-11-17).** Build OK; `Scripts/test.ps1 -Filter CodexTactics.LevelGen` 76 / 76 (10 new TacticalMap tests);
TacticalMapSmoke on L_Gen_Depot_X4 - counts = JSON, every spot (46 places, 7 drop land + 7 edges, 163 window stand points, 44 roof
spots, 150 hiding spots + 150 threatened points, 14 ladder-roof points) on the navmesh or a walkable surface, danger 0.97 in CP_01 vs 0.10
on the best open street, 65 % of the ladder-only roofs picked over 200 seeds; MissionGoalsSmoke and LevelGenRouteSmoke PASS;
Training Ground typecheck OK, `npm test` 455 / 457 (the two failures - movement golden `formation_Slots.json` stale, a combatSim timing
check under build load - are outside this change).
