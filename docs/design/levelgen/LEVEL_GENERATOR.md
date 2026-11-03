# Level generator (Training Ground department «Генератор уровней») - design and status

> Branch `tools/level-generator` (2026-11-01). The game designer's request: a generator that uses his annotated reference maps
> (`REF_01_02_DEPOT.md`, the first one) as visual / statistical guidance and produces ORIGINAL greybox layouts for our tactical
> squad game, evaluates them (the mandatory route must be completable in the formation simulator; mechanic coverage, sightlines,
> ambush quality, similarity to the reference), lets critics (Jev, the Claude API) judge them, and builds the accepted ones as
> new UE maps the bot walks. Nothing here copies the reference's layout or art: structure, proportions, densities and tactical
> patterns only. UE-only (no Godot reference).

## 1. Where things are

| What | Where |
|---|---|
| Reference profiles (the numbers a reference gives the generator) | `Content/Data/LevelGen/refs/<id>.json`, schema `codex.levelgen_ref/1` (`src/levelgen/profile.ts`); the reading: `docs/design/levelgen/REF_*.md` |
| Generated layouts | schema `codex.levelgen_layout/1` (`src/levelgen/schema.ts`), an extension of the lab's `codex.formation_layout/1`; candidates in `Saved/TrainingGround/levelgen/<name>.json` (+ `.eval.json`, `.verdicts.json`, `.png`), accepted ones in `Content/Data/LevelGen/accepted/<name>.json` (+ `.meta.json`) |
| Generator | `Tools/TrainingGround/src/levelgen/generate.ts` (pure, deterministic: profile + seed + params) |
| Evaluator | `src/levelgen/evaluate.ts` (pure; uses the lab's nav grid and the formation simulator) |
| Critics | `src/levelgen/critic.ts` (Claude API over fetch as `clients.ts`, Jev System One, the offline heuristic); CLI `src/levelgen/cli/critic.ts` |
| CLI | `npm run levelgen -- --seed 1 --seeds 3 [--size 177x124 --density 1 --variants 3 --verticality 3 --no-sim --accept <name>]`, `npm run levelgen:critic -- --layout <name> --client claude|jev|mock --budget-usd 2 [--model claude-opus-5-5] [--dry-run]` |
| Department page | `src/departments/LevelGenDepartment.tsx` + `LayoutView.tsx` (the canvas); server `server/levelgenApi.ts` (`/api/levelgen/*`) |
| Renders | `Scripts/Tools/levelgen_render.py` (profile schematic over the un-skewed reference, candidate top-down, the critic's reference JPEG) |
| UE builder | `Scripts/Editor/build_levelgen_map.py` (section 7; generalises `build_formation_lab.py`) -> `/Game/Maps/Generated/L_Gen_<profile>_<seed>` + `Content/Data/LevelJson/L_Gen_<...>.json` |
| In-game check | `CodexTactics.LevelGenRouteSmoke` (section 8; any generated map: `Scripts/levelgen_route_smoke.ps1 -Map <map>`); `CodexTactics.LevelGenLaddersSmoke` (section 8.5: every ladder up / down, the cut's ramp / bridge / ladder ways in and out) |
| Ladders + cut ways (builder) | `Scripts/Editor/levelgen_ladders.py` (pure, `--test`: the ladder frame from pos -> foot, 10 cm off the wall's outer face, the top exit openings, the trench faces, the cut ramps; section 7) |
| Bot route + drop links | `Scripts/Editor/levelgen_bot_route.py` (pure: the level JSON's `bot_route` from the mandatory route, the hole drop links; `--test`; alone it patches a level JSON without a rebuild) |
| Combat bot runs / video | `Scripts/bot_run.ps1 -ReportDir` (section 8.1), `Scripts/levelgen_record_run.ps1` + `Scripts/Tools/clip_assemble.py` (section 8.2); the Training Ground «Боевая лаборатория» lists every generated map with a `bot_route` |
| Pipeline job status | `Scripts/job_status.ps1` (`codex.job_status/1`, section 8.3) |
| Combat simulator (a layout judged WITH enemies before the build) | `src/combatSim/` (section 8.4; `docs/design/levelgen/COMBAT_SIM.md`): `npm run combatsim -- --layout <name> --seeds 200`, `npm run combatsim:calibrate -- --batch <Unreal batch dir>`; output `codex.combat_sim/1` |
| Tests | `src/levelgen/levelgen.test.ts`, `src/combatSim/combatSim.test.ts` (`npm test`) |

## 2. The layout format `codex.levelgen_layout/1`

Everything the lab format has keeps its meaning (`bounds`, `ground_z`, `spawn`, `walls`, `zones`, `routes`, `props`; UE cm, X
north = up, Y east = right); `toFormationLayout(layout, layer)` flattens one LAYER into a plain lab layout, so the simulator,
the audit, the movement bot and the map builder read a generated layout unchanged. The extension:

- **Height levels** (game designer decision 2026-11-01, night): every map is split into discrete levels 3 m apart - level -1 =
  -3 m (the rail cut / canal, pits, craters, trenches, basements), 0 = the ground, +1 = +3 m (first floors, low roofs,
  platforms, raised rims), +2 = +6 m (second floors, the ruin's roof), +3 = +9 m (the tall halls' roofs - game designer
  2026-11-02, `HALL_ROOF_LEVEL`: the sawtooth hall, the warehouse, the Steel Works, for the view from the roofs), upward in 3 m
  steps when needed (`LEVEL_HEIGHT` 300, `levelZ(level)`). A **layer** is one walkable surface on one level: `ground` (0), `cut` (-1), `floor:<building>:<n>`
  (level n), `roof:<building>` (level = the building's `floors`). `layersOf`, `layerLevel`, `levelsOf` enumerate them.
- `walls[]`: the lab wall + `floor` (the level it stands on), `kind` (wall / fence / curb / cut_wall / building / parapet /
  interior), `building`.
- `buildings[]`: an axis-aligned `footprint` [min, max], `height` (= `floors` x 3 m when the roof is walkable), `floors`
  (interior floors counting the ground floor; floor n on level n; the roof on level `floors`), `hollow` (no intermediate slabs:
  halls, towers - the stairwell climbs ground -> roof in one flight), `enterable`, `roof_walkable`, `doors` (cut out of the
  perimeter walls), `stairs` (`stairwell` inside / `external` on a face; `pos` on the lower level, `top` on the upper),
  `roof_holes` (down onto the top floor), `floor_holes` (down one floor), `set_piece`. A `chimney` set-piece (`NEVER_CLIMBABLE`)
  is an obstacle only: never enterable, never a walkable roof, no stairs, no ladder onto it, no layer / level of its own - the
  validator refuses anything else (game designer 2026-11-02: "chimneys are NOT ladder towers").
- `cut`: the trench polygon on level -1 (`floor_z` -300), its `crossings` (ramps / bridges at ground level); the retaining
  walls are ground-layer `cut_wall` walls (a 2.2 m parapet + fence) with gaps at the crossings.
- `ladders[]`: the actor origin `pos` (the bottom centre of the rung plane, on the lower level, 10 cm off the face), `yaw`
  (+X away from the wall, LADDERS.md 3.1), the layers `from` / `to` and their `level_from` / `level_to`, `rungs` +
  `plinth_cm` (`ladderForLevels`: one level = 8 rungs x 36.4 cm + an 8.8 cm plinth under the foot, two = 16 + 17.6, so the
  top lands exactly on the level within LADDERS.md's 8 cm), the link points `foot` (1 m out, where a route arrives) and `top`.
  Fewer than 7 rungs is decoration (LADDERS.md) - the validator refuses it.
- `covers[]`: `pos`, `yaw`, `size` [l, w, h] cm, `kind` (jersey / sandbag / crate / wreck / barrel / silo / boiler); >= 140 cm
  tall ones block sight.
- `routes[]`: the lab route + `mandatory` (exactly one), `layers[]` (the layer of every point), `links[]` (`{ at, via }`:
  the leg ending at point `at` goes through the ladder / stair / hole (`hole_roof_<i>`, `hole_floor_<i>`) / crossing `via`),
  `set_pieces[]`, `intent` + `intent_note` (`'kill_zone'`: a deliberately exposed passage the designer wants - V3 along the cut
  floor, 2026-11-02; never on the mandatory route). A leg that ends at an open hole stops on its RIM (`HOLE_RIM_M` = the hole's
  half side 1.2 m + 0.8 m, on the side it comes from); the drop leg lands 1 m past the hole's centre on the level below.
- `zones[].intent`: `'kill_zone'` on the cut (`Z_cut`).
- `end`, `spawn_points[]` (edge spawns), `ambush_points[]` (authored hiding spots, the AmbushSpotRules vocabulary), `patrol_routes[]`,
  `pressure` (a combat-lab style block for the level JSON, COMBAT_LAB.md section 2).

## 3. The grammar (`generate.ts`)

The map is a STREET SKELETON laid out to the profile's proportions (every position jittered by the seed): the west street,
the T-junction cross street, the north street along the cut's north wall (the marksman lane), the east yard street, the south
street (the second lane), the inner south street. The cut is a band across the middle with two crossings (a central ramp by
the silos, a NE bridge) and three ladders on its south wall. The BLOCKS between the streets are filled with set-pieces, each a
function emitting walls / buildings / covers / zones:

| Set-piece | What it emits | Reference part |
|---|---|---|
| facade | a closed tall block (3 levels, hollow) | the NW office blocks |
| ladder_overwatch | a 1-level platform with a ladder onto its roof | the pipe gallery B2 |
| one_entry_compound | a fence with one gate, a shed with a door, covers | the west compound; the boiler yard |
| industrial_hall | an enterable hollow hall, walkable roof on +3 = 9 m, one stairwell flight ground -> roof (12.9 m run; verticality >= 2), two doors | the sawtooth hall, the Steel Works |
| warehouse | a hollow hall, roof on +3, + a fenced front lot with gates | the warehouse |
| junkyard_field | a fence with two gaps and a very dense wreck field | the junkyard |
| tank_farm | two silos (round covers), a fenced lot with three gates, a pump house | the tanks |
| open_square | open ground with few covers | the park |
| dead_end | a 5 m alley closed at one end (the wall layout at the end) | - |
| ruined_two_level | 2 floors, roof holes (verticality 3) + a floor hole, an external stair to the roof, interior partitions with doorways, an annex 2.5 m west | the ruined factory |
| chimney | a 3 x 3 m closed 20 m obstacle (a cylinder in UE): blocks sight, never climbable, no level (designer 2026-11-02) | the chimneys |
| tower | (vocabulary only; the generator places none since 2026-11-02) a hollow block with a ladder | - |
| street / marksman_lane / alley / t_junction | the lab zone kinds for the checks (wall_run / corridor / corner) | the streets |

**Routes.** The mandatory route is the designer's loop: the west street -> the west compound (its one gate, to the shed's door
and back) -> the T-junction -> the north street full length (the lane) -> the NE crossing -> the east yard round the Steel Works
-> the south street -> the ruined factory FROM ABOVE (the external stair onto the roof, a roof hole down to floor 1, a floor
hole down to the ground; with verticality < 3 through its doors) -> the tank farm past the pump house -> the dead end (the
wall layout at the end). It HUGS one side of every street (2.6 m off the facade / fence / cut wall) so the classifier sees the
wall and picks the echelon or the column instead of the open middle. Variants: V1 the gallery ladder + roof (an overwatch over
the start), V2 the north blocks (the boiler yard gate, the alley between the office and the lot, the lot gate, the warehouse's
south door and its stairwell onto the roof), V3 through the cut (the central ramp, the trench floor, out by the last ladder onto
the inner street), V4 the inner street and the ruin from below (its south door in, west door out). `params.variants` keeps
the first n. **V3 is a deliberate kill zone** (game designer 2026-11-02): `intent: 'kill_zone'` on the route and on `Z_cut`;
the evaluation reports it as intended and the critics are told it is not a defect. The junkyard stays an ambush field beside
the north street, never a route (designer 2026-11-02); the route ends in the SW dead end (kept).

**Covers** per zone by the profile's density x `params.density` (one greybox cover object ~ 1.7 of the reference's small
covers), never within 2.6 m of a route, 1.2 m of another cover or 1 m of a building. **Ambush points** (COMBAT_LAB.md 2.1's
spot vocabulary): per free wall end one spot on each face (1 m inside the end, 2.6 m off the face), every building's outside
corners (1.6 m out diagonally), the far side of every tall cover - kept when 14-30 m from a route. **Patrol loops** (4
points, radius 4-12 m) in the open set-pieces >= 30 m from the start; **spawn points** at the edges >= 40 m from the start.

**Parameters.** `size_m` (default the profile's 177 x 124; everything scales with it), `density` (cover multiplier),
`variants` (0-4), `verticality` 0 flat / 1 ladders + roofs / 2 + stairwells and floors / 3 + the ruin's holes.

## 4. The evaluation (`evaluate.ts`), 0 tokens

1. **Completability (required).** Every leg of every route is pathed on its layer with the lab's nav grid (walls widened by
   the 35 cm agent radius); a layer change needs a link the route touches at both ends (within 5 m). The first failure names
   the leg and why (`point in a wall / off the layer`, `unreachable`, `no link`, `missing link`, `link off the leg`).
2. **Mechanic coverage.** The formation simulator (`sim.ts`, 4 men, the lab's `formation_config.json`) walks the mandatory
   route's ground legs: the share of time per terrain class (open / wall_left / wall_right / corridor / at_wall) and per
   formation (diamond / column / echelons / wall), corner watches, wall layouts, doorways crossed, ladder / stair / hole uses,
   roofs and floors visited, the height levels visited and the share of route points per level.
3. **Sightlines.** Every 2 m along the mandatory route, 36 rays against the layer's walls and the tall covers, clipped at the
   map's edge: p50 / p90 / max of the longest ray (how far the squad can see or be seen), the share of samples under 40 m
   (short-sightline zones), the number of lanes >= 80 m (runs of >= 24 m), the mean ray (openness). **Elevated vision** (2026-11-02):
   on a ROOF the eye stands at the roof's level + 1.6 m and only obstacles whose top reaches that height block a ray (walls on
   their level, buildings, covers by their height; a 20 m chimney and a 12 m silo block even the +3 roofs). **Roof views**: from
   every walkable roof (its centre + 4 points 1.5 m inside the corners, 36 rays each) the mean / longest ray and the share of rays
   >= 60 m - seed 1: the three halls on +3 mean 64-73 m, the ruin on +2 58 m, the gallery on +1 40 m; the same sawtooth hall on +2
   sees less (test). The metric keeps the LONGEST line; the dead zone behind a roof's edge is the in-game vision cone's job
   (main `fix/elevated-vision-cone`). Documented gap (2026-11-02, `fix/vision-from-height`): the metric has no sight RANGE (rays to 200 m);
   the game's range grows with the height level (`SightRules::HeightRangeMultiplier`: +3 = x1.75 of 60 m) - mirrored in the combat sim
   (`HeightSightRules`), not here, since this metric measures the open lines a level offers, not who sees whom.
4. **Ambush quality.** The layout's ambush points 14-30 m from the route AND concealed from the nearest route sample, per
   100 m of route; the share of route samples round which a coordinated plan (3+ concealed spots in 3+ sectors) could be laid.
5. **Kill zones (intended).** Every route with `intent: 'kill_zone'`: its metres on the killing ground (the cut layer or a zone
   marked `kill_zone`), their share of the route, the ways out of the ground (ramps + cut ladders) - one report line "kill zone
   (INTENDED, not a defect)"; the variant line carries "[kill zone by design]". Nothing in the score penalises it.
6. **Profile similarity.** Every `targets` range of the profile (route length, variants, ladders, walkable roofs, stairwells,
   enterable buildings, cut crossings, lanes >= 80 m, the short share, the cover density, the height levels on the map and on
   the route) scored 1 inside, decaying to 0 one range-width outside; the mean is the similarity.

Score = 0.4 x completable + 0.2 x similarity + 0.2 x terrain diversity + 0.2 x ambush quality. The report is <= 30 lines.

**Limits of the per-level simulation (documented on purpose).** Each level is simulated as its own 2D layer: a roof is its
parapet, a floor its perimeter + interior walls, the cut its trench edges; the links are instantaneous (no climb time, no
queue at the ladder), holes are one-way links, nobody falls, and the formation logic is only measured on the GROUND legs (the
squad on a roof or a floor is not classified - the lab's rules have no floors). The real climb, the queue and the formation on
a roof are the in-game smoke's job (section 8).

## 5. The critics (`critic.ts`)

- **Claude API critic** (offline, Node; the server holds the key, never the page): sees the candidate's top-down render (the
  page's canvas, PNG), the reference's un-skewed annotated map (JPEG, 1200 px) and the evaluation JSON + the profile targets,
  answers strict JSON `{ fits, confidence, reasons[], fixes[] }`. Default model `claude-opus-5-5` (the claude-api skill's
  default; `--model claude-fable-5-1` / `claude-sonnet-5-5`), `output_config.effort low`, adaptive thinking. **Budget cap in
  USD** like the red team: the ledger `Saved/TrainingGround/levelgen/critic_ledger.json` keeps the cumulative spend across the
  page and the CLI; a call is skipped when spend + the estimate (~$0.03-0.07 per call) would exceed `--budget-usd` (default 2).
- **Jev (TypeSafe System One)**: typed judgments over the metrics only, for mass ranking at no USD cost - `fits` (a Noul
  probability), `quality` (a Score over 5 described levels), `weakest` (a Choice among route / scale / verticality / covers /
  sightlines / ambush / similarity). Without `TYPESAFE_API_KEY` the offline heuristic answers and says so (`offline: true`).
- **Mock / offline heuristic**: fits when the route and the variants complete and the similarity is >= 0.75.
- **The designer's decisions in every critic** (2026-11-02): the Claude system prompt lists them as "not defects" (chimneys are
  obstacles, never towers; a route marked `kill_zone` is a deliberate killing zone; the junkyard is an ambush field; the route
  ends in the SW dead end; the hall roofs sit on +3 for the view), the prompt carries `roof_views` and `kill_zones_intended`, Jev's
  `fits` instructions and state say the same, the heuristic adds an "intended kill zone - by design" reason.

Every verdict goes to `<name>.verdicts.json`; the page shows them under the metrics; an accepted layout keeps them in its
`.meta.json`. First real run 2026-11-01 on seed 1 (before the compound / ruin legs were folded in): Jev "does not fit 44 %,
quality 3.1 / 5, weakest sightlines"; Claude Opus 5.5 "does not fit 80 %" - the route stayed on long streets, never crossed a
short-sightline compound, used none of the verticality ($0.033). Both were right; the generator was changed accordingly.

## 6. The department page

Left: the profile, seed, candidate count, size / density / variants / verticality, Generate (in the browser, structural checks
only - fast), the candidate list (score, route ok, verdict icons), the accepted list. Middle: the candidate's top-down canvas
(layer selector: all / ground / cut / each roof and floor) next to the reference image; «Прогнать отряд» runs the simulator
walk (~5-20 s); «Принять» saves to `Content/Data/LevelGen/accepted/` (+ `.meta.json` with the summary and the verdicts),
«Отклонить» drops it (logged to `rejected.jsonl`). Right: the route verdict with the failing leg, the variants, the metrics,
the similarity per metric, the critic buttons (Jev / Claude with the budget and model / heuristic), the verdicts, the <= 30-line
report. What a verdict means: FITS / DOES NOT FIT with the confidence (Claude: self-reported; Jev: the probability), Jev's
quality 1-5, the reasons (each should cite a metric or a place) and the fixes (one change each); «offline» = no model answered,
only the heuristic.

## 7. The UE builder (phase 5)

`Scripts/Editor/build_levelgen_map.py` generalises `build_formation_lab.py`: for an accepted layout it creates
`/Game/Maps/Generated/L_Gen_<profile>_<seed>` (never an existing map; `L_Demo01_B_FrozenFactory` is not touched) with a ground
slab sized to the bounds + a 10 m margin, the walls (per level: a floor-n wall stands on its slab at `levelZ(n)`), the buildings
(perimeter walls with door gaps, floor slabs at `levelZ(n)` with the floor holes cut as four slabs round the hole, a roof slab
with its holes, stairwells as sloped cubes between the levels - the slab opening above a stairwell covers the ramp's last 3.5 m
and ENDS exactly at its top end (no gap the navmesh cannot cross), an external stair's ramp ends exactly where its flat landing
starts (5 cm tucked under it; the landing used to be centred on the ramp's top and left a ~50 cm step over the 35 deg ramp -
above the navmesh agent's 35 cm step, the 2026-11-01 knownFail) and the landing crosses the wall's top onto the roof; a
`chimney` is one closed cylinder, its 2D walls are not built), the cut (the ground slab
split round the trench, a trench floor slab at -300, the retaining walls from -300 up to the parapet, crossing slabs / ramps),
the ladders (`ALadderActor`, `Height` = rungs x pitch, a plinth cube under the foot), the covers (cubes / cylinders), the
fences, PlayerStart at the start, route TargetPoints (tags `LevelGenRoute` + the route id, label `<route>_<n>`, with the layer
in a tag), spawn / ambush / patrol TargetPoints (tags `CodexAmbushPoint` for the pressure subsystem), NavMeshBoundsVolume over
the bounds from -4 m to 3 m above the highest walkable level (12 m with the +3 halls; a chimney's top stays outside, so it gets
no navmesh island), then `Scripts/bake_navmesh.ps1 -Map <name>`. The level JSON's `minimap.floors_cm` lists every level from +1
to the highest walkable one (300 / 600 / 900). It writes the level JSON
`Content/Data/LevelJson/<map>.json` from a template (combat_start ambush, skirmish, no cold, cheats on, the layout's `pressure`
block). Status: see section 9.

**2026-11-02 (branch `tools/levelgen-bot-runs`).** The builder also places the **DROP nav links** through the roof / floor holes
(`levelgen_bot_route.drop_links`): per hole a `NavLinkProxy` (`Drop_<building>_<hole>`, tag `LevelGenDrop`) with four one-way point
links from 70 cm outside each edge on the upper slab down to the hole's centre one level below - the squad (and the enemies) walk off
the rim and fall 3 m (no teleport); only one-level drops onto a real slab (a hollow hall's roof hole gets none). The level JSON gets a
**`bot_route`** (`levelgen_bot_route.bot_route`, the format of `L_FormationLabCombat.json`): one waypoint per mandatory-route point
with `"z"` = its floor height (the bot clicks just above that level and checks it on arrival), a point over an open hole of its own
level pushed out to the rim + 70 cm, `"drop": "<hole>"` on the leg that drops, the `wall` action at the last point when the route
expects the wall layout there (Z_deadend), `loop: true` (the timed runs walk it again and again). A rebuild KEEPS the existing level
JSON's `pressure` / `cheats` (tuned on the combat lab page) when it was written for the same layout. Without a rebuild:
`python Scripts/Editor/levelgen_bot_route.py <accepted layout.json> [<level json>]` adds / replaces only the `bot_route`.
`L_Gen_Depot_1` was rebuilt with the links (3 drop link actors, 12 links; navmesh Default 440 tiles).
`Scripts/levelgen_build_map.ps1` writes a job status (section 8.3) and refuses with a clear status when the editor is open.

**2026-11-02 (branch `fix/levelgen-ladders-cut`, the game designer's bug report on `L_Gen_Depot_1`).** "The ladders on the building walls are sunk
into the wall, nobody can climb them; the cut's ladder cannot be climbed; the cut cannot be entered at all - the squad runs round it and stops."
Root causes and fixes (the pure part in `Scripts/Editor/levelgen_ladders.py`, `--test`; the builder imports it):
1. **Every ladder faced its wall.** `generate.ts` converted the profile yaw with `90 - profile` (a MIRROR; the right conversion for `toUE` is
   `profile + 90`), so every ladder's +X pointed INTO its wall (LADDERS.md 3.1 wants +X away from it): the hang line, the bottom link point and the
   plinth inside the wall, the top link point in the air off the edge, the brackets / grab bars sticking out - no link point on the navmesh. The
   builder now takes the actor's yaw from the geometry (`pos` -> the layout's `foot`; the accepted layouts keep their mirrored `yaw`, the report
   says "MIRRORED in the layout, fixed"); `yawToUE` is fixed for new layouts (cover yaws: a box turned 180 deg is the same box; the wrecks' +-30 deg
   jitter is mirrored, which is still random).
2. **The rung plane stood inside the building wall.** The builder stands a wall on its centre line, so a 30 cm perimeter wall reaches 15 cm past
   the footprint the generator measured `pos` from (10 cm off it): the plane was 5 cm inside the wall. The builder finds the wall behind each
   ladder (the walls on its lower level; the trench's faces for a cut ladder) and moves the ladder out to stand 10 cm off its OUTER face
   (`LAD_B_gallery`: 15 cm). The plinth now turns with the ladder (110 x 140 cm, from 5 cm in front of the rungs).
3. **The cut ladders climbed out into the cut wall.** The cut's retaining wall (`cut_wall`, 2.2 m tall, 1.5 m thick) stands right behind the
   ladders' top; the climb out ended inside it. Every wall on a ladder's upper level that the top exit crosses (rung plane -> the layout's `top`)
   gets a 2 m opening (`CUTWALL_S_0_l0 / _l1 / _l2`, `CUTWALL_S_1_l0 / _l1`). The trench's below-ground faces now stand OUTSIDE the trench (their
   inner face on its edge; they narrowed the floor by 10 cm a side and put the cut ladders' rungs on the face).
4. **The cut ramp joined nothing.** The old ramp ran from 5 m before the crossing's middle (its top end in the air over the trench) to 3 m past it,
   under the bridge slab with 0.7 m of head room. Now (`cut_ramps`) it leaves the bridge slab's far edge (tucked 5 cm under it) and runs down
   ALONG the trench onto its floor: 7 m run, 23 deg, 3 m wide (1.5 m walkway on each side; the trench floor passes under the bridge, 2.8 m).
5. **C++ (formation, not the ladder rules):** with the map connected, the squad still went round the trench's end (the formation bypass read the
   below-ground face as a free-standing wall between the leader and a goal on the rim), the leader walked to a stale bypass waypoint after a
   ladder, and the followers did not follow him DOWN a ladder (their slots were projected from their own height) - `docs/systems/squad-and-orders.md`
   "The formation across height levels". LADDERS.md is unchanged.
Walking off the rim / jumping down into the cut (the falls system) is a separate later task.

**2026-11-02 (branch `fix/ramp-roof-cutaway`, `LevelGenRampSmoke`): `B_hall_n`'s stairwell is cut.** The accepted layout `gen_ref_01_02_depot_1`
overlaps two buildings: `B_office_w` ((5200, -8750) - (6100, -2850), 9 m) reaches 4 m into `B_hall_n` ((3200, -3250) - (5700, 550)); its north wall
`B_office_w_E_0` (y -2850, 9 m) crosses the hall's stairwell ramp `B_hall_n_stair_0` 2.8 m up - the navmesh along the ramp stops there ("ramp nav ...
24 92 152 --- 305 ..."), the 9 m roof is not reachable by it. `LevelGenRampSmoke` reports it as `Smoke WARN` and skips it (`strictnav` = a FAIL). The
generator's validator should refuse overlapping building footprints; the map is unchanged here (no rebuild: the squad's ramp fix does not need one) -
open for the generator task. The other four stairs from the ground (`STAIR_ext_7`, `STAIR_in_8`, the warehouse / steelworks 9 m stairwells) are
connected and walked by the squad (docs/systems/squad-and-orders.md "The squad on a narrow ramp").

## 8. The in-game check

`CodexTactics.LevelGenRouteSmoke` (CodexTacticsDev) loads the generated map's layout JSON (by the map name from
`Content/Data/LevelGen/accepted/`), walks the squad along the mandatory route with the leader's move orders (the FormationLabSmoke
carrot pattern: a point every 6 m ahead, renewed every second; ladders and stairs through the nav links; a leg has 3 x its
walking time to finish) and reports completion or the leg where it got stuck (position, the leg's layer, the last order).
**The hook (done 2026-11-02):** when the level JSON has a `bot_route` the smoke hands the walk to the playtest bot's ROUTE mode
(`Codex.Bot.Route`, `PlaytestBotRouteMode.cpp`: the player's own clicks, the formation, the drop nav links) with the pressure off
(`Codex.Pressure.Enabled 0`) and only reads its tracker: PASS = every waypoint reached once, none skipped; each hole drop is reported
(fell through the link / scripted). No `bot_route` or `walk=carrot`: the carrot walk above (its drops teleported). Arguments
`map=<L_Gen_...>` (must be the loaded map), `layout=`, `route=`, `walk=carrot`; any generated map:
`powershell -ExecutionPolicy Bypass -File Scripts/levelgen_route_smoke.ps1 -Map /Game/Maps/Generated/<map>` (status section 8.3).
Result on `L_Gen_Depot_1`: **PASS, 25 / 25 waypoints, 538 m in 319 s game time, both drops a real fall** (2.7-3.0 s each), one
stuck retry at #19 (fixed after: the click on a walk waypoint 2.7 m off the pump house's corner became a wall-cover preview that
waited for a second click - the route mode now drops such a preview and orders the leader there).

### 8.1 Combat bot runs on generated maps

The bot's route mode (COMBAT_LAB.md section 7.2) runs on every generated map with a `bot_route` and a `pressure` block: the squad
walks the route in formation, the pressure subsystem spawns patrols / ambushers at the builder's `CodexAmbushPoint` points, wall ends and
doorways (never the Brute or the Cryo Drone; the first group at 30 s, then every 120 s), immortality + infinite ammo on. Per waypoint
`"z"`, the drop legs and the drop record (`route.drops` in `codex.combatlab_run/1`: `waypoint`, `label`, `via`, `fell`, `scripted`,
`reached`, `seconds`) are in COMBAT_LAB.md 7.2. A drop leg that gets stuck is SCRIPTED (the bot puts the squad down at the waypoint
itself, `scripted: true`). The Training Ground «Боевая лаборатория» has a map selector (the lab + every `/Game/Maps/Generated/*` with a
level JSON that has a `bot_route`): settings, presets (a preset never overwrites a generated map's own `ambush.points`), bot runs and
reports per map.
Batch: `Scripts/bot_run.ps1 -Map /Game/Maps/Generated/L_Gen_Depot_1 -LevelJson L_Gen_Depot_1.json -ReportDir <dir> -DurationSec 600 -Runs 2
-Parallel 2 -TimeoutSeconds 2400` (a 10 min run takes ~1 min real, nullrhi fixed step), then `node src/combatLab/cli/report.ts --batch <dir>`.
`L_Gen_Depot_1`'s pressure tuned in its level JSON (2026-11-02): groups of **2-4** (was 1-3), `max_alive` **6** (was 4) - with 1-enemy
groups the 2 x 10 min batch had 6 short fights; the 22 authored points were enough.
**Batch 2026-11-02 `Saved/Telemetry/combat_lab/20261102_102340`** (2 x 10 min, seeds 1-2, the final build): route completed 2 / 2 (1 lap
each, 0 skipped, 4 / 4 drops fell through the links), stuck 1 (#18, run 2, retried), contacts 9 (patrol 6, ambush 3, coordinated 0), first
look 0.1 s, unwatched strikes 0, formation changes 131 (6.6 / min), wall hugs 9, cover taken 7 of 7 orders (212 s at the wall), stance
changes 1.9 / min, fights 7 (310 s), rounds 221, damage 0 (589 absorbed), knockdowns 7. Jev: route SometimesStuck, contacts Alert, focus
FixRouteOrNavigation. Earlier: `20261102_090514` (the original pressure, groups 1-3): contacts 9 (patrol 3 / ambush 6), fights 6, rounds 85;
`20261102_090718` (groups 2-4, before the cover-preview fix): contacts 8, fights 7, stuck 2 (both #19).

### 8.2 The video of a run

`powershell -ExecutionPolicy Bypass -File Scripts/levelgen_record_run.ps1 -Map /Game/Maps/Generated/L_Gen_Depot_1 -LevelJson L_Gen_Depot_1.json
-Name lgdepot [-Seconds 600] [-SpeedUp 4] [-Highlights 3] [-Seed 1] [-OutDir Saved/Screenshots/LevelGenRun] [-Keep]`: the game RENDERED
offscreen (`-RenderOffscreen -ResX=1280 -ResY=720 -benchmark -fps=30`, never a desktop capture) with the bot's route mode
(`-CodexBot -BotRoute=1 -BotDuration=<s>`) and `CodexTactics.ClipRecorder <name>` (the director camera on the action - since
2026-11-02 at most 7 m off the operative towards the foe, so a far foe no longer pushes the squad off the frame).
`Scripts/Tools/clip_assemble.py watch` encodes the PNG frames in 900-frame blocks while the game records and deletes them at once (a 10 min
run is 18 000 frames, ~25 GB as PNG); `assemble` concatenates the blocks and cuts `<name>_full_x<K>.mp4` (the whole run sped up, an "xK"
overlay, H.264 crf 28, <= 1280x720, <= 40 MB) and `<name>_fight_<n>.mp4` (real speed, <= 20 s: 3 s before to 16 s after each
`[Bot] Route: fight at` line, placed on the video by the recorder's `ClipRecorder <name> f=N` lines), checks each (duration, size, the
middle frame's mean brightness YAVG > 8 = not black) and writes **`clips.json`** (`codex.levelgen_clips/1`):
`{name, created_utc, fps, speedup, frames, video_sec, game_log, run_report, ok, files: [{kind: "full" | "fight", file, name, duration_sec,
size_bytes, yavg, ok, start_sec?, end_sec?, event?}], events: [{kind: "fight" | "cover" | "drop" | "scripted_drop" | "stuck", frame,
video_sec, game_sec, text}]}`. Also in the folder: `<name>_run.json` (the bot's run record), `<name>.log` (the game log). One folder per
recording (clips.json is per folder). Rendering runs at ~6-9 frames per real second: 10 min of game ~45 min (+ ~1 min to assemble).
**First run 2026-11-02** (`Saved/Screenshots/LevelGenRun/`, seed 1, 600 s): `lgdepot_full_x4.mp4` (150 s, 35 MB), `lgdepot_fight_1.mp4`
(the 30 s patrol contact at the first compound, the squad's cover at the wall) and `lgdepot_fight_2.mp4` (the patrol at 401 s on the way
back to the spawn), 19 s each; the route lap completed, both drops a real fall (events in clips.json). Only two fights in that seed - more
with `-Seed` 2 (the batch's run 2 had 5 contacts).

**Pressure of generated maps since 2026-11-02 (`tools/combat-sim`, COMBAT_SIM.md section 5):** the user's watch of the first run ("few
enemies, almost no ambushes") and the combat simulator's diagnosis (the event mix, not the hiding spots: 60 % patrols and 5 events in 10 min left
~2 ambush-type events a run; 37 % of the coordinated plans found no concealed spot on the squad's open flank) moved the template in `generate.ts`
and `L_Gen_Depot_1.json` to `patrol_share` **0.3**, `ambush.coordinated.share` **0.5**, `interval_sec` **90**, `max_alive` **8**,
`ambush.coordinated.max_alive` **8** (groups 2-4 kept; the lab's own level keeps the designer's 60 % / 35 % / 120 s). Check batch
`Saved/Telemetry/combat_lab/combatsim_check_02` (2 x 10 min): contacts 13 (patrol 6 / ambush 6 / coordinated 1), fights 10 (452 s), 40 enemies,
5 of 6 ambushes struck from an unwatched side, 1 of 4 plans laid out (3 "no coordinated layout" - the open flank again), route 2 / 2, stuck 0.

### 8.3 Job status for the Training Ground pipeline

Every pipeline script writes a status JSON the server can poll (`Scripts/job_status.ps1`, schema `codex.job_status/1`):
`{schema, job: "build_map" | "bot_batch" | "record_run" | "route_smoke", state: "running" | "waiting_lock" | "done" | "failed" | "refused",
reason: "" | "editor_open" | "lock_held" | "timeout" | "no_layout" | "not_saved" | "navmesh" | "run_failed" | "no_video" | "clip_check" |
"smoke_failed" | ..., message, exit_code: null | n, pid, started_utc, updated_utc, finished_utc, log, lock_holder: null | {agent, purpose, pid,
started}, result: {...}}`. A running / waiting_lock status whose `pid` is gone = the script died. The shared build lock is waited for with
`state: "waiting_lock"` (+ who holds it) and refused after `-LockWaitMinutes` (`reason: "lock_held"`, exit 3) - never a silent hang.

| Script | Status file (default) | Exit codes | `result` |
|---|---|---|---|
| `levelgen_build_map.ps1 -Layout <name> -Target <map> [-StatusFile] [-LockWaitMinutes 60]` | `Saved/LevelGen/build_map_status.json` | 0 done, 2 editor open / no layout, 3 lock / timeout, 4 not saved, 5 navmesh | `layout, target, map, level_json, report, navbake_log, drop_links, actors` |
| `bot_run.ps1 ... -ReportDir <dir> [-JobStatusFile] [-LockWaitMinutes 240]` | `<dir>/status.json` | 0 ok, 1 a run errored / timed out, 3 lock | `report_dir, map, level_json, runs, duration_sec, parallel, finished, results, runs_jsonl, report_md, aggregate, jev, logs` |
| `levelgen_record_run.ps1 -Map -LevelJson -Name [-StatusFile]` | `<OutDir>/<Name>.status.json` | 0 done, 1 no video, 2 a clip failed its check, 3 lock, 4 the game did not start | `map, level_json, name, seconds, speedup, frames_dir, out_dir, game_pid, frames_encoded, segments, game_exit_code, manifest, files, video_sec, run_report, game_log` |
| `levelgen_route_smoke.ps1 -Map [-Carrot] [-StatusFile]` | `Saved/LevelGen/route_smoke_<map>.json` | 0 PASS, 1 FAIL, 2 no level JSON, 3 lock / timeout | `map, level_json, result, summary, checks` |

### 8.4 The combat simulator (2026-11-02, branch `tools/combat-sim`)

`Tools/TrainingGround/src/combatSim/` - the full design in `docs/design/levelgen/COMBAT_SIM.md`. A layout is judged WITH enemies in seconds before
it is built: the squad replays its formation walk along the mandatory route (the formation simulator's track, cached), the combat-lab pressure runs
with faithful ports of the game's pure rules (`CombatPressureRules`, `AmbushSpotRules`, `AmbushWatchRules`, `AmbushPlanRules`, `AmbushRelocateRules`;
the C++ stays the reference), fights are simplified but explicit (2.5D sight, who saw first, the bot's wall cover, DPS / HP, knockdowns, the
immortality cheat). Deterministic per seed; ~0.1 s a 10-minute run, 200 seeds ~10 s.
- `npm run combatsim -- --layout L_Gen_Depot_1 --seeds 200 [--pressure <json>] [--out <dir>]` -> `combatsim.json` (`codex.combat_sim/1`) + a <= 40-line
  `report.md`: contacts by kind / trigger, fights, knockdowns, rounds, the pressure's counters, and the DIAGNOSTICS - every event's outcome and
  fallback, every coordinated attempt's failing condition (which sector had no concealed spot), every hiding spot's fate (chosen / why rejected), a
  20 m contact heat map by zone.
- `npm run combatsim:calibrate -- --batch <Saved/Telemetry/combat_lab/<id>>` -> the per-metric sim / Unreal table (6 main metrics held to 0.6..1.6;
  on the 2026-11-02 Depot batch 6 / 6 in band, 17 / 19 in all).
- A filter, not a replacement: the Unreal bot batch (8.1) remains the ground truth; the simulator says WHY (the event mix, the open flank) and lets
  the pressure template / the generator be tuned before a build. The Training Ground panel over `combatsim.json` is a later task (the schema is
  stable; `src/combatSim/fixtures/combatsim.example.json` is its sample).

### 8.5 The ladders and the cut's ways in / out (2026-11-02, branch `fix/levelgen-ladders-cut`)

`CodexTactics.LevelGenLaddersSmoke` (`map=<L_Gen_...>` = the loaded map, `layout=`, `only=<id>`, `noladders`, `nocut`; registered in
`Scripts/test_map.json` with `-LevelJson=L_Gen_Depot_1.json`), the pressure off:
- **every ladder of the accepted layout**: its actor (tag `ladder=<id>`, else the nearest to `pos`) is climbable with the layout's rungs, has a wall
  behind it, FACES AWAY from it (+X . (foot - pos) > 0.9), tops out on its upper level within 8 cm, both link points on the navmesh; then the leader
  alone (Solo, the others holding beside the foot) climbs it UP by a move order to the upper level behind the top and DOWN again to the ground in
  front of the foot - the climb must be seen on that ladder;
- **the cut, with the whole squad**: per `ramp` crossing into the trench (every member on the cut floor, nobody on a ladder) and out again; per
  `bridge` crossing across at ground level (never below it); out of the trench by the cut ladder the routes use (V3's `LAD_cut_3`) and back in
  by it (goals 8 m along the rim / the trench so the column trails on the goal's level) - every member climbs;
- each leg is reported by its id; a leg that stops short (still 8 s off the goal) or times out fails with the leader's position, his climb state,
  every member's feet and a navmesh path check (`PARTIAL` = the goal is not connected).
**On the old map (main `fd3511cb`): FAIL, 1 / 13 legs** - all four ladders "FACES THE WALL", no link point on the navmesh; every ladder leg and the
ramp legs "STOPPED SHORT", navmesh path PARTIAL (only the X_ne bridge crossing passed). **On the rebuilt map: PASS, 13 / 13**: `LAD_cut_1/2/3`
up 7.7-8.0 s / down 8.4-8.5 s, `LAD_B_gallery` up 7.1 / down 7.8 s, X_centre into the cut 9.9 s / out 10.1 s (the ramp), X_ne across 7.7 s,
the squad out of the cut by `LAD_cut_3` 20.5 s (3 / 3 climbed) and back in 27.5 s (3 / 3). Pictures (rendered offscreen, the game's own camera):
`CodexTactics.LevelGenLaddersShots` -> `Saved/Screenshots/LevelGenLadders/ladder_gallery.png`, `ladder_cut.png`, `cut_ramp.png`.

### 8.6 Drops off the edges (2026-11-03, branch `fix/falls-and-drops`)

The game designer's play-test items 6 / 10 / 11a (falls from heights, roof-to-roof drops, the way down into the cut): the builder now writes
`"drop_links": true` into the generated map's level JSON (`L_Gen_Depot_1.json` patched by hand, no rebuild needed); at level start
`UDropLinkSubsystem` generates one-way drop nav links off every open navmesh edge with a walkable surface 1.5-6.5 m below at most 2.5 m beyond it
(roof -> lower roof, roof -> ground, the crossings' open sides -> the cut floor). On `L_Gen_Depot_1`: ~600 links (163 of them hurt: drops of more than
one level), among them 8 off the X_ne bridge into the cut and the ruin roof (+2) -> the annex roof (+1) across the 2.5 m gap. The cut's long sides
stay closed: its `cut_wall` walls (2.2 m) stand on the rim everywhere but the crossings, so a drop into the cut starts on a bridge / the ramp's
crossing; `B_shed` overlaps the north cut wall (footprint X 250..1200 against the wall's 283..433) - the generator should keep buildings off the cut
walls (open question). The builder's own hole links (`Drop_<building>_<hole>`, section 7) stay; their 3 m falls are free under the fall rules.
Rules, defaults (pending the designer) and checks: `docs/systems/stances-and-movement.md` "Falls and drops"; smoke `CodexTactics.FallDropSmoke`.

## 9. Status

| Phase | State |
|---|---|
| 1 reference analysis + profile + schematic | done (`REF_01_02_DEPOT.md`, `ref_01_02_depot.json`, `Saved/Screenshots/LevelGen/`) |
| 2 generator core + evaluation + tests | done (`npm test`: the levelgen suite; every seed 1..8 completable with its variants) |
| 3 department page | done (`/api/levelgen/*`, `LevelGenDepartment.tsx`) |
| 4 Jev + Claude critic | done (real calls verified 2026-11-01; ledger) |
| 5 UE builder + one generated map + smoke | done. `Scripts/levelgen_build_map.ps1` = the commandlet + the navmesh bake; `/Game/Maps/Generated/L_Gen_Depot_1` rebuilt 2026-11-02 from the accepted seed 1 with the designer's answers (539 actors, 4 climbable ladders, the halls' roofs on +3, the chimneys as cylinders, navmesh 440 tiles, bounds up to 12 m) + `Content/Data/LevelJson/L_Gen_Depot_1.json` (minimap floors 300 / 600 / 900). **`LevelGenRouteSmoke` PASSES the whole mandatory route** (24 / 24 legs, 307 s game time, ~74 s wall: the ground loop, the external stair onto the ruin's roof, the roof to the hole's rim, the two hole drops teleported as designed, the tank farm, the dead end); its knownFail is gone. **The stair fix** (2026-11-01 knownFail): the landing was centred on the ramp's top end, so its west half sat over the 35 deg ramp and left a ~52 cm step (above the navmesh agent's 35 cm step) - the landing now starts at the ramp's top (5 cm tucked under it); the stairwell openings end exactly at their ramps' tops (they ran 25 cm past - a gap); the roof / floor legs end on the holes' rims (a point over an open hole is off the navmesh). 2026-11-02 (`tools/levelgen-bot-runs`): the drop nav links (no teleport), the `bot_route`, `LevelGenRouteSmoke` through the bot's route mode (PASS 25 / 25), combat bot runs + the run video (sections 8.1-8.3). 2026-11-02 (`fix/levelgen-ladders-cut`, the designer's bug report): the ladders face away from their walls (the builder's yaw from pos -> foot; `yawToUE` fixed), stand 10 cm off the walls' outer faces, the cut wall opens behind each cut ladder, the trench faces stand outside the trench, the X_centre ramp joins the bridge to the trench floor; `L_Gen_Depot_1` rebuilt (545 actors, navmesh Default 440 tiles); **`LevelGenLaddersSmoke` PASS 13 / 13** (FAIL 1 / 13 on the old map), `LevelGenRouteSmoke` still PASS (section 8.5). |

## 10. The game designer's answers (2026-11-02)

1. The route ends at the SW dead end - kept.
2. Scale B 177 x 124 m - kept (the default).
3. The junkyard stays an ambush field beside the north street, not a route variant.
4. Factory chimneys are NOT ladder towers (no extra height): the `chimney` set-piece, closed 20 m obstacles, the validator refuses
   a ladder / roof / entry on one; the tower ladders are gone (ladders 6 -> 4, inside the profile's 4-7).
5. The canal-floor variant (V3) IS a deliberate kill zone: `intent: 'kill_zone'` on the route and `Z_cut`, reported as intended,
   the critics are told (section 4.5 / 5).
6. The workshop / hall roofs go to +3 = 9 m for the view from the roofs (the sawtooth hall, the warehouse, the Steel Works; the
   ruin keeps its two storeys, roof +2 - the reference reads it as 8 m / 2 floors): hollow halls with one 9 m stairwell flight
   (12.9 m run at 35 deg), walls 9 m, the profile's `height_levels` target 4-5, the elevated-vision roof views (section 4.3).
