# Systems Audit: Depot (`L_Gen_Depot_1.json`) vs. Benchmark (`L_Demo01_A_FrozenLake.json`) & Design Specifications

**Document Purpose:**
Audit and comparison of all gameplay systems on Depot (`Content/Data/LevelJson/L_Gen_Depot_1.json`) against the project benchmark level (`Content/Data/LevelJson/L_Demo01_A_FrozenLake.json`) and the systems documentation in `docs/systems/`.
This audit identifies every system currently disabled (`OFF`) or weakened on Depot, detailing the controlling JSON keys, console variables (CVars), current values, and their functional impact, preparing for Task #46 ("Depot as a real game level, not a lab").

---

## 1. Master Systems Comparison Matrix

| Gameplay System | Depot (`L_Gen_Depot_1.json`) | Benchmark (`L_Demo01_A_FrozenLake.json`) | Controlling JSON Key & Value | Controlling CVar & Current Value | Status / Impact on Depot |
|---|---|---|---|---|---|
| **Cheats: Immortality** | **ON** (Enabled) | **OFF** (Disabled) | `"cheats": { "immortal": true }` | `Codex.Cheat.Immortal` (default: `-1`) | **WEAKENED / LAB MODE**: Squad cannot die; takes zero lethal damage; trivializes combat. |
| **Cheats: Infinite Ammo** | **ON** (Enabled) | **OFF** (Disabled) | `"cheats": { "infinite_ammo": true }` | `Codex.Cheat.InfiniteAmmo` (default: `-1`) | **WEAKENED / LAB MODE**: Weapons never deplete ammunition; weapon reload/ammo economy ignored. |
| **Cold & Frostbite** | **OFF** (Disabled) | **ACTIVE** (Enabled) | `"cold_rate_mult": 0` *(Lake: `0.4`)* | N/A (Multiplies `FColdEnvironment::LevelMultiplier`) | **OFF**: Open-air cold accumulation is completely frozen at 0; frostbite never triggers. |
| **Weather Ladder & Day Cycle** | **OFF** (Missing block) | **ACTIVE** (`Snowfall`, 23:00, locked) | Missing `"environment"` block *(Lake: `"environment": { "locked": true, "start_hour": 23, "weather": "Snowfall", "snow_cover": 0.6 }`)* | `Codex.Env.Visuals` (`1`), `Codex.Env.SetWeather`, `Codex.Env.SetHour`, `Codex.Env.ClockScale` (`72.0`) | **OFF**: Starts on neutral fallback (15:00 Clear); no weather progression ladder; no snow accumulation. |
| **Wounds & Bleeding** | **NULLIFIED** by Cheat | **ACTIVE** | Inherited from `HealthComponent` / `TakeHit` | `Codex.Cheat.Immortal` | **WEAKENED**: Bleeding status effect is applied but `AbsorbByImmortalCheat` absorbs tick damage. |
| **Knockdown Mechanics** | **WEAKENED** | **ACTIVE** | Inherited from `KnockdownComponent` | `Codex.Knockdown.Chance` (`-1.f` = 0.5), `Codex.Knockdown.DamageThreshold` (`-1.f`) | **WEAKENED**: Knockdown can trigger, but lethal downed damage does not eliminate operatives due to immortal cheat. |
| **Hunger & Fatigue (Food)** | **OFF / OBSOLETE** | **ACTIVE** (Consumable Warmth/HP) | `"squad_loadout": {}` lacks food items *(Lake: `canned_food: 0`, matches: 3)* | N/A (`EPersonalItem::CannedFood` / `Bread` / `Chocolate`) | **OFF**: No food rations provided; cold is disabled so warmth-restoring food is unnecessary. |
| **Campfires & Stoves (Heat)** | **OFF** (No placed actors) | **ACTIVE** (Huts, barrels, campfires) | Procedural layout lacks `ACampfireActor` / `APortableStoveActor` / `ABarrelActor` | `UTacticalHeatSubsystem`, `tactical_heat.json` | **OFF**: Zero heat sources in procedural layout; squad has 0 matches and 0 heat packs. |
| **Loot & Ammo Supply** | **OFF** (No placed crates) | **ACTIVE** (Authored crates & ammo) | Missing `ALootCrateActor` in layout; `"squad_loadout"` has no ammo counts | N/A (`ItemStashComponent`, `LootCrateActor`) | **OFF**: No interactive loot containers; ammunition economy completely bypassed. |
| **Enemy Pressure Director** | **ACTIVE (Lab Trickle)** | **OFF (Replaced by Waves)** | `"pressure": { "first_event_sec": 30, "interval_sec": 90, "group_min": 2, "group_max": 4, "max_alive": 8 }` | `Codex.Pressure.Enabled` (`-1`), `Codex.Pressure.IntervalSec` (`0.f`) | **MODERATE**: Endless trickle of 2–4 enemies every 90 s, max 8 alive. Non-escalating. |
| **Horde Escalation** | **OFF** (Disabled) | **OFF** (Uses 3 authored waves) | `"horde_enabled": false` | `Codex.Horde.Enabled` (`-1`), `Codex.Horde.TriggerSeconds` (`-1.f`) | **OFF**: Horde director will not trigger emergency swarms regardless of mission elapsed time. |
| **Combat Waves** | **WEAKENED (1 Dummy Wave)** | **ACTIVE (3 Scaled Waves)** | `"waves": [ { "enemy_type": "FROSTBITTEN", "count": 2 } ]` | `UWaveSubsystem` | **WEAKENED**: Only 1 dummy wave of 2 Frostbitten zombies; Lake has 3 multi-lane sudden waves (up to 10 enemies). |
| **PMC Encounters** | **DEGRADED BY BUGS** | **N/A (Tutorial Scripted)** | `"pmc_squads": [ { "delay_s": 30, "posture": "defend" }, { "delay_s": 120, "posture": "hunt" } ]` | `CodexTactics.PmcSpawn` | **DEGRADED**: Spawns 2 squads (3 each), but compromised by Bug #43 (corridor blindness), Bug #44 (frozen corpses), Bug #33 (prone sideways fire). |

