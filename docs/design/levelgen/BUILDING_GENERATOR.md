# Building generator - walkable buildings, not boxes (levelgen v3)

Owner: Claude (lead of the topic). Started 2026-11-18 from the designer's report on the generated maps (`L_Gen_Depot_1_X6`, `L_Gen_Depot_X4`):
*"the buildings are boxes of outer walls with small windows and doors, almost always empty inside; ramps, roof climbs, inner rooms and
inner stairs do not work; the operatives often get stuck and cannot go up."*

Related: [LEVEL_GENERATOR.md](LEVEL_GENERATOR.md) (the pipeline), [BUILDINGS.md](BUILDINGS.md) (the schema extension of task 027:
openings, partitions, floor slabs, hatches, parapets, mantle edges), [PROPS_LAYERS.md](PROPS_LAYERS.md), `docs/design/LADDERS.md`,
`Content/Data/LevelGen/obstacle_heights.json`, `DoorwayBreachRules.h`.

**Design + technical spec for the studio discussion: [BUILDINGS_GDD.md](BUILDINGS_GDD.md)** (building stories, opening states,
breach panels, route model, data contract, phases, open questions; 2026-11-18).

**Status 2026-11-18: DRAFT, on hold.** The designer answered the forks of section 2, then asked not to run ahead: the approach (TS
generator vs UE PCG vs Blender building plugins), the building classes and the horde-defence buildings are to be discussed at the studio
level first. Section 1 (audit) is fact; sections 3-6 are a proposal. No generator task is dispatched until that discussion.

Open topics for the studio discussion (designer 2026-11-18):
- Three building classes: **through** (passable: 2+ exits on different sides, a route through, no dead end), **enterable** (interiors,
  storeys, roof), **decorative** (closed, background only). The share and placement of each per level.
- Tool chain: who decides the plan (rooms, stairs, doors, tactics - must be validated) vs who makes the look (kit pieces from Blender
  plugins, assembly by UE PCG shape grammar or our builder). Nothing mass-generated before a pilot of a few buildings is reviewed.
- **Horde defence buildings**: entrances that hold without a permanent jam on the horde side and stay interesting to fight (fallback
  layers outer room -> inner room -> stair -> roof, firing positions over every approach, an inner circulation loop for the squad,
  barricadable openings, an exit); validated in the combat sim, related to Grok's G008 `docs/design/PMC_DEFENCE.md`.
- **Destructible elements** (designer 2026-11-18): doors that can be breached by explosive or broken (kicked / rammed); glass partitions and
  windows that shatter / blow out. Each changes the navmesh, cover and line of sight at runtime, so the plan must mark which openings are
  destructible and what they become (open gap, rubble, frame).
- Later (designer 2026-11-18, "think about it later"): furniture; stair flights inside; external stairs (fire escapes).
- **Real roofs**: not a flat slab with a parapet. Real flat roofs carry many elements - parapets of different heights, vent shafts, AC / HVAC
  units, stair / lift penthouses, skylights, pipes, chimneys, antennas, water tanks, level changes - and they are combat cover. Study
  reference photos; roof furniture follows the cover heights of `obstacle_heights.json` and the clearance / breach rules like interiors.

Sections: 1 audit (what the generator emits, what the builder builds, measured traversal) - 2 design decisions (designer forks) -
3 the building grammar - 4 rules for our mechanics - 5 the traversal gate before «Принять» - 6 phases and owners.

---

## 1. Audit 2026-11-18 (evidence)

### 1.1 Method

- **Static**: `Scripts/Tools/levelgen_building_audit.py` over the accepted layouts (`Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x6.json`,
  `..._x4.json`, `L_Gen_Train_2.json`) + the props files: per building the floor reachability graph (stairs + ladders), the room door
  graph per storey, stair geometry (slope, run, head room, fit in the rotated footprint), covers / props inside the footprint.
- **In engine**: `CodexTactics.LevelGenRampSmoke legs=solo` on the built maps `L_Gen_Depot_1_X6` and `L_Gen_Depot_X4` (headless, fixed step,
  the existing builds - no rebuild): per stair a navmesh probe foot -> top every 1 m, then one operative ordered up with the other two
  standing in the stair mouth. Logs `Saved/Logs/Audit-Ramp-L_Gen_Depot_1_X6.log`, `Audit-Ramp-L_Gen_Depot_X4.log`.
  `LevelGenRouteSmoke` (PASS on both maps, 2026-11-16) only walks the mandatory route R_main (25 waypoints, through `B_ruin` only) - it never
  enters the other 60+ buildings, which is why the defects below were not caught.

