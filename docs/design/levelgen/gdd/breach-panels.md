# Breach Panels (Destructible Walls)

> **Status**: Draft v2 - generator phase S4 waits for the studio decisions below, a charge economy and the R3 spike
> (reworked after `/design-review` + the 4-role studio review, 2026-11-18)
> **Author**: Claude (building generator owner)
> **Last Updated**: 2026-11-18
> **Implements Pillar**: 1 Non-linear, never impossible; 2 Readable; 3 Costs, not walls (see [systems-index.md](systems-index.md))
> **Source**: BUILDINGS_GDD.md section 5 (commit 9210a435); runtime row from section 11; designer 2026-11-18 on destructible
> elements (BUILDING_GENERATOR.md open topics).

## Overview

A breach panel is a marked section of wall that explosives can open: a mine, a charge or an RPG round turns it into a hole
with rubble at its foot. Panels are placed by the generator in one scored pass after the story, only where a breach is a
meaningful shortcut - above all **around an entry the defenders cover** (game designer, studio review), into the next flat,
around a blocked stair, between hospital wings. In the route model a panel is an `explosive` edge: it may shorten a route, it
is never the only way. At runtime the breach changes the navmesh, cover and line of sight locally.

Designer 2026-11-18: walls blown by mines, charges or RPGs - a special wall, a crack, it can be punched through.

## Player Fantasy

"We are not going through their kill zone at the door - we make our own door." The player spots a cracked panel, spends a
charge they may need later, and opens a new line into the building that the defenders did not cover. It is loud, it is
expensive, and it is the player's choice - the building never forces it.

## Detailed Rules

1. **Marked panels, not any wall** (*proposal*): a breach panel is its own kit module, **200 cm wide**, full storey height,
   with a visible tell readable at the standard zoom - cracks, broken plaster, a bricked-up former opening, rusty sheet in a
   hall.
2. **States**: `intact` (a wall for movement, sight and cover) and `breached` (a hole 120 x 220 cm with one rubble box at its
   foot, *proposal*). Two adjacent panels = a **wide breach** (hole 240 x 220, opened by a big charge).
3. **Damage** (*proposal*): an RPG-7 round opens one panel, a charge / mine one panel, a big charge two adjacent panels; other
   weapons do nothing. RPG-7 is on the designer's weapons list (WEAPONS_FRAMEWORK, week 2); **charges / mines are not in
   WEAPONS_FRAMEWORK yet** - S4 waits for them (see Dependencies).
4. **Placement** (generator, one pass = pipeline step 8 after the story and the default states): every candidate wall
   segment is scored by F1; panels are placed greedily by score while `score >= G_min` and F2 holds. A story's
   `opening(wall, breach)` brick only **adds a candidate** (forced into the pass, still scored). Never in a stair core, a
   slab, a building corner, or within 60 cm of another opening's jamb; the breach triad space (DoorwayBreachRules) free on
   both sides; walkable floor on both sides at the same storey; the rubble box kept free of props / cover.
5. **Validation**: reachability is checked with every panel as a wall
   ([building-route-model.md](building-route-model.md) rule 5); the breach route (panel as `explosive` edge) is stored only
   when cheaper. A breach may only shorten a route.
6. **Runtime** (R3): swap intact -> breached mesh + rubble + dust; local navmesh rebuild (the project navmesh is already
   `RuntimeGeneration=Dynamic`, `Config/DefaultEngine.ini`); cover slots, LOS and the baked tactical data
   (`Content/Data/LevelGen/tactical/*.json`) refreshed for the tiles around the hole; a noise event (`N_charge`). Until R3
   lands, panels behave as walls (safe degradation).

## Formulas

### F1. Placement score

```
gain(p)  = T(link_a -> link_b without p) - T(link_a -> link_b through the hole)        seconds, walk time only (v_ref, actions)
score(p) = gain(p) * (1 + k_def * defended(p))
place p  if score(p) >= G_min
```

| Variable | Meaning | Default (*proposal*) | Range |
|---|---|---|---|
| `link_a`, `link_b` | the two nodes the panel joins (`links` in the data) | geometry | - |
| `T` | route-model time `t` (F1 there) without noise / explosive weights | - | - |
| `defended(p)` | 1 if the panel bypasses an entry of a `garrison` / `objective` building (building-stories roles), else 0 | - | 0 / 1 |
| `k_def` | bonus for bypassing a defended entry | 1.0 | 0-3 |
| `G_min` | minimum score | 20 s | 5-60 |

Walk time only: the explosive and noise price is the player's trade-off at runtime, not a placement filter (the earlier
draft charged the explosive here and no panel could ever qualify).

### F2. Panels per building

```
panels(b) <= min(P_max, floor(perimeter_wall_length(b) / 10 m))       P_max = 2 (*proposal*); most buildings get 0
```

### F3. Hole geometry

