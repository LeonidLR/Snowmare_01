# T-021 cold indoors on generated maps + weather_strength - report (WIP, handed over 2026-11-19)

## Handover (read first)

**State 2026-11-19 (worker slot1, local build): BUILT, TESTED, SMOKED. Gate passed. Branch `worker/t021-cold-indoors`.**

**Build** (`Scripts/build.ps1`, local, CODEX_AGENT=claude): Succeeded, 669 s, NO compile errors (the earlier `Hole` fix 837654aa was the only one UBT had;
the full 59-action build found no more). Only the pre-existing `ParseJson` unity-clash warning (not mine).

**Designer decision 2026-11-19 (leafless doorway = OPEN) - implemented**
- `ColdShelterRules::IsOpeningInitiallyOpen(const FPmcLayoutOpening&)` (Source/CodexTactics/Public/Survival/ColdShelterRules.h, Private/.../ColdShelterRules.cpp):
  a DOOR with an empty state is open (leaks with the door weight 3 and makes the draught: outdoor cold within 3.5 m, fading to the base at 10 m);
  every other kind (stateless window / loophole) is sealed; an explicit door state (closed / locked / kicked ...) wins. `InitialOpenFlags` uses it.
  `IsStateOpen(State)` itself is unchanged (empty = false), so the rule stays pure; the layout reader is unchanged.
- New test `CodexTactics.Survival.ColdShelter.LeaflessDoorwayIsOpen` (2 leafless doors + 8 windows -> leak 6/14, draught 1 at 2 m, base at the far wall,
  explicit closed / locked sealed, kicked open, stateless window / loophole sealed).
- Assumptions changed ONLY where they were exactly "a stateless door is sealed": (1) test fixture `CsMakeHall` now gives its doors an explicit
  `closed` state (a hall with leafed doors) so AllSealedIsAirtight / LeakWeights / Draught / Storey / Zone tests keep every check unchanged; the leafless
  variant is `CsMakeHall(1, true)`. (2) `LayoutParse`: "the legacy stateless door is sealed (leak 0)" became "is open (leak 1)" - the legacy doors[] entry has no
  state = a leafless doorway. (3) `ColdIndoorsSmoke`: no longer requires the picked building to start sealed; it first checks the building AS GENERATED
  (leak > 0, door open, draught = outdoor cold beside the doorway), then seals every opening through `SetOpeningOpen` for the airtight baseline, then
  the unchanged kick / far wall / reseal / roof checks. Comments in cold_shelter.json and docs/systems/survival-cold-heat.md updated.

**Smoke measurement fix (not a rule change):** the committed `Content/Data/LevelJson/L_Gen_Depot_1_X6.json` has `"cold_rate_mult": 0` (also L_Gen_Depot_1,
PmcArena*, FormationLab*), so every cold RATE on it is 0 whatever the zone. The smoke's `RatePerSecond` now pins LevelMultiplier / Weather / Phase /
Accumulation to 1 (only the zone is under test; weather / phase are covered by ColdSmoke / EnvironmentSmoke). The first smoke run failed 4 checks purely on
the 0.000 %/s rate (the zone values were already right).

**Tests** (`Scripts/test.ps1 -Filter ...`, counts compared with the macros; no crash): `CodexTactics.Survival.ColdShelter` 11 tests / 0 failed (11 macros);
`CodexTactics.Environment` 114 / 0 failed; `CodexTactics.LevelJson` 7 / 0 (7 macros); `CodexTactics.AI.PmcDefence` 7 / 0 (7 macros);
`CodexTactics.AI.Pmc` 70 / 0 (70 macros).

**Smokes** (`Scripts/smoke_parallel.ps1 -Remote off -Jobs 3`, full SYNC file around each run): ColdSmoke PASS, EnvironmentSmoke PASS, WeatherSmoke PASS,
WarmIdleSmoke PASS, PmcRoofDefenceSmoke PASS, TacticalPmcSmoke PASS (the last two guard the layout-parser change). ColdIndoorsSmoke: FAIL in the first run
(the rate issue above), PASS 16 checks / 0 failed after the fix (building B_x1_4_t7_hall: as generated leak 0.250 = 2 doors; sealed zone 0; kicked door
share 0.125; near door zone 1.0 / 0.880 %/s, mid 0.562, far wall (39.4 m) 0.125 / 0.110 %/s; roof = open air). Logs: `Saved/Logs/Smoke-ColdIndoorsSmoke.log`
(final), `Smoke-ColdIndoorsSmoke-alone.log` (the failing run). Log string `[ColdShelter] L_Gen_Depot_1_X6: 70 buildings, 399 openings, 57 buildings leak at the start`.

## Decisions / things to tell the designer
- DECIDED 2026-11-19 (designer): X6's generator writes no opening states and its doors are wall gaps with no leaf; these now count as OPEN with a draught
  (see the Handover). 57 of the 70 X6 buildings leak at the start. Windows / loopholes without a state stay sealed until a runtime glass / boarded state exists.
- Buildings with NO openings in the layout (office_w, gallery, chimneys, facades: 13 of 70) are airtight (leak 0, multiplier 0) with no door at all - designer
  to confirm or fix in the generator (they have no entrance, so only matter when someone is inside).
- The committed X6 level JSON has `cold_rate_mult: 0` (cold off) - the designer's local version evidently differs; the indoor rule only matters where the level cold is on.

## Follow-ups
- No runtime door / window / breach actor exists yet (grep of Source: nothing holds an opening state; `DoorwaySubsystem` is a doorway registry for
  breach slots, `DoorBreachSmoke` is a stack-up). Whoever adds kickable doors, glass or breach charges must call
  `UColdShelterSubsystem::SetOpeningOpen(AtPosition)`. Until then only the layout state and the smoke change the leak.
- `UPmcSquadSubsystem` has its own copy of the layout file loader (T-05 owns that file); dedupe later with the subsystem's loader.
- Campfire indoor rule and blizzard walk now see generated interiors (a campfire inside a generated building counts as indoors: its wind / noise rules).
- A `-LevelJson=` override with another map's name still reads the MAP's own level JSON for the layout.
- `find_unity_clashes` warns about `ParseJson` in `EnvironmentDirectionalSnowTest.cpp` / `PropsGameplayRulesTest.cpp` (pre-existing, not mine).