### 1.2 What the generator emits (`Tools/TrainingGround/src/levelgen/generate.ts` `addBuilding`, templates `blockTemplates.ts`)

- One rectangle per building; `floors` x 300 cm; walls on the perimeter only, doors on 1-2 sides (`doorSides`), windows 120 x 115 cm
  (sill 95) every ~4 m, **only on walkable storeys** - a hollow building has walkable storey 0 only, so a 9 m wall gets one row of windows
  at 0.95-2.1 m ("small windows").
- `hollow: true` = no floor slabs at all: the "3-storey" hall / warehouse / store / facade is one 9 m empty volume.
- Stairs: `stairwell` = ONE straight 20 cm sloped slab (35 deg, 150 cm wide) per storey pair, placed 120 cm from the end wall and 75 cm from
  the side wall (a corner pocket); hollow buildings get one 13 m flight 0 -> 3 straight to the roof. No landings, no enclosure, no rail.
- Rooms: BSP split only in 1-storey offices / sheds and 2-storey blocks (`partitionRooms`), 120 cm doors, one exterior door.
- Roof: walkable roofs get a 95 cm parapet + a roof hatch with an `ALadderActor` (24 rungs from the ground in halls).
- Covers / props: the layout's `covers` never fall inside a footprint (0 of 792 on X6); the props layers place a few crates by walls.

### 1.3 Results per building type (X6: 68 buildings, X4: 49; numbers are X6, X4 in brackets where they differ)

| Type (set_piece / id) | n | What it is | Measured | Why (cause) |
|---|---|---|---|---|
| **industrial_hall / hall, steelworks** | 7 (8) | 3 "floors", hollow, 2 doors on one side, 1 row of ground windows, 13 m straight ramp 0->3 to the roof + 24-rung hatch ladder | Ramp smoke: **1 of 7 passed** (B_hall_n), 6 STUCK at the foot (X4: 1 of 8; 2 ramps navmesh-broken: B_x2_4_t8_hall has no navmesh for 9 m mid-ramp, B_hall_n not joined) | (C) one 9 m empty volume - nothing to do inside; (B) the ramp mouth is a corner pocket: whoever stands at the foot blocks the only way in, the leader pushes at 14 cm/s for 10 s; rotated halls lose navmesh on the ramp |
| **warehouse / store, warehouse** | 5 (3) | same as the hall | **1 of 5 passed** (B_warehouse); B_x1_17_3_t1004_store: navmesh along the ramp but **no path to the roof** | same (B), (C); the top end does not join the roof slab |
| **facade / block** (2 storeys) | 5 (3) | BSP rooms on floors 0 and 1, stairwell 0->1 and 1->2 (roof), hatch ladder floor 1 -> roof | **5 of 5: the 0->1 ramp navmesh stops 45 cm below floor 1** ("path PARTIAL, ends at z 10") -> floor 1 and the roof are **unreachable** in every block; 0 covers / 0 props inside | (A) `floors_detail` cuts the stairwell gap from `pos - 0.4 m` to `top + 0.4 m`: the floor-1 slab starts 40 cm PAST the ramp's top (the 1->2 flight onto the roof has no such gap and its navmesh joins) |
| **facade / office** (1 storey) | 17 (10) | 2-4 BSP rooms, 1 door | static: every room reachable through the door graph; **12 of 17 have 0 covers and 0 props** | (E) no interior content stage |
| **facade / facade** | 13 (6) | 3-storey hollow shell, **not enterable, no doors, no windows** | a box | (D) a decor shell standing inside the playable blocks |
| **one_entry_compound / shed** | 9 (5) | 1 storey, 2 rooms, 1 door | reachable; 5 of 9 empty | (E) |
| **tank_farm / pump** | 2 (3) | 1 room, 1 door | reachable; mostly empty | (E) |
| **ruined_two_level / ruin, annex** | 2 | hand-made reference building: external stair 0->2, inner stair 0->1, roof + floor holes | external stair passed; **inner stair STAIR_in_8 FAILED on X6** (the operative took the outer stair to the roof and stuck 10 s at the roof hole drop), passed on X4 in 40 s | path through a drop link the mover does not take reliably (F) |
| **ladder_overwatch / gallery** | 6 (7) | solid 3 m block, ladder onto the roof | (ladder smoke - see 1.4) | not a building: an overwatch block |
| **chimney** | 2 | solid cylinder | - | decor |

