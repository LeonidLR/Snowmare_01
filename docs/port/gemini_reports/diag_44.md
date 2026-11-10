# Diagnostic Report: Bug #44 (Killed PMC Operator Remains Standing in Aim Pose)

**Issue Summary:**
When a hostile PMC operator (`TeamId >= 1`) is killed in close combat on Depot (`L_Gen_Depot_1`, e.g. near corner engagements), the character does not collapse, play a death animation, or transition to ragdoll physics. Instead, the skeletal mesh remains permanently standing frozen in the combat aim pose (`CombatAim`).

---

## 1. Exact Files, Functions, and Line Numbers

1. **`Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp`**
   - **Function:** `UOperativeAnimInstance::HandleDied()`
   - **Lines:** `757–808` (specifically line `787`)
   - *Detail:* Evaluates `DeathStandAnimations`. If empty or null, it falls back to `KnockdownComponent->PlayDeathFall(Dir)`. If the target montage is incompatible or returns a duration of `0.0f`, no animation plays and the AnimGraph remains in the active aim state.

2. **`Source/CodexTactics/Private/Combat/KnockdownComponent.cpp`**
   - **Function:** `UKnockdownComponent::PlayDeathFall(EDirection Dir)`
   - **Lines:** `442–463`
   - *Detail:* Selects death clip (`AM_Knocked_Back` or `AM_Death_Back`) and calls `PlayClip(Clip, ...)`.
   - **Function:** `UKnockdownComponent::PlayClip(UAnimSequenceBase* Clip, ...)`
   - **Lines:** `310–335`
   - *Detail:* Calls `AnimInstance->Montage_Play(...)`. If playback fails or duration is 0, no fallback physics or pose clamping occurs.

3. **`Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp`**
   - **Function:** `AOperativeCharacter::BecomeCorpse()`
   - **Lines:** `143–175`
   - *Detail:* Disables character movement component (`MovementComponent->DisableMovement()`) and sets collision profiles to ignore. However, it does NOT clear `bIsAiming = false`, does NOT reset upper-body Aim Offset weights (`CombatAimWeight = 0.f`), and does NOT enable physics simulation (`Mesh->SetSimulatePhysics(true)`).
   - **Function:** `AOperativeCharacter::HandleDied()`
   - **Lines:** `46–97`
   - *Detail:* Stops active combat orders and delegates death visuals to AnimInstance / KnockdownComponent.

4. **`Scripts/Editor/setup_pmc_operator_body.py`**
   - **Functions:** `make_compatible()`, `operator_mesh()`
   - **Lines:** `53–112`
   - *Detail:* Registers `UE4_Mannequin_Skeleton` in `compatible_skeletons` for `SK_PmcOperator_Skeleton`. However, slot groups (specifically the `FullBody` slot required by death montages) and retarget profile mappings were not fully verified on the imported PMC skeletal mesh assets.

5. **`Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp`**
   - **Function:** `UTacticalSightSubsystem::UpdateHostileOperatives`
   - **Lines:** `640–679` (specifically lines `651–657`)
   - *Detail:* When an operative dies:
     ```cpp
     SetOperativeConcealed(*Operative, State, false);
     HostileStates.Remove(Operative);
     ```
     The deceased character is permanently unhidden from Fog of War and removed from the active update list. Its frozen pose is never updated or evaluated again.

---

## 2. Ranked Hypotheses with Code Evidence

### Hypothesis 1: Death Montage Playback Fails on `SK_PmcOperator_Skeleton` (Rank 1 - Primary Cause)
- **Evidence:** In `OperativeAnimInstance.cpp:787`:
  ```cpp
  if (KnockdownComponent)
  {
      KnockdownComponent->PlayDeathFall(Dir);
  }
  ```
  `KnockdownComponent::PlayDeathFall` selects `AM_Knocked_Back` or `AM_Death_Back`, which are animation montages authored for `SK_Mannequin`. For PMC operators using the newly imported `SK_PmcOperator_Skeleton`, `Montage_Play` fails (returns `0.0f`) if the `FullBody` slot is missing or if the montage is rejected due to mismatched bone hierarchies/curves.
  Because `Montage_Play` returns 0, no montage plays. In `ABP_Operative`, the death state machine was deferred in favor of the montage. With no montage playing, the AnimGraph continues to evaluate the current state: the aim pose.

