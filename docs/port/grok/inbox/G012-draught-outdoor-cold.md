# G012 - by an open door indoors it is as cold as outside, and a campfire in a leaking building is not a shelter (GAME fix) - LIGHT, C++
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: origin/main newest (>= b9507af7, T-021 merged). Branch grok/G012-draught-outdoor-cold. Worktree CodexTactics-grok.
Start ONLY after G011 is FINISHED (one Grok lane at a time). Router: L2 (ue-l2-routine, conf 0.76). Build-fast model.

## Design gate (lead filled)
- Spec: designer 2026-11-19 (T-021 brief docs/port/claude/inbox/T021-cold-indoors-generated.md, DESIGNER UPDATE): «в радиусе 3-4
  метра от входной двери будет холодно ... чем дальше от открытой двери, тем теплее». Config Content/Data/Survival/cold_shelter.json
  (draught_full_m 3.5, draught_fade_m 10).
- Acceptance:
  (a) within draught_full_m of an open opening the cold multiplier equals the OUTDOOR multiplier at that moment (wind / blizzard
      included), fading linearly to the building's base multiplier at draught_fade_m; today it lerps from a hard 1.0
      (ColdShelterRules.cpp:118-130, `FMath::Max(1.f, Base)` / `FMath::Lerp(1.f, Base, Alpha)`), so in a blizzard the doorway is
      warmer than the street.
  (b) a campfire inside a generated building counts as "sheltered" only when that building's leak is below a JSON threshold
      (new key in cold_shelter.json, e.g. `campfire_shelter_max_leak`, default chosen so a building with all openings closed counts
      and one with >= half its weighted openings open does not); today any generated building counts.
- Proved by: unit tests in Source/CodexTacticsTests/Private/Survival/ColdShelterRulesTest.cpp: draught at 0 m = outdoor (e.g. 2.5),
  at 10 m = base, at 6.75 m = midpoint; outdoor < 1 (calm) still never colder than outdoor; campfire threshold both sides.
  Smokes ColdIndoorsSmoke, ColdSmoke, WarmIdleSmoke, EnvironmentSmoke PASS.
- Designer decision needed? none for (a). For (b) the threshold default is yours - write the number + why in the report; the lead
  asks the designer before merging if it changes play.

## Evidence so far
- Fable review 11-19 finding 8 (docs/port/claude/outbox/WORKER_LEAD_2026-11-20.report.md): "draught lerps to 1.0, not to the
  outdoor multiplier; a campfire is 'sheltered' in any generated building, even a fully leaking one."
- Caller: ColdShelterRules.cpp:180 `Result.Multiplier = FMath::Max(Result.Base, GetDraughtMultiplier(Nearest, Result.Base, Config));`
  - find where the outdoor multiplier lives (EnvironmentRules / ColdSurvivalComponent) and pass it in; the campfire check is in
  Source/CodexTactics/Private/Interactables/CampfireActor.cpp.

## Task
1. Read ColdShelterRules / ColdShelterSubsystem / ColdSurvivalComponent / CampfireActor and the T-021 report
   (docs/port/claude/outbox/T021-cold-indoors.report.md). 2. Fix (a), then (b); numbers in cold_shelter.json, no new literals.
3. Update docs/systems/survival-cold-heat.md (the draught paragraph). 4. Stop rule: > ~2 h -> WIP + exact next step.

## Never
- Change / weaken a smoke or test check, its timing or fixture. Touch maps, Content/VFX, Config/DefaultScalability.ini, the designer's
  X6 files. Push or merge main.

## Gate (all before FINISH)
build: `Scripts/build.ps1 -Remote worker` (exit 2 -> local) | `Scripts/test.ps1 -Filter CodexTactics.Survival` | smokes:
ColdIndoorsSmoke, ColdSmoke, WarmIdleSmoke, EnvironmentSmoke, WeatherSmoke via `Scripts/smoke_parallel.ps1` |
`python Scripts/Tools/typesafe_triage.py --audit-diff --agent grok` | `git diff --check`.
Allowed paths: Source/CodexTactics/**/Survival/*, Source/CodexTactics/**/Interactables/CampfireActor*, Source/CodexTactics/**/Environment/*
(read; edit only if the outdoor multiplier needs a getter), Source/CodexTacticsTests/**/Survival/*, Content/Data/Survival/cold_shelter.json,
docs/systems/survival-cold-heat.md, docs/port/grok/**.

## Report
`docs/port/grok/outbox/G012-draught-outdoor-cold.report.md` (in Russian): handover, before -> after multipliers at 0 / 3.5 / 6.75 / 10 m
in calm and blizzard, the campfire threshold + why, gate results. FINISH in chat topic `grok-G012`:
`G012 FINISH | <status> | <commit> | done: ... | NOT done: ... | gate: ... | report: <path>`.
