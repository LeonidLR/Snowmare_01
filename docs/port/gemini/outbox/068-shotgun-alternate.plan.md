# Plan 068: Shotgun Alternate Walk & Foot Drift Mitigation

- **Task:** 068 (`weapon-anim-layer`)
- **Governing Spec:** `docs/port/gemini/inbox/068-shotgun-alternate-walk-feet.md` (Designer 2026-11-15, Claude WIP)
- **Base Commit:** `origin/wip/2026-11-15-pause` (`526aad44`)
- **Branch:** `gemini/068-shotgun-walk`
- **Worktree:** `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-cxnight`
- **Gate / Review:** `Scripts/Tools/gemini_gate.ps1 -Task 068 -SonnetReview`

---

## 1. Requirements & Intent

1. **Two Shotgunners Alternate Walk (Not Twins):**
   - Designer decision (2026-11-15): Two operatives carrying the shotgun must not walk identically.
   - Rank 0 (lower squad index among shotgun carriers, or a lone carrier) walks on the **M16 ABP legs** (`weapon_anim_sets.json` `legs_abp_walk: "alternate"`, `bShotgunWalkOnAbpLegs = true`).
   - Rank 1 (higher squad index among shotgun carriers) walks on the **shotgun pack walk** (`bShotgunWalkOnAbpLegs = false`, full-body set from `/Game/ShotgunLocomotionPack/Animations/Loop_Root/`).
   - Unit test `CodexTactics.Animation.WeaponLayer.AlternateWalk` currently tests the rank formula (`AlternateWalkUsesAbpLegs`).

2. **No Foot Sliding (< 35% limit):**
   - Neither carrier may have planted foot drift exceeding **35%** of body ground speed (do NOT raise the limit).
   - The pack legs typically drift < 20%; the ABP legs reference band is < 35%.

3. **Sprint Animates Legs for Both Ranks:**
   - Both carriers sprint on ABP legs (`legs_abp: ["run", "sprint"]`).
   - Fix the false failure where sprint lower-body deviation is reported as `0.00` ("legs frozen").

4. **Update `ShotgunSmoke` Check 2b:**
   - Must validate **BOTH** carriers:
     - Carrier 0 (e.g. Engineer, rank 0): walks on ABP legs (`bAbpLegs = true`, `MoveMaxLower < 0.01f`, slide < 35%).
     - Carrier 1 (e.g. spawned PMC or teammate with shotgun, rank 1): walks on pack legs (`bAbpLegs = false`, `MoveMaxLower > 1.f`, slide < 35%).
   - Both carriers sprint on ABP legs without foot sliding exceeding 35%.

---

## 2. Root Cause Diagnosis & Evidence

### 2.1 Sprint "Lower Body Max Change 0.00" (Legs Frozen False Positive)
- **Observed:**
  `Smoke FAIL: 2b: sprinting at up to 725 cm/s over 20 frames: the lower body follows the pack (full-body set) (max change 0.00), the upper body is the weapon hold (max change 98.1; mask 1, upper 1.00, lower 1.00)`
- **Mechanism:**
  - `Content/Data/Weapons/weapon_anim_sets.json` specifies `"legs_abp": ["run", "sprint"]`.
  - When sprinting (`bIsSprinting = true`), `LegsAbpWeight` evaluates to `1.0`.
  - In `FOperativeAnimInstanceProxy::BlendWeaponLayer` (`OperativeAnimInstanceShotgun.cpp:1085-1086`):
    ```cpp
    const float LegsAbp = FMath::Clamp(WeaponLegsAbpWeight, 0.f, 1.f);
    const float Lower = FMath::Min(1.f, WeaponLowerWeight) * (1.f - LegsAbp);
    ```
    When `LegsAbp == 1.0`, `Lower == 0.0f`.
  - Because `Lower == 0.0f`, the weapon layer does not modify any pelvis or leg bones. The lower body remains 100% the underlying AnimBP pose (`BS_Rifle`).
  - `RecordWeaponProbe` computes `LowerDeviation` by comparing `After` (post-layer) with `Before` (pre-layer). Since the layer untouched the lower body, the deviation is `0.00`.
  - In `ShotgunSmokeCommand.cpp:644`:
    ```cpp
    const bool bFull = MoveAnim && MoveAnim->IsWeaponLayerFullBody();
    Check(State, State.MoveSamples >= 8 && (bFull ? State.MoveMaxLower > 1.f : State.MoveMaxLower < 0.01f) && State.MoveMaxUpper > 1.f, ...);
    ```
    `bFull` is based purely on `IsWeaponLayerFullBody()` (which is always true for the shotgun set), ignoring whether `bAbpLegs` (`LegsAbpWeight > 0.5f`) is active for that gait!
    Therefore, the test erroneously demanded `State.MoveMaxLower > 1.f` even when the design intentionally delegates the lower body to the ABP graph (`MoveMaxLower < 0.01f`), as properly handled in `RpgAnimSmokeCommand.cpp:302-315`.

### 2.2 Sprint Foot Drift 57% (Sampling Window Artifact)
- **Observed:**
  `ShotgunSmoke: foot plant sprinting: planted foot drift median 371.7 cm/s / mean 262.8 cm/s (57 % / 41 % of 649 cm/s) over 0.03 s mid-stance (2 frames)`
