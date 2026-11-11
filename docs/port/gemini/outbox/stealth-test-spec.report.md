# Stealth Smoke Gaps — Test Specification Report

**Branch:** `gemini/013-3-stealth-test-spec` (base: `main`, referencing `interim/2026-11-12`)  
**Task:** 013 Afternoon Queue — Item 3 (Read-only test specification for stealth smoke blind spots)  
**Author:** Gemini (CodexTactics Multi-Agent Team)  
**Target Consumer:** Codex (Stealth implementation and C++ smoke command maintenance)

---

## Executive Summary

During the interim integration of native stealth mode (`90a6bf64` on `interim/2026-11-12`), five primary blind spots plus one concrete follow-up risk were identified in the review:
1. **Wall Shimmy**: Bi-directional along-wall locomotion coverage (left vs. right wall facing, reverse shimmy sign, and idle recovery).
2. **Fast Jog**: Non-sprinting accelerated locomotion in stealth, blend weight mechanics, and fast clip playback verification.
3. **Noise Perception**: Acoustic footstep detection with distance scaling (`FootstepNoiseMultiplier = 0.5`) and wall occlusion, completely omitted in the current sight-only perception smoke.
4. **Female Skeleton Set**: Runtime validation of the 62 retargeted female clips (`/Game/Characters/Animations/Stealth/Female/`) and female stride/phase measurement tables (`Female_Soldier_Skeleton`).
5. **Death & Knockdown**: Immediate zeroing of stealth weight, clean stance recovery, and full restoration of material slots and overlay states upon incapacitation.
6. **Weapon Parts Attached After Stealth Entry**: Mesh registration race condition where dynamic attachments (`WeaponMeshParts`, `WeaponRoundMesh`) spawned after `SetStealthMode(true)` miss the snapshot and render opaque.

This document provides a precise, actionable test specification for each case, detailing:
- Exact **Setup**
- Step-by-step **Action**
- Quantitative **Assertions**
- **Existing Smoke Helpers** to reuse
- Exact **File:Line** references on `interim/2026-11-12`

---

## Case 1: Wall Shimmy (Directional & Facing Coverage)

### 1.1 Objective & Gap Description
In `OperativeAnimInstanceStealth.cpp:120-136`, wall locomotion relies on:
- Cover facing prefix (`Stealth_LeftWall_` vs `Stealth_RightWall_`).
- Direction sign: `const float Sign = Operative->bShimmying && !Operative->IsShimmyForward() ? -1.f : 1.f;`.
- Phase integration: `StealthPhase = FMath::Fmod(StealthPhase + DeltaSeconds * Rate * Sign + 1.f, 1.f);`.

The current smoke (`StealthModeSmokeCommand.cpp:350-375`, stages 41/40) tests only a single forward shimmy on a generic wall. It lacks:
- Left-wall facing vs. Right-wall facing verification.
- Reverse shimmy (`!IsShimmyForward()`), verifying phase decrements cleanly without popping or reversing facing.
- Transition back to `Wall_Idle` upon shimmy completion.

### 1.2 Test Specification
- **Setup**:
  - Spawn or locate cover wall using `CoverTraceRules::FindCoverSlotAt(World, At(State, 570.f, 150.f, 90.f), State.Forward, Slot)`.
  - Place operative in cover: `Op->EnterCover(Slot)` with `Op->bIgnoreCoverThreatForTesting = true`.
  - Ensure stealth active: `Op->SetStealthMode(true)`.
  - Identify two adjacent cover slots along the wall: `SlotForward` (positive relative tangent) and `SlotBackward` (negative relative tangent).
- **Action**:
  - **Step 1A (Forward Shimmy - Left Wall)**: Ensure `Op->CoverFacing == ECoverFacing::Left`. Order `Op->OrderShimmyTo(SlotForward)`.
  - **Step 1B (Sampling)**: Monitor while `Op->bShimmying && Op->GetVelocity().Size2D() > 3.f`.
  - **Step 1C (Reverse Shimmy - Left Wall)**: Order `Op->OrderShimmyTo(SlotBackward)` where `!Op->IsShimmyForward()`.
  - **Step 1D (Shimmy Stop)**: Allow movement to complete (`Op->bShimmying == false`) or call `Op->StopOperative()`.
  - **Step 1E (Right Wall Mirror)**: Repeat with `Slot` configured for `ECoverFacing::Right`.
