# S2 plan - room layouts (building generator, levelgen v3)

Owner: Claude (building generator). Status: **PLAN, no code** (2026-11-19, branch `claude/building-generator`). Read:
`gdd/*.md`, `BUILDINGS_TECH_SPEC.md`, `BUILDING_GENERATOR.md` 1 / 3 / 4 / 7, ADR-0001..3, `generate.ts` (`addBuilding`,
`planStairCore`), `blockTemplates.ts`, `schema.ts`, `buildingTraversal.ts`, `tacticalEvaluate.ts`, `buildingRoutes.ts`,
`buildingStories.ts`, `storyPass.ts`, `rotation.ts`, `Scripts/Editor/build_levelgen_map.py`. Numbers assume commit 20b02316
(approach box 200, a wall between the two core lanes; it landed while this plan was written). All sizes in cm.
**Verified** = read in code or measured by a read-only probe; *proposal* = a number to tune; everything else is design.

## 1. Summary

- **Recommendation: a band grammar** - one slicing engine, archetypes as data. A storey is a 1D sequence of **bays** along the
  long axis (core bay, room bay, flat bay, end bay); each bay has a **cross recipe** over the depth (back rooms / core -
  corridor - front rooms). Corridor-first slicing gives the engine, the template idea survives as the per-archetype recipe
  table, and today's BSP stays only as the fallback for footprints below an archetype's minimum.
- **Order changes**: core -> rooms -> interior doors -> exterior doors -> windows -> balcony / escape slots -> emit. Today
  `addBuilding` places exterior doors and windows *before* rooms and without looking at the core.
- **Topology becomes explicit**: every door carries `links` (the two rooms it joins) and a `role`; rooms carry `unit` (flat,
  suite, shop) and `zone` (wing, common). Stories 5 / 17 / 19 and the balcony escape of story 4 use these handles, and are
  expressed as **opening states** (a sealed door), never as wall edits - so the S1 rollback stays a building clone.
- **Before S2**: one building graph instead of three, `addBuilding` split into a pipeline with a multi-core
  `planStairCore`, the builder building interior walls from `partitions[]` (section 7).
- Verified by probes on `accepted/gen_bgen_test_x6.json` (52 buildings, 41 enterable, 18 with a core; generated
  2026-11-18 with the 150 approach - the window and BSP code is unchanged since, the counts may shift a little at 200):
  1. the builder plans **3 713 actor specs**, 2 121 of them in buildings, one `StaticMeshActor` each (no instancing); a
     2-storey block with 8 rooms = 92 specs;
  2. a building turned 17.3 deg with the Training Ground's 0.01 cm rounding **loses all 8 door lintels** in
     `builder.plan()` (`_collinear_gaps` compares coordinates exactly); the door-state actors sit behind the same match (code read);
  3. **9 of 18** core buildings have a ground window opening onto flight 0 (19 windows beside a flight or its slab gap);
     the validator still counts such a window as a street entry;
  4. **13 of 114** partition ends land inside a window opening (7 of 23 partitioned buildings), 29 more within 120 cm of a
     window's edge: the BSP split never looks at the windows placed before it.
- `CodexTactics.LevelGenBuildingsSmoke` (P0b) **does not exist yet** (only in the docs): S2 has no per-room engine gate
  until step S2.9 lands it.

## 2. Approaches compared

