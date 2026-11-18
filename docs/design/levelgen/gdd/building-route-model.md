# Building Route Model

> **Status**: Draft v2 (studio discussion; reworked after `/design-review` + the 4-role studio review, 2026-11-18)
> **Author**: Claude (building generator owner)
> **Last Updated**: 2026-11-18
> **Implements Pillar**: 1 Non-linear, never impossible; 3 Costs, not walls (see [systems-index.md](systems-index.md))
> **Source**: BUILDINGS_GDD.md section 7 (commit 9210a435); traversal gate BUILDING_GENERATOR.md section 5;
> validator `Tools/TrainingGround/src/levelgen/buildingTraversal.ts` (P0, *done*).

## Overview

The route model turns one building and its surroundings into a weighted graph and answers four questions for the generator,
the page, the bot and the PMC AI: **is everything reachable** (every room, storey and walkable roof, without explosives and
without drops), **what is the key route** (the cheapest walkable way from the street to the top), **is there a real choice**
(do the fast and the quiet routes differ), and **how much longer did the story make it** (the detour ratio, a report).
Nodes are rooms, landings, balconies, the roof and the street; edges are doors, windows, flights, ladders, hatches, fire
escapes, links to neighbours and breach panels, each with a mode and a cost.

**The TS graph is a generation-time proxy; the engine navmesh is the runtime truth** (technical director, ADR-1 in
BUILDINGS_TECH_SPEC). One cost table (JSON) is read by both the TS generator and the C++ runtime; the in-engine smoke
measures the difference per building, so the proxy cannot silently drift from what the operatives can actually walk.

## Player Fantasy

There is always a way up, but the obvious one is not always the best or even open. The player weighs "kick the front door and
run up the core" against "round the back, through the broken window, up the fire escape" and both are real, priced options.
The model is invisible to the player; what they feel is that every building has a puzzle with more than one answer and
never a dead end that strands the squad.

## Detailed Rules

1. **Graph per building** (and its outside ring): nodes = rooms / corridor cells / landings / balconies / roof / **street**;
   edges = openings ([opening-states.md](opening-states.md)), flights, ladders, hatches, fire escapes, building links,
   breach panels ([breach-panels.md](breach-panels.md)), outside legs.
2. **Street node and outside legs**: every exterior opening (door, ground window, snow-ramp window, escape ground stop) has an
   *approach cell* 150 cm outside it. The street node is the point of the layout's main route (`R_main`) or road graph nearest
   to the building's footprint; if there is none, a point 10 m out from the middle of the street facade. Outside legs join
   the street node and the approach cells along the footprint perimeter offset by 150 cm (rectilinear), length = that path.
3. **Edge modes**: `walk`, `open`, `kick`, `shotgun`, `vault`, `shatter`, `climb` (ladder / hatch), `explosive`, `drop`.
   Each edge has a cost triple (time, noise, explosives) and an exposure flag (outside legs, fire escapes, balconies, planks,
   roof bridges). Blocked states produce no edge. Blocked flights (`stair.blocked`) produce no edge.
4. **Direction**: `drop` edges (floor holes onto a rubble heap, roof edges <= 250 cm onto a landing box) and `down_only`
   fire escapes exist only downwards.
5. **Reachability (hard rule, same as the validator)**: every room, every walkable storey and the walkable roof is reachable
   from the street using only edges with `x = 0` and no `drop`. No archetype is exempt (ruins included: a ruin's room may
   also be reached by a drop, but never only by one). Checked by the TS validator before «Принять».
6. **Key route** = the cheapest street -> top route (F2) under the `balanced` profile **on the walkable graph** (rule 5
   edges only: no explosives, no drops) - so the bot smoke can always walk it. "Top" = the walkable roof if any, else the
   highest storey's room farthest (by graph cost) from the street. Stored as legs (`from`, `to`, `mode`) with cost and `D`.
   A separate **breach route** (explosive edges allowed) is stored only when it is cheaper; it is a shortcut, never walked by
   the gate.
7. **Choice criterion** (studio review 2026-11-18, replaces "D in band" as the acceptance test): a building with a *choice
   story* passes only if its `fast` and `quiet` key routes enter by **different exterior openings** (F4). *Detour stories*
   (one forced bypass) pass only on buildings whose role pays it off (objective, garrison, overwatch -
   [building-stories.md](building-stories.md) section 1).
8. **Rollback**: a story whose recipe breaks rule 5 (or its rule 7 test) is undone; the generator tries the next story with a
   deterministic sub-seed (max 4 candidates), then the building keeps the `open` story.
