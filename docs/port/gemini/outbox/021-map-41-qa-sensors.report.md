# Implementation Map #41 — In-Game QA Sensors (Runtime Watchdogs for Codex)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-12  
**Target:** Task #41 (`pl-qa-sensors`) — Внутриигровые датчики качества (In-Game QA Sensors) для ночного автотестирования  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/design/AUTO_QA.md`, `Scripts/bot_run.ps1`, `docs/port/TASK_BOARD.md` §41

---

## 1. System Architecture & Incident Reporting Pipeline

The Autonomous QA Watchdog operates as a non-intrusive runtime diagnostic layer embedded into existing subsystems. When any sensor trips:
1. It records a structured telemetry event into `Saved/Automation/QA_Sensors.jsonl`.
2. Emits an engine log warning: `[QA_SENSOR][<SENSOR_ID>] <ActorName> at <Coordinates>: <Details>`.
3. In interactive debug builds (`-CodexDebugSensors`), draws a 3D debug billboard or sphere in world space for $10.0\text{ s}$.
4. Captures an incident metadata packet for the morning aggregator (`AUTO_QA.md`).

---

## 2. Sensor 1: `SENSOR_STUCK` (Pawn Stuck / Blocked Motion)

### 2.1. Monitored Condition & Mathematical Trigger
A pawn is considered stuck if it is under an active movement or path traversal order, but fails to make physical displacement over a sustained observation window, excluding valid gameplay holds (suppression, tactical pause, cover wait).

$$\text{Trigger if: } (\text{HasActiveMoveOrder}) \land (\Delta \vec{x}_{\text{horizontal}} < 15.0\text{ cm}) \land (t_{\text{elapsed}} \ge 4.0\text{ s}) \land (\neg \text{bSuppressed}) \land (\neg \text{bPaused})$$

### 2.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Characters/OperativeCharacter.cpp` (and `EnemyCharacter.cpp`).
- **Function:** `AOperativeCharacter::Tick(float DeltaSeconds)`.
- **Implementation:**
  ```cpp
  if (bHasActiveMoveGoal && !bIsSuppressed && !bIsDying)
  {
      if (FVector::Dist2D(GetActorLocation(), LastTrackedLocation) < 15.f)
      {
          StuckAccumulator += DeltaSeconds;
          if (StuckAccumulator >= 4.0f && !bStuckReported)
          {
              UQASensorSubsystem::ReportIncident(EQASensor::Stuck, this, FString::Printf(TEXT("Stuck for %.1fs at goal %s"), StuckAccumulator, *CurrentGoalLocation.ToString()));
              bStuckReported = true;
          }
      }
      else
      {
          LastTrackedLocation = GetActorLocation();
          StuckAccumulator = 0.f;
          bStuckReported = false;
      }
  }
  ```

---

## 3. Sensor 2: `SENSOR_OFF_NAVMESH` (NavMesh Boundary Breach)

### 3.1. Monitored Condition & Mathematical Trigger
A living pawn has moved or been pushed outside the navigable volume, such that no valid NavMesh polygon exists directly beneath or within immediate vertical/horizontal reach.

$$\text{Trigger if: } \text{ProjectPointToNavigation}(\vec{p}_{\text{feet}}, \text{Extent} = [60, 60, 150]\text{ cm}) == \text{false}$$

### 3.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp`.
- **Function:** Called every $1.0\text{ s}$ via timer `AOperativeCharacter::CheckNavMeshSanity()`.
- **Mitigation against False Positives:** Ignored while pawn is in falling / ladder climbing / vaulting states (`EMovementMode::MOVE_Falling` or `bIsClimbingLadder == true`).

---

## 4. Sensor 3: `SENSOR_FIRE_OUTSIDE_ARC` (Fire Outside Weapon Sector)

### 4.1. Monitored Condition & Mathematical Trigger
A weapon projectile or hitscan trace is fired toward a target angle that exceeds the biomechanical or stance-allowed rotation limit relative to the pawn's torso/pelvis forward vector without turning the character first.

