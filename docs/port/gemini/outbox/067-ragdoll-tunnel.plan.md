# Plan 067: Death Ragdoll Tunneling Prevention & Impulse Tuning

- **Task:** 067 - Ragdoll: bodies sink into the ground + impulse too strong
- **Branch:** `gemini/067-ragdoll-tunnel`
- **Worktree:** `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim`

---

## 1. Diagnosis & Investigation of `CorpseSubsystem.cpp` (Line 370-380)

### What was found at Line 370-380:
```cpp
if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Char))
{
    if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
    {
        const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
        if (Mesh->GetRelativeLocation().Z > -HalfHeight * 0.5f)
        {
            Mesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 15.f));
        }
    }
}
```

### Root Cause Analysis:
1. **Capsule-based snap pushes settled bodies underground:**
   - When ragdoll simulates physics, the mesh bodies settle naturally in world space on the collision geometry/floor.
   - The Character's root CapsuleComponent remains at its death location/height.
   - In `CorpseSubsystem.cpp:375`, if `Mesh->GetRelativeLocation().Z > -HalfHeight * 0.5f` (e.g. for a standard zombie with `HalfHeight = 90cm`, `-HalfHeight * 0.5f = -45cm`), the subsystem forcibly executed `Mesh->SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 15.f))` (`-75cm`).
   - This shoved the settled mesh downwards by ~30–50 cm directly into the snow/terrain, and forcibly reset `(X, Y)` to `(0, 0)`, teleporting the mesh horizontally back to capsule center.
   - In the 056b smoke test report, Brute pelvis settled at `-11.3 cm` (underground) directly due to this offset.

2. **Resolution:**
   - Remove the capsule-based `SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 15.f))` for settled ragdolls. A settled ragdoll body must stay exactly where physics placed it.
   - Before freeze: Log pelvis world Z vs floor trace.
   - After freeze: Verify pelvis world Z vs floor trace.
   - Add safety net: If pelvis settles > 20 cm below the floor (`PelvisZ < FloorZ - 20.0f`), lift the mesh along Z by `FloorZ - PelvisZ` so the pelvis rests on the floor, and log a warning once per corpse.

---

## 2. Proposed Numbers & Configuration

### New Keys in `death_ragdoll.json` & `FDeathRagdollConfig`:
- `max_launch_speed_cm_s`: `600.0` (Hard linear speed clamp for normal damage, e.g. Kinetic/Melee/Fire/Cryo/Energy)
- `max_launch_speed_explosive_cm_s`: `900.0` (Hard linear speed clamp for Explosive damage)

### Impulse Tuning Adjustments in `death_ragdoll.json`:
- `max_impulse_magnitude`: Lowered from `120000.0` to `60000.0` (For an 80kg body, 60,000 corresponds to DeltaV = 750 cm/s; prevents physics engine explosive launches).
- `impulse_scales.Explosive`: Lowered from `100000.0` to `50000.0` (With damage factor up to 1.5x–2.0x, yields 50,000–75,000, clamped by 60,000 and 900 cm/s hard speed cap).
- `impulse_scales.Kinetic`: Lowered from `45000.0` to `35000.0` (Rifle hits feel responsive without catapulting corpses).

---

## 3. Implementation Steps

### Step 1: Pure Evaluation Rules (`DeathRagdollRules` & `DeathRagdollTypes`)
- Update `FDeathRagdollConfig`:
  - Add `float MaxLaunchSpeedCmS = 600.0f;`
  - Add `float MaxLaunchSpeedExplosiveCmS = 900.0f;`
- Update `death_ragdoll.json` with new keys and lowered impulse numbers.
- In `FDeathRagdollRules`:
  - Add `static float GetMaxLaunchSpeed(EDamageType DamageType, const FDeathRagdollConfig& Config);`
  - Add `static FVector ClampLinearVelocity(const FVector& Velocity, float MaxSpeed);`
  - Add `static float CalculatePelvisFloorCorrection(float PelvisZ, float FloorZ, float MaxSinkCm = 20.0f);`
- Deserialization in `LoadConfigFromJson` to read `max_launch_speed_cm_s` and `max_launch_speed_explosive_cm_s`.

### Step 2: Unit Tests (`DeathRagdollRulesTest`)
- Add `CodexTactics.Combat.DeathRagdoll.SpeedClamp`:
  - Clamping linear velocity for Kinetic (600 cm/s) and Explosive (900 cm/s).
  - Sub-threshold velocities remain untouched.
- Add `CodexTactics.Combat.DeathRagdoll.PelvisFloorCorrection`:
  - Pelvis > 20 cm below floor: returns positive lift distance.
  - Pelvis within 20 cm or above floor: returns 0.0f.
- Update `CodexTactics.Combat.DeathRagdoll.ConfigLoads` to verify new parameters.

### Step 3: Subsystem Integration (`CorpseSubsystem`)
1. **CCD (Continuous Collision Detection):**
   - In `TryActivateDeathRagdoll`: Call `Mesh->SetAllUseCCD(true);` when starting ragdoll simulation.
   - In `UpdateSettling` freeze branch: Call `Mesh->SetAllUseCCD(false);` when freezing corpse.
2. **Launch Speed Clamp & Multi-Shooter Impulse Guard:**
   - In `TryActivateDeathRagdoll`: Check if `Mesh->IsSimulatingPhysics()` or if the character is already registered in `Corpses`. If already active/registered, return `false` immediately so impulse is applied strictly ONCE even if multiple hits land simultaneously.
   - Right after `Mesh->AddImpulse(Impulse, PelvisBone, false);`, iterate all simulating bodies on the mesh (via `Mesh->Bodies` and `Mesh->SetPhysicsLinearVelocity`) and clamp linear velocity to `GetMaxLaunchSpeed(DamageType, RagdollConfig)`.
3. **Freeze Path & Floor Safety Net:**
   - Remove the capsule-based `SetRelativeLocation(FVector(0.f, 0.f, -HalfHeight + 15.f))`.
   - Log pelvis world Z vs floor trace before freeze.
   - Call freeze physics: `PutAllRigidBodiesToSleep()`, `SetAllBodiesSimulatePhysics(false)`, `SetCollisionProfileName(TEXT("NoCollision"))`, `bPauseAnims = true`, `SetComponentTickEnabled(false)`.
   - Check pelvis world Z vs floor trace after freeze.
   - If `PelvisZ < FloorZ - 20.0f`, call `Mesh->AddWorldOffset(FVector(0.f, 0.f, FloorZ - PelvisZ), false, nullptr, ETeleportType::TeleportPhysics);` and log warning.

### Step 4: Smoke Test Extension (`DeathRagdollSmokeCommand`)
- Case (a): Target killed while moving at full sprint speed: verify corpse settles naturally and pelvis is within 20 cm of floor trace.
- Case (b): Multi-hit target (3 kinetic hits + 1 explosive blast simultaneously): verify impulse applied once, max body speed <= `MaxLaunchSpeedExplosiveCmS` (900 cm/s).

---

## 4. Affected Files
- `Content/Data/Combat/death_ragdoll.json`
- `Source/CodexTactics/Public/Combat/DeathRagdollTypes.h`
- `Source/CodexTactics/Public/Combat/DeathRagdollRules.h`
- `Source/CodexTactics/Private/Combat/DeathRagdollRules.cpp`
- `Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp`
- `Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp`
- `Source/CodexTacticsDev/Private/Debug/DeathRagdollSmokeCommand.cpp`
- `docs/port/gemini/outbox/067-ragdoll-tunnel.plan.md`
