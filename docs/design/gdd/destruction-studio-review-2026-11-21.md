# Destruction of buildings and cover - studio design review (2026-11-21)

> **Status**: studio-level review for the designer and the lead. Nothing here is decided or scheduled. It builds on
> [ADR-0004](../../architecture/adr-0004-destructible-buildings.md) (draft) and proposes five amendments to it.
> **Author**: Claude Fable (senior systems / technical design review session, 2026-11-21), for the lead session.
> **Method**: read-only research. No build, no engine run, no measurement was made for this document.
> **Source of intent**: designer 2026-11-21 - buildings fully destructible (doors, windows, walls), damage follows the
> structure (load-bearing tougher, annexes / plaster partitions weaker), structure computed in advance by the generator.

Marking: **[V]** = verified today by reading the named file (repo or UE 5.8 engine source); **[A]** = assumption, not
verified; *proposal* = my recommendation, not decided; **[D date]** = a designer decision already made.

## 0. Verdict in one page

ADR-0004's core is right and stays: the gameplay truth is a deterministic, precomputed state (not physics), Chaos fracture is
kept out of gameplay, and data ships before code. I recommend five amendments:

1. **Penetration before destruction.** The cheapest and most valuable part of "walls follow their material" is not holes -
   it is *soft cover vs hard cover*: a rifle round passes a plaster partition or a wooden door and hurts the man behind it,
   and does not pass brick or a concrete panel. It needs no geometry change, no navmesh work, no new art, and it already
   changes where the player and the enemy choose to stand. ADR-0004 has no such rule. It becomes phase P1.
2. **The generator precomputes the cells; the runtime never slices.** ADR-0004 ports the builder's wall slicer to C++ and
   keeps two slicers in parity. Instead the generator exports the cell decomposition of every wall run (rectangles, class,
   flags, supports); the runtime only toggles precomputed pieces. One source, no parity port.
3. **The look is a function of the truth.** ADR-0004 swaps authored damaged variants per kit module (about 20 meshes per
   series, and a cell -> module map to keep in step). I propose a per-building *damage field* (a small texture written from
   the cell states, read by the wall material as a mask) plus a dozen generic rim / rubble meshes. It works the same on the
   kit's instanced modules and on the merged interior partitions, and it cannot drift from the truth. It needs a 2-day
   spike; authored variants stay the fallback, and stay the method for door leaves and window panes.
4. **Holes never collapse anything.** First release: no collapse at all (walls that carry a route element are protected).
   Local collapse of one floor panel comes last, only when most of a load-bearing run is gone, only if the designer wants it.
5. **Cover is three-valued for the AI (hard / soft / none) and every runtime cover change is A/B-gated.** The 2026-11-21
   result (cover added inside a defended zone made the defence worse) proves that a cover change is not monotonic. Destruction
   edits cover at runtime, so it ships behind arena switches, with `Destruction=0` replaying old seeds bit-identically.

Order of work: **P0** data only (now, on the generator lane) -> **spikes** (navmesh cost, look, determinism) -> **P1** soft
cover + doors / glass / boards -> **P2** walls (explosive holes) -> **P3** structure (collapse, enemy breaching).
*Proposal*: P1 does not start before the enemy-defence finish line the designer set for next week (2026-11-21) and
before the door runtime (R1) exists.

## 1. Design pillars

### What destruction is for in this game

| # | Pillar | The player says | Kills |
|---|---|---|---|
| D1 | **Make your own door** | "We do not go through their kill zone - we open the wall they did not cover." | buildings whose only answer is the covered entrance |
| D2 | **Deny the position** | "He is behind plaster - shoot through it. He is in that window - take the wall away." | an enemy who is safe forever because he reached cover first |
| D3 | **Material is information** | "Concrete protects. Plaster only hides. Wood is nothing." Read from the camera, learned once, true everywhere. | cover that looks the same and behaves differently |
| D4 | **Everything has a price** | Explosives are scarce, every breach is loud, noise calls the horde, your hole is also their door. | "RPG everything" as the universal answer |
| D5 | **The battlefield remembers** | The hole stays for the mission and in the save; the fight that happened here can be read from the walls. | fights that leave no trace |

### What it must never do

1. **Never strand or soft-lock.** Generator rule 5 stays absolute [V building-route-model.md rule 5]: every room, storey and
   roof is reachable without explosives. Holes only add routes. Anything that removes a route (collapse) is validated in
   advance or protected.
2. **Never be random.** Same seed + same actions = same cells broken, in the same order. No physics decides an outcome.
   The A/B harness and the night regressions depend on it [D 2026-11-21 "determinism first"].
3. **Never surprise the player unfairly.** An enemy that removes the player's cover gives a tell first (the RPG tube, the
   shout - smart-enemies pillar P5). A wall about to fail shows it (the Damaged state).
4. **Never make the AI stupid.** No unit keeps standing behind cover that is gone; no plan is built on stale geometry.
   A destruction feature that lowers the behaviour floors does not ship, whatever it looks like.
5. **Never cost a hitch.** No frame over the hitch budget from a blast; the navmesh work is time-sliced and measured before
   the feature is promised.
6. **Never become the spectacle.** No falling-building set pieces, no rubble physics the player must wait for. This is a
   tactics game seen from a tactical camera; destruction serves a decision, not a cinematic.
7. **Never erase the designed encounter for free.** The generator's stories (locked door, rubble on the stair, snow drift)
   are priced puzzles; destruction is one more priced answer, never the cheapest one.

A consequence worth stating to the designer: "fully destructible" should mean **every element has a material answer** (the
game can always tell you what would break it), not that every element breaks often. With the approved roster a concrete
bearing panel is opened by two RPG hits on the same spot and nothing less - rare by economy, which is what protects pacing.

## 2. Destruction granularity

### The options

