# Review log - building-stories.md

Skill `/design-review` applied by hand (helper scripts are not installed; the same checks were done by reading). The GDD itself
was not edited. Systems-index status left as is (recommended: `Needs Revision`).

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: L (4 formulas, 6 dependencies, 22 stories over generator + validator + page; producer should verify)
Specialists: none (lean mode)
Blocking items: 3 | Recommended: 10
Summary: All 8 required sections are present; the 7 bricks and all 22 catalogue rows of BUILDINGS_GDD.md section 6 are carried
over cell for cell, with a useful phase column added. Three things stop the S1 implementation: the brick set cannot express
two of the five S1 recipes, the layout stores one story while the rules allow two, and the distribution criterion is not
testable as written.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Detailed Rules 1 vs 3: the 7 bricks cannot express the recipes of S1 stories 7 (core stops below the roof, no hatch, facade ladder) and 10 (snow ramp to a storey-1 window), nor 2 (add a door), 11 (down-only flag), 16, 17, 19, 21. "A recipe of 1-3 bricks", AC1 (every brick undone exactly) and AC2 (the S1 stories) depend on it, and the data contract has no element for a snow / rubble ramp. Fix: add bricks (`add_door`, `trim_core`, `add_roof_access`, `add_ramp(kind, target)`, `set_down_only`) or split the catalogue into brick recipes and layout-condition stories (17, 19).
- [BLOCKING] Detailed Rules 2 (steps 5-6) / Formulas F2 vs the data contract: a building may carry two stories, but the stored field is one `story { id, bricks, seed }`. Fix: `stories[]` in application order, and say how a rollback of the second story leaves the first intact.
- [BLOCKING] Acceptance Criteria 4 / Formulas F3: "each story's share within its JSON target +- 10 %" - per-story targets are defined nowhere (only 30 / 70), "+- 10 %" is ambiguous (points or relative), and one map is too small a sample (the accepted X6 layout has 57 enterable buildings: about 6 points of standard deviation for a 30 % share; x1 maps sit near the "fewer than ~10" exemption of the edge cases). Fix: pool 5 seeds on x6, `share_open` within 30 +- 10 points; per story: "share > 0 for every story with weight > 0 that has eligible buildings", or a stated statistical bound.
- [RECOMMENDED] Detailed Rules 3, row 2: "2 doors on opposite sides, all >= 150 m2" is already the base rule for every building over 150 m2 (P1a, done) - the story adds nothing and its D is 1. Define pass-through as the "through" class (a straight route, no dead end).
- [RECOMMENDED] Detailed Rules 3, row 6: the key route "down by the hole, up by the far stair" needs a drop on the only way, which the no-drop rule forbids, and a floor hole is a 300 cm drop (controlled drop <= 250). Redefine the story (the hole is a shortcut down and a sight line) or drop it.
- [RECOMMENDED] Detailed Rules 2 (step 1) / Formulas F1: the eligibility inputs are not defined. "Courtyard" is not a concept in the generator (no such field in `generate.ts` or the templates); "a neighbour within X m" has a value only for story 18; "blocks in a row" (14) needs a shared wall. Stories 10 and 12 need about 430 cm of free run outside the footprint - add it to eligibility.
- [RECOMMENDED] Formulas F1 / F3: weights alone cannot guarantee the 30 / 70 share (eligibility differs per archetype, rollbacks add to `open`). A two-stage pick is simpler: P(storied) = 0.7, then F1 among the eligible non-open stories. No default weights are given (expected with Q1 open, but AC4 needs them before it can run).
- [RECOMMENDED] Formulas F2: may a second story follow Open or Pass-through? Derive compatibility (disjoint brick targets) instead of a hand-kept 22 x 22 list; keep the list for exceptions only.
- [RECOMMENDED] Edge Cases (stories 14 / 18): generation is "deterministic per (seed, building id)", but a link depends on two buildings. Fix the order (sorted ids) and say which building owns the story and its `story` record.
- [RECOMMENDED] Detailed Rules 3 (Phase column): S1 story 4 uses `add_balcony_escape(flat, ...)` while flats arrive in S2; story 5 lists hospital / school (S2). Say what S1 uses instead (any room on an exterior wall; office only).
- [RECOMMENDED] Detailed Rules 3, row 15: `block_flight(barricade)` must name its bypass ("else the long way") - otherwise it always rolls back; row 19 uses `fixed` (a window state) for shutters (door kind).
- [RECOMMENDED] Acceptance Criteria 2: "gives the key route in its catalogue row" compares prose - rewrite as "the key route's leg modes equal the fixture's expected sequence". AC5 and AC6 are testable as written.
- [RECOMMENDED] Detailed Rules 4 / Dependencies / Visual / UI: the tech-spec section numbers are stale (9, 10, 12, 13 are the old BUILDINGS_GDD.md numbers; now 2, 3, 5, 6).

Completeness: 8/8 required sections present in the standard's order; extra template sections Visual/Audio, UI, Open Questions.

