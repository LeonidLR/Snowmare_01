# ADR-0001: The engine navmesh is the runtime truth for building routes

## Status
Proposed

## Date
2026-11-18

## Last Verified
2026-11-18 (UE 5.8.2, `Config/DefaultEngine.ini`, `Source/CodexTactics/Public/{Falls,Ladders,Interactables}/*Navigation.h`)

## Decision Makers
Claude (building generator owner); technical director view from the studio review 2026-11-18; designer go-ahead 2026-11-18.

## Summary
The Training Ground TS route graph is a **generation-time proxy**. In game, building routes are what the Recast navmesh says:
edge modes become nav areas and nav links, route profiles become navigation query filters, and the PMC AI queries the navmesh
instead of a second graph. One cost table is read by the TS generator and the C++ runtime; an in-engine smoke measures the
drift between the two per building.

## Engine Compatibility
UE 5.8.2. Navmesh `RecastNavMesh`, `RuntimeGeneration=Dynamic` (`DefaultEngine.ini:77`), agents Default (r 35, h 144) and
Vehicle. Existing project patterns this follows: `UNavArea_Drop` / `UDropLinkSubsystem` (one-way smart links with entering
costs), `UNavArea_Ladder`, `UNavArea_Vault`, `UNavFilter_NoVault` / `UNavFilter_NoVaultLadder`. No engine change, no plugin.

## ADR Dependencies
None (first ADR). ADR-0002 (schema /2) and ADR-0003 (opening states at runtime) build on it.

## Context

### Problem Statement
The 2026-11-18 audit found a green static check while only 3 of 24 stairs on X6 could be walked: the geometry the navmesh
builds is not the graph the generator imagines (a 40 cm slab-gap overshoot, rotated halls losing navmesh). The building GDDs
add priced edges (kick, shatter, explosive, outside exposure) that the generator, the bot smoke and the PMC AI all need. Three
copies of the costs (TS graph, stored `key_route`, AI) would drift.

### Constraints
- The TS generator must stay instant and engine-free (page preview, «Принять» gate).
- The runtime must not depend on generator-only data that can go stale.
- Existing nav areas / filters (drop, ladder, vault) keep working.

### Requirements
- building-route-model rules 5-6, F1, F5; opening-states section 5; breach-panels rule 6; TECH_SPEC section 6 (drift gate).

## Decision
1. **Edge modes -> nav representation**:

| Mode (GDD) | Runtime |
|---|---|
| walk, open (closed door before R1) | plain navmesh |
| kick / shotgun (locked door) | nav area `UNavArea_LockedDoor` on the doorway: `DefaultCost` 1, `EnteringCost` = route-model seconds from the cost JSON |
| vault, shatter (windows) | existing `UNavArea_Vault` path; shatter = vault + entering cost |
| climb (ladder / hatch / fire-escape ladder) | existing `UNavArea_Ladder` links |
| drop, down_only | existing `UNavArea_Drop` one-way links |
| explosive (intact breach panel) | NavModifier `NavArea_Null` until breached (a wall); no planning through it |
| blocked (sealed, barred, barricaded, blocked flight) | collision + `NavArea_Null` |

2. **Profiles -> `UNavigationQueryFilter`s**: `UNavFilter_RouteBalanced / Quiet / Fast` set area cost multipliers from the
   same profile weights (`a_n`, `a_x`, `a_e`) the generator uses.
3. **One cost table**: `Content/Data/LevelGen/building_route_costs.json`, parsed by TS (`buildingTraversal.ts`) and by C++
   (a small `FBuildingRouteCosts` loader in `LevelGen/`); the nav areas read their costs from it at startup.
4. **Drift gate**: `LevelGenBuildingsSmoke` times every key route in engine and writes `C_engine` next to the TS `C` per
   building (route-model F5); > 15 % on more than 5 % of buildings fails the build check.

### Key Interfaces
`FBuildingRouteCosts::Get()` (costs by action), `UNavArea_LockedDoor`, `UNavFilter_Route*`, smoke JSON
`Saved/LevelGen/buildings_smoke_<map>.json` (`building`, `pass`, `first_unreachable`, `c_ts`, `c_engine`, `drift`).

### Implementation Guidelines
New areas follow the `UNavArea_Drop` header style (designer-tunable through `DefaultEngine.ini` overrides). Enemies' base
filters exclude `UNavArea_LockedDoor` until R1 lets them kick.

## Alternatives Considered

### Alternative 1: TS graph is the truth, the runtime follows stored `key_route`s
Simple to start; but the stored route is blind to runtime changes (breaches, opened doors) and repeats the audit failure mode
(a route that exists on paper only). Rejected.

### Alternative 2: Custom C++ building graph beside the navmesh
Full control over costs; but a second pathfinder to keep in sync with collision, two places to debug, and the AI would need
both. Rejected.

## Consequences

### Positive
One truth for movement; breaches and doors change routes automatically; the drift number catches proxy bugs early.

### Negative
Nav area / filter classes to add (small); the TS proxy must model what Recast does closely enough to stay under 15 %.

## Risks
- Entering costs on doorway areas may make units hesitate at thresholds - tune in the smoke.
- Dynamic navmesh rebuild cost (breaches) - covered by the R3 spike and ADR-0002 budgets.

## GDD Requirements Addressed
building-route-model (rules 5, 6, F1, F5), opening-states (section 5), breach-panels (rule 6), building-stories (AC bot walk).

## Performance Implications
Nav areas and filters cost nothing per frame; area changes on door state flips dirty one tile (time-sliced rebuild).

## Migration Plan
Existing drop / ladder / vault areas unchanged. New areas land with R1 (doors) and R3 (breaches); before that the builder emits
closed / locked leaves without navigation impact (ADR-0003).

## Validation Criteria
`LevelGenBuildingsSmoke` on X6: PASS >= 95 % of buildings, drift <= 15 % for >= 95 %; the same profile picks the same entry in
TS and engine on the route-model F1 fixture.

## Related
[building-route-model.md](../design/levelgen/gdd/building-route-model.md),
[BUILDINGS_TECH_SPEC.md](../design/levelgen/BUILDINGS_TECH_SPEC.md) section 8, ADR-0002, ADR-0003.
