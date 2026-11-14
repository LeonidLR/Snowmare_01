# Implementation Map #17 / #16c — Ladders & Ramps: Mid-Traversal Turning, Combat Stalls, and Slope Velocity

**Author:** Gemini (Architecture & Systems Specialist)  
**Date:** 2026-11-11  
**Target:** Task #17 & #16c (`pl-ramp-ladder-motion`) — Разворот на лестнице и пандусе на ходу, застревание на пандусе при атаке, бег по пандусу  
**Target Assignee:** Codex / Claude  
**Governing Documents:** `docs/port/gemini_reports/diag_17_16c.md`, `docs/design/LADDERS.md`, `docs/systems/squad-and-orders.md` §6, `docs/port/TASK_BOARD.md` §17 & §16c

---

## 1. Tactical Concept & Operational Goal

### 1.1. Problem Breakdown
Vertical transitions across Depot (`L_Gen_Depot_1`) frequently suffer from three acute mechanical friction points:
1. **Mid-Ladder Direction Reversal (#17a):** When a player orders a squad to reverse direction while an operative is halfway up a ladder (`ULadderClimbComponent::OnMoveOrderChanged`), the character sets `bReversePending = true`. If followers are already ascending behind or queueing at the base, head-on deadlock or clipped body passing occurs.
2. **Squad Stuck on Ramps During Combat (#16c):** Ramps restrict movement to single file (`SquadSubsystemRampFile.cpp`). When a firefight erupts while the squad is on a ramp, `SetRampHold` keeps trailing operatives frozen in place behind the leader, unable to return fire, seek lateral cover, or evacuate.
3. **Unrealistic Ramp Sprint Speeds (#17b):** Operatives ascend steep ramps ($15^\circ - 35^\circ$) at identical speed ($550\text{ cm/s}$) as flat terrain, looking physically weightless and skipping animation foot plants.

---

## 2. Existing Classes to Extend vs. New Code

### 2.1. Reusable Engine Components (~85% Reuse)

| Component | File & Line | Reusable Function / Capability |
| :--- | :--- | :--- |
| **Ladder Climb Component** | `Source/CodexTactics/Private/Ladders/LadderClimbComponent.cpp:495-513` | `ULadderClimbComponent::OnMoveOrderChanged` manages `bReversePending` and rung boundaries. |
| **Ladder State Machine** | `Source/CodexTactics/Private/Ladders/LadderRules.cpp:1-120` | `LadderRules::NextRungBoundary` calculates loop times and climb transitions. |
| **Ramp File Management** | `Source/CodexTactics/Private/Characters/SquadSubsystemRampFile.cpp:490-520` | `USquadSubsystem::TickRampFile` updates single-file holds and release conditions. |
| **Ramp Geometry Rules** | `Source/CodexTactics/Private/Formation/RampFileRules.cpp:7-66` | `IsRampSurface` (floor normal Z) and `FindWaitBehind`. |
| **Character Movement Speed** | `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:200-280` | `UpdateGaitSpeed` applies stance, injury, and weather multipliers. |

### 2.2. New Code Required (~15%)

1. **`RampSlopeRules` (Pure Slope Physics Logic):**
   - Header: `Source/CodexTactics/Public/Movement/RampSlopeRules.h`
   - Source: `Source/CodexTactics/Private/Movement/RampSlopeRules.cpp`
   - Calculates incline angle $\theta$ from floor surface normal:
     $$\theta = \arccos(N_z)$$
   - Computes ascent velocity scaling factor:
     $$\text{SpeedScale}_{\text{Ascent}} = \text{Clamp}\left(\cos(\theta) - 0.25 \cdot \sin(\theta),\, 0.60,\, 1.0\right)$$
   - Computes descent sprint stability factor ($0.85$ to prevent excessive runaway momentum).
2. **Combat Break-Out Hook in `SquadSubsystemRampFile.cpp`:**
   - Detects when an operative on a ramp takes damage (`bUnderFire`) or enters active combat.
   - Instantly breaks `Hold.bHeld`, releasing followers from traffic lock and allowing immediate evasive dispersion.
3. **Cascading Ladder Reversal in `LadderClimbComponent`:**
   - When a climber reverses, check whether a follower occupies the rung directly behind him.
   - If occupied, propagate reverse command down the ladder chain: lowest climber reverses first, clearing the rungs.

---

## 3. Data Keys & Tuning

Configured in `Content/Data/Movement/ladder_ramp_tuning.json`:

```json
{
  "ramp_speed_scaling": {
    "enabled": true,
    "min_slope_deg": 12.0,
    "max_slope_deg": 40.0,
    "max_ascent_slowdown": 0.40,
    "descent_sprint_clamp": 0.85
  },
  "ramp_combat_evacuation": {
    "break_hold_on_damage": true,
    "break_hold_on_combat_start": true,
    "evacuation_spread_lateral_cm": 120.0
  },
  "ladder_reversal": {
    "rung_pause_duration_s": 0.25,
    "follower_safety_clearance_rungs": 2,
    "slide_down_on_emergency_reverse": true
  }
}
```

---

## 4. Insertion Points in Existing Code

### 4.1. Slope Speed Modifier in `OperativeCharacterMovement.cpp`
```cpp
float AOperativeCharacter::GetSlopeSpeedMultiplier() const
{
    const FHitResult Floor = GetCharacterMovement()->CurrentFloor.HitResult;
    if (!Floor.bBlockingHit) return 1.0f;

    const FVector FloorNormal = Floor.ImpactNormal;
    const FVector MoveDir = GetVelocity().GetSafeNormal();

    // Ascending: MoveDir points upward (positive Z)
    const bool bAscending = MoveDir.Z > 0.1f;
    return RampSlopeRules::CalculateSlopeSpeedMultiplier(FloorNormal, bAscending, RampTuningConfig);
}
```

### 4.2. Combat Evacuation in `SquadSubsystemRampFile.cpp`
```cpp
void USquadSubsystem::TickRampFile(float DeltaSeconds)
{
    // Fix for Task 16c: Check if combat or incoming fire breaks the file lock
    const bool bCombatActive = IsSquadUnderFire() || (CombatSubsystem && CombatSubsystem->IsInCombat());

    for (FRampCandidate& Candidate : Candidates)
    {
        if (bCombatActive)
        {
            // Release hold immediately so operative can flee or return fire
            SetRampHold(*Candidate.Operative, false, 0, TEXT("Combat emergency evacuation"));
            continue;
        }
        
        // Existing normal traffic hold logic...
    }
}
```

### 4.3. Cascading Ladder Reversal in `LadderClimbComponent.cpp`
```cpp
void ULadderClimbComponent::OnMoveOrderChanged(bool bUp)
{
    if (bUp == (State == ELadderClimbState::ClimbingUp))
    {
        bReversePending = false;
        return;
    }

    // Check for trailing climber beneath us
    if (ULadderClimbComponent* Follower = FindClimberBehindMe())
    {
        // Order follower to reverse first, or slide down to clear ladder
        Follower->OnMoveOrderChanged(bUp);
    }

    bReversePending = true;
    ReverseAt = LadderRules::NextRungBoundary(LoopPos, bUp ? 1 : -1, LoopLength);
}
```

---

## 5. Automated Verification & Smokes

* **Unit Test:** `Source/CodexTacticsTests/Private/Movement/RampSlopeRulesTest.cpp`
  - Validates slope multiplier on flat ($0^\circ \to 1.0$), moderate ramp ($18^\circ \to 0.85$), and steep ramp ($30^\circ \to 0.65$).
  - Asserts descent clamp applies correctly.
* **Smoke Command:** `RampFileSmokeCommand` (`Codex.Test.RampFile`):
  1. Orders squad to walk up $25^\circ$ warehouse ramp in single file.
  2. Applies simulated sniper damage to point operative halfway up.
  3. Asserts trailing operatives immediately break file hold and disperse laterally.
  4. Reverses order on a climber mid-ladder $\to$ asserts clean 180° turnaround at rung boundary without clipping.

---

## 6. Open Design Questions (with Recommended Defaults)

1. **Emergency Ladder Slide:**
   - *Question:* If an operative is ordered to reverse downwards while under intense fire, should they slide down the rails instead of climbing rung-by-rung?
   - *Recommended Default:* Yes. Double-clicking ground while descending or under fire triggers an emergency fast-slide ($2.5\times$ speed) to ground level.
2. **Ramp Weapon Firing Restrictions:**
   - *Question:* Can operatives fire weapons while ascending steep ramps?
   - *Recommended Default:* Yes, but firing while walking on an incline $> 20^\circ$ incurs a $-15\%$ stability penalty unless the operative is stationary.
3. **Prone on Ramps:**
   - *Question:* Should operatives be allowed to drop prone on ramps when under fire?
   - *Recommended Default:* Allowed on ramps with slope $\le 20^\circ$. On steeper ramps ($> 20^\circ$), prone stance is clamped to crouching to avoid visual mesh clipping through incline geometry.