| | A. Band grammar (corridor-first slicing) | B. BSP with constraints (extend `partitionRooms`) | C. Authored floor plans per archetype |
|---|---|---|---|
| Idea | bays along the length x bands over the depth, on the module grid; archetype = a recipe | recursive splits with keep-outs, then a door graph and a corridor found afterwards | hand-drawn parametric plans (a Soviet "section"), stamped and stretched to the lot |
| Reads as its archetype | yes: straight corridors, flats round a landing, two wings | no: a hospital and an office look the same | best |
| Handles for stories | by construction: corridor, wing door, unit, zone, dead-end suite, balcony column | must be discovered by graph analysis after the fact | by authoring, per plan |
| Contract 7 (grid 100 / 50) | by construction | splits at 0.35-0.65 of a span: needs snapping + re-check | by authoring |
| Validity | by construction; the validator is a safety net | by rejection: every rule (window, jamb, triad, loop) is a retry | pre-validated per plan, not per stretch |
| Variety | medium: core position, bay widths, merges, enfilades, end types | high, but shapeless | low: = number of plans |
| Any rectangle | from 8 x 9 m; below that falls back to B | yes | only sizes someone drew; needs a fallback anyway |
| Cost (agent sessions) | 6-8 (S2.1-S2.5) | 3-4, then 3-4 more for corridors / flats / rollbacks | 2 for the stamper + ~24 plans (4 archetypes x 3 sizes x 2 storey kinds) + stretch rules |
| Failure modes | sameness; rectangles only (no L / U wings); the 900 x 350 core bay eats small footprints | windowless inner rooms, doors that cannot get a triad, partitions landing on windows (today's bug), high rollback share, no dead-end flat on demand | a lot nobody drew a plan for; stretch breaks a rule the author never saw; every new story needs every plan re-tagged |

**Verdict: A.** B is cheap to start and expensive to finish (each S2 requirement is a filter over random output, and stories
need structure B does not have). C gives the best single building but its real work is the stretch rules - which is A.
A keeps C's strength as data (`building_plans.json`: recipes and knobs) and B as the last fallback, already written.

## 3. Recommended algorithm

### 3.1 Frame, grid, constants

The planner works in its own integer frame: `u` along the long side (length `L`), `v` across (depth `D`), `v = 0` on the
**core wall**, origin at a footprint corner, wall *axes* as coordinates. It emits only existing coordinate types (room
bounds, partitions, openings) through the same draft -> UE mapping as the rest of `addBuilding`; all new fields are ids.

| Constant | Value | Source / note |
|---|---|---|
| Wall-axis grid / opening-centre grid | 100 / 50 | contract 7.2 (modules 100-300; door and window centred in a 200 / 300 module) |
| Footprint | multiples of 100, snapped down (<= 99 lost per axis) | today footprints are jittered floats (11.5 x 16.3 m) |
| Exterior wall / partition thickness | 30 / 20 | today's greybox (`BUILDING_T`); the kit is 20-25 - resolved at the kit swap |
| Door | 150 x 240, `wall_door_200` (interior, back) / `wall_door_300` (main) | 7.2; today 120 x 210 in a 180 wall gap |
| Window | 150 wide, sill 95, head 245, in a 200 / 300 module | 7.2; today 120 wide, head 210 |
| Core bay | 900 along x `K`; `K` = 350 (2 lanes) / 200 (1 lane) | `planStairCore`: 15 + 829 along, 15 + 320 / 160 across; one special kit module, the grid resumes at its edge |
| Corridor `C` | 200 double-loaded; 250 beside a 2-lane core (350 -> 600); 300 hospital / school (*proposal*) | GDD 1.8-2.4 m |
| Room (axis to axis) | 400-800 x 400-800 | 4 m = one window + its 120 clearances, and a 350 triad lane |
| `service` room | 200-350 x 300-500, no window, exempt from the triad rule | the core band beside a core (store, WC) |
| Jamb zone | no crossing wall / opening within 100 of a door's opening edge -> door centre >= 200 from a crossing wall axis | BUILDING_GENERATOR 4.4: jamb slot 30 + agent radius 35 |
| Full triad lane | 150 wide x 400 deep, free, along the door normal | same 4.4: rear slot 350 + radius |
| Window clearance | opening edge >= 120 from a partition axis, a door edge, a corner; >= 60 jamb to jamb | grammar 3 rule 7, 4.4 |
| Windows per facade span `w` | 1 if `w` < 700 (centred), 2 if 700-800 (at 200 and `w` - 200) | rhythm 3-4 m |

### 3.2 Pipeline (per building, plans on)

Inputs: footprint, archetype, storeys, roof kind, street side and requested door sides (template), neighbours' footprints,
plan seed. Output: the existing building fields + section 4.1.

1. **Shell**: snap the footprint; pick the core wall (block, shop: the long wall away from the street; else as today).
2. **Core(s)**: count and bay position from the recipe; `planStairCore` per bay (parametric, section 7 item 2). No fit ->
   fallback 3.8.
3. **Bay sequence** along `u` from the archetype's 1D grammar (3.4), widths in 100 steps, the remainder given to the widest
   rooms first.
4. **Cross recipe per bay** -> rooms as an exact rectangle tiling, for the `ground` and the `typical` storey (same wall axes
   on every storey, so partitions and balcony columns stack).
5. **Units and zones**: flats, suites, wings, shop, common.
6. **Interior doors** (3.5): spine tree first (connectivity), then second doors and loops; positions snapped; jamb / triad
   checked while placing.
7. **Exterior doors** on the requested sides (+ the opposite side above 150 m2, as today): slots = a room on that wall
   turned into a ground-storey lobby, or a corridor end. The slot nearest to the legacy draw `t` is taken (the same
   `random()` call as today, so the shared stream stays aligned); no slot on a side -> the next side + a warning.
8. **Windows** per room and facade span (3.6); balcony / escape **slots** on storeys >= 1.
9. **Emit** partitions + openings, interior walls, rooms, units, slab gaps (unchanged), window mantle edges (as today).
10. **Validate** (4.2). On an error: next plan sub-seed (max 4), then the fallback chain (3.8).
11. Finalize, rotate, **stories** - unchanged from S1 (post-finalize), with the new bricks of 4.4.

### 3.3 Bays and cross recipes

```
double-loaded (office, hospital / school; D >= 1000)          d = door, P = pad portal (200, no leaf), === = core side wall

v=D +--------+--------+-----------------+--------+--------+
    | F      | F      | F       | F     | F      |        |   front rooms, >= 400 deep
    +---d----+---d----+----d----+---d---+---d----+  end   |
    |                 corridor C                 d  suite |   one door, in the corridor's end
    +---d----+---d----+-P=============P-+---d----+        |
    | B      | B      |  core bay 900   | B      |        |   back rooms, 400-600 deep
v=0 +--------+--------+-----------------+--------+--------+   core wall
      room     room          core          room     end

landing (block section, shop + flats upper storeys; D >= 1000)       flat L = A + B + C on the left, flat R on the right

v=D +----------+-----------+-----------+----------+
    | B  front d C  front  | C  front  d B  front |           front rooms, >= 400 deep
600 +----d-----+-----d-----+-----------+----d-----+           second entry of flat L through C = the storey's loop
    | A        d    landing 250        d  A       |           flat R has one entry = a dead-end flat
350 |  back    +-P===================P-+   back   |
    |          |     core bay 900      |          |           ground storey: one A = lobby (courtyard door)
v=0 +----------+-----------------------+----------+           core wall = courtyard side
       flat             core               flat
```

| Bay | Width | Cross recipe (from the core wall) |
|---|---|---|
| `core` | 900 | core `K` - corridor / landing (starts at `K`: locally wider) - front rooms (1-2 across the bay) |
| `room` | 400-800 | double-loaded: back room `B` (400-600) - corridor - front room `F` (400-800). Single-loaded (800 <= D < 1000): `service` room - corridor - front room |
| `flat` (block) | 400-700 | back room - front room, split at the landing's far axis; no corridor |
| `end` | 400-600 | one suite over the whole depth, its only door in the corridor's end; or `exit`: the corridor runs to the end wall (exterior door, or a window = escape slot) |
| `lobby` (ground storey) | a room of a `room` / `flat` / `core` bay | exterior door + a 200 portal to the corridor / landing; taken out of its flat |

The core bay emits, besides the flights and the lane wall: a **side wall** along the run on its open side (the two pads at
the ends stay open as 200 portals to the corridor / landing). The active pad alternates per storey (flight 0 arrives at the
far end, flight 1 at the near end), so the landing runs the whole bay and both pads open onto it on every storey.

### 3.4 Archetype recipes

| Archetype | Min footprint check | Bay grammar | Units / zones | Ground storey | Hooks |
|---|---|---|---|---|---|
| **office** 1-2 st. | 8 x 10: 1-lane core 200 + 200 + 400 = 800 ok; a core needs a long side >= 900 | `core room* (end \| exit)` or `room* core room*`; double-loaded when D >= 1000 | each room alone; `end` = unit `suite` | lobby = a front room on the street side | dead-end suite (17), locked wing later |
| **block** 2-3 st. | 10 x 18: 350 + 250 + 400 = 1000 ok; section 1700-2300 | `(flat core flat)+`; `core flat` when L < 1700; sections divided by a wall without doors (they meet on the roof) | storeys >= 1: flat = its flat bay (2 rooms) + the nearer front room of the core bay = **2-3 rooms**; zone `common` = stairwell + landing | back room beside the core -> lobby (courtyard door); one front room of the core bay -> lobby (street door) | balcony column per flat (4); single-entry flat = dead end (17); section wall = link / breach candidate (14, S4) |
| **hospital / school** 2-3 st. | 12 x 30: 400 + 300 + 500 = 1200 ok | `wing \| wing`, wing = `(end \| exit) room* core room*`; a wall across the middle, the corridor crossing it by a `wing` door | zones `wing_a`, `wing_b`; wards / classrooms 1-2 bays | wing A lobby on the street wall, wing B on the back or its end | `wing` doors (5), end-of-corridor escape slots (5), barricaded corridor (15), dead-end suites (17) |
| **shop + flats** 2-3 st. | 10 x 15: core at an end (900) + one flat bay 600 | upper storeys: one block section (`core flat`) | zone `shop` (hall + store), `common`, flats | front band of all bays merged into the `shop` hall: shutter door + `shop_window`s to the street; back band: core, lobby (back door), the shop's store room where a second flat bay exists; a `service` door joins the shop to the landing / lobby | `service` door (19) |

Sheds, pumps, garages, ruins, halls: unchanged (BSP / hand-shaped / hall). Depth above 1700: not planned in S2 (no such lot).

### 3.5 Door graph (pipeline step 6)

1. **Spine**: every room gets a door to the corridor / landing in front of it; a flat gets one `unit_entry` (the landing's
   end wall or its front wall) and a chain of `room` doors inside. This alone makes every room reachable.
2. **Second door**: rooms >= 700 wide (> 25 m2, 2 windows) get two corridor doors (grammar rule 5).
3. **Loops**: every storey segment (office storey, block section, wing) with >= 3 rooms gets >= 1 `enfilade` door between
   two neighbours that both have corridor doors (`p_enf` 0.35 per pair, at least one forced, *proposal*); a block section
   gives one flat per storey a second entry (landing - back room - front room - middle room - landing). Check: cyclomatic
   number `E - V + 1 >= 1` on the segment with its dead-end units removed.
4. **Dead ends** (story 17): `end` suites and single-entry flats are tagged `dead_end` with `run_cm` = the walk from the
   nearest stair pad; eligible when `run_cm` >= 800.
5. **Positions**: centre on the 50 grid, >= 200 from a crossing wall axis; doors facing each other across a corridor are
   offset >= 100 where the walls allow; nothing in the core bay's walls except the two pad portals. Exception: a door that
   closes a corridor's or a landing's end (its jambs are the corridor walls).
6. **Breach triad**: per door side the validator computes `full` (free jamb zones + the 150 x 400 lane) or `stack` (in line,
   >= 150 deep); every room except `service` needs >= 1 door with a `full` side. By construction: rooms are >= 400 deep
   along their door's normal, so the room side is `full`; the corridor side (200-300 wide, or a corridor end) is `stack`.

### 3.6 Windows, balconies, fire escapes

- Windows per room and exterior wall span by the table of 3.1; none on the core bay's exterior wall on any storey; one in
  each corridor end wall that is not an `exit` door.
- **Slots instead of new geometry fields**: on storeys >= 1 one window per flat, in the same column on every storey, on a
  long wall other than the core wall, is marked `slot: 'balcony'`; a hospital wing's corridor-end window is marked
  `slot: 'escape'`. A brick turns the slot window into a door at the same point and adds the balcony (150 deep everywhere:
  one module, an escape can land on any).
- **Story 4** stops using "any exterior-wall room" (`escapeRoom`): `add_balcony_escape` takes a flat **column** whose slots
  exist on storeys 1..n-1 on a wall without ground doors. Route: ground - escape - balcony 1 - flat 1 - landing - up.
  Buildings without units (legacy, BSP fallback) keep the S1 rule.
- Base balconies of the block archetype (firing positions without an escape): the same brick at generation, share
  `p_balcony` (question Q3).

### 3.7 Interior cover hooks for P3

A pure function `interiorAnchors(layout, building)` (not stored in the layout; P3 stores the chosen pieces in the props
layer `interior`), per room:
- **keep-outs**: jamb zones and triad / stack lanes of every door, a 120 walk lane door to door, a 150 x 80 standing cell at
  every window, the core's approach boxes and pads, hatch and ladder feet, balcony doors;
- **anchors** `{ room, pos, yaw, class: low | tall | stub, why }`: `door_flank` (the corner-peek stub of BUILDING_GENERATOR 4.4), `back_wall`
  (cover facing the triad door), `window_flank`, `centre` (rooms >= 25 m2), `corridor_alcove` (the idle pad, 300 corridors).
S2 acceptance: every room > 9 m2 has >= 1 anchor where a 100 x 60 piece fits outside the keep-outs. P3 must also extend
`blockedOn` (`buildingTraversal.ts`) to covers and props: today it tests partitions and interior walls only.

### 3.8 Fallbacks, rotation, determinism

- **Fallback chain**: next plan sub-seed (max 4) -> simpler recipe (double- -> single-loaded -> core + one room per storey)
  -> today's BSP (`partitionRooms`) for footprints under the minimum (D < 800 or L < 900) -> one room. No core fit: the
  building becomes 1-storey (the external-escape fallback of building-archetypes is a later step). Recorded in
  `plan.fallback`; map report `plan_fallback_share`.
- **Rotation**: the plan is local and axis-aligned; it emits only types that `finalizeDraftToLayout`, `rotateContent` and
  the builder's `_building_local` already transform; sides are named by **wall id** (`<bid>_N`), never by a `Side` label
  (the draft and the UE frames name sides differently). Grid checks run after `toBuildingFrame` with a 1 cm tolerance.
- **Determinism**: plan seed = `fnv1a(bid | footprint | storeys | archetype)` + the re-roll step; integer cm; ids sorted
  wherever a choice iterates; no draw from the caller's stream beyond today's one per door side. Plans on => schema `/2`.
- **Toggle**: `GenParams.building_plans` (page «Планировки зданий (S2)», CLI `--plans`), off by default: `/1` output byte
  for byte as today.

## 4. Data / validator / route-model changes

### 4.1 Fields beyond TECH_SPEC section 2 (all optional, ids and scalars only)

```ts
type Archetype = 'hall' | 'warehouse' | 'office' | 'block' | 'hospital' | 'shop_flats' | 'shed' | 'ruin' | 'gallery' | 'shell';
type OpeningRole = 'entry' | 'unit_entry' | 'room' | 'enfilade' | 'wing' | 'service' | 'portal' | 'balcony' | 'shop_front' | 'shop_window';
interface WallOpening { /* existing */ links?: [string, string];          // room ids | 'street' | 'balcony:<id>'
  role?: OpeningRole; slot?: 'balcony' | 'escape'; module?: string }      // module = kit name (wall_door_200 ...)
interface GenRoom { /* existing */ kind?: /* existing */ | 'lobby' | 'service' | 'shop'; unit?: string; zone?: string;
  walls?: string[] }                                                      // exterior wall ids it touches (replaces roomOnFacade)
interface BuildingUnit { id: string; kind: 'flat' | 'suite' | 'shop' | 'common'; floor: number; rooms: string[]; entries: string[];
  column?: string; dead_end?: boolean; run_cm?: number }                  // column = the stack of flats sharing balcony slots
interface GenBuilding { /* existing */ archetype?: Archetype; units?: BuildingUnit[];
  plan?: { v: 1; kind: 'bands' | 'bsp' | 'single'; seed: number; core_wall: string; street_wall: string; fallback?: string } }
```

Defaults by role (`openingStates.ts` + its Python mirror): `shop_window` -> `fixed` (the marker the default table lacks
today), `portal` -> `open`, `unit_entry` -> `closed` (question Q2). Every opening of a planned building has an `id`
(`openings[i]` index refs of S1 are not stable once rooms add openings). `floors_detail[].rooms` (a second copy of
`rooms`, read by nothing but the transforms) is not written for planned buildings.

### 4.2 Validator (`buildingTraversal.ts`, planned buildings)

| Code | Severity | Rule |
|---|---|---|
| `room_tiling` | error | rooms + core tile each storey exactly (no overlap, no gap) |
| `off_grid` | error | wall axes on 100, opening centres on 50 (1 cm tolerance) |
| `room_size` | error | outside the bounds of its kind (3.1) |
| `room_no_door`, `opening_no_links` | error | a room no opening links to; a door / portal without `links` or `id` |
| `storey_no_loop` | error on the base plan, warning after a story | 3.5 rule 3 |
| `door_jamb_blocked`, `room_no_triad` | error | 3.5 rules 5-6 |
| `corridor_narrow` | error < 120 clear, warning < 180 | BUILDING_GENERATOR 4.1 |
| `opening_into_core` | error | a door or window (not a pad portal) beside a flight run, its slab gap or an approach box |
| `stair_pad_unlinked` | error | a pad without a >= 200 opening onto a corridor / landing |
| `window_rule`, `exterior_doors_one_side` | warning | a facade room without a window / a window inside its clearances; > 150 m2 with doors on one side |
| `wing_no_uproute`, `zone_leak`, `dead_end_short` | error (story 5 / 19 / 17) | each wing has an entrance and an up-route; shop and flats stay separate; `run_cm` >= 800 |

### 4.3 Route model

- Built on the one graph of section 7 item 1. Nodes = **portals** (openings, flight feet and tops, pads, hatch and ladder
  feet) + one anchor per room; inside a room portals are joined by straight segments (rooms are rectangles), in a corridor
  only neighbours along its axis. Reason: with one node per room a 40 m corridor costs "door - centre - door" (up to 40 m
  for doors 3 m apart), which breaks F4 entry choice and the 15 % drift gate (F5).
- `links` replace the 60 cm proximity test; without `links` (legacy) the old inference stays as the fallback.
- Reports per building: rooms, units, loops per storey, dead-end units, `plan.fallback`; per map: `plan_fallback_share`,
  rooms per archetype, distinct plan signatures per archetype (variety).

### 4.4 Story bricks for S2 (all as opening states)

| Brick | S2 form |
|---|---|
| `split(wall)` (5) | every `wing` door `sealed` + `block_flight` on wing B's 0 -> 1 + escape on wing B's `escape` slots |
| `dead_end(flat)` (17) | pick a `dead_end` unit with `run_cm` >= 800; seal its second entry / enfilade doors if any |
| `isolate(zone)` (19) | `service` doors `sealed`; the shutters per Q1 |
| `add_balcony_escape` (4) | by flat column and slots (3.6) |

`STORY_PICK_KNOBS.weights` gets the archetype dimension of GDD F1 (`w[r][a][s]`); stories 5 and 19 are archetype-bound.

### 4.5 Builder

Interior walls from `partitions[]` through `_slice_wall_with_openings` (section 7 item 3); doors 150 x 240 with the wall gap
equal to the opening; pad portals without a lintel; core side wall; a blocking fill under every flight (no navmesh pocket
under a ramp) and a rail at the open end of every slab gap; base balconies; a per-building spec count in the build report.

## 5. Step list (one task at a time; level = model-router tier of the core part)

**Refactors first** (section 7):

| # | What | Files | Tests | Acceptance |
|---|---|---|---|---|
| R1 (L3) | `addBuilding` -> pipeline `buildings/{shell,core,openings,emit}.ts` with a `BuildingPlan` in local cm; `planStairCore` takes a bay (start, wall, id prefix) and may be called per wing; `GenBuilding.archetype` + template -> archetype table. Starts from 20b02316 | `generate.ts`, new `buildings/*`, `blockTemplates.ts`, `schema.ts` | golden: x1 / x6, 5 seeds, toggle off = byte-equal to before; `planStairCore` unit at 3 bay positions | all existing tests green; no behaviour change |
| R2 (L3) | one graph `buildingGraph.ts` (portal nodes, ids namespaced by building); `validateBuilding` (rooms, levels, roof) and `computeBuildingRoutes` consume it; `checkBuildingReachability` becomes a wrapper | `buildingRoutes.ts`, `buildingTraversal.ts`, `tacticalEvaluate.ts`, new file | equivalence on x1 / x6, 5 seeds: same `unreachable_*` sets; same fast / quiet entries on >= 95 % of buildings, every difference listed in the report | the three old edge lists are gone; story tests green |
| R3 (L2) | builder: interior walls from `partitions[]`; 0.05 cm tolerance in `_collinear_gaps` / `_merged_building_edges` for legacy files | `build_levelgen_map.py`, `tests/test_build_levelgen_buildings.py` | new: a building turned 17.3 deg with 0.01 rounding keeps every lintel and state actor (fails today: 8 -> 0); legacy bit-identical test | both tests pass |
| R4 (L2) | `BUILDING_COORD_FIELDS`: one table of coordinate-bearing paths used by `finalizeDraftToLayout`, `rotateContent`, `_building_local`; cost table -> `building_route_costs.json`, knobs -> `building_stories.json` | `generate.ts`, `rotation.ts`, builder, `buildingRoutes.ts`, `buildingStories.ts` | coverage: translating / rotating a building with every field set moves every coordinate | hash = of the JSON file (ADR-0002) |

**S2**:

| # | What | Files | Tests | Acceptance |
|---|---|---|---|---|
| S2.1 (L3) | band engine: constants, bays, cross recipes, tiling, fallback chain, `planToAscii(b, floor)`; **office**; toggle | new `buildings/bandPlan.ts`, `building_plans.json`, `schema.ts` | property test, seeds 1-200 x footprints 8 x 9 .. 18 x 14 in 100 steps: tiling exact, grid, sizes, determinism; ASCII goldens for 3 seeds | 0 failures; fallback share reported |
| S2.2 (L3) | door graph, exterior doors, windows, `links` / `role` / `module`; validator codes of 4.2 | `buildings/planDoors.ts`, `planWindows.ts`, `buildingTraversal.ts` | per rule one failing fixture + the property test extended (every code = 0 errors) | offices pass `validateBuilding`, seeds 1-200 |
| S2.3 (L3) | **block**: sections, landing, flats, lobbies, slots, base balconies; story 4 by column; `dead_end` tags | `bandPlan.ts`, `buildingStories.ts`, `blockTemplates.ts` (`two_storey` -> archetype block, 2-3 storeys) | block fixtures 10 x 18, 14 x 20, 12 x 36 (two sections: engine fixture, its template waits for Q6); story 4 key-route modes equal the fixture's sequence | every room / storey / roof reachable, >= 1 loop per section storey |
| S2.4 (L3) | **hospital / school**: template + catalog entry, two wings, two cores, wing doors, escape slots; story 5 | `blockTemplates.ts`, `levelgen_v2.json`, `bandPlan.ts`, `buildingStories.ts` | fixtures 12 x 30, 12 x 60; story 5 applied and rolled back byte-equal; `wing_no_uproute` fixture | both wings reachable with the wing doors sealed |
| S2.5 (L3) | **shop + flats**: template, shop zone, `shop_window`, `service` door; story 19 (after Q1) | same + `openingStates.ts` and its Python mirror | fixture 10 x 15; `zone_leak` fixture; default-state unit for the new roles in TS and Python | flats reachable from the back door only; the shop per Q1 |
| S2.6 (L2) | builder items of 4.5 + spec-count budget test | `build_levelgen_map.py`, tests | pure `plan()` tests (no editor): specs per building and per map against a recorded baseline | counts in the build report |
| S2.7 (L2) | page: plan overlay (rooms by unit / zone, door roles, loops, triad flags), «другой план» per building (drops and re-runs that building's stories), map summary, CLI `--plans` | `LevelGenDepartment.tsx`, `planView.ts`, `storyPass.ts`, CLI | re-roll determinism; locked buildings keep plan and stories | the designer can read a plan before any build |
| S2.8 (L2) | `interiorAnchors` (3.7) | new `buildings/interiorAnchors.ts` | every room > 9 m2 has an anchor that fits; the 120 lane exists | handed to P3 |
| S2.9 (L3, C++) | **P0b** `LevelGenBuildingsSmoke`: per building a complete navmesh path from each entry to every room anchor, storey landing and roof; squad leg through every `unit_entry`; drift (F5). Then X6 with plans built on the full checkout (the designer's editor closed) | `Source/CodexTacticsDev/Private/Debug/`, `levelgen_build_map.ps1` | the smoke itself; ramp / ladders smokes re-run | PASS >= 95 % buildings, drift <= 15 % for >= 95 %, navmesh build <= baseline + 20 % |
| S2.10 (L1) | docs: building-archetypes 1 / 3 / 4 / F4 (core needs a long side >= 9 m with the 200 approach, not 8), building-stories 3 (bricks as states), TECH_SPEC 2 / 2.1, ADR-0002 addendum, HANDOFF, TASK_BOARD | docs | - | the numbers of this plan are in the GDDs |

Order: R1 -> R2 -> R3 -> R4 -> S2.1 ... S2.5 -> S2.6 -> S2.7 -> S2.8 -> S2.9 -> S2.10. R2 / R3 touch disjoint files and
could run side by side if two tasks are ever allowed; S2.9's C++ part can be queued for an API agent from the start.
Review gate after S2.3: three ASCII / page plans per archetype shown to the designer before S2.4.

## 6. Risks and early detection

| # | Risk | Detect early by | Mitigation |
|---|---|---|---|
| 1 | The TS graph says "reachable", the navmesh disagrees - now for hundreds of rooms, with no per-room engine gate (P0b missing) | S2.9 smoke per room (PASS share, drift); until then R3's rotated-building test and `opening_into_core` | S2.9 queued at once; walls and openings from one record (R3); no planned map published while the smoke FAILs |
| 2 | Actor count and navmesh build time: no instancing, 3 713 specs on X6 today, a planned 3-storey block is estimated at 250-300 specs, a hospital 500+ (estimate, not measured) | S2.6 spec-count test in pure Python on every change; X6 navmesh build time at S2.9 | one ISM per building for untagged cubes (walls, slabs); tagged pieces stay actors |
| 3 | S1 stories roll back more on planned buildings (slots, many window entries change F4, ids move) | the S1 distribution test on planned maps: rollback <= 10 %, role shares +- 10 pp; `plan_fallback_share` <= 10 % | bricks read units / slots / roles, not geometry; fallback counted per archetype |
| 4 | Squad jams in 200 corridors and at doors (designer bug 11-02 item 7; give-way merged only recently). The contract door is 150 = 80 cm of navmesh after the agent radius; every smoke so far ran on today's built 180 gap | S2.9 squad leg: time <= 2 x solo per `unit_entry`; `LevelGenRampSmoke` at every pad | corridor 250-300 where the depth allows; pads joined to the landing over 200 |
| 5 | Sameness: every office is the same corridor | distinct plan signatures per archetype over 50 seeds (map report) | core position, bay widths, merges, enfilades, end types; per-storey door variation |
| 6 | The 900 x 350 core bay dominates small footprints (31 % of a 10 x 10 storey) | rooms per storey and room area share in the property test | 1-lane core where the roof is not walkable; 1-storey archetype under the minimum |
| 7 | Navmesh pockets under flights; an open slab-gap edge beside the idle pad (a 3 m fall) - both follow from today's geometry, not yet checked in engine | navmesh island count per building in S2.9 | under-flight fill and gap rail (4.5); rail kind = archetypes Q9 |
| 8 | A new coordinate field forgotten in one of the three transform lists | R4 coverage test | S2 adds ids only |
| 9 | Upper-storey interiors unreadable from the tactical camera | first built X6 screenshot per storey to the designer | out of generator scope (camera cutaway); flagged before S3 |
| 10 | Generation time (page budget 5 s on x6) | node timing test of `generateCandidate` with plans + stories | the planner is O(rooms); Dijkstra with a heap if R2 shows it |

## 7. S1 architecture review (ranked by impact on S2-S4)

1. **Three graphs with the same rules written three times.** `checkBuildingReachability` (`tacticalEvaluate.ts`, rooms),
   the level graph inside `validateBuilding` (`buildingTraversal.ts`, "levels:" block), `buildRouteGraph`
   (`buildingRoutes.ts`) each re-implement doors by state, blocked / `down_only` flights, escapes, snow ramps, hatches,
   ladders, holes - with small differences (`fallbackSingle` defaults, balcony doors, external stair feet). A fourth copy
   lives in `Scripts/Tools/levelgen_building_audit.py`. The copies can disagree (reachable for the validator, no "top" for
   the route model: `pickTop` then returns null and the building silently has no key route); ten times more rooms make
   that likely. Node ids are not namespaced (`street`, `roof`), so S3 building links need a rewrite anyway -> step R2.
2. **`addBuilding` (`generate.ts`, ~355 lines) does shell, doors, windows, loopholes, walls, core, BSP, parapets, hatch and
   slabs in one closure, openings before rooms.** Measured consequence: windows onto flights (section 1). `planStairCore`
   is fixed to the low-end corner of one long wall, one core per building - a hospital needs two. No `archetype`: stories
   key off `set_piece` (`RUBBLE_ESCAPE_SET_PIECES = ['facade']` = block, office and shell alike) -> step R1.
3. **Interior walls exist twice** (`partitions[]` and pre-split `walls[]` of kind `interior`); the builder builds from
   `walls[]` and re-finds each door by `_collinear_gaps` (exact `ax == bx`, gap 40-300, door within 30 cm). Verified: after
   the 0.01 cm rounding of a rotated block all 8 of its door lintels are lost, and `opening_state_specs` sits behind the same match -
   a `sealed` or `barricaded` door of a rotated building would be an open gap in engine (code path read, not run for `/2`).
   Also `applyRecipe` (`buildingStories.ts`) clones the building and the ladders only: a brick that edits `layout.walls`
   cannot be rolled back. S4 breach panels meet the same split -> step R3, and "bricks are states" (4.4).
4. **Topology by proximity.** `ROOM_TOUCH_TOL_CM = 60` (`tacticalEvaluate.ts`), `inBounds(..., 60)` (`buildingRoutes.ts`),
   `roomOnFacade(tol 30)` and `escapeRoom` = "first room by id on a facade" (`buildingStories.ts`). With 200 corridors and
   300 service rooms a door near a T-junction joins three rooms: a phantom edge is the dangerous direction (the validator
   passes what the engine cannot walk) -> `links`, `unit` (4.1), inference only as the legacy fallback.
5. **Three coordinate conventions in one building**: `footprint` / room bounds in the building frame, openings /
   partitions / stairs in world coordinates, balconies / fire escapes / snow ramps in the building frame again; plus the
   draft frame with other side names. `rotateContent` (`rotation.ts`) does not move balconies, escapes or ramps - harmless
   only because stories run after rotation; a region re-roll that moves a storied building would break it -> step R4.
6. **ADR-0001 / ADR-0002 are not met by the cost data**: the table is the TS constant `ROUTE_COSTS`, `routeCostHash` is
   FNV-1a of that object, there is no `building_route_costs.json` for C++ to read. The PMC AI will also need room and unit
   ids at runtime (stories 16, 17, PMC_DEFENCE); no C++ code reads `rooms` or `door_ids` today (git grep on `Source/`). Decide
   the runtime carrier (tactical JSON) before S3 / R4 -> step R4 for the JSON.
7. **The default-state table is implemented twice** (`openingStates.ts`, `build_levelgen_map.py` `classify_opening` /
   `effective_opening_state`), C++ will be the third. Role-based defaults (4.1) widen it: either one shared JSON table or
   states written explicitly at «Принять» (the fallback ADR-0002 already names).
8. **No instancing in the builder** (`Placer.place`: one `StaticMeshActor` per cube), while ADR-0002 budgets <= 40
   non-instanced actors per building: a block is at 92 before S2 -> risk 2.
9. **Minor**: `exteriorEntries` de-duplicates `b.doors` against `b.openings` by exact float equality; `blockedOn` ignores
   covers / props / ladders in approach boxes; `shortestFromStreet` is O(V^2) with a path copy per label; the page keeps
   pre-story buildings in memory only, so a loaded map cannot re-roll (the same will hold for plans).

## 8. Open questions for the designer

| # | Question | Recommendation |
|---|---|---|
| Q1 | Story 19: the shop loses its inner door and its shutters are `sealed` - then its rooms are unreachable, against pillar 1. (a) a back service door, (b) one shutter `locked` (kick / shotgun, loud), (c) the shop becomes a closed volume | (b): flats only from the courtyard, the shop only by a loud breach from the street |
| Q2 | Flat entrance doors: `closed` (1 s to open, a leaf to breach later) or `open` like other interior doors? Stairwell of a block: enclosed with flats behind doors (proposed), office: open lobby | `closed` for flats |
| Q3 | Balconies on every flat of a block from storey 1 by default (firing positions, 150 deep), or only where a story adds a fire escape? Rail 95 is vaultable = a 3 m fall (archetypes Q8) | default on, tall rail until Q8 is decided |
| Q4 | Hospital / school (archetypes Q7): 12 x 30-60 m, 2-3 storeys, corridor 300 (wider than the GDD's 2.4)? A catalog entry and lots that long are needed | yes; 300 leaves room for cover in the corridor |
| Q5 | Minimum room 4 x 4 m (window and breach rules) instead of 3 x 3; smaller only as windowless service rooms | accept |
| Q6 | Long blocks of 2 sections (36-40 m, two entrances, a blind wall between, joined only over the roof)? Today a block is at most 20 x 14 m = one section | yes, as a second template after S2.3 |
| Q7 | S2 behind a page toggle, off until the first planned X6 is reviewed; accepted maps stay as they are | yes |