$$\Delta \theta = |\text{Yaw}_{\text{aim}} - \text{Yaw}_{\text{facing}}| > \theta_{\text{max}}$$
Where:
- $\theta_{\text{max}} = 75.0^\circ$ for Standing / Crouching.
- $\theta_{\text{max}} = 20.0^\circ$ for Prone (strict prone cone per #33).

### 4.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Combat/WeaponComponent.cpp`.
- **Function:** `UWeaponComponent::CommitFireShot(const FVector& TargetLocation)`.
- **Implementation:** Compares `(TargetLocation - Owner->GetActorLocation()).ToOrientationRotator().Yaw` against `Owner->GetActorRotation().Yaw`. If $|\Delta \text{Yaw}| > \text{Limit}$, logs sensor violation with weapon ID and stance.

---

## 5. Sensor 4: `SENSOR_T_POSE` (Animation Evaluation Failure)

### 5.1. Monitored Condition & Mathematical Trigger
A character is moving at speed ($v_{\text{ground}} > 20.0\text{ cm/s}$), but its skeletal mesh evaluates to the default bind pose or T-pose (characterized by straight horizontal arms and zero root motion delta for $> 1.0\text{ s}$).

$$\text{Trigger if: } (v_{\text{ground}} > 20\text{ cm/s}) \land (\text{Angle}(\text{UpperArm\_L}, [0, -1, 0]) < 10^\circ) \land (\text{Angle}(\text{UpperArm\_R}, [0, 1, 0]) < 10^\circ)$$

### 5.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp`.
- **Function:** `UOperativeAnimInstance::NativePostEvaluateAnimation()`.
- **Implementation:** Inspects local bone transforms of `upperarm_l` and `upperarm_r`. If both arms remain locked in orthogonal reference orientation during non-zero ground speed, triggers `SENSOR_T_POSE`.

---

## 6. Sensor 5: `SENSOR_FALL_THROUGH_FLOOR` (Sub-Floor Collision Breach)

### 6.1. Monitored Condition & Mathematical Trigger
A character's vertical coordinate drops below the lowest valid geometry level of the map, or downward velocity exceeds terminal falling limits without floor collision contact.

$$\text{Trigger if: } (Z_{\text{pawn}} < Z_{\text{map\_floor\_min}} - 200.0\text{ cm}) \lor (v_z < -2500.0\text{ cm/s})$$

### 6.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Characters/OperativeCharacter.cpp`.
- **Function:** `AOperativeCharacter::Tick(float DeltaSeconds)`.
- **Action on Trigger:**
  1. Emits `SENSOR_FALL_THROUGH_FLOOR` incident with XYZ coordinates and last safe floor transform.
  2. Auto-Recovery: Teleports pawn to `LastSafeNavLocation` to prevent bot run termination.

---

## 7. Sensor 6: `SENSOR_COVER_LINE_OF_SIGHT_ANOMALY` ("In Cover but Visible")

### 7.1. Monitored Condition & Mathematical Trigger
An operative is officially marked as in full cover (`bInCover == true`), is tucked against the wall (`!bIsPeeking`), yet a hostile line-of-sight trace directly hits the operative's capsule center without intersecting any geometry with collision response `ECC_WorldStatic` or cover tags.

$$\text{Trigger if: } (\text{bInCover}) \land (\neg \text{bIsPeeking}) \land (\text{LineTrace}(\text{EnemyEye}, \text{OperativeCenter}) == \text{OperativeActor})$$

### 7.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp`.
- **Function:** `UTacticalSightSubsystem::EvaluateLoS(AActor* Viewer, AActor* Target)`.
- **Significance:** Detects misaligned cover slots, inverted wall normals, or concave corner geometry clipping where characters visually poke through walls.

---

## 8. Sensor 7: `SENSOR_ORDER_NOT_EXECUTED` (Order Queue Stall)

### 8.1. Monitored Condition & Mathematical Trigger
A command issued by the player controller or playtest bot remains in the operative's active order slot for longer than $12.0\text{ seconds}$ without transitioning to `Completed`, `Cancelled`, or `Failed`, and without any path progress.

$$\text{Trigger if: } (t_{\text{order\_active}} > 12.0\text{ s}) \land (\text{OrderState} == \text{PendingProgress}) \land (\text{DistanceToGoalRemaining} \approx \text{Constant})$$

### 8.2. Insertion Point
- **File:** `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`.
- **Function:** `ACodexTacticsPlayerController::TickOrders(float DeltaSeconds)`.
- **Payload:** Logs issuing controller, assigned operative, order type (Move, Flank, Revive, Defuse), and blocking condition.

---

## 9. Telemetry Schema & Log Format

Each sensor serializes to `Saved/Automation/QA_Sensors.jsonl`:

```json
{
  "timestamp": "2026-11-12T03:14:15.204Z",
  "sensor_id": "SENSOR_COVER_LINE_OF_SIGHT_ANOMALY",
  "severity": "CRITICAL",
  "level": "L_Gen_Depot_1",
  "seed": 1042,
  "actor": "BP_Operative_Engineer_C_1",
  "location": [ -1450.2, 320.5, 45.0 ],
  "details": {
    "wall_actor": "SM_Concrete_Wall_400_12",
    "hostile_actor": "BP_Enemy_Marksman_C_2",
    "distance_cm": 2840.0,
    "slot_index": 2
  }
}
```

---

## 10. Verification Tests for Codex

- `FQASensorStuckTriggerTest`: Artificially forces a pawn with `MoveTo` goal to stay stationary for $4.1\text{ s}$; verifies sensor event is generated.
- `FQASensorOffNavmeshTest`: Teleports test dummy $150\text{ cm}$ above empty void; asserts `SENSOR_OFF_NAVMESH` triggers.
- `FQASensorFireArcTest`: Calls `UWeaponComponent::CommitFireShot` with a $90^\circ$ yaw offset in prone; verifies `SENSOR_FIRE_OUTSIDE_ARC` triggers.
- `FQASensorFallFloorTest`: Disables gravity and teleports pawn to $Z = -5000\text{ cm}$; asserts instant trigger and recovery teleport.
- `FQASensorOrderStallTest`: Queues dummy move order with unreachable destination outside nav bounds; asserts order stall triggers after $12.0\text{ s}$.
