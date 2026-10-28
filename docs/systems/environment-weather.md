# Environment: day cycle, procedural weather, cold and enemy roster (phases 1-3 implemented: clock, weather, cold, roster, visual presenter; phase 4a: snow cover state, MPC, presenter wiring; section 17-18: temperate climate + rain visuals)

> Design of the time-of-day + weather system and how it drives cold and the enemy roster. Status 2026-10-22: phase 1 (clock, weather ladder,
> carry-over, tutorial lock, events, console) and phase 2 (cold multipliers, roster by conditions) implemented - see §8 / §9; phase 3 visual presenter (lights / fog / post by hour + weather) §11; Niagara / snow cover phase 4.
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
19. **The Demo 01 tutorial SHOWS its night + snowfall** (weather visuals on for the lake, user decision 2026-10-22; `"visuals": false` stays available per level).
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

- `AWeatherPresenterActor` (C++) adopts the lighting actors already placed in the map (sun, sky atmosphere, sky light, height fog, volumetric clouds) - or, since
  2026-10-25, lights a map without them with its OWN rig (section 16) - and
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
4. Niagara layers for the tactical camera, MPC / snow / frost in the master materials, Storm idle clips in Blizzard (the walking storm upper body is done 2026-10-22: animation-setup "Blizzard walk").
   **Gradual snow cover (user requirement 2026-10-22, «objects are gradually covered with snow»):** port the WeatherSystem_01 shader
   chain - `MF_SnowyWeatherBlend` (snow layer over any base material by the world-up normal, Megascans `Fresh_Snow`, inputs
   `IsLandScapeBlend` / `CanAnimateSnowCoverage`) driven by the MPC scalar `SnowAmount` (old: `SnowCoverageAnimationTimeline`,
   `S_SnowyWeatherSettings.FullSnowCoverageTime` / `ShouldAnimateSnowCoverageInRuntime`). New: `SnowAmount` becomes AUTHORITATIVE STATE in
   `UEnvironmentSubsystem` (saved + carried to the next level, deterministic): it accumulates while it snows (Snowfall, Blizzard faster), holds
   under Overcast and Clear - it NEVER recedes (user decision 2026-10-22: «stops growing» in clear weather; rates in `environment.json`), and the presenter only writes it
   to `MPC_Environment` each tick. Wire `MF_SnowyWeatherBlend` into the project master materials (`M_Landscape_Master`, props / buildings,
   the characters' master, vehicles), with a per-material mask so roofs and the tops of objects whiten first and interiors / under cover
   stay clean; `FrostAmount` (Clear frost) uses the same chain with a frost texture. Tests: accumulation rules (pure), save / carry; smoke +
   stills: snow cover rising during a forced Blizzard on the lake.
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

## 9. Implementation - phase 2 (2026-10-22): cold and the enemy roster

- Config (`FEnvironmentConfig`, `environment.json`): `cold_mult_clear / _overcast / _snowfall / _blizzard` 1 / 1 / 1.25 / 2.5,
  `cold_mult_early_morning / _midday / _afternoon / _night` 1.15 / 0.9 / 1.0 / 1.3, `cold_on_locked_levels` false; block `"roster"` (enabled, half_below 2.0,
  all_below 1.4, bonus_above 2.7, bonus_multiplier 1.5, pairs MARKSMAN -> 2 FROSTBITTEN / SPITTER -> 1 CUTTER, bonus_types HOUND, `suitability` = the Jev
  6 x 16 table per type -> weather -> [EarlyMorning, Midday, Afternoon, Night]; a malformed row is rejected with a warning).
- Cold (`ColdRules`, `UColdSurvivalComponent::GatherEnvironment`): `FColdEnvironment::WeatherMultiplier` (blended Weather -> NextWeather through a
  transition, `EnvironmentRules::GetBlendedColdWeatherMultiplier`) and `PhaseMultiplier` (steps at a phase boundary). Open air =
  `ColdRules::CombineZoneWeather(Zone, Weather)` x Phase x the old factors: closed 0; zone <= 1 (shelter, standard, a wave `cold_drain_mult` <= 1) -> zone x
  weather; zone > 1 (Blizzard camera zone 2.5, a harsh wave drain) -> max(zone, weather) - a storm is never counted twice. Warming, preparation, sprint
  warm-up, scripted shocks unscaled. A locked level (Demo 01) keeps 1 / 1 (`EnvironmentRules::ShouldApplyCold`). Numbers: clear midday 0.79 %/s vs
  blizzard night 2.86 %/s for the commander in the open (x3.6; 100 % in ~35 s) - to be checked in a play-test.
- Roster (`EnvironmentRosterRules::ResolveSpawns`, hook `UWaveSubsystem::StartWave` on a copy of the wave definition; every wave start path incl. sudden
  start / horde intro / fallback waves): only when the environment is initialised and the level is NOT locked; skirmish / ambush adoption, flank breach
  packs and map-placed enemies never change; `"environment_lock": true` on a spawn entry (schema, Wave Editor `types.ts`, no UI yet) keeps it as
  written; types without a suitability row (bosses) never change. A load mid-wave restarts the wave from Preparation and resolves it once again.
  `UWaveSubsystem::GetCurrentWaveSpawns / GetCurrentWaveSpawnTotal` give the resolved roster (LevelWaveSmoke / HordeIntroSmoke count against it).
  Log: `[Env] roster wave N (Weather / Phase): A -> B enemies, K change(s)` + one line per change.
- Automated runs: a smoke (`-ExecCmds=CodexTactics.*`) or a bot run (`-CodexBot`) on an unlocked level starts NEUTRAL - Clear at 15:00 (cold x1, roster as
  written), seed 1 - unless `-EnvWeather=<name>` / `-EnvHour=<h>` / `-EnvSeed=<n>` set the start or `-EnvRandom` keeps it procedural.
- Tests: `CodexTactics.Environment.Cold.*` (8), `.Roster.*` (12), `.Tuning.*` (6). Smokes: `ColdSmoke` (new first stage: clear midday vs blizzard night),
  `EnvironmentRosterSmoke` (blizzard night: no marksmen, frostbitten x2, hounds x1.5; clear midday as written).
- Not yet: the elevated-wind rule scaled by the weather wind, Wave Editor roster preview / lock UI (phase 5), per-level roster matrix and custom presets.
- Router split: cold Opus, roster Opus, tuning Sonnet; contract, automated-run neutral start, review the main session.

## 7. Open questions

- Start weights, up / down weights, phase bias on the weather.
- Does weather lower sight ranges / accuracy / hound scent for both sides.
- Which "zombie" body the tutorial's few zombies use (FROSTBITTEN on the Military Zombie / FrozenPMC body?).
- Footprints: the old decal notify or the existing RT `SnowTracks`; volumetric clouds or the 2D sky sphere only.

## 10. Migrated assets (phase 3)

Source: the old project `WeatherSystem_01 5.4` (read-only; only copied from). Script: `Scripts/Editor/migrate_weather_assets.py` (re-runnable:
`python Scripts/Editor/migrate_weather_assets.py stage`, then the editor commandlet `-run=pythonscript -script=...` under the build lock; it renames the
staged assets with reference fixup, fixes up the redirectors of the temporary folders only, deletes them, saves only the moved assets and verifies that
every asset loads and references nothing outside `/Game/Environment/Weather` except engine content). Result: 37 assets, 78.8 MB, no missing refs; after the user dropped the volumetric clouds (2026-10-22) **32 assets, ~55 MB** remain.

**Closure method.** A read-only scan of the `.uasset` name tables for `/Game/` packages, followed transitively from the KEEP set (no editor, nothing saved).
The closure is clean: no rain, lightning, footprint or example asset is pulled in by the KEEP set.

| Group | New path (`/Game/Environment/Weather/...`) | Role |
|---|---|---|
| Sky | `Sky/SM_SkySphere`, `Sky/M_DynSkySphere`, `Sky/MI_DynSkySphere` | Sky sphere mesh, dynamic sky material (clouds, moon, stars) and its instance |
| Sky | `Sky/Textures/T_Sky_Clouds_M`, `T_MoonNASA_1K`, `T_Sky_Stars` | Cloud layer, moon, stars textures of the sky sphere |
| Snow | `Snow/Materials/MF_SnowyWeatherBlend` | Snow cover blend over a material (reads `MPC_WeatherSettings`) |
| Snow | `Snow/Materials/MF_LandscapeLayerProcessed`, `MF_ComputeLandscapeUV`, `MF_TextureBombingVoronoi` | Helpers `MF_SnowyWeatherBlend` calls |
| Snow | `Snow/Textures/T_Fresh_Snow_ugkieimdy_2K_D`, `_N`, `T_FreshSnow_ugkieimdy_2K_ORDp` (Megascans Fresh Snow, 40 MB) | Snow albedo / normal / ORD |
| Snow | `Snow/Niagara/NS_SnowStorm`, `FollowCameraTemplate` | Falling snow / blizzard system (follows the camera) and its template |
| Snow | `Snow/Niagara/M_SnowSmokeMaster_Add`, `M_SnowSmokeMaster_Add_Inst`, `T_Smoke_SubUV` | Snow particle material, instance and sub-UV smoke sheet |
| MPC | `MPC/MPC_WeatherSettings` | Old parameter collection; **becomes `MPC_Environment` later** (name kept for now, rain parameters to be removed) |
| Common | `Common/Textures/T_TilingNoisePacked` (sky sphere + snow blend), `T_VoronoiNoise_Packed` (landscape layer helper) | Shared noise textures |
| Reference | `Reference/BP_WeatheDataAsset_Base`, `DA_Snowy`, `DA_Sunny`, `S_SkyAtmosphereSettings`, `S_DirectionalLightSettings`, `S_SkyLightSettings`, `S_ExponentialHeightFogSettings`, `S_SnowyWeatherSettings`, `EWeatherType` | Old preset data assets, kept only as a look reference (the C++ presenter replaces them; `EWeatherType` still has a rain entry as a plain enum value, no rain assets) |

**Not migrated (and why).**
- Volumetric clouds (`M_VolumetricClouds_Master` + MI, `T_PackedCloudMasks`, `T_VolumeNoiseShape128`, `VT_PerlinWorley_Balanced`, and the cloud-only
  `MF_MavroVar` / `T_MacroVariation`): migrated first, then removed - user decision 2026-10-22: «volumetric clouds are not visible» from the tactical
  camera; the presenter disables the map's `AVolumetricCloud` at runtime (restored when the visuals are off).
- `BP_DynamivSky` (+ the "(1)" copy): drags rain and lightning (`NS_Raindrops`, `NS_Lightning`, `PPM_RainDropsOnScreen`, `DA_Rainy`); the C++ `AWeatherPresenterActor` replaces it.
- Rain: `NS_Raindrops`, `NS_PuddleSplashes`, `MLiquidMaster`, `MI_Raindrops` / `MI_TainSplashes` / `MI_PuddleSplash_01`, `PPM(I)_RainDropsOnScreen`, `T_RainDropsPacked`
  and `Textures/Liquid`, `MF_GenerateRainyWeatherPuddles`, `PM_Puddle_01`, `DA_Rainy`, `S_RainyWeatherSettings` - no rain in the game (decision 5).
- Lightning: `NS_Lightning`, `M_Lightning_Master`, `MI_LightningBolt_01`, `Textures/Lightning`.
- Footprints (`NS_LeftFoot` / `NS_RightFoot`, `M_FootPrint_Master`, `Textures/Footprints`) - decided later (open question).
- Landscape automaterial (`M_LandscapeAutomaterial`, `MI_Weather_LandscapeAutomaterial`), `Examples`, Houdini / Gaea / ONECOURSESOLUTION / ThirdPerson / StarterContent / MSPresets / other Megascans / maps, `Textures/Clouds/2D/T_Sky_Blue`, unused 3D cloud noises.

**Known issue (pre-existing in the old project).** UE 5.8 logs "Some expression in Material Function `MF_ComputeLandscapeUV` failed to load correctly"
(the same line is in the old project's own log). `MF_LandscapeLayerProcessed` / `MF_SnowyWeatherBlend` call it, so a material using the snow blend may fail to
translate until that function is repaired in the editor (phase 3 presenter work).

## 11. Implementation - phase 3 (2026-10-22): the visual presenter

Visual only: nothing in gameplay reads the lighting; the presenter never changes the state, the clock or the RNG (the wave / horde smokes keep
their timing). Works under `-nullrhi` (component values only; the sky recapture is skipped).

Key files:
- `Public/Environment/WeatherVisualTypes.h` - `FWeatherVisualPreset` (per weather: sun intensity x / tint / temperature offset / source angle,
  moon x, sky light x, Rayleigh / Mie x, fog density / falloff / colour / brightness x / start distance / max opacity, volumetric fog on + extinction,
  exposure bias / saturation / tint; phase-4 data SnowIntensity / FrostAmount / WindStrength), `FEnvironmentDayCurves` (dawn 6 / dusk 18, sun max
  elevation 52, night depth 35, yaw sweep, moon 28-55 deg, moon colour, `FRuntimeFloatCurve`s by hour: sun intensity, sun temperature, moon
  intensity, sky light, fog brightness, exposure bias; night tint / saturation - defaults built in the C++ constructor), `FWeatherVisualFrame`.
  Intensities are MULTIPLIERS of the values authored in the map (each map keeps its exposure balance); fog numbers and post values are absolute.
- `Public/Environment/EnvironmentVisualRules.h` / `.cpp` - pure: `GetDefaultPreset` (the 4 C++ fallbacks), `LerpPresets`, `EvalHourCurve`,
  `GetSunElevation` / `GetSunYaw` (the old BP_DynamivSky MapRangeUnclamped arc: max x sin(pi t) between dawn and dusk, below the horizon at night),
  `GetMoonElevation` / `GetMoonYaw`, `GetNightAmount`, `ComputeFrame` (hour curves x the preset blended Weather -> NextWeather by `GetTransitionBlend`).
- `Public/Environment/WeatherPresetAsset.h` - `UWeatherPresetAsset : UPrimaryDataAsset` (Weather, Visual, phase-4 `NiagaraLayers` soft refs,
  `ResetToDefaults()` CallInEditor). Instances `/Game/Environment/Weather/Presets/DA_Weather_Clear / _Overcast / _Snowfall / _Blizzard`
  (`Scripts/Editor/create_weather_presets.py`, new assets only, started from the C++ defaults; the user tunes them in the editor).
- `Public/Environment/WeatherPresenterActor.h` / `.cpp` - `AWeatherPresenterActor` (Blueprintable): spawned by `UEnvironmentSubsystem` at the
  end of `InitialiseFromWorld` (`SpawnForWorld`; a game world with the project's game mode only; since section 16 it MAY be placed in a map - then that one is used). Adopts the map's
  `ADirectionalLight` (sun = the atmosphere sun light 0, else the brightest), a second directional light as the moon (atmosphere index 1 / "Moon"
  in the name) else its own `MoonLight` component (Movable, atmosphere light index 1), `ASkyAtmosphere`, `ASkyLight`, `AExponentialHeightFog`,
  an unbound `APostProcessVolume` else its own unbound `PostProcess` component. Records the authored values (`FWeatherAuthoredLighting`) and
  drives them at `ApplyRateHz` 20 + at once on `OnStateSet`. A Static / Stationary sun: warning, not rotated (Stationary still re-lit). Sky
  light: real-time capture -> nothing; else `RecaptureSky` at most every `SkyRecaptureInterval` 2 s when the look moved. The first application
  runs synchronously in the presenter's `BeginPlay` (since 2026-10-23, section 15.5; the old 4 s `MinimapHoldSeconds` wait for the minimap
  captures is gone: the minimap now captures the weather look). Presets: soft refs
  `PresetClear / _Overcast / _Snowfall / _Blizzard` (default paths above), C++ defaults when an asset is missing. `OnEnvironmentApplied(Frame)` /
  `OnEnvironmentVisualsRestored()` BlueprintImplementableEvents for the user's `BP_WeatherPresenter`. `EnvironmentParameters` (MPC soft ref) = phase 4.
- **Volumetric clouds (user decision 2026-10-22): not visible from the tactical camera** - the presenter hides the map's `AVolumetricCloud`
  (actor hidden, component invisible + deactivated) while the visuals run and restores it when they are off; presets carry no cloud values;
  overcast / snow skies come from light, sky light, fog and post-process (later the sky sphere). Nothing references `/Game/Environment/Weather/Clouds`.
- `Public/Environment/EnvironmentVisualSettings.h` - Project Settings > Game > Codex Environment Visuals: `bSpawnPresenter`, `PresenterClass`
  (default `/Game/Environment/Weather/BP_WeatherPresenter`, used once that BP exists; C++ class otherwise). Defaults in C++, no .ini written.
- Level JSON `"environment": { ..., "visuals": false }` (`FEnvironmentLevelSettings::bVisuals`, default true; schema) -> no presenter, the
  authored lighting stays. Console variable `Codex.Env.Visuals 0|1` (runtime module): 0 restores every authored value, 1 applies again.
- Adopted on both test maps: sun `DirectionalLight` (Movable, authored 6 lux), `SkyAtmosphere`, `SkyLight` (real-time capture), `ExponentialHeightFog`
  (density 0.0436, inscattering black = lit by the sky atmosphere), 1 `VolumetricCloud` (switched off); moon and post-process own (no PPV in the maps).
- Look (stills `Saved/Screenshots/Environment/visuals/`): Clear = crisp cold light, hard shadows, low fog; Overcast = flat grey, soft shadows;
  Snowfall = fog from ~18 m, short visibility; Blizzard = dense white-grey, weak light; night = moonlit blue (moon 0.22 x the sun, sky light x2.2,
  -1.2 EV, cold tint) - the squad stays readable as dark figures with moon shadows. Dawn on the lake snow is still warm-pink (atmosphere at low
  sun) - tune `DayCurves` / the Clear preset if needed.
- Tests `CodexTactics.Environment.Visuals.*` (4): SunArc, PresetLerp, CurveDefaults (monotonic dawn / dusk, readable night floor), LevelJson.
  Smokes `CodexTactics.WeatherSmoke` (L_MovementTest) and `CodexTactics.WeatherLakeSmoke` (lake + `-LevelJson`). Stills / timelapse:
  `CodexTactics.EnvironmentShots visuals [timelapse]` (`docs/systems/build-test-smokes.md`).
- Not yet (phase 4): Niagara layers / User parameters, MPC_Environment writes, snow cover, the sky sphere, the user's `BP_WeatherPresenter`.

## 12. Implementation - phase 4a (2026-10-22): snow cover state, MPC_Environment, presenter wiring

Falling snow (`UWeatherSnowFxComponent`, `NS_Weather_Snow`) and the overlay material (`UWeatherSnowOverlayComponent`, `M_SnowOverlay`) are separate
pieces with their own files; this part is the state, the save, the MPC and the wiring.

- **State.** `FEnvironmentState::SnowAmount` (0..1) is authoritative state in `UEnvironmentSubsystem`. `EnvironmentRules::StepSnowCover(Config, State, Dt)`
  runs in the tick right after `StepWeather`. It grows only while it snows: `snow_cover_rate_snowfall` (1/480 per real second, bare to full in ~8 min)
  and `snow_cover_rate_blizzard` (1/180, ~3 min), blended Weather -> NextWeather by `GetTransitionBlend`. It holds in Clear / Overcast and **never
  decreases** (user decision 2026-10-22). It is clamped to [0, 1], and a negative rate from a hand-edited JSON cannot lower it. It is frozen whenever
  the clock is frozen (tactical pause, turn-based, world-AI pause) and on a locked level.
- **Start.** A fresh run starts at `snow_cover_start` (0.35, the arctic ground already has old snow). A locked or fresh level may set
  `"environment": {"snow_cover": 0..1}` (`FEnvironmentLevelSettings::bHasSnowCover / SnowCover`, schema). The Demo 01 lake is 0.6 (snowy night, locked:
  it stays 0.6 there). The next level CONTINUES the carried value (`UEnvironmentCarrySubsystem`, the carried cover wins over the level's).
  `RestartWeatherSequence` keeps the cover. Command line `-EnvSnow=<0..1>` (unlocked levels). Smokes and bot runs start neutral with 0.35.
- **Save** (closes the phase-5 save item for the whole state): `game_state.environment` = {hour, weather, next_weather, weather_elapsed,
  weather_dwell, seed, locked, snow_amount} (`EnvironmentRules::StateToSaveJson / StateFromSaveJson`, every key optional). It is restored by the new
  restore step **2 "Environment"** (right after Reset, before anything that samples the weather: a wave start resolves its roster from it, the cold
  reads it) through `SetState`. The level keeps its own lock. Save files without the key keep the level's resolved state.
- **MPC.** The new asset `/Game/Environment/Weather/MPC/MPC_Environment` (`Scripts/Editor/create_environment_mpc.py`, editor commandlet, new
  asset only) has scalars `SnowAmount`, `FrostAmount`, `WindStrength`, `IsSnowing` and the vector `WindDirection` (xyz direction, w strength).
  `MPC_WeatherSettings` is untouched. The presenter writes the MPC every time it applies (`UMaterialParameterCollectionInstance`): SnowAmount =
  the state, FrostAmount / WindStrength = the blended preset, IsSnowing = `ComputeIsSnowing` (blended SnowIntensity / 0.25, clamped). With the
  visuals off all values are 0.
- **Presenter wiring** (`AWeatherPresenterActor`): it creates the `SnowFx` and `SnowOverlay` components, with the default soft refs
  `/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow` and `/Game/Environment/Weather/Snow/Materials/M_SnowOverlay`. Each time it applies
  it calls `ApplySnowFx(blended SnowIntensity, WindStrength, wind direction, dt)` and `ApplySnowCover(SnowAmount, blended FrostAmount)`. It
  calls `RefreshTargets` when it starts and every `TargetRefreshInterval` (5 s). `SetVisualsEnabled` follows the visuals toggle (off at
  BeginPlay until the first application, off with `Codex.Env.Visuals 0` / EndPlay). The wind direction (`ComputeWindDirection`) drifts
  slowly: `WindBaseYaw` 35 +- `WindDriftDegrees` 25 over `WindDriftPeriodSeconds` 180. It is visual only and uses the world time.
- **API for the animation layer** (BlueprintPure on `UEnvironmentSubsystem`):
  - `GetWindStrength()`: the blended preset WindStrength. It takes the presenter's presets (DA_Weather_*) when the world has a presenter,
    else the C++ defaults; 0 before the environment is initialised. Helper: `EnvironmentVisualRules::GetBlendedWindStrength`.
  - `GetWeatherAmount(Weather)`: 1 - blend for Weather, the blend for NextWeather, else 0; the four amounts sum to 1. Helper:
    `EnvironmentRules::GetWeatherAmount`.
  - Also `GetSnowAmount()` and the debug setter `SetSnowAmount`.
- **Console:** `Codex.Env.SnowCover <0..1>` sets the cover (debug; it may lower it). `Describe()` / `Codex.Env.Dump` print `snow`.
- **Tests:** `CodexTactics.Environment.SnowCover.*` (11): RatesPerWeather, Blend, NeverRecedes, ClampAtOne, Freeze, Carry, SaveRoundTrip,
  LevelJson (incl. the shipped lake 0.6), Tuning (keys + the shipped values), PresenterParams, WindAndWeatherAmount; `Save.Policy.RestoreOrder` (12 steps).
- **Smokes:**
  - `WeatherSmoke`, stage S: the components exist; MPC_Environment has its 5 parameters with distinct ids; a forced Blizzard at x20 raises
    the cover by the Blizzard rate; a forced Clear holds it; the MPC SnowAmount equals the state; IsSnowing is 1 / 0; with the visuals off
    the MPC goes back to 0.
  - `WeatherLakeSmoke`: the lake starts at 0.6 (state and MPC).
  - `EnvironmentSmoke`: Clear holds the cover; the AI pause freezes it under Blizzard; it grows again after the pause.
  - `EnvironmentLakeSmoke`: 0.6, locked, never grows.
  - `SaveCoverageSmoke` (live `env.*` dump + 0.73 cover / 16:30) and `SaveRoundTripSmoke` (mutation) prove the save.
- **Campaign hook (after the merge of origin/main):** the env carry already rides on world teardown (`UEnvironmentSubsystem::Deinitialize`
  -> `UEnvironmentCarrySubsystem::SetCarriedState`), so a campaign transition carries the cover with no extra call. Where the campaign
  clears / applies its own carry it should treat the environment carry the same way:
  - `UCampaignSubsystem::Clear` (NEW GAME) together with `UEnvironmentCarrySubsystem::Clear` (already done in `UCodexFrontendSubsystem::StartNewGame`).
  - A campaign restart of the arrival level (`UCampaignSubsystem` keeps the squad as it arrived) should also keep the environment it
    arrived with: snapshot the carried `FEnvironmentState` in `UCampaignSubsystem::CaptureSquad` (`UEnvironmentCarrySubsystem::GetCarriedState`)
    and give it back on a restart (`UEnvironmentCarrySubsystem::SetCarriedState`). Otherwise a restart continues the state the failed attempt
    ended with (the snow keeps growing over retries).
- Not yet: `MF_SnowyWeatherBlend` in the project master materials (the overlay covers outdoor meshes for now), the frost texture chain, Storm idle clips.

### 12.b Falling snow for the tactical camera (phase 4b, 2026-10-22)

- Asset `/Game/Environment/Weather/Snow/Niagara/NS_Weather_Snow` = a retuned copy of the migrated `NS_SnowStorm` (left untouched), built by
  `Scripts/Editor/create_weather_snow_fx.py` (editor commandlet; `CODEX_SNOWFX_MODE=dump` writes the stacks to `Saved/Logs/weather_snow_fx_dump.txt`)
  through the C++ editor helper `UNiagaraAuthoringLibrary` (`Source/CodexTacticsEditor/Private/NiagaraAuthoringLibrary.*`: dump, rapid-iteration
  constants, user parameters add / rename, link / dynamic-input / clear module inputs, renderer properties, fixed bounds, compile - UE 5.8 has no
  Python API for Niagara stacks). Re-running the script deletes and rebuilds the asset: tune either there (constants at the top) or by hand.
- Flakes (`SnowFlakes`, GPU): spawn in a cylinder standing on the SYSTEM (`ShapeLocation.Shape Origin = Engine.Owner.Position`; the old
  camera-relative `NMS_CameraProperties` is disabled), radius 30 m, ground to 28 m (above the camera at max zoom), 8-18 cm sprites, 6-9 s life,
  depth-buffer collision kills them on the ground, off screen they live 30 s (the wind carries them in). Material `MI_Weather_Snowflake`
  (`M_Weather_Snowflake`: translucent unlit, particle colour x soft disk x a fade within 3-12 m of the camera, so no flake blots the view).
  Wind: `WindForce.Wind Speed = User.WindDirection`, `Wind Speed Scale = WindStrength^2 x 1100` (turbulence kept).
- Ground drift (`SnowSmoke`, CPU): a 2.5 m band on the ground (r 26 m), 14-24 puffs/s, 8-12 s, size `User.DriftSpriteSize` x random 1.2-2.5,
  opacity random 0..`User.DriftOpacity`, wind `WindStrength^2 x 1200`.
- Contract User parameters: `SpawnRate` (was Snow_SpawnRate), `Opacity` (was Max_Snow_Opacity; `Min_Snow_Opacity` 0.5 stays the low end),
  `DriftOpacity` (was Max_SnowSmoke_Opacity; `Min_SnowSmoke_Opacity` 0), `DriftSpriteSize` (was SnowSmoke_SpriteSize), new `WeatherIntensity`
  (exposed, not read by the graph), `WindStrength`, `WindDirection`.
- `UWeatherSnowFxComponent` (presenter-owned): lazy async load, own `UNiagaraComponent` (no auto-destroy, not pooled), each tick placed on the
  view focus (`ATacticalCameraPawn::GetViewCentre`, else a ground trace along the view) moved UPWIND by `UpwindOffsetAtFullWind x wind^2`
  (40 m in a blizzard: flakes cross the view instead of being born in it); intensity smoothed (0.35 / s up, 0.25 / s down), under 0.02 a soft
  `Deactivate` (the flakes in the air land); never loads / spawns without a renderer (`FApp::CanEverRender`); visuals off = immediate off + hidden.
  Mapping in `WeatherSnowFxRules` (`FWeatherSnowFxTuning`, editable on the component): spawn rate 12000 x intensity^2 (Snowfall 0.6 -> 4320 / s,
  Blizzard 12000 / s; was 24000 until the cost measurement below), opacity 0.7..1, drift from intensity 0.4 up to opacity 0.65, drift size 250..500. Tests `CodexTactics.Environment.SnowFx.*` (3).
- Stills / clips: `CodexTactics.EnvironmentShots snow` (`Saved/Screenshots/Environment/snow/`, the lake: Snowfall 12:00, Blizzard 12:00 + 40 m,
  Snowfall 23:00, Clear 12:00 off; 10 s clips per snowing weather, 15 fps).
- Tune by hand (NS_Weather_Snow / MI_Weather_Snowflake): flake size / life, cylinder radius / height (keep `UpwindOffsetAtFullWind` in step),
  the `Multiply_Float001.B` wind speed constants, the drift spawn rate, MI `NearFadeStart` / `NearFadeLength` / `Softness` / `Brightness`;
  density per weather on the presenter's `SnowFx` component (`Tuning`). GPU cost: up to ~90k flakes alive in a blizzard (12000 / s x 7.5 s), see Cost.

- Cost (2026-10-22, `CodexTactics.EnvironmentShots snowperf`, 1920x1080 offscreen, -fps=60, ~3 s average after a 9 s settle; GPU ms from the RHI, stills in
  `Saved/Screenshots/Environment/snowperf/<map>_<snowfall|blizzard|off>[_far].png`, the 24000 / s stills in `snowperf/before/`). "off" = Blizzard weather with the
  snow Niagara system hidden (isolates the FX). Camera 26 m = the default tactical distance, 40 m = max zoom. Noise about +-0.2 ms.

  | Scene | Camera | off | Snowfall 24k / 12k | Blizzard 24k / 12k | Blizzard delta 24k / 12k |
  |---|---|---|---|---|---|
  | Frozen Lake (A) | 26 m | 6.86 / 6.68 | 6.95 / 6.90 | 7.15 / 7.04 | +0.29 / +0.36 |
  | Frozen Lake (A) | 40 m | 7.38 / 7.39 | 8.17 / 7.99 | 8.03 / 7.69 | +0.65 / +0.30 |
  | L_MovementTest wave 2 (57 enemies) | 26 m | 6.35 / 6.28 | 6.56 / 6.47 | 7.04 / 6.55 | +0.69 / +0.27 |
  | L_MovementTest wave 2 (57 enemies) | 40 m | 6.23 / 6.50 | 7.80 / 7.26 | 7.37 / 7.04 | +1.14 / +0.54 |
  | Frozen Factory (B, level 0.1) | 26 m | 7.47 / 7.38 | 7.61 / 7.53 | 7.99 / 7.56 | +0.52 / +0.18 |
  | Frozen Factory (B, level 0.1) | 40 m | 7.72 / 7.52 | 9.14 / 8.67 | 8.87 / 8.30 | +1.15 / +0.78 |

  Render / game thread and draws are unchanged by the snow (+-2 draws: the flakes + drift emitters; game thread 3-4 ms on the levels, 13-14 ms in the wave).
  The budget is <= ~0.8 ms GPU in a blizzard at 1080p: at 24000 / s the max zoom exceeded it (+1.1 ms on the factory and the wave), so `SpawnRateAtFull` went to
  12000 (about 90k flakes alive; Snowfall 4320 / s, the ratio Blizzard : Snowfall stays 2.8 : 1, the stills show the blizzard clearly heavier). Re-measure after
  touching the spawn rate, flake size / life or the MI.

### 12.c Snow overlay on world meshes and the campaign restart rule (2026-10-22)

**Snow overlay (`M_SnowOverlay`)**
- The material is TRANSLUCENT: masked overlays are invisible in UE 5.8 (drawn under the own mesh). A tiny WPO push toward the camera prevents z-fighting.
- The mask is world-up against a threshold driven by `SnowAmount` plus noise, so the snow creeps down the sides.
- Targets: outdoor `UStaticMeshComponent`s within 60 m of the view focus, nearest first, capped by CVar `Codex.Env.SnowOverlayMax` (250). `Codex.Env.SnowOverlay 0/1` switches the feature.
- Skipped: ISM / HISM / foliage / pawns / landscape / the overlay's own components / tag `NoSnow` / translucent meshes / radius < 35 cm / wider than 60 m / flat slabs > 20 m (low, wide ones and floor templates take the ground overlay instead, §12.d) / meshes under a roof (an upward trace) / meshes inside Closed or Shelter `ACameraZoneVolume`s (the zone's own roof counts as outside).
- Nanite meshes are skipped (the overlay is not drawn on Nanite); `Codex.Env.SnowOverlayNanite 1` switches them to the fallback mesh.
- Cost: +0.3 ms GPU / +18 draws on a building view, ~0 in a wave fight. The set is rescanned every 3 s or on camera move.

**Campaign restart rule**
- The environment carry (`UEnvironmentCarrySubsystem`) holds the END state of the level just left. `UEnvironmentSubsystem::Resolve` records the state the level was ENTERED with (`RecordEntryState`; unset = fresh start).
- Every restart of the level (Ctrl + X, the mission-failed Restart button; both go through `UMissionSubsystem::RestartMission`) calls `RestoreEntryState` BEFORE `OpenLevel`: the carry is set back to the entry state (or cleared when the level was entered fresh). Snow cover and time of day therefore no longer accumulate across failed attempts.
- NEW GAME (`Clear`) forgets both. Test `CodexTactics.GameFlow.Campaign.EnvironmentRestore` (0.40 / 09:00 -> end 0.80 / 14:00 -> restart -> 0.40 / 09:00).

### 12.d Snow on the ground: the ground overlay M_SnowGroundOverlay (phase 4d, 2026-10-23)

User decision 2026-10-23: «meanwhile dust the current floors». The levels have no landscape yet (the lake: `SM_Template_Map_Floor` slabs with
the engine `MI_ProcGrid`; MovementTest / Frozen Factory: engine-cube floors with `WorldGridMaterial`), so the floors get a second overlay
material. No map, config or existing material is changed.

**Material** (`Scripts/Editor/create_snow_ground_material.py`, editor commandlet, own assets only, rebuilt in place on a re-run):
`/Game/Environment/Weather/Snow/Materials/M_SnowGroundOverlay` + `MI_SnowGroundOverlay`.
- Translucent, Surface ForwardShading, world-space normal, the camera-ward WPO nudge of M_SnowOverlay (0.5 cm + 0.0005 x distance).
  Masked overlays are invisible in UE 5.8.
- Snow dust: Fresh Snow albedo / normal / ORD projected from above (world XY, 3 m), upward faces only (`UpMin` 0.55).
- Coverage = `MPC_Environment.SnowAmount` against a two-scale noise field from `T_TilingNoisePacked` (24 m macro, 5.2 m mid). Each channel
  is normalised by the texture's measured statistics (R 0.31 +- 0.175, G 0.50 +- 0.19, B 0.42 +- 0.21), so the field is ~N(0.5, 0.2).
  - Threshold `lerp(1.05, 0, SnowAmount^0.7)`: 0.35 ~ 40 % in patches, 0.7 ~ 90 %, 1 ~ 99 %.
  - Soft edges (`EdgeSoft` 0.11). A light cover is thin (`ThinOpacity` 0.4) and speckled by a 37 cm grain.
  - From 0.4 up, a thin film (`FilmOpacity` 0.4) covers the bare spots, so they never read as black holes.
  - Wind-swept thinner spots stay (`BareSpots` 0.3), so the cover is never a white sheet.
- Albedo: lifted by gamma 0.6, so the snow is brighter than the grey floor. Detail flattens beyond 22 m (no far moire). Macro shading +-8 %.
- `FreshSnow` (MPC): brighter and rougher. `FrostAmount`: a smoother crust with more glints. Sparkle = rare fine-noise points with a
  sharper highlight, fading out over 15-45 m.
- Interior zones: up to 6 rectangles (`Zone<i>A` = centre XY + half extent, `Zone<i>B` = cos / sin yaw, top Z, bottom Z), written by the
  component into its dynamic instance. Pixels inside the nearest closed / shelter camera zones stay bare (40 cm soft edge, from the zone
  bottom - 1 m to its top).
- Interactive snow (`docs/design/INTERACTIVE_SNOW.md` contract, **wired**). The material reads `RT_SnowInteraction` (RG16F 2048²) at
  UV = (WorldXY - origin) x invSize from `MPC_Environment.SnowTrackWindow`, only where w > 0.5 and inside the window (2 m edge fade).
  - Height cm = G(1-R)·`MaxBermCm` 3 - R·`MaxDepthCm` 8 (R, G saturated).
  - Three samples (centre, +x, +y texel) in one custom node with one sampler: the height plus a finite-difference normal.
  - Compacted (R) = x0.8 colour, roughness -0.25, a slight occlusion in the print, and a thin dusting shows the ground through a print.
  - Berm (G) = x1.06, roughness +0.08.
  - Bump-offset parallax on the snow UVs (`ParallaxScale` 0.6).
  - An empty RT or w = 0 gives the plain dust.
  - The script compiles the track part only when the RT and the MPC vector exist. Otherwise it logs "track read PENDING".
    `CODEX_GROUND_TRACK_DRYRUN=1` compiles it against stand-ins.
- Stats (SM6, `MaterialEditingLibrary.get_statistics`):
  - With the track read: VS 367 / PS 2057 instructions, 7 samplers. 9 texture samples: 3 noise + 3 snow + 3 RT in the custom node.
    The noise and snow samples share one wrap sampler (`SSM_Wrap_WorldGroupSettings`).
  - Without the track read: PS 1979, 6 samplers.
  - For scale: M_SnowOverlay PS 1883, WorldGridMaterial PS 225. The forward-lit translucency base dominates.

**Ground mode** (`UWeatherSnowOverlayComponent`, `SnowOverlayRules`):
- A mesh is ground (`IsGroundShape`) when it is wide in both directions (narrow side >= `GroundMinWidthCm` 3 m: a 134 m low fence is not
  ground) and one of these holds:
  - it is a floor template (`IsFloorTemplateMeshName`: `Template_Map_Floor`) and low (height < 25 % of the footprint);
  - it is a flat slab >= 20 m (height < 5 %);
  - it is wider than 60 m and low (< 25 %; a sky dome stays `TooLarge`).
- `Classify` returns `Ground` for these. They no longer get M_SnowOverlay and become ground candidates instead.
- `ClassifyGround`: the same checks as the objects (instanced, pawn, landscape, hidden, own overlay, `NoSnow` tag, translucent-only, Nanite
  unless `Codex.Env.SnowOverlayNanite 1`), then `NotGround`, `Interior` (the slab inside a zone), `Covered` and `Accept`.
  - A slab is covered when MOST of its 5 sample points (the centre + 4 quarter points, `GroundCoverSamplePoints` / `IsGroundCovered`)
    have a roof above. One hut on a field does not decide for the whole floor.
- Ordered by the nearest EDGE to the view focus (`DistanceSqToBox2D`) within the 60 m radius. Cap `Codex.Env.SnowGroundMax` (32), switch
  `Codex.Env.SnowGround 0/1` (the object overlay keeps `Codex.Env.SnowOverlay` / `SnowOverlayMax`).
- One `UMaterialInstanceDynamic` of `GroundMaterial` (default `MI_SnowGroundOverlay`) per world holds the zone rectangles; it goes on every
  ground slab.
- Restored exactly like the objects: visuals off / snow + frost 0 / CVar 0 / EndPlay set the overlay back to none where it is still ours,
  and give the Nanite fallback back.
- Log line `[Env] snow ground: N slab(s) of M ground candidate(s) ...` next to the object line.
- Meshes per map (tactical start view):

  | Map | Ground slabs | Object meshes | Notes |
  |---|---|---|---|
  | Lake | 1 (of 2) | 3-4 | Floor2's edge is beyond 60 m; TooLarge 4 = sky sphere, 2 low fences, 1 more |
  | Frozen Factory | 3 | 20 | |
  | MovementTest | 2 | 22-26 | wave 2 view |

- Known limit: roofs that are not a camera zone (a hut, the factory's open platform) do not hide the ground snow under them.

**Tests** `CodexTactics.Environment.SnowOverlay.*` (+3, 7 total):
- `GroundClassify`: the shipped floors are ground. Crate, low fence, wall, 18 m roof, sky dome, building and stood-up template are not.
  The same rules as the objects apply, Nanite with the fallback, thresholds, template names.
- `GroundCover`: the sample points, the majority rule, the nearest-edge distance and order.
- `GroundZones`: the C++ mirror of the material's zone HLSL (a turned shelter, the soft edge, the top / bottom bounds, an unused slot).
- `Filter`: the old "80 m mesh = TooLarge" case is now a sky dome (wide AND tall); an 80 m low mesh is `Ground`.

**Stills / movie / cost** (`CodexTactics.EnvironmentShots grounddust [nomovie] [noperf] [notracks]`, run on the map it starts on; the lake
with `-LevelJson=L_Demo01_A_FrozenLake.json`). The run uses 1920x1080 offscreen, `-benchmark -fps=30`, Clear 12:00 and the tactical camera's
numbers (pitch -35.26, vertical FOV 30, 26 m) on the squad leader.
- Output `Saved/Screenshots/Environment/grounddust/`:
  - `<map>_ground_000/035/070/100.png`;
  - `movie_<map>/` (0 -> 1 in 10 s, 15 fps; `grounddust_<map>.mp4`);
  - `<map>_tracks_loop[_close].png` (the leader walks a 4.5 m loop, the squad follows);
  - `L_MovementTest_wave2_ground_070.png`;
  - `grounddust_sheet.png`.
- GPU A/B at SnowAmount 0.7 (`Codex.Env.SnowGround` 0 vs 1, the object overlay on in both; off / on / off / on, 150 frames each after
  60 frames of settling; log `[GroundDustPerf]`):

  | Scene | Camera | Ground off | Ground on | Delta | Draws |
  |---|---|---|---|---|---|
  | Frozen Lake (A), 1 slab | 26 m | 5.67 | 6.37 | +0.70 | +2 |
  | Frozen Lake (A) | 40 m | 6.34 | 7.05 | +0.72 | +2 |
  | Frozen Factory (B), 3 slabs | 26 m | 5.97 | 6.52 | +0.55 | +2 |
  | Frozen Factory (B) | 40 m | 6.60 | 7.12 | +0.52 | +2 |
  | MovementTest wave 2 (57 enemies), 2 slabs | 26 m | 5.84 | 6.34 | +0.50 | +3..+15 (enemies vary) |
  | MovementTest wave 2 | 40 m | 6.24 | 6.69 | +0.45 | ~0 |

  - The cost is one forward-lit translucent pass over the visible floor (~0.45-0.7 ms at 1080p). Render and game thread are unchanged.
  - Cheaper options if needed: `TLM_Surface` (translucency-volume lighting: loses the sharp character shadows on the snow), or a DBuffer
    snow decal on the floors (deferred lighting, needs the characters to not receive decals). Both change the look; not done.
- Smokes after the change: WeatherSmoke, WeatherLakeSmoke, HordeIntroSmoke, MinimapSmoke, FrozenFactoryStartSmoke all PASS (they run
  without a renderer, so the overlay itself is covered by the stills above).

## 13. Landscape material fix + snow functions (2026-10-23)

User approval 2026-10-23 («Давай», weather chat): fix the existing `M_Landscape_Master` graph and add the snow / interactive-snow functions so the
material is ready for a future terrain. No map used the material then (no level has a landscape yet).

**What was broken (SM6, as found)** - 2 compile errors, the default material would have been used in game:
- `(Node Multiply) Arithmetic between types float2 and float3`: the slope mask read `PixelNormalWS.rgb` (acos of a float3) and the ice mask
  `WorldPosition.rgb` (a per-channel SmoothStep), so both masks were float3; the old track sample `RT_SnowTracks.rg` (float2) was multiplied by
  `1 - slope` (float3).
- `(Node PixelNormalWS) Invalid node used in vertex shader input`: the World Position Offset track depth (`TrackMaxWPODepth`) read the
  PixelNormalWS slope mask.

**What was fixed (the user's graph and intent kept)**
- Slope mask: `PixelNormalWS.z` -> clamp -> acos -> degrees -> SmoothStep `RockSlopeMinAngle` / `RockSlopeMaxAngle` (scalar).
- Ice mask: `WorldPosition.z` -> SmoothStep `IceMaxHeight` -> `IceMinHeight` (scalar; ice below -100..-250 cm).
- The old track sample reads `RT_SnowTracks.r` only (a scalar mask), then the old track nodes are taken out of use (below).
- The landscape WPO is disconnected (useless at landscape vertex density, invalidates VSM). The `04_SnowTracks` parameters (`TrackCenterLocation`,
  `TrackAreaSize`, `TrackIndentColor`, `TrackRoughnessDrop`, `TrackMaxWPODepth`, `RT_SnowTracks`) and their nodes stay in the graph, unused (the
  user may delete them); every other parameter keeps its name, default and group. The interactive window replaces `TrackCenterLocation` /
  `TrackAreaSize` (below).

**Snow functions** (`/Game/Environment/Weather/Snow/Materials/`, built by the script; all neutral at 0):
- `MF_SnowPowder` (group `08_SnowPowder`: `PowderTiling` 900 cm, `PowderUpMin` 0.6, `PowderContrast` 3, `PowderStrength` 1): powder on the bare layers =
  `SnowAmount` x world up (vertex normal; the up threshold sinks from 0.97 to `PowderUpMin` as the cover grows) x (rock + ice) x noise patches
  (`T_TilingNoisePacked`, shared wrap sampler), lerping toward the existing `SnowBaseColor` / `SnowRoughness`. Also outputs `SnowWeight`
  (snow layer + powder) for the next two.
- `MF_SnowSurface` (group `09_SnowSurface`: `FreshBrightness` 0.06, `FreshRoughness` 0.9, `FreshNormalStrength` 0.1, `MicroTiling` 140 cm,
  `FrostRoughness` 0.3, `FrostOnBare` 0.35, `GlintTiling` 25 cm, `GlintThreshold` 0.46, `GlintParallax` 0.6, `GlintStrength` 1, `GlintEmissive` 1.5):
  MPC `FreshSnow` (written by the interactive-snow subsystem; rises while it snows) = brighter, rougher, a micro normal; `FrostAmount` = lower
  roughness + a view-dependent glint (two lookups of the same noise, one shifted by the camera vector; specular 0.5 -> 1, roughness -> 0.05, a small
  emissive x the glint); fresh powder hides most of the rime.
- `MF_SnowInteraction` (group `07_SnowInteraction`: `MaxDepthCm` 20, `MaxBermCm` 6 (the §14 contract), `EdgeFadeCm` 200, `NormalSampleCm` 8,
  `TrackNormalStrength` 0.25, `CompactDarken` 0.12, `CompactRoughness` 0.45, `CompactAO` 0.25, `BermBrighten` 0.06, `BermRoughness` 0.9,
  `BumpOffsetScale` 1, `CrushTiling` 18 cm, `CrushStrength` 0.1): reads `RT_SnowInteraction` through `MPC_Environment.SnowTrackWindow`
  (origin = the window's min corner, 1/size, enabled; §14); height = `G·(1−saturate(R))·MaxBermCm − saturate(R)·MaxDepthCm`; normals from 3 RT
  samples (one shared clamp sampler); bump offset (the crushed-snow detail inside a print is looked up where the view ray meets the lowered
  surface); compacted = darker / smoother / AO, berm = brighter / rougher; 2 m edge fade; a disabled window (`.w` 0) = exactly no effect.
- In the master: MPC `SnowAmount`, `FrostAmount`, `FreshSnow`, `SnowTrackWindow`; texture object parameters `SnowNoiseTexture`
  (`10_SnowTextures`) and `SnowInteractionRT` (`07_SnowInteraction`, default `RT_SnowInteraction`). Outputs: BaseColor / Roughness / Normal / AO
  from `MF_SnowInteraction`, Specular / Emissive from `MF_SnowSurface`. Everything that feeds the Normal output is vertex-normal based (a second
  rock mask from `VertexNormalWS` with the same angles): PixelNormalWS may not feed Normal. The base layers keep the PixelNormalWS slope mask.
- `MI_Landscape_Master`: its `RT_SnowTracks` override now points at `RT_SnowInteraction` (the old 1024² RGBA16F `RT_SnowTracks` is no longer loaded
  through the MI; nothing draws into it).

**Stats** (`Scripts/Editor/landscape_snow_patch.py`, `UMaterialEditingLibrary::GetStatistics`, PCD3D_SM6, base pass, local VF):

| State | Errors | VS instr. | PS instr. | Samplers | Texture samples (PS) |
|---|---|---|---|---|---|
| As found | 2 | n/a | n/a | n/a | n/a |
| After the fix | 0 | 148 | 233 | 2 | 4 |
| After the snow functions | 0 | 148 | 355 | 3 | 10 |

+122 PS instructions, +6 texture samples (3 RT + 3 noise), +1 sampler (the two shared samplers replace `RT_SnowTracks`' own): well under the
landscape limit of 16 (the landscape's weightmaps use shared samplers too). No compile warning; the terrain run compiled the landscape vertex
factory shaders without a material warning.

**Script** `Scripts/Editor/landscape_snow_patch.py` (`CODEX_LANDSNOW_MODE`): `dump` (read-only, as before), `patch` (idempotent: the fix sets the mask
bits; its own nodes carry the description `landsnow:<key>` - an already patched graph is detected by them and only those are rebuilt; the MFs are
emptied and rebuilt; saves only the master, the MI and the 3 MFs; logs `Saved/Logs/landscape_snow_patch.txt`, the first run on an unpatched graph
also `landscape_snow_patch_first.txt` with the "after the fix" stats), `stats` (read-only, `Saved/Logs/landscape_snow_stats.txt`). Run as a
commandlet under the build lock with `-AllowCommandletRendering`. Note: `MaterialEditingLibrary.DeleteAllMaterialExpressionsInFunction` leaves
expressions behind in 5.8 (stale outputs shadow new ones) - the script deletes them one by one.

**Backups** (before the first patch): `Saved/Backups/landscape_snow_20261022_233432/` with `manifest.txt` (sha1 of `M_Landscape_Master`
bd5b6102..., `MI_Landscape_Master` 45b95f1a..., `M_SnowTracks_Fade`, `MPC_Environment`); verified unchanged before patching; the final assets were
produced by a clean patch from these originals (restore = copy the two files back).

**Test map** `/Game/Environment/Weather/Test/L_SnowTerrainTest` (agents' map, `Scripts/Editor/create_snow_terrain_test_map.py`; re-running replaces it):
a 101 x 101 m landscape (4 x 4 components of 63 quads, 40 cm) with `MI_Landscape_Master`: a flat plateau (snow), a 56° ridge to the north (rock),
a 4.2 m bowl to the east (ice below -250 cm), a 19° slope to the west; sun, sky light, sky atmosphere, fog, PlayerStart (the project's game mode
spawns the squad; the environment and the interactive snow run), NavMeshBoundsVolume. The landscape comes from the editor helper
`ULandscapeAuthoringLibrary::CreateLandscapeFromHeights` (`Source/CodexTacticsEditor/Private/LandscapeAuthoringLibrary.*`).
Captures: `CodexTactics.EnvironmentShots terrain [nomovie] [notracks]` on that map (`EnvironmentTerrainShotsCommand.cpp`) ->
`Saved/Screenshots/Environment/terrain/`: `terrain_snow_000/050/100.png` (Clear 12:00 overview), `night_frost.png` / `night_fresh.png` (23:00,
Clear vs Snowfall with FreshSnow ~1), `tracks_loop.png` / `tracks_close.png` / `crater_close.png` / `tracks_off.png` (a walked loop + a forced
Crater stamp; off = `Codex.Env.SnowTracks 0`), `movie/` -> `terrain_10s.mp4` (SnowAmount 0 -> 1, camera orbit, the leader walking).
Tests: `CodexTactics.Environment.LandscapeMaterial.Graph` (every user parameter with its default / group, Z-only masks, no WPO, the 3 MFs drive
the outputs, the MI no longer loads `RT_SnowTracks`) and `.EncodeHeight`; `CodexTactics.Editor.Materials.VfxGraphsConnected` still passes.
The lake, L_MovementTest and Frozen Factory load with rendering without a material error (they do not use the material).

**Assigning it to a real terrain:** select the Landscape actor -> Details -> Landscape Material = `MI_Landscape_Master` (or a child MI of it for
per-level tuning). No paint layers are needed: rock / ice / snow are automatic (slope, height), so the layer list stays empty. Tune per level in the
MI: `IceMaxHeight` / `IceMinHeight` are absolute world Z (cm) - set them around the level's frozen-water height; `RockSlopeMin/MaxAngle` for the
rock band. Snow cover, fresh snow, frost and tracks come from `MPC_Environment` at runtime (presenter + interactive-snow subsystem); tracks need the
landscape inside the 64 m window around the view (automatic). Keep `MaxDepthCm` / `MaxBermCm` in step with the stamp set (§14 contract).

## 14. Interactive snow (phase 1, 2026-10-23): core + walkers

Plan: `docs/design/INTERACTIVE_SNOW.md` §2-§4 phase 1. UE-only (no Godot reference), visual only: nothing in the game reads it; no gameplay class
was changed. The ground look (a material reading the RT) is phase 2 / the ground overlay (§12.d).

**Contract (the ground materials rely on it; keep it stable)**
- Render target `/Game/Environment/Weather/Snow/Tracks/RT_SnowInteraction`: **RG16F** (`RTF_RG16f` = `PF_G16R16F`), 2048², clamp, cleared black.
  R = compaction depth 0..~4, G = berm 0..~4. Reader: height (cm) = `G·(1−saturate(R))·MaxBermCm − saturate(R)·MaxDepthCm`, with the material
  parameters MaxDepthCm 20 / MaxBermCm 6 on the reader side.
- `MPC_Environment` (`Scripts/Editor/create_environment_mpc.py`, now idempotent: it appends the missing parameters and keeps the existing ones with
  their ids): vector `SnowTrackWindow` = (origin X cm, origin Y cm, 1 / size cm, enabled 0/1); scalars `SnowRefill` (the blended preset
  SnowIntensity) and `FreshSnow` (0..1: rises over 120 s of full snowfall, falls over 600 s after it; visual only); all default 0.
  UV = (WorldXY − origin.xy) × (1 / size). `.w` = 0 whenever nothing is drawn (feature off, no renderer, visuals off): readers treat the RT as empty then.
- Test `CodexTactics.Environment.SnowInteraction.Contract` pins the RT format / size / clamp and the MPC parameters (defaults 0, distinct ids).

**Assets** (`Scripts/Editor/create_snow_interaction_assets.py`, editor commandlet; all under `/Game/Environment/Weather/Snow/Tracks/`; the user's
`Content/VFX/SnowTracks` and `M_Landscape_Master` are untouched)
- `RT_SnowInteraction` (kept when it exists; only format / size / addressing are re-applied).
- `M_SnowStamp`: unlit ADDITIVE; procedural shapes in local UV picked by the vertex colour (R = shape id × 16 + 8, G = depth / 4, B = berm / 4 in
  8 bits; the canvas stores colours with `ToFColor(true)`, `SnowInteractionRules::EncodeStampColor` pre-compensates): 0 boot (toe + heel), 1 paw
  (pad + 4 toes), 2 body (rounded box), 3 crater with a rim, 4 dent, 5 segment cross-section (trails). A print fills 72 % of its quad; the berm
  lies in the margin.
- `M_SnowInteraction_Fade`: unlit MODULATE, `RT *= (FadeR, FadeG)`.
- `M_SnowTracks_Shift`: unlit OPAQUE copy, `out = clamp(Source(uv + Offset).rg, 0, ClampMax 4)`, 0 outside [0, 1].
- `DA_SnowStamps` (`USnowStampSet`: `FSnowStampTable Stamps` + `FSnowInteractionTuning Tuning`): created once from the C++ defaults, then tuned by
  hand (the script keeps it). Without the asset the C++ defaults are used (the tests never need it).
The three materials are the script's own (rebuilt on every run).

**`USnowInteractionSubsystem`** (`Environment/SnowInteractionSubsystem.*`, `SnowInteractionWalkers.cpp`; pure logic in `SnowInteractionRules`)
- `UTickableWorldSubsystem`, Game / PIE only, **not created without a renderer** (`FApp::CanEverRender`: every `-nullrhi` smoke / bot run is
  untouched) or on a dedicated server. Inactive (nothing drawn, `.w` = 0) while the weather presenter is not applying (`Codex.Env.Visuals 0`, a level
  with `"visuals": false`), with `Codex.Env.SnowTracks 0`, or when an asset is missing (one warning). The RT is cleared at world begin (a restart
  reloads the world), on (re)activation and at the end (it is an asset: PIE shares it with the editor). No Display logs (Verbose only).
- Window: 64 m (Low effects quality: 1024² / 48 m), centred on the tactical view centre (`ATacticalCameraPawn::GetViewCentre`, else a point 15 m
  below a cinematic camera) + `WindowLeadCm` 800 along the view yaw, snapped to 256-texel steps (8 m; 12 m at Low) with hysteresis (it re-snaps when
  the centre is 0.75 step off: no flip-flop on a step boundary). A scroll = `M_SnowTracks_Shift` RT → scratch (a transient RT of the same format) →
  RT by whole texels (exact, no blur) plus the MPC vector in the same frame; a jump past the whole window clears.
- Fade on WORLD time (pauses with the game): `exp(−rate·dt)`, rate = `FadeBaseRate` 1/900 s × (1 + `FadeSnowBoost` 12 × SnowRefill): e-fold 15 min
  in Clear, ~110 s in Snowfall (0.6), ~70 s in a blizzard. At most `FadeHz` 5 passes / s, and a pass waits until it changes the value by ≥ 0.4 %
  (`MinFadeStep`: the RT is 16-bit float, a smaller step rounds back and never fades): 5 Hz in snowfall, one pass every ~3.6 s in Clear. A clamp
  pass (a shift by 0) every 10 s bounds the additive pile-up at 4.
- Stamps: ONE canvas draw per frame (`BeginDrawCanvasToRenderTarget` + `K2_DrawMaterialTriangle`, 2 triangles a stamp), at most
  `MaxStampsPerFlush` 512 (the farthest from the view dropped first), stamps outside the window culled. The yaw / size variation comes from a hash
  of the position: the game's random stream is never touched.
- Walkers, polled at `PollHz` 20 through the registries (no actor iterators; `Combat.EnemyRegistry.HotPathsUseRegistry` scans both files):
  - operatives (`USquadSubsystem::GetMembers`): boot prints alternating ±12 cm at a 70 cm stride; Prone stance / prone dive = a 60 cm body furrow;
    knockdown (rising edge of `IsKnockedDown`) = a body imprint; hidden (in the APC) = nothing;
  - enemies (`UEnemyRegistrySubsystem::GetEnemyCharacters`, by archetype): Base / Spitter / Marksman boots, Brute big boots (52 cm, 120 cm stride),
    FrostHound / Cutter paws, Frostbitten a 28 cm drag furrow, Cryo Drone nothing; a knockdown imprint (beast / brute / humanoid sizes); dying = no prints;
  - the APC (`UVehicleSubsystem::GetVehicles`): twin ruts ±105 cm (45 cm wide); its ground is only checked while `GetWheelSpinRate` ≠ 0 or it moved;
  - turn-based relocation (`UTurnBasedCombatSubsystem::GetRelocatingObject`): the object is watched for 10 s (it moves after the order cleared it)
    and leaves a 55 cm drag trail (a jump of up to 15 m still draws its segment);
  - death imprints from the event bus (read-only): `UCodexEventBus::OnSoldierDowned` (operative), `OnGameplayEventDetailed` with
    `GameplayEvents::EnemyKilled` (enemy, by archetype; drones none).
  A stride / trail restarts without a stamp after a teleport (> 4 m in one poll) or off snow.
- Ground gate (no trace for characters: `CharacterMovement->CurrentFloor`): a landscape (`LandscapeHeightfieldCollisionComponent`, matched by class
  name), a floor tagged `SnowGround` (component or actor tag), **or any non-physics static mesh / BSP floor while `Codex.Env.SnowTracksAnyGround`
  is 1** (default 1 until the levels have a real terrain: today every ground is `SM_Template_Map_Floor`; set it to 0 once the snow terrain exists).
  Never inside a Closed / Shelter `ACameraZoneVolume` (gathered at begin play and every 10 s). Vehicles / dragged objects / bodies: one short
  trace down.
- Console: `Codex.Env.SnowTracks 0/1`, `Codex.Env.SnowTracksRes 0` (auto) | 1024 | 2048 | 4096 (a scalability CVar), `Codex.Env.SnowTracksAnyGround 0/1`,
  `Codex.Env.SnowTracksFadeScale` (debug fade multiplier). Stats: `stat SnowInteraction` (tick / poll / draw / passes, stamps drawn / dropped,
  walkers). `GetMetrics()` holds cumulative counters + CPU seconds; `DebugRunExtraPasses` is for the perf probe only.

**Tests** `CodexTactics.Environment.SnowInteraction.*` (10): WindowSnap (resolution, size, 256-texel snap, hysteresis, shift texels), WorldToUV (and
the reader's UV from the MPC vector), Stride (count, ±12 cm alternation, poll-rate independence, teleport, scale), Segments (trail spacing, quads),
Cap, VertexColour (exact 8-bit round trip of the shape code), Fade (rates, the 16-bit due rule, FreshSnow), Gating (ground gate, archetype tracks /
imprints), DataDefaults (C++ defaults = the asset class defaults, deterministic jitter), Contract (assets).

**Rendered smoke** `SnowTracksSmoke` (`Debug/SnowTracksSmokeCommand.cpp`; test_map `"rendered": true`, so `smoke_parallel.ps1` starts it with
`-RenderOffscreen -ResX=1280 -ResY=720` instead of `-nullrhi`; under `-nullrhi` it fails by design with "needs a real RHI"). L_MovementTest: RG16F
2048 / 64 m and the MPC vector; the view snaps onto the leader, who walks ~4.4 m (the others frozen): the read-back (`FRenderTarget::ReadLinearColorPixels`,
raw) shows R ≈ 0.89 in every metre of the path, prints on both sides of the path line, R = 0 three metres off it and behind the start; a 20 m view
move scrolls the window (shift passes, the MPC follows) and every metre keeps its value at the new UV; `Codex.Env.SnowTracksFadeScale 300` for 2 s
lowers R 0.89 → 0.24. Stills: `Saved/Screenshots/SnowTracks/path_rt.png` (R / G of the path region), `walk.png`.

**Cost** (`CodexTactics.SnowTracksSmoke perf`: L_MovementTest wave 2 = 57 enemies running at the squad, 1920x1080 offscreen, `-benchmark -fps=60`,
4 s per phase; GPU noise ±0.2 ms):

| Phase | GPU ms | render ms | game ms | snow CPU ms / frame | stamps / s |
|---|---|---|---|---|---|
| tracks on | 4.94 | 7.20 | 14.89 | 0.054 | 297 |
| tracks off | 5.00 | 6.78 | 14.32 | 0.002 | 0 |
| tracks on (again) | 4.84 | 7.37 | 14.73 | 0.051 | 54 |
| stress +512 stamps / frame | 5.16 | 7.96 | 15.74 | 0.142 | 30848 |
| stress +10 fade passes / frame | 5.73 | 11.14 | 15.99 | 0.054 | 51 |
| stress +10 shift passes / frame | 7.01 | 14.20 | 16.68 | 0.060 | 56 |
| tracks off (again) | 4.81 | 7.23 | 15.20 | 0.002 | 0 |

The real load (60 walkers, ~300 stamps / s while the horde runs in) is within the GPU noise; the subsystem's game-thread cost is ~0.05 ms / frame
(the 20 Hz poll of 60 walkers + one canvas draw). Per item: a stamp ~0.0005 ms GPU; a fade pass (2048² modulate) ~0.085 ms GPU + ~0.4 ms render
thread (the canvas setup of `DrawMaterialToRenderTarget`), at ≤ 5 Hz; a scroll (2 copies) ~0.2 ms GPU + ~0.7 ms render thread, only every 8 m of view
travel (+ the clamp pass every 10 s). If the render thread gets tight: draw the fade in the stamp canvas (one canvas a frame) - phase 4.

**Not yet** (phase 4 of the plan): re-applying recent stamps to scrolled-in strips, Nanite displacement, `NS_SnowPuff` (skipped in phase 3).
The ground overlay reads the RT (§12.d); explosions / dents / real-time drag are phase 3 (§14.3).

### 14.3 Interactive snow phase 3 (2026-10-23): event stamps

Plan §5 (orchestrator-approved additive hooks). The gameplay only BROADCASTS; `USnowInteractionSubsystem` listens and stamps (new
`SnowInteractionEvents.cpp`; rules in `SnowInteractionRules`). Nothing feeds back: no gameplay class reads the snow.

**Hooks (one line each, no logic / order change)**
- `PlayerCommands.h/.cpp`: `GameplayEvents::Explosion` + `ExplosionKinds` (Grenade / Barrel / Mine / Tripwire / Cannon / Trap),
  `RaiseExplosion(WorldContext, Kind, Location, Radius, Source)` (one `RaiseWithContext`), `GetExplosionKind(Actor)` (barrel / proximity
  mine / tripwire, else Trap) and `FGameplayEventContext::Radius` (new field, default 0).
- Raise sites: `AGrenadeActor::Detonate` (after `ApplyAreaEffect`, radius `EffectRadius`), `AInteractableActor::ApplyBlast` (barrel / mine /
  tripwire / trapped objects, after `AlertPatrolsNearTrap`), `UVehicleTurretComponent::FireCannon` (at `Impact`, the splash radius),
  `UTurnBasedCombatSubsystem::DetonateBarrel` (in the `if (Barrel)` block) and `DetonateMine` (after its `ApplyBlast`; radius 0 = the kind's size).
- `UCombatFeedbackSubsystem::OnTracerSpawned(Start, End, bHit)` (native multicast), broadcast first in `SpawnTracer` / `SpawnTurretTracer`.
  bHit is false from both (they do not know the outcome); the listener classifies the end point.
- `URelocationSubsystem::GetMovingObjects(TArray<const AInteractableActor*>&) const`: the objects of running tasks in stage 2 (pushed).
- No tutorial pattern matches "Explosion" (checked: the shipped patterns are `Enemy*`, `ColdTier.*`, `Gone.*` and command patterns).

**Stamps**
- Explosion -> one trace down (WorldStatic / WorldDynamic, the source ignored, at most `CraterMaxDropCm` 300 below; snow ground facing up,
  not sheltered) -> `ComputeCrater` + `MakeCraterStamps`:
  - diameter = blast radius x `CraterDiameterPerRadius` 0.55, clamped 120..380 cm (no radius: the kind spec's length);
  - depth / rim by kind (`GrenadeCrater` 1.5 / 1.1 (tripwire too), `BarrelCrater` 0.9 / 1.0, `MineCrater` 2.0 / 1.4, `CannonCrater` 1.3 / 1.0,
    `Crater` for traps);
  - the stamps: the bowl (Crater shape) + a steep-walled pit (Body shape, 0.55 x, 0.6 x the depth: the bowl alone is ~8 cm over a metre
    and barely shades) + the thrown-snow ring (Crater shape with no bowl, `CraterSprayScale` 1.7 x) + `CraterClods` 7 clods (small pits
    with rims, 0.6..1.15 x the diameter out). Yaw / oval / clod layout from a hash of the centre (deterministic).
- Tracer -> `EnqueueShot` (queue `ShotQueueMax` 32, the oldest dropped) -> per frame at most `ShotTracesPerFrame` 8:
  - a pawn within `ShotHitRadiusCm` 30 of the end = a hit (no trace);
  - else one line trace from 30 cm before the end to `ShotExtendCm` 15 m past it;
  - snow ground facing up = a dent (`Dent` 20 cm, depth 0.9, stretched along a grazing ray up to `DentMaxStretch` 2x).
- Real-time drag: `GetMovingObjects` is polled with the walkers (20 Hz) and feeds the existing drag-trail tracker (`DragTrail`, watched
  `DragTrackSeconds` after the push).
- New `DA_SnowStamps` fields (`GrenadeCrater`, `BarrelCrater`, `MineCrater`, `CannonCrater`, `CraterSpray`, `CraterClod`; tuning
  `CraterDiameterPerRadius`, `CraterMinCm` / `MaxCm`, `CraterSprayScale`, `CraterClods`, `CraterMaxDropCm`, `ShotExtendCm`,
  `ShotTracesPerFrame`, `ShotQueueMax`, `ShotHitRadiusCm`, `DentMaxStretch`) load with the C++ defaults until the asset is re-saved.
- Metrics: `Explosions`, `Craters`, `ShotsQueued` / `Dropped` / `OnTarget`, `ShotTraces`, `MaxShotTracesInFrame`, `Dents`,
  `LastMovingObjects`, `DragSegments`.

**Proof the explosion gameplay is unchanged**
- Under `-nullrhi` the subsystem does not exist, so the hooks only broadcast. The tracer delegate has no listener there. The Explosion bus
  event reaches the tutorial matcher only, which has no matching pattern. No RNG call is added.
- 29 smokes (all PASS) were run before (pre-change build) and after:
  - Grenade, AIGrenade, GrenadeAnim, Barrel, PauseBarrelOrder, Deployable, PrepDeploy, Tripwire, PMCTripwire, PeekMines, TrapSolo;
  - Knockdown, KnockdownCover;
  - TurnBased, TurnBasedBarricade, TurnBasedDeploy, TurnBasedPush, TurnBlastDeath, TurnRules, BarricadeTurnContact;
  - Vehicle, VehicleCombat, VehicleDamage, TargetedShot, Turret, Relocation, EventBus, HordeIntro, SnowTracks.
- Compared: every `LogCodexTactics` line, minus `[Env]` / minimap / ms timings.
- 18 are byte-identical, among them Barrel, Tripwire, PMCTripwire, PeekMines, TrapSolo, Deployable, KnockdownCover, every turn-based
  barricade / deploy / push smoke and EventBus.
- The rest differ only in lines that also differ between two runs of the SAME build: random barks, fire-puddle durations, a Brute crit,
  horde-intro stats, the TB hound in TurnBlastDeath, vehicle timings.

**Tests / smokes**
- `CodexTactics.Environment.SnowInteraction.*` +3 (13 total):
  - `CraterShape`: kind -> spec, diameter from the radius / clamps, the bowl / pit / ring / clods, determinism, the vertex colour ids;
  - `DentTraceCap`: the ray, the drop-oldest queue, at most 8 traces a frame, the grazing stretch;
  - `DragSegments`: a 20 Hz push gives contiguous segments, a 10 m jump is one segment, 20 m restarts.
- `SnowTracksSmoke` (rendered) +6 stages:
  - a grenade's crater 8 m off the walk: R 2.4 at the centre, G 1.9 on the rim, 1.3 in the ring, 0 beyond 1.5 D;
  - a missed shot's dent: R 0.89;
  - a hit leaves none;
  - 22 tracers in one frame: max 8 traced a frame, 21 dents;
  - a real-time barrel push: `GetMovingObjects` 1, 16 drag segments, R 0.71 along it.
  - The smoke now switches the camera's edge scroll off: the offscreen cursor on the viewport edge panned the view, and the window, off the
    walk. That made the phase-1 smoke fail at the baseline.
- Stills + clip: `CodexTactics.EnvironmentShots tracks [nomovie]` (`EnvironmentTracksShotsCommand.cpp`), Frozen Factory, Clear 12:00,
  SnowAmount 0.85, 1600x900. Output `Saved/Screenshots/Environment/tracks/`: `squad_walk`, `prone_crawl`, `grenade_crater`, `barrel_drag`,
  `bullet_dents`, `tracks_overview`, `tracks_off` and `tracks_10s.mp4` (the squad walks, a grenade, a burst of misses, a barrel pushed).
- Known limit: the ground overlay's `MaxDepthCm` 8 / `MaxBermCm` 3 make a crater's large bowl shade weakly (hence the pit). A deeper
  crater needs the reader side (material) changed.

## 15. Directional snow with memory on objects and characters, melting in the sun (2026-10-23)

User decisions 2026-10-23: (1) in sunny weather (Clear, sun up) the snow on OBJECTS and CHARACTERS melts a little, the ground cover
(`SnowAmount`) still never recedes; (2) characters and enemies get covered gradually during Snowfall / Blizzard; (3) the snow sticks to the
windward SIDES too, with memory: 5 pockets (top +Z, +X, -X, +Y, -Y). UE-only, no Godot reference; visual only (no gameplay reads it).

### 15.1 State and rules (`FDirectionalSnow`, `EnvironmentRules`)
- `FDirectionalSnow {Top, PX, NX, PY, NY}` (0..1 each). `FEnvironmentState::ObjectSnow` = the static objects' pockets in WORLD axes.
- `StepDirectionalSnow` (pure): top += rate x dt; side_i += rate x `object_snow_side_rate_scale` x max(0, -dot(wind, side_i)) x wind
  strength x dt (wind = the TRAVEL direction, `UEnvironmentSubsystem::GetWindDirection`); then every pocket loses melt x dt (the top never
  below its floor, a side down to 0; a pocket already under its floor is not raised) and the clear rate x dt (characters only). Capped at 1.
  A pocket the wind does not reach keeps its snow (memory when the wind turns).
- Objects (`StepObjectSnow`, in `UEnvironmentSubsystem::Tick` after `StepSnowCover`, same clock rules; a locked level never changes):
  rate = `GetBlendedSnowCoverRate` (the snow cover rates, blended by the weather); melt = `object_snow_melt_rate` (1/600 per real s) x the
  Clear weather amount (0 while overcast / snowing, blended through a transition) x the sun ramp (0 at the horizon -> 1 at
  `object_snow_melt_full_sun_elevation` 20 deg; the sun elevation from the presenter's `DayCurves`, `UEnvironmentSubsystem::GetSunElevation`);
  top floor = `object_snow_melt_floor` 0.5 x `SnowAmount`.
- Start / save / carry: a fresh state starts with `Top = SnowAmount` (`MakeInitialState`, the level's `snow_cover` too); saved in
  `game_state.environment.object_snow {top, px, nx, py, ny}` (all optional); a save WITHOUT the key (older builds) gets `Top = snow_amount`,
  sides 0 (`SeedObjectSnow`). Carried to the next level with the state; a restart restores the entry state (section 12.c).
- Wind: `UEnvironmentSubsystem::GetWindDirection()` = the presenter's drift (`WindBaseYaw` 35 +- 25 deg over 180 s, the C++ defaults
  without a presenter) unless a debug override is set (`SetWindDirectionOverride`, console `Codex.Env.WindFrom <+X|-X|+Y|-Y|yaw|off>`: the
  side the wind comes FROM). The presenter feeds the same wind to the falling snow and MPC `WindDirection`.
- `SetSnowAmount` (debug, `Codex.Env.SnowCover`) also sets the objects' top; `Codex.Env.ObjectSnow <top> <px> <nx> <py> <ny>` sets the pockets.
- Tuning (`environment.json`, `FEnvironmentConfig`): `object_snow_side_rate_scale` 1, `object_snow_melt_rate` 0.0016667,
  `object_snow_melt_floor` 0.5, `object_snow_melt_full_sun_elevation` 20, `character_snow_fill_scale` 1, `character_snow_melt_scale` 1,
  `character_snow_clear_rate` 0.05, `character_snow_spawn_share` 0.5.

### 15.2 Objects: MPC + M_SnowOverlay
- `MPC_Environment` gains the scalars `SnowTop`, `SnowPX`, `SnowNX`, `SnowPY`, `SnowNY` (`create_environment_mpc.py`, appended; every
  existing parameter kept its id, logged). The presenter writes them every application (`WriteObjectSnow`), 0 with the visuals off.
- `M_SnowOverlay` (`create_snow_overlay_material.py`): the old world-up mask is the TOP pocket, driven by `SnowTop` instead of `SnowAmount`
  (bare sides + `SnowTop = SnowAmount` = exactly the old look; the creep / noise / frost stay); the windward sides add
  sum side_i x max(0, N . dir_i) (world axes), not on downward faces, broken by the side-projected noise (`SideGain` 0.85, `SideNoise` 0.5,
  `SideThreshold` 0.25, `SideSharpness` 1.6, `SideOpacity` 0.8). PS 1883 -> 1898 instructions. The overlay shows while the ground or ANY
  object pocket holds snow. The ground overlay (`M_SnowGroundOverlay`) still reads `SnowAmount`.

### 15.3 Characters: `UCharacterSnowSubsystem` + the master materials
- `UCharacterSnowSubsystem` (`Environment/CharacterSnowSubsystem.*`, pure logic `CharacterSnowRules`): a `UTickableWorldSubsystem`, Game /
  PIE, not created without a renderer (`-nullrhi` smokes / bot runs untouched) or on a dedicated server; `Codex.Env.CharacterSnow 0/1`;
  inactive (every touched body back to 0) while the presenter is not applying. Characters from the registries (`USquadSubsystem::GetMembers`,
  `UEnemyRegistrySubsystem::GetEnemyCharacters`; hounds are enemies; no actor iterators, the file is in
  `Combat.EnemyRegistry.HotPathsUseRegistry`); the `Codex.Env.CharacterSnowMax` 40 nearest the camera are processed (the others keep their
  snow frozen). Every ~0.25 s per character: the world wind is turned into the BODY mesh's local frame (`ACharacter::GetMesh` component
  rotation: the snow stays on the back when the character turns), the top / windward local sides fill at the cover rates x
  `character_snow_fill_scale`, the pockets melt in the sun (the objects' melt x `character_snow_melt_scale`, floor 0) and clear at
  `character_snow_clear_rate` near an active `UHeatSourceComponent`, inside a Closed / Shelter `ACameraZoneVolume` (gathered every 10 s) or
  while an operative is hidden (APC; enemies hidden by the fog of war keep snowing). Time: the environment's clock rules (frozen in the
  tactical pause / turn-based / AI pause, x `Codex.Env.ClockScale`) but NOT its lock (the locked Demo 01 lake snows on the squad).
- An enemy that appears while it snows starts with `character_snow_spawn_share` 0.5 x the objects' pockets x the snowing amount
  (`MakeSpawnCover`, turned into its body frame). Operatives start every level bare (per world: nothing is carried or saved).
- Push: `SetCustomPrimitiveDataFloatArray(8, {Top, PX, NX, PY, NY})` on the body mesh, at most every 0.25 s and only on a change > 0.01
  (`CharacterSnowRules::ShouldPush`; the last step to exactly 0 is always pushed).
- Custom Primitive Data indices **8..12** (Top, +X, -X, +Y, -Y in the body's local axes; `CharacterSnowRules::FirstCustomDataIndex`), also
  listed in `docs/systems/animation-setup.md`. Verified free on every switched body (operatives incl. the SquadOutfits MIs, Female, MZ1-3,
  Hound, Frostbitten, Brute, Cutter, Marksman): no C++ calls `SetCustomPrimitiveData*` on a character (only the turn grid's ISM uses
  per-instance data); a binary scan of every `Content/**/*.uasset` finds `bUseCustomPrimitiveData` / `CustomPrimitiveData` nowhere and
  `PerInstanceCustomData` only in `M_WeaponMatrixDots` (an ISM weapon effect, not a body); `add_character_snow_layer.py` re-checks every
  master's expressions before it adds the layer. The FrozenPMC wears its pack material: no snow layer on it (by design).
- Snow layer (`Scripts/Editor/add_character_snow_layer.py`, run with `-AllowCommandletRendering`; idempotent: a master that has the layer
  only gets the HLSL / defaults re-applied) on `M_CharacterMaster`, `_Masked_TwoSided`, `_TwoSided`, `_Subsurface`: 5 scalar parameters on
  Custom Primitive Data 8..12; the vertex normal in LOCAL space (world -> local Transform; the two-sided masters multiply by TwoSidedSign
  first, so a back face takes its own side); mask = Top x max(0, Nl.z) + `SnowSideScale` x sum side_i x max(0, Nl . dir_i), broken by
  `T_TilingNoisePacked` on UV0 x `SnowNoiseTiling` 7 (sticks to the body; the shared wrap sampler) with `SnowGain` 0.8 / `SnowNoiseAmp` 0.9
  / `SnowBias` 0.5 / `SnowSharpness` 2 / `SnowMaxCover` 0.65; BaseColor lerps to `SnowColour` (0.78, 0.82, 0.87), Roughness to 0.75, the
  tangent normal toward (0, 0, 1) by mask x 0.6. No WPO; the overlay slot, ABPs and BPs untouched. The mask is exactly 0 without snow.
- Instruction delta (SM6, `get_statistics`, before = the git HEAD masters): `M_CharacterMaster` PS 225 -> 271 (+46), `_Masked_TwoSided`
  232 -> 278 (+46), `_TwoSided` 232 -> 278 (+46), `_Subsurface` 239 -> 284 (+45); VS 314 unchanged; samplers +1 (5 -> 6, 6 -> 7).
- A/B at 0 (`CodexTactics.EnvironmentShots charsnow ab <tag> [snow]`: one body per master, all in ONE run - the MI copied to a temporary
  `/Game/_CharSnowBefore/` parented to the old master, deleted afterwards; `Scripts/Tools/image_diff.py`): mean absolute difference before vs
  the layer at 0 = 0.216 / 0.201 / 0.231 / 0.134 levels, below the A/A noise floor of the same run (0.383 / 0.220 / 0.251 / 0.151; Lumen /
  shadow noise); with snow (1, 0.8, 0, 0, 0.5) 1.6 / 2.5 / 1.0 / 0.5.

### 15.4 Captures, cost, tests, smokes
- `CodexTactics.EnvironmentShots charsnow [nomovie] [noperf] [nomelt]` (`EnvironmentCharSnowShotsCommand.cpp`): MovementTest wave 2 / the
  factory's wave 1, the squad safe, 12:00: a blizzard with the wind FROM +X (x30, 5 s), the stills in Clear light with the clock stopped
  (windward / leeward / close), the wind FROM -Y (the -Y sides fill, +X kept), Clear 12:00 held at x20: the melt after 0 / 2 / 10 / 40
  design minutes (characters bare after ~10 min, the objects' top at 0.5 x the ground, the ground unchanged); a 10 s movie (15 fps) of the
  two fills. Output `Saved/Screenshots/Environment/charsnow/` (+ `charsnow_<map>.mp4`, `ab_<tag>/`, `ab_sheet.png`).
- Cost (MovementTest wave 2, 1920x1080 offscreen, 57+ enemies): the subsystem 0.006-0.007 ms game thread per frame, 40 characters
  processed, ~21-23 CPD pushes / s; `Codex.Env.CharacterSnow` 0 vs 1: GPU 4.74 / 4.75 ms, game / render within noise. The layer's ALU cost
  (+46 PS instructions per body pixel) is paid always, also at 0.
- Tests `CodexTactics.Environment.DirectionalSnow.*` (8): PocketFillByWind, MemoryWhenWindTurns, MeltFloor, NoMeltAtNightOrSnowing,
  LocalFrame (incl. the mannequin -90 mesh yaw, the spawn cover rotation, the shelter clear), PushThrottle, SaveAndBackCompat, Tuning.
- `WeatherSmoke` stage S: the 5 MPC scalars with distinct ids; wind FROM +X -> the top + the +X pocket grow by rate x wind, -X / +Y / -Y
  stay 0, MPC SnowPX = the state; Clear 12:00 -> the top melts to exactly 0.5 x the ground, +X to 0, the ground unchanged, MPC SnowTop =
  the state (nullrhi-safe; the characters' subsystem does not exist there).

### 15.5 The level opens in its weather look (user feedback 2026-10-23)
- Before: the lake first showed the map's authored daylight and switched to the snowy night after ~4 s (`MinimapHoldSeconds`: the first
  application waited for the minimap captures).
- Now `AWeatherPresenterActor::BeginPlay` applies the whole state synchronously (lights, fog, sky light, post-process, MPC, the snow
  overlay scan, the falling snow intensity jump-started: the first `ApplySnowFx` gets a 1000 s delta so its smoothing lands on the target).
  The presenter is spawned in `UEnvironmentSubsystem::OnWorldBeginPlay`, so its `BeginPlay` runs in the same dispatch as the map's actors,
  before the first frame and before any start fade (the factory's 2 s fade-in starts on an already applied scene). For the first 1.5 game
  seconds the post-process eye adaptation runs at 60 f-stops / s (a night level does not open over-exposed), then the authored speeds return.
- Minimap: no minimap code changed; its one-off captures now simply run after the first application, so the minimap shows the weather look
  (a night level's minimap is darker than before). If the daylight minimap is wanted back: capture it with the Lighting show flag off, or
  under the start fade (follow-up, not done).
- Verified with `CodexTactics.EnvironmentShots charsnow startframes` (frames 1 / 2 / 30 / 60 / 150 as the game shows them): the lake is
  night + snow from frame 1 (the presenter applying at frame 1; mean luminance 130 / 113 / 108 / 108 / 108; frame 30 vs 150 differ by 0.6
  levels; frame 1 is the editor build's first frame with shaders still compiling). Stills `start_L_Demo01_A_FrozenLake_f*.png`,
  `start_lake_sheet.png`. The falling-snow Niagara warm-up itself (flakes already in the air at frame 1) belongs to the NS_Weather_Snow owner.

## 16. The level opens in its final look; the presenter stands alone (game designer 2026-10-25)

Designer feedback 2026-10-25: "When I press Play, first a sunset shows, then it switches to night. The snow also starts falling with a delay;
the night lighting appears with a delay. Make it appear at once." and "If I just need to delete the lighting folder from all levels and keep
only our Blueprint, tell me". Cause on main: the presenter held its first application for the 4 s minimap captures (`MinimapHoldSeconds`;
the lake's authored sun is at pitch -0.2 = a sunset), the falling snow loaded asynchronously and then ramped from 0 (0.35 / s) with flakes
born at the top of a 28 m cylinder (~6-9 s to fill the air), and the eye adaptation faded. UE-only, no Godot reference.

### 16.1 Frame 1 = the final state
- `AWeatherPresenterActor::BeginPlay` applies the resolved start (random / forced `-EnvWeather` / carry-over / locked level) synchronously
  (section 15.5): sun / moon / sky / fog / post / MPC_Environment / snow cover, before the world's first tick (`GetFirstApplyGameTime() == 0`).
- **Snap** (`BeginSnap`, rules in `EnvironmentStartRules`): on the first application and whenever the whole state JUMPS -
  `UEnvironmentSubsystem::GetSnapSerial()` moves on the level start resolve and on `SetState` (a save load, debug) but not on `ForceWeather` /
  `SetHour` / the clock. A snap: (1) the falling-snow intensity jumps to its target (no 0.35 / s ramp); (2) a snowfall that starts with it is
  **pre-warmed**: `UWeatherSnowFxComponent::RequestStartWarmup` loads NS_Weather_Snow / DA_SnowFX / the flake MI synchronously, the
  activation runs in the first `TickComponent` (TG_PostUpdateWork, the camera has its view) and `AdvanceSimulation` simulates
  `EnvironmentStartRules::GetSnowWarmupSeconds` (DA_SnowFX `StartWarmupSeconds`, 0 = the longest flake life, 9 s) in
  `StartWarmupTickSeconds` 0.1 s steps (90 ticks): flakes and ground drift are in the air on the first frame that shows the system (the
  lake: ~51k flakes right after the warm-up); if Niagara defers the activation (system / PSOs not ready) the warm-up runs on the first tick
  it is active, editor builds wait for the system compile at that activation; (3) ~~the eye adaptation runs at `ExposureSnapSpeed` 1000
  f-stops / s for `ExposureSnapSeconds` 1 s~~ - superseded by section 19 (it was the start flicker): the camera-cut frames of (4) settle the
  exposure, the authored speeds apply at once; (4) the next 3
  frames are camera cuts (`SetGameCameraCutThisFrame`: no TAA / motion-blur smear from the camera's first placement).
- **Start cover** (level start only): the stills showed that the level's very first rendered frame is drawn before the camera manager
  updated (the view at the world origin, editor builds also with default materials) and the pre-warmed flakes show one frame after the
  warm-up. The presenter therefore holds a black manual camera fade (its own marker colour) from BeginPlay and lifts it on its tick after
  the camera ticked and the snow is warmed (`StartCoverMinTicks` 2, safety cap 10 ticks): the player sees black for ~2 frames (~33 ms),
  then the final night / day with the snow already falling. Never over another fade (the factory's start fade-in wins and is never
  touched); not without a renderer. `WasStartCoverShown` / `IsStartCoverOn` / `GetStartCoverTicks`.
- Weather CHANGES during play keep their smoothing (fade-in at `IntensityRisePerSecond`, no second warm-up).
- Sky light: the own / the levels' sky light is real-time capture (its first capture is complete); a recaptured one is recaptured at start.
- Not changed by this: Lumen's GI / reflections still converge over the first ~0.5 s (temporal); editor `-game` runs show the default
  material on objects whose shaders are still compiling in the very first frame (packaged builds have cooked shaders).

### 16.2 The own rig (the level's lighting actors become optional)
- `EnvironmentStartRules::PlanRig(FLevelLightingInventory, bBuildMissingLighting)`: every part the level has is ADOPTED as before (backward
  compatible); each missing part of sun / sky atmosphere / sky light / height fog is the presenter's OWN component; moon and post-process as
  before (own unless the level has a second directional light / an unbound volume). Mode `adopted` / `own` / `mixed`, logged
  `[EnvVisuals] rig mode ...` and in `DescribeAdopted` (`rig own: sun own, ...`).
- Own components (default subobjects, hidden unless used): `OwnSun` (Movable, 6 lux, temperature 6500, source angle 0.7357, atmosphere
  light 0, 4 cascades to 200 m), `OwnSkyAtmosphere` (planet top at the component, 60 m below the presenter), `OwnSkyLight` (Movable,
  real-time capture, intensity 1), `OwnHeightFog` (0.0436, falloff 0.2, black inscattering, at -68.5 m), plus the old `MoonLight` /
  `PostProcess`. Defaults = the Demo 01 levels' template "Lighting" folder (the dry-run dump in 16.4). Their CLASS-DEFAULT values are the
  "authored" baseline the hour / weather multiply (`AuthoredSource` = the component archetype, so values an editor preview wrote on a
  placed instance never become the baseline) and the look with the visuals off (`Codex.Env.Visuals 0`, level `"visuals": false`): a
  daylight sun at pitch -49.5, never a black level. Tune them in BP_WeatherPresenter.
- No own volumetric clouds (switched off anyway, section 11). `bBuildMissingLighting` false = the old behaviour.
- Getters for the parts in use: `GetSunLight()`, `GetSkyAtmosphereComponent()`, `GetSkyLightComponent()`, `GetHeightFogComponent()`,
  `GetRigMode()`; `WeatherSmoke` / `WeatherLakeSmoke` check these (they pass with or without the level's lights).

### 16.3 Editor preview (a level without lights is not black in the editor)
- The presenter MAY be placed in a level (`SpawnForWorld` uses a placed one instead of spawning). A placed presenter is the editor
  preview: `OnConstruction` in an editor world (not PIE / game) counts the level's lighting actors and lights the parts the level lacks
  with its own rig as `PreviewHour` (12) / `PreviewWeather` (Clear) of the DA_Weather_* presets (plain working light; 23 / Snowfall shows the
  lake's game look; own moon + own post-process too; no snow in the editor). It never writes the level's own actors (they and the map package stay untouched); with the level's lights present
  the own rig stays hidden. `bEditorPreview` off = a dark editor viewport. Change the hour / weather on the placed actor to preview
  another look.
- `remove_level_lighting.py` places one (label `WeatherPresenter`, folder `Lighting`; BP_WeatherPresenter when it exists) after removing
  the lights.

### 16.4 Map cleanup `Scripts/Editor/remove_level_lighting.py`
- Removes exactly what the presenter replaces: `DirectionalLight`, `SkyLight`, `SkyAtmosphere`, `ExponentialHeightFog`, `VolumetricCloud`,
  an UNBOUND `PostProcessVolume`, the template sky dome (StaticMeshActor `/Engine/EngineSky/SM_SkySphere`); lists any other actor of a
  lighting-looking folder as KEPT. Then places the presenter (`CODEX_PLACE_PRESENTER=0` skips).
- Dry run by default (report only); `CODEX_APPLY=1` backs the map files up first (`Saved/Backups/remove_level_lighting_<stamp>/`, SHA1
  manifest, each copy verified) and saves; `CODEX_SAVE_AS=<map>` writes a test COPY only (the source is never saved). `CODEX_MAPS`
  (default: the two Demo 01 maps; names or /Game paths). Report `Saved/Logs/remove_level_lighting.txt`.
- Navmesh: the lake's A4 smokes need its EDITOR-built navmesh (docs/port/TRAPS.md); a commandlet save drops it, so `CODEX_APPLY=1` refuses
  in a commandlet when the map has a RecastNavMesh unless `CODEX_ALLOW_COMMANDLET=1` (then `Scripts/bake_navmesh.ps1 -Map <name>`).
  Run it in the full editor (this checkout's editor closed): `UnrealEditor.exe <uproject> -ExecutePythonScript="<abs path>" -unattended`
  with `CODEX_APPLY=1` set.
- Dry run 2026-10-25 (worktree): the lake and the factory each lose the same 6 actors of their `Lighting` folder - `DirectionalLight`
  (6 lux, Movable; lake pitch -0.2 = the "sunset", factory -49.5), `SkyAtmosphere` (factory: `SkyAtmosphere2`), `SkyLight` (factory:
  `SkyLight2`; real-time capture), `ExponentialHeightFog`, `VolumetricCloud`, `SM_SkySphere`; nothing else is in the folder; no
  post-process volume on either map.

### 16.5 Tests, smokes, stills
- Tests `CodexTactics.Environment.StartLighting.*` (3): RigPlan, SnowWarmup, Snap.
- `StartLightingSmoke` (nullrhi, the lake): A the start state applied in BeginPlay (frame / sun pitch / moon / MPC / exposure snap / snow
  at its target); B the level's lights destroyed + the presenter re-spawned -> rig own, applied in its spawn frame, the night lit by the own
  moon / sky light / post, 12:00 lights the own sun, visuals off = the own daylight baseline; C `SetState` snaps again, `ForceWeather` fades.
- `StartSnowSmoke` (rendered, the lake): the snow active and pre-warmed once by frame 2 at its full target, particles in the air; a
  ForceWeather Blizzard afterwards fades in without a second warm-up.
- Stills: `-StartLightingShots=<tag>` on the command line (armed at `PostLoadMapWithWorld`, so frame 1 is the level's first rendered
  frame; `CodexTactics.StartLightingShots <tag>` from the console) -> `Saved/Screenshots/StartLighting/<map>_<tag>_f001..f005, f060.png` +
  a `[StartShots]` log line per frame (presenter, rig, sun / moon, exposure snap, snow warm-ups and live particles per emitter).
- Verified 2026-10-25 (editor -game, 1280x720 offscreen, `Saved/Screenshots/StartLighting/`, sheets in `sheets/`): the lake (locked 23:00
  Snowfall) with its lights and the no-lights copy: frames 1-2 black (start cover; frame 1 is drawn with the camera still at the world
  origin), frame 3 on = the final moonlit night with the snow falling (~51k flakes warmed), mean luminance 113.7 / 113.8 / 113.4 / 111.4 at
  frames 3 / 4 / 5 / 60 with lights and 111.3 / 111.1 / 110.9 / 107.4 without - the same look, no sunset, no ramp. The factory day
  (12:00 Clear) / night (23:00 Snowfall) / blizzard, with and without its lights: its own start title fade covers frames 1-5, then the
  final look (own-rig daylight with shadows and the own-rig night match the level's lights). Editor preview of the no-lights lake copy:
  `EditorPreview_L_LakeNoLights*.png` (lit, not black). Ground drift (`SnowSmoke`) stays at 0 particles with the current DA_SnowFX also
  in a blizzard, before and after this change (a tuning matter of the snow FX, not of the start).

## 16b. Migrated rain / sunny / storm assets (2026-10-26)

Source: the old `WeatherSystem_01 5.4` (read-only). Script: `Scripts/Editor/migrate_weather_assets.py`, new batch `rain` (`MANIFEST_RAIN`; `sky` is the old batch).
**Method.** Closure: read-only scan of the `.uasset` name tables for `/Game/` packages, transitive from the requested roots (35 packages; the 9 that the sky batch already
moved are listed in the manifest only so the reference fixup lands on their final paths). The project's `Content` has junctioned top folders, so nothing was staged there:
the 35 files were staged into a **scratch content-only project** (outside the repo), moved with `AssetTools.rename_assets` (reference fixup), saved, verified
(every asset loads, no reference outside `/Game/Environment/Weather` except engine content, editor log: 0 errors / 0 warnings), then `export` copied only the 26 new files
into this worktree (aborts on any existing target). Result: **26 new assets, 16.3 MB, 0 missing refs, 0 broken expressions** (the `MF_ComputeLandscapeUV` issue is not in this closure).

| New path (`/Game/Environment/Weather/...`) | Role | MB |
|---|---|---|
| `Rain/Niagara/NS_Raindrops` | Rain system: 3 emitters (CPU close rain with collision, GPU far rain, splash sprites) | 1.36 |
| `Rain/Niagara/NS_PuddleSplashes` | One-burst sprite emitter: ground splash / puddle ring | 0.35 |
| `Rain/Niagara/MLiquidMaster`, `MI_Raindrops`, `MI_TainSplashes`, `MI_PuddleSplash_01` | Additive unlit particle material (mask + normal texture params) and its 3 instances (streaks, splash, puddle splash) | 0.05 |
| `Rain/Niagara/NDI_RandomOpacity` | Niagara module script used by the splash emitters | 0.02 |
| `Rain/Materials/MF_GenerateRainyWeatherPuddles`, `MF_Motion4WayChaos_Fixed` | Puddle / ripple surface function and its flow helper | 0.07 |
| `Rain/PostProcess/PPM_RainDropsOnScreen`, `PPMI_RainDropsOnScreen` | Rain drops on the lens (post process); the old name had a typo (`...OnSreen`) | 0.04 |
| `Rain/Physical/PM_Puddle_01` | Physical material (empty marker) | 0.00 |
| `Rain/Textures/` `T_DetailedNormal`, `T_DropletsMasks_Packed`, `T_Droplets_Normal`, `T_SpalshVariations`, `T_SplashBig`, `T_RippleNormal_FlipBook`, `T_RainDropsPacked` (4.9), `T_Water_N` | Rain / splash / ripple / droplet textures | 13.4 |
| `Storm/NS_Lightning`, `Storm/M_Lightning_Master`, `Storm/MI_LightningBolt_01`, `Storm/Textures/T_Lightning_Bolt_01` | Lightning bolt + flash system, additive bolt material, bolt texture | 1.0 |
| `Reference/DA_Rainy`, `Reference/S_RainyWeatherSettings` | Old rain preset (sky / light / fog values) and the rain-settings struct, look reference only | 0.02 |

Already present, reused by the new assets (not copied again): `Snow/Niagara/FollowCameraTemplate`, `MPC/MPC_WeatherSettings` (untouched; the rain params are reproduced in `MPC_Environment` by the C++ work),
`Common/Textures/T_TilingNoisePacked`, `Reference/BP_WeatheDataAsset_Base`, `EWeatherType`, the four `S_*Settings` structs.

**Sunny.** Nothing extra is needed: `Reference/DA_Sunny` is a plain copy of the old preset (the same `BP_WeatheDataAsset_Base` data asset type as `DA_Rainy` / `DA_Snowy`: light, sky atmosphere and fog values, no
textures or meshes). The sky look comes from the already migrated sky sphere. `Textures/Clouds/2D/T_Sky_Blue` (15 MB) is referenced by no asset of the old project (not even the sky material), so it is skipped.

**Skipped, and why.**
- `Textures/Liquid/T_RainDropsPacked` and `Textures/Liquid/T_Water_N`: nothing references them; the materials reference the `Examples/RainDropsOnSurface/T_RainDropsPacked` and `StarterContent/Textures/T_Water_N` copies (those were migrated).
- `MI_Weather_LandscapeAutomaterial` + `M_LandscapeAutomaterial` (reference): the closure is 26 packages / ~195 MB because it drags the Megascans landscape packs `Gouged_Rock_Cliff_vlcpcbc` (64 MB), `Grassy_Soil_xbreair` (59 MB),
  `StarterContent/T_MacroVariation` (10 MB), `T_VoronoiNoise_Packed` (8 MB) and `MF_SlopeBlend`, `MF_MavroVar`. Not migrated. The puddle / ripple logic is entirely in `MF_GenerateRainyWeatherPuddles` (migrated); the automaterial only calls it with the landscape layers.
- `S_RainyWeatherSettings (1)`, `NS_Raindrops (1)`, `BP_DynamivSky` (+ copy), `Examples/PuddleExample`, `RainDropsOnSurface` materials: duplicates / examples / replaced by the C++ presenter.

### How the old rain worked

Read-only analysis: name tables, T3D export of the migrated assets, decoded rapid-iteration values (the pairing of a few values to names is noted where uncertain).

**NS_Raindrops** (3 emitters; the system was attached to the camera, like `Snow/Niagara/FollowCameraTemplate`; it also has a camera-properties module `NMS_CameraProperties`).
- User params (defaults): `CPU_RainSpawnRate` 2000, `GPU_SpawnRateMultiplier` 10, `RainParticlesSizeMin/Max` 0.6 / 1.6, `RainParticlesSizeRatio` (1, 55) (width : length of the streak),
  `RainParticlesCloseBrightnessMin/Max` 0.1 / 0.25, `RainParticlesFareBrightnessMin/Max` 2.0 / 2.5 ("Fare" = far; far drops are brighter to stay visible).
- `MainRaindropsCPU`: CPU sim, `SpawnRate` = `CPU_RainSpawnRate`; `ShapeLocation` = cylinder, radius 400, height 500 (cm) around the camera; gravity (0, 0, -980), speed limit 1000; lifetime 1..2 s;
  sprite size 5..10 scaled by the user size / ratio; brightness by distance to the camera (`NDI_BrightnessByDistToCamera`, look-up-angle threshold 60 deg); `Collision` module in CPU mode (trace length 500, 1 collision, **kills on collision**, restitution 0.6, friction 0.25);
  `GenerateCollisionEvent` (delay 0.1 s) feeds the splash emitter.
- `MainRaindropsGPU`: GPU sim (fixed bounds), same module stack plus `KillParticlesInVolume`, rate = CPU rate x `GPU_SpawnRateMultiplier`: the distant rain (many particles, no CPU trace).
- `RainSplashes`: GPU sim, **spawns from `ReceiveCollisionEvent`** (a splash sprite at each CPU drop hit), sprite size 25..30, `NDI_RandomOpacity`, 3x2 sub-UV sheet.
- Renderers: one sprite renderer per emitter, `MI_Raindrops` (additive unlit `MLiquidMaster`, `MaskTexture` = `T_DropletsMasks_Packed`, `NormalTexture` = `T_Droplets_Normal`), velocity-aligned, sub-image 3x2, no sub-image blend.
- For us: the CPU emitter is the cost (traces); the splashes are the only coupling to the world. Intensity scaling = `CPU_RainSpawnRate`, `GPU_SpawnRateMultiplier`, the size / brightness user params.

**NS_PuddleSplashes**: one emitter `SimpleSpriteBurst`, `SpawnBurst_Instantaneous` (a single sprite), position offset (0, 0, -40), sprite size 100..120, short lifetime (rapid ranges 0.25..0.35 s), `NDI_RandomOpacity`;
renderer `MI_PuddleSplash_01` (`MLiquidMaster` + `T_DetailedNormal` + `T_SplashBig`). It does **not** trace or spawn on surfaces by itself: the caller (the old BP) placed it. It is a leftover copy of the snow template
(still exposes unused `User.Snow_*` params; fixed bounds +-50000). The next agent must spawn it at hit points / puddle surfaces and drive its rate from the rain intensity.

**MF_GenerateRainyWeatherPuddles** (Material Function, reads `MPC_WeatherSettings`: `IsRaining`, `PuddleSize`). Inputs: `BaseColor`, `BaseNormal`, `Roughness`, `PuddleSettings`, `RippleSettings`, `PuddlesWavesSettings`,
`NormalStrength`, `PuddleEdgeSharpness`, flip-book `SizeX` / `SizeY`, `Speed`. Textures: `T_TilingNoisePacked` (puddle mask, channels R/G/B/A), `T_RippleNormal_FlipBook` (animated ripples), `T_Water_N` + `MF_Motion4WayChaos_Fixed` (wave normal, 4-way chaotic motion).
Outputs: blended base colour, normal (water normal + ripples inside the mask), roughness and the puddle mask. `PuddleSize` (0..1) grows the mask; `IsRaining` gates the ripples. Its only user in the old project is the landscape automaterial (not migrated).

**PPM_RainDropsOnScreen** (post-process material; `PPMI_RainDropsOnScreen` is the instance). Reads `MPC_WeatherSettings.IsRaining` and `EnableRainDropsPostProcessFX` (switch), samples `T_RainDropsPacked`
and distorts the scene colour with a drop normal. Parameters: `AnimationSpeed`, `RainDropsTiling`, `RaindDropsNormalStrength` (typo in the old name); instance values 0.3, 0.03, 0.8, 0.3333 (the pairing to names was not extracted).
The old BP added it to the camera post-process blendables while raining with the FX flag on.

**NS_Lightning** + **M_Lightning_Master**: emitters `LightingBolt` (1 bolt per burst, 4x1 sub-UV bolt texture `T_Lightning_Bolt_01`, modules compute the bolt position / large number) and `LightningFlash`
(a big instantaneous flash sprite, default sprite material). Material: additive, one texture param `LightningTetxure` (typo kept). Attached to the camera like the rain; burst emitters, so it must be re-activated per strike.

**How BP_DynamivSky drove rain** (name table of the blueprint): components `Rain_Niagara_SystemComponent` (NS_Raindrops) and `LightningNiagaraSysComponent` (NS_Lightning); one data asset per weather
(`DA_Rainy`, type `BP_WeatheDataAsset_Base`) holds the sky atmosphere / directional light / sky light / fog values and a struct `S_RainyWeatherSettings` with the rain params
(`CPU_RainSpawnRate`, `RainParticles*` size / brightness / ratio, `EnableRainDropsPostProcessFX`, `FullPuddleCoverageTime`, `ShouldAnimatePuddleAppearingAtRuntime`); the BP pushed them as `User.*` into the Niagara system and
`IsRaining` / `EnableRainDropsPostProcessFX` / `PuddleSize` into `MPC_WeatherSettings`. `PuddleSize` was animated by the timeline `PuddleSizeTimeLine` (normalized 0..1, update function writes the MPC scalar, length =
`FullPuddleCoverageTime`; skipped when `ShouldAnimatePuddleAppearingAtRuntime` is false), the same pattern as `SnowCoverageAnimationTimeline`. `DA_Rainy` holds: directional light intensity 8, colour (237,237,237), temperature 9100;
sky light 1.75; sky atmosphere multi-scattering 0.5, Mie scale 0.75, anisotropy 0.55, aerial perspective scale 0.5; fog emissive 0.135 grey, extinction scale 0.2. The `S_RainyWeatherSettings` values are not stored in the asset (struct defaults).


## 17. Climate profiles (2026-10-26)

User decisions 2026-10-26 (weather chat): «Rain and sunny weather are used only on certain levels that I will specify later. They are for the
art / narrative side. The whole game takes place in the weather conditions we already built.» Confirmed: (1) the arctic ladder stays exactly
Clear (frosty) / Overcast / Snowfall / Blizzard; (2) on rain / sunny levels there is NO snow at all (no cover / object / character snow growth,
no falling snow); screen raindrops ON for the tactical camera while it rains. UE-only, no Godot reference. The rain visuals (Niagara, puddle /
wet materials, screen drops) are the next step; this section is the state, the rules, the presets, the MPC and the test map.

### 17.1 Profile and names
- `EClimateProfile { Arctic, Temperate }` (`EnvironmentTypes.h`). Level JSON `"environment": { "climate": "temperate" }`; absent / unknown
  (warning) = Arctic. `FEnvironmentLevelSettings::Climate`, `FEnvironmentState::Climate` (saved, carried).
- Temperate runs the SAME one-step ladder and `EWeatherState` values with another meaning: Clear = **SunnyDay**, Overcast = mild overcast,
  Snowfall = **Rain**, Blizzard = **Storm** (lightning later). `EnvironmentRules::GetWeatherDisplayName(Climate, Weather)` names them for the UI /
  logs (arctic = the ladder names, so every arctic log line is unchanged); `UEnvironmentSubsystem::GetWeatherDisplayName()`.
- Names: `WeatherFromString(Climate, Text)` takes the ladder names on any level and, on a temperate level only, "Sunny" / "SunnyDay" -> Clear,
  "Rain" -> Snowfall, "Storm" -> Blizzard (level JSON `weather`, `-EnvWeather=`, `Codex.Env.SetWeather`). On an arctic level "Rain" is unknown
  (ignored + reported). Saves and `environment.json` keep the ladder names.
- A level may lock the weather (`"locked": true, "weather": "Rain"`): a narrative level fixed at rain.

### 17.2 Carry, start, save
- `UEnvironmentSubsystem::ResolveInitialState`: a carried state of the SAME climate continues exactly (as before); a carried state of ANOTHER
  climate (arctic -> a temperate level, or back) is re-initialised for the new profile: the time of day continues, the weather / snow / wetness
  start over from the level's keys or the defaults, seeded by the carried RNG (deterministic). A locked level ignores the carry (as before).
- Start: snow cover `snow_cover` or `snow_cover_start` (arctic) / 0 (temperate); wetness / puddles 0 in SunnyDay / Overcast, `start_wetness_rain`
  1 / `start_puddles_rain` 0.5 in Rain, 1 / 1 in Storm; the level's `"wetness"` / `"puddles"` win.
- Save `game_state.environment` gains `climate` ("arctic" / "temperate"), `wetness`, `puddles` (all optional; an older save keeps the level's
  climate and a dry ground). The restore step sets the climate from the LEVEL (like the lock): a debug switch is never saved into a level.
- The neutral start of dev runs (`-ExecCmds=CodexTactics.*`, `-CodexBot`) is unchanged: Clear 15:00 seed 1 = SunnyDay 15:00 on a temperate level.

### 17.3 State and rules (pure, `EnvironmentRules`)
- `Wetness` 0..1 (`StepWetness`, after the snow steps in the tick; temperate only; frozen with the clock and on a locked level): the rate is
  blended by `GetWeatherAmount`: Rain + `wetness_rate_rain` 1/120 s, Storm + `wetness_rate_storm` 1/45 s, Overcast - `drying_rate_overcast`
  1/900 s, SunnyDay - lerp(overcast, `drying_rate_sunny` 1/240 s, sun ramp 0 at the horizon -> 1 at `drying_full_sun_elevation` 20 deg).
- `PuddleAmount` 0..1 lags it: target = (wetness - `puddle_wetness_threshold` 0.5) / 0.5; up at `puddle_fill_rate` 1/90 s only while the
  ground gets wetter, down at `puddle_drain_rate` 1/600 s (slower than the ground dries).
- No snow on a temperate level: `GetBlendedSnowCoverRate` = 0 (the ground cover, the objects' pockets and the characters' snow never grow),
  `GetSnowingAmount` = 0 (no spawn snow on enemies), the interactive snow tracks stay off (`USnowInteractionSubsystem`, nothing to stamp).
  The arctic melt / growth rules are unchanged.
- `GetRainIntensity` (= `UEnvironmentSubsystem::GetRainIntensity()`): 0 SunnyDay / Overcast, `rain_intensity_rain` 0.6, `rain_intensity_storm` 1,
  blended; 0 on arctic. `ComputeIsRaining` = saturate(rain / 0.25); `ShouldShowScreenDrops` = temperate, `screen_drops` and IsRaining >= 0.5.
  Also `GetWetness()`, `GetPuddleAmount()`, `GetClimate()`, the debug `SetClimate()`.

### 17.4 Gameplay per profile (`environment.json` block `"temperate"`; the arctic keys unchanged)
| Key | Value | Note |
|---|---|---|
| `cold_mult_sunny` / `_overcast` / `_rain` / `_storm` | 0.25 / 0.3 / 0.4 / 0.6 | = 0.25 / 0.3 / 0.32 / 0.24 of the arctic Clear / Overcast / Snowfall / Blizzard; rain colder than sun |
| `cold_mult_early_morning` / `_midday` / `_afternoon` / `_night` | 1.1 / 0.85 / 1.0 / 1.2 | the temperate phase table (arctic 1.15 / 0.9 / 1.0 / 1.3) |
| `roster_enabled` | false | narrative levels keep their waves as written (`[Env] roster wave N (...): temperate level - the wave stays as written`) |
| `falling_snow` | false | the presenter feeds the snow Niagara 0 whatever the preset says |
| `screen_drops` | true | MPC ScreenDrops while it rains (user decision) |
| `rain_intensity_rain` / `_storm` | 0.6 / 1.0 | |
| wetness / drying / puddles | see 17.3 | |
| `start_wetness_rain` / `start_puddles_rain` / `_storm` | 1 / 0.5 / 1 / 1 | |

Cold: `ColdSurvivalComponent::GatherEnvironment` reads `GetBlendedColdWeatherMultiplier` (the state's climate table) and
`GetColdPhaseMultiplier(Config, Climate, Phase)`; commander in the open, clear / sunny midday: arctic x0.9, temperate x0.21. Unchanged by the
climate (noted): the formation's blizzard diamond and the operatives' storm walk layer key on the Blizzard rung, so they also run in a
temperate Storm (wind + low visibility); say if they should not.

### 17.5 Presenter, presets, MPC
- Per-profile preset sets: arctic `DA_Weather_Clear / _Overcast / _Snowfall / _Blizzard` untouched; temperate
  `/Game/Environment/Weather/Presets/DA_Weather_Temperate_Sunny / _Overcast / _Rain / _Storm` (`UWeatherPresetAsset::Climate` = Temperate,
  created by `Scripts/Editor/create_weather_presets.py` from the C++ defaults `EnvironmentVisualRules::GetDefaultPreset(Climate, Weather)`, new
  assets only). The presenter loads both sets at BeginPlay and applies the state's climate (`GetPreset(Climate, Weather)`, `GetActiveClimate()`);
  `PreviewClimate` on a placed presenter previews the temperate look in the editor.
- The looks (multipliers of the authored rig where noted; no snow, no frost on any temperate preset):

  | Preset | Sun x / K offset | Sky light x | Rayleigh / Mie | Fog density / start | Exposure / saturation | Wind |
  |---|---|---|---|---|---|---|
  | SunnyDay | 1.35 / -600 (noon ~5800 K), tint warm | 1.0 | 1.0 / 0.5 | 0.006 / 30 m | 0 / 1.1, warm tint | 0.1 |
  | Overcast | 0.4 / +500, source 6 deg | 1.35 | 0.65 / 2.2 | 0.025 / 15 m | 0 / 0.8 | 0.3 |
  | Rain | 0.15 / +1200, source 10 deg | 1.0 | 0.5 / 3.5 | 0.035 / 15 m, volumetric | -0.6 / 0.72, blue-grey | 0.45 |
  | Storm | 0.06 / +1500, source 15 deg | 0.8 | 0.4 / 5 | 0.055 / 9 m, volumetric | -1.2 / 0.6, darker blue | 0.9 |

  Stills (`Saved/Screenshots/Environment/climate/`, `climate_sheet.png`; mean luminance): SunnyDay 12:00 172, Overcast 12:00 164, Rain 12:00 138,
  Storm 12:00 110, SunnyDay 07:00 162 (long warm shadows), Rain 23:00 77 (moonlit). The first pass (exposure +0.3 .. 0, fog from 6-12 m) washed
  Rain / Storm out to the overcast grey; the negative exposure and a later fog start fixed the ladder of darkness. Wet surfaces / rain FX: next step.
- Day curves: the arctic curves are reused for both profiles (their noon 6400 K + the SunnyDay -600 K offset gives the warm sun); a profile
  of its own was not needed.
- `MPC_Environment` (`Scripts/Editor/create_environment_mpc.py`, idempotent, every existing parameter kept with its id): scalars `RainIntensity`,
  `Wetness`, `PuddleAmount`, `IsRaining`, `ClimateTemperate`, `ScreenDrops`, all default 0. The presenter writes them only on a temperate
  level (`ApplyRain`; zeros once after a switch back / visuals off), so an arctic level never touches them. For the rain visuals (next step,
  the migrated assets of section 16 "Migrated rain / sunny / storm assets"): the old `MPC_WeatherSettings.IsRaining` -> `IsRaining`,
  `PuddleSize` -> `PuddleAmount`, `EnableRainDropsPostProcessFX` -> `ScreenDrops`; `RainIntensity` scales `NS_Raindrops`' spawn rate.

### 17.6 Console, test map, tests, smokes
- `Codex.Env.Climate arctic|temperate` (debug): re-initialises the weather for the profile like a level of that climate (the hour continues,
  the visuals snap). `Codex.Env.SetWeather` takes the temperate names on a temperate level.
- Test map `/Game/Environment/Weather/Test/L_WeatherRainTest` (`Scripts/Editor/create_weather_rain_test_map.py`, own assets only): a 60 x 60 m
  floor, a 6 x 5 m hut, crates / barrels / a low wall / pillars / a cone with `M_RainTest_Surface` + `MI_RainTest_Ground / _Wood / _Stone`,
  PlayerStart, NavMeshBoundsVolume, no lighting actors (the presenter's own rig; a placed `WeatherPresenter` previews 12:00 SunnyDay in the
  editor). Level data `Content/Data/LevelJson/L_WeatherRainTest.json` (`"climate": "temperate"`, start 12:00, combat by button).
- Stills: `CodexTactics.EnvironmentShots visuals climate` on that map -> `Saved/Screenshots/Environment/climate/`.
- Tests `CodexTactics.Environment.Climate.*` (8): ProfileParse, NameMapping, TemperateLadder, WetnessRates, NoSnowGrowthInTemperate,
  ArcticUnchanged, SaveAndCarry, Tuning.
- Smokes: `ClimateSmoke` (nullrhi, L_WeatherRainTest: temperate, SunnyDay start without snow, Rain wets the ground at the rain rate, SunnyDay
  noon dries it at the sunny rate, puddles drain slower, the MPC rain scalars match, `Codex.Env.Climate` arctic / temperate) and
  `ClimateArcticSmoke` (L_MovementTest: arctic, the rain scalars 0, Snowfall grows the cover as before, Rain refused). `Scripts/test_map.json`
  rule `climate-profiles`.
- Results 2026-10-26: `CodexTactics.Environment + LevelJson + Save` 114 / 114 (the 8 new ones included); smokes ClimateSmoke, ClimateArcticSmoke,
  WeatherSmoke, WeatherLakeSmoke, TutorialSmoke, FrozenFactoryStartSmoke, EnvironmentSmoke, EnvironmentLakeSmoke, ColdSmoke,
  EnvironmentRosterSmoke, StartLightingSmoke all PASS (ClimateSmoke: Rain 30 design s 0.000 -> 0.250 wetness, SunnyDay noon 1.000 -> 0.875).

### 17.7 Arctic A/B (the presenter must not change the arctic look)
`CodexTactics.EnvironmentShots visuals` (1280x720 offscreen, -benchmark -fps=30) on the lake (`-LevelJson=L_Demo01_A_FrozenLake.json`) and the
factory, two runs on the build before the change (A/A = the noise floor) and two after; `Scripts/Tools/image_diff.py`, mean absolute
difference (levels of 255) / pixels > 8 levels; stills in `Saved/Screenshots/Environment/climate_ab/<before_1|before_2|after_1|after_2>/`.

| Still | A/A before_1 vs before_2 | before_1 vs after_2 | before_2 vs after_1 |
|---|---|---|---|
| Lake Snowfall 23:00 | 0.865 / 1.56 % | 0.876 / 1.54 % | 0.861 / 1.50 % |
| Lake Clear 12:00 | 0.296 / 0 % | 0.293 / 0 % | 0.246 / 0 % |
| Lake Blizzard 12:00 | 0.592 / 0.93 % | 0.620 / 1.05 % | 0.537 / 0.88 % |
| Factory Clear 12:00 | 0.165 / 0 % | 0.163 / 0 % | 0.248 / 0 % |
| Factory Snowfall 23:00 | 2.348 / 4.43 % | 2.256 / 4.36 % | 2.324 / 4.35 % |

Every still is at the A/A noise level (falling snow / Lumen). One outlier: `after_1` factory Clear 07:00 was +17 levels brighter - the first
run after the MPC got its new parameters (the referencing materials recompiled their shaders during the first frames); `after_2` vs
`before_1` is 0.39 (A/A 0.20, p99.9 3 levels), so it was the shader warm-up, not the climate code.

### 17.x Blizzard-only behaviours stay arctic (user decision 2026-10-26)
The squad's tight blizzard diamond (`USquadSubsystem::IsFormationBlizzard`) and the operatives' walk-against-the-wind layer
(`UOperativeAnimInstance` blizzard walk) run only in the ARCTIC blizzard; a temperate Storm (the same `EWeatherState::Blizzard` rung) never
triggers them (`EnvironmentRules::IsArcticBlizzardClimate`; test `CodexTactics.Environment.Climate.BlizzardBehavioursArcticOnly`).

## 18. Rain visuals (2026-10-26)

User decisions 2026-10-26 (weather chat): rain / sunny exist ONLY on the temperate narrative levels (section 17); every other level stays arctic
exactly as before; the screen raindrops are ON for the tactical camera while it rains; like the snow, the rain look is tuned by the user
without code. UE-only (no Godot reference). Visual only: nothing in the game reads any of it; smokes stay deterministic.

### 18.1 Pieces
| Piece | Where | What |
|---|---|---|
| `UWeatherRainFxComponent` | `Source/CodexTactics/.../Environment/WeatherRainFxComponent.*` | Created by the presenter at runtime the first time the climate in force is Temperate (`AWeatherPresenterActor::EnsureRainFx`; an arctic level never has one: zero cost). Owns the rain and puddle-splash Niagara components, the screen-drops post process, the lightning flash light. Mirrors `UWeatherSnowFxComponent`: follows the tactical view focus (moved upwind in a strong wind), async load, nothing without a renderer, the visuals toggle, a pre-warmed start at a level start / load in rain. |
| `WeatherRainFxRules` | `.../Environment/WeatherRainFxRules.*` | Pure logic (tests `CodexTactics.Environment.RainFx.*`): look intensity per rung, smoothing, intensity -> spawn / opacity / slant / splashes, puddle-splash density, screen-drop strength, the indoor kill-box rules (`ToIndoorTuning` -> the snow's `WeatherSnowFxRules`), the lightning schedule / flash. |
| `URainFxSettings` / `DA_RainFX` | `/Game/Environment/Weather/Rain/DA_RainFX` | The look, edited in the editor (18.2). |
| `NS_Weather_Rain` | `/Game/Environment/Weather/Rain/Niagara/` | Retuned copy of the migrated `NS_Raindrops` (untouched). |
| `NS_Weather_PuddleSplashes` | same | Copy of `NS_PuddleSplashes` (untouched): a spawn-rate disc instead of one burst. |
| `M_Weather_Raindrop` + `MI_Weather_Raindrop` | same | The streak material (new). |
| `PPM_Weather_RainScreenDrops` + `PPMI_` | `/Game/Environment/Weather/Rain/PostProcess/` | Copy of `PPM_RainDropsOnScreen` (untouched): the old `MPC_WeatherSettings` switches replaced by a scalar `Strength`. |
| `M_WetOverlay` / `M_WetGroundOverlay` + MIs | `/Game/Environment/Weather/Rain/Materials/` | The wet look of objects / ground slabs (new overlay materials; the snow overlays are not touched). |
| `MF_RainWetSurface` | same | Wet ground + puddles + ripples inside `M_Landscape_Master` (the only existing asset edited). |
| Wet mode of `UWeatherSnowOverlayComponent` | `.../WeatherSnowOverlayComponent.*` | On a temperate level the SAME overlay targets get the wet materials (`ApplyWetCover`); `ApplySnowCover` (arctic) switches back. The arctic path is unchanged and never loads a wet material. |
| Scripts | `Scripts/Editor/create_weather_rain_fx.py`, `create_wet_surface_materials.py`, `landscape_rain_patch.py`, `rain_hlsl.py` (shared HLSL) | Build the assets (18.8). |

Presenter wiring (`AWeatherPresenterActor`): on a temperate level `ApplyRain` writes the MPC rain scalars (section 17) and feeds the rain
component (`FWeatherRainFxInput`: the Rain / Storm weather amounts, MPC RainIntensity / PuddleAmount / ScreenDrops, the blended preset wind and
its direction, the environment's RNG state for the lightning); `ApplySnow` calls `SnowOverlay->ApplyWetCover(Wetness, PuddleAmount)` instead of
the snow cover; `ApplyPostProcess` adds `DA_RainFX.WetExposureBias x Wetness` (18.2). After a debug switch back to arctic the rain stops at once.
New debug setter `UEnvironmentSubsystem::SetWetness(Wetness, Puddles)` and console `Codex.Env.Wetness <0..1> [puddles]` (preview / tuning).

### 18.2 DA_RainFX - how the user tunes the rain (no code)
Open `/Game/Environment/Weather/Rain/DA_RainFX` and edit; the component re-reads it on every presenter application (20 Hz), so a change made
while PIE runs shows within a fraction of a second. Preview the wet look with `Codex.Env.Wetness <0..1> [puddles]`, the weather with
`Codex.Env.SetWeather Rain|Storm|Sunny instant` (on a temperate level), a lightning strike with `Codex.Env.Lightning 1` in a storm.
`Scripts/Editor/create_weather_rain_fx.py` never overwrites it (only `CODEX_RAINFX_FORCE=1`, which also rebuilds the other rain assets).

| Group | Field (default) | Meaning |
|---|---|---|
| Weather | `RainLookIntensity` 0.55, `StormLookIntensity` 1 | How heavy the rain LOOKS on the Rain / Storm rung (blended through a transition). The gameplay rain (wetness rates, MPC RainIntensity) stays in `environment.json`. |
| Drops | `SpawnRateAtFull` 26000, `SpawnRateExponent` 1.5 | Drops / s at look 1 (Rain 0.55 -> 0.41 of it). GPU cost: 18.9. |
| | `DropLengthMin / Max` 35 / 65 cm, `DropWidthMin / Max` 3 / 4.5 cm | The streak (velocity-aligned sprite). |
| | `FallSpeed` 1100 cm/s, `DropAcceleration` 4000, `DropLifetime` 3 s | Terminal speed (reached in ~0.3 s); the drops die on the depth buffer (ground, roofs) or after the lifetime. |
| | `OpacityMin / Max` 0.3 / 0.5, `DropOpacityRandomMin` 0.5, `DropColor`, `DropBrightness` 1.1 | The drop is LIT (translucency volume): pale by day, dark at night. |
| | `SpawnRadius` 26 m, `SpawnHeight` 26 m | The cylinder on the view focus (keep it above the highest camera). |
| | `NearFadeStart` 7 m, `NearFadeLength` 11 m | No big streaks right in front of the camera. |
| Wind | `DropWindSpeed` 750 cm/s (x wind^2), `UpwindOffsetAtFullWind` 15 m, `WindStrengthFloor` 0.05 | The slant (Storm wind 0.9 -> ~29 deg). |
| Splashes | `bSplashes`, `SplashRateAtFull` 3000, `SplashSizeMin / Max` 14 / 28 cm, `SplashRadius` 22 m, `SplashOpacity` 0.45 | Small ground splashes on a disc around the view (no per-drop traces). |
| Puddle splashes | `bPuddleSplashes`, `PuddleSplashRateAtFull` 700, `PuddleSplashSizeMin / Max` 22 / 40 cm, `PuddleSplashRadius` 20 m, `PuddleSplashOpacity` 0.35, `PuddleSplashMinPuddles` 0.05 | Splash crowns: rate x look intensity x MPC PuddleAmount. |
| Screen drops | `bScreenDrops`, `ScreenDropsStrength` 0.3, `ScreenDropsTiling` 0.9, `ScreenDropsSpeed` 0.3 | Strength x MPC ScreenDrops x RainIntensity (Rain 0.18, Storm 0.3: subtle at the tactical camera). |
| Wet look | `WetExposureBias` -0.4 EV | x Wetness: the auto exposure would otherwise brighten a soaked (darker) world back to the dry look. |
| Lightning | `bLightning`, `LightningMinStorm` 0.5, `LightningIntervalMin / Max` 7 / 20 s, `LightningFlashLux` 9, `LightningColor`, `LightningFlashSeconds` 0.55, `LightningMaxPulses` 3 | Storm only (18.6). |
| Indoors | `bNoRainIndoors`, `IndoorSearchRadius` 60 m, `IndoorMarginCm` 20, `IndoorBelowCm` 150, `bKillUnderRoofs`, `RoofTraceHeight` 25 m, `RoofMaxSizeCm` 60 m | The falling snow's rules (section 12.b). |
| Smoothing / start | `IntensityRisePerSecond` 0.4, `IntensityFallPerSecond` 0.3, `OffThreshold` 0.02, `StartWarmupSeconds` 0 (= the drop life), `StartWarmupTickSeconds` 0.1 | |

The materials are tuned in their instances (no code either): `MI_Weather_Raindrop` (defaults only - the component overrides NearFade /
Brightness / OpacityRandomMin from DA_RainFX; `Glow` = an emissive share), `MI_WetOverlay` (`WetOpacity` 0.5, `TopMin`, `SideShare`, `WetColor`,
`TopRoughness` / `SideRoughness`, `Specular`, drips `DripThreshold` / `DripStrength` / `DripTilingCm`, ripples), `MI_WetGroundOverlay` (film:
`FilmOpacity` 0.4, `FilmColor`, `FilmRoughness`, `FilmReflection`; puddles: `PuddleOpacity` 0.8, `PuddleColor`, `PuddleRoughness`, `PuddleBare` /
`PuddleFull` (coverage at PuddleAmount 0+ / 1), `PuddleEdge`, `FlatMin`, `PuddleReflection` 7, `PuddleMacroTilingCm` / `PuddleMidTilingCm`; ripples:
`RippleStrength`, `RippleCellCm`, `RippleSpeed`, `RippleRing`, `RippleFadeStartCm` / `RippleFadeLengthCm`; `ZoneSoftCm`), `MI_Landscape_Master`
group `11_RainWet` (`WetStrength`, `WetDarken`, `WetSaturation`, `WetRoughnessScale`, puddles, ripples), `PPMI_Weather_RainScreenDrops`
(`RaindDropsNormalStrength` - the old name kept).

### 18.3 Falling rain: NS_Weather_Rain (the retune of NS_Raindrops)
- `MainRaindropsGPU` = the rain: spawn rate `User.SpawnRate`; a cylinder on the SYSTEM (`Shape Origin = Engine.Owner.Position`, radius / height
  `User.SpawnRadius / SpawnHeight`, from the ground up; the old camera-relative `NMS_CameraProperties` is disabled); colour `User.DropColor`
  (alpha = opacity); life `User.DropLifetime`; size `User.DropSize` (width, length) x a random `User.DropSizeRandomMin`..1; gravity
  `User.DropGravity`, speed limit `User.FallSpeed`; the wind slant = a spawn velocity `User.DropWindVelocity` (module `AddVelocity`); the old
  size-over-life curve (it shrank the streaks to dashes) and the distance brightness are off; depth-buffer collision ON with kill on collision
  (drops end on the ground / a roof in view); the old single kill volume (a 1 m box at the world origin) off; 4 kill boxes `User.KillBox<i>`
  (the snow's indoor rules); material `MI_Weather_Raindrop` bound to `User.DropMaterial` (the component's dynamic instance); fixed bounds
  +-65 m / 32 m.
- `RainSplashes` (CPU): small splash sprites on a disc around the view (`SpawnRate <- User.SplashRate`, a flat cylinder `User.SplashRadius` on
  the system, size `User.SplashSizeMin / Max`, tint `User.SplashTint`); the old event-driven spawn stays but never fires.
- `MainRaindropsCPU` (per-drop line traces -> collision events) is idle (`User.CpuDropRate` 0): too costly for a top-down view.
- `M_Weather_Raindrop`: translucent, lit by the translucency volume (non-directional), responsive AA (thin fast streaks survive TSR), a soft
  core across the streak and tapered ends, a near-camera fade, a per-drop random opacity. PS 362 instructions, 2 samplers.

### 18.4 Puddle splashes: NS_Weather_PuddleSplashes
The burst emitter of `NS_PuddleSplashes` became a spawn-rate disc on the view focus (`User.SpawnRate`, `User.Radius`, size `User.SizeMin / Max`,
tint `User.Tint`; renderer `MI_PuddleSplash_01`, the migrated crown). The component writes rate = `PuddleSplashRateAtFull x look intensity x MPC
PuddleAmount` (0 under `PuddleSplashMinPuddles` or without rain) and deactivates it at 0.

### 18.5 Screen drops
`PPMI_Weather_RainScreenDrops` (the old refraction of the scene colour by `T_RainDropsPacked`, gate replaced by `Strength`) as the blendable of the
component's own unbound `UPostProcessComponent` (`RainScreenDropsPost`, priority 10) on the presenter; strength = `ScreenDropsStrength x MPC
ScreenDrops x RainIntensity` (Rain 0.18, Storm 0.3), tiling / speed from DA_RainFX. Off (no rain, arctic, visuals off) = the post-process
component disabled: no pass. The stills `08` (default) / `09` (x3, to see it) show the look: small refracting drops; edges wobble slightly.

### 18.6 Lightning (Storm)
A shadowless directional flash light (`RainLightningFlash`, from high above, `ForwardShadingPriority` -1 so the sun stays the forward-shading
light) flickers 2..`LightningMaxPulses` decaying pulses within `LightningFlashSeconds`. While the Storm weather amount >= `LightningMinStorm`, strikes
come every `LightningIntervalMin..Max` s, the schedule derived from the environment's RNG state when the storm started (`ComputeStrikeDelay(seed,
index)`: deterministic; the environment's RNG is never advanced). Off on automated runs (smokes, bots, captures: `-ExecCmds=CodexTactics.*` /
`-CodexBot`) unless `Codex.Env.Lightning 1`; `0` = always off. No bolt sprite: the top-down tactical camera never sees the sky (the migrated
`NS_Lightning` stays unused).

### 18.7 Wet surfaces (all exactly neutral at Wetness / PuddleAmount 0)
- **Objects** (`M_WetOverlay`, put by the overlay component in wet mode on the same outdoor meshes the snow overlay uses, under-roof /
  interior meshes excluded): a translucent, forward-lit near-black layer, opacity = Wetness x face mask x `WetOpacity` 0.5 (tops full, sides
  `SideShare` 0.45 + vertical drip streaks, none under overhangs), low specular, rougher on the sides; rain dimples on the tops while it rains.
  VS 367 / PS 1856, 6 samplers (the forward translucency base; `M_SnowOverlay` PS 1883 for scale).
- **Ground slabs** (`M_WetGroundOverlay`): a wet film (Wetness x `FilmOpacity` 0.4), puddles (a two-scale noise field from `T_TilingNoisePacked`,
  coverage from 0 at PuddleAmount 0 to ~20 % at 1, flat faces only, none on a dry ground), a water look in the puddles (near-black albedo,
  roughness 0.06, a faked reflection = the sky light cubemap along the rippled reflection ray x Schlick Fresnel x `PuddleReflection`, so it follows
  the time of day and the weather), procedural ripple rings while it rains (no texture; faded out beyond 18-40 m against sparkle), interior camera
  zones dry. VS 367 / PS 2008, 7 samplers (`M_SnowGroundOverlay` 2057 / 7).
- **Landscape** (`M_Landscape_Master` + `MF_RainWetSurface`, between the snow functions and the outputs; `MI_Landscape_Master` unchanged): wet =
  darker (`WetDarken` 0.55) and a little more saturated albedo, roughness x 0.45, specular up to 0.5; puddles in the flat spots by the noise field
  (the master's `SnowNoiseTexture` with the shared wrap sampler); flat water normal + ripples (tangent space). Every blend is `lerp(input, wet,
  mask)` with the mask exactly 0 at Wetness 0 / PuddleAmount 0, no renormalisation: the arctic output equals the input.
  Stats (PCD3D_SM6, `landscape_rain_patch.py`): before VS 148 / PS 355, 3 samplers, 10 PS texture samples; after VS 148 / **PS 456 (+101)**,
  **3 samplers (+0)**, 12 PS texture samples (+2 noise lookups through the shared sampler) - far under the landscape's 16-sampler budget.
- **Characters: skipped.** A wet sheen needs the character masters (the operatives / enemies are the user's BP assets with their own materials)
  and a second per-character overlay pass; not cheap enough for this step.
- **Exposure:** a soaked world is darker; the auto exposure brightened the stills back to the dry look (2026-10-26: wet 1.0 measured as bright as
  dry). `WetExposureBias` (-0.4 EV x Wetness, temperate only) keeps the wet darkening readable.

### 18.8 Scripts (editor commandlets, under the build lock, `-AllowCommandletRendering`)
- `create_weather_rain_fx.py`: the drop material, `NS_Weather_Rain`, `NS_Weather_PuddleSplashes`, the screen-drops PPM copy, `DA_RainFX`. Keeps
  every existing asset (hand edits are safe); `CODEX_RAINFX_FORCE=1` (+ optional `CODEX_RAINFX_ONLY=material,rain,puddles,screen,settings`)
  rebuilds; `CODEX_RAINFX_MODE=dump` writes `Saved/Logs/weather_rain_fx_dump.txt`. Log `Saved/Logs/create_weather_rain_fx.txt`.
- `create_wet_surface_materials.py`: `M_WetOverlay` / `M_WetGroundOverlay` + MIs (kept unless `CODEX_WET_FORCE=1`).
- `landscape_rain_patch.py` (`CODEX_LANDRAIN_MODE=patch|stats`): `MF_RainWetSurface` + its one call in `M_Landscape_Master` (own nodes tagged
  `landrain:`, idempotent). Re-run it after `landscape_snow_patch.py` (that one rebuilds its own nodes and the outputs).
- `rain_hlsl.py`: the shared HLSL (procedural ripples, puddle field / mask, interior zones).
- Backups before the first edit: `Saved/Backups/rain_20261026_115538/manifest.txt` (sha1 of every asset that could be touched). Verified at the
  end: only `M_Landscape_Master` differs (fff01c88... before); `MI_Landscape_Master`, `M_SnowOverlay`, `M_SnowGroundOverlay`, their MIs,
  `MPC_Environment` and every migrated rain / storm original are byte-identical. Restore = copy the backup file back.

### 18.9 Cost (GPU, 1920x1080, `CodexTactics.EnvironmentShots rainperf` on L_WeatherRainTest, -fps=60, median of 180 frames after a 6 s settle)
The PC was shared (two other editors open), so the cases run interleaved in two rounds per distance; compare WITHIN a round (the rounds drift
by ~1 ms). `storm_fxoff` = the rain / puddle systems hidden and the screen drops off; `storm_nooverlay` = `Codex.Env.SnowOverlay 0` +
`SnowGround 0` (no wet overlays). Log lines `[RainPerf]` in `Saved/Logs/rain_media_perf2.log`, stills `Saved/Screenshots/Environment/rainperf/`.

| Camera / round | Sunny | Rain | Storm | Storm fx off | Storm no overlay | Particles + screen drops | Wet overlays |
|---|---|---|---|---|---|---|---|
| 26 m / 1 | 5.60 | 7.40 | 8.03 | 7.94 | 8.17 | +0.09 | (-0.14, noise) |
| 26 m / 2 | 6.96 | 8.72 | 9.09 | 8.82 | 8.40 | +0.27 | +0.69 |
| 40 m / 1 | 6.91 | 8.71 | 8.80 | 8.57 | 8.30 | +0.23 | +0.50 |
| 40 m / 2 | 7.07 | 8.95 | 9.07 | 8.75 | 8.54 | +0.32 | +0.53 |

- The rain visuals cost **~0.1-0.3 ms** (24k drops alive in a storm, 10.5k in rain, + splashes + the screen-drop pass) **+ ~0.5-0.7 ms** for the wet
  overlays (13 props + the floor slab: one forward-lit translucent pass, like the snow ground overlay, 12.d). Draws +8 (fx) / +14 (overlays).
- The rest of Sunny -> Rain / Storm (~1.8 ms) is the temperate Rain / Storm look itself (section 17: volumetric fog), not the rain FX.
- Game thread unchanged (~3.1-3.3 ms). Arctic levels: **0** - no rain component exists there (smoke `ClimateArcticSmoke` checks it), the wet
  materials are never loaded, the snow overlay path is unchanged; the landscape's extra instructions run with Wetness 0 (see 18.10).

### 18.10 Arctic A/B (every edited material renders identically with the arctic profile / rain 0)
Method of section 17.7: `Scripts/Tools/image_diff.py`, mean absolute difference (levels of 255) / pixels > 8 levels; two runs before the edit
(A/A = the noise floor) and runs after it; 1280x720 offscreen. The landscape (`CodexTactics.EnvironmentShots terrain nomovie notracks` on
L_SnowTerrainTest, the only map with `M_Landscape_Master`; `after_3` = the final material) and the arctic levels (`visuals` on the lake and the
factory: the presenter / overlay code changes). Stills `Saved/Screenshots/Environment/rain/ab/<terrain|arctic>_<before|after>_N/`, sheet
`Saved/Screenshots/Environment/rain/rain_ab_sheet.png` (before | after | diff x8).

| Still | A/A before_1 vs before_2 | before_1 vs after_1 | before_2 vs after_2 |
|---|---|---|---|
| terrain snow 0.0 / 0.5 / 1.0 (landscape, day) | 0.000 / 0 % | 0.000 / 0 % (max 1-2 levels) | 0.000 / 0 % |
| terrain night fresh (falling snow) | 0.344 / 0.71 % | 0.329 / 0.70 % | 0.360 / 0.79 % (after_3) |
| terrain night frost | 0.287 / 0 % | 0.279 / 0 % | 0.280 / 0 % (after_3) |
| lake Blizzard 12:00 / Snowfall 23:00 | 0.520 / 0.74 %, 0.835 / 1.53 % | 0.542 / 0.82 %, 0.858 / 1.57 % | 0.544 / 0.79 %, 0.832 / 1.51 % |
| lake Clear 12:00 / 23:00 | 0.254 / 0 %, 1.005 / 2.31 % | 0.194 / 0 %, 1.026 / 2.40 % | 0.230 / 0 %, 0.850 / 2.20 % |
| factory Clear 07:00 / 12:00 | 4.504 / 0.53 %, 0.217 / 0 % | 0.368 / 0 %, 0.205 / 0 % | 4.493 / 0.18 %, 0.154 / 0 % |
| factory Blizzard 12:00 / Snowfall 23:00 | 1.752 / 3.33 %, 2.391 / 4.57 % | 1.714 / 3.37 %, 2.311 / 4.53 % | 1.718 / 3.28 %, 2.421 / 4.58 % |

Every still is at the A/A noise level; the lit landscape stills are identical (the rain function's masks are exactly 0). One outlier, as in
17.7: the factory's first still (Clear 07:00) of the first run after the material changes came out 50 levels darker (exposure not settled while
the shaders compiled on a loaded PC); the two re-runs (`after_2`, `after_3`) match it to 0.37 / 4.49 (A/A 4.50). Instruction counts of every
edited material: `M_Landscape_Master` (and through it `MI_Landscape_Master`) VS 148 / PS 355 -> VS 148 / PS 456, samplers 3 -> 3, PS texture
samples 10 -> 12. No other existing material was edited (the wet / rain materials are new: 18.3, 18.5, 18.7).

### 18.11 Tests, smokes, captures
- Tests `CodexTactics.Environment.RainFx.*` (7): LookPerWeather, IntensityToSpawn, PuddleSplashDensity, ScreenDrops, KillBoxReuse, Lightning,
  WetOverlayGate; `CodexTactics.Environment.LandscapeMaterial.Graph` now expects BaseColor / Roughness / Normal / Specular through
  `MF_RainWetSurface` fed by the snow functions (AO / Emissive direct). `CodexTactics.Environment` 111 / 111.
- `RainSmoke` (rendered, L_WeatherRainTest; `Scripts/test_map.json` rule `rain-visuals`): SunnyDay - the rain component exists, nothing falls,
  no screen drops / puddle splashes, the overlay in wet mode shows nothing; Rain - active at RainLookIntensity, ~10.6k drops, screen drops 0.18,
  puddle splashes = the rule, MPC Wetness / PuddleAmount, wet overlays on 13 props + 1 slab, no snow; Storm - x2.45 spawn, ~24k drops, screen drops
  0.3, wind slant, no lightning under the smoke until `Codex.Env.Lightning 1`, a strike flashes; the leader in the hut inside a kill box; SunnyDay
  - stopped; `Codex.Env.Climate arctic` - stops at once, the overlay back in snow mode. `ClimateArcticSmoke` also checks that an arctic level has
  no rain component. Results 2026-10-26: RainSmoke, ClimateSmoke, ClimateArcticSmoke, WeatherSmoke, WeatherLakeSmoke, TutorialSmoke,
  FrozenFactoryStartSmoke PASS (`smoke_parallel -Jobs 2`).
- Captures (`EnvironmentRainShotsCommand.cpp`; run with `r.SetRes 1920x1080w,DisableAllScreenMessages` first in `-ExecCmds` - the project config
  sets 1280x720): `CodexTactics.EnvironmentShots rain [noclip]` (L_WeatherRainTest) -> `Saved/Screenshots/Environment/rain/` `01_sunny_1200`
  .. `13_hut_storm` + `clip/` -> `rain_10s.mp4`; `rainterrain` (L_SnowTerrainTest switched to temperate) -> `rain/terrain/t1..t5`; `rainperf`;
  `rainprobe` (drop look variants for tuning).

### 18.12 Known limits / next
- Roofs that are no camera zone keep the RAIN out (kill boxes over the view focus / leader) but not the wet ground under them (the snow ground
  overlay's known limit, 12.d): a hut floor that is part of a big outdoor slab gets the wet film / puddles too.
- The puddle splashes and the ground splashes sit on the plane of the view focus (no per-drop traces): on crates / roofs there are none.
- The landscape's wet look was only seen on the arctic terrain test map switched to temperate (its layers are snow / rock / ice): a temperate
  landscape (grass / soil layers) should be checked once a narrative level has one.
- Characters get no wet sheen (18.7).

## 19. Start flicker fix (2026-10-27)

User report (main 804d64c2, PIE): "a strange flicker when the level starts"; then "the first ~1 second looks as if there's a thunderstorm -
flashes", on any level. UE-only, no Godot reference.

### 19.1 Evidence
- `CodexTactics.StartFlickerShots` (dev, `-StartFlickerShots=<tag>` arms it at `PostLoadMapWithWorld`, so frame 1 = the level's first
  rendered frame; no `-ExecCmds` = the player's start) reads EVERY frame back (`UGameViewportClient::OnScreenshotCaptured`, nothing written
  by the engine) and records per frame: the mean luma (Rec. 709 on 8-bit sRGB) of the frame and of the top / middle / bottom third and the
  HUD band (bottom 15 %), the mean abs 128x72-cell difference to the previous frame, the eye adaptation exposure read back through a world
  view extension, the camera fade, the presenter's snap / applications / sun / moon / sky light, the lightning flash scale / strikes
  -> `Saved/Screenshots/Environment/flicker/<tag>/<map>.csv` + 320 px thumbnails. `CodexTactics.PIEFlickerShots <map> <tag>` does the same
  in the EDITOR: loads the map, presses Play in the level viewport, records from the PIE world's first frame (real frame times).
- Timestamped events: log category `LogCodexEnvTrace` (`[EnvTrace] f<engine frame> t<game s> ...`; one-offs at Log, per apply at Verbose:
  `-LogCmds="LogCodexEnvTrace Verbose"`): presenter BeginPlay / applications, the exposure mode switch, sky light recaptures, overlay
  scans / toggles (+added -removed, Nanite fallbacks), snow / rain Niagara activations and warm-ups, screen drops, lightning flash on / off,
  the snow-tracks render target clear / scroll / MPC window, minimap captures, character snow pushes.
- `Scripts/Tools/capture_start_flicker.ps1 -Tag <t> [-Compare <t0>]` (one game at a time under the build lock; 1280x720 offscreen,
  `-UseFixedTimeStep -FPS=60`, 240 frames each of the lake, the factory, L_MovementTest, L_WeatherRainTest) + `Scripts/Tools/flicker_strip.py`
  -> `strip_<tag>.png` (per map: mean + bands + cell diff, fade / snap / lightning spans, event ticks, thumbnails round the largest jumps),
  `compare_<a>_<b>.png`.
- Result on main (tag `before`): during the presenter's start EXPOSURE SNAP WINDOW (section 16: 1 s at 1000 f-stops / s, logged
  `exposure mode SNAP` at f0 and `exposure mode authored` at f60 t1.017) the picture jumps every 2-5 frames - the lake 109 -> 174 -> 18 ->
  109 ... -> 237, largest step **169.4** (frame 17), 28 steps > 50 in 58 frames; L_WeatherRainTest 117 <-> 9 / 198, largest **167.3** (frame 46); the
  read-back exposure swings 0.14 <-> 37 (the min / max clamps). It stops on the frame the window ends; afterwards the cell diff is ~0.2.
  The factory's oscillation (exposure 7 <-> 62) runs under its black title fade, but leaves the exposure off target (an overshoot to luma
  173 at frame 166 and a slow fall to 144); the rain test also drifts 117 -> 160 -> 110 after the window. L_MovementTest's dawn sat at the exposure
  clamp (no visible jump). No other event lines up with a jump: the overlay re-scan (f3), the snow warm-up (f1), the minimap captures
  (f61-64 / f115-118), the sky light (real-time capture: no recapture) all fall on flat frames.
- Lightning: no flash on any start - arctic levels never create the rain FX (no flash light at all); the rain test (temperate, Rain)
  had flash 0 / 0 strikes. The "thunderstorm" was the exposure.

### 19.2 Cause
The scene colour is rendered PRE-EXPOSED with the exposure the CPU read back from the GPU a few frames earlier
(`FViewInfo::UpdatePreExposure` -> `GetLastEyeAdaptationExposure`); at 1000 f-stops / s one 60 fps frame closes the whole gap
(1 - 2^(-16.7)), so the adaptation chases its own stale pre-exposure (clipping, Lumen's pre-exposed caches) and oscillates for as long as
the window lasts. In PIE the window is 1 s of WALL time (long first frames), exactly "the first ~1 second".

### 19.3 Fix
- `EnvironmentStartRules::ExposureSnapSeconds` 1 -> **0**: no fast adaptation window. The snap's `SnapCameraCutFrames` (3) already settle
  the exposure: on a camera cut the engine's eye adaptation jumps to its target (`ForceTarget`); the lake's read-back exposure is 1.367 on
  frame 4 and stays there (= its final value). Then the authored speeds (3 up / 1 down).
- The optional window (`Codex.Env.StartExposureSeconds` > 0) runs at `ExposureSnapSpeed` **30** (was 1000), and any speed is capped by
  `EnvironmentStartRules::ClampStableExposureSpeed` so one frame closes at most `MaxStableExposureClosure` 0.5 of the gap (a long PIE hitch
  frame too). Console: `Codex.Env.StartExposureSpeed`, `Codex.Env.StartExposureSeconds`, `Codex.Env.StartCameraCutFrames`,
  `Codex.Env.StartExposureNoCap 1` (debug: reproduces the old flicker with seconds 1 / speed 1000).
- Lightning gated on the climate at the source: `FWeatherRainFxInput::bTemperateClimate` (false by default, set only by the presenter's
  temperate path) and `WeatherRainFxRules::IsLightningAllowed`; outside a temperate level a strike in flight is dropped too. The flash light
  was and is created hidden at 0 lux.
- Swept on the lake before choosing (`sw_*`): window 0 / speed 10 / speed 30 all flat (largest step 3.6 - the tutorial panel opening at
  frame 62, UI).

### 19.4 Before / after (fixed step 60 fps, 240 frames, largest frame-to-frame mean-luma step outside camera fades)
| Map | before | after |
|---|---|---|
| L_Demo01_A_FrozenLake (23:00 Snowfall, locked) | 169.4 | 3.65 (UI panel, frame 62) |
| L_WeatherRainTest (temperate 12:00 Rain) | 167.3 | 0.98 |
| L_Demo01_B_FrozenFactory (title fade-in) | 4.3 (+ overshoot after the fade) | 1.85 (smooth fade-in) |
| L_MovementTest (random start) | 1.5 | 2.2 |
PIE (the user's case; `CodexTactics.PIEFlickerShots`, the editor with the lake, Play in the level viewport, `-UseFixedTimeStep -FPS=60`): the
old window (reproduced with `Codex.Env.StartExposureSeconds 1`, `StartExposureSpeed 1000`, `StartExposureNoCap 1`) jumps 70 <-> 25 / 143
every 4-5 frames, 28 steps > 20, largest **97.0**; after **1.75** (`strip_pie60_before.png`, `strip_pie60_after.png`,
`compare_pie60_before_pie60_after.png`). On this loaded machine's wall clock the offscreen PIE ran at 2-6 fps, so the 1 s window held only
~3 frames and showed nothing (`pie_before` / `pie_after`): the flicker needs a real frame rate, as on the user's PC.
The lake still opens on the moonlit night with the snow falling from the first visible frame (frame 3, after the 2-frame start cover).
Strips: `Saved/Screenshots/Environment/flicker/strip_before.png`, `strip_after.png`, `compare_before_after.png`,
`lake_frames_before_after.png`. (The starts of the unlocked levels are random: MovementTest / the factory opened in other weathers in the
two runs.)

### 19.5 Tests, smokes
- `CodexTactics.Environment.StartLighting.Snap` (no default window, the old speed closes a frame, the stable cap at 1/120..0.5 s frames),
  `CodexTactics.Environment.RainFx.Lightning` (the climate gate).
- `StartLightingSmoke` A / C: snapped by camera cuts at stable speeds (no 1000 window) / a state jump sets camera-cut frames again.
- `StartFlickerSmoke` (rendered, `test_map.json`; opens the lake, the factory, L_MovementTest, L_WeatherRainTest, `Codex.Env.Lightning 1`):
  the first 180 frames from each level's first frame - largest mean-luma step <= 20 outside camera fades, no lightning frame unless a
  temperate Storm start, an arctic level has no rain FX; each arctic level forced into a Blizzard for 60 frames: no rain FX / flash.
