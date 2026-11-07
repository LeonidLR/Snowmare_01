# Report: T6. Solo -> Formation Switch Clears Box Selection; No Box Selection in Formation Mode

**Task:** T6. Solo -> formation switch clears the box selection; no box selection in formation mode (small C++)  
**Branch:** `gemini/t6-formation-box-selection`  
**Date:** 2026-11-04  

## Summary of Deliverable

Fixed player selection and formation control when transitioning between Solo and Formation modes:
1. **Disabled box selection in formation mode**: A ground drag no longer creates a selection box in formation mode. Selection remains anchored to the squad leader. Dragging for camera panning, rotation, and other systems is preserved. Releasing a ground drag in formation mode no longer issues an accidental move order.
2. **Solo -> Formation switch clears multi-selection**: When exiting solo mode (via key **B** or leash), any box or multi-selection is automatically cleared, resetting the selected group to `{leader}` so the squad formation works immediately without stale group orders.
3. **Verified with automated tests**: Added pure rule unit tests (`CodexTactics.Squad.Orders.BoxSelectionOnlyInSoloMode`) and extended `CodexTactics.SquadControlSmoke` with end-to-end assertions. All 5 required smoke suites passed.

---

## 1. Architectural Changes

### 1.1 Pure Rule: `SquadOrderRules::IsBoxSelectionAllowed`
- File: [`Source/CodexTactics/Public/Characters/SquadOrderRules.h`](../../Source/CodexTactics/Public/Characters/SquadOrderRules.h), [`Source/CodexTactics/Private/Characters/SquadOrderRules.cpp`](../../Source/CodexTactics/Private/Characters/SquadOrderRules.cpp)
- Implemented pure function:
  ```cpp
  bool IsBoxSelectionAllowed(bool bSoloMode, bool bTurnBasedCombat)
  {
      return bSoloMode && !bTurnBasedCombat;
  }
  ```
  Isolates the domain rule from controller state: box selection is strictly permitted only in solo mode and outside turn-based combat.

### 1.2 Multi-Selection Reset: `USquadSubsystem::ClearGroupSelection`
- File: [`Source/CodexTactics/Public/Characters/SquadSubsystem.h`](../../Source/CodexTactics/Public/Characters/SquadSubsystem.h), [`Source/CodexTactics/Private/Characters/SquadSubsystem.cpp`](../../Source/CodexTactics/Private/Characters/SquadSubsystem.cpp)
- Added method:
  ```cpp
  void USquadSubsystem::ClearGroupSelection()
  {
      AOperativeCharacter* LeaderRef = Leader.Get();
      if (LeaderRef)
      {
          SetSelectedGroup({ LeaderRef }, false);
      }
      else
      {
          SetSelectedGroup({}, false);
      }
  }
  ```
- Integrated into `USquadSubsystem::ExitSoloMode(bool bCausedByLeash, float Distance)`:
  Every transition exiting solo mode (manual **B** tap, distance leash limit exceeded, or scripted exits) invokes `ClearGroupSelection()`. This resets operative selection rings (hiding follower group rings and clearing `bInMultiSelection`), ensuring all subsequent commands are given to the leader while followers resume their formation positions.

### 1.3 Controller Input Handling: `ACodexTacticsPlayerController`
- File: [`Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`](../../Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp), [`Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp`](../../Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp)
- **`PlayerTick`**:
  ```cpp
  const USquadSubsystem* Squad = GetSquad();
  const bool bSoloMode = Squad && Squad->IsSoloMode();
  const bool bTurnBased = GetActiveTurnBased() != nullptr;
  if (!bBoxSelecting && SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBased)
      && FVector2D::Distance(BoxStart, BoxCurrent) > BoxSelectThreshold)
  {
      bBoxSelecting = true;
  }
  ```
- **`OnClickReleased`**:
  If mouse was dragged beyond `BoxSelectThreshold` in formation mode, `bBoxSelecting` remains false, and release now gracefully early-returns without calling `HandleWorldHit(Hit)`:
  ```cpp
  const bool bDragged = FVector2D::Distance(BoxStart, BoxCurrent) > BoxSelectThreshold;
  if (bDragged)
  {
      return;
  }
  ```
  Prevents accidental move orders from being dispatched to wherever the drag ended.
- **`SelectInBox`**:
  Validates `SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBased)`. If not allowed, returns 0 and does not alter selection.
- **`ToggleSoloMode`**:
  Cancels any in-flight box selection state (`bBoxSelecting = false; bLmbDown = false;`) on toggle.

---

## 2. Test Verification

### 2.1 Automation Unit Tests
- Suite: `CodexTactics.Squad.Orders.*`
- Added test: `FSquadOrderBoxSelectionTest` (`CodexTactics.Squad.Orders.BoxSelectionOnlyInSoloMode`)
- Command: `powershell -ExecutionPolicy Bypass -File Scripts\test.ps1 -Filter CodexTactics.Squad.Orders`
- Result: **10 tests, 0 failed**
  - `Success CodexTactics.Squad.Orders.BoxSelectionOnlyInSoloMode`
  - All existing squad order rules verified green without regression.

### 2.2 Smoke Tests
All five required smoke suites executed and passed:

1. **`CodexTactics.SquadControlSmoke`**:
   - Extended with Stage 4 covering the T6 behavior in active combat:
     - Verified box select refused in formation mode (`SelectInBox` returns 0, selection stays leader).
     - Verified Solo mode toggle (**B**).
     - Verified box selection of all three squad members in solo mode.
     - Verified toggle back to formation mode (**B**): multi-selection cleared, `Squad->GetSelectedGroup()` is `{leader}`.
   - Result: **Smoke RESULT: PASS**

2. **`CodexTactics.SoloRejoinSmoke`**:
   - Verified prone/crouch/crawl solo scouting, hold positions, leader stance synchronization, wall cover, and regrouping behavior.
   - Result: **Smoke RESULT: PASS**

3. **`CodexTactics.CombatFormationSmoke`**:
   - Verified squad follows leader in real-time combat and tactical pause, individual orders during pause, Solo toggle in fight, and regroup back into formation.
   - Result: **Smoke RESULT: PASS**

4. **`CodexTactics.GroupCornerPeekSmoke`** (Map: `L_FormationLab`):
   - Verified solo mode 3-operative box selection along wall `W_B_3`, west and east corner peeks, and stack settlement.
   - Result: **Smoke RESULT: PASS**

5. **`CodexTactics.WallLeadershipSmoke`** (Map: `L_FormationLab`):
   - Verified wall leadership corner peeks, leader handoff to end operative, selection ring transfer, and leaving the wall keeping new leader.
   - Result: **Smoke RESULT: PASS**

---

## 3. Touched Files

- `Source/CodexTactics/Public/Characters/SquadOrderRules.h`
- `Source/CodexTactics/Private/Characters/SquadOrderRules.cpp`
- `Source/CodexTactics/Public/Characters/SquadSubsystem.h`
- `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp`
- `Source/CodexTactics/Public/Core/CodexTacticsPlayerController.h`
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp`
- `Source/CodexTacticsDev/Private/Debug/SquadControlSmokeCommand.cpp`
- `Source/CodexTacticsTests/Private/Characters/SquadOrderRulesTest.cpp`
- `docs/port/gemini_reports/t6-formation-box-selection.md`