| Criterion | (a) Whole-element states (door / window / wall run: intact -> damaged -> broken) | (b) Kit module swap per 300 cm bay | (c) Chaos Geometry Collections | (d) ADR-0004 hybrid: cell HP grid + runtime re-slice + module variants | (e) **Recommended**: precomputed cells + damage field look |
|---|---|---|---|---|---|
| Gameplay resolution | too coarse for walls (a run is 3-20 m: "the whole wall vanished") ; right for leaves and panes | one bay; coarse but readable | free-form, unpredictable | 150 cm cell x 3 bands | same as (d) |
| UE 5.8 tech cost | trivial | low: ISM instance remove / add by stable id (`RemoveInstanceById`, `AddInstanceById` exist [V InstancedStaticMeshComponent.h]) | high: no Chaos destruction module is linked today [V CodexTactics.Build.cs]; a GC per bay instead of an instance; ISM pooling for unbroken GCs exists in the engine [V GeometryCollectionISMPool*.h] but is unproven for us [A] | medium-high: world subsystem + a C++ port of the Python slicer with parity tests | medium: world subsystem; cells arrive as data, no slicer |
| Runtime perf | best | best | worst: rigid bodies, islands, memory per GC, clean-up | small (byte arrays, one wall run re-sliced per hole) | small (byte arrays, a few box instances per damaged run, one texture region write) |
| Determinism (A/B, bot runs, replay) | full | full | **fails our bar**: the outcome depends on the solver step and thread timing; two machines (main PC, worker) will not produce byte-equal event logs [A, not worth a spike] | full for the truth; debris never queried | full; the look is derived from the bitset, so even the picture replays |
| Navmesh / LOS update | per element | per bay | debris must be kept off the navmesh and the hole still carved by hand - needs (d) / (e) underneath anyway | one run's collision changes -> dirty tiles | same, and the upper bands can be made navigation-irrelevant (5.2) |
| Art pipeline against the ISM kit | leaf / pane variants only | 2-4 damaged meshes per module family per series; the Fab pack has intact modules only; pilot facade fillers are stretched (20 of 52 beyond the 10 % rule, ADR-0004 C3) so variants must stretch too | fracture every module family; fracture authoring is an editor GUI workflow - headless authoring unverified [A], and the team works headless [D 2026-11-18] | as (b), plus the cell -> module map from the kit dresser | one material function in the wall master material(s) + about 12 generic rim / rubble meshes + decals; no per-module art; independent of stretch because the mask is in the building frame |
| Interior partitions (merged mesh per storey) | n/a | **cannot swap a part of a merged mesh** - partitions would have to be re-instanced | n/a | same problem as (b) for the look | works unchanged (the mask is spatial, not per instance) |
| Main risk | wrong grain for walls | wrong grain for "walls anywhere"; partitions | cost, unpredictability, authoring | two slicers; two layers (cells, modules) to keep in step; art debt per series | the material spike may fail on the pack's materials; masked materials cost; thin-wall look at hole edges (hidden by rims) |

### Recommendation

**(e)**, which is ADR-0004's option (d) with the truth layer kept and two parts replaced:

- **Truth**: per building, per wall run, a row of precomputed **cells** (nominal 150 cm = the project's door width and half a
  kit bay) with two breakable **bands** per storey: sill 0-95 cm and body 95-245 cm (the lintel 245-300 cm never breaks on its
  own). Each band holds integer HP and a 2-bit state. Body gone = a window-like hole (sight and fire, low cover behind the
  95 cm sill). Sill + body gone = a walk-through hole. This is the builder's own sill / body / lintel vocabulary [V ADR-0004].
- **Cells come from the generator**, exported per map (`structure/<Map>.structure.json`, *proposal*): id, wall, storey,
  rectangle, `hp_class`, and precomputed flags - `passable_when_broken` (width >= 150 cm, walkable floor at one storey on both
  sides: the breach-panel candidate rules [V breach-panels.md rule 4]), `sill_protected` (an exterior cell on an upper storey
  with no balcony or roof outside: it may become a window, never a door into the air), `protected` (corners, jamb piers
  under 100 cm, carriers of a route element, validator-set), `supports[]`. The runtime needs no geometry reasoning at all.
- **Collision** (the proxy is what blocks movement, sight, bullets and navmesh [V ADR-0004 C3]): an undamaged run stays
  exactly as built (legacy maps and untouched buildings bit-identical). On its first damage a run's proxy is replaced by its
  precomputed cell boxes; breaking a band removes boxes. Door leaves, panes, boards and barricades stay actors with states
  (option (a) is right for them; BUILDINGS_TECH_SPEC R1 / R2 already say mesh swap).
- **Look**: the damage field (section 5.9) + generic rims, decals, pooled rubble piles and Niagara dust. Debris is cosmetic,
  has no collision, is never queried. Chaos stays an optional cosmetic layer for a later polish pass, never for an outcome.

Why not simply (d) as written: the slicer port doubles a fragile piece of code for no gameplay gain, and the authored-variant
look fails on the merged partitions - which are plaster, the class that will be destroyed most.

## 3. Material and structure model

### 3.1 Classes (what exists)

[V `Content/Data/LevelGen/structure_classes.json` + `structureClasses.ts` on `claude/building-generator`] The generator
already writes `structure` (`load_bearing | partition | annex | facade`) and `material` (`concrete_panel | brick | plaster |
wood | sheet_metal`) on every wall, partition and opening, by archetype and wall role (perimeter / core / cross / inner).
A panel block: perimeter, stair core and the walls between flats are load-bearing concrete; room walls are plaster. Halls and
warehouses: sheet-metal facade on a concrete core. Sheds: wood. Free walls: brick; fences: sheet metal. The `classes` table
(10 combinations) has `hp_class: "TBD"` everywhere.

**One P0 defect to fix in the data** [V structureClasses.ts lines 66-73]: an opening takes its host wall's material, so a door
in a concrete wall is "concrete". For destruction an opening needs its **own** material: leaf `wood | steel`, pane `glass`,
boards `wood`, bars `steel`, shutter / gate `sheet_metal`; the host's class describes only the frame. This adds `glass` and
`steel` to the material list (*proposal*).

