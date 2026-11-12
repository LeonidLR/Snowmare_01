# Implementation Map #41 — In-Game QA Sensors (Runtime Watchdogs for Codex)

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-12  
**Target:** Task #41 (`pl-qa-sensors`) — Внутриигровые датчики качества (In-Game QA Sensors) для ночного автотестирования  
**Target Assignee:** Codex / Claude  
**Governing Contracts:** `docs/design/AUTO_QA.md`, `Scripts/Tools/morning_qa_report.py`, `Scripts/bot_run.ps1`, `docs/port/TASK_BOARD.md` §41

---

## 1. System Architecture & Incident Reporting Pipeline

The Autonomous QA Watchdog operates as a non-intrusive runtime diagnostic layer embedded into existing subsystems. When any sensor trips (with per-sensor cooldown `SameSensorCooldown = 10.0 s` and cap `MaxIncidentsPerRun = 25` per `AUTO_QA.md` §6):
1. **Engine Log Line (required by `Scripts/Tools/morning_qa_report.py:134`):**
   `morning_qa_report.py` parses engine logs using `\b(MON_[A-Z_]+)\b[:\s]+(.*)`. Emit:
   `UE_LOG(LogCodexTactics, Warning, TEXT("%s: %s at %s (%s)"), *SensorId, *ActorName, *Location.ToString(), *Details);`
   e.g. `LogCodexTactics: MON_STUCK: BP_Operative_Medic_C_1 at X=... Y=... Z=... (Stuck 4.1s, delta < 5 cm)`.
2. **Incident Bundle (`docs/design/AUTO_QA.md` §3):** Writes `Saved/QA/Incidents/<Date>_<Time>_<SensorID>_<Seed>/incident.json` (`incident_id`, `sensor` = `"MON_*"`, `severity`, `level_id`, `seed`, `location`, `actor`, `order`, `target_location`, `active_subsystems`, `engine_version`) + optional `screenshot.png` / `slice.log`, and appends structured JSON lines into `Saved/Automation/QA_Sensors.jsonl`.
3. In interactive debug builds (`-CodexDebugSensors`), draws a 3D debug sphere/label at the actor coordinates for `10.0 s`.

---

## 2. Canonical Sensor Matrix (`docs/design/AUTO_QA.md` §2 & `morning_qa_report.py`)

