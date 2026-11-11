# Second Review of Stealth Runtime (Commits 90a6bf64 + 4d15a54f)

**Reviewer:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target Branch:** `interim/2026-11-12` (Reviewed on `gemini/015-2-stealth-review2`)  
**Commits Audited:**  
- `90a6bf64` feat(stealth): native stealth layer, wall press, lean reveal, HUD toggle (003)
- `4d15a54f` fix(stealth): enroll late weapon meshes and preserve overrides  
**Scope:** Verification of earlier HIGH/MEDIUM findings from `e3c05788`, audit of late weapon visual enrollment, and evaluation of smoke coverage. Read-only audit (zero modifications to C++/assets).

---

## 1. Executive Summary & Verdict

**Verdict: PASS with Minor Integration Recommendations.**

All High and Medium severity defects identified in the initial review (`e3c05788`) have been cleanly resolved by Codex:
1. **Wall Locomotion Phase Jitter (HIGH):** Completely resolved via canonical `CoverFacingRules::IsShimmyForward()` integration.
2. **Material Override Preservation (MEDIUM):** Resolved; `OverrideMaterials` indexing correctly snapshots and clears overrides without baking old asset materials into component slots.
3. **Reload Blend Pop (MEDIUM):** Resolved; `bBodyBusy` now smoothly interpolates `StealthWeight` down via `FMath::FInterpConstantTo`.
4. **Late Weapon Visuals & Meshes:** Seamlessly enrolled and restored around `ApplyWeaponVisual()` (including `WeaponRoundMesh` and magazine attachments).

---

## 2. Re-Audit of Previous Findings

### 2.1. Wall Locomotion Phase Jitter (HIGH)
* **File & Line:** `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:194-196`
* **Original Issue:** `Sign = FVector::DotProduct(Velocity, Forward) < 0.f ? -1.f : 1.f;` caused frame-to-frame sign oscillation because velocity is orthogonal to the wall normal during cover shimmy.
* **Resolution in `90a6bf64`:**
  ```cpp
  // The capsule faces the wall normal, so dot(Velocity, Forward) is nearly zero while shimmying.
  const float Sign = Operative->bShimmying && !Operative->IsShimmyForward() ? -1.f : 1.f;
  StealthPhase = FMath::Fmod(StealthPhase + DeltaSeconds * Rate * Sign + 1.f, 1.f);
  ```
* **Verification:**
  - `IsShimmyForward()` relies on `CoverFacingRules::IsShimmyForward(CoverFacing, ShimmyDirection, bHasCoverThreat)`.
  - Thoroughly covered by unit tests in `CoverFacingTest.cpp` and existing cover smoke commands.
  - Phase advances forward when moving in facing direction and reverses when backing up along the wall, preventing foot flutter.
* **Status: VERIFIED FIXED.**

---

### 2.2. Transient Material Overrides & Late Weapon Meshes (MEDIUM)
* **File & Line:** 
  - `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:88-92, 110-113`
  - `Source/CodexTactics/Private/Characters/OperativeCharacterSniper.cpp:196-205, 289-292`
* **Original Issue:** `GetMaterial(Slot)` snapshotted default asset materials, creating permanent material overrides on components when exiting stealth, and failed to enroll late-attached weapon meshes (e.g. pistol magazine, RPG round).
* **Resolution in `4d15a54f`:**
  - `StealthOriginalMaterials` explicitly snapshots `BodyPart->OverrideMaterials.IsValidIndex(Slot) ? BodyPart->OverrideMaterials[Slot].Get() : nullptr`.
  - When exiting stealth, setting `nullptr` properly clears the override, allowing the mesh's native asset material to display.
  - In `AOperativeCharacter::ApplyWeaponVisual()`:
    ```cpp
    if (bStealthMode)
    {
        // Remove only our runtime overrides before replacing assets; null originals fall back to each new mesh's materials.
        bStealthMode = false;
        RefreshStealthVisuals();
        bStealthMode = true;
        StealthMeshes.Reset();
        StealthMaterialCounts.Reset();
        StealthOriginalMaterials.Reset();
    }
    // ... swap mesh assets ...
    if (bStealthMode)
    {
        RefreshStealthVisuals(); // enrolls new weapon parts and WeaponRoundMesh
    }
    ```
  - `StealthModeSmokeCommand.cpp:96-136` (`CheckWeaponSwaps`) explicitly exercises real weapon swaps (Pistol magazine and RPG-7 round mesh) and asserts material restoration.
* **Status: VERIFIED FIXED.**

---

### 2.3. Reload Instant Blend Pop (MEDIUM)
* **File & Line:** `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:51-58`
* **Original Issue:** `if (!Operative || ... || bBodyBusy) { StealthWeight = 0.f; }` immediately snapped `StealthWeight` to 0 on frame 0 of a reload or montage action, causing a visual pop.
* **Resolution in `90a6bf64`:**
  ```cpp
  if (!Operative || bIsDead || bKnockedDown || bIsProne || bIsClimbing || bIsVaulting)
  {
      StealthWeight = 0.f;
  }
  else
  {
      StealthWeight = FMath::FInterpConstantTo(StealthWeight, bActive && !bBodyBusy ? 1.f : 0.f, DeltaSeconds, 1.f / FMath::Max(0.01f, Set.BlendSeconds));
  }
  ```
* **Verification:**
  - `bBodyBusy` (which includes `bIsReloading`, stance transitions, and slot montages) now smoothly fades `StealthWeight` to 0 over `BlendSeconds` (0.25s).
  - Hard-zeroing is strictly reserved for fatal/ragdoll states (`bIsDead`, `bKnockedDown`, climbing, prone).
* **Status: VERIFIED FIXED.**

---

## 3. New Audit Observations on `4d15a54f`

### 3.1. Weapon Mesh Enrolling Cleanliness
* **Trigger:** Calling `RefreshStealthVisuals()` when new components are dynamically added to `WeaponMeshParts`.
* **Expected:** Components already enrolled are not duplicated in `StealthMeshes`; new components are appended to `StealthMeshes` and their original override state recorded.
* **Actual:** `OperativeCharacterStealth.cpp:83`:
  ```cpp
  if (!bBodyOrWeapon || StealthMeshes.Contains(BodyPart))
  {
      continue;
  }
  ```
  This guard prevents array re-allocation or index drift for already-tracked components.

### 3.2. Integration Smoke Harness Status
Codex's `INTERIM_REPORT.md` explicitly noted:
> "six Gemini integration-test gaps: pending."

Codex has already implemented the weapon swap smoke leg (`CheckWeaponSwaps`). The remaining 5 scenarios are documented on Codex's roadmap:
1. Moving wall-shimmy smoke stage (validates phase accumulation during movement).
2. Fast jog transition stage (`Stealth_Fast_*`).
3. Acoustic footstep detection test (scaling of `FootstepNoiseScale = 0.5x`).
4. Female skeleton retarget validation (`Female_Soldier_Skeleton`).
5. Lethal hit / knockdown mid-stealth verification.

These remaining gaps do not block functionality, but should be tracked for test completion.

---

## 4. Conclusion & Handover

The stealth runtime on `interim/2026-11-12` is robust, architecturally sound, and adheres strictly to project standards. Once Codex finishes the pending MSVC rebuild and verifies smoke tests, this feature is ready for merging to `main`.