### 3.2 Two independent properties per material

A wall answers two different questions, and mixing them is what makes destruction models unreadable:

- **Stop** - does a round pass through, and how much of it? Instant, no state. This is soft vs hard cover.
- **Toughness** - what does it take to remove the wall? HP per cell band plus a minimum class. This is destruction.

Weapon classes reuse the armour ladder that already exists [V `weapons_tuning.json`, `ArmorRules.h`]: pistol Light, shotgun
Light, rifle (M16) Medium, sniper rifle Heavy, RPG-7 Vehicle. For walls, explosives get their own `breach_class` (*proposal*:
grenade Medium, mine / tripwire / fuel barrel Heavy, RPG-7 and the mortar round Vehicle) - against bodies every blast counts
as Vehicle today [V Explosion.h], which would let a grenade open concrete if reused as is.

### 3.3 Stop: soft cover and hard cover

Share of a round's damage the material stops (*proposal*; "-" = the round does not pass at all):

| Material | Pistol | Shotgun | Rifle | Sniper | Reads as |
|---|---|---|---|---|---|
| glass | 0 | 0 | 0 | 0 | nothing |
| wood (leaf, boards, shed, crate) | 0.5 | 0.5 | 0.25 | 0.1 | concealment |
| plaster (room partitions) | 0.8 | 0.8 | 0.5 | 0.2 | soft cover |
| sheet_metal (hall cladding, fences) | - | - | 0.4 | 0.15 | soft cover against rifles |
| brick | - | - | - | 0.7 | hard cover (a sniper can still reach through one wall) |
| concrete_panel | - | - | - | - | hard cover, always |

How it meets the existing fire model. Fire is a line trace on `ECC_Visibility` plus a hit-chance roll, and cover absorbs a
share of a frontal hit - 0.9 behind high cover, 0.35 crouched behind low cover [V `CoverRules.h`, `OperativeCharacterCombat.cpp`].
So the rule is three small changes, not a ballistics simulation:

1. **A man in a cover slot**: `absorb' = absorb x stop(material, weapon)`. Concrete: 0.9 as today. Plaster against a rifle:
   0.45. The wall he hugs is known from the slot [V `FCoverSlot.WallComponent`], its material from the structure data.
2. **A man behind a soft wall with no line of sight**: the shot line is "soft-blocked" instead of blocked. Firing is allowed
   only as the existing blind fire at a ghost / last known position [V `SightRules::BlindFireHitChance`, `EnemyGhostActor`],
   with its hit penalties, and damage x (1 - stop). Nobody gains sight through a wall.
3. **At most one soft cell on the line** in the first release. Two partitions = blocked. Simple to read, cheap to trace.

The same rules apply to enemies (smart-enemies pillar P3: the same body, the same rules), limited by their honest knowledge:
an enemy may fire through plaster only at a position he saw or heard within his contact memory.

What this gives in a khrushchevka without a single hole: inside a flat you are hidden but not protected; flats are separated by
hard walls; the stair core is a hard-walled funnel; a wooden door is not cover. That is the designer's "structure matters"
delivered by a table.

### 3.4 Toughness: what it takes (design targets first, numbers second)

The targets are the design; the numbers only have to reproduce them and live in `structure_classes.json` (tuned in data).

| `structure/material` | Bullets | Grenade (Medium) | Mine / tripwire as a charge (Heavy) | RPG-7 direct hit (Vehicle) | Seed HP per band |
|---|---|---|---|---|---|
| pane / glass | first hit | breaks in the radius | breaks | breaks | 1 |
| leaf, boards, shed / wood | about 10 rifle rounds, 2 shotgun shells at the door, 15 pistol rounds | breaks | breaks | breaks | 60 |
| partition / plaster | pass through (3.3); do not break it (fork 1) | door-sized hole next to the blast | hole | hole + neighbours | 240 |
| facade, partition, annex / sheet_metal | pass through; cosmetic holes only | Damaged; a second grenade opens it | hole | hole | 400 |
| partition, annex / brick (free walls, infill) | nothing | nothing (scorch) | door-sized hole | window-sized hole | 800 |
| load_bearing / brick | nothing | nothing | Damaged; a second charge opens it | Damaged; a second hit opens it | 1600 |
| load_bearing / concrete_panel | nothing | nothing | nothing | Damaged; a second hit on the same cell opens it | 2400 |

Seed structural damage (*proposal*): pistol 4, rifle 6, sniper 20 per round, shotgun 40 per shell at breach distance; grenade
300, mine / tripwire 900, RPG-7 1200 on the struck cell and 300 on its neighbours. All integer; a class below the material's
gate does zero. The invariants to keep when tuning: the order of the rows, "one more hit" after Damaged, and that the tool
column reads the same on every map.

**Bullets vs explosives, stated once.** Bullets *pass* soft materials and *break* only glass and wood. Explosives are the
only thing that opens plaster, metal, brick and concrete. Reasons: (1) a long firefight must not grind the map's cover into
nothing - that would silently rewrite every encounter and every AI position; (2) bullet erosion of walls means navmesh and
position churn during fire, the most expensive moment; (3) soft cover already gives bullets their tactical voice. Bullet
erosion of plaster can be switched on later per `hp_class` (`bullet_erodes`) if playtests ask for it (fork 1).

**Which rounds reach a wall** (deterministic, no new random draws):
- the share a cover absorbs from a hit goes to the cell between shooter and target (glass and wood only, by the gate);
- a missed round already has an end point drawn from the seeded Combat stream [V commit 897c5828 on `ai/arena-determinism`:
  "miss offset"] - the first cell on that line takes the round [A: that the miss line is traced to an end point];
- a deliberate shot: the Ctrl + click targeted shot gets a new kind for a leaf, a pane or boards
  [V `TargetedShotRules.h` has Enemy / Barrel / Mine / Crate / TrappedObject].

