# Environment: day cycle, procedural weather, cold and enemy roster (phase 1 implemented: clock + weather core)

> Design of the time-of-day + weather system and how it drives cold and the enemy roster. Status 2026-10-22: phase 1 (clock, weather ladder,
> carry-over, tutorial lock, events, console) implemented - see §8; cold coupling, roster, visuals are phases 2-6.
> Visual base = the user's own `WeatherSystem_01 5.4` project (`BP_DynamivSky`, `NS_SnowStorm`, `MPC_WeatherSettings`, `MF_SnowyWeatherBlend`, sky sphere
> materials, `S_*` preset structs, `DA_Snowy` / `DA_Sunny`), migrated and stripped of rain / lightning / puddles. **No Ultra Dynamic Sky / Weather** (user decision 2026-10-22).
>
> Planned key files: `Public/Environment/EnvironmentRules.h` (pure logic), `UEnvironmentSubsystem` (authoritative state), `AWeatherPresenterActor` + the user's
> `BP_WeatherPresenter`, `UWeatherPresetAsset` (`DA_Weather_*`), `Content/Data/Environment/environment.json`, level JSON key `"environment"`.
> [system docs index](README.md) | [survival-cold-heat](survival-cold-heat.md) | [waves-horde-encounters](waves-horde-encounters.md)

## 1. User decisions (2026-10-22)

1. **Day cycle is predictable**: EarlyMorning -> Midday -> Afternoon -> Night -> EarlyMorning ... always in this order.
2. **One full day ~= 20 real minutes.** A 10-minute session started in the morning sees the morning, the midday, and comes close to the afternoon
   (~2 phases); a typical 15-minute session ~3 phases.
3. **Weather is procedural and unpredictable**: a new run can start in any weather; the sequence differs every run.
4. **Weather transitions are logical**: states form a ladder and weather only moves ONE step at a time:
   `Clear (frosty) <-> Overcast <-> Snowfall <-> Blizzard`. Blizzard never jumps to Clear (Blizzard -> Snowfall -> Overcast -> Clear);
   Overcast reaches Blizzard only through Snowfall.
5. Four weather states, all winter: **Clear** (clear sky, hard frost - not "sunny"), **Overcast**, **Snowfall**, **Blizzard**. No rain, no summer sun.
6. **Time of day and weather choose the enemy types** on the map. Example rule: clear, bright day -> marksmen (snipers) can appear (they see well through
   scopes); dark night + blizzard -> on the same location the frostbitten appear INSTEAD of the marksmen.
7. **The look of every weather state is tuned by the user in Niagara** (and presets) without code changes.

