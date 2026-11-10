# Diagnostic Report: Bug #43 (Squad in Column Does Not React to Corner PMC Fire)

**Issue Summary:**
When the player squad moves in column formation through a narrow corridor between buildings on Depot (`L_Gen_Depot_1`), a hostile PMC operator positioned around a corner opens fire on the squad (often producing floating `MISS!` indicators). Despite being in AGG (Aggressive) combat stance, the squad members do not return fire, take cover, or pivot towards the threat, only reacting after they physically exit the narrow corridor.

---

## 1. Exact Files, Functions, and Line Numbers

1. **`Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp`**
   - **Function:** `UTacticalSightSubsystem::NotifyFired(AOperativeCharacter* Shooter, const FVector& MuzzleLoc, const FVector& ImpactLoc, float Loudness)`
   - **Lines:** `367–396`
   - *Detail:* Handles gunfire revelation. Line 377 iterates over `HostileStates` to reveal shooter, but does not register directional muzzle alerts for friendly squad members blocked by corner geometry.

2. **`Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp`**
   - **Function:** `AOperativeCharacter::EvaluateShotLine(AActor* Target, EShotLineHit& OutHit)`
   - **Lines:** `73–153` (specifically line `127`)
   - *Detail:* Line `127`: `World->LineTraceSingleByChannel(Hit, GetMuzzleLocation(), EnemyFeet + 80.f, ECC_Visibility, Params)`. Returns `EShotLineHit::Blocked` when the building corner intersects the trace, immediately rejecting the target for combat engagement.
   - **Function:** `AOperativeCharacter::TickCombat(float DeltaTime)`
   - **Lines:** `650–710`
   - *Detail:* Gated on `EvaluateShotLine` returning `Direct` or `CoverPass`. If blocked, combat fire is completely suppressed.

3. **`Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp`**
   - **Function:** `AOperativeCharacter::UpdateCombatFacing(float DeltaTime)`
   - **Lines:** `78–185` (specifically lines `144–154` and `159–165`)
   - *Detail:* Evaluates `ThreatFacingRules::FFacts`. Line `146`: `Facts.bStanding = !bHasMoveOrder;`. Because the squad is executing a column move order (`bHasMoveOrder == true`), `bStanding` is false, and character facing is strictly clamped to `SmoothedVelocity.Rotation().Yaw` or `FormationLookYaw`. Threat rotation is ignored during active movement.

4. **`Source/CodexTactics/Private/Characters/OperativeCharacterInternal.h`**
   - **Function:** `AddShotLineAllies(FCollisionQueryParams& Params, const AOperativeCharacter* Self)`
   - **Lines:** `119–136`
   - *Detail:* Collects actors to ignore during shot-line calculation. Iterates through all `AOperativeCharacter` in the world; if team filtering is ambiguous, leading column allies or corner obstacles block line-of-fire validation.

5. **`Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp`** & **`OperativeCharacterCombat.cpp`**
   - **Lines:** `OperativeCharacterCombat.cpp:857` (spawns floating `MISS!` above shooter).
   - **Lines:** `OperativeCharacterHealth.cpp:264–312` (`TakeHit`).
   - *Detail:* When enemy shots miss the squad, no incoming fire suppression or "Under Fire" alert event is propagated to the squad controller.

---

## 2. Ranked Hypotheses with Code Evidence

### Hypothesis 1: Physical Line Trace Blocked by Corridor Corner Geometry (Rank 1 - Primary Cause)
- **Evidence:** In `OperativeCharacterCombat.cpp:127`:
  ```cpp
  World->LineTraceSingleByChannel(Hit, GetMuzzleLocation(), EnemyFeet + 80.f, ECC_Visibility, Params);
  ```
  In a narrow corridor, the corner of the building sits directly between the trailing operatives in the column and the PMC around the corner. `LineTraceSingleByChannel` hits the static mesh corner (`ECC_Visibility`). `EvaluateShotLine` sets `OutHit = EShotLineHit::Blocked` and returns `false`.
  In AGG mode, target acquisition requires a viable shot line. Because the corner occludes the direct ray to the target's center-of-mass, the target is rejected as an engagement candidate. Operatives will not stop to engage a target they have no line of sight to, continuing to march until they clear the corner threshold.

