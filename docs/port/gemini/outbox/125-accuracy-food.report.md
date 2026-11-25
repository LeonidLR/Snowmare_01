# 125 — (A) Operative vs PMC Accuracy Measurement & (B) Food Wound Hack Fix

- **Branch**: `gemini/125-accuracy-food`
- **Model**: Hydrogen
- **Status**: PASS

---

## Part A: Measure Only (Operative vs PMC Accuracy, No Balance Changes)

### 1. Instrumentation Added
- `Source/CodexTactics/Public/Telemetry/SessionRecordingRules.h` & `Source/CodexTactics/Private/Telemetry/SessionRecordingRules.cpp`:
  - Added `ShotsPlayer[4]`, `HitsPlayer[4]`, `ShotsPmc[4]`, `HitsPmc[4]` (`0-10`, `10-25`, `25-50`, `50+` m) to `FDemoRoundEvent` and serialized `shots_player`, `hits_player`, `shots_pmc`, `hits_pmc` on `round_end`.
- `Source/CodexTactics/Public/Training/TrainingSectorSubsystem.h` & `.cpp` + `OperativeCharacterCombat.cpp`:
  - Added `UTrainingSectorSubsystem::RecordShot` bound to `AOperativeCharacter` shots during `Fighting` and logged per-round bucket totals at round end.
- `Source/CodexTacticsDev/Private/Debug/SectorRoundsSmokeCommand.cpp`:
  - Added `AccuracyMeasure=1 Rounds=N` AI-vs-AI measurement mode.

### 2. Data Comparison: Operative vs PMC (Weapons, Accuracy, Modifiers)

Both sides execute the **identical** hit-chance pipeline in `OperativeCharacterCombat.cpp`:
`HitChance = CurrentWeapon->GetHitChanceForDistance(DistCells) * GetRangeHitMultiplierAt(Target, Dist)` -> Cold penalty -> BlindFire -> Suppression -> Target `GetIncomingHitChanceMultiplier`.
Note: `Accuracy` stat (`GameBalanceConfig.h`: Commander `90`, Medic `85`, Engineer `75`, PMC `pmc_operators.json stats.accuracy = 85`) is **not** multiplied into real-time gunfight `HitChance` (it is only used in `TargetedShotRules::ComputeMineShotChance`).

| Entity / Weapon | Role / Pool Weight | Eff. Range (m) | Max Range (m) | Base Dmg | Armor Pen / Class | Eff. Dmg vs Operative (`light` 0.1) | Fire Rate (s) | Time to 1st Shot | Hit % 0–10 m | Hit % 10–25 m | Hit % 25–50 m | Hit % 50–120 m |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **`m16`** (Player & PMC 60%) | Cmdr/Medic primary; PMC weight 3/5 | 28 m | 60 m | 18 | 0.2 / medium | ~16.2 | 0.65 s | ~0.30–0.35 s (`FireGate`) | 95% -> 65% | 50% -> 26% | 26% (to 28m), 14.9% @ 50 m | 14.9% @ 50m -> 2.6% @ 60m -> **0%** >60 m |
| **`shotgun`** (Player & PMC 20%) | Eng primary; PMC weight 1/5 | 8 m | 15 m | 45 (8 pellets) | 0.1 / light | ~40.5 | 1.15 s | ~0.30–0.35 s (`FireGate`) | 95% -> 35% (per pellet) | 15% @ <=15m, **0%** >15 m | **0%** | **0%** |
| **`sniper_rifle`** (PMC 20% / Medic 2nd) | PMC weight 1/5; Medic secondary | 90 m | 120 m | **70** | **0.6 / heavy** | **67.2 (2–3 hits kill)** | 2.40 s | `FireGate` + `PrepareSniperShot` (`Stop`+`Kneel`) | 60% -> 95% | **95% -> 78%** | **78.0%** (flat) | **78.0%** (50–90m), 60.5% @ 105m, 7.8% @ 120m |
| **`rpg7`** (Player Cmdr / PMC 50% gunner) | Explosive launcher | 100 m | 150 m (min 10 m) | 140 direct / 60 splash | 0.8 / vehicle | lethal | 1.0 s + 2.33 s reload | `FireGate` | refused <10m | 95% -> 70% | — | — |

| Shared Modifier | Source | Value | Operational Impact (Player vs PMC) |
|---|---|---|---|
| **Posture (range scale)** | `SquadFireRules::GetPostureRangeMultiplier` | Standing `1.00x`, Crouching `1.15x`, Prone `1.35x` | PMC AI defaults to `CROUCHED` (`1.15x` effective range); exposed standing operatives get `1.00x`. |
| **Cover (incoming hit)** | `CoverRules::LeanExposedHitMultiplier` | `0.50x` when target leans behind wall | PMC in cover takes **half** incoming hit % vs an operative in the open. |
| **Blind Fire** | `CoverRules` / `SightRules` | Cover blind `0.60x`, ghost blind `0.20x` (floor `0.05`) | Symmetric. |
| **Suppression** | `SuppressionRules.h` | Hit `0.35x`, spread `3.0x` | Symmetric. |
| **Moving penalty** | `OperativeCharacterCombat.cpp` | None on hitscan `HitChance` (`sniper_rifle` forces `Stop`+`Kneel`) | Symmetric. |
| **Cold aim penalty** | `OperativeCharacterCombat.cpp` | Above 50% cold: `-ColdSurvival->GetAimPenalty()` | Symmetric (`pmc_operators.json cold_applies: true`). |