Second round (2026-10-22, later):
8. **Turn-based combat freezes the clock and the weather** at its start; rounds add no time (0 minutes per round).
9. **A weather state lasts 75 % of a time-of-day phase** (interpreted as 3:45 of a 5:00 phase - asked the user to confirm vs 75 % of the whole day).
10. **Map-placed enemies never change.** Enemies never affect the weather; the weather and the user's Wave Editor settings shape the waves.
11. **Demo 01 tutorial: fixed Night + Snowfall** - the only fixed place in the game; everywhere else time and weather run procedurally.
12. Roster rules: proposed as pairs / visibility bands / presets with custom presets (studio-review page `docs/design/roster-by-weather.html`,
    artifact https://claude.ai/artifact/5ueXAfXNHXx2Ks87emiErq).

Third round (2026-10-22, confirmed):
13. **Roster version 2 approved**: the Jev-seeded suitability table + thresholds 2.0 / 1.4 / 2.7 (§4.2). A clear night keeps the marksmen.
14. **HOUND x1.5 in hunting weather** (overcast night, snowfall, blizzard) - approved.
15. **A weather state lasts 3:45** (75 % of a 5:00 phase) - confirmed.
17. **The next level continues the time and weather where the previous level (e.g. the tutorial) ended**; only a fresh run starts
    at the default hour with a random start weather.
18. **Bosses do not depend on time of day / weather** (never substituted). CryoDrone is not an enemy in the game now - out of scope.
16. **Demo 01 tutorial waves are locked "as written"**: under its fixed Night + Snowfall the waves are mostly hounds and frostbitten, maybe a few zombies.

## 2. Day cycle

- Each phase has a game-hour range and a **real duration** (defaults: 5 min each = 20 min/day). The clock speed is per phase
  (`game hours of the phase / real seconds of the phase`), so the long night (11 game hours) still lasts ~5 real minutes.
- Default ranges (tunable): EarlyMorning 05-10, Midday 10-14, Afternoon 14-18, Night 18-05.
- The clock runs in exploration and real-time combat, stops in the tactical pause / dialogue / cutscene / menus and for the whole turn-based combat
  (clock AND weather frozen from its start, decision 8). A level sets the start hour (default: early morning); Demo 01 is locked to Night.
- Visuals: sun / moon pitch, light intensity / colour temperature, sky tint, stars / moon visibility come from curves over the hour (data, the old
  `HandleSunMoonRotation` / `HandleNightSettings` logic moved to C++ curves).

## 3. Procedural weather

- **Start state**: weighted random (tunable weights) from a run seed; the seed is logged and can be fixed (smokes, bot, a tutorial level can lock the weather).
- **Dwell time**: 75 % of a phase's real duration (3:45 at 5:00 phases), the blend included; weather changes therefore drift against the phase changes.
- **Next state**: only a neighbour on the ladder (`EnvironmentRules::PickNextWeather(Current, Rng)`): ends are forced (Clear -> Overcast, Blizzard -> Snowfall),
  middle states pick up / down by tunable weights (optionally biased by the phase, e.g. worse weather more likely at night - OPEN).
- **Transition**: a blend of `transition_seconds` (proposal 60-90 s) between the two presets; while blending, the gameplay values (cold, visibility) interpolate too.
- Never two steps inside one transition; a transition is never interrupted (the next roll waits until the blend ends).
- Saved: hour, weather, target, blend, remaining dwell, RNG state - a load continues the same weather.

## 4. Gameplay coupling

### 4.1 Visibility score
`Visibility = PhaseLight(phase) x WeatherClarity(weather)` in 0..1 (proposal: light Midday 1.0, Afternoon 0.9, EarlyMorning 0.7, Night 0.25;
clarity Clear 1.0, Overcast 0.8, Snowfall 0.5, Blizzard 0.2). One number feeds the enemy roster and (later, OPEN) sight ranges / accuracy.

### 4.2 Enemy roster by conditions (proposal, see the studio-review page)
- **Version 2 (Jev review, 2026-10-22):** `Scripts/Tools/jev_roster_matrix.py` asked Jev (128 questions) to score how each of the 6 types
  suits each of the 16 cells (0 useless .. 4 ideal) and what should replace MARKSMAN / SPITTER. Finding: the WEATHER dominates, not the night
  (MARKSMAN: clear day 3.7-3.9, clear night 3.2, overcast 2.3-2.7, overcast night 1.9, snowfall 1.4-1.8, blizzard 0.4; SPITTER clear 3.0-3.4,
  snowfall 1.8-2.0, blizzard 1.0-1.1; HOUND / FROSTBITTEN rise with worse weather to ~3.0-3.2; BRUTE / CUTTER flat). Jev's replacement picks had
  low confidence (0.15-0.41), so the user's pairs stay. Proposed rule replacing the visibility bands: a **suitability table** (6 types x 16 cells,
  seeded from Jev, in `environment.json`) + thresholds: < 2.0 -> 50 % of the pair swaps, < 1.4 -> all, HOUND > 2.7 -> x1.5 (overcast night,
  snowfall, blizzard). Custom presets = a copy of the table with edited cells / thresholds + extra rules. **Approved by the user (decision 13).**
- Version 1 (superseded by version 2 if approved):
- Visibility bands: Long >= 0.60 ("Long sight", as written), Mid 0.35-0.59 ("Grey day": 50 % MARKSMAN -> 2 FROSTBITTEN), Close 0.15-0.34
  ("White veil": all MARKSMAN -> 2 FROSTBITTEN, 50 % SPITTER -> CUTTER), Blind < 0.15 ("White dark": all MARKSMAN -> 2 FROSTBITTEN, all SPITTER -> CUTTER);
  overlay "Night hunt" HOUND x1.5 in the Night phase; BRUTE never changes.
- Pairs: MARKSMAN / FROSTBITTEN (1 -> 2), SPITTER / CUTTER (1 -> 1). Custom presets = the same rule kinds (substitute from -> to with share and ratio,
  count multiplier, ban), assignable per cell for a level or per wave. Priority: Wave Editor row lock > wave preset > level matrix > global matrix, then
  wave modifiers and `max_simultaneous_enemies`. Conditions are sampled once at wave start.