- **Assertions**:
  - `Op->IsInStealthMode() == true` throughout all shimmy steps.
  - While moving forward: `Anim->GetStealthWeight() > 0.9f`, dominant clip is `Stealth_LeftWall_Slow_IP` (or `Fast_IP`).
  - While moving in reverse (`!Op->IsShimmyForward()`): dominant clip remains `Stealth_LeftWall_Slow_IP`, `Anim->GetStealthWeight() > 0.9f`, and character yaw remains aligned to wall normal (`FVector::DotProduct(Op->GetActorForwardVector(), Normal) > 0.8f`).
  - When stationary at wall: dominant clip immediately becomes `Stealth_LeftWall_Idle_IP` with `Anim->GetStealthWeight() > 0.9f`.
  - Stance stability on cover exit: calling `Op->LeaveCover()` results in `Op->GetStance() == EOperativeStance::Crouching` (never snaps to standing).
- **Reusable Smoke Helpers**:
  - `CoverTraceRules::FindCoverSlotAt` (`Source/CodexTactics/Public/Tactics/CoverTraceRules.h:35`)
  - `StealthModeSmoke::At` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:88`)
  - `StealthModeSmoke::Anim` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:66`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:120-136`
  - `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:328-360`
  - `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:350-375`

---

## Case 2: Fast Jog in Stealth (Locomotion Blend & Sprint Boundary)

### 2.1 Objective & Gap Description
In `OperativeAnimInstanceStealth.cpp:67-75, 137-154`, stealth locomotion blends between `Set.Walk` (`Stealth_Slow_<Dir>_IP`) and `Set.Jog` (`Stealth_Fast_<Dir>_IP`) based on:
$$\text{Fast} = \text{Clamp}\left(\frac{\text{LocomotionSpeed} - \text{SlowSpeed}}{\text{FastSpeed} - \text{SlowSpeed}}, 0, 1\right)$$
Where `SlowSpeed \approx 170\text{ cm/s}` and `FastSpeed \approx 470\text{--}500\text{ cm/s}`.

The current smoke (`StealthModeSmokeCommand.cpp:245-275`, Stage 2) only asserts `Stealth_Slow_%s_IP`. The fast jog clips are never sampled, and the boundary condition (fast jog must NOT break stealth, whereas sprint DOES break stealth) is untested.

### 2.2 Test Specification
- **Setup**:
  - Clear arena via `SmokeUtils::FindClearArena(World, Op->GetActorLocation(), ArenaForward, ...)`.
  - Operative in stealth: `Op->SetStealthMode(true)`, `Op->GetStance() == EOperativeStance::Crouching`.
  - Target destination placed at distance $1200\text{ cm}$ along forward vector to allow sufficient runway for acceleration.
  - Temporarily configure `Op->GetCharacterMovement()->MaxWalkSpeedCrouched = 480.f` to permit reaching fast jog speed while crouching.
- **Action**:
  - **Step 2A (Order Move)**: `Op->OrderMoveTo(TargetLocation, false)`.
  - **Step 2B (Sample Acceleration Profile)**: Record clip weights across three speed brackets:
    - Bracket 1 (Low speed: $80\text{--}150\text{ cm/s}$): Slow clip dominant.
    - Bracket 2 (Mid speed: $260\text{--}340\text{ cm/s}$): Dual blend active.
    - Bracket 3 (High speed: $> 420\text{ cm/s}$): Fast clip dominant.
  - **Step 2C (Sprint Break Contrast)**: While moving in Bracket 3, call `Op->SetSprinting(true)`.
- **Assertions**:
  - Throughout Step 2B: `Op->IsInStealthMode() == true`, `!Op->IsSprinting()`, and `Anim->GetStealthWeight() > 0.9f`.
  - In Bracket 1: Dominant clip is `Stealth_Slow_F_IP` (`Fast < 0.1f`).
  - In Bracket 2: Both `Stealth_Slow_F_IP` and `Stealth_Fast_F_IP` have blend weights $> 0.15f$, and total locomotion weight equals `Base * Move`.
  - In Bracket 3: Dominant clip is `Stealth_Fast_F_IP` (`Votes.FindRef(TEXT("Stealth_Fast_F_IP")) > Samples / 2`, with `Fast > 0.7f`).
  - In Step 2C: Calling `SetSprinting(true)` immediately sets `Op->IsInStealthMode() == false` on next tick via `UpdateStealthMode()`.
- **Reusable Smoke Helpers**:
  - `SmokeUtils::FindClearArena` (`Source/CodexTacticsDev/Private/Debug/SmokeUtils.h:170`)
  - `SmokeUtils::FreeSpot` (`Source/CodexTacticsDev/Private/Debug/SmokeUtils.h:120`)
  - `StealthModeSmoke::Sample` voting logic (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:92-135`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:67-75, 137-154`
  - `Content/Data/Weapons/weapon_anim_sets.json:180-240`
  - `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:245-275`

---

## Case 3: Noise Perception & Acoustic Footstep Attenuation

### 3.1 Objective & Gap Description
In `EnemyCharacterPerception.cpp:145-165`:
```cpp
const float NoiseScale = StealthModeRules::FootstepNoiseScale(Member->IsInStealthMode(), StealthModeRules::GetConfig());
const float NoiseDistance = NoiseScale > 0.f ? Distance / NoiseScale : TNumericLimits<float>::Max();
if (!Heard && PerceptionRules::HearsMovement(Params, Noise, NoiseDistance)
    && PerceptionRules::HearsMovementThroughWalls(Params, Noise, NoiseDistance, CountHearingWalls(*Member)))
{
    Heard = Member;
}
```
`StealthModeRules::FootstepNoiseScale` returns `Config.FootstepNoiseMultiplier = 0.5f`.  
Therefore, an operative in stealth doubles their virtual acoustic distance:
$$\text{NoiseDistance} = \frac{\text{Distance}}{0.5} = 2 \times \text{Distance}$$
The current smoke only verifies visual perception (`FindVisibleOperativeNow`), completely omitting acoustic footstep perception.

### 3.2 Test Specification
- **Setup**:
  - Spawn an `AEnemyCharacter` (Frostbitten archetype) using `WaveSubsystem->SpawnEnemy`.
  - Freeze enemy movement and disable non-acoustic senses:
    - `Enemy->bOverridePerception = true`
    - `Enemy->PerceptionOverride.SightRangeCm = 0.f` (sight completely disabled)
    - `Enemy->PerceptionOverride.SightHalfAngleDeg = 0.f`
    - `Enemy->PerceptionOverride.SmellRadiusCm = 0.f`
    - `Enemy->PerceptionOverride.HearCrouchWalkCm = 250.f` ($2.5\text{ m}$)
    - `Enemy->PerceptionOverride.HearingOcclusionPerWall = 0.4f`
  - Put enemy in patrol mode: `Enemy->bSearching = false`.
- **Action & Assertions (Two-Distance Bracket)**:
  - **Distance 1: $200\text{ cm}$ (Open Air)**:
    - *Control Pass (Stealth OFF)*: Operative at $200\text{ cm}$, crouched, moving at $100\text{ cm/s}$ (`Noise == ESquadMovementNoise::CrouchWalk`).  
      $\text{NoiseDistance} = 200\text{ cm} \le 250\text{ cm}$.  
      Execute `Enemy->TickPatrolPerception(0.1f, Input)`.  
      **Assert:** `Input.bHearsOperative == true` (Enemy hears footsteps and triggers alert).
    - *Stealth Pass (Stealth ON)*: Reset enemy patrol (`Enemy->ResetPatrolAlert()`). Operative activates stealth (`SetStealthMode(true)`), moving at $100\text{ cm/s}$ at $200\text{ cm}$.  
      $\text{NoiseDistance} = \frac{200}{0.5} = 400\text{ cm} > 250\text{ cm}$.  
      Execute `Enemy->TickPatrolPerception(0.1f, Input)`.  
      **Assert:** `Input.bHearsOperative == false` (Enemy does NOT hear footsteps).
  - **Distance 2: $100\text{ cm}$ (Open Air)**:
    - *Stealth Pass (Stealth ON)*: Operative at $100\text{ cm}$, stealthed, moving at $100\text{ cm/s}$.  
      $\text{NoiseDistance} = \frac{100}{0.5} = 200\text{ cm} \le 250\text{ cm}$.  
      Execute `Enemy->TickPatrolPerception(0.1f, Input)`.  
      **Assert:** `Input.bHearsOperative == true` (Inside halved acoustic radius, stealth operative is heard).
  - **Distance 3: $100\text{ cm}$ with Wall Occlusion**:
    - Spawn a wall between operative and enemy (`CountHearingWalls == 1`).  
      $\text{EffectiveRadius} = 250\text{ cm} \times 0.4 = 100\text{ cm}$.  
      At distance $105\text{ cm}$: $\text{NoiseDistance} = 210\text{ cm} > 100\text{ cm}$.  
      **Assert:** `Input.bHearsOperative == false` (Wall correctly muffles stealth footstep).
- **Reusable Smoke Helpers**:
  - `UWaveSubsystem::SpawnEnemy` (`Source/CodexTactics/Public/Combat/WaveSubsystem.h`)
  - `PerceptionRules::ClassifyMovement` (`Source/CodexTactics/Public/AI/PerceptionRules.h:28`)
  - `StealthModeSmoke::Check` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:52`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/EnemyCharacterPerception.cpp:145-165`
  - `Source/CodexTactics/Private/Characters/StealthModeRules.cpp:28-31`
  - `Source/CodexTactics/Private/AI/PerceptionRules.cpp:138-165`

---

## Case 4: Female Skeleton Retargeted Set Validation

### 4.1 Objective & Gap Description
In `003-stealth-mode.report.md`:
> *"Female copies were generated, skeleton-validated and measured; the game smoke currently exercises the default male leader... Preserve these limitations in review."*

124 retargeted stealth clips exist (62 male in `/Game/Characters/Animations/Stealth/Male/` and 62 female in `/Game/Characters/Animations/Stealth/Female/`).
`weapon_anim_sets.json:150-250` defines separate `body_measurements` for `Female_Soldier_Skeleton` (e.g. `Stealth_Slow_F_IP` speed is $159.94\text{ cm/s}$ vs male $170.29\text{ cm/s}$; phase offset is $0.0625$ vs $0.05$).
This case verifies that a female operative loads the female retargeted folder, uses female stride/phase tables, and plays smoothly without asset or retargeting mismatch.

### 4.2 Test Specification
- **Setup**:
  - Select or spawn an operative assigned the `SK_Female_Soldier` mesh (`/Game/Female_Soldier/Meshes/SK_Female_Soldier.SK_Female_Soldier` on skeleton `Female_Soldier_Skeleton`).
  - Force anim set refresh: `Anim->LoadShotgunAnimSet()`.
- **Action**:
  - Select female operative: `Squad->SetSelectedGroup({ FemaleOp })`.
  - Toggle stealth: `PC->ToggleStealthModeKey()`.
  - Order directional movement (Forward and Right) on clear arena.
- **Assertions**:
  - `Anim->HasStealthSet() == true`.
  - `Anim->LoadedSetsSkeleton->GetName() == TEXT("Female_Soldier_Skeleton")`.
  - Folder resolution: `Anim->StealthRetargetFolder == TEXT("/Game/Characters/Animations/Stealth/Female")`.
  - Clip asset validation:
    `Anim->GetStealthCrouchClip()->GetPathName()` contains `/Game/Characters/Animations/Stealth/Female/Stealth_Crouch_IP`.
  - Measurement calibration:
    `Anim->StealthSetData.ClipSpeeds.FindRef(TEXT("Stealth_Slow_F_IP"))` equals $159.94\text{ cm/s} \pm 0.1\text{ cm/s}$ (confirming female table applied, not male $170.29\text{ cm/s}$).
  - Locomotion sampling: While moving forward, dominant clip is the female sequence, `Anim->GetStealthWeight() > 0.9f`, and measured root motion drift over $400\text{ cm}$ is $\le 0.5\text{ cm}$.
- **Reusable Smoke Helpers**:
  - `UOperativeAnimInstance::LoadShotgunAnimSet` (`Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:115`)
  - `SmokeUtils::FindClearArena` (`Source/CodexTacticsDev/Private/Debug/SmokeUtils.h:170`)
  - `StealthModeSmoke::CheckVisuals` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:70-80`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp:115-165`
  - `Content/Data/Weapons/weapon_anim_sets.json:150-250`
  - `Scripts/Editor/setup_stealth_animation.py`

---

## Case 5: Death & Knockdown During Stealth

### 5.1 Objective & Gap Description
In `OperativeCharacterStealth.cpp:97-108`:
```cpp
void AOperativeCharacter::UpdateStealthMode()
{
	if (bStealthMode && (!HealthComponent || !HealthComponent->IsAlive() || IsCriticallyWounded() || IsKnockedDown()
		|| IsPanicking() || IsRaging() || bCarrying || bVaulting || IsClimbingLadder() || VehicleQueries::IsMounted(this)
		|| Stance == EOperativeStance::Prone || StealthModeRules::ShouldBreak(true, false, bSprinting, StealthModeRules::GetConfig())))
	{
		SetStealthMode(false);
	}
	RefreshStealthVisuals();
}
```
And in `OperativeAnimInstanceStealth.cpp:48-52`:
```cpp
if (!Operative || bIsDead || bKnockedDown || bIsProne || bIsClimbing || bIsVaulting)
{
	StealthWeight = 0.f;
}
```
If an operative is eliminated or knocked down while stealthed, stealth must abort **instantly**:
- `StealthWeight` must drop to 0 without interpolation delay (so ragdoll / get-up anims are not corrupted by stealth additive blending).
- Mesh materials and overlay must revert to original states (no glowing ghost overlay on corpses).
- Stealth must not automatically re-engage upon resurrection or recovery.

### 5.2 Test Specification
- **Setup**:
  - Operative in stealth: `Op->SetStealthMode(true)`.
  - Capture pre-stealth materials: `OriginalMaterial = Op->GetMesh()->GetMaterial(0)`.
  - Verify stealth active: `Op->IsInStealthMode() == true`, `Op->GetMesh()->GetOverlayMaterial() == Op->StealthMaterial`.
- **Action & Assertions (Two Scenarios)**:
  - **Scenario 5A (Lethal Damage / Death)**:
    - Apply lethal damage: `Op->HealthComponent->ApplyDamage(1000.f, nullptr, nullptr, nullptr)`.
    - Advance 1 tick (DeltaTime $0.05\text{ s}$).
    - **Assert:**
      1. `Op->IsInStealthMode() == false` (immediately cleared).
      2. `Anim->GetStealthWeight() == 0.f` (immediate cut to zero).
      3. `Op->GetMesh()->GetMaterial(0) == OriginalMaterial` (body slot restored).
      4. `Op->GetMesh()->GetOverlayMaterial() == nullptr` (stealth overlay cleared).
      5. `Op->CanEnterStealthMode() == false` (refuses re-entry while dead).
  - **Scenario 5B (Non-Lethal Knockdown)**:
    - Revive/reset operative: `Op->HealthComponent->RestoreHealth(100.f, 100.f)`. Re-enter stealth: `Op->SetStealthMode(true)`.
    - Apply knockdown: `Op->ApplyKnockdown(2.0f, FVector::ForwardVector)`.
    - Advance 1 tick.
    - **Assert:**
      1. `Op->IsInStealthMode() == false`.
      2. `Anim->GetStealthWeight() == 0.f`.
      3. `Op->GetMesh()->GetOverlayMaterial() == nullptr`.
    - Advance $2.5\text{ s}$ until knockdown get-up completes:
      **Assert:** `Op->IsKnockedDown() == false`, but `Op->IsInStealthMode() == false` (does NOT re-arm automatically).
- **Reusable Smoke Helpers**:
  - `UHealthComponent::ApplyDamage` / `RestoreHealth` (`Source/CodexTactics/Public/Combat/HealthComponent.h`)
  - `AOperativeCharacter::ApplyKnockdown` / `IsKnockedDown` (`Source/CodexTactics/Public/Characters/OperativeCharacter.h`)
  - `StealthModeSmoke::CheckVisuals` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:70-80`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:12-16, 70-108`
  - `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:38-52`
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStance.cpp:50-75`

