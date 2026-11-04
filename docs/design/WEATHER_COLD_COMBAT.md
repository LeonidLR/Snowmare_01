# Weather & Cold vs Sight, Accuracy & Spotting: Tactical Combat Model

> **Status:** Proposal — pending the user  
> **Author:** Gemini (Advanced Agentic Assistant)  
> **Date:** 2026-11-04  
> **Governing ADRs & References:** `docs/systems/environment-weather.md`, `docs/systems/sight-and-vision-cones.md`, `docs/systems/survival-cold-heat.md`, `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp`  

---

## 1. Executive Summary & Design Vision

In harsh arctic tactical warfare, combat effectiveness is fundamentally dictated by environmental exposure. Under the user's core directions:
1. **Blizzard Sight Reduction:** In a **blizzard, sight range is halved** (the vision cone is half as long — operatives walk shielding their faces and cannot spot distant threats). *(Note: Base sight range and night/day vision cone scales are owned and implemented by Claude per `docs/systems/sight-and-vision-cones.md`; this document references those ranges and defines their tactical multipliers).*
2. **Stacked Accuracy Degradation:** Weapon accuracy drops significantly in a blizzard, and degrades further as body temperature falls into severe cold and hypothermia (**stacked penalty**). A frostbitten operative fighting inside a raging blizzard hits targets very rarely.
3. **Unified Model for Armed Humans:** PMC operatives and marksmen obey the exact same environmental sight, cold, and accuracy constraints as the player's squad.
4. **Asymmetric Monster Adaptation:** Arctic monsters adapt differently: Frostbitten and Hounds leverage non-visual senses or cold resistance, whereas projectile beasts (Spitters) suffer from ballistic wind drift.

All numerical parameters in this document are marked **`proposal - pending the user`**.

---

## 2. Environmental State Matrix: Weather x Cold Exposure

### 2.1 Weather States & Day Phases
The environment ladder follows the 4 canonical arctic states defined in `docs/systems/environment-weather.md`:
- **Clear:** Frosty, high contrast, crisp air.
- **Overcast:** Diffuse light, light wind, grey veil.
- **Snowfall:** Active falling snow, steady precipitation, reduced visibility.
- **Blizzard:** Severe gale, blowing snow, whiteout conditions, violent wind chill.

Day phases: **EarlyMorning (05–10)**, **Midday (10–14)**, **Afternoon (14–18)**, **Night (18–05)**.

### 2.2 Cold Severity Tiers
As defined by `EColdTier` in `Source/CodexTactics/Public/Survival/ColdRules.h`:
- **Tier 0: Normal** (`0% .. 39%` cold) — Operative is warm/comfortable; steady hands.
- **Tier 1: Chills** (`40% .. 69%` cold) — Shivering starts, fingers stiffen; minor aim tremor.
- **Tier 2: Freezing** (`70% .. 89%` cold) — Violent shivering, labored breathing, weapon misfire risk (from 60%).
- **Tier 3: Hypothermia** (`90% .. 99%` cold) — Numb extremities, severe motor degradation, weapon freezing risk.
- **Tier 4: Frostbite** (`100%` cold) — Operative collapses prone, takes freezing damage (`FreezeDamagePerSecond 0.66 HP/s`), extreme disorganization.

---

### 2.3 Comprehensive Multiplier Matrix

*(All values marked: proposal - pending the user)*