**Explosions**: `Explosions::Detonate` is the single entry of every blast [V Explosion.h]. It gets a structure pass: cells
whose band centre lies in the breach radius, falloff by distance, the class gate, and the existing wall rule so that a cell
behind another standing cell takes x0.25 [V `explosions.wall_multiplier`]. Cells are processed in id order. A charge within
100 cm of a wall counts as contact with the nearest cell (sill and body). An RPG strikes at its impact height, so it makes a
window in brick; a charge at the wall's foot makes a door. The blast preview can outline exactly which cells will fail,
because the rule is pure.

### 3.5 Cover degradation states

Per cell band: **Intact** (HP above half) -> **Damaged** (HP at or under half) -> **Broken** (0).

- *Intact*: blocks movement, sight, rounds by 3.3, blasts.
- *Damaged*: **no rule changes** - same stop, same sight. It is a tell (cracks, exposed rebar, dust falling) and an AI
  trigger (5.5). "One more hit and it is gone" must be readable from the tactical camera.
- *Broken*: the band is removed. Body = hole for sight and fire with a 95 cm sill (low cover by the existing classifier,
  under 130 cm [V CoverTraceRules.h]). Sill + body = passage if `passable_when_broken`.

I argue against the tempting alternative "Damaged cover protects less". It makes the stop table depend on a hidden state,
doubles what the player must read, and gives the AI a continuous quantity to mis-estimate. Material decides protection;
state decides existence.

**Cover objects** (crates, barricades, vehicles, fences): the same table by material, one cell per object, whole-object
states. The barricade already has 200 HP [V BarricadeActor.h]; a wooden crate becomes 60-120, a concrete block is never
broken by bullets. They go **last** in P2 with their own A/B, because street cover is what the enemy plays are built on.

## 4. Structural integrity

| Level | Rule | Player value | Cost and risk |
|---|---|---|---|
| **L0 no collapse** | load-bearing only means tougher; a hole is a hole; cells that carry a route element (ladder, fire escape, a balcony on the key route) are `protected` | all of D1-D5 | none beyond the data |
| **L1a hanging elements** | when the cell an element hangs on breaks, the balcony / fire escape / ladder / parapet section is removed with its nav links | denying a height or an escape | each removes a route: validator V4 re-checks or protects; the enemy's "never without an exit" rule must re-evaluate |
| **L1b local floor panel** | a load-bearing run **fails** when at least half of its cells have a broken band (minimum 2); a floor panel whose standing supports drop under `needs` falls **once**: a floor hole, a rubble heap, a drop edge. No chain | a rare, earned event: dropping a floor under a garrison | validator V2 must replay every single failure on every generated map; people, loot and bodies on and under the panel need rules; navmesh change on two storeys at once |
| L2 progressive / storey | failed loads move to neighbours | spectacle | chains are unpredictable for the player, unbounded for the validator, a soft-lock generator. **Cut.** |
| L3 whole building | - | spectacle | **Cut.** |

**Recommendation.** Ship the wall phase (P2) at **L0**. Decide L1 afterwards from play (fork 2). The key rule either way:
**a hole never collapses anything - only a lost run does.** A door-sized breach in a bearing wall is the core player verb
(D1) and must never be punished with an unreadable collapse. With seed numbers a 6 m concrete run fails after four RPG rounds
on two cells - an event the player has to mean.

**Stairs and goal floors**: stair flights, the floor under a mission goal, a survivor or key loot are always protected in
the first release (ADR-0004 questions 7-8, same lean). The enemy's fall-back-upstairs play and the reachability rule both
stand on the stair core.

**What the generator precomputes** (all in P0, runtime ignores it until P3 - data ships before code):
- the load-path graph of ADR-0004 (`supports`, `supported_by`, `needs`), acyclic, every element reaching the ground;
- per load-bearing run `fail_share` (0.5) and its cell list; per floor panel the **outcome** as existing layout concepts
  (floor hole rectangle, rubble heap, drop edge, blocked flight);
- `protected` flags from the validator: V1 (rule 5 with every wall intact - holes only add edges), V2 (replay each single
  failure, every objective room / exit / key-route top still reachable), V3 (graph sanity), V4 (route carriers), V5
  (byte-equal on 5 seeds, `structure_hash`);
- the per-cell flags of section 2.

Two additions to ADR-0004's validator: **V6** every cell's flags are derived, never hand-set, and are part of the
determinism check; **V7** a random 10 % of breakable cells pre-broken by seed must leave the reachability result unchanged
(a cheap property test for "holes only add").

## 5. Systems impact

### 5.1 One event, many consumers

A world subsystem owns the cell state; pure rules (`StructureRules`: tables, damage resolution, stop, state transitions,
collapse evaluation) are unit-tested without a world, as everywhere in the project. Every change raises one event on the
event bus [V `UCodexEventBus`]: `StructureChanged { building, bounds, kind, cells, time }` with
`kind = CoverDamaged | SightOpened | PassageOpened | ElementLost`. Consumers below subscribe; nobody polls.

### 5.2 Navmesh and pathing

Facts: Recast, `RuntimeGeneration=Dynamic`, no tile or cell overrides [V DefaultEngine.ini]; generated maps are baked.
Engine defaults in force: `bDoFullyAsyncNavDataGathering = false` (collision is gathered on the game thread),
`MaxSimultaneousTileGenerationJobsCount = 1024` [V RecastNavMesh.cpp 5.8]. A Recast tile is a 2D column, so a hole on storey 3
re-rasterises every storey in that tile. The cost on a multi-storey kit block is **unmeasured**. One existing data point:
three cover blocks placed on a street at round start cut the navmesh and the round waited 0.3 s [V T08-slice2 report] -
outdoors, single storey, not a budget proof.

