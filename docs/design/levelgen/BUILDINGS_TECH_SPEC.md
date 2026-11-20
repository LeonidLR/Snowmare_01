# Buildings - Technical Specification (levelgen v3)

Owner: Claude (building generator). Status: **DRAFT v2 for the studio discussion** (reworked after `/design-review` and the
4-role studio review, 2026-11-18). Nothing beyond what is marked *done* is implemented; numbers marked *proposal* wait for the
discussion.

Design (studio GDD standard, 8 sections each): [gdd/systems-index.md](gdd/systems-index.md) -
[building-archetypes](gdd/building-archetypes.md), [opening-states](gdd/opening-states.md),
[building-route-model](gdd/building-route-model.md), [breach-panels](gdd/breach-panels.md),
[building-stories](gdd/building-stories.md). Source: BUILDINGS_GDD.md sections 8-15 (commit 9210a435).

Related: [BUILDING_GENERATOR.md](BUILDING_GENERATOR.md) (audit, grammar, rules, phases P0-P4, section 7 = kit -> generator
module contract), [LEVEL_GENERATOR.md](LEVEL_GENERATOR.md), [BUILDINGS.md](BUILDINGS.md), `docs/design/MANTLING.md`,
`docs/design/LADDERS.md`, `Content/Data/LevelGen/obstacle_heights.json`, `Source/CodexTactics/Public/.../DoorwayBreachRules.h`,
`Tools/TrainingGround/src/levelgen/` (`generate.ts`, `buildingTraversal.ts`).

Sections: 1 generation pipeline - 2 data contract - 3 builder + kit - 4 runtime (C++ / AnimBP) - 5 Training Ground page -
6 tests, gates, budgets - 7 phases - 8 ADRs + owners.

---

## 1. Generation pipeline

Map level first, then per building in its local frame (axis-aligned, origin at the footprint centre, turned by `rotation`
at the end; every rule and check runs in the local frame).