```
hole_w = 120 cm (one 200 panel) | 240 cm (wide breach: two adjacent 200 panels)
hole_h = 220 cm   (>= head room 220, BUILDING_GENERATOR.md 4.1)
rubble box = hole_w x 100 cm at the foot, height <= 35 (step) so it never blocks the hole
```

## Edge Cases

- **If a panel would be the only way to a room or storey**: reachability fails -> the panel is removed or the story rolls
  back (`unreachable_without_explosive`).
- **If the squad has no charge / RPG**: the panel stays a wall; nothing reachable is lost.
- **If an RPG hits a panel from inside**: same result; the hole is symmetric.
- **If a panel is within 60 cm of a jamb, in a core, slab or corner**: not a candidate.
- **If one side of the panel is a slab edge or a drop**: not a candidate (both sides need walkable floor at one storey).
- **If two panels are adjacent and a single charge hits**: one panel opens; the big charge opens both.
- **If a unit stands in the rubble box at the moment of the breach**: runtime pushes it out / applies blast damage (R3; the
  generator only keeps the box free of props).
- **If a story forces a panel whose score < `G_min`**: it is not placed; the story still applies its other bricks.
- **If the tile-local navmesh rebuild breaks the budget** (Q6): fallback = pre-baked navmesh states per tile around each
  panel (needs a decision in ADR-1, it conflicts with "navmesh built at load").

## Dependencies

| System | Direction | Interface |
|---|---|---|
| [building-archetypes.md](building-archetypes.md) | upstream | wall segments, storey height 300 |
| [opening-states.md](opening-states.md) | sibling | a panel is an opening with states intact / breached; `t_charge`, `N_charge` |
| [building-route-model.md](building-route-model.md) | upstream | `explosive` edges, breach route, `T` |
| [building-stories.md](building-stories.md) | upstream | roles (`defended`), brick `opening(wall, breach)` |
| DOORWAY_BREACH.md, `DoorwayBreachRules.h` | upstream | triad space on both sides |
| WEAPONS_FRAMEWORK.md | upstream | RPG-7 (week 2); **charges / mines: missing - must be added before S4** |
| Charge economy (how many charges a squad carries / finds) | upstream | not designed yet - **blocks S4** (game designer) |
| Navigation (dynamic navmesh) | downstream | tile-local rebuild |
| Cover / LOS / tactical JSON | downstream | refresh after a breach |
| PMC AI (PMC_DEFENCE.md) | downstream | defenders cover breach panels |

## Tuning Knobs

| Knob | Default | Range | Effect |
|---|---|---|---|
| `G_min` | 20 s | 5-60 | low: panels everywhere; high: almost none |
| `k_def` | 1.0 | 0-3 | how strongly panels gather round defended entries |
| `P_max` | 2 | 0-4 | per-building cap |
| Hole size | 120 x 220 | 100-160 x 220-240 | squad throughput through the breach |
| Weapons that breach | RPG-7, charge, mine, big charge (2) | - | Q5 |
| `t_charge`, `N_charge` | 5 s, 25 m | 3-10, 15-40 | shared with opening-states F1 |

## Visual/Audio Requirements

Kit modules `wall_breach_200` + `wall_breach_200_broken` (BUILDINGS_TECH_SPEC section 3); tell = cracks / broken plaster /
bricked-up opening / rusty sheet, readable at the standard zoom. Breach VFX (dust, debris) and SFX are R3 tasks.

## UI Requirements

Page plan view: a breach glyph per panel with its score. In game: the visual tell plus the opening-state overlay
(opening-states UI Requirements) marks breachable panels.

## Acceptance Criteria

- [ ] Unit: on a fixture where a panel is the only route to storey 1, the validator fails; with a fire escape added it passes,
      and the panel's score >= `G_min`.
- [ ] Unit: a panel bypassing a garrison building's covered door gets `score = gain * (1 + k_def)`.
- [ ] Generator (x1 / x6, 5 seeds): no panel in a core, slab, corner, or within 60 cm of a jamb; every panel's score >= `G_min`;
      `panels(b) <= P_max`; at least one panel per map when any building has a defended entry with `gain >= G_min`.
- [ ] Builder unit: an intact panel spawns `wall_breach_200` with tag `breach_panel=<id>`; the breached variant spawns hidden.
- [ ] R3 spike (before S4): one charge opens one panel on X6; a navmesh path through the hole completes < 1 s after the blast;
      the rebuild costs <= 2 ms per frame (time-sliced).

## Open Questions

| # | Question | Recommendation |
|---|---|---|
| Q4 | Only marked panels, or whole materials breachable anywhere (hall sheet walls, wooden partitions)? | marked only |
| Q5 | Breach size per weapon: one size (120 x 220) or per weapon? | one size; big charge = two panels |
| Q6 | Dynamic navmesh for breaches: acceptable cost? | tile-local rebuild, budget <= 2 ms / frame, measured by the R3 spike on X6 |
| - | Charge economy: how many charges per mission, where found | needed before S4 (game designer) |
