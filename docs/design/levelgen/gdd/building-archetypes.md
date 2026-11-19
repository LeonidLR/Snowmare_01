# Building Archetypes + Vertical Circulation

> **Status**: Draft v2 (studio discussion; reworked after `/design-review` + the 4-role studio review, 2026-11-18)
> **Author**: Claude (building generator owner, designer 2026-11-18)
> **Last Updated**: 2026-11-18
> **Implements Pillar**: 1 Non-linear, never impossible; 4 The setting tells it; 5 Gameplay first, then the kit
> (see [systems-index.md](systems-index.md))
> **Source**: BUILDINGS_GDD.md sections 1 (share), 2, 3 (commit 9210a435); grammar + rules in
> `docs/design/levelgen/BUILDING_GENERATOR.md` sections 3-4, module contract section 7.

## Overview

A building archetype is the generator's template for one kind of Soviet Arctic building: how many storeys it has, how its
rooms are laid out, how its storeys are connected (stair core, fire escape, balcony, ladder, hatch) and which stories it can
carry. Archetypes are the foundation layer of the building generator: opening states, the route model, breach panels and
building stories all act on the shell, rooms and circulation that an archetype produces. The generator (Training Ground TS,
`Tools/TrainingGround/src/levelgen/`) emits every element into the layout; the Python builder turns the named modules into
actors; the art kit replaces the greybox modules by name. **This table is authoritative for storeys and layout**; the older
BUILDING_GENERATOR.md section 3.1 table is updated to match it when S1 lands (until then: offices 1-2 storeys here, 2-3
there - this document wins).

## Player Fantasy

The squad reads a building from the street the way a real assault team would: a panel block has a stair core and balconies,
a hospital has two wings and a fire escape per wing, an industrial hall has one tall volume with a mezzanine and a roof that
is reached from outside. Each kind of building suggests its own plan of attack before the squad goes in, and the plan is
right - the building behaves the way it looks. Going up is never "one ramp and you are on top": there is a core, a landing,
a balcony, a ladder, and each one is a tactical position.

## Detailed Rules

### 1. Archetype catalogue

| Archetype | Storeys | Min footprint (*proposal*) | Layout | Typical stories | State |
|---|---|---|---|---|---|
| **hall** (industrial) | 1 tall (9 m) + mezzanine at +300 | 12 x 18 m | hall volume, mezzanine along the core wall | locked gate, roof only outside, breach in sheet wall | *done* (P1a) |
| **warehouse** | as hall | 12 x 18 m | hall + racks + office box | shutter closed, office on the mezzanine | *done* (P1a) |
| **office** | 1-2 (*proposal*: 2 when >= 8 m long) | 8 x 10 m | corridor spine + rooms | locked wing, dead-end ambush room | |
| **block** (residential, panel) | 2-3 | 10 x 18 m | stair core + flats (2-3 rooms each) per storey, balconies | rubble on the stair -> balcony + fire escape, snow drift entrance, courtyard-only entrance | |
| **hospital / school** (*new*) | 2-3 | 12 x 30 m (30-60 m long) | long block, central corridor, two wings, a stair core per wing | split wings, barricaded corridor, wing B upstairs only by its fire escape | S2 |
| **shop + flats** (*new*) | 2-3 | 10 x 15 m | ground-floor shop (shutters to the street) not connected to the flats | flats only from the courtyard entrance | S2 |
| **shed / pump / garage** | 1 | 4 x 4 m | 1-3 rooms | locked door, barred windows, roof by a ladder | |
| **ruin** | 1-2 | 6 x 8 m | hand-shaped: partial slabs, holes | collapsed facade ramp, burnt-out flight | |
| **gallery / overwatch** | solid | 3 x 3 m | - | ladder onto the roof (validator: a ladder must exist) | |
| **facade shell** | 2-3 | any | closed | map edge only | |

Lot templates (`blockTemplates.ts`) name a `set_piece` / `id` (hall, warehouse, store, office, block, shed, pump, ruin,
gallery, facade); the mapping template -> archetype is a table in the generator (*to add in S1*, BUILDINGS_TECH_SPEC section 2).

### 2. Building classes, roles and shares

- Three building classes (designer 2026-11-18, BUILDING_GENERATOR.md): **through** (2+ exits on different sides, a route
  through, no dead end), **enterable** (interiors, storeys, roof), **decorative** (closed, background only).
- Every enterable building also gets a **tactical role** (objective, garrison, overwatch, transit, background) before its
  story - defined in [building-stories.md](building-stories.md) section 1 (studio review 2026-11-18: role first).
- Storeys: 1-3 + roof (designer fork 2, option a). Storey height **300 cm** floor-to-floor for every walkable building
  (contract 7.1); 400 / 800 kit walls only for single-volume halls.
- Share (*proposal*, designer 2026-11-18 working assumption): ~30 % open / pass-through, ~70 % with one or two stories -
  now applied **per role** (building-stories F3), not as one map-wide number; closed shells only on the map edge.

### 3. Vertical circulation elements