---

## Case 6: Weapon Parts & Modular Attachments Attached After Stealth Entry

### 6.1 Objective & Gap Description
In `003-stealth-mode.report.md`:
> *"New weapon mesh components created after stealth entry are not enrolled in the initial snapshot. A weapon switch introducing new modular parts may leave those new parts opaque. This remains a concrete follow-up for Claude; current smoke uses the initial weapon. Do not claim weapon-switch coverage."*

**Root Cause:**
In `OperativeCharacterStealth.cpp:45-68`, `SetStealthMode(true)` iterates existing components:
```cpp
TArray<UMeshComponent*> Meshes;
GetComponents(Meshes);
// Populates StealthMeshes and StealthOriginalMaterials
```
However, when switching weapons (e.g. to Pistol 9mm via `SwitchToWeaponById("pistol")`), `OperativeCharacterSniper.cpp:340-375` (`ApplyWeaponVisual`) dynamically spawns new `UStaticMeshComponent` instances:
```cpp
while (WeaponMeshParts.Num() < PartCount)
{
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
    Part->SetupAttachment(WeaponMesh);
    ...
    WeaponMeshParts.Add(Part);
}
```
And for launcher/explosive weapons:
```cpp
WeaponRoundMesh = NewObject<UStaticMeshComponent>(this, TEXT("WeaponRoundMesh"), RF_Transient);
```
These components are created **after** `SetStealthMode(true)` was executed. Consequently:
1. They are not in `StealthMeshes`.
2. They do not receive `StealthMaterial` and render completely opaque (e.g. opaque black magazine or optic on a translucent ghost body).
3. Upon exiting stealth (`SetStealthMode(false)`), `StealthMeshes` has stale indices.

