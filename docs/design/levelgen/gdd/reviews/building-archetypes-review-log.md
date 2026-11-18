# Review log - building-archetypes.md

Skill `/design-review` applied by hand (helper scripts `yaml-helper.sh`, `review-receipts.sh`, `gdd-structure-check.sh` are not
installed; the same checks were done by reading). The GDD itself was not edited. Systems-index status left as is
(recommended: `Needs Revision`).

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: L (4 formulas, 8 dependencies, generator + builder + validator; producer should verify before sprint planning)
Specialists: none (lean mode)
Blocking items: 4 | Recommended: 9
Summary: All 8 required sections are present and the content of BUILDINGS_GDD.md sections 1-3 is carried over completely. The
document cannot go to implementation yet: the ladder formula is wrong for every rise other than 300 cm (and its acceptance
criterion fails on ladders that exist today), the archetype table contradicts the grammar table it declares authoritative, the
balcony depth conflicts with the approach-box rule, and the archetype-pick formula is not computable.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Formulas F1 / Acceptance Criteria 3: `rungs = round((rise - 8.8) / 36.4)` with a fixed plinth P = 8.8 holds only for rise 300. The implemented rule (`schema.ts` `ladderForLevels`) is rungs = floor(rise / 36.4), plinth = the remainder (8.8 at 300, 17.6 at 600, 26.4 at 900); contract 7.1 gives a 400 wall 11 rungs with no plinth (400.4). F1's own constraint fails at 600 (591.2), 900 (882.4) and 400 (409.2), so AC3 fails on every existing 16 / 24-rung ladder (hall roof ladders, story 7 facade ladders). Fix: rungs = floor(rise / r), P = rise - rungs * r, constraints rungs >= 7 (LADDERS.md minimum) and 0 <= P < r; AC3 = "rungs * 36.4 + plinth_cm within 1 cm of the rise".
- [BLOCKING] Detailed Rules 1 vs BUILDING_GENERATOR.md 3.1: the section says storeys / layout / roof "are defined in section 3.1", but its own table differs: office 1-2 storeys (F4: 2 when >= 8 m) vs 3.1 office 2-3; hall 9 m with a mezzanine at +300 vs 3.1 6-9 m, +300 or +600. A programmer has to guess. Fix: state which table wins (recommended: this GDD supersedes 3.1 for storeys, with a note in BUILDING_GENERATOR.md) and give the storey rule per archetype once.
- [BLOCKING] Detailed Rules 3 (balcony / fire escape) vs BUILDING_GENERATOR.md 4.2-4.3: a 120-150 cm deep balcony cannot hold the mandatory free 150 x 150 approach box at a ladder / flight top ("joined to a room / corridor >= 200 wide"), and no landing rule is given for escapes (the validator checks approach boxes only for `kind === 'stairwell'`). Fix: minimum depth 150 where an escape lands, or an explicit exemption with its own validator check; say where the escape stands relative to the balcony door and where the rail opens at the arrival.
- [BLOCKING] Formulas F3: not computable. `eligible(a, lot)` refers to footprint limits that the section-1 table does not contain; `w_a` are weights of lot templates (`levelgen_v2.json` `templates.catalog[*].weight`, today 0.6-1.2, with `min_m`), not of archetypes, and there is no template -> archetype mapping (hospital / school and shop + flats have neither a template nor a minimum size). Fix: add the minimum footprint and the template mapping per archetype (including the two new ones) or restate F3 as the existing template pick; correct the knob range (0-1 is wrong today).
- [RECOMMENDED] Detailed Rules 3 / F2: the balcony "step-over" (gap <= 90 cm between two 95 cm rails, a storey above the ground) is not a traversal action in MANTLING.md; `gap(a, b)` is not defined (edge to edge? along the facade?); the route model has no mode for it. Define the action (clip / height class) or drop the link until one exists.
- [RECOMMENDED] Detailed Rules 3 (stair core): the implemented switchback (one 300-rise flight per storey, 150 wide, run ~430) differs from BUILDING_GENERATOR.md 4.2 (enclosed dog-leg 270 x 500, two 150-rise flights 120 wide) and from kit module `stair_flight_150` (120 wide, rise 150, run 215) in contract 7.2. Say that the switchback supersedes 4.2 and raise the kit module mismatch.
- [RECOMMENDED] Edge Cases 1: "two 430 cm runs + two 150 cm approach boxes" is not what `planStairCore` needs (one run ~429 + two 150 boxes = ~7.3 m of inner length; two lanes + 1.2 m of free floor across). The first fallback (a straight stair with 150 landings) has the same length, so it never helps, and the chain differs from BUILDING_GENERATOR.md 3 step 9 (regenerate x4, then a closed shell). Give the numeric threshold and one fallback chain.
- [RECOMMENDED] Formulas F4 / Tuning Knobs: the office second-storey threshold (8 m) sits at the core's own minimum length - the core would fill the building. Raise the default or add a minimum floor area per storey after the core.
- [RECOMMENDED] Detailed Rules 2: the three building classes are an open studio topic in BUILDING_GENERATOR.md, not a decision - mark them *proposal*; cite designer fork 2 ("~60 % enterable") and say what the 30 / 70 share is a share of (enterable buildings, as in building-stories F3).
- [RECOMMENDED] Player Fantasy vs Detailed Rules 3: the fantasy says a hall's roof "is reached from outside"; the rule says "Halls climb to the roof by it [the core]". Outside-only is story 7, not the archetype default.
- [RECOMMENDED] Tuning Knobs / Q8: a 95 cm balcony rail tagged as a mantle edge on storey >= 1 gives a 300+ cm drop, above the 250 cm controlled-drop limit (contract 7.1: no storey is a controlled drop). Q8 carries no recommendation; suggested: the rail is a mantle edge only where it faces an escape landing.
- [RECOMMENDED] Acceptance Criteria: AC1 mixes done archetypes with ones that are not designed yet (hospital / school and shop + flats are S2, the ruin is hand-shaped) - tag each criterion with its phase. AC6 uses the P0 ramp-smoke target (>= 90 % of legs) for `LevelGenBuildingsSmoke`, while building-route-model asks >= 95 % of buildings and BUILDING_GENERATOR.md 5 says a map is not published while the smoke FAILs - one threshold per smoke. AC4: name the layout field that defines "the playable frame".
- [RECOMMENDED] Detailed Rules 1 / Edge Cases (400 / 800 hall walls): the catalogue says halls are 9 m (3 x 300) while kit hall walls are 400 / 800. State the hall height rule (900 greybox vs 800 kit) and the ladder that fits it.