- **Mechanism:**
  - `ShotgunSmokeCommand.cpp:634` abruptly exits stage 16 as soon as `State.MoveSamples >= 20` (0.33 seconds at 60 FPS).
  - During those first 20 frames, the character is accelerating from standing (0 cm/s) up to 649 cm/s.
  - Only 2 frames ever satisfied the mid-stance criteria (`Height < MinFootZ + 1.5f`), both captured during transient acceleration where the foot is not in steady plant.
  - In contrast, `RpgAnimSmokeCommand.cpp:568` waits until the operative finishes the move or reaches steady strides, where the bare ABP sprint drift was measured at **22%** (well within 35%).

### 2.3 Walk Foot Drift 78% on ABP Legs
- **Observed:**
  `ShotgunSmoke: foot plant walking: planted foot drift median 136.2 cm/s / mean 166.7 cm/s (78 % / 96 % of 174 cm/s) over 0.53 s mid-stance (32 frames)`
- **Mechanism:**
  - For Rank 0, `bShotgunWalkOnAbpLegs == true`, so the lower body is handed to the ABP graph (`BS_Rifle`).
  - In `OperativeAnimInstance.cpp:1087`:
    `Axis(FMath::Min(WalkSampleSpeed, StandBlendSpaceMaxSpeed), StandBlendSpaceMaxSpeed, StandBlendSpeed, StandPlayRate);`
    `WalkSampleSpeed = 120.f`, `StandBlendSpaceMaxSpeed = 480.f`.
    At 174 cm/s, `LocomotionSpeed` is 174. `StandBlendSpeed = 174`, but `StandPlayRate = FMath::Max(1.f, 174 / 480) = 1.0f`.
  - In `BS_Rifle`, the walk sample sits at ~120 cm/s. With `StandPlayRate == 1.0`, the animation legs cycle at 120 cm/s while the capsule moves at 174 cm/s, causing a 54 cm/s foot lag / sliding.
  - Furthermore, `SampleFeet` records `Feet[Lower]` in world space during initial acceleration, where `MinFootZ[0]` is caught before steady strides are reached.
  - In addition, during the first 0.2s of movement, `ShotgunLegsAbpWeight` steps from 0.0 to 1.0. During that blend-in window, `Lower > 0`, which caused `MoveMaxLower = 158.81` to be recorded as the max lower deviation even though steady walking is 100% ABP legs.

---

## 3. Implementation Plan

### Step 1: Fix Gait & ABP Legs Disambiguation in `ShotgunSmokeCommand.cpp`
- In `ShotgunSmokeCommand.cpp`:
  - Determine `bAbpLegs = MoveAnim && MoveAnim->GetWeaponLayerLegsAbpWeight() > 0.5f`.
  - For the lower body deviation check:
    - If `bAbpLegs`: verify `MoveMaxLower < 0.01f` (lower body untouched by layer, follows ABP) and `MoveMaxUpper > 1.f`.
    - If `!bAbpLegs` (pack legs): verify `MoveMaxLower > 1.f` and `MoveMaxUpper > 1.f`.
  - Ensure steady sampling window: allow sufficient frames for foot plant sampling so mid-stance reflects steady strides (at least 5-10 mid-stance frames, wait until steady velocity or move completed as in `RpgAnimSmoke`).

### Step 2: Implement Rank 0 vs Rank 1 Two-Carrier Validation in `ShotgunSmoke`
- In `ShotgunSmokeCommand.cpp`:
  - Test Carrier 0 (Engineer, rank 0):
    - Walks on ABP legs (`bAbpLegs = true`, `MoveMaxLower < 0.01f`).
    - Foot drift < 35%.
  - Spawn / setup Carrier 1 (e.g. PMC Fighter given shotgun, or squad mate with shotgun, rank 1):
    - Verify `bShotgunWalkOnAbpLegs == false`.
    - Walks on shotgun pack loops (`bAbpLegs = false`, `MoveMaxLower > 1.f`).
    - Foot drift < 35% (pack typically < 20%).
  - Both sprint on ABP legs with foot drift < 35%.

### Step 3: Anim Instance / Rules / Data Tuning for Foot Drift
- Inspect `OperativeAnimInstanceShotgun.cpp`, `weapon_anim_sets.json`, and `WeaponAnimLayerRules`:
  - Ensure `ShotgunLegsAbpWeight` initializes immediately to target upon stance/weapon switch without unwanted blend-in lag during smoke checks.
  - Verify root bone yaw and translation compensation during ABP walk.
  - Verify `StandPlayRate` scaling or `WalkClipSpeed` / blend space speeds so that ABP walk foot drift satisfies `< 35%`.

---

## 4. Exact Files to Touch

1. `Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp`:
   - Two-carrier test sequence (Rank 0 ABP legs, Rank 1 pack legs).
   - Correct lower-body deviation assertion for ABP legs vs pack legs.
   - Steady foot plant sampling window.
2. `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp` & `Public/Characters/OperativeAnimInstance.h`:
   - Prompt evaluation of `bShotgunWalkOnAbpLegs` and `ShotgunLegsAbpWeight`.
   - Any root compensation needed for alternate walk.
3. `Content/Data/Weapons/weapon_anim_sets.json`:
   - Tuning parameters if needed (speeds, blend bands).
4. `Source/CodexTacticsTests/Private/Characters/WeaponAnimLayerTest.cpp`:
   - Add unit tests for alternate walk configuration and data integrity.

---

## 5. Verification Strategy

1. **Compilation:**
   - `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`
2. **Unit Tests:**
   - `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Animation.WeaponLayer`
3. **Targeted Smokes:**
   - `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands ShotgunSmoke,ShotgunCoverSmoke,PistolSmoke,WeaponSelectorSmoke`
4. **Gate Validation:**
   - `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 068 -SonnetReview`
