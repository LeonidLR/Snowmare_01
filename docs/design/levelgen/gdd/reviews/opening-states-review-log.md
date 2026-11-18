# Review log - opening-states.md

Skill `/design-review` applied by hand (helper scripts are not installed; the same checks were done by reading). The GDD itself
was not edited. Systems-index status left as is (recommended: `Needs Revision`).

## Review — 2026-11-18 — Verdict: NEEDS REVISION
Review mode: lean · Workflow: full
Scope signal: L (2 formulas, 8 dependencies, generator + builder + runtime R1-R2 + noise / perception; producer should verify)
Specialists: none (lean mode)
Blocking items: 3 | Recommended: 10
Summary: All 8 required sections are present and both state tables of BUILDINGS_GDD.md section 4 are carried over row for row.
Three gaps stop implementation: the generator's default states are not defined (so the quiet-share criterion cannot pass), the
safe-degradation table misses `closed` and the layer that degrades, and the explosive action has no time in F1.
Prior verdict resolved: First review
Findings:
- [BLOCKING] Detailed Rules 3 (rule 3) / Formulas F2 / Acceptance Criteria 5: the generator's default states are not defined. Rule 3 gives only the schema default (absent = `open` / `broken`), while the Overview, the knob "default window state per archetype / facade" and the pipeline speak of defaults per archetype and facade. With every door `open` and every window `broken`, nothing ever emits `closed` or `boarded` (and `glazed` only in story 9), and `quiet_share` is ~100 % - outside its own 60-90 % band, so AC5 cannot pass. Fix: a default-state table (archetype x facade: door open / closed share, window broken / glazed / boarded / barred share), or drop the band.
- [BLOCKING] Detailed Rules 4 (safe degradation): `closed` is missing. Opening a door is itself a runtime feature that is not known to exist (tech spec runtime table: "exists?"; `Source/` has `UDoorwaySubsystem` and the checkpoint `AGateActor`, no door leaf), and the builder spawns a leaf actor for `closed` / `locked`. With the project's dynamic navmesh a spawned leaf closes the doorway that the validator counts as passable. The layer that degrades (builder spawn rule or runtime default) is not named. Fix: add `closed` -> `open` until R1 and state "no blocking leaf / pane is spawned until the feature is on" (or the equivalent runtime rule).
- [BLOCKING] Formulas F1: `t_charge` (time of the explosive action) is not defined, so `t(e)` of every `explosive` edge in the route model is undefined. Add it with a default and a range; state explicitly `n = 0` for `vault` and `(0, 0, 0)` for `open`.
- [RECOMMENDED] Formulas F1 (noise) vs STEALTH_LURE.md: a whistle is 12 m and a stone impact 8 m in open air (x0.5 per wall). A "loud" kick or glass at 6 m is quieter than a thrown stone. Calibrate `N_kick` / `N_glass` / `N_charge` on that scale and say whether wall occlusion applies.
- [RECOMMENDED] Detailed Rules 1 / Q2 vs DOORWAY_BREACH.md section 8 item 5: locked doors there are forced by "Sapper / Shotgun / Kick". The recommendation "kick only" narrows an existing spec - cite it and decide.
- [RECOMMENDED] Detailed Rules 1-2 ("Passable by"): "everyone" / "vault" without agent classes. LADDERS.md: mutants never climb; the enemies' nav filter excludes vault links. Add a column (operative / human enemy / mutant) - otherwise a `closed` door or a window-only room is either a safe room or undefined for the horde.
- [RECOMMENDED] Detailed Rules 1-2: gates, shutters and loopholes are named in the Overview but have no row. Can a 3-6 m gate (`MaxGateWidthCm` 600) be kicked? Story 19 puts `fixed` (a window state) on shutters (door kind); an opening of kind `loophole` with an absent state would default to `broken` = vaultable. Add the mapping per opening kind.
- [RECOMMENDED] Acceptance Criteria 3: "`locked` costs `t_kick` + `N_kick`" adds seconds to metres - rewrite as "the edge is (t_kick, N_kick, 0)". "`glazed` costs once" is a runtime fact, not a property of a static graph - rewrite as "the route model prices `glazed` as (t_vault + t_glass, N_glass, 0); runtime: the second vault through the same window emits no noise event". AC5: name the map set and pool the seeds (x6, 5 seeds pooled).
- [RECOMMENDED] Edge Cases (barred window): "sight and fire pass" needs the collision channels spelled out (block Pawn and navigation, ignore Visibility and the weapon trace channel); "no visibility block" alone does not let bullets through.
- [RECOMMENDED] Detailed Rules 3 (rule 6): "`glazed` -> `broken` ... or a shot" is new relative to the source. Then say whether a stray round opens the quiet entry for free and whether the shatter noise is emitted.
- [RECOMMENDED] Formulas F2: define "entry" (a storey-0 exterior opening, or an upper one with a ramp / roof below it) and the case of zero enterable buildings.
- [RECOMMENDED] Formulas F1: `t_vault` "~1.2 s, measure" - measure the LowVault / RunLowVault clip lengths (the clips named in `obstacle_heights.json`) before S1; `N_glass`, `N_charge`, `t_glass` are new numbers - keep them marked *proposal* in the tables as well.
- [RECOMMENDED] Dependencies / Tuning Knobs / Visual: the tech-spec section numbers are stale (9, 10, 11 are the old BUILDINGS_GDD.md numbers; the tech spec is numbered 1-8: data contract 2, builder 3, runtime 4).

