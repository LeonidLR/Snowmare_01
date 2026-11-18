# Review log - BUILDINGS_TECH_SPEC.md

A technical specification, not a GDD: reviewed for completeness, consistency and implementability only (the 8 GDD sections
do not apply). Skill `/design-review` applied by hand (helper scripts are not installed). The spec itself was not edited.

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: XL (cross-cutting: TS generator + validator, Python builder, kit, C++ runtime R1-R4, page, tests; producer should
verify before sprint planning)
Specialists: none (lean mode)
Blocking items: 3 | Recommended: 9
Summary: Sections 8-15 of BUILDINGS_GDD.md are carried over completely (pipeline, contract types, validator codes, builder
rules, 9 runtime rows, page, tests, 9 phase rows, Q12). An agent cannot implement S1 from it yet: the degradation mechanism and
the collision / navigation behaviour of the spawned state actors are not specified, the data contract misses or under-defines
elements that S1 stories need, and the validator changes are not listed and do not fit the current validator's types.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Sections 3-4 (builder, runtime, safe degradation): the mechanism is not specified. The builder spawns leaf / pane / bars actors per state, and the project navmesh is `RuntimeGeneration=Dynamic`, so a spawned leaf with collision closes the doorway in the navmesh and a pane blocks the mantle trace through a window. State which layer degrades (the builder spawns a non-blocking or no actor until a feature is on, or a runtime default), the collision and navigation settings per module, and `closed` (missing from the list; "open door ... exists?" must be answered - `Source/` has `UDoorwaySubsystem` and the checkpoint `AGateActor`, no door leaf).
- [BLOCKING] Section 2 (data contract) is incomplete for S1: (a) `story` is singular although two stories are allowed; (b) there is no element for the snow ramp (story 10, in S1) or the rubble ramp (12); (c) `FireEscape` has no position / foot, every stop requires a `balcony_id` although the ground and roof stops have none, and its relation to `layout.ladders` (layer names `floor:<id>:<n>` / `roof:<id>` that the validator, the bot route and the builder use) is not stated; (d) the coordinate frame of `Balcony.rect` (an axis-aligned pair cannot be world coordinates on a rotated building), `BreachPanel.at` and `BuildingLink.at` (which also lacks a floor) is not stated.
- [BLOCKING] Sections 2 / 6 (validator): the S1 validator changes are not listed and the new codes do not fit the current type. `ok = issues.length === 0` with no severity, so `story_rolled_back` (informational) and the detour-band warnings would fail the gate; `no_exterior_door` and `checkBuildingReachability` take doors only as entries (stories 10 and 20 enter by a window or a ramp); blocked and `down_only` stairs count as edges today; `blocked_flight_no_bypass` is defined in no GDD. Fix: a "validator changes" list and a `severity` field (ok = no errors).
- [RECOMMENDED] Section 1 (pipeline order): the story (step 5) is validated before the default opening states (6) and the breach panels (7) exist, while opening-states says "defaults, then overridden by the story". Put the defaults before the story, validate the base (open) building before any story, and state the relation to BUILDING_GENERATOR.md 3 step 9 (regenerate x4 -> closed shell) versus the story fallback (-> open).
- [RECOMMENDED] Section 4 (runtime): "navmesh runtime generation must be enabled" is stale - it already is Dynamic; what remains is measuring the tile rebuild after a panel swap on X6. Add: how the baked tactical JSON (`tactical/*.tactical.json`) follows a breach; save / load of runtime opening states; which trace channels the bars let through.
- [RECOMMENDED] Section 4 vs BUILDING_GENERATOR.md 5: `LevelGenBuildingsSmoke` is specified there as navmesh paths to every room plus the ramp legs; here it "walks every building's key route" (which contains actions: kick, vault, shatter). Say which, and which legs the bot can perform before R1-R2.
- [RECOMMENDED] Section 3 (kit): no modules for the snow / rubble ramp, the plank, the roof bridge or the elevator rungs; `stair_flight_150` (120 wide, rise 150) in contract 7.2 does not match the implemented flight (150 wide, rise 300).
- [RECOMMENDED] Section 6 (tests): "several seeds" and "+- 10 %" - use the GDD numbers (5 seeds, pooled, points); the smoke thresholds (>= 90 % of legs, >= 95 % of buildings, "must not FAIL") need one definition.
- [RECOMMENDED] Section 7 (phases): S1 depends on S2 items (flats for story 4, hospital / school for story 5) - list the archetypes S1 covers; give `building_stories.json` and `building_route_costs.json` a schema id like the other levelgen data.
- [RECOMMENDED] Cross-references: the five GDDs cite this spec by the old BUILDINGS_GDD.md section numbers (9-13, ten places); the spec is numbered 1-8. Renumber here or fix the references there.
- [RECOMMENDED] Header: BUILDINGS_GDD.md carries no "superseded by" note and BUILDING_GENERATOR.md still links to it as the design + technical spec - two sources of truth until one is retired.
- [RECOMMENDED] Section 3 (tags): door / window state by actor tag strings (`door_state=locked`) - name the runtime reader (subsystem or component) and where the tag grammar is defined, so the builder and the C++ parse the same strings.

Completeness: 8 sections (pipeline, data contract, builder + kit, runtime, page, tests + gates, phases, owners) - all of the
source's part B section 8 and parts C-D are present.

Consistency with the GDDs and the code (checked by reading): the type names and fields equal BUILDINGS_GDD.md section 9; the
existing `Stair.kind` is `'stairwell' | 'external'` and `GenBuilding` has `openings`, `hatches`, `floor_holes`, `roof_holes`
(extensions do not collide); the validator codes extend `TraversalCode` in `buildingTraversal.ts`; owners agree with the role
table. Conflicts are the findings above.

Content preservation (BUILDINGS_GDD.md sections 8-15): nothing missing - 9 pipeline steps, the whole contract block, 4 validator
codes, 7 builder rules, 9 runtime rows, the degradation rule, page items, 6 test lines, 9 phase rows, Q12. Changed: S1 state
"next" is now "next - not started until the designer / studio says so"; S4 "after the studio decisions on section 5" now points
at breach-panels Q4-Q6. Added (not in the source): "building links are applied after every building's story is final", the
tuning-data table (`building_route_costs.json` is new), the map-summary line, the process constraints, the owners table. The
provenance quote of the source header («обсудим на уровне студии, создадим нормальный гейм-дизайн документ…») is not in the
new set.

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
8b99076b63452ded4098e10894977c5046c89b8894519214b89403950808aec8  docs/design/levelgen/BUILDINGS_TECH_SPEC.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
4541b9f6781306b1865787a852133d3b9bdd7a7f69e0a04ce1ca6a0b3e62e3ab  Tools/TrainingGround/src/levelgen/buildingTraversal.ts
c4eca2f6bd3a9a4693721c9edc5158437525ee7a9a53838635b29d469af7074b  docs/design/levelgen/gdd/systems-index.md
913da2246354cc826bd23daab5e887cea226ab35c36918dea30d6fdc462190e1  docs/design/levelgen/gdd/building-archetypes.md
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
5ac121ddeca7ea59ca187b3f2dea5b2a7e357d946df71df14f8736f69707935a  docs/design/levelgen/gdd/breach-panels.md
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