Totals X6: of 24 stairs, **3 walked up (12 %)**, 10 stuck at the foot, 6 with broken navmesh; of 33 enterable buildings, **18 have 0 covers and
0 props inside**; 13 of 35 "facade" set pieces are closed boxes; no enterable building has more than one storey of real floor that a
squad can reach by stairs.

Causes, in order of damage:

- **(A) stairwell gap overshoot** - `generate.ts` floors_detail gap `[pos - 0.4, top + 0.4]` leaves a 40 cm hole beyond the ramp top; with the
  35 cm agent radius the navmesh cannot cross -> every multi-storey non-hollow building is cut at floor 1. One-line class of bug, but
  nothing validated it.
- **(B) stair mouth pocket** - flight foot 120 cm from the end wall, 75 cm from the side wall, 150 cm wide, no landing: one body in the
  mouth blocks the way up (the designer's "they all get stuck at the bottom", 2026-11-02 item 7, still true).
- **(C) hollow multi-storey volumes** - halls / stores / warehouses are 9 m empty boxes; "floors" exist only in the data.
- **(D) decor facades in the playable area** - closed boxes without doors.
- **(E) no interior stage** - no cover pieces, furniture or props inside rooms; the props layers only hug walls.
- **(F) drop links through holes** are not reliable for the mover; a route that needs one to reach a storey is fragile.
- **No gate** - `LevelGenRouteSmoke` checks one route, `LevelGenRampSmoke` is not run before «Принять», and the TS critic has no building
  traversal check. A building can be accepted with unreachable storeys.

### 1.4 Ladders

`CodexTactics.LevelGenLaddersSmoke map=L_Gen_Depot_1_X6 nocut` (log `Saved/Logs/Audit-Ladders-L_Gen_Depot_1_X6.log`): **48 of 50 legs
passed** - the ladders themselves work (24-rung hall hatch ladders 21-22 s per climb, 8-rung ones 7-9 s). The one failure is the rotated
small gallery `LAD_B_x1_3_1_t1004_gallery` (up: timed out 218 cm from the goal, never started the climb; down: stopped short). But the
blocks' hatch ladders start on floor 1, which cause (A) makes unreachable, so in play they are dead too. Ladders are not the problem; what
leads to them is.

---

## 2. Design decisions (the designer's forks)

Asked 2026-11-18; the designer answered the same day (all four: option (a)), before any code.

| # | Fork | Options | Recommendation | Decision |
|---|---|---|---|---|
| 1 | Geometry now | (a) greybox on a strict module grid (50 cm, storey 300) whose pieces are named modules (wall, wall_window, wall_door, slab, stair_core, rail) so a kit can replace them later; (b) a modular asset kit first | (a): walkability first, the kit swaps in by module name without touching the generator | **(a) greybox on the module grid, kit-ready names** (designer 2026-11-18) |
| 2 | Storeys and interiors | (a) 1-3 storeys + roof, ~60 % of buildings enterable with full interiors, closed shells only at the map edge; (b) up to 4-5 storeys; (c) every building enterable | (a): 3 storeys is what the camera / zoom and the squad orders were tuned for; more storeys multiply the validation and the PMC AI search | **(a) 1-3 storeys + roof, ~60 % enterable with interiors, shells only on the edge** (designer 2026-11-18) |
| 3 | Where generation lives | (a) Training Ground TS generates every building element, the Python builder only turns modules into actors; (b) the builder expands building "types" into geometry | (a): one source, the page can preview and validate before «Принять», rotation / regen / locks already live there | **(a) Training Ground TS** (designer 2026-11-18) |
| 4 | Halls | (a) one tall storey (6-9 m) with a mezzanine / catwalk ring reachable by a stair core + roof access; (b) real 2-3 storeys like an office | (a): keeps the industrial look and long sight lines, gives a height level inside | **(a) tall hall + mezzanine / catwalk** (designer 2026-11-18) |

---

## 3. Building grammar

A building is generated in its own local frame (axis-aligned, origin at the footprint centre) and turned by `rotation` at the end
(`rotation.ts`; the builder already turns every actor back, `build_levelgen_map.py` `_walls_and_buildings_rotated`). Every rule and every
check below runs in the local frame.

```
Building   := Shell Storey+ VerticalCore+ RoofAccess? Openings Interior
Shell      := footprint (50 cm grid) , archetype , storeys , roof (flat walkable | flat closed | pitched-closed)
Storey     := slab (except storey 0) , Layout(rooms) , door graph
Layout     := Corridor-spine Rooms   (office, block: depth >= 9 m)
            | BSP Rooms              (shed, store office, depth < 9 m)
            | Hall [+ Mezzanine]     (hall, warehouse: one tall storey)
VerticalCore := StairCore (enclosed dog-leg)  | StraightStair (+ landings) | Ladder (hatch)   | ExternalStair
RoofAccess := core-to-roof (roof door) | hatch + ALadderActor | external ladder | external stair
Openings   := doors (ext / int) , windows per walkable storey , loopholes , roof-edge parapet
Interior   := cover pieces (low / tall) , furniture props , breach / peek clearances
```

Generation order (each step only sees the previous ones; each emits into the existing `codex.levelgen_layout/1` fields, extended where noted):

1. **Archetype + shell** from the lot template (`blockTemplates.ts`): archetype table (section 3.1) gives storeys, storey height, the layout
   kind, door count, roof kind. Footprint snapped to 50 cm.
2. **Vertical core first** (it constrains the rooms): pick the core module and its cell (a short wall, never the door wall, never a
   corner pocket - section 4.2), reserve it on every storey it serves (core cells are not split by BSP).
3. **Storeys**: real slab per storey >= 1 (no `hollow` for enterable multi-storey buildings); the slab gap is exactly the flight
   footprints (+0 overshoot), the arrival landing is solid.
4. **Rooms per storey**: corridor spine (1.8-2.4 m) along the long axis with rooms on one or both sides, or BSP leaves (min 3 x 3 m,
   max 8 x 8 m), or the hall volume. The core opens onto the corridor / hall on every storey.
5. **Door graph**: every room >= 1 door; rooms > 25 m2 or with 3+ neighbours get a second door; at least one loop per storey with >= 3 rooms
   (flanking); exterior doors: >= 2 for buildings > 150 m2, on different sides when possible.
6. **Roof access** (walkable roofs): the core continues to a roof door (blocks, offices), else a hatch + ladder from the top storey, else an
   external ladder / stair. Parapet 95 cm (`low_vault`) with a gap at every arrival.
7. **Openings**: windows on every walkable storey (including the hall's upper band and the mezzanine), rhythm 3-4 m, no window within
   1.2 m of a door / partition / corner; ground-floor windows are vaultable mantle edges (sill 95); upper-storey windows are
   firing positions only (no mantle out, no drop link unless one storey down onto a walkable roof / ground with a landing box).
8. **Interior**: cover pieces and furniture per room (section 4.4), placed last, never in a keep-out (doors, approach boxes, ladders).
9. **Validate** (section 5) - a building that fails is regenerated with the next sub-seed (max 4), then replaced by a closed shell with
   a warning; the layout never carries an unreachable storey.

### 3.1 Archetypes

| Archetype | Storeys | Layout | Core | Roof | Notes |
|---|---|---|---|---|---|
| hall (industrial) | 1 tall (6-9 m) | hall + mezzanine on 1-2 sides at +300 or +600 | stair core to the mezzanine, ladder / core to the roof | walkable, parapet | windows in two bands (ground + mezzanine) |
| warehouse / store | 1 tall | hall + racks (tall cover rows) + a 1-2 storey office box inside | straight stair to the office roof / mezzanine | walkable | racks = `tall_ledge` 230 cm cover lines with 1.5 m aisles |
| office | 2-3 | corridor spine + rooms | enclosed dog-leg core, to the roof | walkable | 2 exterior doors |
| block (residential) | 2-3 | core + flats (2-3 rooms each) per storey | enclosed core, to the roof | walkable | balconies later |
| shed / pump | 1 | BSP 1-3 rooms | - (ladder to the roof if walkable) | closed or walkable | |
| ruin | 1-2 | hand-shaped: partial slabs, floor / roof holes | straight stair / rubble ramp | broken | the only archetype allowed drop-only links |
| gallery / overwatch | solid | - | external ladder | walkable | not a building: an overwatch block |
| facade (shell) | 2-3 | none (closed) | - | closed | only on the map edge / outside the playable frame |

---

## 4. Rules for our mechanics

Numbers are in cm unless stated; the navmesh agent is radius 35, height 144 (`DefaultEngine.ini`), storey `LEVEL_HEIGHT` 300, slab 20.

### 4.1 Clearances (every walkable cell)
- Corridors, doors, stair flights: clear width >= 120 (doors 120 interior, 120-180 exterior, gates <= 600 = `DoorwayBreachConfig::MaxGateWidthCm`);
  stair flights >= 150 (two operatives pass).
- Head room >= 220 over every walkable point (agent 144 + the 2026-11-02 margin), checked under slabs, lintels and flights above.
- No walkable strip narrower than 2 x 35 + 10 = 80 between obstacles (the navmesh drops it).

### 4.2 Stairs
- **Enclosed dog-leg core** (default for 2+ storeys): footprint 270 x 500: two flights of 150 rise (run 215, 35 deg) x 120 wide with a
  120-deep half landing, 150-deep storey landing; walls on three sides, opening onto the corridor on each storey.
- **Straight stair** (sheds, mezzanines, ruins): one 300-rise flight (run 430) with a 150 landing at both ends.
- **Approach box**: a free 150 x 150 in front of every flight foot and after every flight top, joined to a room / corridor >= 200 wide.
  No walls, partitions, props, covers, ladders or doors in it. A flight foot is never in a corner pocket (>= 150 from the end wall).
- Slab gap = the flight + half landing exactly; the storey landing is solid slab. Head room over the flight checked (4.1).
- Slope <= 35 deg (the CMC walkable floor angle is 44.76, the navmesh 44: margin for the 20 cm thick ramp's edges).
- Collision stays a ramp (smooth for the mover / navmesh); visual steps come with the kit.

### 4.3 Ladders, hatches, drops
- `ALadderActor` with an integer rung count at 36.4 cm pitch (`docs/design/LADDERS.md`): a storey = 8 rungs + 8.8 cm plinth (as now);
  ladders longer than one storey only outside on a facade (no 24-rung ladders inside halls). Approach box 150 x 150 at the foot and the top.
- Hatch 100 x 100 with a 3-sided rim (builder `_hatch_rim_specs`), ladder from the storey below.
- Drop links (`Scripts/Editor/levelgen_bot_route.py` `drop_links`) only for: roof / floor holes and low roof edges of <= 1 storey onto a
  landing box. **A storey or the roof must be reachable without a drop** - drops are shortcuts, never the only way.

### 4.4 Cover, peek, mantle, breach
- Cover pieces from `obstacle_heights.json`: low cover = `low_vault` 95 (peek over, vaultable), tall cover = `tall_ledge` 230 (peek around).
  The cover system finds slots from geometry (`FCoverTraceConfig::SlotOffsetCm` 45), so pieces are real collision boxes.
- Density: 1 piece per ~12 m2 of room, >= 1 per room > 9 m2; >= 120 free walk lane through every room between its doors.
- Peek corners: an L corner next to a door gets a tall piece or a partition stub (60-90) so a corner-peek exists.
- Mantle edges: windows (sill 95), parapets (95), low cover (95). Obstacle heights only from the classes (step <= 35, low_vault 25-105,
  low_mantle 75-130, high_mantle 130-240, wall_no_traverse >= 320); never between classes.
- **Door breach** (`DoorwayBreachRules.h`): on both sides of every door a breach zone: jamb slots 30 from the opening edge, 45 off the wall;
  centre slot 220 and rear 350 back along the normal. Rule: no cover / prop / partition inside the jamb slots; where a corridor is shorter
  than 350 the breach falls back to the stack (`FallbackStackSpacingCm` 60) - allowed, but each room needs >= 1 door with a full triad side.
- Openings never within 60 of a jamb of another opening (jamb slots must exist).

### 4.5 Rotation
- All rules in the local frame; the builder turns the actors. The validator also checks the turned footprint against neighbours / fences
  (task 085 validator) and that the navmesh probe points (section 5) are turned the same way.

---

## 5. Traversal gate (before «Принять»)

Two layers; both must pass for every enterable building, the result is shown per building on the page and stored in the layout
(`analysis.buildings[]`).

1. **Training Ground validator** (TS, instant, `src/levelgen/buildingTraversal.ts`): builds the walk graph of the building from the layout
   alone: storey cells (slab minus gaps), rooms, door graph, flights / ladders / hatches as edges, approach boxes. Checks: every room,
   every storey and the walkable roof reachable from an exterior door **without drops**; 4.1 clearances; 4.2 approach boxes free and the
   slab gap = flight footprint (catches cause A); 4.3 ladder rung counts; 4.4 breach jamb slots free. «Принять» is disabled while any
   enterable building fails (the designer may still force it with an explicit "accept with N broken buildings" warning).
2. **Bot smoke on the built map** (`CodexTactics.LevelGenBuildingsSmoke`, C++ dev command, headless): per building, from outside each
   exterior door: a navmesh path to every room centre, every storey landing and the roof (`complete`, not partial); then the squad legs of
   `LevelGenRampSmoke` (squad / group / solo) on every flight and ladder of the building, the squad bunched in the mouth. Writes
   `Saved/LevelGen/buildings_smoke_<map>.json` (per building PASS / FAIL with the first unreachable target), which `levelgen_build_map.ps1`
   runs after a build and the page shows. A map is not published to the designer while it FAILs.

---

## 6. Phases and owners

| Phase | Content | Owner | Done when |
|---|---|---|---|
| **P0 - measure and stop the bleeding** (first phase) | a) TS validator `buildingTraversal.ts` + the per-building panel + «Принять» gate; fix (A) the gap overshoot and (B) the flight placement / approach boxes in the current `addBuilding`; b) C++ `LevelGenBuildingsSmoke` + hook in `levelgen_build_map.ps1`; c) the audit script (done, this commit) | a) Antigravity (task 086); b) API agent (`claude_api_agent.ps1`, Sonnet, Opus review); c) Claude | X6 regenerated in place: every block's floor 1 and roof reachable, validator 0 errors on new layouts, ramp smoke >= 90 % legs on X6 |
| P1 - grammar v1 | `src/levelgen/buildings/` module: archetype table, shell, core-first placement, storeys, corridor / BSP rooms, door graph with loops, roof access, openings per storey; replaces the internals of `addBuilding` for hall / warehouse / office / block / shed; facades move to the edge | Antigravity (plan -> OK -> code), Claude review | new X6-sized map: 100 % validator PASS, buildings smoke PASS |
| P2 - builder modules | stair core (flights, landings, enclosure, rails), mezzanine / catwalk, roof door, kit-ready module names on actors | subagent / API agent (Python `build_levelgen_map.py`) | builder unit tests + smokes PASS on the P1 map |
| P3 - interiors | cover pieces / furniture per room (4.4), breach / peek clearances, props layer `interior` | Antigravity (TS) + Grok (prop rules) | each room >= 1 cover, CoverSmoke / DoorBreachSmoke on a generated building |
| P4 - dressing | modular kit swap by module name (if fork 1 = a), pitched roofs, balconies | after the designer picks a kit | - |