### 6.2 Test Specification
- **Setup**:
  - Operative in initial state equipped with primary rifle (which has `WeaponMeshParts.Num() == 0`).
  - Enter stealth: `Op->SetStealthMode(true)`.
  - Confirm initial stealth visuals via `CheckVisuals(State, Op)`.
- **Action**:
  - **Step 6A (Dynamic Part Creation)**:
    While stealthed, execute weapon swap: `Op->SwitchToWeaponById(TEXT("pistol"))` (or call `PC->CycleWeaponKey()`).
    This triggers `ApplyWeaponVisual()`, spawning the magazine static mesh component into `Op->WeaponMeshParts[0]`.
  - **Step 6B (Refresh Visuals Call)**:
    Call `Op->RefreshStealthVisuals()`.
  - **Step 6C (Material Audit)**:
    Inspect all components in `WeaponMeshParts` and `WeaponRoundMesh`.
  - **Step 6D (Stealth Exit)**:
    Call `Op->SetStealthMode(false)`.
- **Assertions**:
  - **During Stealth (Step 6C)**:
    - `Op->WeaponMeshParts.Num() > 0 && Op->WeaponMeshParts[0] != nullptr`.
    - `Op->WeaponMeshParts[0]->GetMaterial(0) == Op->StealthMaterial` (Part must receive stealth MID).
    - `Op->WeaponMeshParts[0]->GetOverlayMaterial() == Op->StealthMaterial` (Overlay applied).
    - Opacity scalar `OpacityFill` on the part matches `Config.VisualOpacity`.
  - **On Stealth Exit (Step 6D)**:
    - `Op->WeaponMeshParts[0]->GetMaterial(0) != Op->StealthMaterial` (Restored to genuine weapon texture material).
    - `Op->WeaponMeshParts[0]->GetOverlayMaterial() == nullptr`.
    - No dangling or null pointers in `StealthMeshes`.