| Element | Rule | State |
|---|---|---|
| **Switchback stair core** | One 300-rise flight per storey (35 deg, run 430), lanes side by side along a long wall (2 lanes when the core serves 2+ storeys), a free 200 cm approach box at every foot and after every top (BUILDING_GENERATOR.md 4.2, 2026-11-19; was 150), the slab gap exactly the flight; a 2-lane core has a doorless full-length lane wall (interior, one storey high, 10 cm, on the lane gap, the run only - both landings stay free) on every storey its flights span, so a path cannot hug the ramp edge. Needs inside length 200 + 430 + 200 and a cross width of the core + 120 cm of floor (`planStairCore`). Halls climb to the roof by it. | *done* (P0) |
| **External fire escape** | A steel stair or a ladder on a facade, landing on balconies. Storey-by-storey from a ground stop; the top one may reach the roof. May be `down_only` (iced, story 11). | *new* (S1 with story 4) |
| **Balcony** | A slab outside a flat's balcony door, 95 cm rail (= `low_vault`). Depth 120-150; **150 minimum where a fire escape lands** (the 150 x 150 approach box must fit). Connected by a fire escape or, to a neighbour's balcony, by a walkway / plank module (walk, no gap). A balcony with neither is a firing position only. | *new* |
| **Ladder / hatch** | `ALadderActor`, 36.4 cm rungs, rungs = floor(rise / 36.4), the remainder is the plinth (F1; a storey = 8 rungs + 8.8). Hatch 100 x 100 with a 3-sided rim. Ladders longer than one storey only outside on a facade. | *exists* |
| **Elevator shaft** | Doors forced open, rungs inside the shaft = a ladder to a storey the stair does not serve (story 21). | *new*, later |
| **Mezzanine edge** | No rail yet. Options: 95 cm rail (vaultable into a 3 m drop) vs tall rail (no vault, blocks sight). | *parked* (designer 2026-11-18: «подожди с антресолью») |

Mantle / drop rules (unchanged, MANTLING.md): low_vault 95; very_tall 300-320 only at a parapet gap (1-storey roofs);
controlled drop <= 250 cm. **A storey is never reachable by a drop only.**

### 4. Layout rules carried from the grammar

Corridor spine 1.8-2.4 m, BSP leaves 3 x 3 to 8 x 8 m, core reserved on every storey before rooms, every room >= 1 door,
>= 1 loop per storey with >= 3 rooms, >= 2 exterior doors for buildings > 150 m2 on different sides when possible, windows on
every walkable storey (rhythm 3-4 m, sill 95). Full list: BUILDING_GENERATOR.md sections 3-4.

### 5. Kit performance (technical director, 2026-11-18)

Kit modules are 100-300 cm wide, so a map of 60+ buildings means thousands of module actors. The builder emits modules as
instances (ISM / HISM per module type per building, or Packed Level Actors) - never one actor per module. Budget in
BUILDINGS_TECH_SPEC section 6.

## Formulas

### F1. Storey and ladder geometry

```
z_floor(k)  = k * H_storey                       H_storey = 300 cm, k = 0..storeys
rungs(rise) = floor(rise / r)                    r = 36.4 cm
plinth(rise)= rise - rungs(rise) * r             (300 -> 8 rungs + 8.8; 600 -> 16 + 17.6; 900 -> 24 + 26.4)
flight_run  = H_storey / tan(slope)              slope = 35 deg -> run ~430 cm per storey
```

Same as `ladderForLevels` in `schema.ts`. Constraint: `0 <= plinth < r`.

### F2. Balcony link

```
link(balcony_a, balcony_b) = walkway   if a walkway / plank module joins them and z_a = z_b
                             none       otherwise
```

A bare gap is not a link: MANTLING.md has no step-over action. The walkway is a walkable module, max span 300 cm.

### F3. Archetype pick (per lot)

```
P(a | lot) = w_a * eligible(a, lot) / sum_b(w_b * eligible(b, lot))
eligible(a, lot) = 1 if lot footprint >= a's min footprint (section 1), the storeys fit, and the map position allows it
                   (facade shells: map edge only), else 0
```

`w_a` comes from the lot template (`blockTemplates.ts`, today 0.6-1.2 per template) through the template -> archetype map.

### F4. Office storey count (*proposal*)

```
storeys(office) = 2 if long_side >= 8 m else 1
```

## Edge Cases

- **If a lot is too short for a switchback core** (`planStairCore` returns null: inside length < 200 + 430 + 200 or cross
  width < core + 120): the archetype falls back to an external fire escape as the up-route (2+ storeys) or a straight stair
  with 150 landings (sheds / ruins); if neither fits, the lot gets a 1-storey archetype.
- **If a balcony has neither a fire escape nor a walkway to a neighbour**: firing position only, never a route node (validator
  `balcony_orphan` when a story expected it as a route).
- **If a fire escape lands on a balcony shallower than 150**: invalid - the generator deepens it to 150 or drops the stop.
- **If a hall mezzanine edge has no rail** (parked decision): the edge is not a drop link; the mezzanine is reached only by
  its stair; nothing reachable depends on dropping off it.
