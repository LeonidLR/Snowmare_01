# 049 — Implementation Plan: Character Death Ragdoll & Standing Corpses Fix (Task 005 / #16)

**Target:** Task 005 & Mechanics Board #16 (`pl-ragdoll`)  
**Context:** `docs/port/codex/inbox/005-dead-enemies-standing-ragdoll.md`, `docs/port/TASK_BOARD.md` §16, `docs/systems/pmc-squad-ai.md`  
**Deliverable:** Technical Specification and Implementation Plan (Pure docs, zero code modifications)  
**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-14  

---

## 1. Executive Summary & Problem Diagnosis

### 1.1. The Bug on Depot (`L_Gen_Depot_1`)
Defeated enemies on Depot frequently freeze in standing poses instead of collapsing to the snow (observed in `Saved/Orchestrator/dead-standing/2026-11-11-zombies-standing.webp` and `play.log` lines 7526–7529).

### 1.2. Root Causes Identified in Code
1. **Knockdown Race Condition:** When an explosive burst (e.g. RPG-7, mortar, grenade) deals lethal damage, `UKnockdownComponent::TryKnockDown()` is invoked in the same frame as `UHealthComponent::ApplyDamage()`. The knockdown logic schedules a get-up sequence (`EKnockdownPhase::GettingUp`), which finishes after death and forces the dead actor back into an upright standing pose.
2. **Animation Blend-Out to Idle:** In `AEnemyCharacter::HandleDied()` (`EnemyCharacterHealth.cpp:211-223`), `bDeathPose` is set to `Anim->HasDeathAnimation()`. If the death montage asset blends out or does not hold its terminal frame, the animation blueprint seamlessly transitions back into default idle locomotion. Because `bDeathPose == true`, `bLayDownOnFreeze` is set to `false`, entirely bypassing `LayCorpseDown()`.
3. **Absence of Physical Collapse:** Characters lack real physics simulation on death. The capsule collision is disabled (`SetCollisionEnabled(NoCollision)`), leaving the skeletal mesh evaluated purely by the animation blueprint without gravity or kinetic impulse.

---

## 2. Inventory: What Currently Exists in Code (~85% Reusable)

| System / Component | Path & Key Lines | Existing Capability & Status |
| :--- | :--- | :--- |
| **Enemy Death Flow** | `Source/CodexTactics/Private/Characters/EnemyCharacterHealth.cpp:122-250` | `HandleDied()` handles dying flag, tactics notification, airborne drops, and calls `UCorpseSubsystem::RegisterCorpse()`. Has fallback `LayCorpseDown()` (90° pitch tip). |
| **Operative Death Flow** | `Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp:52-132` | `HandleDied()` and `BecomeCorpse()`. Transitions friendly squad members to corpse state. |
| **Corpse Lifecycle Subsystem** | `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp:14-180` | `UCorpseSubsystem : public UWorldSubsystem`. Ticks active corpses, checks `CorpseRules::ShouldFreeze()`, freezes meshes, disables tick, and enforces level corpse cap (`corpse_cap`). |
| **Freeze & Settle Rules** | `Source/CodexTactics/Private/Characters/CorpseRules.cpp:5-17` | Pure rules evaluating whether corpse has settled (`SettleSeconds >= SettleDelay`). |
| **Knockdown Component** | `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp:99-160, 410-450` | `CanFallNow()` checks `Health && !Health->IsAlive()`, but blast damage paths can race. `HandleDeath()` exists but is only called from anim instance. |
| **Ledge Fall Component** | `Source/CodexTactics/Private/Falls/LedgeFallComponent.cpp:1-200` | Manages airborne trajectory, gravity falls off roofs/bridges, and impact landing. |
| **Health Component** | `Source/CodexTactics/Public/Combat/HealthComponent.h:49-50` | Exposes `bool IsAlive() const { return CurrentHealth > 0.0f; }`. Note: No `IsDead()` method; always use `!IsAlive()`. |
| **Physics Assets Inventory** | `Content/Combat_Dog/`, `Content/Mutant_monster_1/`, `Content/mutant_monster_2/`, `Content/military_zombie_set2/`, `Content/Biochemical_Monster_1/`, `Content/Biochemical_Monster_2/` | **Verified 1:1 match for all 10 active character rigs** (Hound, Brute, Frostbitten, Military Zombie, Cutter, Marksman, PMC Operator, Operatives A/B/C). Single-slot optimized bodies inherit identical Physics Assets (asserted in `EnemyLookBodiesAssetTest.cpp:66`). |

---

## 3. Step-by-Step Implementation Plan