- **Concrete C++ Fix Guidance for Codex**:
  1. In `OperativeCharacterSniper.cpp:ApplyWeaponVisual()`:
     Add a call to `RefreshStealthVisuals()` at the end of the method if `bStealthMode` is true.
  2. In `OperativeCharacterStealth.cpp:RefreshStealthVisuals()`:
     Instead of only looping over existing `StealthMeshes`, check if `WeaponMeshParts` or `WeaponRoundMesh` contain components not present in `StealthMeshes`. If so, enroll them:
     ```cpp
     for (UStaticMeshComponent* Part : WeaponMeshParts)
     {
         if (Part && !StealthMeshes.Contains(Part))
         {
             StealthMeshes.Add(Part);
             StealthMaterialCounts.Add(Part->GetNumMaterials());
             for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
             {
                 StealthOriginalMaterials.Add(Part->GetMaterial(Slot));
             }
         }
     }
     ```
- **Reusable Smoke Helpers**:
  - `AOperativeCharacter::SwitchToWeaponById` (`Source/CodexTactics/Public/Characters/OperativeCharacter.h`)
  - `AOperativeCharacter::ApplyWeaponVisual` (`Source/CodexTactics/Private/Characters/OperativeCharacterSniper.cpp:320`)
  - `StealthModeSmoke::CheckVisuals` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:70-80`)
- **File:Line References**:
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:45-95`
  - `Source/CodexTactics/Private/Characters/OperativeCharacterSniper.cpp:340-375`
  - `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerItems.cpp:190-220`