Earlier draft notes:
- Spawn entries keep their `enemy_type`; a **substitution table** in `environment.json` swaps a type when the conditions match, at spawn time:
  `{ "from": "MARKSMAN", "to": "FROSTBITTEN", "when": { "visibility_below": 0.3 } }` (night + blizzard, also night + snowfall with the default numbers);
  `{ "type": "MARKSMAN", "allowed_when": { "visibility_at_least": 0.6 } }`-style gates for types that only appear in good conditions.
- Applies to wave / runtime spawns. Already spawned enemies never change when the weather changes. Map-placed enemies never change (decision 10).
- Wave Editor shows the effective roster for each phase x weather combination.
- Roster today: HOUND, SPITTER, BRUTE, FROSTBITTEN, MARKSMAN, CUTTER. Only MARKSMAN -> FROSTBITTEN is a user rule so far; other pairs are OPEN.

### 4.3 Cold
`rate = Accumulation x Stance x Zone x Level x Wave x WeatherMult x PhaseMult x Wind x Cover x Buffer` (`FColdEnvironment` gets `WeatherMultiplier`,
`PhaseMultiplier`; the elevated-wind rule scales with the weather wind). Proposal: weather Clear 1.0 / Overcast 1.0 / Snowfall 1.25 / Blizzard 2.5 (parity with
the Blizzard camera zone); phase Night 1.3, EarlyMorning 1.15, Midday 0.9, Afternoon 1.0. Closed zones (`ZoneMultiplier 0`) stay immune.

## 5. Visual presenter and the Niagara contract

- `AWeatherPresenterActor` (C++) adopts the lighting actors already placed in the map (sun, sky atmosphere, sky light, height fog, volumetric clouds) and
  drives them from the active and target `DA_Weather_*` presets by the subsystem's blend. `BP_WeatherPresenter` (child, the user's) is where art tweaks live.
- Each preset references its **Niagara layers as soft asset references** (falling snow, ground drift / "pozyomka", frost sparkle for Clear, blizzard sheets)
  plus parameter overrides. The user can swap systems or edit them freely; code only writes these **User parameters** (the contract):
  `User.WeatherIntensity` (0..1, blended), `User.SpawnRate`, `User.Opacity`, `User.WindStrength`, `User.WindDirection` (vector),
  `User.DriftOpacity`, `User.DriftSpriteSize`. A system may ignore any of them.
- Material Parameter Collection `MPC_Environment` (from `MPC_WeatherSettings`, rain removed): `SnowAmount`, `FrostAmount`, `WindStrength`, `IsSnowing`.
- `NS_SnowStorm` was built for a third-person camera ~2 m from the ground: it needs a spawn volume around the tactical camera's view.

## 6. Phases of work

1. Core C++: enums, `EnvironmentRules` (phase from hour, per-phase clock, weather ladder RNG, blend, visibility), `environment.json`, `UEnvironmentSubsystem`,
   event-bus delegates, console `Codex.Env.SetWeather / SetHour / Seed / ClockScale`, unit tests `CodexTactics.Environment.*`.
2. Cold coupling + turn-based clock step + roster substitution at spawn (tests, `ColdSmoke` / wave smoke extensions).
3. Migration (`/Game/Environment/Weather/`), presenter, 4 presets, day curves, `WeatherSmoke` + stills.
4. Niagara layers for the tactical camera, MPC / snow / frost in the master materials, Storm idle clips in Blizzard.
5. Level JSON `"environment"` + Wave Editor tab (seed, start hour, weights, roster preview), save keys + `RestoreOrder`.
6. Further gameplay effects (sight, accuracy, scent), night darkness + flares, wind ambience, HUD clock / weather indicator.

## 8. Implementation - phase 1 (2026-10-22)

Key files:
- `Public/Environment/EnvironmentTypes.h` - `ETimeOfDayPhase`, `EWeatherState` (ladder order), `FEnvironmentConfig`, `FEnvironmentState` (saved / carried),
  `FWeatherStepResult`, `FEnvironmentLevelSettings`.
