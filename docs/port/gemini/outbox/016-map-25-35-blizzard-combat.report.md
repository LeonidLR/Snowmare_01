# Implementation Map #25 + #35 — Blizzard: Combat Accuracy & Formation Break-Up

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Tasks #25 (`pl-blizzard-acc`) and #35 (`pl-blizzard-break`)  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/design/WEATHER_COLD_COMBAT.md`, `docs/systems/environment-weather.md`, `docs/systems/survival-cold-heat.md`, `docs/port/TASK_BOARD.md` §§25, 35

---

## 1. Tactical Concept & Operational Goal

Under arctic combat conditions, weather and cold interact to severely degrade both marksmanship and unit cohesion:
1. **Task #25 (Accuracy Degradation):**
   - Sight range is halved in a blizzard ($0.5\times$, clamped to a hard ceiling of $30\text{ m}$).
   - Accuracy suffers stacked multiplicative penalties: Blizzard weather ($0.65\times$) $\times$ Hypothermia ($0.50\times$) results in $0.325\times$ base hit chance. At 100% Frostbite, hit chance collapses to the $5\%$ floor.
   - Applies symmetrically to both player operatives and armed PMC forces.
2. **Task #35 (Formation Break-Up & Blizzard Diamond):**
   - In dense whiteouts, squad members who lose sight of the leader ($> 15\text{ m}$ in spread formations like Line or Loose Wedge) lose follower tether and enter a disoriented state (lagging behind, breaking formation).
   - Only the dense **Blizzard Diamond** formation ($\le 3.0\text{ m}$ spacing) preserves mutual visual contact, preventing squad fragmentation.

---

## 2. Existing Classes & Functions to Extend vs. New Code

### 2.1. Reusable Engine Components (~85% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Weapon Accuracy Rules** | `Source/CodexTactics/Private/Data/WeaponStatsRules.cpp:119-200` | `HitMultiplier` evaluates distance, cover, and status effects. |
| **Hit Probability Calculation** | `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:450-510` | `CalculateHitProbability` rolls hit/miss against target profiles. |
| **Cold & Freezing State** | `Source/CodexTactics/Public/Survival/ColdRules.h:20-60` | `EColdTier` (Normal, Chills, Freezing, Hypothermia, Frostbite) and `FreezeDamagePerSecond`. |
| **Sight & Vision Cones** | `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp:894-910` | `DetectionRangeScale` and cone rendering. |
| **Formation Follower Logic** | `Source/CodexTactics/Private/Formation/FormationFollowerRules.cpp:45-120` | `ComputeFollowerTarget` evaluates follower positions behind formation leader. |
| **Weather Presenter** | `Source/CodexTactics/Private/Environment/WeatherPresenterActor.cpp:317-420` | Current weather state queries (`EWeatherPreset::Blizzard`). |

### 2.2. New Code Required (~15%)

1. **`WeatherCombatRules` (Pure Multiplier Solver):**
   - Files: `Source/CodexTactics/Public/Combat/WeatherCombatRules.h`, `Private/Combat/WeatherCombatRules.cpp`.
   - Pure function: `float GetEffectiveAccuracyMultiplier(EWeatherPreset Weather, EColdTier Cold, bool bIsMonster)`.
2. **Formation Visual Tether in `FormationFollowerRules`:**
   - Adds `bLostInBlizzard` condition when follower-to-leader distance exceeds `BlizzardTetherDistanceCm` ($1500\text{ cm}$).
3. **`Blizzard Diamond` Formation Preset:**
   - Added to `Content/Data/Formation/formation_shapes.json` with dense radial offsets ($R \le 280\text{ cm}$).

---

## 3. Data Keys & Tuning

Configured in `Content/Data/Environment/weather_combat.json`:

```json
"weather_combat": {
  "accuracy_multipliers": {
    "Clear": 1.0,
    "Overcast": 0.95,
    "Snowfall": 0.85,
    "Blizzard": 0.65
  },
  "cold_tier_multipliers": {
    "Normal": 1.0,
    "Chills": 0.90,
    "Freezing": 0.75,
    "Hypothermia": 0.50,
    "Frostbite": 0.25
  },
  "blizzard_sight_clamp_cm": 3000.0,
  "min_hit_chance_floor": 0.05,
  "blizzard_tether_distance_cm": 1500.0,
  "blizzard_diamond_spacing_cm": 280.0
}
```

---

## 4. Insertion Points in Existing Code

1. **Accuracy Evaluation Hook:**
   - In `OperativeCharacterCombat.cpp::CalculateHitProbability()`:
     ```cpp
     const float WeatherMult = WeatherCombatRules::GetWeatherAccuracyMultiplier(CurrentWeather);
     const float ColdMult = WeatherCombatRules::GetColdAccuracyMultiplier(CurrentColdTier);
     HitChance = FMath::Max(Config.MinHitFloor, HitChance * WeatherMult * ColdMult);
     ```
2. **Sight Range Clamping:**
   - In `TacticalSightSubsystem.cpp::GetOperativeSightRange()`:
     ```cpp
     if (Weather == EWeatherPreset::Blizzard)
     {
         EffectiveRange = FMath::Min(EffectiveRange * 0.5f, 3000.f);
     }
     ```
3. **Formation Cohesion Break:**
   - In `FormationFollowerRules.cpp::ComputeFollowerTarget()`:
     ```cpp
     if (Weather == EWeatherPreset::Blizzard && DistanceToLeader > 1500.f && Formation != EFormationType::BlizzardDiamond)
     {
         State.bLostVisualContact = true;
         // Follower reduces pace, drifts slightly along wind vector
     }
     ```

---

## 5. Automated Verification & Smokes

* **Unit Tests:** `Source/CodexTacticsTests/Private/Combat/WeatherCombatTest.cpp`
  - Validates multiplicative compounding of weather and cold tiers.
  - Verifies 5% minimum hit floor clamp.
* **Smoke Command 1 (Accuracy):** `Codex.Test.WeatherCombatAccuracy`
  - Verifies M16 hit rates across Clear vs. Blizzard at 20 m and 40 m distances.
* **Smoke Command 2 (Formation Break):** `Codex.Test.BlizzardFormationBreak`
  - Squad navigates across Depot in Line formation during Blizzard: outer followers drop behind and trigger "Lost visual!" callout.
  - Squad switches to Blizzard Diamond: maintains tight cohesion with 0 stragglers.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **Вопрос 1: Минимальный шанс попадания (Hit Floor)**
   - *Варианты:* Возможен ли полный 0% шанс, или гарантирован 5% шанс шальной пули?
   - *Рекомендуемый дефолт (Recommended):* **Гарантированный пол 5%**. В тактических играх (XCOM, Jagged Alliance) полный ноль на близкой дистанции воспринимается как баг; 5% оставляет шанс на отчаянный критический выстрел.
2. **Вопрос 2: Восстановление строя после потери бойца в буране (Regroup Behavior)**
   - *Варианты:* Боец ищет ведущего сам, или стоит на месте и ждет команды?
   - *Рекомендуемый дефолт (Recommended):* **Авто-сближение при сокращении дистанции до $\le 6\text{ м}$ или по свистку командира [B]**. Отставший боец замедляется до шага и кричит по рации. При прожатии клавиши сбора [B] отставший ускоряется навстречу отряду.
3. **Вопрос 3: Иммунитет арктических мутантов**
   - *Варианты:* Подвержены ли Hounds и Frostbitten штрафам метели?
   - *Рекомендуемый дефолт (Recommended):* **Ближнебойные монстры полностью иммунны к метели и холоду** (сражаются на родной территории); дальнобойные монстры (Спиттер) получают штраф только на снос плевка ветром ($0.85\times$).