| Strategy | What | For | Against |
|---|---|---|---|
| N1 geometry change, async tile rebuild | what gates and moved barricades already do | no new mechanism; ADR-0001 keeps one truth | unmeasured in multi-storey tiles; game-thread gathering by default |
| N2 only the sill band is navigation-relevant | after the first split, body cells have `CanEverAffectNavigation = false`; only a walk-through hole dirties a tile | most holes (windows, loopholes) cost the navmesh nothing | the first split of a run still swaps collision (one dirty area) [A: can be suppressed by a navigation lock] |
| N3 modifier-only (`DynamicModifiersOnly` exists [V NavigationData.h]) | navmesh pre-built through weak walls, blocked by a Null-area modifier until broken | cheapest update, no re-rasterisation | only sane for a handful of *marked* cells per building - not for 781 walls; conflicts with "any wall" |
| N4 pre-baked tile variants | - | - | contradicts "navmesh is the runtime truth" (ADR-0001); combinatorial. **Rejected.** |

**Recommendation**: N1 + N2, with coalescing (one dirty area per blast per building; at most a few structural updates in
flight), `bDoFullyAsyncNavDataGathering` tried in the spike, smaller tiles on generated maps if needed, and N3 kept as the
fallback for marked breach panels only. Budget as in ADR-0004 C7 (2 ms per frame, path through the hole under 1 s) - to be
**measured by the spike before P2 is promised**.

Other pathing rules:
- "Hole open" is delivered to gameplay at a **fixed game-time delay** after the blast (1.0 s, *proposal*), not when the
  worker thread happens to finish - otherwise two runs of one seed diverge. If the tiles are late, gameplay waits and logs
  `nav_late` (a budget failure, visible in the smoke).
- A walk-through hole registers as a doorway (`UDoorwaySubsystem::RegisterDoorway` exists [V]; ADR-0004 states it works at
  runtime).
- A body-band hole is sight and fire only in the first release - no vault link through a blown hole (cut, section 8).
- Order previews under the tactical pause wait for the tile (ADR-0004 risk, unchanged).
- Rubble at a hole's foot is at most 35 cm, never blocks, gives no cover [V breach-panels.md F3].

### 5.3 Cover system

Cover is found by live traces [V CoverTraceRules.h], so geometry changes are followed for free. To add: the slot's material
(for `absorb'`), the "soft cover" classification shown to the player and the AI, and an occupant check on the event - a unit
whose covering cell broke gets `cover lost` and reacts within the fighter brain (acceptance: nobody in a dead cover slot
for more than 2 s). `CoverWrapRules` walks a component's footprint [V CoverTypes.h]; split runs must keep presenting one
outline per wall, which is a test case for the collision swap, not a design question.

### 5.4 Sight and line of fire

Live traces need nothing. Cached consumers refresh on the event, inside the bounds only: vision cone meshes
(`RequestRebuild()` exists [V VisionConeComponent.h]; per ADR-0004 only smoke commands call it today); the firing-position cache of T-05,
which is already specified as "re-validated on a geometry event only" [V smart-enemies-roadmap.md T-05, on a branch, not in
main]; the baked tactical map, which is TS-side analysis and is **not** recomputed at runtime - its items inside the bounds
are marked dirty and re-validated by trace or dropped. Damaged cells do not change sight. Dust is cosmetic and never blocks
sight: smoke is not on the weapons roster and must not arrive through the back door.

### 5.5 AI: how enemies and squad mates reason about destructible cover

**The 2026-11-21 evidence.** In the paired A/B on Crossroads (about 150 rounds per arm), low-cover blocks added inside the
defended goal zone moved defender wins from 51 % to 33 % and captures from 13 % to 29 %. The 5-seed sanity report had already
warned why this could happen: the crossfire picker took the new blocks as the hammers' firing positions, pulling the defence
into the goal instead of onto the approach [V T08-slice2 report, note 1; the cause is a hypothesis, not proven]. Cover inside
a goal also serves whoever reaches it. Lessons that bind this feature:

1. **Cover is not monotonic.** More cover can be worse, less cover can be better. Nothing may assume a cover change helps.
2. **Destruction is a runtime cover editor.** It must enter the AI through the same position scoring as generated cover,
   with explicit terms - never through a side channel - and it must be measurable in the same harness.
3. **Judge by behaviour floors, report the win rate.** A shift in defender wins is a design outcome for the designer to
   accept or reject; a broken floor (idle in the open, a man behind dead cover, bunching) is a bug.

**Static knowledge (P1, no destruction needed).** Every firing position and cover slot knows its covering material from the
generator data. The position score F2 [V smart-enemies.md] gets a hardness term:

```
protect'(p)  = CoverQualityAgainst(p, threat) * hard(p, W)              W = best weapon class known on the threat side (unknown = rifle)
hard(p, W)   = stop(material of p's covering cell, W)                    1 for a material W cannot pass
S'(p)        = S(p with protect') * (1 - w_fail * failing(p)) * (1 - w_breach * breach_risk(p))
failing      = 1 when the covering cell is Damaged
breach_risk  = 1 when the threat side is known to carry an explosive whose class opens p's cell AND p has fired for > t_known s
```

So an enemy with time takes the concrete corner over the plaster one, uses soft cover to hide before contact (an ambush
position needs concealment, not protection) and does not hold a plaster wall under return fire. The kill-zone score F8's
`cover_for_victim` counts only cover that protects against the defenders' weapons - a wooden fence in the kill zone is not
cover for the victim, which makes more places valid ambush sites. The player's autonomous squad mates use the same term in
their under-fire cover search [V `UnderFireRules` uses `CoverQualityAgainst`].

**Reactions to the event (P2).** By layer, respecting the commitment rule (a play is re-chosen on events, not every tick):
- *Fighter*: `cover lost` -> relocate now to the next position of his role; `cover failing` (Damaged) -> relocate when a
  position scoring at least 0.8 of his current one exists; `line opened` (a hole now sees his position) -> re-validate.
  This is the player-facing loop of pillar D2: hammer his cover and he moves.
