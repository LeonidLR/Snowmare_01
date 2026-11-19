# T-021 cold indoors on generated maps + weather_strength - report (WIP, handed over 2026-11-19)

## Handover (read first)

**State: code, tests, smoke, data and docs are written and pushed on `claude/cold-indoors`. NOTHING IS BUILT OR RUN YET.**
The first remote build (HEAD ec8c665e) failed with one compile error in `PmcDefenceRules.cpp` (`Hole` undeclared); fixed in 837654aa.
A 2nd remote build was queued behind other agents on the worker (the main PC then went offline); an earlier attempt hit an ssh timeout
(infra failure 1). No test or smoke result exists. No compile error other than the `Hole` one was reported by UBT, but UBT stops listing
after the failing TU group, so more may surface.

**Done**
- Designer model (replaces the lead's 0 / 0.5 / 1 rule): per building `leak` = open opening weight / all weight (door 3, window 1, loophole 0.5,
  wall breach and roof hole 4); base multiplier = `leak * leak_to_cold` (1.0); draught = 1 within 3.5 m of an OPEN opening on the SAME storey,
  linear to the base at 10 m; indoor multiplier = max(base, draught); a camera zone still wins; outside = wave multiplier (today).
  Numbers (commented): `Content/Data/Survival/cold_shelter.json`.
- `Source/CodexTactics/{Public,Private}/Survival/ColdShelterRules.*` (pure rule + `ConfigFromJson`), `ColdShelterSubsystem.*` (loads the map's
  `levelgen_layout` once via the existing `PmcDefenceRules::ParseLayout`; live per-opening flags; `SetOpeningOpen(BuildingId, OpeningId, bool)`,
  `SetOpeningOpenAt(XY, Storey, Radius, bool)`, `OnLeakChanged`; event-driven, no per-tick scan).
- `PmcDefenceRules` parser extended additively: `FPmcLayoutOpening` (id, kind, state, floor, pos) in `FPmcLayoutBuilding::Openings`
  (openings[]; legacy doors[] only if no door opening; roof_holes[] as kind `roof_hole`, state open, top storey). API unchanged.
- `UColdSurvivalComponent::GatherEnvironment` uses it (camera zone > layout building > wave multiplier). Campfire `IsIndoors` and the blizzard-walk
  shelter test call `UColdShelterSubsystem::IsShelteredLocation` (zone OR layout building) - one line each.
- `environment.weather_strength` (0..2, default 1): `FEnvironmentLevelSettings::WeatherStrength`, `LevelJsonRules::ParseEnvironmentSettings`,
  `EnvironmentRules::ApplyWeatherStrength` = `1 + (blended - 1) * strength`, applied in GatherEnvironment's weather step only.
- Tests `Source/CodexTacticsTests/Private/Survival/ColdShelterRulesTest.cpp` (`CodexTactics.Survival.ColdShelter.*`: all sealed 0, 1/14 and 6/14 leaks,
  draught 1 at 2 m / base at 10 m / linear, same storey only, inside / outside / roof Z / rotated footprint, camera zone wins, layout parse,
  config JSON incl. the shipped file, weather strength 0 / 1 / 2 + level JSON key).
- Smoke `CodexTactics.ColdIndoorsSmoke` (`Source/CodexTacticsDev/Private/Debug/ColdIndoorsSmokeCommand.cpp`), registered in `Scripts/test_map.json`
  (map `/Game/Maps/Generated/L_Gen_Depot_1_X6`, `-LevelJson=L_Gen_Depot_1_X6.json`): camera zone count (diagnosis), outside open air, sealed building 0,
  door kicked via `SetOpeningOpen` -> event + leak = door share, near door = outdoor value, far wall lower, resealed 0, roof = open air.
- Doc note: `docs/systems/survival-cold-heat.md` (last section).

**Diagnosis (verified without running):** the X6 `.umap` has 0 `CameraZoneVolume` references (binary scan), `build_levelgen_map.py` places none, and
`GatherEnvironment` only lowered cold through `ACameraZoneVolume` -> every generated building was open air (zone multiplier 1). The smoke logs the
live count + multiplier too.

**Next (in order)**
1. `$env:CODEX_AGENT='claude'; powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Remote worker` (or local `Scripts/build.ps1`). Fix any compile
   errors. Confirm the copied-back DLLs contain the change (search for the log string `[ColdShelter]`).
2. `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Survival.ColdShelter` then `-Filter CodexTactics.Environment`,
   `-Filter CodexTactics.LevelJson`, `-Filter CodexTactics.AI.PmcDefence` (the parser changed).
3. `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Remote on -Commands ColdIndoorsSmoke,ColdSmoke,EnvironmentSmoke,WeatherSmoke,WarmIdleSmoke`
   (use full names; the smoke needs the committed `cold_shelter.json` and `Content/Data/LevelJson/L_Gen_Depot_1_X6.json`).
4. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` (it audits unstaged changes only: run it before committing a fix).
5. Update this report with real results, commit (Conventional Commits, `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`), push the branch only.

**Gate state:** build NOT passed, tests NOT run, smokes NOT run, audit NOT run (all changes were committed, so the audit saw 0 files).

## Decisions / things to tell the designer
- X6's generator writes no opening states and its doors are wall gaps with no leaf, so every layout opening starts SEALED (an intact closed door /
  glazed window, per the designer's model): a generated building is airtight (multiplier 0) until something opens an opening. Only B_ruin starts
  leaking (its 2 roof holes count as breaches, weight 4 each). If the designer wants door gaps to count as open, set the door's state to `open` in the
  layout (or flip the default in `ColdShelterRules::IsStateOpen`).
- Stateless layout windows / loopholes are sealed.

## Follow-ups
- No runtime door / window / breach actor exists yet (grep of Source: nothing holds an opening state; `DoorwaySubsystem` is a doorway registry for
  breach slots, `DoorBreachSmoke` is a stack-up). Whoever adds kickable doors, glass or breach charges must call
  `UColdShelterSubsystem::SetOpeningOpen(AtPosition)`. Until then only the layout state and the smoke change the leak.
- `UPmcSquadSubsystem` has its own copy of the layout file loader (T-05 owns that file); dedupe later with the subsystem's loader.
- Campfire indoor rule and blizzard walk now see generated interiors (a campfire inside a generated building counts as indoors: its wind / noise rules).
- A `-LevelJson=` override with another map's name still reads the MAP's own level JSON for the layout.
- `find_unity_clashes` warns about `ParseJson` in `EnvironmentDirectionalSnowTest.cpp` / `PropsGameplayRulesTest.cpp` (pre-existing, not mine).