9. **Squad**: the cost is computed for `n_squad` operatives (F1); solo is the primary play mode, so the default is 1 and the
   page also reports `n_squad = 3`.
10. **Determinism**: same (seed, building id, cost-table hash) -> same graph, same key route.

## Formulas

### F1. Edge cost (scalar, per profile)

```
t1(e)  = len(e) / v_ref + t_action(e)                       seconds, one operative
           ladders / hatches: rungs(e) * t_rung instead of len / v_ref
t(e)   = n_squad * t1(e)                         if e is single-file (ladder, hatch, window vault, fire-escape ladder)
         t1(e) + (n_squad - 1) * t_follow        otherwise (flights >= 150 wide, doors >= 120, open ground)
exp(e) = t(e) * (1.0 if e is outside above storey 0, 0.5 if outside at ground, 0 inside)
C(e)   = t(e) + a_n * n(e) + a_x * x(e) + a_e * exp(e)
```

| Variable | Meaning | Default (*proposal* unless noted) | Range |
|---|---|---|---|
| `len(e)` | walking length, m (flights along the slope; outside legs by rule 2) | geometry | - |
| `v_ref` | reference walk speed | 3.0 m/s (`BotWalkSpeedCm` 300, PlaytestBotSubsystem) | 2-4 |
| `t_action` | opening action time (opening-states F1) | per state | - |
| `t_rung` | ladder time per rung | 0.9 s (measured: 8 rungs 7-9 s, 24 rungs 21-22 s, audit 1.4) | 0.7-1.2 |
| `n_squad` | operatives moving together | 1 (solo primary); report 3 | 1-6 |
| `t_follow` | extra seconds per following operative on a wide edge | 0.5 s | 0-2 |
| `n(e)` | noise radius, m | per state | - |
| `x(e)` | explosives used | 0 / 1 | - |
| `a_n` | seconds charged per metre of noise | balanced 0.5, quiet 3, fast 0.1 | 0-5 |
| `a_x` | seconds charged per explosive | balanced 60, quiet 120, fast 20 | 10-300 |
| `a_e` | weight on exposed seconds | balanced 1, quiet 1, fast 0.2 | 0-3 |

Example (solo): kicking a locked front door (`t_kick` 3 s, `N_kick` 15 m) under `balanced` costs 3 + 0.5 * 15 = 10.5
s-equivalent; going 20 m further round the outside at ground (exposure factor 0.5) to a broken back window costs
6.7 + 1 * 3.3 + 1.2 = 11.2. `balanced` slightly prefers the kick (10.5 < 11.2); `quiet`: kick 3 + 3 * 15 = 48 vs window 11.2
-> window; `fast`: kick 3 + 0.1 * 15 = 4.5 vs window 6.7 + 0.2 * 3.3 + 1.2 = 8.5 -> kick. Fast and quiet enter by different
openings: a real choice by F4.

### F2. Key route

```
key_route(profile) = argmin over walkable paths P(street -> top) of  C(P) = sum over e in P of C(e)      (Dijkstra)
key_route          = key_route(balanced)
```

Ties break by fewer outside legs, then fewer actions, then edge id order.

### F3. Detour ratio (report)

```
D = C(key_route) / C_open(key_route_open)
```

`C_open` is the key-route cost of the **same building with its story removed** (every opening at its default state, every
flight unblocked, no story bricks). `D = 1` for `open`; `D < 1` only when a story adds a shortcut (links, breaches) - reported
as "shortcut". Bands, report-only: open / pass-through 1.0-1.3, storied 1.5-4.0, > 4.0 = warning (tedious).

### F4. Choice test

```
choice(b) = 1 if entry(key_route(fast)) != entry(key_route(quiet)), else 0
entry(P)  = the first exterior opening on P
```

Map report: `choice_share = sum choice(b) / buildings with a choice story`; target 100 % (a choice story that fails F4 is
rolled back).

### F5. Engine-vs-proxy drift (ADR-1)

```
drift(b) = | C_engine(key_route) - C(key_route) | / C(key_route)
```

`C_engine` = the same legs timed by `LevelGenBuildingsSmoke` in engine (navmesh path length / v_ref + measured actions).
Gate: `drift(b) <= 15 %` for >= 95 % of buildings; a larger drift is a proxy bug, not a level bug.

## Edge Cases

- **If a storey or room is reachable only through a breach panel**: rule 5 fails -> the story rolls back
  (`unreachable_without_explosive`).
