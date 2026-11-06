# Diagnostic Report: Bug #33 (Prone PMC Shoots in One Direction While Looking in Another)

**Issue Summary:**
When a hostile PMC operator (or player operative) is in the prone stance (`bProne == true`), the character can fire at a target (e.g. the player) located at an angle to their body (e.g. 90° or 180° away). Visually, the character's body and rifle remain pointed in their original direction, while bullets and muzzle tracers fly out sideways directly towards the target without the character first rotating their body.

---

## 1. Exact Files, Functions, and Line Numbers

1. **`Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp`**
   - **Function:** `AOperativeCharacter::TickCombat(float DeltaTime)`
   - **Lines:** `655–669`
   - *Detail:* Manages turn rate towards combat target. Uses a fast turn interpolation:
     ```cpp
     const float TurnRate = AimTurnRateDegPerSec; // 360 deg/sec
     const FRotator TargetRot = (TargetLoc - GetActorLocation()).Rotation();
     SetActorRotation(FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, 12.f));
     ```
     Actor rotation is snapped/interpolated without checking stance or waiting for prone animation rotation.
   - **Function:** `AOperativeCharacter::ShootAtTarget(AActor* Target)`
   - **Lines:** `773–889` (specifically lines `864–882`)
   - *Detail:* Computes shot direction and line trace directly from muzzle socket to target center:
     ```cpp
     const FVector FireDir = (Target->GetActorLocation() - MuzzleLocation).GetSafeNormal();
     ```
     Does NOT check if `FireDir` is aligned with the actual forward vector of the weapon barrel mesh (`GetWeaponSocketRotation().Vector()`).

2. **`Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp`**
   - **Function:** `AOperativeCharacter::GetShotAimResidualDeg()`
   - **Lines:** `251–279`
   - *Detail:* Calculates the angular difference between the aiming orientation and target direction. Line `277` falls back to `AimOffsetRules::YawToTarget` using the Actor's root rotation. Because Actor rotation was already updated in `TickCombat` (line `663`), the calculated residual angle immediately drops below `AimConeDeg` (30°), granting permission to fire before the skeletal mesh visual turn finishes.

3. **`Source/CodexTactics/Private/Characters/AimOffsetRules.cpp`**
   - **Function:** `AimOffsetRules::WantsAimOffset(const FStanceState& State)`
   - **Lines:** `13–17`
   - *Detail:* Explicitly disables upper-body Aim Offset when prone:
     ```cpp
     if (State.bProne)
     {
         return false;
     }
     ```
     Because Aim Offset is disabled, the spine and weapon bones do NOT twist towards the target. The skeletal mesh remains fixed forward along the body axis.

4. **`Source/CodexTactics/Public/Characters/OperativeMovementRules.h`**
   - **Line:** `132`
   - *Detail:* Defines `static constexpr float TurnRateProne = 143.f;` (prone turning speed is intentionally restricted to 143°/sec). However, `OperativeCharacterCombat.cpp:655` ignores this constant and turns rapidly using `AimTurnRateDegPerSec` (360°/s) and `12.f` interpolation speed.

---

## 2. Ranked Hypotheses with Code Evidence

### Hypothesis 1: Aim Offset Disabled in Prone + Actor Yaw Decoupled from Prone Mesh (Rank 1 - Primary Cause)
- **Evidence:** In `AimOffsetRules.cpp:15`:
  ```cpp
  if (State.bProne) return false;
  ```
  In standing or crouching stances, spine aim offsets twist the upper body towards the target. In prone, aim offset is explicitly disabled by design.
  Simultaneously, `GetShotAimResidualDeg()` checks actor yaw rather than the physical weapon barrel bone transform. When `TickCombat` rotates actor yaw, `GetShotAimResidualDeg()` drops below `AimConeDeg` (30°), giving fire clearance. But because prone characters lack an instantaneous crawl/pivot turn animation, the physical skeletal mesh remains facing the old direction while the weapon fires bullets sideways out of the muzzle socket.

