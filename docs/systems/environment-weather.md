# Environment: day cycle, procedural weather, cold and enemy roster (DESIGN, not implemented yet)

> Design of the time-of-day + weather system and how it drives cold and the enemy roster. Status 2026-10-22: user decisions recorded, no code yet.
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

## 2. Day cycle

- Each phase has a game-hour range and a **real duration** (defaults: 5 min each = 20 min/day). The clock speed is per phase
  (`game hours of the phase / real seconds of the phase`), so the long night (11 game hours) still lasts ~5 real minutes.
- Default ranges (tunable): EarlyMorning 05-10, Midday 10-14, Afternoon 14-18, Night 18-05.
- The clock runs in exploration and real-time combat, stops in the tactical pause / dialogue / cutscene / menus; in turn-based combat each round adds a fixed
  number of game minutes (OPEN). A level sets the start hour (default: early morning).
- Visuals: sun / moon pitch, light intensity / colour temperature, sky tint, stars / moon visibility come from curves over the hour (data, the old
  `HandleSunMoonRotation` / `HandleNightSettings` logic moved to C++ curves).

## 3. Procedural weather

- **Start state**: weighted random (tunable weights) from a run seed; the seed is logged and can be fixed (smokes, bot, a tutorial level can lock the weather).
- **Dwell time**: each state lasts a random time in its own `[min, max]` real seconds (proposal: Clear 4-8 min, Overcast 3-6, Snowfall 3-6, Blizzard 2-4).
- **Next state**: only a neighbour on the ladder (`EnvironmentRules::PickNextWeather(Current, Rng)`): ends are forced (Clear -> Overcast, Blizzard -> Snowfall),
  middle states pick up / down by tunable weights (optionally biased by the phase, e.g. worse weather more likely at night - OPEN).
- **Transition**: a blend of `transition_seconds` (proposal 60-90 s) between the two presets; while blending, the gameplay values (cold, visibility) interpolate too.
- Never two steps inside one transition; a transition is never interrupted (the next roll waits until the blend ends).
- Saved: hour, weather, target, blend, remaining dwell, RNG state - a load continues the same weather.

## 4. Gameplay coupling

### 4.1 Visibility score
`Visibility = PhaseLight(phase) x WeatherClarity(weather)` in 0..1 (proposal: light Midday 1.0, Afternoon 0.9, EarlyMorning 0.7, Night 0.25;
clarity Clear 1.0, Overcast 0.8, Snowfall 0.5, Blizzard 0.2). One number feeds the enemy roster and (later, OPEN) sight ranges / accuracy.

### 4.2 Enemy roster by conditions
- Spawn entries keep their `enemy_type`; a **substitution table** in `environment.json` swaps a type when the conditions match, at spawn time:
  `{ "from": "MARKSMAN", "to": "FROSTBITTEN", "when": { "visibility_below": 0.3 } }` (night + blizzard, also night + snowfall with the default numbers);
  `{ "type": "MARKSMAN", "allowed_when": { "visibility_at_least": 0.6 } }`-style gates for types that only appear in good conditions.
- Applies to wave / runtime spawns. Already spawned enemies never change when the weather changes. Map-placed enemies: OPEN (default: unchanged).
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

## 7. Open questions

- Game minutes per turn-based round.
- Dwell times, start weights, up / down weights, phase bias on the weather.
- More substitution / gating rules beyond MARKSMAN -> FROSTBITTEN (e.g. hounds at night?).
- Map-placed enemies: swap by conditions or keep as placed.
- Does weather lower sight ranges / accuracy for both sides.
- Tutorial (Demo 01): fixed weather / time or procedural.
- Footprints: the old decal notify or the existing RT `SnowTracks`; volumetric clouds or the 2D sky sphere only.
