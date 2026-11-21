# Roadmap: building generation -> smart enemies -> destruction (designer-agreed 2026-11-21, pending Lead approval of the week)

The three mechanics form a chain: **buildings** produce space and data (entrances, windows, stairs, wall materials),
**enemies** consume it (cover, entries, firing positions), **destruction** changes both at runtime. Changing the whole chain
at once makes failures unattributable. Destruction design: `docs/design/gdd/destruction-studio-review-2026-11-21.md`,
`docs/architecture/adr-0004-destructible-buildings.md`.

## Rules
1. **One hot track at a time** merges gameplay code into main; the other two work on data, spikes and tests in their branches.
2. **Every mechanic behind a switch** (`Destruction=0`, `SoftCover=0`, ...); with it off, old seeds replay bit-identically, else no merge.
3. **Enemies train on frozen reference buildings**: each stage fixes a reference set (story showcase + Crossroads); the generator
   may change, the references do not until the stage ends.
4. **Every combat-changing step passes a paired A/B on the same seeds** (lesson 11-21: goal-zone cover made defence worse).
5. **Destruction is data-only until stage 2**: the generator already emits materials, load-bearing walls, wall cells (P0).

## Stages
| Stage | Buildings | Enemies | Destruction | Gate |
|---|---|---|---|---|
| **1 "A house you can enter"** (now -> 2026-11-28) | **HOT**: kit khrushchevka, gable door, 6-house showcase, story variants, openable doors, whole squad enters a flat | defence finish line in the street (Crossroads >= 60 % wins, <= 10 % captures, >= 2 kills/round) in branches | P0 data only (opening materials, cell export, validator) | designer plays the showcase; walk smoke all houses; finish-line numbers |
| **2 "Fight in a house"** (~2-3 weeks) | frozen references; new house types (old buildings) as data only | **HOT**: defend a flat / stairwell, fire from windows, enter-and-clear | P1 in branch then merge: soft-cover penetration, doors / glass / boards, door blast; spikes S-NAV / S-LOOK / S-DET on the worker | fight in a reference house; A/B penetration on/off |
| **3 "Walls break"** (~5-6 weeks) | new house types land | reactions to new holes; PMC RPG removes cover | **HOT**: P2 explosive holes, navmesh update, rims / dust | breach smokes, determinism, nav budget, A/B |
| **4 "Structure"** (later) | - | deliberate enemy breaching | P3 local floor collapse (only if the designer wants it) | collapse smokes, no soft-locks |

## Do not
- Start destruction code before openable doors and the defence finish line (doors are the first, simplest destruction case).
- Tune enemies inside buildings while house plans still change daily (hence frozen references).
- Promise a date for breakable walls before S-NAV measures navmesh rebuild cost (the main risk).

## Week 2026-11-22 .. 11-28 (stage 1) - for Lead approval
- Bgen (19c): gable door + balconies + all-khrushchevka showcase (running) -> formation compression in tight rooms -> story variants
  (random by weight + manual override) -> openable doors (R1 runtime prerequisite) -> destruction P0 data (opening material, cells, validator).
- Enemy AI session: defence finish line on Crossroads / Alley with paired A/B; goal-zone cover stays ON HOLD.
- Destruction: no runtime code; designer answers the 9 forks of the studio review (section 7).
- Worker: off overnight 11-21; powered on by hand on 11-22.