- `Public/Environment/EnvironmentRules.h` / `.cpp` - pure logic: phase from hour, per-phase clock rate (`AdvanceClock` crosses any number of phase
  boundaries), `ShouldAdvance` (frozen in the tactical pause, turn-based combat, world-AI pause - same as `TacticalHeatRules::ShouldAdvanceRealTime`),
  the ladder (`PickStartWeather`, `PickNextWeather` = always a neighbour), dwell = 75 % of the phase it started in, the blend in its last
  `weather_transition_seconds` (45), `StepWeather` (deterministic from `FEnvironmentState::RandomSeed`; leftover seconds carry; a huge step changes
  weather one rung at a time; `OldWeather` = the state that ended last).
- `Public/Data/EnvironmentTuning.h` / `.cpp` + `Content/Data/Environment/environment.json` (17 snake_case keys of `FEnvironmentConfig`, Unreal is master;
  values clamped like the UPROPERTY meta). Console `CodexTactics.DumpEnvironment`.
- `Public/Environment/EnvironmentSubsystem.h` / `.cpp`:
  - `UEnvironmentSubsystem` (`UTickableWorldSubsystem`, Game / PIE) - initialised at `OnWorldBeginPlay` only when the world has
    `ACodexTacticsGameMode` (menus never run a clock); reads `PeekLevelConfig()->Config.Environment`; `ResolveInitialState` (pure, static): locked level ->
    its hour / weather, `bLocked`; else the carried state of the previous level continues exactly (unlocked); else fresh (level `start_hour` / `weather` /
    `seed` or `default_start_hour` 6 + weighted random weather). Tick: hour first (`AdvanceClock`), then `StepWeather`, x `ClockScale` (debug).
    API: `GetState` / `GetStateRef`, `GetHour`, `GetPhase`, `GetWeather`, `GetNextWeather`, `GetTransitionBlend`, `IsLocked`, `CanAdvance`,
    `SetState` (load, phase 5), `ForceWeather(Weather, bInstant)`, `SetHour`, `SetClockScale`, `RestartWeatherSequence(Seed)`, `ApplyLevelSettings`;
    native delegates `OnPhaseChanged`, `OnWeatherTransitionStarted`, `OnWeatherChanged`, `OnStateSet` (resync for the future presenter).
  - `UEnvironmentCarrySubsystem` (`UGameInstanceSubsystem`) - the end state of a level (written in `Deinitialize`, also from the locked tutorial) for the
    next level; `UCodexFrontendSubsystem::StartNewGame` clears it (a new game starts fresh).
- Event bus (`UCodexEventBus`): `OnTimeOfDayPhaseChanged(Old, New)`, `OnWeatherTransitionStarted(From, To)`, `OnWeatherChanged(Old, New)`.
- Level JSON `"environment": { "locked", "start_hour", "weather", "seed" }` -> `FLevelCombatConfig::Environment` (`LevelJsonRules::ParseEnvironmentSettings`;
  unknown weather = ignored + warning; schema `level_config.schema.json`). `L_Demo01_A_FrozenLake.json`: locked, 23:00, Snowfall.
- Console (`CodexTacticsDev/Private/Debug/EnvironmentCommands.cpp`): `Codex.Env.Dump`, `Codex.Env.SetWeather <name> [instant]` (debug: may skip rungs),
  `Codex.Env.SetHour <h>`, `Codex.Env.ClockScale <x>`, `Codex.Env.Seed <n>`.
- Tests: `CodexTactics.Environment.Rules.*` (14), `.Tuning.*` (4), `.Level.*` / `.Carry.*` (3). Smokes: `CodexTactics.EnvironmentSmoke`
  (L_MovementTest), `CodexTactics.EnvironmentLakeSmoke` (L_Demo01_A_FrozenLake, locked).
- Not yet: save keys + `RestoreOrder` (phase 5; hook = `GetStateRef` / `SetState`), cold multipliers / roster (phase 2), visuals (phase 3-4). A quick restart
  continues the carried clock (expected).
- Built by the model router split (2026-10-22): rules + tests Opus, tuning + JSON Sonnet, subsystem Fable, contract headers + review the main session.

## 7. Open questions

- Start weights, up / down weights, phase bias on the weather.
- Does weather lower sight ranges / accuracy / hound scent for both sides.
- Which "zombie" body the tutorial's few zombies use (FROSTBITTEN on the Military Zombie / FrozenPMC body?).
- Footprints: the old decal notify or the existing RT `SnowTracks`; volumetric clouds or the 2D sky sphere only.
