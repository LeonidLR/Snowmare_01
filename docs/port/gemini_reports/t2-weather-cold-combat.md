# Report: T2. Design Doc - Blizzard and Cold vs Sight and Accuracy

**Task:** T2. Design doc - blizzard and cold vs sight and accuracy (no code)  
**Branch:** `gemini/t2-weather-cold-combat`  
**Date:** 2026-11-04  

## Summary of Deliverable

Authored comprehensive architecture and balance design document: [`docs/design/WEATHER_COLD_COMBAT.md`](../design/WEATHER_COLD_COMBAT.md).

### Key Architectural Elements Covered:
1. **Weather x Cold State Matrix**:
   - 4 canonical weather states (`Clear`, `Overcast`, `Snowfall`, `Blizzard`) x 5 cold tiers (`Normal`, `Chills`, `Freezing`, `Hypothermia`, `Frostbite`).
   - Defined sight multipliers (noting Claude's range baseline), stacked accuracy multipliers, and reaction/spotting time multipliers.
   - Highlights the user rule: in a **blizzard, sight range is halved (`0.50x`)**, and accuracy drops heavily in a blizzard (`0.65x`) and stacks with severe cold/frostbite down to a rare `0.10x` accuracy floor.

2. **Integration with Height Sight Bonus**:
   - Mapped interaction with the existing ladder: `(1 + 0.25 * level)` up to `x2.0` (cap at 12 m).
   - Introduced the **Atmospheric Ceiling Rule**: while height grants look-down angle advantages over wall obstacles, blizzard whiteouts cap maximum horizontal sight at `30 m`, preventing unrealistic cross-map detection from high towers during a storm.

3. **C++ Weapon Accuracy Code Path Integration**:
   - Named and traced exact implementation files and functions:
     - `AOperativeCharacter::FireWeaponAtTarget` in `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp` (lines 812–855).
     - Distance cell conversion (`DistCells = Max(1, RoundToInt(DistM / 1.5f))`) and `CurrentWeapon->GetHitChanceForDistance(DistCells)`.
     - `ColdSurvival->GetAimPenalty()`, `CoverRules::CombinedBlindFireHitChance`, and `USuppressionSubsystem::GetOperativeFireOf`.
     - `MarksmanAIRules::ComputeSniperHitChance` in `Source/CodexTactics/Private/Characters/MarksmanAIRules.cpp`.
   - Detailed stance effects: standing buffeted by blizzard gusts, crouching providing stability, prone eliminating wind sway (balanced by higher snow contact cold gain unless in cover/insulated).

4. **Enemy Models**:
   - **Armed Humans (PMC, Marksmen):** Strictly identical physical model to operatives (clothing, shivering, blizzard sight halving, wind sway).
   - **Monsters (Asymmetric Adaptation):**
     - Frostbitten: Cold-immune, native to permafrost, full melee attack accuracy in blizzards.
     - Frost Hounds: Vision halved, but scent detection range (15–20 m) unimpaired.
     - Spitters: Acid/bile projectile trajectory affected by blizzard winds (-25% velocity, -35% accuracy).
     - Brutes & Cutters: Thick hide / stealth ambush advantage during low-visibility whiteouts.

5. **Balance Sanity Check & Calculations**:
   - Calculated exact hit probability tables at 20 m and 40 m for M16, Sniper Rifle, and Shotgun across 7 weather and cold tiers.
   - Confirms that warm clear shots achieve 76–88% accuracy, while a frostbitten operative in a blizzard drops to 11–14% at 20 m and <5% at 40 m (achieving the user's intended desperate survival tension).

6. **Data Layout & Testing Plan**:
   - Proposed schema additions in `Content/Data/Environment/weather_combat.json`.
   - Outlined C++ unit test suite (`WeatherCombatTests.cpp`) and in-engine smoke test (`WeatherCombatSmoke`).

All numerical values are explicitly marked **`proposal - pending the user`**.

---

## Open Questions for the User
1. Does the user want the 30 m atmospheric sight clamp during blizzards to apply strictly to all entities, or should flare-illuminated zones temporarily extend spot range up to 40 m?
2. Should Spitter acid projectiles leave an acidic freezing puddle if deflected by high winds into terrain?