### Hypothesis 2: Direct Vector Computation from Muzzle to Target Without Barrel Forward Check (Rank 2)
- **Evidence:** In `OperativeCharacterCombat.cpp:864–876`:
  ```cpp
  const FVector MuzzleLoc = GetWeaponMuzzleLocation();
  const FVector TargetLoc = Target->GetActorLocation();
  const FVector ShotDir = (TargetLoc - MuzzleLoc).GetSafeNormal();
  World->LineTraceSingleByChannel(Hit, MuzzleLoc, MuzzleLoc + ShotDir * MaxRange, ECC_Visibility, Params);
  ```
  The raycast and visual tracer ignore the physical forward vector of the weapon barrel (`MuzzleRotation.Vector()`). The system simply draws a line from the muzzle position directly to the target, allowing the weapon to fire at 90° or 180° angles relative to the barrel.

### Hypothesis 3: Combat Aim Loop Bypasses `TurnRateProne` Constraint (Rank 3)
- **Evidence:** `OperativeMovementRules.h:132` sets `TurnRateProne = 143.f`, but `OperativeCharacterCombat.cpp:655` bypasses movement rules and applies `AimTurnRateDegPerSec` (360.f) with `RInterpTo(..., 12.f)`. This creates a severe desynchronization between the combat controller's virtual orientation and the animation system's root motion / turn-in-place capabilities in the prone stance.

---

## 3. How to Verify Each Hypothesis

### Automated Smoke Tests (`Scripts/test_map.json`)
- **`StanceSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke StanceSmoke`
  - Validates prone transitions, crawling, aiming, and stance recovery.
- **`TurnBasedAimSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke TurnBasedAimSmoke`
  - Verifies aim limits, aim cones, and facing angle verification before firing.
- **`PmcStandoffSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke PmcStandoffSmoke`
  - Tests PMC aiming and firing arcs against player positions.

### Console Variables and Debug Logging
- **Console Variables:**
  - `Codex.Debug.FireTrace 1`: Renders line traces and muzzle vectors during firing.
  - `Codex.Debug.Anim 1`: Dumps current stance flags and Aim Offset weights.
  - `Codex.Debug.Facing 1`: Displays actor forward vector vs skeletal mesh root bone forward vector.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Display: Combat: Shot fired with residual angle %.1f deg (prone=%d)`
  - `LogCodexTactics: Warning: Shot direction deviates by %.1f deg from barrel forward`

---

## 4. Smallest Proposed Fix (Description Only)

1. **Enforce Barrel Forward Angle Verification Before Firing:**
   In `AOperativeCharacter::CanShootTarget()` (or immediately prior to calling `ShootAtTarget` in `OperativeCharacterCombat.cpp`):
   Verify that the weapon's physical barrel forward vector (`GetWeaponSocketRotation().Vector()`) matches the direction to the target within an acceptable tolerance cone:
   ```cpp
   const FVector BarrelForward = GetWeaponSocketRotation().Vector();
   const FVector DirToTarget = (Target->GetActorLocation() - GetWeaponMuzzleLocation()).GetSafeNormal();
   const float AlignmentDot = FVector::DotProduct(BarrelForward, DirToTarget);
   if (AlignmentDot < FMath::Cos(FMath::DegreesToRadians(bProne ? 15.f : AimConeDeg)))
   {
       return false; // Withhold fire until body/barrel turns towards target
   }
   ```
2. **Apply `TurnRateProne` in Combat Turn Interp:**
   In `OperativeCharacterCombat.cpp:655–669`, if `IsProne()` is true, clamp the turn rate to `OperativeMovementRules::TurnRateProne` (143°/s) and prevent firing until the prone turn completes and alignment is confirmed.
3. **Check Both Player Squad and PMC AI:**
   Apply this check to both `TeamId == 0` (player squad) and `TeamId >= 1` (PMC operators) to maintain symmetric tactical rules.
