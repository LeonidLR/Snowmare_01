# Building Stories

> **Status**: Draft v2 (studio discussion; reworked after `/design-review` + the 4-role studio review, 2026-11-18)
> **Author**: Claude (building generator owner)
> **Last Updated**: 2026-11-18
> **Implements Pillar**: all five, chiefly 1 Non-linear, never impossible and 4 The setting tells it
> (see [systems-index.md](systems-index.md))
> **Source**: BUILDINGS_GDD.md sections 1 (vision, share) and 6 (commit 9210a435); studio review 2026-11-18 (game, level,
> narrative, technical director) and the designer's answers the same day.

## Overview

A building story is a small, readable reason why a building is not "in, up the stair, done": the front door is locked, rubble
blocks the stair so the way up is a balcony and a fire escape, the roof is reached only from outside, a snow drift buries the
entrance. A story is a **recipe of 1-3 bricks** - small edits to the generated building - plus an **author** (who did it:
survivors, the PMC, the cold, Soviet decay) whose traces tell the world. Stories are not sprinkled at random: every
building first gets a **tactical role** (objective, garrison, overwatch, transit, background) and every district a theme, and
the story budget follows the roles along the mission route. After a recipe the route model checks reachability and, for
choice stories, that the fast and the quiet way really differ; a failing recipe is rolled back.

## Player Fantasy

Every building is a small story (designer 2026-11-18: «Я хочу, чтобы каждое здание было небольшой историей… чтобы маршруты
были нелинейными, чтобы не только зашел, поднялся на лестнице, и вот ты уже наверху.»). The squad arrives, reads the facade -
a welded door with a stencil, chalk "ЗДЕСЬ ЛЮДИ / НЕ СТРЕЛЯТЬ", a snow drift up to a first-floor window, a fire escape on the
side - and knows who was here and how to get in. The buildings that matter on the way to the objective carry the puzzles;
the rest of the town breathes. The player remembers buildings, not boxes.

## Detailed Rules

### 1. Tactical role first (level designer, studio review)

| Role | Assigned to | Story budget |
|---|---|---|
| `objective` | buildings holding a mission goal (`objectives/*.json` `goals[].building`) | always a story; a **choice** story preferred |
| `garrison` | buildings holding PMC defender places (passed in as `placesByBuilding` - `tactical/*.json` places carry no building id, the PMC squads data of task 087 will supply them) or within `R_garrison` of an objective | most get a story; author mostly PMC |
| `overwatch` | the tallest enterable buildings with a walkable roof within `R_watch` of `R_main` / an objective | a story allowed to be a **detour** (the height pays it off) |
| `transit` | enterable buildings the main route passes through or beside (<= 15 m from `R_main`) | choice story or pass-through |
| `background` | everything else | mostly `open`; a light story (author traces only) |

Roles come from data the generator already has; they are stored per building (`role`) and shown on the page. A building
that qualifies for several roles takes the first in the order **objective > garrison > overwatch > transit > background**.
`overwatch` = at most `N_watch` buildings per map (*proposal* 3), each with >= 2 storeys and a walkable roof, the tallest
first (ties by id).

### 2. District theme (narrative + level, studio review)

A district (a block of lots in the level generator) gets a theme that sets the **author weights** of its stories: districts
next to an objective are PMC-held, edge districts belong to survivors and the cold, the rest mix with decay. The gradient
"towards Ronin" reads as a front line. Later: one hand-authored hero story per map (a note, a radio call).

### 3. Bricks

