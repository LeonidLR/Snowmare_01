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
  `spots[]` {pos xyz, facing_yaw, covered_m, longest_los_m, `gap`? (the firing gap id the shooter stands behind)}, places. Since analysis v2 a spot stands at the roof
  rim (60 cm inside the parapet line) and counts only what lies outward of its edge.
- `firing_gaps[]` (only when the layout has gaps, BUILDING_GENERATOR.md section 8): id, building, layer (roof:B), parapet (wall id), `pos` xyz (gap centre, roof
  floor z), `stand` xyz (60 cm behind it), `outward` [x, y], `yaw`, width_cm, sill_cm (a kneeling muzzle clears the sill, not the 95 cm parapet).
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

## 7. P2 / P3 implemented (Claude, 2026-11-17, branch `claude/tactical-p2`) - PMC on the map + arena behaviour metrics
**Rule: no tactical file = the old behaviour.** `UPmcSquadSubsystem::GetTacticalMap()` is null without a loaded, non-empty file; then the
danger is 0, there are no post candidates, every new term is x1 (test `CodexTactics.AI.PmcTactical.NoMapUnchanged`), the arena's random
scenario pick is the old one (`PickScenarioOnMap(seed, false) == PickScenario(seed)`). L_Gen_Depot_1 (no tactical file) is unchanged.

**P2 - the commander (`AI/PmcTacticalRules.*`, `PmcCommanderRules::ScoreOrder`, `UPmcSquadSubsystem`).** View fields `DangerHere`
(`DangerAt(feet, Pmc)`), `PostKind`, `PostScore`, `bAtPost`. Weights in `pmc_commander.json` (+ `.bounds.json`, trainer genes):
- `terrain_danger` 0.6 - "danger of my position": Hold / Crouch / Suppress (off a post) and Ambush (without a post) x (1 - w x danger);
  TakeCover / Relocate x (1 + w x danger); every cover slot (`FindCoverNear`) and post candidate x (1 - w x danger).
- `choke_leave_danger` 0.5 - in contact at this danger he relocates (cover available) before he is spotted there.
- `ambush_exit` 0.8 / `overwatch_roof` 0.6 / `window_position` 0.4 - the Ambush order with a post x (1 + w x post score); the order walks
  him onto the post (`bKeepGoalLevel`), then faces its look yaw, crouched (standing at a window); 3 walks without arriving drop the post.
- `route_cover` 0.4 - a cover slot whose wall faces the squad's route (`SetTeamRoute`) x (1 + w x facing; half without an analysis hiding
  spot within 2.5 m).
- `chokepoint_avoid` 0.8 - FallBack tries 0 / +-35 / +-70 deg off straight away, score (0.5 + 0.5 cos) x (1 - w x danger) (`PickFallBack`).
- `window_share` 0.5 - seeded share of riflemen the placement sends to windows. Frozen: `tactical_reach_m` 40, `marksman_band_min_m` 40,
  `ladder_roof_men` 1.