---

## Summary Matrix for Codex

| # | Gap Area | Primary Files to Update / Assert | Helper to Reuse | Key Numeric / Logic Boundary |
|---|---|---|---|---|
| **1** | Wall Shimmy | `OperativeAnimInstanceStealth.cpp:120-136` | `CoverTraceRules::FindCoverSlotAt` | `Sign = -1.f` on `!IsShimmyForward()`; facing prefix `Stealth_LeftWall_` vs `Stealth_RightWall_` |
| **2** | Fast Jog | `OperativeAnimInstanceStealth.cpp:67-75, 137-154` | `SmokeUtils::FindClearArena` | Speed $> 420\text{ cm/s}$ activates `Stealth_Fast_*`; stealth does NOT break (only sprint breaks) |
| **3** | Noise Perception | `EnemyCharacterPerception.cpp:145-165` | `PerceptionRules::ClassifyMovement` | At $200\text{ cm}$: non-stealth heard ($\le 250\text{ cm}$), stealth unheard ($400\text{ cm} > 250\text{ cm}$) |
| **4** | Female Retarget | `OperativeAnimInstanceShotgun.cpp:115-165` | `LoadShotgunAnimSet` | Skeleton `Female_Soldier_Skeleton` -> `/Stealth/Female/`; speed table $159.94\text{ cm/s}$ vs $170.29\text{ cm/s}$ |
| **5** | Death / Knockdown | `OperativeCharacterStealth.cpp:97-108` | `UHealthComponent::ApplyDamage` | Instant `StealthWeight = 0.f`; full material restore; no auto re-entry after get-up |
| **6** | Weapon Parts | `OperativeCharacterSniper.cpp:340-375` | `ApplyWeaponVisual`, `CheckVisuals` | Dynamic `WeaponMeshParts` / `WeaponRoundMesh` enrolled into `StealthMeshes` and rendered with MID |