Completeness: 8/8 required sections present in the standard's order (Overview, Player Fantasy, Detailed Rules, Formulas, Edge
Cases, Dependencies, Tuning Knobs, Acceptance Criteria); extra template sections Visual/Audio, UI, Open Questions.

Dependency graph (all files exist): BUILDING_GENERATOR.md, MANTLING.md, `obstacle_heights.json`, LADDERS.md, opening-states.md,
building-route-model.md, breach-panels.md, building-stories.md, PMC_DEFENCE.md. Bidirectional: the four sibling GDDs list this
one as upstream; the pre-standard docs do not reference back (expected).

Registry values: no `design/registry/entities.yaml`. Values cross-checked by reading against the sibling GDDs,
BUILDING_GENERATOR.md 3-4 / 7, `schema.ts`, `generate.ts`, `buildingTraversal.ts`: storey 300 ok; 35 deg / run 429 ok
(`STAIR_RUN_PER_RISE` 1.43); approach 150 and flight width 150 ok (`TRAVERSAL`); sill 95 ok; controlled drop <= 250 ok
(MANTLING 60-250); very_tall 300-320 ok; G_step 90 and balcony depth 120-150 consistent with building-stories. Conflicts are
the findings above (F1, office / hall storeys, balcony depth, stair core, edge case 1).

Content preservation (BUILDINGS_GDD.md sections 1 share, 2, 3): nothing missing - all 10 archetype rows, all 6 circulation
elements, the mantle / drop rule, Q7-Q9. Changed wording: "A balcony without a fire escape is a firing position only" became
"without a fire escape or neighbour link". Added (not in the source): the building classes, the layout-rule digest, `down_only`
escapes, "ladders longer than one storey only outside", F1-F4, the knob table.

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
913da2246354cc826bd23daab5e887cea226ab35c36918dea30d6fdc462190e1  docs/design/levelgen/gdd/building-archetypes.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
4541b9f6781306b1865787a852133d3b9bdd7a7f69e0a04ce1ca6a0b3e62e3ab  Tools/TrainingGround/src/levelgen/buildingTraversal.ts
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
5ac121ddeca7ea59ca187b3f2dea5b2a7e357d946df71df14f8736f69707935a  docs/design/levelgen/gdd/breach-panels.md
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