Process: every task via `docs/port/gemini/inbox/0NN-*.md` (Antigravity) or an API prompt; API Opus review (`Scripts/Tools/api_task.py`)
before each merge; Claude merges into main after build / tests (`Scripts/build.ps1` never while the designer's editor is open).

---

## 7. Kit -> generator module contract (2026-11-18, building-kit session)

> **Added 2026-11-18 by the building-kit side session, at the lead's request.** Sections 1-6 above are unchanged and still
> on hold for the studio discussion. This section only fixes the module sizes the generator emits and the art kit must
> match, so that P4 "kit swap by module name" works without touching the generator. Evidence lives in the scratch UE project
> `Documents/Unreal Projects/BuildingKitLab` (not part of this repo): `Saved/mesh_bounds.csv` (1 018 meshes),
> `Saved/kit_analysis_powerplant.json`, `Saved/Contract300/_report.json`, scripts in `BuildingKitLab/Scripts/`.

### 7.1 Storey height 300 cm (designer decision 2026-11-18)

Decision: **every walkable building uses a 300 cm storey** (floor-to-floor, slab 20 included). Condition set by the designer:
it must not break climbing / mantling. Checked:

| Check | Source | Result |
|---|---|---|
| Current generator / builder value | `Tools/TrainingGround/src/levelgen/schema.ts` `LEVEL_HEIGHT`, `Scripts/Editor/build_levelgen_map.py` `LEVEL_HEIGHT = 300.0` | already 300 - the contract formalises it, no rule changes |
| Climb onto a 1-storey roof | `Content/Data/LevelGen/obstacle_heights.json` `very_tall` 300-320 (JumpClimb clips 308), MANTLING.md 2026-11-15: 300 stays valid inside very_tall, `wall_no_traverse` moved to 330 | a 300 roof edge is climbable **only where the generator leaves a parapet gap** (95 parapet -> 395 edge = not climbable) |
| Upper storeys from the ground | sill of storey 1 = 300 + 95 = 395 > 320 traversal ceiling; a 2-storey roof = 600 | not climbable - correct, they need stairs / ladders |
| Window sills, parapets, low cover | `low_vault` 95 +-5 | 95 cm everywhere (vault over / out) |
| Coming down | MANTLING.md controlled drop 60-250 cm | no storey or roof is a controlled drop (300 > 250); down only by stair / ladder or a <= 250 intermediate step (container 230, low roof) - matches 4.3 "drops are shortcuts, never the only way" |
| Ladders | `docs/design/LADDERS.md`: 36.4 cm rungs | 8 rungs x 36.4 = 291.2 + 8.8 plinth = 300 (as today); a 400 hall wall = 11 rungs (400.4) |
| Very-tall climb animation in C++ | MANTLING.md section 11 | not implemented yet; the contract prepares the geometry, it does not depend on it |

Industrial kit walls of 400 / 800 cm are allowed **only for single-volume halls** (archetype `hall` / `warehouse`, section 3.1):
nothing inside them is traversed by height except by stair / ladder, and their roof edge is never marked climbable.
A mezzanine sits at +300 so it lines up with every other storey.

### 7.2 Module sizes (kit-ready names; first kit: Power Plant converted to the contract)

Grid 50 cm in the generator, module widths multiples of 100. Pivot of every wall module: **bottom corner, wall along +X from
x = 0 to its width, thickness on -Y (y = -T..0)**, z = 0 at the finished floor of the storey. All values in cm.

| Module name (generator) | Kit piece (BuildingKitLab `/Game/Kit300/Meshes`) | Size W x T x H | Opening (x0-x1, z0-z1) | Collision |
|---|---|---|---|---|
| `wall_100/200/300` | `SM_W300_Wall_Plain_100/200/300` | 100/200/300 x 20 x 300 | - | 1 box |
| `wall_outward_100/200/300` (plinth band at 95) | `SM_W300_Wall_Plain_Outward_*` | x 25 x 300 | - | 1 box |
| `wall_door_200` | `SM_W300_Wall_Door_200` | 200 x 20 x 300 | 25-175, 0-240 (door 150 x 240) | 3 boxes (sides + lintel), opening free |
| `wall_door_300` | `SM_W300_Wall_Door_300_01` | 300 x 20 x 300 | 75-225, 0-240 | 3 boxes |
| `wall_window_s_200/300` | `SM_W300_Wall_Window_Small_03/04` | 200/300 x 20 x 300 | 150 wide centred, sill 95, head 245 | 4 boxes |
| `wall_window_m_200/300` | `SM_W300_Wall_Window_Medium_02/01` | 200/300 x 20 x 300 | 150 wide centred, sill 95, head 265 | 4 boxes |
| `wall_window_band_*` (ribbon, full width) | `SM_W300_Wall_Window_Small_01/02`, `Medium_03/04` | 200/300 x 25 x 300 | full width, 95-245 / 95-265 | 2 boxes (below / above) |
| `window_insert_s / _m` (visual only) | `SM_W300_Window_Small_0x` (150 x 150), `SM_W300_Window_Medium_0x` (150 x 170) | | set into the opening at z 95 | **none** - a window stays a vaultable opening for gameplay |
| `stair_flight_150` (visual) | `SM_W300_Stair_Flight_150` | 120 wide, run 215, rise 150 (35 deg) | - | none; collision stays the builder's ramp (4.2) |

Rules the generator keeps (from sections 4.1-4.4, restated as kit constraints): door clear height 240 (>= 220 head room),
window sills exactly 95 (`low_vault`), parapets 95 with a gap at every climb / arrival point, no edge between classes
(`obstacle_heights.json`), every opening >= 60 from another opening's jamb.

### 7.3 Fab kits measured 2026-11-18

| Kit | Grid / storey as bought | Status for the generator |
|---|---|---|
| Modular Old Power Plant (Nimikko) | 100 grid, 400 walls, 20-25 thick | **passed** - converted: 23 modules to storey 300 (table 7.2); Tall / Wide windows, Structure walls, large gate stay 400 = hall-only |
| Industrial Infrastructure (Sierra Division) | 200 grid (100/50), 400 panels | candidate, same conversion (next) |
| Post-Apocalyptic Factory Overgrown (Indus North) | 400 grid, 300/400 walls, 25 centred | candidate (walls already 300 in part) |
| Factory (Dave Berg, 5.7 build) | 600 grid, 400 + 100 cap, 50 thick, sill 127 | hall walls only; sills must be refitted to 95 |
| Metro Maintenance Station (Nimikko, 5.6 build) | tunnel sections 250 x 400-500 | special set piece, not a building kit |
| Post-Apocalyptic Urban City (Akairo) | not modular (454 / 600 / 1053), storey ~420 | props and set dressing only |
| Modern City Bundle (Pedja Racan) | whole buildings 25-40 m, storey 350 | backdrop only (map edge) |
| Military Training Facility (Dekogon) | project template ("Create Project" only) | not added |
| Warehouse, Unfinished Building (Quixel Megascans) | photogrammetry, unique textures | **dropped** (designer 2026-11-18: no Megascans for now); Warehouse deleted from BuildingKitLab to free 6.8 GB |
| Soviet modular residential kit | - | **to come from the designer** - needed for `block` / residential archetypes |

### 7.4 Art pipeline findings (TextureCraft / Blender, headless)

Pipeline proven on one wall and then the Power Plant batch: UE commandlet FBX export -> Blender 5.2 `-b` (LOD0 only,
piecewise height remap, one material slot, UVs, pivot, UCX boxes) -> TextureCraft 0.5.0 CLI (bake + smart material) or a
shared tiling material -> UE commandlet import.

- **The UE FBX export carries LOD0-LOD3 + the UCX collision.** TextureCraft loads the whole file as one mesh: the first test
  wall got a second, empty "Default" material slot and auto-generated (broken) UVs on part of the mesh. Fix: keep LOD0 only
  before texturing (the Blender step does it).
- **Concrete detail scale**: TextureCraft's smart `concrete_dirty` on a unique 2K layout gives pores / streaks that read too
  large on a 3 x 3 m wall at our camera; a texel density for the top-down camera (proposal 256-512 px/m) must be set per
  material, and modular walls should use shared tiling / trim materials, not a unique texture per piece.
- Unique-UV texturing (TextureCraft) is for props and hero pieces; walls / slabs use a world-scale box UV (1 UV unit = 1 m)
  and a shared material (`M_Kit300_Tiling`, base maps from Substance Painter starter `.sbsar` rendered with Designer's
  `sbsrender`).
- MaterialCraft 0.4.1 (CLI + MCP) builds trim / tiling sheets headless; only about half of the Substance Designer library
  cooks in it today, so graphs are built from atomic nodes.

### 7.5 Open questions for the designer

1. Window types per archetype: is the Medium window (95-265, 170 tall) needed besides Small (95-245), or one size for all?
2. Ribbon (full-width) windows: allowed on offices / halls only, or also on residential blocks?
3. Door width: 150 clear (kit) vs 120 interior / 120-180 exterior (section 4.1) - accept 150 as the exterior door module?
4. Slab thickness and floor pieces: the kit floor is a 0-thickness plane and the ceiling 42 thick; keep the builder's 20 cm
   slab as collision and use kit pieces as visuals only?
5. Residential storey: if the Soviet kit is 280 cm, scale it to 300 (7 %) rather than add a second storey height?
6. Texel density target for walls at the tactical camera (256 vs 512 px/m) - decide on the first rendered corner.
