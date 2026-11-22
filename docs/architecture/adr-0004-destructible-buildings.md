# ADR-0004: Destructible generated buildings - damage follows the precomputed structure

## Status
Draft (design-ahead). The "Proposed decision" is the architect's proposal, **not decided**; nothing here is scheduled for
implementation. Source: designer intent 2026-11-21.

## Date
2026-11-21

## Last Verified
2026-11-21, UE 5.8.2. main `e67dd784`: `Config/DefaultEngine.ini`, `Source/CodexTactics/{Public,Private}/{Combat,Tactics,
LevelGen,Interactables,AI,Core}`. `origin/claude/bgen-test` `024ab75c`: ADR-0001..0003, `BUILDINGS_TECH_SPEC.md`, the
building GDDs, `Scripts/Editor/build_levelgen_map.py`, `Tools/TrainingGround/src/levelgen/schema.ts`,
`KIT_PILOT_2026-11-20.md`. ADR-0001..0003 and the building GDDs are not in main yet; the links below resolve after that merge.

## Decision Makers
Designer (intent; answers to "Questions for the designer"); Claude (architect's proposal). Nobody has decided yet.

## Summary
Buildings become fully destructible - doors, windows, walls - and the damage follows the building's structure: load-bearing
walls are tough, annexes and plaster partitions are weak. The level generator computes this in advance (class and material per
wall segment / opening, and who carries what); the runtime only looks the answer up. Proposal: the gameplay truth is an **HP
grid of wall cells on the collision proxies** (pure deterministic rules); a hole is a **runtime opening** cut with the builder's
own slicing rules; the look is **pre-made damaged / destroyed kit module variants** swapped per bay; a collapse is a **discrete,
precomputed outcome**. Chaos fracture is not used for gameplay. Now: data only (phase P0).

## Engine Compatibility
UE 5.8.2. Recast navmesh, `RuntimeGeneration=Dynamic`, agents Default (r 35, h 144) and Vehicle (r 200); no tile / cell
overrides in the ini (engine defaults); maps are baked by `Scripts/bake_navmesh.ps1`. The project links no Chaos destruction
module today (nothing in `CodexTactics.Build.cs` / `.uproject`). No engine change, no new plugin in the proposal.

## ADR Dependencies
ADR-0001 (the navmesh is the runtime truth), ADR-0002 (schema `/2`, budgets), ADR-0003 (opening states; this ADR widens its
"breach panel: blocked until damaged" row from marked panels to every wall).

## Context

### Time model (verified)
Real time is the default in a fight; a Space tap is the tactical pause (world x0.02, limited charges, fights only); holding
Space enters the turn-based grid, laid on open ground with building footprints subtracted (`CombatTimeModeRules.h`,
`TurnArenaRules.h`). So destruction is a **real-time** feature that must also behave under the pause (orders queued while the
navmesh changes). The turn grid treats buildings as closed cells; this ADR does not change that.

### What the game has today (verified by grep on main)

| Area | Today | Gap for destruction |
|---|---|---|
| Damage | `UHealthComponent` on characters, `ABarricadeActor` (200 HP), barrels, generators, vehicle hulls | no HP on walls, doors, windows |
| Explosions | one entry `Explosions::Detonate(FExplosionSpec)`: grenade, barrel, mines / tripwires / trapped objects, projectile blasts, RPG-7 (`LauncherRules`); walls only **occlude** a blast (`ExplosionRules` cover rule) | no structure pass; no breaching charge item (breach-panels.md: blocks S4) |
| Bullets | hitscan `ECC_Visibility` traces, `AProjectileActor` sweeps; armour classes Light..Vehicle | walls stop rounds and take nothing |
| Navmesh | dynamic; runtime flips already ship: `AGateActor`, moved barricades (`URelocationSubsystem`), NavModifiers on mines / tripwires | breach rebuild cost never measured (R3 spike not run) |
| Cover | `CoverTraceRules` traces collision live, so it follows geometry | a unit in a slot whose wall is gone is not told |
| Sight | `UTacticalSightSubsystem`, `UVisionConeComponent` trace `ECC_Visibility` live | a cone mesh rebuilds only when its owner moves / turns (`RequestRebuild()` exists, called only by smoke commands) |
| Baked tactics | `tactical/<Map>.tactical.json` (chokepoints, windows, overwatch roofs, hiding spots, `geometry_hash`) read once by `UTacticalMapSubsystem`; PMC posts are planned from it (`UPmcSquadSubsystem`) | stale after a hole; the analysis lives in TS, not in the game |
| Doorways | `UDoorwaySubsystem` registry from the layout JSON; `RegisterDoorway` works at runtime | a new hole is not a doorway |
| Buildings | cube `StaticMeshActor`s, each wall run sliced round its openings (segment / sill / lintel; tags `building=`, `floor=`); state actors tagged `door_state=*`, `window_state=*`, `breach_panel=<id>` | no C++ reads those tags (R1-R3 not started); pieces carry no wall id |
| Kit | kit visuals `NoCollision` over hidden proxies; one block = 779 kit actors + 244 proxy actors (pilot) | ADR-0002 allows 40 non-instanced actors per building; the pack has intact modules only |
| Save | outside fights only; optional JSON sections per version (v3) | no building state |

### Constraints
- **C1** The navmesh is the runtime truth (ADR-0001): a hole must become walkable through Recast, not through a second graph.
- **C2** Computed in advance: toughness and collapse come from generator data, never from a runtime physics guess.
- **C3** The collision proxy is what blocks movement, sight, bullets, blasts and navmesh; the kit is dressing. Proxies are cut
  on the 50 cm grid, kit bays are 300 cm (pilot: 20 of 52 facade fillers stretched beyond the 10 % rule) - the two
  granularities differ.
- **C4** Scale: the committed X6 layout has 70 buildings (57 enterable), 781 walls, 397 openings. Measured on block `B_x`:
  396 m of wall = about 260 cells at 150 cm or 400 at 100 cm; order of 10-20 thousand cells per map (estimate). Cells can
  never be actors.
- **C5** Consumers that cache geometry: vision cones, PMC posts, tactical JSON, doorway registry, occupied cover slots,
  mantle / vault / drop / ladder links owned by a wall.
- **C6** Deterministic outcome: smokes are judged on repeated seeds, the PMC trainer compares runs, the replay page redraws
  from events.
- **C7** Budgets (ADR-0002, *proposal*, unmeasured): navmesh rebuild <= 2 ms per frame time-sliced, path through a hole
  < 1 s.
- **C8** building-route-model rule 5 stays: every room, storey and roof is reachable without explosives and drops.
- **C9** Single player: replication cost is not a criterion.

## Options Considered

| Criterion | (a) Chaos Geometry Collections, pre-fractured | (b) Swap modules intact -> damaged -> destroyed | (c) Cell HP grid on the proxies | (d) Hybrid: (c) truth + (b) look + cosmetic debris |
|---|---|---|---|---|
| Authoring with generator + kit | fracture every kit module family and keep it valid under the pilot's stretch (x 0.06-1.15; unverified for fractured pieces); one GC per bay instead of an instance | 2 extra meshes per module family (about 10 families in the block series -> about 20 meshes, estimate); the pack has none | none for gameplay; **no look** on its own (a hole in a hidden grey box) | as (b), and the variants can be tool-baked because the hole shapes are the fixed cell shapes |
| Follows the precomputed structure | weakly: Chaos thresholds and its own connectivity graph decide what falls = runtime guesswork (against C2) | per module only (300 cm bay) | directly: HP per cell from class x material | directly |
| Runtime cost | rigid bodies and physics islands per break, memory per GC; thousands of GC components on a map unless pooled (engine ISM pool for unbroken GCs: not verified here) | cheapest: instance swap | small: byte arrays, one re-slice of a wall run per hole | as (c) + instance swap + pooled debris |
| Navmesh | debris must stay off the navmesh and the hole is still carved by hand -> needs (c) underneath anyway | proxy boxes swapped with the module -> dirty tiles | one wall run's collision changes -> dirty tile(s), async time-sliced rebuild; alternative to measure: Null-area modifier per run (modifier-only update) | as (c) |
| Cover / LOS refresh | pieces block `ECC_Visibility` wherever they land: unpredictable cover and sight | exact but coarse (whole bay) | exact: live traces follow the proxies; cached consumers need one event | as (c) |
| Determinism / replays | the result depends on the solver step; a replay cannot redraw it from events (against C6) | deterministic | deterministic pure rules, unit-testable, mirrorable in TS for the page | deterministic truth; debris is never queried |
| Save / load | per-piece transforms, or snap to a canonical state (visible pop) | one state byte per module | bitset + HP bytes per building | as (c); visuals rebuilt from cell state |
| Main risk | cost, unpredictability, the heaviest authoring | too coarse for "walls anywhere"; kit bays do not match the proxy grid | wrong look alone | two layers to keep in step (cell -> module map) |
| Verdict | rejected for gameplay; optional cosmetic layer later | kept for leaves and glass (TECH_SPEC R1 / R2 already say mesh swap) | the truth layer | **proposed** |

## Structural Model (computed by the generator)

**Elements**: wall segment (existing ids: `B_x_N_0` perimeter run, `B_x_part_f0_0_0` interior piece), opening (door / window
ids), slab panel (new: one per room / bay per storey), stair flight, balcony, fire escape, wall ladder, parapet.

**Classes** (`structure`): `load_bearing` (carries slabs or flights), `facade` (outer skin: carries itself and what hangs on
it), `partition` (carries nothing), `annex` (light attached volume: carries only its own roof). **Materials**:
`concrete_panel | brick | plaster | wood | sheet_metal`. An opening inherits its wall's class and has its own material.

**Classification by construction system** (*proposal*, generator owner): `block` = panel cross-wall: the plan's `core_wall`,
stair-core walls and the walls between flat columns are load-bearing concrete, room walls are plaster partitions; `hall` /
`warehouse` = frame: frame lines load-bearing, cladding is `facade` sheet metal or brick infill (never a collapse); `office` =
brick perimeter + spine load-bearing; `shed` and attached volumes = `annex`; `ruin` = already failed, no further collapse.

**Load path graph**: directed edges "A carries B". Each slab panel and flight lists `supported_by[]` and `needs` (how many
supports must stand); each wall lists `supports[]` (slab panels, flights, the wall segment above, and what hangs on it:
balcony, fire escape, ladder, parapet). The graph is acyclic and every element reaches the ground.

**Collapse rules** - the level is the designer's choice (questions 1-2):

| Level | When a load-bearing element is gone | Cost |
|---|---|---|
| L0 none | nothing falls; load-bearing only means "very tough" or indestructible | data only |
| L1 local (*proposed*) | each carried element with standing supports < `needs` fails **once**, with its precomputed outcome; no chain | small: outcomes are existing layout concepts |
| L2 progressive | failed loads move to the neighbours, chains possible | validator must simulate chains; soft-lock risk |

Outcomes reuse the layout vocabulary the builder and validator already know: slab panel -> a floor hole + rubble heap + `drop`
edge; flight -> `stair.blocked = rubble` (block box + `NavArea_Null`); hanging element -> removed with its nav links.

**Data contract sketch** (additive and optional in schema `/2`; absent = indestructible, the same safe degradation as opening
states; `hp_class` is resolved by the generator and stored so the runtime never guesses):

```ts
type StructureClass = 'load_bearing' | 'partition' | 'annex' | 'facade';
type StructureMaterial = 'concrete_panel' | 'brick' | 'plaster' | 'wood' | 'sheet_metal';
interface StructureInfo { structure: StructureClass; material: StructureMaterial; hp_class: string;   // key of structure_classes.json
  supports?: string[];        // element ids this one carries
  protected?: boolean }       // never fails (validator-set, rule V2)
interface GenWall     { /* existing */ struct?: StructureInfo }       // perimeter runs and interior pieces
interface WallOpening { /* existing */ struct?: StructureInfo }       // leaf / pane / frame
interface SlabPanel   { id: string; floor: number; rect: [Vec2, Vec2]; supported_by: string[]; needs: number;
  on_fail: 'floor_hole' | 'none'; protected?: boolean }
interface Stair       { /* existing */ supported_by?: string[]; on_fail?: 'blocked_rubble' | 'none' }
interface GenBuilding { /* existing */ slab_panels?: SlabPanel[]; structure_hash?: string }   // hash of structure_classes.json
// Content/Data/LevelGen/structure_classes.json (values TBD, tuned in data, one source for TS and C++)
// { classes: { "<structure>.<material>": "<hp_class>" },
//   hp_classes: { "<hp_class>": { hp_per_cell, min_breach_class, noise_m, rubble } } }
```

**Validator rules** (TS, before «Принять»):
- **V1** Rule 5 unchanged: reachability is checked with every wall intact. Holes only add edges, so they cannot break it.
- **V2** Collapse removes edges, so it can: for every single load-bearing failure the generator replays the L1 outcome and
  requires every objective room, exit and key-route top to stay reachable on the walkable graph. An element that fails the
  test gets `protected: true`. A stair core is protected unless a second vertical route (fire escape, ladder) exists.
- **V3** Graph sanity: acyclic, every slab panel and flight has `supported_by.length >= needs`, a partition supports nothing,
  an annex supports only annex elements, every `hp_class` resolves.
- **V4** A wall that carries a ladder, a fire escape or a balcony on the key route is protected, or the route is re-checked
  without it.
- **V5** Determinism: byte-equal layouts on 5 seeds including the structure fields; `structure_hash` marks stale data.

## Proposed Decision (architect's proposal, not decided)
Option **(d)** with (c) as the single truth:
1. **Truth layer**: pure `BuildingDamageRules` + a world subsystem that reads the structure from the **layout JSON** (keyed
   by wall / opening id, not from actor tags) and `structure_classes.json`. A wall run is a row of cells (nominal pitch
   150 cm = the planned door / window width and half a kit bay; 100 cm is the alternative - to settle in P2) in three bands
   per storey that the builder already uses: sill 0-95, body 95-245, lintel 245-300. Body gone = a window-like hole (sight,
   fire, vault over the 95 cm sill); sill + body gone = a walk-through hole.
2. **A hole is a runtime opening**: destroyed cells re-slice their wall run with the builder's `_slice_wall_with_openings`
   rules ported to C++ (parity-tested on fixtures). Sill, lintel, mantle edge and doorway registration come for free; rubble
   is the breach-panels F3 box (<= 35 cm, never blocks). Undamaged runs stay exactly as built (lazy split), so untouched
   buildings, the legacy bit-identical build and the navmesh bake do not change.
3. **Look**: the kit dresser records which module instance covers which wall span; cell state picks the variant (intact /
   hole left / hole right / destroyed). Debris is cosmetic (pooled meshes or Niagara), no gameplay collision.
4. **One event** `OnStructureChanged(building, bounds, kind)`: vision cones in range rebuild, cover occupants re-validate,
   tactical spots and PMC posts inside the bounds are invalidated and the commander re-plans, the hole registers as a
   doorway, noise is raised, the replay log and the save record it. The navmesh follows the collision change by itself; AI
   treats "hole open" as an event delivered when its tiles finish.
5. **Entry points**: `Explosions::Detonate` gets a structure pass (cells in the radius, behind the existing occlusion rule);
   bullets only if question 3 says so. A weapon needs `min_breach_class` (the armour-class idea reused for walls).
6. **Collapse**: L1, discrete and precomputed; units on a failing slab panel go through the existing fall rules.
7. Breach panels become the marked **weak cells** of this system (a tell + a low `hp_class`), not a separate mechanism.

### Phases

| Phase | Content | Needs first | Gate |
|---|---|---|---|
| **P0 data only (now)** | generator writes `struct` per wall / opening, `slab_panels`, `supported_by`, `structure_classes.json`; validator V1-V5; page overlay coloured by class; builder may copy `structure=` / `material=` into tags; runtime ignores all of it | classification table per archetype | TS units, determinism on 5 seeds, legacy `/1` builds bit-identical |
| **P1 doors + windows** | leaves and panes break by material (wood / steel / glass); state flips of ADR-0003 driven by damage; event + save section | R1 / R2 (leaf + pane components), kit visuals instanced, the R3 navmesh timing spike, damaged leaf / pane meshes | `DoorBreachSmoke`, glass smoke, budget C7 measured on X6 |
| **P2 partitions + annex walls** | cell grid, runtime slicer, module variants for interior / annex families, tactical invalidation, cone rebuild, PMC re-plan | charge item + charge economy (designer), cell -> module map from the kit dresser, slicer parity tests | a hole is walkable < 1 s; no unit left in a dead cover slot; repeated-seed smoke identical |
| **P3 load-bearing + collapse** | supports graph at runtime, L1 outcomes, falls, facade variants, collapse VFX / SFX | answers 1-2, V2 simulation proven on generated maps, P2 stable | `BuildingCollapseSmoke`; no soft-lock on 5 seeds; frame budget held |

## Consequences
- **Positive**: one deterministic truth; untouched buildings cost nothing; every outcome is an existing layout concept, so the
  generator, the validator and the page can show it before it is built; data ships years before code without changing.
- **Negative**: no free-form rubble physics; damaged variants must be made per module family; the builder's slicer exists
  twice (Python and C++) and needs parity tests; a hole is plain navmesh, so everyone uses it - the horde included.

## Risks and Not Verified
- Navmesh rebuild cost after a hole is **unmeasured**. A Recast tile is a vertical column, so a hole on storey 3 re-rasterises
  all storeys of that tile. Fallbacks: the modifier-only path, smaller tiles, at most N dirty runs per second.
- Whether a Null-area modifier update is really cheaper than a geometry change on our tiles: assumption, measure in the spike.
- Instanced kit visuals with per-instance variant swap in 5.8: assumption. The kit pilot is 779 actors per block today, far
  over the ADR-0002 budget; instancing is a precondition for P1 whatever option is chosen.
- Damaged variants of stretched modules must stretch the same way; the hair-line seams of the pilot may show at ragged edges.
- Under the tactical pause an order preview through a fresh hole may see the old navmesh; previews wait for the tile.
- Props, loot and bodies on a failing slab panel need a rule (move onto the rubble heap or destroy).

## Performance Implications
State: a few bytes per cell, under 100 KB per map (estimate). A blast adds one spatial lookup per building in range. The
frame cost is the navmesh rebuild (C7) and the consumer refresh after the event - both bounded by rate limits, both to be
measured by the P1 spike before P2 starts.

## Validation Criteria
P0: the fields round-trip, V1-V5 pass on x1 / x6 with 5 seeds, the page shows classes and supports. Later phases: the gates
in the phase table; every phase keeps `LevelGenBuildingsSmoke` PASS >= 95 % and the drift gate of ADR-0001.

## Questions for the Designer

| # | Question | Architect's lean |
|---|---|---|
| 1 | Can load-bearing walls be destroyed at all - or are they only "very tough", or never? | destroyable only by the heaviest means |
| 2 | When a load-bearing wall goes, what happens above: nothing / that piece of floor falls (a hole and rubble; people on it fall) / more can follow? | one piece falls, no chain |
| 3 | Which weapons destroy what? Do rifles chew through plaster and wood over time, or only explosives count? Shotgun on locks, grenade on partitions, RPG / charge on brick, big charge on concrete? | explosives for walls; bullets only glass and wooden doors |
| 4 | Can enemies destroy too (PMC firing an RPG at your cover wall, monsters breaking doors and boards)? | PMC yes, later; monsters doors only |
| 5 | Is destruction permanent for the whole mission and kept in saves? | yes |
| 6 | Noise: is every destruction loud (a collapse louder than a charge)? Should quiet destruction exist (prying boards, cutting a lock)? | loud; quiet ways later |
| 7 | May a staircase be destroyed when it is the only way up? | no - protected unless a second way exists |
| 8 | May the floor under a mission goal, a survivor or key loot fall? | no - protected |
| 9 | Fixed hole shapes (window-size, door-size, whole bay) with pre-made damaged modules, or any shape? | fixed shapes |
| 10 | How much may a big collapse cost: must the frame rate hold, or is a short dip hidden by a dust cloud acceptable? | hold it; dust hides the swap |
| 11 | Does "every building is passable without explosives" stay? Do marked breach panels stay as the visible weak spots? | yes to both |

## Related
[BUILDINGS_TECH_SPEC.md](../design/levelgen/BUILDINGS_TECH_SPEC.md) (R1-R3, budgets),
[breach-panels.md](../design/levelgen/gdd/breach-panels.md) (Q4 is reopened by this intent),
[opening-states.md](../design/levelgen/gdd/opening-states.md),
[building-route-model.md](../design/levelgen/gdd/building-route-model.md) (rule 5),
[KIT_PILOT_2026-11-20.md](../design/levelgen/KIT_PILOT_2026-11-20.md), ADR-0001, ADR-0002, ADR-0003.