---

## 2. In-Depth System Breakdown

### 2.1 Cheats (Immortality & Infinite Ammo)
- **Code Reference:** `Source/CodexTactics/Private/Combat/CombatCheats.cpp:12–37`
- **JSON Keys:**
  ```json
  "cheats": {
    "immortal": true,
    "infinite_ammo": true
  }
  ```
- **Console Variables:**
  - `Codex.Cheat.Immortal`: Current value `-1` (resolves to Level JSON `bCheatImmortal = true`).
  - `Codex.Cheat.InfiniteAmmo`: Current value `-1` (resolves to Level JSON `bCheatInfiniteAmmo = true`).
- **Benchmark State:** `L_Demo01_A_FrozenLake.json` omits the `"cheats"` block entirely, resolving to `false`.
- **Impact on Depot:** Operatives are invincible. Ammo is never consumed from magazines or inventory reserves. High-stakes tactical positioning is completely disabled.

### 2.2 Cold, Frostbite & Temperature Simulation
- **Code Reference:** `Source/CodexTactics/Private/Survival/ColdSurvivalComponent.cpp:176–195`, `Survival/ColdRules.cpp`
- **JSON Key:**
  ```json
  "cold_rate_mult": 0
  ```
- **Benchmark State:** `L_Demo01_A_FrozenLake.json` sets `"cold_rate_mult": 0.4`.
- **Impact on Depot:** `ColdSurvivalComponent.cpp:180` directly assigns `Environment.LevelMultiplier = Level->Config.ColdRateMult;`. With a multiplier of `0`, open-air cold accumulation is completely disabled. Squad members never experience cold shivering, hypothermia debuffs, movement slowing, or frostbite damage.

### 2.3 Weather Ladder, Day/Night Cycle & Snow Shaders
- **Code Reference:** `Source/CodexTactics/Private/Environment/EnvironmentSubsystem.cpp:120–165`, `EnvironmentRules.cpp`
- **JSON Configuration:**
  - Depot completely lacks an `"environment"` block.
  - Benchmark (`L_Demo01_A_FrozenLake.json`):
    ```json
    "environment": {
      "locked": true,
      "start_hour": 23,
      "weather": "Snowfall",
      "snow_cover": 0.6
    }
    ```
