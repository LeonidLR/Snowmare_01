# Review log - breach-panels.md

Skill `/design-review` applied by hand (helper scripts are not installed; the same checks were done by reading). The GDD itself
was not edited. Systems-index status left as is (recommended: `Needs Revision`; the GDD already waits for studio decisions).

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: XL (3 formulas, 9 dependencies, runtime destruction + navmesh + cover / LOS + weapons; an ADR on runtime
navigation / destruction is likely; producer should verify)
Specialists: none (lean mode)
Blocking items: 3 | Recommended: 9
Summary: All 8 required sections are present and BUILDINGS_GDD.md section 5 is carried over completely. The placement formula
as written never places a panel (the explosive penalty of the route model is larger than any detour inside a building), the
100-wide panel is self-contradictory, and the trigger that places a panel is defined twice with neither version executable.
Open questions Q4-Q6 are expected and are not counted as defects.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Formulas F1 / Acceptance Criteria 1-2: gain is measured in route-model cost, where an `explosive` edge costs a_x + a_n x N_charge = 60 + 0.5 x 25 = 72.5 s-equivalent under `balanced`. `gain >= G_min = 20` therefore needs a detour of about 92 s-equivalent (~280 m of walking; ~130 m even under `fast`). No building qualifies, so no panel is ever placed and AC1 ("the panel's gain >= G_min") cannot pass. F1 also covers only the key route, not rule 4's "joins two otherwise distant nodes". Fix: gain(p) = time-only cost between the panel's two linked nodes (`links`) without p minus through p (a_x = a_n = 0), evaluated on the final building.
- [BLOCKING] Formulas F3 / Detailed Rules 1-2 / Edge Cases: the 100-wide panel contradicts itself. It "cannot hold a 120 hole" and is "only one half of a wide breach", but a wide breach is a 240 hole (100 + 100 = 200 cannot hold it), and the edge case "a single charge opens one panel" would open the 100 half alone. The data contract still allows `width_cm: 100`. Fix: breachable width = 200 only (wide = 200 + 200), or define the 100 hole (clear width >= 80) and which panel a single hit opens.
- [BLOCKING] Detailed Rules 4 / pipeline step 7: who places a panel is defined twice and neither is executable - the brick `opening(wall, breach)` is used by no catalogue story, and the pipeline says "where the story wants a shortcut". Fix: one trigger - after the building's story is final, score candidate wall segments by F1 and keep the best `P_max`.
- [RECOMMENDED] Detailed Rules 6 / Edge Cases (navmesh) / Q6: the project already runs `RuntimeGeneration=Dynamic` (`Config/DefaultEngine.ini`: "levels ship without baked NavMesh ... updated at runtime"), so nothing has to be enabled and there are no baked tiles to "pre-bake both states" from. What remains is the tile rebuild time after the swap on X6. A cheaper fallback worth naming: build the hole open and keep the intact panel as a dynamic navigation obstacle (null area) that is removed on the breach.
- [RECOMMENDED] Detailed Rules 6: the baked tactical data (`Content/Data/LevelGen/tactical/*.tactical.json`: cover, chokepoints) does not know the breached state. Say whether it is recomputed, carries a per-panel delta from the generator, or is ignored near panels.
- [RECOMMENDED] Detailed Rules 3 vs WEAPONS_FRAMEWORK.md: "charge" and "big charge" are not weapons or deployables there (grenade, mines / tripwires, fuel barrel, RPG-7, mortar). Say what a charge is, and whether a grenade, a mortar shell or a barrel next to a panel really do nothing.
- [RECOMMENDED] Formulas F2: `perimeter_walls(b)` is not defined (panels may stand in interior walls); with a 10 m divisor the cap equals `P_max` for almost every building - simplify to `P_max` or define it per storey.
- [RECOMMENDED] Formulas F3: one rubble box (one side) or two? Give the side rule; a rubble height of 35 must not exceed the agent step height used by the navmesh.
- [RECOMMENDED] Edge Cases: "a unit in the rubble box: runtime pushes it out / applies blast damage" names two outcomes; units behind the panel (friendly fire through the wall) are not covered. Defer explicitly to R3 with an owner, or decide.
- [RECOMMENDED] Acceptance Criteria 4: "no frame over the profiling budget agreed in Q6" is not testable until the number exists - rewrite as "hitch <= N ms (N from Q6), measured on X6 with <tool>". The 1 s path criterion is testable.
- [RECOMMENDED] Detailed Rules: save / load of the breached state and the defenders' re-plan after a breach are not mentioned (PMC AI is listed as a consumer).
- [RECOMMENDED] Dependencies: route model is "upstream" here and this GDD is "upstream" there (a cycle; the design order puts the route model first). `N_charge` is defined here and in opening-states F1 - keep one owner. Tech-spec "section 10" is stale (the builder section is now 3).

Completeness: 8/8 required sections present in the standard's order; extra template sections Visual/Audio, UI, Open Questions.

Dependency graph (all files exist): building-archetypes.md, opening-states.md, building-route-model.md, building-stories.md,
DOORWAY_BREACH.md, `DoorwayBreachRules.h`, WEAPONS_FRAMEWORK.md, PMC_DEFENCE.md. "Navigation (dynamic navmesh)" and "Cover / LOS
systems" have no design document to point at - name the files.

Registry values: no registry. Cross-checked by reading: jamb distance 60 agrees with BUILDING_GENERATOR.md 4.4; head room 220
agrees with 4.1; module widths 100 / 200 x storey 300 agree with contract 7.2; `N_charge` 25 m agrees with opening-states;
`G_min`, `P_max` are named in BUILDINGS_TECH_SPEC section 2 (`building_route_costs.json`). Conflicts are the findings above
(F1 vs route-model weights, 100-wide panel, navmesh statement).

Content preservation (BUILDINGS_GDD.md section 5 + the runtime row of section 11): nothing missing - marked panels, the tells,
intact / breached, 120 x 220 hole with rubble, the wide breach, damage per weapon, the placement prohibitions, the triad space,
the two-graph validation, Q4-Q6. Changed: "only where the breach is a meaningful shortcut" is now formalised as F1 (gain >=
G_min) "or joins two otherwise distant nodes"; a 100-wide panel is now "only one half of a wide breach". Added (not in the
source): `G_min` 20, `P_max` 2, F2, the rubble box size, the 60 cm jamb distance, the edge cases.

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
5ac121ddeca7ea59ca187b3f2dea5b2a7e357d946df71df14f8736f69707935a  docs/design/levelgen/gdd/breach-panels.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
8b99076b63452ded4098e10894977c5046c89b8894519214b89403950808aec8  docs/design/levelgen/BUILDINGS_TECH_SPEC.md
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