### Step 1: Data Configuration (`Content/Data/Combat/death_ragdoll.json`)
Create data-driven configuration file containing:
- `enabled`: Global toggle (`bool`, default `true`).
- `max_simultaneous_ragdolls`: Cap on simultaneously simulating bodies (`int`, default `4`) to guarantee frame-time budget.
- `ragdoll_delay_s`: Optional delay before physics engagement to allow short death clip blend-in (`float`, default `0.0`).
- `max_settle_timeout_s`: Maximum simulation duration before forced freeze (`float`, default `3.5`).
- `linear_settle_threshold_cm_s`: Linear velocity threshold for sleep ($v < 15.0\text{ cm/s}$).
- `angular_settle_threshold_rad_s`: Angular velocity threshold for sleep ($\omega < 0.25\text{ rad/s}$).
- `impulse_multipliers`: Impulse table per damage source:
  - `Rifle`: $12,000$
  - `Shotgun`: $28,000$
  - `Sniper`: $45,000$
  - `Explosive`: $85,000$
  - `HeavyMelee`: $35,000$
  - `Default`: $15,000$
- `upward_blast_bias_cm`: Upward vertical impulse component for explosions ($15,000$).

### Step 2: Pure Rules Engine (`DeathRagdollRules.h` & `DeathRagdollRules.cpp`)
Location: `Source/CodexTactics/Public/Combat/DeathRagdollRules.h`, `Private/Combat/DeathRagdollRules.cpp`
- **Responsibilities:**
  - `LoadConfig()`: Parses `Content/Data/Combat/death_ragdoll.json` into `FDeathRagdollConfig`.
  - `CalculateImpulse(DamageType, HitDirection, DamageAmount)`: Produces kinetic impulse vector with upward blast bias.
  - `HasBodySettled(LinearVelocity, AngularVelocity, DurationSeconds)`: Pure math check against thresholds.
  - `CanActivateRagdoll(ActiveRagdollCount)`: Budget gate ($< 4$ active ragdolls).

### Step 3: Hard Death State Guards Across Auxiliaries
Prevent post-mortem animation and logic execution:
1. **Knockdown Guard (`Combat/KnockdownComponent.cpp`):**
   - In `TryKnockDown()` and `ForceKnockDown()`: Immediately return `false` if `OwnerHealth && !OwnerHealth->IsAlive()`.
   - In `CanFallNow()`: Ensure `Health && !Health->IsAlive()` returns `false` before any distance/chance calculations.
   - Cancel all scheduled timers (`GetUpTimer`, `DownTimer`) if death occurs.
2. **Suppression Subsystem (`Combat/SuppressionSubsystem.cpp`):**
   - Guard against accumulating suppression or triggering suppression flinch on dead targets.
3. **Status Effects (`Combat/HealthComponent.cpp`):**
   - Reject application of `Stagger`, `Burning`, or `Frozen` status effects when `!IsAlive()`.
4. **Enemy Animation Blueprint (`Characters/EnemyAnimInstance.cpp`):**
   - In `NativeUpdateAnimation()`: If `bIsDead` is true, suppress locomotion graph re-evaluation to prevent blending back to idle.

### Step 4: Death Ragdoll Activation in Character Death Handlers
In `AEnemyCharacter::HandleDied()` (`EnemyCharacterHealth.cpp`) and `AOperativeCharacter::HandleDied()` (`OperativeCharacterHealth.cpp`):
1. Immediately halt movement: `CharacterMovement->StopMovementImmediately()`, `CharacterMovement->DisableMovement()`.
2. Stop AI controller: `Controller->StopMovement()`, `Controller->UnPossess()`.
3. Disable capsule collision: `CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision)`, `CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Ignore)`.
4. Inspect mesh: If `Mesh->GetPhysicsAsset()` is valid AND `DeathRagdollRules::CanActivateRagdoll()`:
   - Configure mesh:
     ```cpp
     Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
     Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
     Mesh->SetAllBodiesSimulatePhysics(true);
     Mesh->WakeAllRigidBodies();
     Mesh->bBlendPhysics = true;
     ```
   - Apply kinetic impulse to hit bone (or `pelvis` as fallback):
     ```cpp
     const FVector Impulse = DeathRagdollRules::CalculateImpulse(AttackerSource, LastHitDir, LastDamage);
     Mesh->AddImpulse(Impulse, HitBone, true);
     ```
   - Set `bIsRagdollActive = true`.
5. Fallback path (Spitter, Drone, or when active ragdoll budget is exceeded):
   - Fall back to death montage or `LayCorpseDown()` 90° pitch tip.
6. Register with `UCorpseSubsystem` for settlement monitoring.