- **Console Variables & Commands:**
  - `Codex.Env.Visuals` (default: `1`): Toggles sky/lighting presenter.
  - `Codex.Env.SetWeather <w> instant`: Overrides weather.
  - `Codex.Env.SetHour <h>`: Sets day hour.
  - `Codex.Env.ClockScale <scale>`: Sets time progression rate (default: `72.0` for 20-minute day cycle).
- **Impact on Depot:** Because no environment settings are supplied, `EnvironmentSubsystem` defaults to neutral fallback conditions (15:00 Clear afternoon in automated runs, or uninitialized). Dynamic weather transitions (Clear → Overcast → Snowfall → Blizzard → Whiteout) never advance naturally, and snow accumulation shaders remain at baseline.

### 2.4 Wounds, Bleeding & Knockdown
- **Code Reference:** `Source/CodexTactics/Private/Combat/HealthComponent.cpp:256–274`, `Combat/KnockdownComponent.cpp`
- **Console Variables:**
  - `Codex.Knockdown.Chance`: Default `-1.f` (50% chance on heavy blow).
  - `Codex.Knockdown.DamageThreshold`: Default `-1.f`.
  - `Codex.Knockdown.DownedSeconds`: Default `-1.f`.
- **Impact on Depot:**
  - In `HealthComponent.cpp:264`:
    ```cpp
    CurrentHealth = AbsorbByImmortalCheat(BleedingTickDamage) ? CurrentHealth : FMath::Max(0.0f, CurrentHealth - BleedingTickDamage);
    ```
    Even if an operative is afflicted with the `Bleeding` status effect from shrapnel or claw wounds, `AbsorbByImmortalCheat` absorbs 100% of the damage.
  - In `KnockdownComponent.cpp`, operatives can be knocked down by explosions or melee strikes, but they can never bleed out or die while downed.

### 2.5 Hunger & Fatigue (Food / Warmth Economy)
- **Code Reference:** `Source/CodexTactics/Private/Characters/PersonalItemRules.cpp:10–55`, `Telemetry/RunTelemetryRules.cpp`
- **Design Fact:** As documented in `docs/systems/survival-cold-heat.md`, there is no independent "hunger" or "fatigue" progress bar in the game architecture. Food items (`CannedFood`, `Bread`, `Chocolate`) serve as consumable warmth items (+25 warmth points) and healing rations (+45 HP). Fatal hypothermia is logged in telemetry under the cause `FREEZING_FATIGUE`.
- **Depot State:** `"squad_loadout"` defines no food items. Because cold accumulation is disabled (`cold_rate_mult: 0`), warming food mechanics are functionally obsolete.

### 2.6 Campfires, Gas Stoves & Tactical Heat Sources
- **Code Reference:** `Source/CodexTactics/Private/Interactables/ACampfireActor.cpp`, `APortableStoveActor.cpp`, `Survival/TacticalHeatSubsystem.cpp`
- **JSON Configuration:**
  - Lake loadout: `"matches": 3, "heat_packs_count": 1`.
  - Depot loadout: missing matches and heat packs.
- **Impact on Depot:** The procedural layout `gen_ref_01_02_depot_1.json` does not instantiate any `ACampfireActor`, `APortableStoveActor`, or `ABarrelActor` entities. Operatives cannot kindle fires, warm hands, or gather around heat points.

### 2.7 Loot Economy, Caches & Ammo Supply
- **Code Reference:** `Source/CodexTactics/Private/Interactables/LootCrateActor.cpp`, `LootRules.cpp`
- **JSON Configuration:**
  - Lake loadout:
    ```json
    "m16_ammo": 120, "pistol_ammo": 48, "medkits_count": 2,
    "turrets_count": 1, "barricades_count": 1, "mines_count": 2, "grenades_count": 2
    ```
  - Depot loadout:
    ```json
    "squad_loadout": {
      "simulation_mode": "EXPLORE_AND_COLLECT",
      "preset_tier": "STANDARD"
    }
    ```
- **Impact on Depot:** Depot provides no starting ammo or item reserves. The procedural greybox map does not spawn any `ALootCrateActor` instances. Scavenging and resource management loops are entirely absent.

