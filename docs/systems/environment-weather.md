# Environment: day cycle, procedural weather, cold and enemy roster (phases 1-3 implemented: clock, weather, cold, roster, visual presenter; phase 4a: snow cover state, MPC, presenter wiring)

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
  end of `InitialiseFromWorld` (`SpawnForWorld`; never placed in a map; a game world with the project's game mode only). Adopts the map's
  `ADirectionalLight` (sun = the atmosphere sun light 0, else the brightest), a second directional light as the moon (atmosphere index 1 / "Moon"
  in the name) else its own `MoonLight` component (Movable, atmosphere light index 1), `ASkyAtmosphere`, `ASkyLight`, `AExponentialHeightFog`,
  an unbound `APostProcessVolume` else its own unbound `PostProcess` component. Records the authored values (`FWeatherAuthoredLighting`) and
  drives them at `ApplyRateHz` 20 + at once on `OnStateSet`. A Static / Stationary sun: warning, not rotated (Stationary still re-lit). Sky
  light: real-time capture -> nothing; else `RecaptureSky` at most every `SkyRecaptureInterval` 2 s when the look moved. The first application
  waits (max `MinimapHoldSeconds` 4 s) for the minimap captures, so the minimap keeps the authored lighting. Presets: soft refs
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
  Mapping in `WeatherSnowFxRules` (`FWeatherSnowFxTuning`, editable on the component): spawn rate 24000 x intensity^2 (Snowfall 0.6 -> 8640 / s,
  Blizzard 24000 / s), opacity 0.7..1, drift from intensity 0.4 up to opacity 0.65, drift size 250..500. Tests `CodexTactics.Environment.SnowFx.*` (3).
- Stills / clips: `CodexTactics.EnvironmentShots snow` (`Saved/Screenshots/Environment/snow/`, the lake: Snowfall 12:00, Blizzard 12:00 + 40 m,
  Snowfall 23:00, Clear 12:00 off; 10 s clips per snowing weather, 15 fps).
- Tune by hand (NS_Weather_Snow / MI_Weather_Snowflake): flake size / life, cylinder radius / height (keep `UpwindOffsetAtFullWind` in step),
  the `Multiply_Float001.B` wind speed constants, the drift spawn rate, MI `NearFadeStart` / `NearFadeLength` / `Softness` / `Brightness`;
  density per weather on the presenter's `SnowFx` component (`Tuning`). GPU cost: up to ~180k flakes alive in a blizzard (24000 / s x 7.5 s).

### 12.c Snow overlay on world meshes and the campaign restart rule (2026-10-22)

**Snow overlay (`M_SnowOverlay`)**
- The material is TRANSLUCENT: masked overlays are invisible in UE 5.8 (drawn under the own mesh). A tiny WPO push toward the camera prevents z-fighting.
- The mask is world-up against a threshold driven by `SnowAmount` plus noise, so the snow creeps down the sides.
- Targets: outdoor `UStaticMeshComponent`s within 60 m of the view focus, nearest first, capped by CVar `Codex.Env.SnowOverlayMax` (250). `Codex.Env.SnowOverlay 0/1` switches the feature.
- Skipped: ISM / HISM / foliage / pawns / landscape / the overlay's own components / tag `NoSnow` / translucent meshes / radius < 35 cm / wider than 60 m / flat slabs > 20 m / meshes under a roof (an upward trace) / meshes inside Closed or Shelter `ACameraZoneVolume`s (the zone's own roof counts as outside).
- Nanite meshes are skipped (the overlay is not drawn on Nanite); `Codex.Env.SnowOverlayNanite 1` switches them to the fallback mesh.
- Cost: +0.3 ms GPU / +18 draws on a building view, ~0 in a wave fight. The set is rescanned every 3 s or on camera move.

**Campaign restart rule**
- The environment carry (`UEnvironmentCarrySubsystem`) holds the END state of the level just left. `UEnvironmentSubsystem::Resolve` records the state the level was ENTERED with (`RecordEntryState`; unset = fresh start).
- Every restart of the level (Ctrl + X, the mission-failed Restart button; both go through `UMissionSubsystem::RestartMission`) calls `RestoreEntryState` BEFORE `OpenLevel`: the carry is set back to the entry state (or cleared when the level was entered fresh). Snow cover and time of day therefore no longer accumulate across failed attempts.
- NEW GAME (`Clear`) forgets both. Test `CodexTactics.GameFlow.Campaign.EnvironmentRestore` (0.40 / 09:00 -> end 0.80 / 14:00 -> restart -> 0.40 / 09:00).
