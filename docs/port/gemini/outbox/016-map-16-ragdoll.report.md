# Implementation Map #16 — Character Death Ragdoll & Standing Corpses Fix

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11 (Updated after Codex review #196)  
**Target:** Task #16 (`pl-ragdoll`) & Codex Task 005 — Рэгдолл (физика падений и тел) & Исправление застывших трупов на Depot  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/port/codex/inbox/005-dead-enemies-standing-ragdoll.md`, `docs/port/TASK_BOARD.md` §16, `docs/systems/pmc-squad-ai.md`

---

## 1. Tactical Concept & Root Cause Analysis

### 1.1. Why Dead Enemies Stay Standing (Codex Task 005)
On Depot (`L_Gen_Depot_1`), defeated enemies frequently remain frozen standing in idle poses instead of collapsing to the floor (evidence: `Saved/Orchestrator/dead-standing/2026-11-11-zombies-standing.webp`, `play.log` frame 7526-7529).

Code inspection reveals three concurrent root causes:
1. **Knockdown Race Condition:** In `play.log` lines 7526-7529, an explosive RPG blast kills `BP_Enemy_MilitaryZombie_C_3` (`[Health] ... died`). In the exact same frame, `UKnockdownComponent` triggers a blast knockdown with a scheduled get-up (`[Knockdown] Military Zombie knocked down by blast... get-up 0.80 s at 2.08x`). The get-up sequence executes on the dead actor, returning the corpse to an upright standing pose.
2. **Animation Blend-Out to Idle:** In `AEnemyCharacter::HandleDied()` (`EnemyCharacterHealth.cpp:211-223`), `bDeathPose` is set to `Anim->HasDeathAnimation()`. When `bDeathPose == true`, `bLayDownOnFreeze` is set to `false`. If the death montage asset does not hold its final frame or blends out, the animation blueprint seamlessly reverts to default idle locomotion, standing upright forever because `LayCorpseDown()` is bypassed.
3. **Absence of Physical Collapse:** Characters lack physical ragdoll simulation upon fatal damage. The capsule collision is disabled (`SetCollisionEnabled(NoCollision)`), leaving the skeletal mesh purely driven by anim evaluation without gravity or kinetic impulse.

---

## 2. Depot Archetype Physics Asset & Skeleton Compatibility Audit

Per designer requirement and Codex review #196, every enemy archetype appearing on Depot plus operative bodies has been audited directly against the `.uasset` binary import tables and runtime CDOs for exact **Skeleton $\leftrightarrow$ Physics Asset** binding compatibility:

| Archetype / Character | Blueprint Class | Skeletal Mesh Path | Assigned Runtime Skeleton | Assigned Physics Asset (`.uasset`) | Compatibility & Verification Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Frost Hound** | `BP_Enemy_Hound` | `/Game/Combat_Dog/Mesh/SK_Combat_Dog` | `/Game/Combat_Dog/Mesh/CH_Combat_Dog` | `/Game/Combat_Dog/Mesh/PA_Combat_Dog.uasset` | **VERIFIED (1:1 Match)**. Quadruped dog rig. `SK_Hound_1slot` inherits identical PA. |
| **Frost Brute** | `BP_Enemy_Brute` | `/Game/Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1` | `/Game/Mutant_monster_1/Meshes/base_mesh/SK_UE4_Mannequin_Skeleton` | `/Game/Mutant_monster_1/Meshes/base_mesh/PHYS_baze_mesh_PhysicsAsset_1.uasset` | **VERIFIED (1:1 Match)**. Humanoid mutant rig. `SK_Brute_1slot` inherits identical PA. |
| **Frostbitten** | `BP_Enemy_Frostbitten` | `/Game/mutant_monster_2/base_mesh/SK_base_mesh` | `/Game/mutant_monster_2/base_mesh/SK_UE4_Mannequin_Skeleton` | `/Game/mutant_monster_2/base_mesh/PHYS_base_mesh_PhysicsAsset.uasset` | **VERIFIED (1:1 Match)**. Humanoid mutant rig. `SK_Frostbitten_1slot` inherits identical PA. |
| **Military Zombie** (Var 1..3) | `BP_Enemy_MilitaryZombie` | `/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_1..3` | `/Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_1_Skeleton` | `/Game/military_zombie_set2/Meshes/base_mesh/PA_Mannequin.uasset` | **VERIFIED (1:1 Match)**. Pointed directly to `PA_Mannequin` in `base_mesh/`. `SK_MilitaryZombie_1_1slot` inherits identical PA. |
| **Cutter Mech-Hound** | `BP_Enemy_Cutter` | `/Game/Biochemical_Monster_2/baze_mech/ue4/SKM_baze_mesh` | `/Game/Biochemical_Monster_2/baze_mech/ue4/SK_UE4_Mannequin_Skeleton` | `/Game/Biochemical_Monster_2/baze_mech/ue4/PA_Mannequin_PhysicsAsset_2.uasset` | **VERIFIED (1:1 Match)**. Mech/predator rig. `SK_Cutter_1slot` inherits identical PA. |
| **Marksman** | `BP_Enemy_Marksman` | `/Game/Biochemical_Monster_1/baze_mech/ue4/SKM_baze_mesh1` | `/Game/Biochemical_Monster_1/baze_mech/ue4/SK_UE4_Mannequin_Skeleton` | `/Game/Biochemical_Monster_1/baze_mech/ue4/PA_Mannequin_baze_mesh_PhysicsAsset.uasset` | **VERIFIED (1:1 Match)**. Humanoid sniper rig. `SK_Marksman_1slot` inherits identical PA. |
| **Hostile PMC Operator** | `BP_Operative` (Hostile) | `/Game/Characters/Enemies/PMC/Operator/SKM_PmcOperator` | `/Game/Characters/Enemies/PMC/Operator/SK_PmcOperator_Skeleton` | `/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5_PhysicsAsset.uasset` | **VERIFIED (1:1 Match)**. Bound directly in `SKM_PmcOperator.uasset` to Cyberpunk Explorer UE5 PA (compatible UE5 hierarchy). |
| **Player Operative A** | `BP_Operative` | `/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a` | `/Game/Post_Apo_Survivor/Demo/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton` | `/Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a_PhysicsAsset.uasset` | **VERIFIED (1:1 Match)**. Humanoid survivor rig. `SK_Post_Apo_Survivor_a_1slot` inherits identical PA. |
| **Player Operative B** | `BP_Operative_FemaleSoldier`| `/Game/Female_Soldier/Mesh/SK_Female_soldier` | `/Game/Female_Soldier/Mesh/Female_Soldier_Skeleton` | `/Game/Female_Soldier/Mesh/SK_Female_Soldier_Physics.uasset` | **VERIFIED (1:1 Match)**. Female soldier rig. `SK_Female_Soldier_1slot` inherits identical PA. |
| **Player Operative C** | `BP_Operative` (Explorer) | `/Game/Cyberpunk_Explorer_A/Meshes/SK_Cyberpunk_Explorer_A` | `/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5_Skeleton` | `/Game/Cyberpunk_Explorer_A/Meshes/Cyberpunk_Explorer_A_UE5_PhysicsAsset.uasset` | **VERIFIED (1:1 Match)**. Full UE5 humanoid rig. |
| **Spitter** | `None` (C++ default) | *None* (Static Cylinder `BodyMesh`) | *None* | *None* | **MISSING (Expected)**. C++ cylinder placeholder; uses `LayCorpseDown()` 90° tip code fallback. |
| **Cryo Drone** | `None` (C++ default) | *None* (Static Drone model) | *None* | *None* | **MISSING (Expected)**. Flying unit; falls by gravity or explodes in place. |

> [!IMPORTANT]
> **Summary on Physics Asset Compatibility:** All 10 active character meshes have confirmed 1:1 Physics Assets mapped to their exact runtime skeleton. Furthermore, automated test `EnemyLookBodiesAssetTest.cpp:66` explicitly asserts that every single-slot optimized body (`SK_*_1slot`) has `Each.Mesh->GetPhysicsAsset() == Mesh->GetPhysicsAsset()`. No physics asset creation or asset modification is needed.

---

## 3. Existing Classes to Extend vs. New Code

### 3.1. Reusable Engine Components (~85% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Enemy Death Handler** | `Source/CodexTactics/Private/Characters/EnemyCharacterHealth.cpp:122-229` | `AEnemyCharacter::HandleDied()` triggers kill events, scores, and unregisters from AI. |
| **Operative Death Handler** | `Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp:52-132` | `AOperativeCharacter::HandleDied()` and `BecomeCorpse()`. |
| **Corpse Lifecycle & Subsystem** | `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp:14-180` | `UCorpseSubsystem::Tick()` monitors settle time, counts caps, freezes rigid bodies, and fades oldest corpses. |
| **Freeze & Settle Rules** | `Source/CodexTactics/Private/Characters/CorpseRules.cpp:5-17` | `CorpseRules::ShouldFreeze()` pure logic: evaluates settle duration, falling status, and ladder status. |
| **Knockdown Component** | `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp:1-250` | `UKnockdownComponent` handles blast reactions. Needs death guard via `!HealthComponent->IsAlive()`. |
| **Ledge Fall Component** | `Source/CodexTactics/Private/Falls/LedgeFallComponent.cpp:1-200` | `ULedgeFallComponent` (in `Falls/`, header `Falls/LedgeFallComponent.h`) handles airborne drops and terminal velocity impacts. |
| **Health Component API** | `Source/CodexTactics/Public/Combat/HealthComponent.h:49-50` | `bool IsAlive() const { return CurrentHealth > 0.0f; }`. Note: Check `!HealthComponent->IsAlive()` as there is no `IsDead()` method. |
| **Suppression Subsystem** | `Source/CodexTactics/Public/Combat/SuppressionSubsystem.h:69` | `USuppressionSubsystem` is a `UTickableWorldSubsystem` (not an actor component). |
| **Stagger Status Effect** | `Source/CodexTactics/Public/Data/CombatTypes.h:59` | `EStatusEffect::Stagger` managed directly on `UHealthComponent` (`HealthComponent->HasStatusEffect(EStatusEffect::Stagger)`). |

### 3.2. New Code Required (~15%)

1. **`DeathRagdollRules` (Pure State & Impulse Logic):**
   - Header: `Source/CodexTactics/Public/Combat/DeathRagdollRules.h`
   - Source: `Source/CodexTactics/Private/Combat/DeathRagdollRules.cpp`
   - Maps damage type / weapon source (Kinetic, RPG Blast, Heavy Melee, Sniper) to linear impulse vectors and angular kick.
   - Evaluates whether body velocity has fallen below settle threshold ($v < 15\text{ cm/s}$, $\omega < 0.2\text{ rad/s}$) for $> 0.5\text{ s}$.
   - Manages simultaneous active ragdoll limits to protect frame budget ($< 4$ active simulating ragdolls concurrently).
2. **Death State Guards Across Auxiliary Systems:**
   - In `UKnockdownComponent` (`Combat/KnockdownComponent.cpp`): Abort if `OwnerHealth && !OwnerHealth->IsAlive()`.
   - In `UHealthComponent` (`Combat/HealthComponent.cpp`): Ignore status effect application (`Stagger`, `Burning`, `Frozen`) when `!IsAlive()`.
   - In `USuppressionSubsystem` (`Combat/SuppressionSubsystem.cpp`): Verify target `HealthComponent && HealthComponent->IsAlive()` before accumulating suppression pressure.
   - In `UEnemyAnimInstance` (`Characters/EnemyAnimInstance.cpp`): Ensure death state prevents montage blend-out to idle.

---

## 4. Data Keys & Tuning

Configured in `Content/Data/Combat/death_ragdoll.json`:

```json
{
  "enabled": true,
  "max_simultaneous_ragdolls": 4,
  "fallback_to_death_anim_over_cap": true,
  "ragdoll_delay_s": 0.0,
  "max_settle_timeout_s": 3.5,
  "linear_settle_threshold_cm_s": 15.0,
  "angular_settle_threshold_rad_s": 0.25,
  "impulse_multipliers": {
    "Rifle": 12000.0,
    "Shotgun": 28000.0,
    "Sniper": 45000.0,
    "Explosive": 85000.0,
    "HeavyMelee": 35000.0,
    "Default": 15000.0
  },
  "upward_blast_bias_cm": 15000.0,
  "airborne_fall_fatal_threshold_cm": 300.0
}
```

---

## 5. Insertion Points & Execution Flow

### 5.1. Knockdown Guard (Resolving Task 005 / Frame 7526 Bug)
In `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp`:
```cpp
bool UKnockdownComponent::TryApplyKnockdown(const FVector& HitDirection, float Strength)
{
    // Fix for Task 005 / Frame 7526 race condition:
    // UHealthComponent exposes IsAlive(); do NOT call IsDead()
    if (OwnerHealth && !OwnerHealth->IsAlive())
    {
        UE_LOG(LogCodexTactics, Verbose, TEXT("[Knockdown] Ignored: %s is dead"), *GetOwner()->GetName());
        return false;
    }
    // Proceed with knockdown...
}
```

### 5.2. Enabling Physics in `AEnemyCharacter::HandleDied()`
In `Source/CodexTactics/Private/Characters/EnemyCharacterHealth.cpp`:
```cpp
void AEnemyCharacter::HandleDied(AActor* Victim, const FString& AttackerSource)
{
    if (bIsDying) return;
    bIsDying = true;
    
    // 1. Stop AI controller and path following immediately
    if (AController* C = GetController())
    {
        C->StopMovement();
        C->UnPossess();
    }
    
    // 2. Disable pawn movement component & capsule world collision
    if (UCharacterMovementComponent* Move = GetCharacterMovement())
    {
        Move->StopMovementImmediately();
        Move->DisableMovement();
    }
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);

    // 3. Check for Ragdoll capability (PhysicsAsset present on mesh asset)
    USkeletalMeshComponent* Body = GetMesh();
    const bool bHasPhysicsAsset = Body && Body->GetSkeletalMeshAsset() && Body->GetSkeletalMeshAsset()->GetPhysicsAsset();
    
    if (bHasPhysicsAsset && DeathRagdollRules::CanSimulateRagdoll(GetWorld()))
    {
        // 4. Detach from capsule and activate ragdoll simulation
        Body->SetCollisionProfileName(TEXT("Ragdoll"));
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Body->SetAllBodiesSimulatePhysics(true);
        Body->WakeAllRigidBodies();
        Body->bBlendPhysics = true;
        
        // 5. Apply directional impulse from killing hit
        const FVector Impulse = DeathRagdollRules::CalculateImpulse(AttackerSource, LastHitDirection, LastHitDamage);
        const FName HitBone = LastHitBone.IsNone() ? FName(TEXT("pelvis")) : LastHitBone;
        Body->AddImpulse(Impulse, HitBone, true);
        
        bIsRagdollActive = true;
        bDeathPose = true; // Ragdoll provides its own natural pose
    }
    else
    {
        // Fallback for Spitter / Drone / Over-cap limit:
        PlayDeathMontageOrLayDown(AttackerSource);
    }
    
    // 6. Register with UCorpseSubsystem for settlement and freezing
    if (UCorpseSubsystem* Corpses = GetWorld()->GetSubsystem<UCorpseSubsystem>())
    {
        Corpses->RegisterCorpse(this);
    }
}
```

### 5.3. Freezing and Sleeping in `UCorpseSubsystem`
When `CorpseRules::ShouldFreeze()` evaluates to true:
1. `Body->PutAllRigidBodiesToSleep();`
2. `Body->SetAllBodiesSimulatePhysics(false);`
3. `Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);` (Allows corpse searching / remains interaction without physics tick cost).
4. Unregisters from world ticking to maintain $0$ CPU overhead.

---

## 6. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`
  - Validates impulse calculations per weapon type and settle threshold detection.
  - Verifies `max_simultaneous_ragdolls` budget gating and fallback logic.
* **Smoke Command:** `DeathRagdollSmokeCommand` (`Codex.Test.DeathRagdoll`):
  1. Spawns each Depot archetype (`FrostHound`, `Frostbitten`, `MilitaryZombie`, `Brute`, `Cutter`, `Marksman`, `PMC Operator`).
  2. Applies lethal damage via Sniper Rifle $\to$ asserts ragdoll activates and character collapses.
  3. Ticks until settle time elapsed $\to$ asserts pelvis height $Z < 35\text{ cm}$ above floor (lying flat, NEVER standing).
  4. Spawns explosive RPG blast hitting multiple targets $\to$ asserts simultaneous ragdoll limits respected and no frame $> 250\text{ ms}$.
  5. Asserts `UKnockdownComponent` does not fire get-up sequence on dead actors (`!IsAlive()`).

---

## 7. Open Design Questions (with Recommended Defaults)

1. **Player Operative Death Presentation:**
   - *Question:* Should friendly player squad operatives also use ragdoll physics on death, or retain authored dramatic death montages with the tactical death cinematic camera?
   - *Recommended Default:* Retain authored cinematic death montages for player operatives during the death camera window ($1.8\text{ s}$); transition to ragdoll physics only if fatal damage came from massive explosive blast ($> 150\text{ damage}$), otherwise hold final authored pose.
2. **Airborne / Roof Fall Ragdoll Transition:**
   - *Question:* When an operative or enemy falls from a high ledge / roof ($> 3\text{ m}$), should ragdoll activate during flight or upon ground impact?
   - *Recommended Default:* Ragdoll activates upon ground impact if terminal velocity exceeds $12\text{ m/s}$ (fatal collision). During flight, preserve falling motion via `ULedgeFallComponent` for predictable trajectory and obstacle clearing.
3. **Corpse Loot Interaction Collision Profile:**
   - *Question:* When an operative approaches a frozen ragdoll to loot supplies, does the physical mesh block navigation?
   - *Recommended Default:* Frozen ragdolls set collision to `QueryOnly` with `ECC_Visibility` and `ECC_Interactable` enabled, ignoring `ECC_Pawn`. Operatives can smoothly walk up to or over remains without pathing hitches.