Dependency graph (all files exist): building-archetypes.md, opening-states.md, building-route-model.md, breach-panels.md,
PMC_DEFENCE.md, KILL_ZONE_AMBUSH.md, BUILDINGS_TECH_SPEC.md; `Content/Data/LevelGen/building_stories.json` does not exist yet
(*proposal*, expected). Bidirectional: all four sibling GDDs name this one as downstream.

Registry values: no registry. Cross-checked by reading: `T_max` 4 agrees with the route model; balcony depth 120-150 agrees with
archetypes; plank distance <= 3 m appears in the row and the knob; the brick names agree with BUILDINGS_TECH_SPEC section 2
(`Stair.blocked`, `BuildingLink.kind`). Conflicts are the findings above (single `story` field, rows 2 and 6, stale section
numbers).

Content preservation (BUILDINGS_GDD.md sections 1 vision / share and 6): nothing missing - 7 bricks, 22 stories (every cell),
the rollback rule (max 4, then "open"), the JSON weights file, "two compatible stories (e.g. 3 + 9)", Q1, Q10, Q11. Paraphrased
rather than kept: the vision sentence "it has an obvious way in that may be closed, a hidden one, a reason why the stair does
not go all the way up, and a second way ... that the squad has to find". Added (not in the source): the phase per story,
`p_two` 0.25, the +- 10 % share band, the rollback target <= 10 %, the edge cases.

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
8b99076b63452ded4098e10894977c5046c89b8894519214b89403950808aec8  docs/design/levelgen/BUILDINGS_TECH_SPEC.md
913da2246354cc826bd23daab5e887cea226ab35c36918dea30d6fdc462190e1  docs/design/levelgen/gdd/building-archetypes.md
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
5ac121ddeca7ea59ca187b3f2dea5b2a7e357d946df71df14f8736f69707935a  docs/design/levelgen/gdd/breach-panels.md

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: minimal (project.yaml; all 8 sections checked anyway)
Scope signal: L
Specialists: none (lean). Same session as the author - not an independent review.
Blocking items: 3 | Recommended: 7
Summary: v2 closes all three prior blockers (bricks for S1 stories 7 / 10, stories[], pooled per-role distribution). New blockers were small rule gaps: S1 story 4 used flats that arrive in S2, no precedence when a building fits several roles, and weights alone could not meet the per-role budget. All three plus recommendations 1-3 were fixed right after this review (two-step pick, role order, N_watch, AC2 leg modes, story 19 shutters sealed); awaiting re-review.
Prior verdict resolved: Yes (3/3 prior blockers)
Findings:
- [BLOCKING] Detailed Rules 3, story 4: add_balcony_escape(flat) in S1 but flats are S2 -> fixed: flat = any room on an exterior wall until S2.
- [BLOCKING] Detailed Rules 1: no role precedence -> fixed: objective > garrison > overwatch > transit > background.
- [BLOCKING] Formulas F1 / F3: weights cannot guarantee the role budget -> fixed: two-step pick.
- [RECOMMENDED] Detailed Rules 1: overwatch count undefined -> fixed: N_watch 3, >= 2 storeys + walkable roof.
- [RECOMMENDED] Acceptance Criteria 2: prose comparison -> fixed: leg modes equal the fixture's sequence.
- [RECOMMENDED] Detailed Rules 6, story 19: shutters "fixed" vs door states -> fixed: sealed.
- [RECOMMENDED] Formulas F2: derive compatibility from disjoint brick targets (open).
- [RECOMMENDED] Edge Cases 14 / 18: link ownership and order by sorted ids (open).
- [RECOMMENDED] Eligibility: 430 cm free run outside for stories 10 / 12 (open).
- [RECOMMENDED] Story 15: name the bypass of block_flight(barricade) (open).
Reviewed-Content-Hash: docs/design/levelgen/gdd/building-stories.md 464dc8d601415dab61e0ea375bf689dc5a987b5e
review-receipts: not stamped, no such file: design/registry/entities.yaml
Reviewed-Content-Hash: /c/Users/Zephyrus15Duo/.claude/skills/design-review/SKILL.md 4dcc8b81162613c757144532f1b72708ab3ba27f
Reviewed-Content-Hash: docs/design/levelgen/gdd/building-archetypes.md 2b579056e1e2d857bb9b3612d593743d579ad0c7
Reviewed-Content-Hash: docs/design/levelgen/gdd/opening-states.md 9c83f89a54727ce67a5176cddebff2f5d0e55791
Reviewed-Content-Hash: docs/design/levelgen/gdd/building-route-model.md 14b282e2fdcdf5fa20fe7a94b2daecbe8a4928eb
Reviewed-Content-Hash: docs/design/levelgen/gdd/breach-panels.md ebffa71ec38d3e852ad316baacf9eea1f6fc13c3
Reviewed-Content-Hash: docs/design/levelgen/BUILDINGS_TECH_SPEC.md fdf691cb0d8cae6bcbaa4559ae9e4b8c8fb1c07f