### Step 5: Settle, Sleeping, and Freezing in `UCorpseSubsystem`
In `UCorpseSubsystem::TickCorpse(FCorpseEntry& Entry, float DeltaTime)`:
1. For active ragdoll corpses:
   - Query root bone (pelvis) velocity: `Mesh->GetPhysicsLinearVelocity()`, `Mesh->GetPhysicsAngularVelocity()`.
   - If `DeathRagdollRules::HasBodySettled(...)` evaluates true OR `Entry.Age >= max_settle_timeout_s`:
     - Put physics to sleep: `Mesh->PutAllRigidBodiesToSleep()`.
     - Disable active simulation: `Mesh->SetAllBodiesSimulatePhysics(false)`.
     - Switch collision to `QueryOnly` (blocks vision traces for looting/searching, ignores `ECC_Pawn` so squad paths are unaffected).
     - Mark corpse frozen: `Entry.bFrozen = true`.
     - Unregister from active tick $\to$ **Zero CPU overhead per settled corpse**.

---

## 4. Technical Risks & Mitigations

| Risk | Impact | Concrete Mitigation |
| :--- | :--- | :--- |
| **1. Physics Jitter / Bone Explosion** | High (Visual glitch / physics blowout) | Clamp maximum linear impulse to $120,000\text{ g}\cdot\text{cm/s}$; enforce angular damping ($1.5$) in `death_ragdoll.json`; apply impulse to `pelvis` rather than distal extremity bones. |
| **2. CPU Spike During Mass Casualties** | High (Hitch / frame-rate drop $> 250\text{ ms}$) | Strict budget cap: maximum $4$ simultaneous active ragdoll simulations. 5th+ simultaneous fatal hit falls back smoothly to standard death clip / `LayCorpseDown()`. |
| **3. Navigation Mesh Path Blocking** | Medium (Operatives getting stuck on dead bodies) | Once settled, corpse collision profile is switched to `QueryOnly`, ignoring `ECC_Pawn`. Operatives and enemies can walk directly past or over corpses without path obstruction. |
| **4. Ladder / Rooftop Death Anomalies** | Medium (Corpse floating in air off ledge) | If unit dies while airborne or falling off ladder, let `ULedgeFallComponent` continue downward trajectory to ground before activating ragdoll on floor impact. |
| **5. Knockdown Race Condition** | High (Corpse standing up after death) | Hard guard in `UKnockdownComponent::CanFallNow()` and `TryKnockDown()` checking `Health && !Health->IsAlive()`. Clear get-up timers on death. |

---

## 5. Automated Verification & Test Strategy

### 5.1. Unit Test Suite (`Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`)
- `DeathRagdollConfigLoads`: Verifies `death_ragdoll.json` parses with valid numbers.
- `ImpulseCalculationByDamageType`: Verifies Sniper vs Rifle vs Explosive impulse vectors.
- `SettleDetection`: Verifies $v < 15\text{ cm/s}$ and $\omega < 0.25\text{ rad/s}$ triggers settle.
- `SimultaneousBudgetCap`: Verifies 5th concurrent kill request correctly triggers fallback flag.

### 5.2. In-Engine Smoke Test (`DeathRagdollSmokeCommand`)
New smoke command registered in `Scripts/test_map.json`:
1. Spawns each Depot archetype (`FrostHound`, `Frostbitten`, `MilitaryZombie`, `Brute`, `Cutter`, `Marksman`, `PMC Operator`).
2. Applies fatal rifle shot $\to$ asserts ragdoll activates and character collapses.
3. Waits until settled $\to$ asserts pelvis height $Z < 35\text{ cm}$ above floor (lying flat, **NEVER standing**).
4. Fires RPG blast into a group of 6 zombies $\to$ asserts:
   - Exactly $\le 4$ simulate physics simultaneously.
   - All 6 end lying flat on the ground.
   - Zero get-up animations trigger.
   - Max frame time during blast and settle stays $< 250\text{ ms}$.
   - All corpses transition to `bFrozen == true` with 0 ticking components.

---

## 6. Implementation Deliverables Checklist for Developer

- [ ] Create `Content/Data/Combat/death_ragdoll.json`
- [ ] Create `Source/CodexTactics/Public/Combat/DeathRagdollRules.h` & `Private/Combat/DeathRagdollRules.cpp`
- [ ] Add `!Health->IsAlive()` guards in `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp`
- [ ] Integrate ragdoll simulation activation in `Source/CodexTactics/Private/Characters/EnemyCharacterHealth.cpp`
- [ ] Implement settle & sleep transition in `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp`
- [ ] Author unit tests in `Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`
- [ ] Implement `DeathRagdollSmokeCommand` in `Source/CodexTacticsDev/Private/Debug/`
- [ ] Verify `build.ps1`, `test.ps1 -Smart`, and capture before/after visual evidence.