- **If a hall wall ladder rise is 400** (single-volume halls only): F1 gives 10 rungs + 36.0 plinth, i.e. a plinth of one full
  pitch; the builder uses 11 rungs (400.4, as LADDERS.md) - the only allowed rounding up.
- **If a 400 / 800 hall wall would hold a walkable storey**: forbidden; such walls are only for single-volume halls whose roof
  edge is never climbable.
- **If a facade shell is placed inside the playable frame**: invalid (audit cause D); moved to the edge or replaced by an
  enterable archetype.
- **If a rotated building's ramp loses navmesh** (audit 1.3): rules are checked in the local frame; the in-engine bot smoke
  is the final check (engine navmesh is the runtime truth, BUILDINGS_TECH_SPEC ADR-1).

## Dependencies

| System | Direction | Interface |
|---|---|---|
| BUILDING_GENERATOR.md grammar + contract | upstream | shell, storeys, module names, 300 / 50 / 100 grid (3.1 superseded by section 1 here) |
| MANTLING.md, `obstacle_heights.json` | upstream | edge height classes (low_vault 95, very_tall 300-320, drop <= 250) |
| LADDERS.md, `ALadderActor`, `ladderForLevels` | upstream | rung pitch, ladder placement |
| [opening-states.md](opening-states.md) | downstream | doors / windows the archetype places get states |
| [building-route-model.md](building-route-model.md) | downstream | circulation elements become graph edges |
| [breach-panels.md](breach-panels.md) | downstream | wall segments where panels may stand |
| [building-stories.md](building-stories.md) | downstream | roles, eligibility per archetype |
| PMC_DEFENCE.md (horde defence buildings) | related | fallback layers, firing positions (open topic) |

## Tuning Knobs

| Knob | Default | Safe range | Where | Effect |
|---|---|---|---|---|
| `H_storey` | 300 cm | fixed (contract) | `schema.ts` `LEVEL_HEIGHT`, builder | every height rule |
| Stair slope | 35 deg | 30-35 | generator | run length, navmesh margin (navmesh 44 deg) |
| Approach box | 200 x 150 cm (depth x flight width; 4.2, 2026-11-19, was 150) | 150-200 | `STAIR_APPROACH_CM` in `schema.ts` (generator + `TRAVERSAL`) | stair mouth jams (audit cause B) |
| Balcony depth | 120-150 (150 at escape stops) | 120-180 | generator | firing position size, escape landing |
| Balcony rail | 95 cm | 95 (low_vault) or tall | generator | vaultable drop vs no exit (Q8) |
| Walkway max span | 300 cm | 100-300 | generator | balcony-to-balcony links |
| Office 2nd storey threshold | 8 m | 6-12 m | generator | share of 2-storey offices |
| Min footprints | section 1 | - | generator (*proposal*) | which lots get which archetype |
| Archetype weights `w_a` | per template | 0-1.2 | `blockTemplates.ts` | mix per map |

## Visual/Audio Requirements

Each archetype reads from the tactical camera by silhouette: panel blocks with balconies, hospitals as long two-wing blocks,
halls as tall sheet volumes, fire escapes as dark diagonals on the facade. Greybox now; kit modules by name later (contract
7.2; the Soviet residential kit is expected from the designer).

## UI Requirements

Training Ground «Здания» panel shows the archetype and role per building. No in-game UI.

## Acceptance Criteria

- [ ] Every archetype row in section 1 is generated on a fixture lot of its min footprint by a TS unit test; the result passes
      `validateBuilding` with 0 errors (seeds 1-20).
- [ ] On generated x1 and x6 maps (5 seeds each) every enterable multi-storey building has every room, storey and walkable
      roof reachable without a drop (`unreachable_room` / `unreachable_level` / `unreachable_roof` = 0).
- [ ] Every ladder in a generated layout equals `ladderForLevels` for its rise (F1).
- [ ] No facade shell inside the playable frame on any generated map.
- [ ] Every fire-escape stop is on a balcony >= 150 deep; every balcony a story lists as a route node has an escape or a
      walkway (F2).
- [ ] Built X6: module actors are instanced (actor count per building <= the TECH_SPEC budget).
- [ ] Bot smoke on the built map (`LevelGenBuildingsSmoke`): >= 95 % of stair and ladder legs pass (gate before S1).

## Open Questions

| # | Question | Recommendation | Owner |
|---|---|---|---|
| Q7 | New archetypes hospital / school and shop + flats - approved? Sizes? | yes, 2-3 storeys, 30-60 m long (S2) | designer |
| Q8 | Balcony rails 95 (vaultable = a drop of one storey onto the escape / ground) - accepted? | - | designer |
| Q9 | Mezzanine / hall landing edge (parked): 95 vaultable rail vs tall rail | - | designer |
| - | Horde defence buildings (fallback layers, firing positions, inner loop, barricadable openings, exit) | separate GDD after PMC_DEFENCE | studio |
| - | Real roofs (vents, HVAC, penthouses, skylights, level changes as cover) | P3 / P4 | studio |