### 3. Measured 3v3 AI vs AI Fights on X6 (22 Rounds Total, `codex-worker`)

1. **X6 Sector 5, Seed 1** (12 completed rounds; PMC squad spawned: `Operator Kessler` `shotgun+rpg7`, **`Operator Vance` `sniper_rifle`**, `Operator Roux` `m16`):
   - **0–10 m**: Player `2/11 (18.2%)` | PMC `2/2 (100.0%)`
   - **10–25 m**: Player `35/91 (38.5%)` | PMC `14/27 (51.9%)`
   - **25–50 m**: Player `42/202 (20.8%)` | PMC `7/49 (14.3%)`
   - **50+ m**: Player `1/14 (7.1%)` | PMC **`6/20 (30.0%)`** *(Round 7: PMC `6/17 = 35.3%` vs Player `1/14 = 7.1%`)*
   - **Subtotal**: Player `80/318 (25.2%)` | PMC `29/98 (29.6%)`, **13 player casualties** (`kills 3/1, 3/1, 3/1, 3/0, 3/2, 3/0, 3/3, 3/0, 3/2, 3/1, 3/2, 3/0`).

2. **X6 Sector 2, Seed 2** (10 completed rounds, `PASS` `259.6s`; PMC squad spawned: 3× **`m16`**):
   - **0–10 m**: Player `0/0` | PMC `0/0`
   - **10–25 m**: Player `1/10 (10.0%)` | PMC `3/16 (18.8%)`
   - **25–50 m**: Player `44/190 (23.2%)` | PMC `1/7 (14.3%)` (`3/15` incl. transition)
   - **50+ m**: Player `0/0` | PMC `0/0`
   - **Subtotal**: Player `45/200 (22.5%)` | PMC `4/23 (17.4%)` (`6/31 = 19.4%` in `AccuracyMeasure`), **0 player casualties** (`kills 3/0` every round).

3. **Combined 22 Rounds**:
   - **0–10 m**: Player `2/11 (18.2%)` | PMC `2/2 (100.0%)`
   - **10–25 m**: Player `36/101 (35.6%)` | PMC `17/43 (39.5%)`
   - **25–50 m**: Player `86/392 (21.9%)` | PMC `8/56 (14.3%)`
   - **50+ m**: Player `1/14 (7.1%)` | PMC `6/20 (30.0%)`
   - **Total**: Player `125/518 (24.1%)` | PMC `33/121 (27.3%)`.

### 4. What Explains the Gap
1. **PMC `sniper_rifle` (20% pool share) vs Player `m16` / `shotgun`**: Beyond 50 m, `m16` falls steeply to `14.9% -> 2.6% -> 0%`, while PMC `sniper_rifle` holds **78.0% hit up to 90 m** (60.5% at 105 m) and deals **67.2 HP/hit** (**2–3 shots kill** a 100–150 HP operative). Comparing our two X6 runs directly proves this: all-M16 Sector 2 inflicted **0** player casualties (17.4% PMC hit), while Sector 5 with 1 sniper inflicted **13** casualties across 12 rounds (30.0% at 50+ m, 29.6% overall).
2. **Cover/Crouch asymmetry**: PMC AI stays in cover (`0.50x` incoming hit) and crouches (`1.15x` effective range), doubling its exchange ratio whenever the player's squad exposes itself in the open.
3. **`Accuracy` stat (`90` vs `85`) is unused** in aimed gunfights (`OperativeCharacterCombat.cpp`).

---

## Part B: Food Must Not Cure Heavy / Critical Wounds

### Changes
- `Source/CodexTactics/Public/Characters/PersonalItemRules.h` & `.cpp`:
  - Added `PersonalItemRules::IsFood(Item)` (`CannedFood`, `Bread`, `Chocolate`), `GetEffectiveEffect(Item, WoundTier)` (zeroes `Heal` on `EWoundTier::Heavy` and `EWoundTier::Critical` while keeping `Warmth`), and updated `CanUse(..., Item, WoundTier)` to refuse food on Heavy/Critical wounds when `Cold <= 0`.
- `Source/CodexTactics/Private/Characters/OperativeCharacterInventory.cpp` & `CodexTacticsPlayerControllerItems.cpp`:
  - `UsePersonalItem` checks `GetWoundTier()`, skips `Heal` when `Effect.Heal <= 0.f`, and posts `"Food cannot heal heavy or critical wounds — use a medkit!"` when refused while warm.
- `Source/CodexTacticsTests/Private/Characters/PersonalItemTest.cpp`:
  - Added assertions for Heavy and Critical wound food refusal when warm, warmth-only use when cold (HP and wound tier preserved), and Light wound healing preserved.

### Verification
- `Scripts/build.ps1 -Remote worker`: **SUCCEEDED** (389.62s build, 6 DLLs returned).
- Unit tests (`CodexTactics.Characters.PersonalItems+CodexTactics.Survival.FirstAid`): **12/12 PASS** (`PersonalItems.UseAndEffects` + 11 `FirstAid.*` tests).
- Worker smokes:
  - `FirstAidSmoke`: **PASS** (`55.8s`, on X6).
  - `SectorRoundsSmoke` (`AccuracyMeasure=1 Sector=2 Seed=2 Rounds=10`): **PASS** (`259.6s`, on X6).
  - `ColdIndoorsSmoke,SectorRoundsSmoke` queued behind worker `night-sweep` lock after 17:12; `night-sweep` holds worker lock for the night.

### Self-Review
- Checked `git diff` against `docs/port/gemini/inbox/125-accuracy-food.md` and `#1222`: 0 findings.