| Weather State | Cold Tier | Sight Range Mult | Accuracy Mult | Reaction / Spotting Time Mult |
| :--- | :--- | :---: | :---: | :---: |
| **Clear (Day)** | Normal (0–39%) | `1.00x` | `1.00x` | `1.00x` (baseline) |
| Clear (Day) | Chills (40–69%) | `1.00x` | `0.90x` | `1.10x` |
| Clear (Day) | Freezing (70–89%) | `0.95x` | `0.75x` | `1.30x` |
| Clear (Day) | Hypothermia (90–99%) | `0.85x` | `0.50x` | `1.75x` |
| Clear (Day) | Frostbite (100%) | `0.75x` | `0.25x` | `2.50x` |
| **Overcast** | Normal (0–39%) | `0.85x` | `0.95x` | `1.10x` |
| Overcast | Chills (40–69%) | `0.85x` | `0.85x` | `1.20x` |
| Overcast | Freezing (70–89%) | `0.80x` | `0.70x` | `1.45x` |
| Overcast | Hypothermia (90–99%) | `0.75x` | `0.45x` | `1.90x` |
| Overcast | Frostbite (100%) | `0.65x` | `0.20x` | `2.75x` |
| **Snowfall** | Normal (0–39%) | `0.65x` | `0.85x` | `1.25x` |
| Snowfall | Chills (40–69%) | `0.65x` | `0.75x` | `1.35x` |
| Snowfall | Freezing (70–89%) | `0.60x` | `0.60x` | `1.65x` |
| Snowfall | Hypothermia (90–99%) | `0.55x` | `0.40x` | `2.20x` |
| Snowfall | Frostbite (100%) | `0.50x` | `0.15x` | `3.00x` |
| **Blizzard** | Normal (0–39%) | **`0.50x`** | `0.65x` | `1.60x` |
| Blizzard | Chills (40–69%) | **`0.50x`** | `0.55x` | `1.80x` |
| Blizzard | Freezing (70–89%) | **`0.45x`** | `0.40x` | `2.20x` |
| Blizzard | Hypothermia (90–99%) | **`0.40x`** | `0.25x` | `2.80x` |
| **Blizzard** | **Frostbite (100%)** | **`0.35x`** | **`0.10x`** | **`3.50x`** |

> [!NOTE]
> **Night Overlay:** During the `Night` phase (18:00–05:00), baseline sight ranges receive Claude's nocturnal cut (down to ~`0.35x–0.40x` without illumination / flares). For accuracy, Night imposes a flat `0.85x` multiplier for targets outside active flare / spotlight illumination, stacking multiplicatively with the weather and cold multipliers above.

---

## 3. Integration with Weapon Accuracy & Combat Code Paths

### 3.1 Existing C++ Accuracy Pipeline
In `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp` (`AOperativeCharacter::FireWeaponAtTarget`, lines 812–855), the hit probability is computed as follows:

```cpp
// 1. Distance in discrete tactical cells (1.5 m / cell)
const float DistM = Dist / 100.0f;
const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));

// 2. Base weapon hit chance from weapons_tuning.json table
float HitChance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;

// 3. Current cold penalty (shivering hands above 50% cold)
if (ColdSurvival && ColdLevel > 50.0f)
{
    HitChance = FMath::Clamp(HitChance - ColdSurvival->GetAimPenalty(), 0.05f, 0.99f);
}

// 4. Blind fire (silhouette / cover lean)
HitChance = CoverRules::CombinedBlindFireHitChance(...);

// 5. Suppression
if (Suppressed.bBlind)
{
    HitChance *= Suppressed.HitMultiplier;
}
```

### 3.2 Proposed Environmental Accuracy Refactoring
To support the stacked weather x cold design cleanly without breaking existing mechanics:
We introduce `WeatherCombatRules::ComputeEnvironmentalAccuracyMultiplier(...)`:

```
FinalHitChance = BaseWeaponHitChance(DistCells)
               * StanceAccuracyBonus(ShooterStance)
               * WeatherAccuracyMult(WeatherState)
               * ColdAccuracyMult(ColdTier, ColdLevel)
               * TargetStanceProfile(TargetStance)
               * SuppressionMult;
FinalHitChance = Clamp(FinalHitChance, MinHitChanceFloor, 0.99f);
```