0. **Map**: building roles (building-stories 1) from `objectives/*.json`, `tactical/*.json`, `R_main`; district themes (2).
1. Archetype + shell (footprint on 50 cm, storeys) - *done* for existing archetypes.
2. Vertical core - *done* (switchback core; halls' mezzanine).
3. Rooms per storey (corridor spine / BSP / flats) - partly done (BSP); corridor + flats *S2*.
4. Doors + windows (two exterior doors > 150 m2 *done*), balconies.
5. Default opening states (opening-states 3).
6. **Story**: pick by role budget + weight, apply bricks, validate (reachability + choice test), roll back on failure.
7. Author + outcome, trace props.
8. Breach panels: one scored pass (breach-panels 4).
9. **Map**: building links (stories 14, 18), then pacing check along `R_main` (building-stories F4), re-roll offenders.
10. Interior content (cover, furniture) - P3.
11. Validate every building (route model); per-building regen with the next sub-seed; results on the page.

Deterministic per (seed, building id, cost-table hash); per-building re-roll and lock on the page (regen / locks already
exist for regions).

## 2. Data contract

**Schema bump to `codex.levelgen_layout/2`** (technical director): the defaults change meaning (a missing door / window
`state` is `open` / `broken` in `/1`, the default table of opening-states 3 in `/2`), so this is not an optional extension of
`/1`. `/1` layouts still load with the old defaults and build bit-identically. All coordinates below are in the building's
**local frame** (cm, like the existing building fields).

```ts
interface GenLayout { /* existing */ schema: 'codex.levelgen_layout/2'; cost_hash: string;       // sha1 of building_route_costs.json
  building_links?: BuildingLink[]; districts?: { id: string; lots: string[]; theme: 'pmc' | 'survivors' | 'cold' | 'decay' | 'mixed' }[] }
interface WallOpening { /* existing */ state?: DoorState | WindowState }
type DoorState = 'open' | 'closed' | 'locked' | 'barricaded' | 'sealed';
type WindowState = 'broken' | 'glazed' | 'barred' | 'boarded' | 'fixed';
interface Stair { /* existing */ blocked?: 'rubble' | 'burnt' | 'barricade'; down_only?: boolean }
interface Balcony { id: string; floor: number; rect: [Vec2, Vec2]; door_id: string; rail_cm: number }          // depth >= 150 at escape stops
interface FireEscape { id: string; kind: 'stair' | 'ladder'; side: Side; at: Vec2;                             // foot position on the facade
  ground: Vec2;                                                                                                 // approach cell of the ground stop
  stops: { floor: number; balcony_id: string }[]; to_roof: boolean; down_only?: boolean;
  ladder_ids?: string[] }                                                                                       // kind 'ladder': entries in layout.ladders
interface SnowRamp { id: string; from: Vec2; to_opening: string; width_cm: number }                            // story 10; slope <= 35 deg
interface BreachPanel { id: string; wall: string; floor: number; at: Vec2; width_cm: 200; wide_with?: string;  // adjacent panel id = wide breach
  hole: [number, number]; links: [string, string]; score: number }
interface BuildingLink { id: string; kind: 'shared_wall_hole' | 'plank' | 'roof_bridge'; a: string; b: string; at: Vec2[] }
interface StoryInstance { id: string; bricks: string[]; seed: number; author: 'survivors' | 'pmc' | 'cold' | 'decay'; outcome?: 'held' | 'fell' }
interface GenBuilding { /* existing */ role?: 'objective' | 'garrison' | 'overwatch' | 'transit' | 'background';
  balconies?: Balcony[]; fire_escapes?: FireEscape[]; snow_ramps?: SnowRamp[]; breach_panels?: BreachPanel[];
  stories?: StoryInstance[];                                                                                    // 0-2 (F2 second story)
  key_route?: Route; breach_route?: Route; fast_entry?: string; quiet_entry?: string }
interface Route { legs: { from: string; to: string; mode: string }[]; cost: number; detour: number; cost_hash: string }
```

Stored routes carry `cost_hash`; on load a route whose hash differs from the layout's is **recomputed**, never trusted.

Tuning data (*proposal*, JSON, read by both the TS generator and the C++ runtime - one source):

| File | Holds | GDD |
|---|---|---|
| `Content/Data/LevelGen/building_stories.json` | role budgets, weights per role x archetype, eligibility, authors, compatibility, `p_two`, `M_detour`, radii | building-stories |
| `Content/Data/LevelGen/building_route_costs.json` | `v_ref`, `t_rung`, `n_squad`, action times, noise radii, profile weights, exposure factors, bands, `G_min`, `k_def`, `P_max`, drift gate | building-route-model, opening-states, breach-panels |
| generator table | template (`blockTemplates.ts` `set_piece` / `id`) -> archetype, min footprints | building-archetypes |

Done in S2 R4: `building_route_costs.json` (ROUTE_COSTS, `buildingGraph.ts`) and `building_stories.json` (pick knobs, compatibility,
detour roles, story 4 / 9 eligibility; `buildingStories.ts`) exist and are read by the TS generator; which building fields are in the
local frame vs world coordinates is the table `Content/Data/LevelGen/building_coord_fields.json`.
`GenRoom.anchor?: Vec2` (2026-11-19): a walkable representative point of the room on its own floor, in the building frame (like `rooms[].bounds`), set by `roomAnchors.ts` (`roomAnchor`, 25 cm grid, >= 50 cm clear of floor gaps, flights touching the floor, hatches, floor holes, partitions; none when no such point) at the end of `finalizeDraftToLayout` and by `npm run levelgen:anchors` for existing files; the engine smoke `LevelGenBuildingsSmoke` probes it instead of the bounds centre (the centre of a stair strip lies on a gap / flight).

### 2.1 Validator changes (`buildingTraversal.ts`)

| Change | Why |
|---|---|
| Issues get `severity: 'error' \| 'warning'`; `ok = no errors` (today `ok = issues.length === 0`) | bands, `D`, shares are warnings |
| Entries = exterior doors **and** vaultable ground windows, snow-ramp windows, fire-escape ground stops (today doors only) | stories 9, 10, 4 |
| Opening states: blocked states remove the edge; `locked` / `glazed` edges carry their cost | opening-states |
| `stair.blocked` removes the flight edge (today every stair is an edge) | stories 4, 13 |
| Fire escapes, balconies, walkways, snow ramps as edges; `down_only` / `drop` one-way | archetypes 3 |
| Breach panels: wall for reachability, `explosive` edge for the breach route | breach-panels |
| New codes: `unreachable_without_explosive` (error), `story_rolled_back` (warning), `blocked_flight_no_bypass` (error), `balcony_orphan` (error), `no_choice` (error for choice stories, F4), `detour_band` (warning), `pacing_detours` (warning) | GDDs |
| Key route + fast / quiet entries + `D` per building in `analysis.buildings[]` | route model |

## 3. Builder + kit (`Scripts/Editor/build_levelgen_map.py`, kit = BUILDING_GENERATOR.md section 7)

- **Instancing**: modules are emitted as ISM / HISM per module type per building (or Packed Level Actors); interactive pieces
  (leaves, panes, breach panels) stay actors. Budget section 6.
- Door states: `open` - frame only; `closed` / `locked` - a door leaf actor with tags `door_state=closed|locked`,
  `CanEverAffectNavigation = false` and no pawn collision until R1 (opening-states 5); `barricaded` - leaf + barricade props
  per author (blocking collision, affects navigation), tag `door_state=barricaded`; `sealed` - wall fill.
  Collision of the state actors (2026-11-20): `use_default_collision = False` + `response_array` on the body instance (TRAPS.md, "Scripted collision responses").
- Window states: tags `window_state=*` on the opening; `glazed` - a glass pane actor (no pawn collision until R2, visual),
  `barred` - bars mesh with pawn collision but no visibility block, `boarded` - boards mesh (visibility + collision),
  `broken` - empty frame; loopholes / shop windows `fixed` - glazing with collision.
- Blocked flight: the ramp stays, a nav modifier volume (NavArea_Null) over it + a blocking box + rubble props.
- Balcony: slab + rail (95, `CodexMantleEdge`) + balcony door opening; fire escape: steel stair ramp or `ALadderActor`s
  (`ladder_ids`); walkway module between balconies.
- Snow ramp: a walkable ramp mesh (collision ramp like the stair) + snow visual; ends at the window sill.
- Breach panel: module `wall_breach_200` (intact, tag `breach_panel=<id>`) + its breached variant spawned hidden.
- Author traces: prop sets per author (building-stories 4), 3-5 variants each, placed outside the keep-outs (doors, approach
  boxes, ladders, rubble boxes).
- Kit module names extend contract 7.2: `door_leaf`, `door_lock_plate`, `door_lock_tell` (large tell), `window_glass`,
  `window_bars`, `window_boards`, `balcony_slab`, `balcony_rail`, `balcony_walkway`, `fire_escape_flight`,
  `fire_escape_landing`, `snow_ramp`, `rubble_flight`, `barricade_door_<author>`, `wall_breach_200` (+ `_broken`).
- Legacy (`/1`) layouts build bit-identically (existing test).

## 4. Runtime (C++ / AnimBP) - separate tasks, the generator does not depend on them

| Task | Feature | Need | Notes |
|---|---|---|---|
| R1 | Open / close / kick / shotgun breach door | door leaf component, open action, kick clip + noise event + leaf swap | `DoorwayBreachRules` covers the squad breach positions; kick animation clip needed (pack?) |
| R2 | Glass shatter | breakable pane by **mesh swap** (not Chaos, technical director), noise, vault through | later: cuts |
| - | Tear off boards | later | |
| R3 | Breach panel | damage from RPG / charge / mine -> swap intact -> breached + rubble + dust; tile-local navmesh rebuild (navmesh already `RuntimeGeneration=Dynamic`); cover / LOS / tactical-data refresh | R3 spike before S4, budget section 6 |
| - | Blocked flight | nav-exclusion + rubble collision | builder only |
| - | Fire escape / balcony | ladders exist; balcony = slab + mantle rail | builder only |
| R4 | PMC AI | edge costs as navmesh area costs / query filters (ADR-1); defenders cover breach panels and locked doors | ties to `docs/design/PMC_DEFENCE.md`; profiles after R1 |
| P0b | Bot smoke | `LevelGenBuildingsSmoke` walks every building's key route, measures drift (route-model F5) | queued for an API agent |

Safe degradation until a feature exists (opening-states 5): `closed` and `locked` behave as `open`, `glazed` as `broken`,
`barricaded` / `boarded` / `sealed` / `barred` as blocked, breach panels as walls. The data does not change when the feature
lands.

## 5. Training Ground page

- «Здания» panel *done* (traversal per building, «Принять» gate). Adds: role, story names, author / outcome, key route as a
  readable chain ("окно Б (разбить) -> кв. 2 -> балкон -> пожарная лестница -> 2 этаж"), cost, `D`, fast / quiet entries and
  the choice flag, a «другая история» button per building (re-roll with the next sub-seed) and a lock.
- Plan view overlay: door / window state glyphs, blocked flights, balconies, fire escapes, snow ramps, breach panels, the key
  route line, roles and district themes.
- Map summary: role budget shares, pacing, `share_open`, `quiet_share`, choice share, detour bands, rollback count; after a
  bot run, drift per building.

## 6. Tests, gates, budgets

- TS unit: every brick and story on a fixture building (state applied, rollback byte-equal, determinism per seed + cost hash);
  roles from fixture goals / places; `/1` vs `/2` defaults.
- Validator: reachability (every room, storey, roof) without explosives and drops on every generated building (x1 / x6,
  5 seeds) = 0 errors; the pre-fix X6 fixture keeps failing (audit reproduction test *done*).
- Distribution: role story shares within targets +- 10 percentage points, seeds pooled; pacing; rollback <= 10 %.
- Determinism: byte-equal layout JSON on 5 seeds in CI (fixed float rounding, sorted keys).
- Builder unit: state -> actors / tags / nav flags; instancing; legacy bit-identical.
- In-engine: `LevelGenBuildingsSmoke` per building key route (PASS >= 95 %, drift <= 15 % for >= 95 %); `LevelGenRampSmoke`
  on every flight; `DoorBreachSmoke` on a locked door once R1 exists.
- Gate before «Принять»: validator 0 errors (or the forced checkbox, recorded as `analysis.forced_buildings`).
- Each GDD's Acceptance Criteria list is the per-system checklist; this section is the shared harness.

Budgets (*proposal*, measured on X6):

| Budget | Value |
|---|---|
| Navmesh tile rebuild after a breach | <= 2 ms per frame, time-sliced; path through the hole ready < 1 s |
| Module actors | instanced; <= 40 non-instanced actors per building (leaves, panes, panels, ladders) |
| X6 load: navmesh build | baseline measured before S1; S1-S3 must stay <= baseline + 20 % |
| Generation time (page) | x6 map <= 5 s in the browser |

## 7. Phases (*proposal*, each = a branch -> lead review -> merge)

| Phase | Content | State |
|---|---|---|
| P0 | traversal validator, stair core, slab gaps, panel + gate | **done** (claude/building-generator, review fixes 373335af) |
| P1a | hall mezzanine, cores everywhere, 2 exterior doors | **done** (70a56a7b, 448dca32) |
| **P0-proof** | X6 rebuilt in Unreal from the P0 layout + `LevelGenBuildingsSmoke` PASS >= 95 % (technical director: prove the base before S1) | after the merge, full checkout, designer's editor closed |
| S1 | schema `/2` + validator changes (2.1) + default states + roles + stories **3, 4, 7, 10** + key route + choice test + page | **not started until the designer / studio says so**; after P0-proof |
| S2 | rooms: corridor spine, flats, hospital / school archetype (story 5), shop + flats (story 19) | |
| S3 | stories 6, 8, 9, 11-14, 16-18, 20 + weights JSON; links last; LOS-based exposure from PMC positions | needs the survivors entry in NARRATIVE_BIBLE |
| R3 spike | one breach on X6, rebuild timing | before S4 |
| S4 | breach panels (generator + validator) | after the studio decisions (breach-panels Q4-Q6), charges in WEAPONS_FRAMEWORK, the charge economy |
| R1..R4 | runtime: doors (open / kick / shotgun), glass (mesh swap), breach panels + dynamic navmesh, PMC AI costs | C++ tasks, owners Q12 |
| K | kit modules for states / balconies / escapes / ramps / breach panels / author traces (BuildingKitLab pipeline) | art track |
| L | stories 15, 21, 22 (barricade clearing, elevator shaft, basement level -1) | later |

Process constraints (2026-11-18): nothing is merged into main until the designer has tested; the X6 rebuild in Unreal and the
ladder bot run happen after the merge, on the full checkout, with the designer's editor closed. The mezzanine edge stays
parked (building-archetypes Q9).

## 8. ADRs + owners

ADRs (Proposed 2026-11-18, `docs/architecture/adr-000{1,2,3}-*.md`):

| ADR | Decision (proposal) |
|---|---|
| ADR-1 Navmesh is the runtime truth | Edge modes become NavAreas / NavLinks (closed / locked = area cost, not geometry; intact panel = NavModifier Null); the profiles `balanced` / `quiet` / `fast` become `UNavigationQueryFilter`s; the PMC AI queries the navmesh instead of a second graph; one cost JSON read by TS and C++; the smoke gates the drift (route-model F5). |
| ADR-2 Layout schema `/2` + budgets | the `/2` bump of section 2, `cost_hash`, recompute-on-load, the budgets of section 6. |
| ADR-3 Runtime representation of opening states | which states are collision, which are nav area costs, which are NavLinks; the degradation of opening-states 5. |

| Track | Owner |
|---|---|
| Generator TS (P*, S*) | Claude |
| Builder Python | Claude / API agent, Claude review |
| Runtime C++ R1-R4 | open question Q12 (C++ gameplay = Claude by the role table; animations = the designer's packs) |
| Kit K, materials, author trace props | Claude (BuildingKitLab, headless) |
| Survivors in NARRATIVE_BIBLE | narrative (designer decides) |

Open question **Q12**: who owns the runtime tasks R1-R4? (*rec*: C++ gameplay = Claude by the role table; animations = the
designer's packs.)