- **If a storey or room is reachable only by a drop**: rule 5 fails (drops are shortcuts, never the only way).
- **If two routes cost the same**: tie-break of F2.
- **If the building has no walkable roof**: "top" = the highest storey's room farthest from the street.
- **If a fire escape is `down_only`** (iced, story 11): it is an exit, never part of an upward key route.
- **If a degraded state is in play** (e.g. `locked` behaving as `open` before R1): the model prices the designed state; the
  runtime report lists the degradation; F5 drift excludes degraded edges until their feature lands.
- **If a building link joins two buildings** (shared wall hole, plank, roof bridge): the graphs merge for reachability; each
  building keeps its own street node and key route.
- **If there is no `R_main` / road graph near a building** (map edge): the street node falls back to 10 m off the street
  facade (rule 2); the street facade is the side with the most exterior doors.
- **If a choice story's fast and quiet routes coincide**: F4 fails, the story rolls back.
- **If no story candidate passes after 4 tries**: the building keeps `open`; `story_rolled_back` is logged per try.
- **If the cost table changes**: stored key routes are stale; they carry the table hash and are recomputed on load
  (BUILDINGS_TECH_SPEC section 2).

## Dependencies

| System | Direction | Interface |
|---|---|---|
| [building-archetypes.md](building-archetypes.md) | upstream | rooms, storeys, circulation elements = nodes / edges |
| [opening-states.md](opening-states.md) | upstream | edge costs (F1 there), blocked = no edge |
| [breach-panels.md](breach-panels.md) | upstream | `explosive` edges, breach route |
| [building-stories.md](building-stories.md) | downstream | accept / roll back each recipe; roles; F4 |
| `buildingTraversal.ts` (P0) | implementation | reachability, approach boxes, gap overshoot |
| `LevelGenBuildingsSmoke` (C++) | downstream | walks every key route in engine, measures F5 |
| PMC AI (PMC_DEFENCE.md) | downstream | the same cost JSON as navmesh area costs / query filters (ADR-1) |
| BUILDINGS_TECH_SPEC sections 2, 5, 6, 8 | downstream | `key_route` field, page, tests, ADRs |

## Tuning Knobs

| Knob | Default | Range | Where |
|---|---|---|---|
| `v_ref` | 3.0 m/s | 2-4 | `building_route_costs.json` |
| `t_rung` | 0.9 s | 0.7-1.2 | `building_route_costs.json` |
| `n_squad`, `t_follow` | 1, 0.5 s | 1-6, 0-2 | `building_route_costs.json` |
| Profile weights `a_n`, `a_x`, `a_e` | see F1 | see F1 | `building_route_costs.json` |
| Outside exposure factors | 1.0 above ground / 0.5 ground | 0-1.5 | `building_route_costs.json` |
| Detour bands (report) | 1.0-1.3 / 1.5-4.0 | - | `building_route_costs.json` |
| Max story candidates | 4 | 1-8 | generator |
| Drift gate | 15 % | 5-25 % | smoke |

## Visual/Audio Requirements

None in game (the model is invisible). Page: the key route as a line on the plan view.

## UI Requirements

Training Ground «Здания» panel: key route as a readable chain (e.g. "окно Б (разбить) -> кв. 2 -> балкон -> пожарная
лестница -> 2 этаж"), its cost, `D`, the fast / quiet entries and the choice flag; per map the choice share, band shares and
the drift after a bot run (BUILDINGS_TECH_SPEC section 5).

## Acceptance Criteria

- [ ] Unit: on the F1 fixture (locked front door, broken back window 20 m round) `quiet` enters by the window, `fast` by the
      door, F4 = 1.
- [ ] Unit: `D = 1.0` for every building with the `open` story.
- [ ] Unit: a fixture whose only up-route is a breach panel fails reachability; adding a fire escape makes it pass, the key
      route contains no explosive edge, the breach route is stored and is cheaper.
- [ ] Unit: `drop` and `down_only` edges never appear in a key route.
- [ ] Unit: every room unreachable without a drop is reported, ruins included.
- [ ] Determinism: same seed + cost hash -> byte-equal key routes on x1 / x6.
- [ ] Generated x1 / x6 maps (5 seeds pooled): reachability 0 failures; choice share = 100 % of choice-story buildings.
- [ ] Bot smoke: `LevelGenBuildingsSmoke` walks every key route, PASS >= 95 % of buildings on X6; drift <= 15 % for >= 95 %.