Completeness: 8/8 required sections present in the standard's order; extra template sections Visual/Audio, UI, Open Questions.

Dependency graph (all files exist): building-archetypes.md, building-route-model.md, building-stories.md, breach-panels.md,
DOORWAY_BREACH.md, `DoorwayBreachRules.h`, MANTLING.md, STEALTH_LURE.md, BUILDINGS_TECH_SPEC.md. Bidirectional: archetypes
(downstream), route model (upstream), stories (upstream) and breach panels (sibling) all name this GDD.

Registry values: no registry. Cross-checked by reading: `t_kick` 3 s / `N_kick` 6 m / `N_charge` 25 m agree with
building-route-model F1 and breach-panels; sill 95 agrees with archetypes and contract 7.2; `FallbackStackSpacingCm` 60 exists
in `DoorwayBreachRules`; data types agree with BUILDINGS_TECH_SPEC section 2. Conflicts are the findings above (noise scale,
DOORWAY_BREACH locked doors, stale section numbers).

Content preservation (BUILDINGS_GDD.md section 4 + the degradation rule of section 11): nothing missing - 5 door states, 5 window
states, both rules, Q2, Q3, the degradation list. Changed: `sealed` visual gained "/ snow drift"; `glazed` -> `broken` gained
"or a shot". Added (not in the source): rules 3-6, F1 variables (`t_vault`, `t_glass`, `N_glass` 6 m, `N_charge` 25 m), F2
`quiet_share` with its 60-90 % band, the `boarded` open question.

Receipt (sha256 computed by hand, `review-receipts.sh` is not installed):
fc5adde662220de8acfa5287775b3e8aceef2a28083bb6676cf49ccdbb2aada4  docs/design/levelgen/gdd/opening-states.md
db33649d74589979c48ed3a2b5b9511cbbf5627dd3145694625e849292e8fbfc  ~/.claude/skills/design-review/SKILL.md
43aa04e093ce039eccdb03918c2182a69b1ef5c7a4d2b8296134e3a591770186  docs/design/levelgen/BUILDINGS_GDD.md
6be4920a1e7bc92f2ffda66622d0e17540d1e10f86c2bef7631b50707a438dd7  docs/design/levelgen/BUILDING_GENERATOR.md
8b99076b63452ded4098e10894977c5046c89b8894519214b89403950808aec8  docs/design/levelgen/BUILDINGS_TECH_SPEC.md
913da2246354cc826bd23daab5e887cea226ab35c36918dea30d6fdc462190e1  docs/design/levelgen/gdd/building-archetypes.md
5a09ff4d3654dcec8bfeb2a52d66e6250420a5fd92d29188e38df9726f38d844  docs/design/levelgen/gdd/building-route-model.md
5e32706a075fb0a1ea760151f3ab3e933433ebc06f28c61c8e10462ed390cd63  docs/design/levelgen/gdd/building-stories.md
