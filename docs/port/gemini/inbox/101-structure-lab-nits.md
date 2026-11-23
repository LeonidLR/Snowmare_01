# Gemini 101 — two small data nits (TS / JSON only)

Branch `gemini/101-structure-lab-nits` from the newest main. LIGHT. Plan line in chat, then go (no wait).

1. **099 structure (destruction P0):** `Tools/TrainingGround/src/levelgen/structure/validate.ts` `collectElements`: a stair `needs`
   that is NaN / Infinity is still accepted (`typeof s.needs === 'number'`). Use `Number.isFinite`, report it as a V3 failure, add a unit.
2. **096 lab layouts:** `B_two_storey` (lab layout data from 096, see `docs/port/gemini/outbox/096-lab-layouts.report.md`) has duplicate
   windows (the same window twice at one position). Find them with a small check, remove the duplicates in the data, and add a unit
   test that fails on duplicate openings in any lab layout.

Allowed paths: `Tools/TrainingGround/src/levelgen/structure/**`, the 096 lab layout data files and their tests, `docs/port/gemini/outbox/101-*`.
Never: the building generator code outside these files, maps, .uasset.
Tests: `cd Tools/TrainingGround; node --test "src/**/*.test.ts"; npx tsc --noEmit -p .` green. Report + FINISH in topic gemini-101.
