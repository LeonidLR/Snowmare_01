# Review log - building-route-model.md

Skill `/design-review` applied by hand (helper scripts are not installed; the same checks were done by reading). The GDD itself
was not edited. Systems-index status left as is (recommended: `Needs Revision`).

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: L (4 formulas, 8 dependencies, validator + page + bot smoke + PMC AI; producer should verify)
Specialists: none (lean mode)
Blocking items: 3 | Recommended: 9
Summary: All 8 required sections are present; the cost formula is dimensionally sound and its worked example checks out
(6 vs 7.9 under `balanced`, 21 under `quiet`). Three things stop implementation: the hard reachability rule is weaker than the
validator it claims to describe (rooms), the key route may contain explosive and down-only edges although a bot has to walk
it, and the start of the route is not defined, so F1-F3 cannot be evaluated.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Detailed Rules 4 vs `buildingTraversal.ts` / BUILDING_GENERATOR.md 5: the hard rule covers storeys and the roof only; the validator (`unreachable_room`) and the traversal gate require every room. Once stories seal or barricade interior doors the GDD must say whether a room reachable only by an explosive (or a drop) is allowed. The ruin edge case ("drop-only links allowed for rooms that are not a storey's sole access") contradicts `validateBuilding`, which has no ruin exemption. Fix: state the room rule; add the exemption to the validator spec or delete the edge case.
- [BLOCKING] Detailed Rules 5-6 / Acceptance Criteria 3, 4, 7: the key route is computed with `explosive` edges and nothing excludes down-only edges, yet it is "walked by the bot smoke" and D is derived from it. A bot cannot blow a panel (degraded = a wall) and drops are unreliable for the mover (audit cause F). Fix: key route = the cheapest path on the reachability graph (x = 0, no drop); report the breach / drop alternative separately (it feeds breach-panels F1). AC3 "the breach shortens the key route" cannot hold under `balanced` (a_x 60 + 0.5 x 25 = 72.5 s-equivalent for the panel edge).
- [BLOCKING] Formulas F1-F3: the start is undefined. "street" is one node, but `len(e)` of the outside legs (the example's "20 m further round") needs a start point and the geometry of the outside ring; "top = the room farthest from the entry" needs "entry". Fix: define the start (for example the street-side approach point of the main door), the ring nodes (per facade / corner, offset from the wall) and "entry".
- [RECOMMENDED] Formulas F3-F4 vs the story catalogue: under "cheapest route" many stories give D close to 1.0 (8 barred ground floor, 9 broken back windows, 11 iced escape, 16, 17; 3 locked door about 1.2-1.5 because the kick costs 6 s-equivalent). The target `share_band(1.5-4) >= 70 %` is unlikely to be reachable. Compute D per story on a fixture before fixing the targets, or split the stories into route-shaping and entry-shaping and band only the first group.
- [RECOMMENDED] Formulas F3: stories that add edges (2 pass-through, 14, 18) give D < 1; a story that adds the top node (16, 7, 4) makes numerator and denominator end at different "top" nodes. Define both cases. Rule 8's band 1.0-1.3 for `open` contradicts "D = 1 by definition".
- [RECOMMENDED] Detailed Rules 2: the mode list has no `drop` (down-only edges) and no mode for the balcony step-over, the plank or the ramps; `key_route.legs[].mode` needs the closed enumeration. Permissions per agent class (operative / human enemy / mutant) are missing although the PMC AI is a consumer (LADDERS.md: mutants never climb).
- [RECOMMENDED] Formulas F1 (`exp`): is walking on a roof "outside and above storey 0"? The outside ring on storey 0 is never exposed although pillar 3 prices exposure. State both.
- [RECOMMENDED] Detailed Rules 3 vs MANTLING.md / contract 7.1: a floor hole is a 300 cm drop, above the 250 cm controlled-drop limit ("no storey is a controlled drop"). Say what a floor-hole edge is (it needs an intermediate <= 250, otherwise there is no edge).
- [RECOMMENDED] Formulas F1 table: `v_ref`, `t_rung`, `a_n`, `a_x`, `a_e` and the three profiles are new numbers without a *proposal* mark; the F4 targets and the bands carry one - mark them alike.
- [RECOMMENDED] Acceptance Criteria 4: "down-only edges never appear in an upward key route" is ambiguous (every key route is upward) - rewrite as "no computed route traverses a down-only edge from its lower node to its upper node". AC6: pool the seeds (x6, 5 seeds) and state the map sizes. AC7 (>= 95 % of buildings) vs building-archetypes AC6 (>= 90 % of legs) vs BUILDING_GENERATOR.md 5 (the map is not published while the smoke FAILs) - one definition.
- [RECOMMENDED] Dependencies: breach-panels is "upstream" here and this GDD is "upstream" there (a cycle); the design order puts 3 before 4 - mark breach-panels as a downstream / optional input. The tech-spec section numbers are stale (9, 12, 13 are the old BUILDINGS_GDD.md numbers; now 2, 5, 6).
- [RECOMMENDED] Edge Cases (building links): "the graphs merge for reachability" - then a storey of building A may be reachable only through building B's stair (story 14 intends this). Say that explicitly and how the per-building verdict on the page is shown when B is regenerated or locked.

Completeness: 8/8 required sections present in the standard's order; extra template sections Visual/Audio, UI.

Dependency graph (all files exist): building-archetypes.md, opening-states.md, breach-panels.md, building-stories.md,
`buildingTraversal.ts`, PMC_DEFENCE.md, BUILDINGS_TECH_SPEC.md; `LevelGenBuildingsSmoke` (C++) does not exist yet (P0b, queued) -
flagged as a not-yet-built dependency.

Registry values: no registry. Cross-checked by reading: `v_ref` 3.0 m/s = `BotWalkSpeedCm` 300 (`PlaytestBotSubsystem.cpp`) ok;
`t_rung` 0.9 s agrees with audit 1.4 (8 rungs 7-9 s, 24 rungs 21-22 s) ok; `t_kick` / `N_kick` agree with opening-states; max 4
candidates agrees with building-stories `T_max`; the validator codes agree with BUILDINGS_TECH_SPEC section 2. Conflicts are the
findings above (rooms, ruin exemption, floor-hole drop, smoke thresholds).

Content preservation (BUILDINGS_GDD.md section 7): nothing missing - graph, 7 modes, reachability, key route, rollback, the 1.5-4
band, the per-map report. **Changed**: (1) the detour ratio was "key-route length / straight in-and-up length, outside legs and
actions counted"; it is now a cost ratio against the same building without its story (F3); (2) the edge cost was "(time, noise,
exposure)"; it is now a triple (time, noise, explosives) plus an exposure term. Added (not in the source): the three profiles
and their weights, `v_ref`, `t_rung`, the 1.0-1.3 band, the > 4.0 and < 1.5 warnings, the F4 targets (>= 70 %, <= 10 %), the tie
rule, the definition of "top".

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
4541b9f6781306b1865787a852133d3b9bdd7a7f69e0a04ce1ca6a0b3e62e3ab  Tools/TrainingGround/src/levelgen/buildingTraversal.ts
913da2246354cc826bd23daab5e887cea226ab35c36918dea30d6fdc462190e1  docs/design/levelgen/gdd/building-archetypes.md
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
5ac121ddeca7ea59ca187b3f2dea5b2a7e357d946df71df14f8736f69707935a  docs/design/levelgen/gdd/breach-panels.md
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