### Hypothesis 2: Move Order Overrides Threat Facing & Absence of "Under Fire" Reaction (Rank 2)
- **Evidence:** In `OperativeCharacterAim.cpp:144–154`:
  ```cpp
  ThreatFacingRules::FFacts Facts;
  Facts.bStanding = !bHasMoveOrder;
  ```
  When the squad is moving down the alleyway in column formation, `bHasMoveOrder` is `true`. `ThreatFacingRules` disables idle threat facing while moving, forcing operatives to look strictly along their movement vector (`SmoothedVelocity.Rotation().Yaw`).
  Furthermore, when the enemy PMC fires and misses, `OperativeCharacterCombat.cpp:857` only renders the floating text `MISS!` at the enemy's muzzle. There is no `NotifyUnderFire` or incoming suppression hook that interrupts the movement order, forces a halt, or commands operatives to turn their heads/weapons towards the source of incoming fire.

### Hypothesis 3: Gunfire Muzzle Revelation Scoped by Fog of War Occlusion (Rank 3)
- **Evidence:** In `TacticalSightSubsystem.cpp:367–396`, `NotifyFired` reveals the shooter by calling `SetOperativeConcealed(*Shooter, State, false)`. However, Fog of War visibility checks (`SightRules::EvaluateVisibility`) require an unoccluded ray from the observer to the shooter. If the corridor corner occludes the PMC's torso, the shooter remains hidden by the Fog of War for trailing operatives who have not yet reached the corner, preventing target acquisition by the squad's combat loop.

---

## 3. How to Verify Each Hypothesis

### Automated Smoke Tests (`Scripts/test_map.json`)
- **`PmcStandoffSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke PmcStandoffSmoke`
  - Validates squad reaction and target exchange when facing hostile PMC squads at standoff distances.
- **`PmcTeamSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke PmcTeamSmoke`
  - Tests team sight sharing and mutual combat engagement between player operatives (`TeamId 0`) and PMC operatives (`TeamId 1`).
- **`SightConditionsSmoke`**:
  - Command: `powershell Scripts/test.ps1 -Smoke SightConditionsSmoke`
  - Tests vision cones, occlusion by corners, and muzzle flash revelation in obscured environments.

### Console Variables and Debug Logging
- **Console Variables:**
  - `Codex.Debug.FireTrace 1`: Visualizes muzzle-to-target traces in green (clear) and red (blocked by wall corner).
  - `Codex.Sight 1` / `Codex.Sight.LongRange 1`: Visualizes vision cones and reveals sight status of concealed hostiles.
  - `Codex.Debug.Facing 1`: Shows current facing vector vs target facing vector for moving operatives.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Verbose: EvaluateShotLine: Blocked by StaticMeshActor [BuildingCorner]`
  - `LogCodexTactics: Verbose: UpdateCombatFacing: Move order active, threat facing suppressed`
  - `LogCodexTactics: Display: [Sight] Shooter %s revealed by gunfire`

---

## 4. Smallest Proposed Fix (Description Only)

1. **Dispatch Incoming Fire Alert on Miss / Near-Hit:**
   In `OperativeCharacterCombat.cpp` (where `MISS!` is spawned) or `TacticalSightSubsystem::NotifyFired`, broadcast an `OnIncomingFire(ShooterLocation, ShooterTeam)` event to nearby friendly operatives (e.g. within 35–50 m).
2. **Prioritize Threat Facing Under Fire During Column Movement:**
   In `OperativeCharacterAim.cpp` (`UpdateCombatFacing`), introduce a transient `bUnderFire` state (lasting 2–3 seconds upon receiving incoming fire). If `bUnderFire` is active, allow upper-body aim offset or head look-at to pivot towards the incoming fire angle even while `bHasMoveOrder` is true, or allow AGG stance to automatically halt movement to return suppressive fire.
3. **Ghost Contact Marker for Blind Corners:**
   When gunfire is heard around an occluding corner, register a "Ghost Contact" at the corner intersection in `TacticalSightSubsystem`, enabling trailing squad members to pre-aim and prepare cover maneuvers before stepping into the crosshairs.