| Brick | Effect on the building | Used by |
|---|---|---|
| `door_state(door, state)` | sets a door's state (locked / barricaded / sealed) - [opening-states.md](opening-states.md) | 3, 10, 15, 20 |
| `window_state(side or storey, state)` | sets window states on a facade / storey | 8, 9, 20 |
| `block_flight(flight, kind)` | the flight stays (geometry) but is impassable: rubble / burnt / barricade (no route edge) | 4, 13, 15 |
| `add_balcony_escape(flat, storeys)` | a balcony door + balcony on each storey + a fire escape between them; until flats exist (S2) `flat` = any room on an exterior wall of a block / office | 4 |
| `roof_access(kind)` | removes the core-to-roof / hatch access and adds a facade ladder or fire escape to the roof | 7 |
| `snow_ramp(window)` | a snow ramp from the ground to a storey-1 window (walkable slope <= 35 deg, ends at the sill as a low_vault) | 10 |
| `escape_down_only(escape)` | marks a fire escape `down_only` (iced) | 11 |
| `hatch_only(room)` | the room's doors become walls, a hatch + ladder from the storey below is its only access | 16 |
| `dead_end(flat)` | a flat with a single door at the end of a corridor >= 8 m | 17 |
| `isolate(zone)` | a ground-floor zone (shop) loses every internal door to the rest; its own street openings only | 19 |
| `split(wall)` | a full partition without a door divides the building into wings; each wing gets an entrance and a core / escape | 5 |
| `opening(floor or wall, kind)` | a floor hole over a rubble heap (drop <= 250), a facade collapse (rubble ramp), a breach-panel candidate ([breach-panels.md](breach-panels.md)) | 6, 12 |
| `link_neighbour(kind)` | a hole in a shared wall, a plank between windows, a roof bridge | 14, 18 |
| `forced_shaft(core)` | elevator doors forced, rungs inside = a ladder | 21 (L) |

### 4. Author and outcome (narrative director, studio review; designer: survivors exist, `author` accepted)

| Author | Reads as | Trace set (props, 3-5 visual variants each) |
|---|---|---|
| `survivors` | improvised, civilian, lived-in | wardrobe in the frame, chalk «ЗДЕСЬ ЛЮДИ / НЕ СТРЕЛЯТЬ», a stove pipe out of a window, buckets for melt water |
| `pmc` | hi-tech, deliberate (Ronin) | a welded plate with a stencil, sandbags, zip ties, 5.45 casings, a tripwire |
| `cold` | the Arctic itself | ice on the stair, icicles, frost, drifts |
| `decay` | the Soviet town falling apart | «Посторонним вход воспрещён», a sealed switchboard, collapsed plaster |

- An **outcome** layer for defensive stories: `held` (intact barricade) or `fell` (breached boards, bodies). A dead-end trap
  with earlier victims is a readable warning; a sniper's nest carries a thermos, PMC rations, casings, a range card scratched
  on the sill; bodies of the executed technicians tie a building to the main plot (Ronin removes witnesses).
- Survivors exist in the canon (designer 2026-11-18); `docs/design/NARRATIVE_BIBLE.md` has no survivor faction yet - an entry
  is a narrative task before S3 (who they are, where they live, how they relate to the Depot massacre).
- Squad barks: once per story type per map.

### 5. Application (per map)

1. Roles (section 1), district themes (section 2), default opening states (opening-states section 3).
2. Per building: pick a story by role budget and weight (F1), apply its bricks in order.
3. Run the route model ([building-route-model.md](building-route-model.md)): reachability (rule 5); for a **choice** story
   also F4 (fast and quiet entries differ).
4. On failure undo every brick of the recipe, log `story_rolled_back`, pick the next candidate (deterministic sub-seed);
   after 4 failures the building keeps `open`.
