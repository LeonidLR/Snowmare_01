# T-021 - Cold does not stop inside buildings of generated maps + per-level weather strength (исполнитель: субагент Sonnet)
Designer 2026-11-19 bug: "даже если я захожу в здание, даже если они полностью закрыты, холод не пропадает" (L_Gen_Depot_1_X6).
C++. Branch `claude/cold-indoors`. Worktree `CodexTactics-gemday` (Antigravity is unavailable; its worktree is free, detached at
origin/main - create the branch there). Status lines: chat topic `claude-T021`.

## Lead diagnosis
`UColdSurvivalComponent::GatherEnvironment` (Source/CodexTactics/Private/Survival/ColdSurvivalComponent.cpp ~160-200) lowers
the cold indoors ONLY through `ACameraZoneVolume` (Closed = 0, Shelter = 0.5: CameraZoneRules::GetColdMultiplier). Hand-made
maps place those volumes; the level-generator builder (`Scripts/Editor/build_levelgen_map.py`) places none, so every generated
building is open air for the cold. Verify this first (log the zone multiplier for an operative standing inside an X6 building).

## Lead design (gameplay rule decided by the lead; tell the designer in the report)
- Do NOT add camera zones to generated maps (they also drive the camera). Add a runtime indoor test from the map's layout:
  the level JSON's `levelgen_layout` (accepted layout JSON) already gives the buildings (footprint, storeys / roof Z) - see how
  `PmcDefenceRules::ParseLayout` / `BuildingAt` / `RoofZ` read it (Source/CodexTactics/*/AI/PmcDefenceRules.*) and REUSE that
  parser (or move the shared part to a neutral place) - do not write a second layout parser.
- New pure rules (e.g. `Survival/ColdShelterRules`) + a small world subsystem that loads the layout once per level:
  an operative is **indoors** when his feet are inside a building footprint and below its roof Z (and above its ground);
  multiplier: **0 (Closed)** for a building whose openings are all closed (doors closed / locked / sealed, windows glazed /
  boarded, no open breach) per the layout's opening states, **0.5 (Shelter)** when it has an open doorway / broken window / open
  breach, **1** outside. Use the opening state fields the layout actually has (read the schema in
  Tools/TrainingGround/src/levelgen + docs/design/levelgen/*); if the layout of X6 carries no states, treat the building as
  Closed and say so. Runtime opening changes (a door opened in play) may be ignored in this task - note it as a follow-up.
- Combine like today: a camera zone, when present, still wins (hand-made maps unchanged); else the layout shelter multiplier.
  Heat sources / campfire indoor rules that use `ACameraZoneVolume::IsShelteredLocation` should see generated interiors too
  if it is a one-line call site - otherwise leave them and list it.
- **Per-level weather strength:** read `environment.weather_strength` (number, default 1, clamp 0-2) from the level JSON
  (where `environment.weather` / `locked` are read: EnvironmentLevelRules / LevelJsonRules) and apply it to the weather's cold
  effect only: `WeatherMultiplier = 1 + (blended - 1) * strength` in GatherEnvironment's weather step. A TG task (T-020) writes
  this key from the UI in parallel; 1 = today's behaviour exactly.

## DESIGNER UPDATE 2026-11-19 (replaces the 0 / 0.5 / 1 rule above)
Quote: "Чем больше в здании разбитых или выбитых дверей, разбитых окон или пробитых стен, тем холоднее здание ... когда они все
закрыты, это 100 % герметичность ... Чем дальше от открытой двери, тем теплее (в радиусе 3-4 м от выбитой двери холодно)."
Lead formula (numbers in a new JSON, e.g. `Content/Data/Survival/cold_shelter.json`, every number commented):
- Each building's openings (from the layout + their live state): weight door 3, window 1, wall breach 4 (`weights`).
  `leak = sum(weight of openings that are broken / kicked out / open / breached) / sum(weight of all openings)`;
  airtightness = 1 - leak. An intact closed door / glazed or boarded window = sealed; an OPEN (not broken) door leaks while open.
- Building base multiplier = `leak * leak_to_cold` (`leak_to_cold` 1.0: a fully open building = open air; all sealed = 0).
- Draught near an open opening, same storey: within `draught_full_m` 3.5 m the multiplier is 1 (outdoor), falling linearly to the
  base at `draught_fade_m` 10 m. Final indoor multiplier = max(base, strongest draught). Openings on other storeys count only
  in `leak`.
- LIVE state: openings change in play (a door kicked, glass shot, a breach blown) - find the runtime events / actors that hold
  door, window and breach states (doors, breach panels, glazed windows: grep the opening / door / window / breach actors and
  the event bus) and update the building's leak when they change (event-driven, no per-tick scans). If some opening kind has no
  runtime state yet, take its layout state and list it as a follow-up.
- Tests: all sealed 0; one window of 2 doors + 8 windows -> leak 1/14; both doors kicked -> 6/14; draught 1 at 2 m, base at 10 m,
  linear between; camera zone still wins; weather strength as before. ColdIndoorsSmoke: sealed building ~0, then break a door in
  the smoke -> the building's drain rises and is higher near the door than at the far wall.

## Gate
Build on the worker: `$env:CODEX_AGENT='claude'; powershell -ExecutionPolicy Bypass -File Scripts/build.ps1 -Remote worker`
(if the remote build fails for infrastructure reasons twice, local `Scripts/build.ps1`). Unit tests for the rules
(`CodexTactics.Survival.ColdShelter.*`: inside / outside / under-roof Z / closed vs open building / camera zone wins /
weather strength 0, 1, 2) - run them and report how. A smoke `ColdIndoorsSmoke` on `/Game/Maps/Generated/L_Gen_Depot_1_X6`
with `-LevelJson=L_Gen_Depot_1_X6.json` (an operative teleported into a closed building: cold drain 0 / near 0; outside: > 0),
registered in Scripts/test_map.json; smokes via `Scripts/smoke_parallel.ps1 -Remote on -Commands ColdIndoorsSmoke,ColdSmoke,EnvironmentSmoke,WeatherSmoke,WarmIdleSmoke`.
NOTE: L_Gen_Depot_1_X6 in the designer's main checkout has uncommitted edits; the worker and your worktree use the committed
version - fine for the smoke. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`.

## Never
Edit Content/Maps, the designer's X6 files, Config/DefaultScalability.ini, Content/VFX; change hand-made maps' cold; push or
merge main; touch Tools/TrainingGround (T-020 owns it).

## Allowed paths
`Source/CodexTactics/{Public,Private}/Survival/**`, `Source/CodexTactics/{Public,Private}/Environment/**` (the level key),
`Source/CodexTactics/{Public,Private}/Data/LevelJson*` (the key), the shared layout parser location if you move it (keep
PmcDefenceRules' API), `Source/CodexTacticsDev/Private/Debug/`, `Source/CodexTacticsTests/`, `Scripts/test_map.json`,
`docs/port/claude/**`, `docs/systems/` (a short note in the cold / environment doc).
Report: `docs/port/claude/outbox/T021-cold-indoors.report.md`.

## Designer decision 2026-11-19: doorways without a leaf = OPEN ("Open, with draught")
A door gap with no leaf / no state counts as open (door weight 3 + draught). Windows sealed until a runtime state exists.