| Sensor ID | Monitored Condition | Trigger Threshold | Insertion Point (`Source/CodexTactics/Private/`) |
|---|---|---|---|
| **`MON_STUCK`** | Pawn has active move order on NavMesh but horizontal displacement `< 5.0 cm` | Duration `> 4.0 s` | `Characters/OperativeAIController.cpp`, `Characters/EnemyAIController.cpp` |
| **`MON_OFF_NAVMESH`** | Pawn center of mass outside valid NavMesh bounds | Distance to nearest nav surface `> 60.0 cm` | `Characters/OperativeCharacterMovement.cpp` |
| **`MON_FIRE_ARC`** | Weapon fired outside barrel-to-target limit (#33) | Residual angle `> 30.0°` (prone `> 15.0°`) | `Characters/OperativeCharacterCombat.cpp`, `Characters/FireArcRules.cpp` |
| **`MON_WEAPON_REACH`** | Shot attempted beyond `attack_range_m` (#39) | Distance `> attack_range_m * 100 cm` | `Characters/OperativeCharacterCombat.cpp`, `Combat/WeaponRangeRules.cpp` |
| **`MON_COVER_EXPOSURE`** | Operative with `bInCover == true && !bIsPeeking` taking unmitigated hits | Direct line trace hits center without wall hit | `Combat/TacticalSightSubsystem.cpp`, `Characters/OperativeCharacterCover.cpp` |
| **`MON_PERF_SPIKE`** | Frame CPU/GPU time or Vision Cone update overrun | Frame `> 33.3 ms` or Vision Cone `> 12.0 ms` | `Combat/VisionConeComponent.cpp` (perf instrumentation) |
| **`MON_FALL_THROUGH`** | Pawn Z drops below lowest map floor geometry | `Pawn Z < (Map Floor Min Z - 200.0 cm)` | `Falls/LedgeFallComponent.cpp`, `Characters/OperativeCharacterFall.cpp` |
| **`MON_T_POSE`** | Default bind pose / frozen locomotion while moving | Velocity `> 50.0 cm/s` & `AnimSpeed == 0` for `> 1.5 s` | `Characters/OperativeAnimInstance.cpp` |
| **`MON_ORDER_STALL`** | Move/Action order issued with no state progression | Elapsed `> 12.0 s` without arrival or refusal | `Core/CodexTacticsPlayerControllerOrders.cpp` |
| **`MON_LOG_ERROR`** | Engine `Error` / watched `Warning` emitted | Immediate trigger on regex match | Custom `FOutputDevice` monitor |

---

## 3. Sensor Details & Insertion Snippets

### 3.1. `MON_STUCK` (Pawn Stuck / Blocked Motion)
- **Condition (`AUTO_QA.md`):** Active Move order on NavMesh, excluding valid holds (suppression, paused, dead/dying):
  `HasActiveMoveOrder && !bIsSuppressed && !bIsPaused && !bIsDying && Dist2D(CurrentLoc, AnchorLoc) < 5.0f && Elapsed >= 4.0f`
- **Insertion:** `AOperativeAIController` / `AEnemyAIController` (`Source/CodexTactics/Private/Characters/OperativeAIController.cpp`, `EnemyAIController.cpp`) or `AOperativeCharacter::Tick`:
  ```cpp
  if (bHasActiveMoveGoal && !bIsSuppressed && !bIsDying)
  {
      if (FVector::Dist2D(GetActorLocation(), LastTrackedLocation) < 5.f)
      {
          StuckAccumulator += DeltaSeconds;
          if (StuckAccumulator > 4.0f && !bStuckReported)
          {
              UQASensorSubsystem::ReportIncident(TEXT("MON_STUCK"), this, FString::Printf(TEXT("delta < 5 cm for %.1fs at goal %s"), StuckAccumulator, *CurrentGoalLocation.ToString()));
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

### 3.2. `MON_OFF_NAVMESH` (NavMesh Boundary Breach)
- **Condition (`AUTO_QA.md`):** Distance from pawn center of mass to nearest valid NavMesh surface `> 60.0 cm` (excluding `MOVE_Falling`, ladder climb, drop link traverse, or vaulting).
- **Insertion:** `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp` — `CheckNavMeshSanity()` on a `1.0 s` timer using `UNavigationSystemV1::ProjectPointToNavigation(FeetLoc, NavLoc, FVector(60.f, 60.f, 150.f))`.

### 3.3. `MON_FIRE_ARC` & `MON_WEAPON_REACH` (Combat Angle & Range Anomalies)
- **Condition (`AUTO_QA.md`):**
  - `MON_FIRE_ARC`: residual angle `|Yaw_aim - Yaw_facing| > 30.0°` (standing/crouching) or `> 15.0°` (prone) when firing a shot.
  - `MON_WEAPON_REACH`: shot attempted at `Dist3D(Shooter, Target) > attack_range_m * 100.f`.
- **Insertion:** `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp` (backed by pure checks in `FireArcRules.cpp` and `WeaponRangeRules.cpp`) at shot commit.

### 3.4. `MON_COVER_EXPOSURE` ("In Cover but Visible")
- **Condition (`AUTO_QA.md`):** `bInCover == true && !bIsPeeking`, yet shot/LOS trace from enemy to operative capsule center succeeds without hitting `ECC_WorldStatic` cover geometry.
- **Insertion:** `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp` & `Characters/OperativeCharacterCover.cpp`.

### 3.5. `MON_FALL_THROUGH` (Sub-Floor Collision Breach)
- **Condition (`AUTO_QA.md`):** `ActorZ < (MapFloorMinZ - 200.0 cm)`.
- **Action:** Log & record `MON_FALL_THROUGH` (categorized `BLOCKER` in `morning_qa_report.py:139`) and recover to `LastSafeNavLocation`.
- **Insertion:** `Source/CodexTactics/Private/Falls/LedgeFallComponent.cpp` / `Characters/OperativeCharacterFall.cpp`.

### 3.6. `MON_T_POSE` (Animation Freeze While Moving)
- **Condition (`AUTO_QA.md`, `morning_qa_report.py:302`):** Pawn ground velocity `> 50.0 cm/s` while `UOperativeAnimInstance` remains in default pose / `AnimSpeed == 0` for `> 1.5 s`.
- **Insertion:** `Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp`.

### 3.7. `MON_ORDER_STALL` (Order Queue Stall)
- **Condition (`AUTO_QA.md`):** Active Move/Action order elapsed `> 12.0 s` without arrival, progress, or refusal.
- **Insertion:** `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp`.

### 3.8. `MON_PERF_SPIKE` & `MON_LOG_ERROR`
- **Conditions (`AUTO_QA.md`):** `MON_PERF_SPIKE` on frame time `> 33.3 ms` or Vision Cone update `> 12.0 ms` (`Combat/VisionConeComponent.cpp`); `MON_LOG_ERROR` via engine log listener when `Error` / tracked `Warning` fires.

---

## 4. Telemetry Schema (`incident.json` per `AUTO_QA.md` §3)

```json
{
  "incident_id": "INC_20261112_031415_MON_COVER_EXPOSURE_01",
  "sensor": "MON_COVER_EXPOSURE",
  "severity": "CRITICAL",
  "level_id": "L_Gen_Depot_1",
  "seed": 1042,
  "location": [ -1450.2, 320.5, 45.0 ],
  "actor": "BP_Operative_Engineer_C_1",
  "order": "MoveTo",
  "target_location": [ -1200.0, 320.0, 45.0 ],
  "active_subsystems": [ "SquadSubsystem", "TacticalSightSubsystem" ],
  "engine_version": "5.8.0-Codex"
}
```

---

## 5. Verification Tests for Codex

- `FQASensorStuckTriggerTest`: Forces a pawn with an active Move order to move `< 5.0 cm` horizontally for `4.1 s` on NavMesh -> verifies `MON_STUCK` triggers and logs `LogCodexTactics: MON_STUCK: ...` (and `< 5 cm` threshold respected when moving `6 cm`).
- `FQASensorOffNavmeshTest`: Places pawn `> 60 cm` off NavMesh -> asserts `MON_OFF_NAVMESH` triggers.
- `FQASensorFireArcTest`: Fires with `35°` offset standing / `20°` offset prone -> verifies `MON_FIRE_ARC` triggers (`test_morning_qa_report.py:44` parity).
- `FQASensorFallFloorTest`: Places pawn at `Z < MapFloorMinZ - 200 cm` -> asserts `MON_FALL_THROUGH` triggers and recovers safely.
- `FQASensorOrderStallTest`: Queues stalled Move/Action order for `> 12.0 s` without progression -> asserts `MON_ORDER_STALL` triggers.
- Verify output log parses cleanly with `Scripts/Tools/morning_qa_report.py` (`python -m unittest Scripts/Tools/tests/test_morning_qa_report.py`).