5. Optionally (F2) add a second **compatible** story and repeat 2-4 for it alone (e.g. 3 + 9).
6. Pick the author (district weights, story's allowed authors) and the outcome; place the trace props.
7. Building links (14, 18) after every building is final; breach panels (breach-panels rule 4) after that.
8. Store `stories[]` (`id`, `bricks`, `seed`, `author`, `outcome`), `role` and the key route in the layout.

### 6. Catalogue (22 stories)

Type: **choice** = adds an alternative entry (must pass F4); **detour** = one forced bypass (needs a role payoff);
**position** = creates a tactical spot. Phase: S1 = first generator slice (designer 2026-11-18: cut to 3, 4, 7, 10; more
later).

| # | Story | Recipe (bricks) | Archetypes | Key route (example) | Type | Authors | Phase |
|---|---|---|---|---|---|---|---|
| 1 | Open | - | all | door -> stair -> up | - | any (traces only) | S1 (base) |
| 2 | Pass-through | 2 doors on **opposite** sides (the base rule already gives 2 doors > 150 m2; this enforces opposite) | all >= 150 m2 | street A -> through -> street B | choice | any | S1 (base) |
| 3 | Locked door | door_state(front, locked) | all | kick the front, or round to the back door | choice | pmc, decay, survivors | **S1** |
| 4 | Rubble on the stair + fire escape | block_flight(0->1, rubble) + add_balcony_escape | block, office | flat 1 -> balcony -> fire escape -> balcony 2 -> up | detour | decay, pmc | **S1** |
| 5 | Split wings ("hospital") | split(corridor) + per-wing entrance; wing B upstairs only by its escape | hospital/school, office | out, round, into wing B, up its escape | detour | decay, survivors | S2 (needs the hospital archetype) |
| 6 | Collapsed floor | opening(floor, hole over a rubble heap, drop <= 250) | block, ruin | down by the hole, up by the far stair | position | decay | S3 |
| 7 | Roof only from outside | roof_access(facade ladder / escape) | all with a walkable roof | out -> ladder -> roof | detour | decay, cold | **S1** |
| 8 | Barred ground floor | window_state(storey 0, barred) | all | doors only | detour | pmc, decay | S3 |
| 9 | Broken back windows | window_state(back, broken) + front glazed | all | quiet entry from the back | choice | survivors, decay | S3 |
| 10 | Snow-drift entrance | door_state(front, sealed: drift) + snow_ramp(storey-1 window) | block, office | up the drift, in by the window | choice | cold | **S1** |
| 11 | Iced fire escape | escape_down_only(escape) | block | escape is an exit, not an entry | position | cold | S3 |
| 12 | Facade collapse | opening(wall, collapse) + rubble ramp to storey 1 | ruin, block | up the rubble into the cut flat | choice | decay | S3 |
| 13 | Burnt-out flight | block_flight(1->2, burnt) + ladder / hatch | block, office | ladder through the hatch | detour | decay, survivors | S3 |
| 14 | Hole into the neighbour | link_neighbour(shared wall hole) | blocks in a row | into the next stairwell, up there | choice | survivors | S3 (links last, technical director) |
| 15 | Defenders' barricade | door_state / block_flight(barricade) | all | (later: clear / blow) else the long way | detour | pmc, survivors | L |
| 16 | Sniper's nest | hatch_only(top room) | block, hall office; role garrison / overwatch | dead end with a view | position | pmc | S3 |
| 17 | Dead-end trap | dead_end(flat) | block, office, hospital; role garrison | ambush room (PMC AI) | position | pmc | S3 |
| 18 | Plank bridge | link_neighbour(plank window -> window) | blocks <= 3 m apart | across the plank | choice | survivors | S3 (links last) |
| 19 | Shop below | isolate(ground-floor shop; street shutters `sealed`) | shop + flats | flats from the courtyard | detour | decay | S2 |
| 20 | Courtyard only | street-side doors sealed / windows barred | block, hospital (lot with a courtyard side) | round the block into the yard | detour | pmc, survivors | S3 |
| 21 | Elevator shaft | forced_shaft(core) | block, office (3 storeys) | shaft to the storey the stair skips | choice | decay | L |
| 22 | Basement passage | basement level -1 + tunnel to the neighbour | later stage (needs level -1) | - | choice | survivors | L |

"Courtyard side" = a lot side facing the enclosed interior of its block; the level generator does not mark it yet (to add
with S3).

Game designer (studio review): 22 stories give 5-6 distinct experiences; keep ~8. Decision: the S1 slice is 4 stories
(3, 4, 7, 10); 16 and 17 stay because they are tied to the garrison role (stories linked to defenders), 11 stays (one data
flag); 21, 22 stay in L. The catalogue is a menu, not a quota.

### 7. Data

Weights per role and archetype, eligibility, allowed authors, compatibility and the targets live in
`Content/Data/LevelGen/building_stories.json` (*proposal*, BUILDINGS_TECH_SPEC section 2).

## Formulas

### F1. Story pick (per building b, role r, archetype a) - two steps

```
step 1: storied(b) = 1 with probability budget[r] (F3), else b keeps open / pass-through (traces only)
step 2: if storied(b), among the eligible stories other than 1 and 2:
P(s | b) = w[r][a][s] * eligible(s, b) / sum_k( w[r][a][k] * eligible(k, b) )
        (no eligible story -> b stays open; counted as a shortfall in the F3 report)
eligible(s, b) = 1 if a is in s's archetype list, s's phase is enabled, and b meets s's conditions
                 (storeys, neighbour distance, courtyard side, roof, role), else 0
detour stories: eligible only if r in {objective, garrison, overwatch}
```

### F2. Second story

```
P(second story) = p_two                                   (*proposal* 0.25)
second pick = F1 restricted to compatible(first) \ {first}
```

### F3. Story budget per role (targets, *proposal*)

| Role | Share of buildings with a story (not `open` / `pass-through`) |
|---|---|
| objective | 100 % |
| garrison | 80 % |
| overwatch | 70 % |
| transit | 50 % |
| background | 10 % |

Map-level result (report): `share_open = buildings with only story 1 or 2 / enterable buildings`. On X6-sized maps the
budget gives ~15-25 storied buildings (level designer); the old map-wide 30 / 70 % working assumption (designer 2026-11-18)
becomes this per-role table (Q1).

### F4. Pacing along the route

```
along R_main, ordered by distance: no more than M_detour = 2 consecutive buildings with a detour story
```

A violating building is re-rolled to a choice story or `open`.

### F5. Rollback budget

```
tries(b) <= T_max = 4;  rollback_share = buildings ending open by rollback / storied candidates;  target <= 10 %
```

## Edge Cases

- **If no story is eligible** (e.g. a 1-storey shed with no neighbour): the building gets `open` (story 1) with author traces
  only.
- **If a story needs a balcony but the facade has no room** (150 depth at escape stops): not eligible; the next candidate is
  drawn.
- **If story 4 blocks flight 0->1 and the fire escape cannot be placed**: rollback (storey 1 unreachable).
- **If story 5 splits a building and wing B has no core and no fire escape**: rollback; each wing needs an entrance and an
  up-route.
- **If two chosen stories touch the same opening** (3 locks the front, 10 seals it with a drift): incompatible pair -
  excluded by the compatibility list.
- **If story 7 removes the only roof access and the facade ladder cannot be placed**: rollback.
- **If story 10's snow ramp would be steeper than 35 deg or land on a lot edge**: not eligible on that facade.
- **If story 6's hole has no room for a rubble heap** (drop would be 300 > 250): not eligible.
- **If story 14 or 18 links two buildings and either end rolls back later**: links are applied after every building is final
  (step 7); a link with an invalid end is dropped.
- **If story 17 would leave its storey with no loop** (BUILDING_GENERATOR.md 3 rule 5): allowed only for that one flat; the
  rest of the storey keeps its loop.
- **If a choice story's fast and quiet entries coincide**: F4 fails, rollback.
- **If a map has no objectives / tactical places yet**: every enterable building is `transit` (<= 15 m from `R_main`) or
  `background`; the budget still applies.
- **If a map is too small** (fewer than ~10 enterable buildings): shares are reported but not enforced.
- **If a building is locked on the page**: its stories, author and role are kept across regeneration; «другая история»
  re-rolls with the next sub-seed.

## Dependencies

| System | Direction | Interface |
|---|---|---|
| [building-archetypes.md](building-archetypes.md) | upstream | eligibility by archetype, balconies, escapes, cores |
| [opening-states.md](opening-states.md) | upstream | `door_state`, `window_state` bricks; default table |
| [building-route-model.md](building-route-model.md) | upstream | reachability, key route, F4 choice test, `D` |
| [breach-panels.md](breach-panels.md) | downstream | `opening(wall, breach)` adds a candidate; roles feed `defended` |
| Mission goals (`objectives/*.json`), tactical places (`tactical/*.json`), `R_main` | upstream | roles (section 1) |
| NARRATIVE_BIBLE.md | upstream | authors, survivors entry (to add), tone of traces |
| PMC AI (PMC_DEFENCE.md, KILL_ZONE_AMBUSH.md) | downstream | ambush rooms (17), nests (16), barricades (15) |
| BUILDINGS_TECH_SPEC sections 2, 5, 6 | downstream | `stories[]`, `role`, `building_stories.json`, page, tests |

## Tuning Knobs

| Knob | Default | Range | Where |
|---|---|---|---|
| `w[r][a][s]` | set to meet F3 | 0-1 | `building_stories.json` |
| Role budget (F3) | table | 0-100 % | `building_stories.json` |
| `R_garrison`, `R_watch` | 30 m, 60 m | 10-80, 30-120 | `building_stories.json` |
| `N_watch` | 3 per map | 1-6 | `building_stories.json` |
| `p_two` | 0.25 | 0-0.5 | `building_stories.json` |
| `M_detour` | 2 | 1-4 | `building_stories.json` |
| `T_max` | 4 | 1-8 | generator |
| District author weights | per theme | 0-1 | `building_stories.json` |
| Neighbour distance for 14 / 18 | <= 3 m (18) | 1-4 m | `building_stories.json` |
| Compatibility list | per pair | - | `building_stories.json` |

## Visual/Audio Requirements

Each story must be readable **from the tactical camera at the standard zoom** (pillar 2, studio review): rubble spilling out
of the stairwell door, a snow drift up to a window, a fire escape on the facade, bricked-up street doors with a large tell. A
brick whose tell does not read from above needs a large-scale trace (from section 4) or the hover highlight
(opening-states UI). Author trace kits: BUILDINGS_TECH_SPEC section 3. Barks once per story type per map.

## UI Requirements

Training Ground «Здания» panel: role, story names, author / outcome, the key route chain, cost, `D`, the choice flag, a
«другая история» button and a lock per building; per map the role budget and pacing report (BUILDINGS_TECH_SPEC section 5).

## Acceptance Criteria

- [ ] Unit: every brick on a fixture building - state applied, undone exactly on rollback (layout byte-equal to before).
- [ ] Unit: each S1 story (3, 4, 7, 10) on its fixture archetype passes reachability; 3 and 10 pass F4; the key route's leg
      modes equal the fixture's expected sequence (e.g. story 4: walk, open, walk, climb, walk, ..., roof).
- [ ] Unit: roles from a fixture layout with 1 goal and 3 tactical places - the goal's building is `objective`, place
      buildings are `garrison`.
- [ ] Determinism: same (seed, building id, cost hash) -> same role, stories, author.
- [ ] Distribution (x1 / x6, 5 seeds **pooled**): each role's story share within its F3 target +- 10 percentage points.
- [ ] Pacing: no run of more than `M_detour` detour buildings along `R_main`.
- [ ] Rollback: <= 10 % of storied candidates end `open` by rollback (F5).
- [ ] No incompatible pair from the compatibility list ever appears on one building; every story's author is in its allowed
      list.

## Open Questions

| # | Question | Recommendation |
|---|---|---|
| Q1 | Story shares: ~30 % open / pass-through, ~70 % with 1-2 stories? Per-archetype weights? | per-role budget (F3), JSON-tunable |
| Q10 | PMC defenders: may they create stories at runtime (barricade a door, seal a stair)? | later; the data model allows it |
| Q11 | Basement level -1 (story 22): in scope for this milestone? | no |
| - | Survivors in NARRATIVE_BIBLE: who, where, relation to the Depot massacre | narrative task before S3 |
| - | Hero story per map (hand-authored) | later |