- *Squad commander*: re-plan only when a role's position is lost with no replacement inside the role, when the kill zone's
  victim cover changed materially, or when a **walk-through hole opens into a garrisoned building** - that is treated as
  "the door is breached" and triggers the existing fall-back-upstairs play. Re-plans are rate-limited to avoid thrash.
- *Exits*: the "never without an exit" rule (R-B3) re-evaluates when a hole adds an exit or a hanging element is lost.

**Breach decisions (P3).** First release: enemies never plan to break a wall. Their existing RPG strike on a cover cluster
or a window the player fires from [V smart-enemies.md DS-18] simply gains the structure pass - the player's cover really goes
away, with the tube / shout tell that already exists. Deliberate breaching arrives with the enter-and-clear play: only the
RPG gunner, only when `gain = T(covered entry) - T(through the hole)` passes a threshold and the entry is defended, never to
create the only route, always after a tell, inside an encounter budget. Squad mates never spend an explosive without an
order - explosives are the player's scarce resource.

**Monsters.** A hole is plain navmesh, so the horde uses it: the player's breach is also their door (pillar D4). A horde
breaks wooden doors [D 2026-11-19, OQ-14]: that is melee structural damage against a wood leaf, P1. Whether a Brute walks
through plaster is fork 4.

**How it is tested.** Arena switches in the style of `ZoneCover` [V T08-slice2]: `SoftCover=0|1`, `Destruction=0|1`, and a
scripted blast (`time, place, kind`) so a scenario breaks the same cell on every seed. Paired A/B of at least 150 rounds per
arm before either switch becomes default-on, with the position choices in the decision trace.

### 5.6 Noise

Destruction raises the existing noise events [V `ESquadNoise`, Explosion.h `bSquadNoise`]: glass 10 m, kick 15 m, shotgun
breach 30 m, gunfire 60 m are already priced [V opening-states.md, smart-enemies.md]. A blast keeps its own noise; a wall
breaking adds nothing separate; a collapse (P3) is the loudest event in the game. One inconsistency to fix in the cost
table: `N_charge` is 25 m while gunfire is 60 m - a charge quieter than a rifle [V opening-states.md F1]. Quiet destruction
(prying boards, cutting a lock) is a later verb, not part of this feature (fork 6).

### 5.7 Save and load

Saves have optional sections per version, currently v3 [V SaveGameRules.h], and are made outside fights (ADR-0004). A new optional
section: per building the `structure_hash`, a 2-bit state per cell band, the opening states, failed panels - under 100 KB
per map. On load the states are applied before the first AI tick and the load waits for the navmesh. A hash mismatch (the
map was regenerated) drops the section for that building and logs it; the building loads intact. Destruction is permanent
for the mission (fork 5).

### 5.8 Determinism

- Integer HP and integer structural damage; cells processed in id order; blasts of one frame ordered by (time, source id).
- **No random draw is added.** With destruction off, old seeds replay bit-identically - this is an acceptance test, because
  the seeded Combat / AI / Cosmetic streams [V commit 897c5828] must keep their draw order.
- Gameplay never reads physics, debris or thread timing; "hole open" has a fixed game-time delay (5.2).
- Every change is appended to the round's event log; the replay page redraws from the events; the TS mirror of the pure
  rules lets the Training Ground show what a charge would do before the map is built.
- Chaos is absent from every outcome, so the main-PC / worker difference cannot appear.

### 5.9 Look and art pipeline against the kit