#### Stance Interactions with Weather & Wind:
- **Standing:** Full wind exposure; penalty multiplier `1.00x` in normal weather, but in Blizzard receives wind buffet sway (`0.85x`).
- **Crouching:** Better stability against gusts; in Blizzard receives `0.95x` sway factor (+`CrouchAccuracyBonus` 1.10x).
- **Prone:** Full ground bracing; completely ignores wind buffet sway (+`ProneAccuracyBonus` 1.25x). However, lying prone on freezing snow increases cold accumulation rate unless insulated or in a cleared zone (`StanceMultiplierProne 0.6x` in `ColdRules.h`).
- **Cover Shelter:** An operative hugging low or high cover (`bInCover`) has wind chill reduced by 50% (`CoverWindChillMultiplier 0.5f`), preserving arm temperature and reducing shivering.

---

## 4. Interaction with Height Sight Bonus & Vision Cones

### 4.1 Height Sight Bonus Model
From `docs/systems/sight-and-vision-cones.md` ("Sight from a height: the roof sniper, the range ladder"):
```
level = floor(eye height above ground below / 3.0 m)
HeightSightMultiplier = FMath::Clamp(1.0f + 0.25f * level, 1.0f, 2.0f)
```
- Level 0 (Ground / Cut): `1.00x`
- Level 1 (3 m / Low roof): `1.25x`
- Level 2 (6 m / Upper floor): `1.50x`
- Level 3 (9 m / Depot roof): `1.75x`
- Level 4+ (12 m+ / Tower): `2.00x` (hard cap)

### 4.2 Combining Height with Blizzard & Fog: The "Atmospheric Ceiling" Rule
In clear weather, elevated snipers enjoy up to 2x sight distance (e.g. 60 m -> 120 m). However, in dense precipitation, physical light attenuation creates an **absolute visibility distance cap**:

1. **Multiplicative Base:**
   `SightRange = BaseSightRange * HeightSightMultiplier * WeatherSightMultiplier;`
2. **Weather Distance Clamps (Atmospheric Density Clamping):**
   Even from a 12 m tower (`2.0x` height bonus), horizontal vision through dense blizzard snow cannot exceed physical particle saturation:
   - **Clear Day:** No atmospheric cap (up to `120 m`).
   - **Overcast:** Capped at `75 m`.
   - **Snowfall:** Capped at `45 m`.
   - **Blizzard:** Capped at **`30 m`** (hard whiteout ceiling).
3. **Tactical Consequence:**
   On a depot roof in a blizzard, an operative sees down into the immediate yard below (`25–30 m`), but cannot spot across the entire 80 m complex. Climbing high in a blizzard grants superior look-down angles over walls, but does not pierce the whiteout.

---

## 5. Enemy Architecture: Armed Humans vs Monsters

### 5.1 Armed Humans (PMC Operators, Snipers / Marksmen)
**Rule:** Strictly unified physical model.
- **PMC Operatives (`UPmcSquadSubsystem`, `PmcOperatorRules.cpp`):**
  - Experience the exact same vision cone halving (`0.50x`) in a blizzard.
  - Suffer identical accuracy penalties from gale winds and cold shivering.
  - Use tactical cover to shield from wind and preserve weapon handling.
- **Marksmen (`MarksmanEnemyCharacter.cpp`, `MarksmanAIRules.cpp`):**
  - In `MarksmanAIRules::ComputeSniperHitChance`:
    `HitChance = BaseAccuracy * ShooterStanceBonus * TargetStanceBonus * CoverMult * RangeMult * WeatherAccuracyMult;`
  - In a blizzard, marksmen lose long-range lock-on ability (their preferred 45 m engagement range is blinded by the 30 m blizzard cap), forcing them into secondary carbine behavior or retreating to indoor perimeters.

### 5.2 Monsters (Asymmetric Bio-Adaptations)
Monsters do not hold metallic rifles and have adapted to the anomalous permafrost:

| Monster Type | Cold Degradation | Blizzard Sight Impact | Special Weather Behavior (Proposal) |
| :--- | :--- | :--- | :--- |
| **Frostbitten** | **Immune** to cold penalty | Vision cone halved (`0.50x`) | Relies on ambient chill; in blizzard moves with uninhibited ferocity; melee attack accuracy `1.00x`. |
| **Frost Hound** | **Immune** to cold penalty | Vision cone halved (`0.50x`), but **Scent Range Unimpaired** | Smell/hearing radius remains 15–20 m; hounds stalk around the squad's blind flanks in whiteouts. |
| **Spitter** | Suffers minor freeze | Vision cone halved (`0.50x`) | **Ballistic Drift:** Acid projectile trajectory buffeted by blizzard winds: `-25%` projectile speed, `-35%` accuracy. |
| **Brute** | **Immune** to cold penalty | Vision cone reduced (`0.60x`) | Thick hide and massive weight ignore wind buffet; devastating close-quarters ramming during low-visibility ambushes. |
| **Cutter** | **Immune** to cold penalty | Vision cone halved (`0.50x`) | Stealth hunter; footsteps masked by roaring blizzard audio; ambush threat score elevated. |

---

## 6. Balance Sanity Check: Concrete Hit Chance Calculations

To verify that the model produces authentic, tension-inducing gameplay without unplayable deadlocks, we calculate representative hit chances across standard combat ranges:
- **Weapon A: M16 Assault Rifle** (Operative Sally, base accuracy 80%, mid-range falloff).
- **Weapon B: Sniper Rifle** (Operative Roger / Marksman, high base accuracy 90%).
- **Weapon C: Shotgun** (Close-range spread weapon, base 85% at 5 m, sharp falloff).

### Scenario 1: Engagement at 20 Metres (Medium Combat Distance)

| Condition | Shooter Stance | M16 Hit Chance | Sniper Hit Chance | Shotgun Hit Chance | Combat Feel |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Warm + Clear (Day)** | Standing | **`76%`** | **`88%`** | **`25%`** | Standard clean firefight. Good marksmanship rewarded. |
| **Warm + Clear (Day)** | Crouching | **`84%`** | **`95%`** | **`28%`** | Optimal tactical defensive baseline. |
| **Warm + Blizzard** | Standing | **`49%`** | **`57%`** | **`16%`** | Wind buffeting and swirling snow cause frequent misses. |
| **Warm + Blizzard** | Crouching | **`55%`** | **`64%`** | **`18%`** | Taking cover/crouching noticeably stabilizes aim. |
| **Chilled (50%) + Snowfall** | Standing | **`48%`** | **`56%`** | **`16%`** | Moderate shivering combined with steady snow. |
| **Freezing (75%) + Blizzard** | Standing | **`30%`** | **`35%`** | **`10%`** | Heavy shivering + violent gale. Operatives struggle to hit. |
| **Frostbitten (100%) + Blizzard** | Prone (Collapsed) | **`11%`** | **`14%`** | **`8%`** | **Extreme desperation.** Almost all shots miss; emergency heat needed. |

### Scenario 2: Engagement at 40 Metres (Long Distance / Sniper Duel)

| Condition | Shooter Stance | M16 Hit Chance | Sniper Hit Chance | Combat Feel |
| :--- | :--- | :---: | :---: | :--- |
| **Warm + Clear (Day)** | Standing | **`52%`** | **`82%`** | Disciplined sniper territory. Rifles maintain effective fire. |
| **Warm + Clear (Day)** | Prone | **`65%`** | **`95%`** | Prone sniper has near-guaranteed hit. |
| **Warm + Blizzard** | Standing | **`33%`** | **`53%`** | Severe drop. Target barely distinguishable through snow. |
| **Chilled (50%) + Blizzard** | Crouching | **`26%`** | **`42%`** | Long-range fire becomes heavily inefficient; ammo wasting. |
| **Freezing (75%) + Blizzard** | Prone | **`21%`** | **`34%`** | Even prone, violent body shivering throws off scope crosshairs. |
| **Frostbitten (100%) + Blizzard** | Any | **`< 5%` (Floor)** | **`< 8%`** | **Near-zero probability.** Target is a ghostly silhouette in whiteout. |

---

## 7. Data Layout Specification

The environmental combat parameters are integrated into existing configuration assets in `Content/Data/`:

### 7.1 Proposed Schema: `Content/Data/Environment/weather_combat.json`
*(or as a dedicated object inside `Content/Data/Environment/environment.json`)*

