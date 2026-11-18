# Opening States (Doors, Windows)

> **Status**: Draft v2 (studio discussion; reworked after `/design-review` + the 4-role studio review, 2026-11-18)
> **Author**: Claude (building generator owner)
> **Last Updated**: 2026-11-18
> **Implements Pillar**: 2 Readable; 3 Costs, not walls (see [systems-index.md](systems-index.md))
> **Source**: BUILDINGS_GDD.md section 4 (commit 9210a435); degradation rule from section 11.

## Overview

Every opening in a building - door, gate, shutter, window, loophole, breach panel - carries a **state**. The state decides who
can pass, which action passing needs, what that action costs in the route model
([building-route-model.md](building-route-model.md)) and what the builder spawns (a leaf, glass, bars, boards, a wall fill).
States are set by the generator (a default table per archetype and facade, then overridden by the building's story) and
stored per opening in the layout; the runtime makes them interactive. Until a runtime feature exists every state degrades to
a safe behaviour, so data can ship before the C++. Breach panels have their own GDD ([breach-panels.md](breach-panels.md)).

## Player Fantasy

The player sees at a glance what each opening offers and what it costs: a lock plate means "kick it, and the block hears
you", glass means "you can go through, loudly", a broken window round the back means "the quiet way in", bars mean "shoot
through, but not walk through". Choosing an entry becomes a decision between speed, noise and exposure, made from what is
visible on the facade - never a guess.

## Detailed Rules

Designer 2026-11-18: doors that are locked and can be kicked; windows with bars; windows one jumps through that shatter;
already broken windows.

### 1. Door states (doors, gates, shutters)

| State | Passable by | Action | Cost (*proposal*) | Visual |
|---|---|---|---|---|
| `open` | everyone | - | 0 | open leaf / empty frame |
| `closed` | everyone | open | `t_open` 1 s, quiet | closed leaf |
| `locked` | breach | kick / shotgun / sapper (DOORWAY_BREACH «Breach Door (Sapper / Shotgun / Kick)») | kick `t_kick` 3 s + `N_kick` 15 m; shotgun `t_shot` 1 s + `N_shot` 30 m; sapper = charge | lock plate, dented leaf |
| `barricaded` | explosive only (later: clear by hand) | charge / clear | charge (`t_charge` + `N_charge`, 1 explosive) | furniture / planks / sandbags in the frame (author traces, building-stories 4) |
| `sealed` | nobody | - | blocked | welded / bricked up / rubble / snow drift |

Gates and rolling shutters use the door states (a shutter is `closed` = lift, `locked`, or `sealed`); passability is the
same for operatives and PMC units (no per-agent rules yet).

### 2. Window states (sill 95 = low_vault), loopholes

| State | Passable by | Action | Cost | Notes |
|---|---|---|---|---|
| `broken` | vault | vault | `t_vault`, quiet | the quiet entry |
| `glazed` | vault, glass breaks | vault + shatter | `t_vault + t_glass`, `N_glass` 10 m; later: cuts | becomes `broken` after the first pass or a shot |
| `barred` | nobody | - | blocked | shoot / see through |
| `boarded` | later: tear off boards | (later) | blocked for now | no sight |
| `fixed` | nobody | - | blocked | too high / too narrow (shop windows); **loopholes are always `fixed`** |

### 3. Default table (when no story sets a state)

Studio review 2026-11-18: with every window `broken` by default every building has a free quiet entry and a locked door is
cosmetic. Defaults (*proposal*):

| Opening | Street facade | Back / courtyard facade | Upper storeys |
|---|---|---|---|
| exterior door | `closed` | `closed` | - |
| interior door | `open` | `open` | `open` |
| ground-floor window | `glazed` | `glazed` | - |
| upper-storey window | - | - | `glazed` (firing position, rule 5) |
| shop window, loophole | `fixed` | `fixed` | `fixed` |

Hall / warehouse gates: `closed`. Ruins: windows `broken`, doors `open` (already wrecked). `broken` elsewhere only by a story
(story 9 "broken back windows" is the quiet-way lever).

### 4. Placement rules

1. A **ground-floor window state is a design lever**: barred fronts + broken backs = "the quiet way is round the back".
2. Defaults are applied **before** the story (pipeline step 5 in BUILDINGS_TECH_SPEC), so the story validates against the
   final states.
3. A story may only set states listed in its recipe ([building-stories.md](building-stories.md)); the validator then checks
   reachability with the new states.
4. Every state must be **visible from the tactical camera** (studio review): the builder spawns its tell (lock plate, glass,
   bars, boards, fill) and the page / game offer a state overlay (UI Requirements). A state without a tell module is not
   emitted.
5. **Upper-storey windows** are firing positions and never an entry unless a balcony, a roof or a snow ramp stands below them
   (story 10).
6. Runtime transitions: `closed -> open` (open), `locked -> open` (breach; leaf swings / breaks), `glazed -> broken` (first
   vault or a shot), `barricaded -> open` (charge; later clear by hand), `boarded -> broken` (later). No transition back to a
   more closed state at runtime (PMC barricading is Q10 in building-stories).

### 5. Safe degradation (until the runtime feature lands)

| State | Behaves as | Navmesh rule |
|---|---|---|
| `closed` | `open` until R1 (doors) | the leaf is spawned with `CanEverAffectNavigation = false` and no pawn collision, so it does not cut the dynamic navmesh |
| `locked` | `open` until R1, then `closed` until the kick lands | as `closed` |
| `glazed` | `broken` | pane without pawn collision, visual only |
| `barricaded`, `boarded`, `sealed`, `barred` | blocked | blocking collision, affects navigation |

The route model always prices the **designed** state; the runtime / build report lists which states are degraded on a map.
The data does not change when a feature lands. Representation of states at runtime (NavArea / NavLink costs vs collision) is
decided in ADR-3 (BUILDINGS_TECH_SPEC section 8).

## Formulas

### F1. Edge cost of an opening (feeds the route model F1)

```
cost(opening) = (t_action, n_action, x_action)
  t_action : seconds to pass
  n_action : noise radius, m
  x_action : explosives used
```

| Action | `t_action` | `n_action` | `x_action` |
|---|---|---|---|
| walk (open) | 0 | 0 | 0 |
| open | `t_open` 1 s (0.5-2) | 0 | 0 |
| kick | `t_kick` 3 s (2-5) | `N_kick` 15 m (10-20) | 0 |
| shotgun breach | `t_shot` 1 s (0.5-2) | `N_shot` 30 m (20-40) | 0 (ammo) |
| vault | `t_vault` (= the low_vault mantle clip, ~1.2 s, measure) | 0 | 0 |
| shatter + vault | `t_vault + t_glass` (`t_glass` 0.5 s, 0-1) | `N_glass` 10 m (6-15) | 0 |
| charge | `t_charge` 5 s (place + fuse, 3-10) | `N_charge` 25 m (15-40) | 1 |

All values *proposal*. Reference: a thrown stone lure is 8 m (STEALTH_LURE.md); every loud entry is louder than a lure.
Blocked states (`sealed`, `barred`, `fixed`, `boarded` for now, `barricaded` without explosives) produce **no edge**.

### F2. Quiet-entry share (map metric)

```
quiet_entry(b) = 1 if b has >= 1 exterior entry with n_action = 0 (open / closed door, broken window), else 0
quiet_share    = sum over enterable b of quiet_entry(b) / enterable buildings
```

Report only (no hard band) until the playtest; with the default table most buildings keep a quiet door, stories remove it
(3, 20) or add a quiet window (9).

## Edge Cases

- **If every exterior opening of a building is blocked** (all doors sealed, all windows barred / fixed): the building is a
  decorative shell; allowed only on the map edge - otherwise the story is rolled back.
- **If a `locked` door is the only entry**: reachable (a kick is not an explosive) but loud; allowed. Until R1 it degrades to
  `open`.
- **If a `barricaded` door is the only entry**: invalid - it needs explosives; the validator rejects
  (`unreachable_without_explosive`) and the story rolls back.
- **If a `glazed` window is vaulted by the second operative**: it is already `broken`; the noise is paid once.
- **If an upper-storey window sits above a snow ramp or a roof**: it becomes an entry with the window's own state cost; with
  no ramp / roof below it never links to the outside.
- **If a door has a lock but its breach triad space is not free** (DoorwayBreachRules): the squad breach falls back to the
  stack (`FallbackStackSpacingCm` 60); the room still needs >= 1 door with a full triad side.
- **If a `barred` window is the only line of sight into a room**: sight and fire pass (bars block collision, not visibility);
  `boarded` blocks both.
- **If a closed leaf is spawned before R1**: it must not affect navigation (section 5); a leaf that cuts the navmesh is a
  builder bug caught by the bot smoke.
- **If a legacy layout has no `state` fields**: the old meaning stays (`open` doors, `broken` windows) - layouts of schema
  `/1` are read with the old defaults, the new default table applies only to `/2` (BUILDINGS_TECH_SPEC section 2).

## Dependencies

| System | Direction | Interface |
|---|---|---|
| [building-archetypes.md](building-archetypes.md) | upstream | where doors / windows stand, sill 95, storey 300 |
| [building-route-model.md](building-route-model.md) | downstream | F1 edge costs, blocked = no edge |
| [building-stories.md](building-stories.md) | downstream | bricks `door_state`, `window_state`; author traces |
| [breach-panels.md](breach-panels.md) | sibling | a breach panel is an opening with its own states (intact / breached) |
| DOORWAY_BREACH.md, `DoorwayBreachRules.h` | upstream | triad slots around every door; Sapper / Shotgun / Kick |
| MANTLING.md | upstream | vault clip for windows |
| STEALTH_LURE.md, PMC perception | downstream | kick / shot / glass / charge noise events (lure = 8 m reference) |
| BUILDINGS_TECH_SPEC sections 2-4 | downstream | data fields, builder tags, runtime R1-R2 |

## Tuning Knobs

| Knob | Default | Safe range | Effect if too high / too low |
|---|---|---|---|
| `t_kick` | 3 s | 2-5 | high: kick never chosen; low: locked = open |
| `N_kick` | 15 m | 10-20 | high: kick alerts the whole block; low: no stealth trade-off |
| `N_shot`, `t_shot` | 30 m, 1 s | 20-40, 0.5-2 | shotgun = fast but loudest non-explosive |
| `N_glass` | 10 m | 6-15 | as above, for windows |
| `t_open` | 1 s | 0.5-2 | |
| `t_charge`, `N_charge` | 5 s, 25 m | 3-10, 15-40 | shared with breach-panels |
| Default table (section 3) | per archetype x facade | - | quiet entries everywhere vs nowhere |

All in the route cost JSON (BUILDINGS_TECH_SPEC section 2), not in code.

## Visual/Audio Requirements

Kit modules per state (BUILDINGS_TECH_SPEC section 3): `door_leaf`, `door_lock_plate`, `barricade_door` (per author),
`window_glass`, `window_bars`, `window_boards`; sealed = wall fill. Tells must read at the standard zoom: a lock plate alone
does not - locked doors also get a large tell (chain, cross planks, a red stencil) (*proposal*). Audio: door open, kick
impact, shotgun breach, glass shatter, charge - each also a gameplay noise event with the F1 radius.

## UI Requirements

- In game: a **state overlay on a held key** and a **highlight on hover** over an opening (glyph: open / closed / locked /
  barricaded / sealed / glazed / barred / boarded) - level and game designer, studio review 2026-11-18.
- Training Ground page: a glyph per opening state on the plan view.

## Acceptance Criteria

- [ ] Every door and window state round-trips through the layout schema (TS unit); `/1` layouts read with the old defaults,
      `/2` with the default table.
- [ ] Builder unit: each state produces its actors / tags (`door_state=*`, `window_state=*`); a closed leaf has
      `CanEverAffectNavigation = false`; a legacy layout builds bit-identically.
- [ ] Route-model unit: blocked states produce no edge; `locked` = kick cost (`t_kick`, `N_kick`); `glazed` pays once.
- [ ] Validator: no generated building (x1 / x6, 5 seeds) has `barricaded` as its only entry.
- [ ] `quiet_share` is reported per map (5 seeds).
- [ ] Runtime (R1): `DoorBreachSmoke` kicks a locked door; the noise event radius = `N_kick` +- 0.5 m.
- [ ] In game (R1): the overlay key shows a glyph on every opening within the camera view.

## Open Questions

| # | Question | Recommendation |
|---|---|---|
| Q2 | Locked doors: kick only, or also lock-picking / keys? Kick time and noise radius? | kick + shotgun + sapper (DOORWAY_BREACH), 3 s, 15 m |
| Q3 | Glazed windows: does the vault cut / slow? Noise radius? | noise only first, 10 m |
| - | `boarded`: tear-off time and noise | later stage |