Facts: kit visuals are one actor per building holding one `InstancedStaticMeshComponent` per mesh / material, `NoCollision`,
over hidden grey-box proxies that own collision, navigation and tags [V `kit_dress.py` on `claude/bgen-test`]. So the
instancing precondition named in ADR-0004 is now met on that branch. Interior partitions are merged meshes per storey
(lead's brief).

**Damage field** (*proposal*, needs the spike): per building a small texture in the building's local frame, 50 cm per texel
with texel centres on the 50 cm wall grid, one slice per storey, channels = sill / body state. The subsystem writes one
region per change. The wall master material samples it by local position (the building's origin and rotation arrive as
custom primitive data on the building's components) and masks the surface out where the band is broken, blends cracks where
it is Damaged. Around every hole a generic **rim** mesh per material x shape (window hole, door hole: about 12 meshes for
five materials) hides the thin-shell edge and shows the wall's section (rebar in concrete, brick courses, lath in plaster).
Plus decals, pooled rubble piles (static, no collision), Niagara dust.

For: authored once per material, not per module and not per series (a stalinka or a dormitory template costs nothing);
unaffected by module stretch; works on merged partitions; cannot disagree with the truth. Against: the pack's materials must
call our function (maintenance when the pack updates) [A: feasible]; masked materials cost more than opaque [A: acceptable,
measure]; corners share a texel (corner cells are protected, so harmless); less bespoke silhouettes than hand-made variants.
Fallback if the spike fails: ADR-0004's authored variants, limited to the block series, with partitions re-instanced.

**UI feedback** (all from the same data):
- the opening-state overlay on a held key [V opening-states.md UI] gains wall classes: hard / soft / "opens with ...";
- the cover indicator gets three faces: hard cover, soft cover (concealment), failing;
- aiming a grenade, a charge or the RPG outlines the cells that will break - exact, because the rule is pure;
- per-material hit sound and dust; cracks at Damaged; the minimap redraws a breached wall.

### 5.10 Tactical pause and the turn grid

Real time is the default; the pause slows the world to x0.02; the turn grid treats buildings as closed cells
[V ADR-0004 "Time model"]. Destruction is a real-time feature. Under the pause, queued orders through a fresh hole wait for
the "hole open" event. The turn grid is not changed by this feature.

## 6. Production plan

Sizes for an agent, as in the enemy roadmap: LIGHT under half a day, MEDIUM 1-2 days, HEAVY 3-5 days. Totals are rough
(plus or minus half) and exclude review. Every phase keeps `LevelGenBuildingsSmoke` at or above 95 %, the ADR-0001 drift
gate and `HitchProbeSmoke`.

| Phase | Content | Acceptance (automation tests `CodexTactics.Structure.*`, smokes) | Effort | Blocked on the designer |
|---|---|---|---|---|
| **P0 data only** (now, generator lane; partly done) | done: `structure` / `material` per wall and opening, window `vaultable` / `sill_cm` / `width_cm` [V]. To do: own material for openings; cells + flags export; `supports` / floor panels / outcomes; seed `hp_classes`; validator V1-V7; Training Ground overlay by class; C++ loader + pure `StructureRules` with no runtime effect | TS units; byte-equal layouts on 5 seeds incl. structure fields; legacy `/1` builds bit-identical; C++ and TS resolve the same fixture to the same table; V7 property test | 3 MEDIUM, about 4-6 agent-days | nothing (numbers are seeds) |
| **Spikes** (parallel, worker) | S-NAV: one walk-through hole on a 5-storey kit block and on X6 - N1 vs N1+N2 vs N3, async gathering on / off; S-LOOK: damage field on kit modules and a merged partition; S-DET: one scripted blast, two runs, one seed | a numbers table with go / no-go per strategy against ADR-0004 C7; one screenshot pair; byte-equal event logs | 3 MEDIUM | nothing |
| **P1 soft cover + openings** | stop table in the fire model (3.3); material on cover slots; AI hardness term; cover indicator; door leaf (R1: open / close / kick / shotgun) and pane (R2) and boards with HP; explosions against openings (unlocks story 3d "barricaded door"); horde vs wooden doors; the event + consumers; save section; arena switch `SoftCover` | units `Stop.*`, `Cover.Hardness.*`, `Opening.*`; `SoftCoverSmoke` (rifle through plaster hurts by the table, through concrete 0, same seed same damage); `DoorBreachSmoke`; `GlassSmoke`; `StructureEventSmoke` (cones rebuilt, nobody in a dead slot over 2 s); save round trip; `SoftCover=0` bit-identical replay; A/B report 150 rounds per arm | 3 HEAVY + 3 MEDIUM, about 3 weeks | forks 1, 5; the kick clip; R1 scheduled |
| **P2 walls** | cell state subsystem; `Detonate` structure pass; lazy split; holes into navmesh (per S-NAV); damage field look, rims, rubble, dust; blast preview; tactical invalidation; fighter and commander reactions; enemy RPG side effect; cover objects last; arena switch `Destruction` + scripted blast | units `Damage.*`, `Cells.*`; `WallBreachSmoke` (grenade on plaster: walkable under 1 s; RPG on brick: window; RPG on concrete: Damaged, second: hole); `BreachDeterminismSmoke` (5 seeds x 2 runs: equal logs and bitsets); `BreachNavBudgetSmoke` on a 5-storey block; reachability with 10 % cells pre-broken; `Destruction=0` bit-identical; A/B report | 5 HEAVY + 4 MEDIUM, about 5-6 weeks | forks 3, 4, 7; spikes green |
| **P3 structure** | supports graph at runtime; hanging elements; local floor panel; falls and the rules for people / loot on a panel; validator V2 proven on generated maps; deliberate enemy breaching (with the enter-and-clear play); breach panels as marked weak cells (generator S4) | `BuildingCollapseSmoke`; no soft-lock on 5 seeds x random single failures; `PmcBreachSmoke`; frame budget held | 4 HEAVY + 3 MEDIUM, about 4-5 weeks | forks 2, 4, 8 |

Sequencing against the rest of the studio: P0 rides the generator work now and costs the critical path nothing. The spikes
are worker jobs. P1 shares its prerequisites (door runtime, glass, hearing) with the enemy roadmap's Phase 5 (buildings) and
should land with it. P2 follows the first half of Phase 5. At today's lane capacity walls that break in the game are a
matter of a couple of months, not weeks - say so to the designer rather than promise a date before S-NAV has numbers.

## 7. Designer forks

Only decisions that change what the player experiences. Technical choices (frame budget, look method, navmesh strategy)
are mine and the lead's and are not listed.

| # | Decision | Options | Recommendation |
|---|---|---|---|
| 1 | What do **bullets** do to walls? | (a) nothing except glass and wooden doors; (b) also pass through soft materials - plaster, wood, sheet metal hurt the man behind (soft vs hard cover); (c) also wear plaster / brick down over time | **(b)**. (c) can be switched on per material after the wall phase if play asks for it |
| 2 | **Load-bearing walls and collapse** | (a) indestructible; (b) can be holed by the heaviest means, never collapse; (c) as (b), and when most of a bearing wall is gone the piece of floor above it falls once (hole, rubble, people fall), no chain | **(b)** for the first wall release; decide (c) after playing it |
| 3 | **The wall-breaking tool and its economy** | (a) no new item: grenade, RPG-7 and the existing mine placed at a wall and set off act as the charge; (b) a dedicated breaching charge joins the roster. Either way: how many explosives does a squad carry and find per mission? | **(a)** now - the roster stays as decided on 11-08; revisit (b) after P2. The economy number is yours and blocks P2 tuning |
| 4 | **Can enemies destroy?** | (a) never; (b) only as a side effect of what they already do - the PMC RPG strike really removes your cover, a horde breaks wooden doors; (c) PMC also blow a wall on purpose to enter or flank. And: does a Brute walk through plaster and wood? | **(b)** with the wall phase, **(c)** later with the storming play and a tell. Brute: later, as its own decision |
| 5 | **Permanence** | holes stay for the whole mission and are saved / reset on load | stay and are saved |
| 6 | **Noise** | every destruction is loud and calls monsters; or quiet ways exist (pry boards, cut a lock) | loud only for now; quiet ways are a later verb |
| 7 | **Hole shapes** | fixed and readable (a window-sized hole, a door-sized hole, two cells = a wide breach) / any shape | fixed |
| 8 | **Protected things** | may a staircase, or the floor under a goal, a survivor or key loot, ever be destroyed? | no - always protected in the first release |
| 9 | **Cover objects on the street** (crates, fences, barricades) | destructible by the same material table / only buildings | yes by the same table, but last and behind its own A/B: street cover is what the enemy plays stand on |

Confirmations rather than forks (I assume yes unless told otherwise): rule 5 stays - every storey reachable without
explosives; marked breach panels stay as the visible weak spots and simply become low-HP cells of this system.

## 8. Risks, and what we explicitly cut

### Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Navmesh rebuild in multi-storey tiles is too slow, or hitches on game-thread gathering | P2 slips or holes feel late | S-NAV before any promise; N2 removes most holes from the navmesh entirely; N3 for marked panels; coalescing |
| AI regresses when cover changes at runtime (the 11-21 A/B pattern) | worse defence, unreadable fights | switches off by default; paired A/B; behaviour floors as the gate; `cover lost` reactions tested by a scripted blast |
| Determinism leaks through async navmesh timing | A/B noise, flaky smokes | fixed game-time "hole open"; `nav_late` logged as a failure; no physics in outcomes |
| The damage field fails on the pack's materials or costs too much | no cheap look | spike first; fallback = authored variants for the block series only |
| Scope pressure against the week plan and the defence finish line | both slip | P0 and spikes only until the finish line; P1 tied to enemy Phase 5 |
| Two representations drift (cells vs proxies vs look) | holes you can see but not pass, or pass but not see | cells are the only truth; collision and look are both derived from the bitset; a smoke compares them by trace |
| Soft cover makes interiors too lethal | frustration, "shot through the wall from nowhere" | blind-fire penalties already apply; enemies fire through walls only at honest last-known positions; one soft cell per line; the cover indicator says "concealment" |
| Regenerated maps invalidate saves | lost building state | `structure_hash`; the section drops per building, never the whole save |
| Stop / toughness numbers are guesses | wrong feel | every number in JSON; the invariants (row order, "one more hit") are the design, the values are tuning |
| Validator V2 (replay every single failure) is slow or finds many unprotectable buildings | P3 slips | only P3 needs it; P2 ships at L0 where holes only add edges |

### Explicitly cut

- Chaos / physics deciding any gameplay outcome; debris with collision, debris as cover or obstacle.
- Free-form holes; holes smaller than a cell band; destruction of the lintel band on its own.
- Progressive, storey and whole-building collapse; destructible stairs; roofs and slabs opened from above (the mortar).
- Bullet erosion of brick and concrete; "Damaged cover protects less".
- Vaulting through a blown window-sized hole (sight and fire only in the first release).
- Dust or smoke as concealment; fire spreading through buildings.
- Rebuilding at runtime (re-barricading, boarding up) by either side.
- Any new weapon. A breaching charge is fork 3, not an assumption.
- Destruction rules specific to the turn grid; multiplayer replication (single player, ADR-0004 C9).
- Recomputing the tactical map analysis at runtime (items are invalidated and re-validated, not regenerated).

## Appendix A. What was read, and what was not verified

Read: `CLAUDE.md`; ADR-0004; `STORY_VARIANTS.md`; `smart-enemies.md` (sections 1-8, formulas, edge cases) and
`smart-enemies-roadmap.md`; on `claude/building-generator`: `structure_classes.json`, `structureClasses.ts`,
`BGEN_KIT_PLANS_2026-11-21.md`, `BUILDINGS_TECH_SPEC.md`, `breach-panels.md`, `opening-states.md`,
`building-route-model.md` (rules); on `claude/bgen-test`: the header of `kit_dress.py`; in main: `weapons_tuning.json`,
`ArmorRules.h`, `ExplosionRules.h`, `Explosion.h`, `CoverTypes.h`, `CoverTraceRules.h`, `CoverRules.h`, `SquadFireRules.h`,
`WeaponBallisticsRules.h`, `TargetedShotRules.h`, `BarricadeActor.h`, `TacticalMapSubsystem.h`, `SaveGameRules.h`,
`DefaultEngine.ini` (navigation), `CodexTactics.Build.cs`; the T08-slice2 report (commit d2d1c93a); commit 897c5828;
UE 5.8 engine headers for ISM instance ids, navmesh generation modes and defaults, the Geometry Collection ISM pool.

Not verified, stated as assumptions above: every navmesh cost; that a missed round's line is traced to an end point; that
the Fab pack's materials can call a project material function; masked-material cost on the kit; headless fracture authoring;
Chaos non-determinism in our harness (not spiked on purpose); that a mine can be placed against a wall and set off as a
charge by the player today; the cause of the 11-21 A/B result (a hypothesis from the 5-seed report, the A/B numbers
themselves come from the lead's brief).

## Appendix B. Proposed amendments to ADR-0004

1. Add the **stop** property and the soft / hard cover rule; move it into a new P1 with doors and glass.
2. Replace "re-slice with the builder's rules ported to C++" by **generator-exported cells**; drop the slicer parity work.
3. Two breakable bands (sill, body); the lintel is not independently breakable.
4. Replace "module variants per bay" by the **damage field** as the default look, variants as fallback; note that
   instancing is already done on `claude/bgen-test` and that merged partitions rule out per-instance swaps.
5. Collapse: add the rule "a hole never collapses; a run fails at `fail_share`"; ship walls at L0 with protected carriers.
6. Openings get their own material (`glass`, `steel` added); fix `annotateStructure`.
7. Explosives get a wall `breach_class` separate from the armour class.
8. Navmesh: add strategy N2 (only the sill band is navigation-relevant) and the fixed-delay "hole open" rule.
9. Add the AI terms (`hard`, `failing`, `breach_risk`), the arena switches and the bit-identical-when-off acceptance.
10. Validator: add V6 (derived flags in the determinism check) and V7 (pre-broken cells property test).
