# ADR-0003: Runtime representation of door / window states

## Status
Proposed

## Date
2026-11-18

## Last Verified
2026-11-18 (`DoorwayBreachRules.h`, `VaultNavigation.h`, builder door / window emission)

## Decision Makers
Claude (building generator owner; C++ gameplay by the role table); technical director view from the studio review.

## Summary
Each opening state maps to one of three runtime representations - **free** (no collision, no nav effect), **priced** (passable,
nav area with an entering cost), **blocked** (collision + `NavArea_Null`). Until a runtime feature lands, states degrade to the
safe representation, so the builder can emit states before R1 / R2.

## Engine Compatibility
UE 5.8.2; `UPrimitiveComponent::SetCanEverAffectNavigation`, `UNavModifierComponent`, nav areas per ADR-0001.

## ADR Dependencies
ADR-0001 (nav areas / filters), ADR-0002 (schema `/2` defaults).

## Context

### Problem Statement
A spawned door leaf with pawn collision cuts the dynamic navmesh; before R1 nobody can open it, so a `closed` door would block
the building the validator calls reachable. Bars must block movement but not sight; boards both.

### Constraints
No runtime code for doors / glass yet; the data must not change when R1 / R2 land.

### Requirements
opening-states sections 3-5; TECH_SPEC sections 3-4.

## Decision

| State | Before the feature | After the feature (R1 doors, R2 glass, R3 breach) |
|---|---|---|
| door `open` | free (frame only) | free |
| door `closed` | free: leaf visual only, `CanEverAffectNavigation = false`, no pawn collision | priced: leaf with an open action, nav area entering cost `t_open` |
| door `locked` | free (as closed) | priced: `UNavArea_LockedDoor` (kick / shotgun cost); enemies excluded until they can breach |
| door `barricaded` | blocked | blocked (charge opens it -> free) |
| door `sealed` | blocked (wall fill) | blocked |
| window `broken` | priced: existing vault path | priced |
| window `glazed` | priced as broken (pane visual, no pawn collision) | priced: vault + shatter cost, pane swapped to broken on the first pass / shot (mesh swap) |
| window `barred` | blocked: pawn collision, no visibility channel block | same |
| window `boarded` | blocked: collision + visibility | same (later: tear off -> broken) |
| window `fixed`, loophole | blocked: collision | same |
| breach panel `intact` | blocked (wall) | blocked until damaged -> free hole, tile rebuild |

Every leaf / pane / panel actor carries the tag `door_state=*` / `window_state=*` / `breach_panel=<id>` so the runtime finds it.
The build report lists degraded states per map.

## Alternatives Considered

### Alternative 1: Spawn nothing until the feature exists
No navmesh risk; but no visual tells, and the art kit / readability work cannot start. Rejected.

### Alternative 2: Full collision now, NavLinks through every closed door
Correct for the AI; but links through hundreds of doors cost nav data and still need an open action for the player. Rejected
for now; revisit with R1.

## Consequences

### Positive
States ship in data and visuals now; the route model prices the designed state; no navmesh holes from props.

### Negative
Until R1 a `locked` door is walkable - the player can walk through a visible lock plate (accepted, reported per map).

## Risks
A builder change forgetting `CanEverAffectNavigation = false` on a leaf: caught by `LevelGenBuildingsSmoke` (unreachable room
behind a closed door) and a builder unit test on the flag.

## GDD Requirements Addressed
opening-states (sections 1-5), breach-panels (rules 2, 6), building-route-model (rule 3).

## Performance Implications
Leaves and panes are actors (counted in the ADR-0002 budget of 40 non-instanced actors per building).

## Migration Plan
S1 emits states with the "before" column; R1 / R2 / R3 switch each row to the "after" column without changing layouts.

## Validation Criteria
Builder unit: closed / locked leaves have no nav effect; X6 smoke: no room unreachable because of a leaf; R1: kicking a locked
door costs `t_kick` and emits the `N_kick` noise.

## Related
[opening-states.md](../design/levelgen/gdd/opening-states.md), [breach-panels.md](../design/levelgen/gdd/breach-panels.md),
ADR-0001, ADR-0002.