```json
{
  "$schema": "../Schemas/weather_combat.schema.json",
  "weather_combat_tuning": {
    "weather_modifiers": {
      "Clear": {
        "sight_range_multiplier": 1.0,
        "accuracy_multiplier": 1.0,
        "spotting_delay_multiplier": 1.0,
        "max_sight_distance_clamp_m": 120.0,
        "wind_sway_standing": 1.0,
        "wind_sway_crouching": 1.0
      },
      "Overcast": {
        "sight_range_multiplier": 0.85,
        "accuracy_multiplier": 0.95,
        "spotting_delay_multiplier": 1.10,
        "max_sight_distance_clamp_m": 75.0,
        "wind_sway_standing": 0.98,
        "wind_sway_crouching": 1.0
      },
      "Snowfall": {
        "sight_range_multiplier": 0.65,
        "accuracy_multiplier": 0.85,
        "spotting_delay_multiplier": 1.25,
        "max_sight_distance_clamp_m": 45.0,
        "wind_sway_standing": 0.92,
        "wind_sway_crouching": 0.97
      },
      "Blizzard": {
        "sight_range_multiplier": 0.50,
        "accuracy_multiplier": 0.65,
        "spotting_delay_multiplier": 1.60,
        "max_sight_distance_clamp_m": 30.0,
        "wind_sway_standing": 0.85,
        "wind_sway_crouching": 0.95
      }
    },
    "cold_tier_modifiers": {
      "Normal": { "accuracy_multiplier": 1.00, "spotting_delay_multiplier": 1.00 },
      "Chills": { "accuracy_multiplier": 0.90, "spotting_delay_multiplier": 1.10 },
      "Freezing": { "accuracy_multiplier": 0.75, "spotting_delay_multiplier": 1.30 },
      "Hypothermia": { "accuracy_multiplier": 0.50, "spotting_delay_multiplier": 1.75 },
      "Frostbite": { "accuracy_multiplier": 0.25, "spotting_delay_multiplier": 2.50 }
    },
    "night_overlay": {
      "accuracy_multiplier_unlit": 0.85,
      "flare_lit_accuracy_multiplier": 1.00
    },
    "min_hit_chance_floor": 0.05
  }
}
```

---

## 8. Verification Strategy: Unit Tests & Automated Smokes

### 8.1 C++ Unit Tests (`Source/CodexTacticsTests/Private/Combat/WeatherCombatTests.cpp`)
- `CodexTactics.Combat.Weather.AccuracyMultipliers`: Verifies that `WeatherCombatRules::GetWeatherAccuracyMultiplier` returns exact table values for all 4 weather states.
- `CodexTactics.Combat.Weather.StackedColdAndBlizzard`: Asserts that `Frostbite + Blizzard` compounds multiplicatively down to the expected 10% effective multiplier (`0.65 * 0.25 = 0.1625 -> clamped`).
- `CodexTactics.Combat.Weather.AtmosphericClamp`: Asserts that a Level 4 tower sniper (2.0x bonus) in a Blizzard is strictly clamped to the 30 m atmospheric ceiling.
- `CodexTactics.Combat.Weather.MonsterExemption`: Verifies that `AFrostbittenEnemy` queries report cold immunity and full melee accuracy under Blizzard conditions.

### 8.2 In-Engine Smoke Tests
- **`WeatherCombatSmoke` (in `Scripts/test_map.json`):**
  - Spawns an operative at 20 m and 40 m from a target dummy.
  - Test Phase 1 (Clear, Warm): Confirms hit rate matches baseline weapon table (>75%).
  - Test Phase 2 (Blizzard, Warm): Confirms hit rate drops to ~45–50%.
  - Test Phase 3 (Blizzard, 100% Frostbitten): Verifies that 10 consecutive shots result in misses or near-total failure (<15% hit rate) with "MISS!" floating feedback text.
  - Test Phase 4 (Deploy Heat Pack): Restores operative temperature; confirms aim accuracy recovers within 1.5 seconds.
