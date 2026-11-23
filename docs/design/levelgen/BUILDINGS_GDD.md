# Buildings — Game Design Document + Technical Specification (levelgen v3)

> **SUPERSEDED 2026-11-18.** Split into the studio-standard GDD set [gdd/systems-index.md](gdd/systems-index.md)
> (building-archetypes, opening-states, building-route-model, breach-panels, building-stories) and
> [BUILDINGS_TECH_SPEC.md](BUILDINGS_TECH_SPEC.md), then reworked (v2) after the design review and the studio review.
> Kept unchanged below as the original draft; edit the new documents, not this one.

Owner: Claude (building generator, designer 2026-11-18: «Бери генератор на себя»). Status: **DRAFT for the studio discussion**
(designer 2026-11-18: «обсудим на уровне студии, создадим нормальный гейм-дизайн документ… нормальное техническое задание»).
Nothing in parts B-D beyond what is marked *done* is implemented; numbers marked *proposal* wait for the discussion.

Related: [BUILDING_GENERATOR.md](BUILDING_GENERATOR.md) (audit, grammar, rules, phases P0-P4, section 7 = the kit -> generator
module contract), [LEVEL_GENERATOR.md](LEVEL_GENERATOR.md), [BUILDINGS.md](BUILDINGS.md), `docs/design/MANTLING.md`,
`docs/design/LADDERS.md`, `Content/Data/LevelGen/obstacle_heights.json`, `Source/CodexTactics/Public/.../DoorwayBreachRules.h`,
`Tools/TrainingGround/src/levelgen/` (`generate.ts`, `buildingTraversal.ts`).

Contents: **A. Design** (1 vision, 2 archetypes, 3 circulation, 4 openings, 5 breach panels, 6 building stories) -
**B. Systems** (7 route model, 8 generation pipeline) - **C. Technical spec** (9 data contract, 10 builder + kit, 11 runtime /
C++, 12 Training Ground, 13 tests + gates) - **D. Plan** (14 phases, 15 open questions).

---

# A. Design

## 1. Vision

**Every building is a small story.** A building is not a box with a door and a stair: it has an obvious way in that may be
closed, a hidden one, a reason why the stair does not go all the way up, and a second way (a balcony, a fire escape, a breach)
that the squad has to find. Designer, 2026-11-18:

> «Я хочу, чтобы каждое здание было небольшой историей… чтобы маршруты были нелинейными, чтобы не только зашел, поднялся
> на лестнице, и вот ты уже наверху.»

Pillars:

1. **Non-linear, never impossible.** A storey may need a detour (outside, a balcony, a neighbour's flat, a breach), but every
   walkable storey and roof stays reachable **without explosives** - charges can run out. The validator enforces it.
2. **Readable.** What can be opened, kicked, broken, climbed or blown is visible: a door with a lock plate, a cracked wall
   panel, boarded windows, a fire-escape ladder. No invisible rules.
3. **Costs, not walls.** A route is chosen by its price (time, noise, explosives, exposure), not only by whether it exists.
   The same building is entered quietly through a broken back window or fast and loud by kicking the front door.
4. **The setting tells it.** Soviet industrial towns in the Arctic: panel blocks, hospitals, depots, halls; snow drifts,
   frost, collapse, barricades left by survivors and PMC defenders.
5. **Gameplay first, then the kit.** All rules live in the generator and the validator on a greybox; the art kit swaps in by
   module name (BUILDING_GENERATOR.md section 7) without changing a rule.

Share of buildings (*proposal*, designer 2026-11-18 accepted as the working assumption): ~30 % open / pass-through,
~70 % with one or two stories; closed shells only on the map edge; 1-3 storeys + roof.

## 2. Archetypes

| Archetype | Storeys | Layout | Typical stories |
|---|---|---|---|
| **hall** (industrial) *done* | 1 tall (9 m) + mezzanine at +300 | hall volume, mezzanine along the core wall | locked gate, roof only outside, breach in sheet wall |
| **warehouse** *done* | as hall | hall + racks + office box | shutter closed, office on the mezzanine |
| **office** | 1-2 (*proposal*: 2 when >= 8 m long) | corridor spine + rooms | locked wing, dead-end ambush room |
| **block** (residential, panel) | 2-3 | stair core + flats (2-3 rooms each) per storey, balconies | rubble on the stair -> balcony + fire escape, snow drift entrance, courtyard-only entrance |
| **hospital / school** (*new*) | 2-3 | long block, central corridor, two wings, a stair core per wing | split wings, barricaded corridor, wing B upstairs only by its fire escape |
| **shop + flats** (*new*) | 2-3 | ground floor shop (shutters to the street) not connected to the flats | flats only from the courtyard entrance |
| **shed / pump / garage** | 1 | 1-3 rooms | locked door, barred windows, roof by a ladder |
| **ruin** | 1-2 | hand-shaped: partial slabs, holes | collapsed facade ramp, burnt-out flight |
| **gallery / overwatch** | solid | - | ladder onto the roof (validator: a ladder must exist) |
| **facade shell** | 2-3 | closed | map edge only |

## 3. Vertical circulation

- **Switchback stair core** *done* (P0): one 300-rise flight per storey (35 deg), two lanes along a long wall, a free 150 cm
  approach box at every foot and after every top, the slab gap exactly the flight. Halls climb to the roof by it.
- **External fire escape** (*new*): a steel stair or a ladder on a facade, landing on **balconies** (a 120-150 cm deep slab
  outside a flat's balcony door, 95 cm rail = low_vault). Storey-by-storey; the top one may reach the roof.
- **Balcony** (*new*): slab + rail + a balcony door into a flat; a fire escape or a neighbour's balcony (gap <= 90 cm = a
  step-over) connects it. A balcony without a fire escape is a firing position only.
- **Ladders / hatches** *exist*: `ALadderActor`, 36.4 cm rungs (a storey = 8 rungs + 8.8 cm plinth), hatch 100 x 100.
- **Elevator shaft** (*new*, later): doors forced open, rungs inside the shaft = a ladder to a storey the stair does not serve.
- **Mezzanine edge** - *parked* (designer 2026-11-18: «подожди с антресолью»): no rail yet; 95 cm rail = vaultable into a
  3 m drop vs a tall rail (no vault, blocks sight) - decided in the discussion.
- Mantle / drop rules unchanged: low_vault 95, very_tall 300-320 only at a parapet gap (1-storey roofs), controlled drop
  <= 250 cm (MANTLING.md); a storey is never reachable by a drop only.

## 4. Openings and their states

Every opening (door, window, gate, loophole, breach panel) carries a **state**. The state decides passability, the action and
its **cost** in the route model (section 7), and what the builder spawns. Designer 2026-11-18: doors that are locked and can
be kicked; windows with bars; windows one jumps through that shatter; already broken windows.

### 4.1 Doors

| State | Passable by | Action | Cost (*proposal*) | Visual |
|---|---|---|---|---|
| `open` | everyone | - | 0 | open leaf / empty frame |
| `closed` | everyone | open | 1 s, quiet | closed leaf |
| `locked` | kick (breach) | kick | 3 s, loud (6 m noise) | lock plate, dented leaf |
| `barricaded` | explosive only (later: clear by hand) | charge / clear | charge, very loud | furniture / planks in the frame |
| `sealed` | nobody | - | blocked | welded / bricked up / rubble |

### 4.2 Windows (sill 95 = low_vault)

| State | Passable by | Action | Cost | Notes |
|---|---|---|---|---|
| `broken` | vault | vault | quiet | the quiet entry |
| `glazed` | vault, glass breaks | vault + shatter | loud (glass), later: cuts | becomes `broken` after the first pass |
| `barred` | nobody | - | blocked | shoot / see through |
| `boarded` | later: tear off boards | (later) | blocked for now | no sight |
| `fixed` | nobody | - | blocked | too high / too narrow (shop windows, loopholes) |

Rules: a ground-floor window state is a design lever (barred fronts + broken backs = "the quiet way is round the back");
upper-storey windows are firing positions and never an entry unless a balcony / roof / snow ramp stands below them.

## 5. Breach panels (destructible walls)

Designer 2026-11-18: walls blown by mines, charges or RPGs - a special wall, a crack, it can be punched through.

- **Marked panels, not any wall** (*proposal*): a breach panel is its own kit module (100 or 200 cm wide, full storey height)
  with a visible tell - cracks, broken plaster, a bricked-up former opening, rusty sheet in a hall. Intact and breached states;
  the breached hole ~120 x 220 cm with rubble at its foot (*proposal*). Two adjacent panels = a wide breach (big charge).
- **Damage** (*proposal*): an RPG round opens one panel, a charge / mine one panel, a big charge two; other weapons do nothing.
- **Placement** (generator): only where the breach is a meaningful shortcut - into the neighbouring flat, around a blocked
  stair, between hospital wings, into a building from its door-less side. Never in a stair core, a slab, a corner, or next to
  another panel's jamb; the breach triad space (DoorwayBreachRules) free on both sides; one rubble box at its foot kept free.
- **Validation**: the graph is checked twice - without the breach (panel = wall) and with it (panel = opening, cost
  "explosive"). A storey must be reachable in the first graph; the breach may only shorten the route.
- Open question: are some whole materials breachable anywhere (hall sheet walls, wooden partitions)?

## 6. Building stories

### 6.1 Model: bricks + recipes

A story is a **recipe of 1-3 bricks** with conditions (archetype, storeys, a neighbour within X m, a courtyard). Bricks:

| Brick | Effect on the building |
|---|---|
| `door_state(door, state)` | sets a door's state (locked / barricaded / sealed) |
| `window_state(side|storey, state)` | sets window states on a facade / storey |
| `block_flight(flight, kind)` | the flight stays (geometry) but is impassable: rubble / burnt / barricade |
| `add_balcony_escape(flat, storeys)` | a balcony door + balcony on each storey + a fire escape between them |
| `split(wall)` | a full partition without a door divides the building into wings (each wing gets an entrance and a core / escape) |
| `opening(floor|wall, kind)` | a floor hole (drop down only), a facade collapse (rubble ramp), a breach panel |
| `link_neighbour(kind)` | a hole in a shared wall, a plank between windows, a roof bridge |

After every recipe the validator runs; a recipe that breaks reachability (section 7) is rolled back and the next candidate
is tried (max 4), then the building keeps the "open" story.

### 6.2 Catalog (22 stories)

| # | Story | Recipe (bricks) | Archetypes | Key route (example) |
|---|---|---|---|---|
| 1 | Open | - | all | door -> stair -> up |
| 2 | Pass-through | 2 doors on opposite sides | all >= 150 m2 | street A -> through -> street B |
| 3 | Locked door | door_state(front, locked) | all | kick the front, or the back door |
| 4 | Rubble on the stair + fire escape | block_flight(0->1, rubble) + add_balcony_escape | block, office | flat 1 -> balcony -> fire escape -> balcony 2 -> up |
| 5 | Split wings ("hospital") | split(corridor) + per-wing entrance; wing B upstairs only by its escape | hospital/school, office | out, round, into wing B, up its escape |
| 6 | Collapsed floor | opening(floor, hole) | block, ruin | down by the hole, up by the far stair |
| 7 | Roof only from outside | core stops below the roof, no hatch; facade ladder / escape to the roof | all with a walkable roof | out -> ladder -> roof |
| 8 | Barred ground floor | window_state(storey 0, barred) | all | doors only |
| 9 | Broken back windows | window_state(back, broken) + front glazed | all | quiet entry from the back |
| 10 | Snow-drift entrance | door_state(front, sealed: drift) + snow ramp to a storey-1 window | block, office | up the drift, in by the window |
| 11 | Iced fire escape | the escape is down-only (no up edge) | block | escape is an exit, not an entry |
| 12 | Facade collapse | opening(wall, collapse) + rubble ramp to storey 1 | ruin, block | up the rubble into the cut flat |
| 13 | Burnt-out flight | block_flight(1->2, burnt) + ladder / hatch | block, office | ladder through the hatch |
| 14 | Hole into the neighbour | link_neighbour(shared wall hole) | blocks in a row | into the next stairwell, up there |
| 15 | Defenders' barricade | door_state / block_flight(barricade) | all | (later: clear / blow) else the long way |
| 16 | Sniper's nest | a top room reached only by a hatch ladder | block, hall office | dead end with a view |
| 17 | Dead-end trap | a flat with one door at the end of a long corridor | block, office, hospital | ambush room (PMC AI) |
| 18 | Plank bridge | link_neighbour(plank window -> window) | blocks <= 3 m apart | across the plank |
| 19 | Shop below | ground-floor shop (shutters fixed) not connected to the flats | shop + flats | flats from the courtyard |
| 20 | Courtyard only | street-side doors sealed / windows barred | block, hospital | round the block into the yard |
| 21 | Elevator shaft | forced doors + rungs = ladder | block, office (3 storeys) | shaft to the storey the stair skips |
| 22 | Basement passage | basement level -1 + tunnel to the neighbour | later stage (needs level -1) | - |

Frequency and per-archetype weights live in a JSON table (`Content/Data/LevelGen/building_stories.json`, *proposal*) so the
designer tunes them without code. A building may carry two compatible stories (e.g. 3 + 9).

---

# B. Systems

## 7. Route model

- **Graph** per building (and its outside): nodes = rooms / landings / balconies / roof / the street; edges = doors, windows,
  flights, ladders, hatches, escapes, links, breach panels. Every edge has a **mode** (walk, open, kick, vault, shatter,
  climb, explosive) and a cost (time, noise, exposure; table in JSON, *proposal*).
- **Reachability**: every walkable storey / roof reachable from the street **without explosives and without drops**
  (P0 validator, `buildingTraversal.ts` *done*, extended with modes). Down-only edges (holes, iced escapes) count only
  downwards.
- **Key route** per building: the cheapest street -> top route (rooms, actions, outside legs). Stored in the layout,
  shown on the page, walked by the bot smoke.
- **Non-linearity** (*proposal*): detour ratio = key-route length / straight in-and-up length; a storied building should land
  in 1.5-4 x; outside legs and actions counted. The share of buildings per band is reported per map.
- Rollback: a story whose recipe fails reachability is undone; the generator tries the next story (deterministic sub-seed).

## 8. Generation pipeline (per building, in its local frame)

1. Archetype + shell (footprint on 50 cm, storeys) - *done* for existing archetypes.
2. Vertical core - *done* (switchback core; halls' mezzanine).
3. Rooms per storey (corridor spine / BSP / flats) - partly done (BSP); corridor + flats *to do*.
4. Doors + windows (two exterior doors > 150 m2 *done*), balconies.
5. **Story**: pick from the weighted catalog, apply bricks, validate, roll back on failure.
6. Opening states (the story's + defaults by archetype / facade).
7. Breach panels (where the story wants a shortcut).
8. Interior content (cover, furniture) - P3.
9. Validate (section 7); per-building regen with the next sub-seed; the result per building on the page.

Deterministic per (seed, building id); per-building re-roll and lock on the page (regen/locks already exist for regions).

---

# C. Technical specification

## 9. Data contract (`codex.levelgen_layout/1` extensions, all optional = old layouts stay valid)

```ts
interface WallOpening { /* existing */ state?: DoorState | WindowState }            // 4.1 / 4.2; absent = 'open' (doors) / 'broken' (windows)
type DoorState = 'open' | 'closed' | 'locked' | 'barricaded' | 'sealed';
type WindowState = 'broken' | 'glazed' | 'barred' | 'boarded' | 'fixed';
interface Stair { /* existing */ blocked?: 'rubble' | 'burnt' | 'barricade'; down_only?: boolean }
interface Balcony { id: string; floor: number; rect: [Vec2, Vec2]; door_id: string; rail_cm: number }   // 3
interface FireEscape { id: string; kind: 'stair' | 'ladder'; side: Side; stops: { floor: number; balcony_id: string }[]; to_roof: boolean; down_only?: boolean }
interface BreachPanel { id: string; wall: string; floor: number; at: Vec2; width_cm: 100 | 200; hole: [number, number]; links: [string, string] }  // 5
interface BuildingLink { id: string; kind: 'shared_wall_hole' | 'plank' | 'roof_bridge'; a: string; b: string; at: Vec2[] }
interface GenBuilding { /* existing */ balconies?: Balcony[]; fire_escapes?: FireEscape[]; breach_panels?: BreachPanel[];
  story?: { id: string; bricks: string[]; seed: number }; key_route?: { legs: { from: string; to: string; mode: string }[]; cost: number; detour: number } }
interface GenLayout { /* existing */ building_links?: BuildingLink[] }
```

Validator codes added: `unreachable_without_explosive`, `story_rolled_back`, `blocked_flight_no_bypass`, `balcony_orphan`.

## 10. Builder + kit (`Scripts/Editor/build_levelgen_map.py`, kit section 7)

- Door states: `open` - frame only; `closed` / `locked` - a door leaf actor with tags `door_state=closed|locked` (the runtime
  makes it openable / kickable); `barricaded` - leaf + barricade props, tag `door_state=barricaded`; `sealed` - wall fill.
- Window states: tags `window_state=*` on the opening; `glazed` - a glass pane actor (breakable at runtime), `barred` - bars
  mesh with block-collision but no visibility block, `boarded` - boards mesh (visibility + collision), `broken` - empty frame.
- Blocked flight: the ramp stays (navmesh-excluded via a nav modifier volume / a blocking box over it) + rubble props.
- Balcony: slab + rail (95, `CodexMantleEdge`) + balcony door opening; fire escape: steel stair ramp or `ALadderActor`.
- Breach panel: module `wall_breach_100/200` (intact, tag `breach_panel=<id>`) + its breached variant spawned hidden.
- Kit module names extend section 7.2: `door_leaf`, `door_lock_plate`, `window_glass`, `window_bars`, `window_boards`,
  `balcony_slab`, `balcony_rail`, `fire_escape_flight`, `fire_escape_landing`, `rubble_flight`, `barricade_door`,
  `wall_breach_100/200` (+ `_broken`).
- Legacy layouts (no states) build bit-identically (existing test).

## 11. Runtime (C++ / AnimBP) - separate tasks, generator does not depend on them

| Feature | Need | Notes |
|---|---|---|
| Open / close door | door leaf component, open action | exists? (`DoorwayBreachRules` covers the squad breach positions) |
| Kick door | kick animation + noise event + leaf swap | animation clip needed (pack?) |
| Glass shatter | breakable pane (mesh swap or Chaos), noise, vault through | later: cuts |
| Tear off boards | later | |
| Breach panel | damage from RPG / charge / mine -> swap intact -> breached + rubble + dust; local navmesh rebuild (dynamic navmesh, tile-local); cover / LOS refresh | navmesh runtime generation must be enabled - cost to measure |
| Blocked flight | nav-exclusion + rubble collision | - |
| Fire escape / balcony | ladders exist; balcony = slab + mantle rail | - |
| PMC AI | edge costs (time / noise / explosive), defenders cover breach panels and locked doors | ties to `docs/design/PMC_DEFENCE.md` |
| Bot smoke | `LevelGenBuildingsSmoke` (P0b, queued for an API agent) walks every building's key route | - |

Until a runtime feature exists the states degrade safely: `locked` behaves as `closed`, `glazed` as `broken`, `barricaded` /
`boarded` / `sealed` / `barred` as blocked, breach panels as walls. The data does not change when the feature lands.

## 12. Training Ground page

- «Здания» panel *done* (traversal per building, «Принять» gate). Adds: story name, key route as a readable chain
  ("окно Б (разбить) -> кв. 2 -> балкон -> пожарная лестница -> 2 этаж"), cost and detour ratio, a «другая история» button
  per building (re-roll with the next sub-seed) and a lock.
- Plan view overlay: door / window state glyphs, blocked flights, balconies, fire escapes, breach panels, the key route line.

## 13. Tests + gates

- TS unit: every brick and story on a fixture building (state applied, rollback on failure, determinism per seed).
- Validator: reachability without explosives on every generated building (x1 / x6, several seeds) = 0 failures; the
  pre-fix X6 fixture keeps failing (audit reproduction test *done*).
- Distribution: story shares per map within the JSON targets +- 10 %.
- Builder unit: state -> actors / tags; legacy bit-identical.
- In-engine: `LevelGenBuildingsSmoke` per building key route; `LevelGenRampSmoke` on every flight; `DoorBreachSmoke` on a
  locked door once the kick exists.
- Gate before «Принять»: validator 0 failures (or the forced checkbox, recorded as `analysis.forced_buildings`).

---

# D. Plan

## 14. Phases (*proposal*, each = a branch -> lead review -> merge)

| Phase | Content | State |
|---|---|---|
| P0 | traversal validator, stair core, slab gaps, panel + gate | **done** (claude/building-generator, review fixes 373335af) |
| P1a | hall mezzanine, cores everywhere, 2 exterior doors | **done** (70a56a7b, 448dca32) |
| S1 | data contract (section 9) + opening states + 5 stories (3 locked door, 4 rubble + balcony escape, 5 split wings, 7 roof outside, 10 snow drift) + key route + page | next |
| S2 | rooms: corridor spine, flats, hospital / school archetype, shop + flats | |
| S3 | stories 6, 8, 9, 11-14, 16-20 + weights JSON | |
| S4 | breach panels (generator + validator) | after the studio decisions on section 5 |
| R1..R4 | runtime: doors (open / kick), glass, breach panels + dynamic navmesh, PMC AI costs | C++ tasks, owners decided by the lead |
| K | kit modules for states / balconies / escapes / breach panels (BuildingKitLab pipeline) | art track |
| L | stories 15, 21, 22 (barricade clearing, elevator shaft, basement level -1) | later |

## 15. Open questions for the studio discussion (with the current recommendation)

1. Story shares: ~30 % open / pass-through, ~70 % with 1-2 stories? Per-archetype weights? (*rec*: yes, JSON-tunable)
2. Locked doors: kick only, or also lock-picking / keys? Kick time and noise radius? (*rec*: kick only, 3 s, 6 m)
3. Glazed windows: does the vault cut / slow? Noise radius? (*rec*: noise only first)
4. Breach panels: only marked panels, or whole materials (hall sheet, wooden partitions)? (*rec*: marked only)
5. Breach size per weapon: one size (120 x 220) or per weapon? (*rec*: one size; big charge = two panels)
6. Dynamic navmesh for breaches: acceptable cost? (*rec*: tile-local rebuild, measured on X6 before S4)
7. New archetypes: hospital / school and shop + flats - approved? Sizes? (*rec*: yes, 2-3 storeys, 30-60 m long)
8. Balcony rails 95 (vaultable = a drop of one storey onto the escape / ground) - accepted?
9. Mezzanine / hall landing edge (parked): 95 vaultable rail vs tall rail.
10. PMC defenders: may they create stories at runtime (barricade a door, seal a stair)? (*rec*: later, data model allows it)
11. Basement level -1 (story 22): in scope for this milestone? (*rec*: no)
12. Who owns the runtime tasks R1-R4 (C++ gameplay = Claude by the role table; animations = designer's packs)?