### Hypothesis 2: `BecomeCorpse()` Freezes Movement Without Clearing Aim State or Enabling Ragdoll (Rank 2)
- **Evidence:** In `OperativeCharacterHealth.cpp:143–175`:
  ```cpp
  GetCharacterMovement()->DisableMovement();
  GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
  ```
  `BecomeCorpse()` disables character movement and collision to turn the character into a corpse, but it never sets `bIsAiming = false`, never resets weapon aim offsets, and does not engage ragdoll physics via `Mesh->SetSimulatePhysics(true)`.
  If animation playback fails to transition to a lying-down frame, the character movement is frozen while the skeletal mesh continues to render its last evaluated stance: standing aiming a rifle.

### Hypothesis 3: Fog of War Visibility Freeze on Death (Rank 3)
- **Evidence:** In `TacticalSightSubsystem.cpp:651–657`:
  Upon death, `SetOperativeConcealed(*Operative, State, false)` forces the PMC to be visible, and `HostileStates.Remove(Operative)` strips the PMC from future sight updates. If the PMC died while partially concealed or during an aim animation, removing it from subsystem updates while the animation LOD or tick optimization freezes deceased meshes results in the standing aim pose being permanently rendered to the player camera.

---

## 3. How to Verify Each Hypothesis

### Automated Smoke Tests (`Scripts/test_map.json`)
- **`PmcArenaSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke PmcArenaSmoke`
  - Runs PMC vs PMC battles on `L_FormationLabCombat` using `L_PmcArena.json`. Inspects casualty animations when PMC fighters kill each other.
- **`PmcTeamSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke PmcTeamSmoke`
  - Validates full kill lifecycle of PMC operatives when shot down by the player squad.
- **`KnockdownSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke KnockdownSmoke`
  - Verifies knockdown fall clips and death animations (`AM_Knocked_Back`, `AM_Death_Back`).
- **`EnemyDeathSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke EnemyDeathSmoke`
  - Checks corpse state transition and collision/mesh states.

### Console Variables and Debug Logging
- **Console Variables:**
  - `Codex.Debug.Knockdown 1`: Prints knockdown and death clip dispatch logs.
  - `Codex.Debug.Anim 1`: Dumps currently playing montages, active slot groups, and anim instance states.
  - `Codex.Sight 1`: Displays sight and visibility status of all operative corpses.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Warning: PlayClip: Montage_Play returned 0.0 for montage AM_Death_Back on SK_PmcOperator`
  - `LogCodexTactics: Display: [Health] PMC Operator %s died, BecomeCorpse called`
  - `LogAnimMontage: Warning: Montage AM_Death_Back could not be played on skeleton SK_PmcOperator_Skeleton`

---

## 4. Smallest Proposed Fix (Description Only)

1. **Clear Aim State on Death in Health / Anim Pipeline:**
   In `AOperativeCharacter::BecomeCorpse()` and `OperativeAnimInstance::HandleDied()`, explicitly clear combat aim states:
   ```cpp
   bIsAiming = false;
   CombatAimWeight = 0.0f;
   ```
2. **Add Physics Ragdoll Fallback on Montage Play Failure:**
   In `KnockdownComponent::PlayClip()` or `OperativeAnimInstance::HandleDied()`:
   If `AnimInstance->Montage_Play(...) <= 0.0f`, immediately trigger ragdoll physics:
   ```cpp
   if (USkeletalMeshComponent* Mesh = GetMesh())
   {
       Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
       Mesh->SetSimulatePhysics(true);
   }
   ```
   This guarantees that even if an animation montage fails to play on a custom or imported skeleton, the character physically collapses to the ground rather than remaining frozen standing.
3. **Validate PMC Skeleton Compatibility Slot:**
   In `Scripts/Editor/setup_pmc_operator_body.py`, verify and configure the `FullBody` anim slot group on `SK_PmcOperator_Skeleton` to match `SK_Mannequin`.
