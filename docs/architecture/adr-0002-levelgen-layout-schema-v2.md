# ADR-0002: Level-generator layout schema `/2` and building budgets

## Status
Proposed

## Date
2026-11-18

## Last Verified
2026-11-18 (`Tools/TrainingGround/src/levelgen/schema.ts`, `Scripts/Editor/build_levelgen_map.py`)

## Decision Makers
Claude (building generator owner); technical director view from the studio review 2026-11-18.

## Summary
Bump the layout schema from `codex.levelgen_layout/1` to `/2` for the building stories (S1): new defaults for opening states,
`stories[]`, roles, districts, snow ramps, fire-escape positions, stored routes with a cost-table hash that are recomputed on
load. `/1` layouts keep loading with the old meaning and build bit-identically. Fix the performance budgets the S1-S4 work must
stay inside.

## Engine Compatibility
Engine-independent data contract; read by the TS generator, the Python builder (editor commandlet) and the C++ runtime
(`LevelJsonRules`, level props). UE 5.8.2.

## ADR Dependencies
ADR-0001 (one cost table; routes are recomputed, the navmesh decides at runtime).

## Context

### Problem Statement
In `/1` a missing door / window `state` means `open` / `broken`. The opening-states GDD changes the default (exterior doors
`closed`, ground windows `glazed`) - the same JSON would mean different buildings. Stored key routes and detour ratios go
stale whenever the cost JSON is tuned.

### Constraints
- Accepted `/1` layouts (X2, X4, X6, Train 1-3) and the designer's in-progress X6 edits must build unchanged.
- Determinism: same seed + inputs -> byte-equal JSON.

### Requirements
TECH_SPEC section 2 (fields), opening-states section 3 (defaults), building-route-model F5 / edge case "cost table changes",
building-stories rules 5-8.

## Decision
1. `schema: 'codex.levelgen_layout/2'` for every layout the S1 generator writes; readers branch on the schema string for the
   default table (`/1` = open / broken, `/2` = opening-states section 3).
2. New fields exactly as TECH_SPEC section 2 (`cost_hash`, `districts`, `role`, `stories[]`, `snow_ramps`, `fire_escapes.at /
   ground / ladder_ids`, `breach_panels.score / wide_with`, `key_route`, `breach_route`, `fast_entry`, `quiet_entry`), all in the
   building's local frame.
3. `cost_hash` = sha1 of `building_route_costs.json`; a stored route whose hash differs is recomputed on load (page) and
   ignored by the builder; the runtime never reads stored routes for movement (ADR-0001).
4. Determinism: floats rounded to 0.1 cm, object keys sorted on write, CI test compares 5 seeds byte-for-byte.
5. Budgets (measured on X6, *proposal*):

| Budget | Value |
|---|---|
| Navmesh tile rebuild after a breach / door flip | <= 2 ms per frame, time-sliced; path ready < 1 s |
| Non-instanced actors per building | <= 40 (leaves, panes, panels, ladders); walls / slabs / rails instanced |
| X6 navmesh build at load | baseline before S1; S1-S3 <= baseline + 20 % |
| X6 generation in the page | <= 5 s |

## Alternatives Considered

### Alternative 1: Keep `/1`, add fields as optional
Zero migration work; but the default change silently reinterprets old layouts (doors appear closed, windows glazed). Rejected.

### Alternative 2: Write explicit states on every opening, no defaults
Unambiguous; but every layout grows by thousands of fields and the default table moves into the generator only, out of the
data's meaning. Kept as a fallback if readers disagree.

## Consequences

### Positive
Old maps are safe; tuning costs never leaves wrong routes in a file; budgets are agreed before the work.

### Negative
Two code paths (schema branch) in three readers until `/1` layouts are regenerated.

## Risks
A reader forgetting the schema branch - covered by a unit test per reader on one `/1` and one `/2` fixture.

## GDD Requirements Addressed
opening-states (defaults), building-route-model (stored routes, determinism), building-stories (`stories[]`, roles),
building-archetypes (instancing budget).

## Performance Implications
See the budget table; schema size grows by the new fields only on buildings that use them.

## Migration Plan
No migration of `/1` files. New maps are generated as `/2`. The designer's X6 stays `/1` until he decides to regenerate it.

## Validation Criteria
Legacy bit-identical build test passes on all accepted `/1` layouts; `/2` round-trip unit; determinism CI on 5 seeds; budgets
measured on the first S1 X6.

## Related
[BUILDINGS_TECH_SPEC.md](../design/levelgen/BUILDINGS_TECH_SPEC.md) sections 2 and 6, ADR-0001, ADR-0003.