**P2 - placement.** `GatherPostCandidates(route, seed)`: ambush = `BestAmbushSpots` per route leg (the EXIT first), overwatch =
`OverwatchSpots` of route samples every 15 m, windows = `WindowPositions` (stand + yaw), drops = `DropSpotsAbove` (edge, jumpers list),
ladder roofs = `PickLadderOnlyRoofs(seed)` (the file's 0.65) near the route; each with its PMC danger. `PlanPlacement`: marksmen (sniper
band, else the squad's longest reach) on the best overwatch roof (one per roof), a jumper archetype on a drop edge (the PMC operative is
not in any drop spot's `jumpers` list - so never today), `ladder_roof_men` on a picked ladder-only roof, the first of the rest on the best ambush spot (always), the others at windows / ambush spots,
3 m apart. Used by `SpawnSquadTactical` (spawn ON the posts; line spawn without candidates), `AssignTacticalPosts` (the level encounter's
Defend posture on a map with a tactical file: posts along the player's way, none closer than half `min_m`), the arena's tactical scenario.

**P3 - arena (`CodexTactics.PmcArena`).** `scenario=tactical` (and half the random rounds on a map with a tactical file): the route = a
seeded pair of the level start + the mission goals (`objectives/<Map>.objectives.json`), 25 m+, a full path both ways; the attackers spawn
at its start, the objective is its end, the defenders spawn on their posts along it; the round gets a `tactical` object (`route_m`,
`posts[]` kind / xyz / score / danger, `ladder_roofs_picked` / `_total`). On every map each round has `sides.A.behaviour`,
`sides.B.behaviour` and `rounds[].behaviour` (= side A, the trainer's candidate) in Grok's G002 rich form (`AI/PmcArenaBehaviourRules.*`):
`{schema: codex.pmc_arena_behaviour/1, side, tactical_map, metrics: {<key>: {name, unit, range, direction, value}}}` with the 11 keys
`time_in_contact_s` (5 s per contact moment, union), `flanks_completed`, `moves` (walk orders that took him >= 3 m), `suppress_then_move`
(a mate's walk under a running suppression, <= 8 s), `ambush_success` (first hits from a chokepoint's hiding spot on a man near its
threatened point), `overwatch_kills` (kills by hits from an overwatch roof spot), `time_in_chokepoint_s` (seconds per man inside an
outline), `window_positions_used`, `quiet_spells` (gaps >= 15 s after the first contact), `order_churn`, `stalemate_share`. Place metrics
need the tactical file (0 without). Stats also carry `post_moves`, `tactical_fall_backs`.

**Smokes.** `TacticalPmcSmoke` (X4: marksman on a roof spot, an ambusher at a chokepoint exit outside its outline, ladder picker, posts held
when commanded, chosen positions safer than random ones along the way, fall-back round a chokepoint, no file = no candidates) and
`PmcArenaTacticalSmoke` (X4: one tactical round, posts, behaviour blocks), both in `Scripts/test_map.json` (rule `tactical-pmc-2026-11-17`).
**Not done:** the trainer side (Grok: ingestion / MAP-Elites), X2 / X6 smokes (the scenario works on any map with a tactical file + goals),
line-of-sight checks for posts (geometric only), PMC jumpers (no archetype allowed), the player-squad hints (P5).

**Verified (2026-11-17, d1e749a6).** Build OK; the full `CodexTactics` automation suite 1465 tests, 1 failure outside this branch
(`CodexTactics.Text.NoCyrillicInPlayerText`: Cyrillic loading strings in `GameplayPreloadSubsystem.cpp` / `CodexTacticsHUD.cpp` from main's
preload commits); smokes on a fixed step, all PASS without re-runs: TacticalPmcSmoke, PmcArenaTacticalSmoke, TacticalMapSmoke,
PmcStalemateSmoke (the Depot_1 arena), PmcEncounterSmoke, PmcEncounterHuntSmoke, RpgPmcSmoke, EnemyFacingJitterSmoke. Manual X2 round
(`CodexTactics.PmcArena rounds=1 seed=11 scenario=tactical` on L_Gen_Test_X2): a 200 m route, defenders on overwatch / ambush / ladder-roof
posts (PMC danger 0), 2 of 3 ladder-only roofs picked, `rounds[0].sides.A/B.behaviour` + `rounds[0].behaviour` (= A) written with all
11 metrics (A: contact 35 s, 1 ambush success, 2.3 s in a chokepoint vs B 7.7 s).

**API review fixes (2026-11-17).** (1) Overwatch roof spots now pass the same reach / `MinFromStartCm` filter as the other posts (1.5 x
reach, like ladder roofs) - no marksman at the player's start. (2) Only candidates on the navmesh AT THEIR OWN LEVEL (|dz| <= 150 cm,
`KeepCandidatesOnNav` / `ProjectToNavAtLevel`) are planned; a planned post that still fails it is dropped and the man spawns on the line.
(3) `SpawnSquadTactical`'s `OutPosts[i]` is the post of result fighter i (a failed spawn leaves out both); the arena attributes posts
by that spawn list (`TacticalFighters`), not by the side's member order. (4) `FindCoverNear`'s route-cover term reads the hiding spots near
the route cached once per route (`FTeam::RouteCoverSpots`, `SetRouteOf`), not every spot of the map per slot. (5) A post walk counts toward
the three tries only when it ended short: refused, or not 1 m closer (`PmcTacticalRules::PostWalkEndedShort`, test
`CodexTactics.AI.PmcTactical.PostWalkEndedShort`); an interrupting order resets it. (6) The arena's side-indexed tracks are guarded by
`IsSideIndex` (0 / 1) everywhere.

## 8. v2 — Buildings, storeys, roof access, open crossings, goal sites (Smart Enemies T-09, 2026-11-20)

**Schema `codex.tactical_map/2` and backwards compatibility:**
- The generator export is updated to schema `codex.tactical_map/2` in `Tools/TrainingGround/src/levelgen/tacticalMap.ts`.
- The C++ loader (`TacticalMapRules::Parse`) supports both `codex.tactical_map/1` and `codex.tactical_map/2`. v1 files parse cleanly without errors (v2 fields default to empty arrays / default values), while newer unrecognized schemas (v3+) are refused.
- All existing v1 fields, structures, and contracts are strictly preserved.

**Structures and storey/building attribution:**
- `building` and `floor` set where applicable (`windows[]` already had them in v1; v2 adds them to the entries below):
  - `windows[]`: building id and floor index of each window (v1 field, unchanged).
  - `overwatch_roofs[]`: roof level (`floor`), plus `building` and `floor` on every spot in `spots[]`.
  - `hiding_spots[]`: building id and floor index when adjacent to a building wall or footprint (`BUILDING_ASSOCIATION_MARGIN_CM = 200` cm).
  - `chokepoints[]`: building id and floor index when passing through or bounded by a building.
- Structured roof access points (`access_points[]` with `id`, `kind`: stairs / ladder / hatch, `top` world XYZ):
  - Added to `overwatch_roofs[]`, `ladder_only_roofs[]`, and `walkable_roofs[]`.
  - Provides exact 3D world arrival positions on roofs rather than bare string tags, avoiding phantom access coordinates.
- `walkable_roofs[]`:
  - Master registry of all climbable roofs on the map (`building`, `layer`, `floor`, `z`, `ladder_only`, `access[]`).
- `open_crossings[]`:
  - Route segments $\ge 10\text{ m}$ (`MIN_CROSSING_LENGTH_CM = 1000` cm) with no cover within 2 m of the path line (`COVER_DISTANCE_THRESHOLD_CM = 200` cm, evaluating cover boxes, walls, and building footprints).
  - List of cover objects within 40 m (`SEEING_COVER_MAX_DIST_CM = 4000` cm) with clear Line of Sight (`seeing_covers[]`, blocked by building footprints and tall walls $> 150\text{ cm}$).
  - Used for squad dash bounding and PMC ambush planning.
- `goal_sites[]`:
  - Cover objects within 25 m (`GOAL_SITE_RADIUS_CM = 2500` cm) of each mission goal (`goal_id`, `cover_id`, `pos`, `dist_m`, `facing_yaw`, `sector_arc_deg: 90`, `cover_kind`, `cover_height_cm`).
  - `facing_yaw` points outward from cover toward the goal.
  - Geometry-only in TS (stop rule: physical UE line-trace validation is handled in T-05).

**C++ Subsystem queries and Smart Enemies integration:**
- `TacticalMapRules::GetGoalSitesForGoal(File, GoalId)` and `UTacticalMapSubsystem::GetGoalSitesForGoal(GoalId)`: retrieve goal defense covers.
- Integration in Smart Enemies:
  - T-05 (Line-trace position validation): Unreal-side raycast verification for `open_crossings` and `goal_sites`.
  - T-12 (Goal defense): PMC squads deploy to `goal_sites` for 360-degree perimeter control around objectives.
