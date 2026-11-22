# 099 - Destruction P0: structure data contract + graph validator as a standalone TS module (ADR-0004) - MEDIUM, TS / data
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: newest main. Branch gemini/099-structure-p0-contract. Worktree CodexTactics-gemday. PLAN first in chat topic `gemini-099`
and WAIT for Claude's "OK 099" (up to 30 min; no answer -> do only steps 1-2 below).
Router: L2-L3. Push may be refused by the pre-push guard - expected; commit locally, Claude merges.

## Design gate (lead filled 2026-11-22)
- Spec: `docs/architecture/adr-0004-destructible-buildings.md`, sections "Structural Model", "Data contract sketch", "Validator
  rules" and the P0 row of the phase table. Read them in full. Destruction stays DATA ONLY: no C++, no runtime, no map changes.
- Split with the building-generator lead (19c): 19c later wires the classification into the generator (which wall is load-bearing,
  slab panels, supported_by) and the walkable-graph rules V2 / V4. YOU build the parts that need no generator internals, in a NEW
  folder, so nobody edits the same files:
  1. `Tools/TrainingGround/src/levelgen/structure/types.ts` - the ADR types (`StructureClass`, `StructureMaterial`, `StructureInfo`,
     `SlabPanel`, the optional fields on wall / opening / stair / building) as standalone interfaces + type guards.
  2. `Content/Data/LevelGen/structure_classes.json` - every `<structure>.<material>` pair of the ADR mapped to an `hp_class`, and
     `hp_classes` with `hp_per_cell`, `min_breach_class`, `noise_m`, `rubble`. Values = PLACEHOLDERS marked `"_placeholder": true`
     with the order load_bearing.concrete_panel > facade.brick > annex.* > partition.plaster > *.wood (designer tunes later;
     section 7 of `docs/design/gdd/destruction-studio-review-2026-11-21.md` holds his open questions - do not answer them).
  3. `structure/validate.ts` - pure functions on one building object: **V3** (acyclic support graph, `supported_by.length >= needs`,
     a partition supports nothing, an annex supports only annex elements, every `hp_class` resolves) and **V5** helpers
     (`structureHash(classesJson)`, stable key order so the hash is byte-identical across runs). Each failure = `{rule, element_id,
     message}`; no throw.
  4. `structure/classify.ts` - the ADR's *proposal* table "Classification by construction system" as a pure lookup
     (`block` / `hall` / `warehouse` / `office` / `shed` / `ruin` + element role -> `{structure, material}`), NOT called by the
     generator yet.
- Acceptance: vitest units for every V3 sub-rule (one pass + one fail case each), the hash stable on 5 repeated calls and changed
  by one edited value, classify covering every construction system; `npm test` + typecheck green; generator output unchanged
  (nothing imports the new module yet - prove with `git diff --stat` that no existing levelgen file changed).
- Designer decision needed? none now (values are placeholders).

## Never
Edit existing files under `Tools/TrainingGround/src/levelgen/**` (19c's hot track), `Scripts/Editor/build_levelgen_map.py`, any C++,
`Content/Maps/**`, accepted layouts. Invent V2 / V4 (they need the walkable graph - 19c's).

## Gate
`npm test` + `npx tsc --noEmit` in Tools/TrainingGround | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` | `git diff --check`.
Allowed paths: `Tools/TrainingGround/src/levelgen/structure/**` (new), `Content/Data/LevelGen/structure_classes.json` (new),
`docs/port/gemini/**`.

## Report
`docs/port/gemini/outbox/099-structure-p0-contract.report.md` (Russian): what each file does, test output, what 19c must wire next.
FINISH in topic `gemini-099`: `099 FINISH | <status> | <commit> | done: ... | NOT done: ... | needs Claude | gate: ... | report: <path>`.