### 2.8 Enemy Pressure (Combat Pressure Director)
- **Code Reference:** `Source/CodexTactics/Private/Combat/CombatPressureSubsystem.cpp:40–120`
- **JSON Key:**
  ```json
  "pressure": {
    "first_event_sec": 30,
    "interval_sec": 90,
    "group_min": 2,
    "group_max": 4,
    "max_alive": 8,
    "patrol_share": 0.3,
    "seed": 1
  }
  ```
- **Console Variables:**
  - `Codex.Pressure.Enabled`: Current `-1` (uses JSON).
  - `Codex.Pressure.IntervalSec`: Current `0.f` (uses JSON 90 s).
- **Impact on Depot:** Pressure spawns a mild, continuous trickle of 2–4 enemies every 90 seconds (capped at 8 alive). It provides continuous lab targets but lacks tension escalations, dynamic flanking waves, or adaptive difficulty.

### 2.9 Horde Escalation
- **Code Reference:** `Source/CodexTactics/Private/Combat/HordeSubsystem.cpp:26–50`
- **JSON Key:** `"horde_enabled": false`
- **Console Variables:**
  - `Codex.Horde.Enabled`: Default `-1`.
  - `Codex.Horde.TriggerSeconds`: Default `-1.f`.
  - `Codex.Horde.RepeatSeconds`: Default `-1.f`.
- **Impact on Depot:** Swarm horde arrivals are disabled. The player can remain in the greybox indefinitely without risking an overwhelming horde surge.

### 2.10 PMC Encounters
- **Code Reference:** `Source/CodexTactics/Private/AI/PmcEncounterSubsystem.cpp:330–395`
- **JSON Key:**
  ```json
  "pmc_squads": [
    { "count": 3, "weights": "pmc_commander.json", "spawn": "random", "min_m": 40, "max_m": 90, "delay_s": 30, "posture": "defend" },
    { "count": 3, "weights": "pmc_commander.json", "spawn": "random", "min_m": 50, "max_m": 90, "delay_s": 120, "posture": "hunt" }
  ]
  ```
- **Console Command:** `CodexTactics.PmcSpawn count=3 posture=hunt`
- **Impact on Depot:** While 2 squads are scheduled, the combat experience is crippled by:
  1. Squad immortality rendering incoming bullets harmless.
  2. Bug #43 (player squad ignoring PMC fire from corridor corners).
  3. Bug #44 (eliminated PMCs freezing upright in aim poses).
  4. Bug #33 (prone PMCs shooting sideways).

---

## 3. Remediation Checklist for Milestone #46 ("Depot as a Real Level")

To transform Depot from an artificial test lab into a fully authentic game level, the following configuration updates in `L_Gen_Depot_1.json` are required:

1. **Disable Cheats:**
   - Remove `"cheats": { "immortal": true, "infinite_ammo": true }`.
2. **Enable Cold Simulation:**
   - Change `"cold_rate_mult": 0` to `"cold_rate_mult": 0.4` (or `0.5`).
3. **Configure Environment & Day/Night Progression:**
   - Add `"environment"` block:
     ```json
     "environment": {
       "locked": false,
       "climate": "arctic",
       "start_hour": 22.0,
       "weather": "Overcast",
       "snow_cover": 0.4
     }
     ```
4. **Supply Authentic Squad Loadout:**
   - Populate `"squad_loadout"` with starting ammunition, rations, and survival items:
     ```json
     "squad_loadout": {
       "simulation_mode": "EXPLORE_AND_COLLECT",
       "preset_tier": "STANDARD",
       "m16_ammo": 120,
       "pistol_ammo": 48,
       "medkits_count": 2,
       "canned_food": 1,
       "matches": 3,
       "heat_packs_count": 1,
       "grenades_count": 2
     }
     ```
5. **Procedural Loot & Tactical Heat Spawners:**
   - Extend `build_levelgen_map.py` to spawn loot crates (`ALootCrateActor`), burning barrels (`ABarrelActor`), and campfires (`ACampfireActor`) in designated depot rooms and shelters.
6. **Enable Horde Director:**
   - Set `"horde_enabled": true` with a scaled timer (e.g. 300 s trigger) to enforce temporal urgency.
