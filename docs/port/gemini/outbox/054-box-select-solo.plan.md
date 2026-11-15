# Plan 054: Box Select in Formation Switches Squad to Solo

- **Task:** 054 (`squad-select`)
- **Governing Spec:** `docs/port/gemini/inbox/054-box-select-formation-to-solo.md` (Designer 2026-11-15)
- **Base Commit:** `c3c34af4` (`origin/main`)
- **Branch:** `gemini/054-box-select-solo`
- **Gate / Review:** `gemini_gate.ps1 -Task 054` with Sonnet/Opus review

---

## 1. Requirements & Intent

1. **Box Selection in Formation:**
   - Mouse drag rectangle (LMB drag past `BoxSelectThreshold`) must function regardless of whether the squad is in Solo mode or Formation mode, and regardless of whether the formation is stationary or actively moving.
   - It remains disabled only during Turn-Based grid combat (`GetActiveTurnBased() != nullptr`).
2. **Transition from Formation to Solo:**
   - When a selection box is completed in Formation mode and contains 1 or more living operatives, the squad automatically transitions to **SOLO** mode.
   - The boxed operatives become the active group selection (`SelectedGroup`).
   - If the previous leader is not inside the boxed selection, the first member of the boxed selection becomes the new leader.
   - The formation shape and leader-follow bindings are disbanded.
   - Operatives **not** in the selection box remain in place, holding their current positions as independent solo operatives (the standard `Codex.Squad.SoloIndependent` behavior).
   - If a box select is drawn in formation mode but encompasses 0 living operatives, the squad remains in formation mode without unintended side effects.
3. **Move Orders for Boxed Solo Group:**
   - Subsequent move orders issue the existing multi-selection solo move (each operative pathfinds to its own point/spread, zero formation constraints, zero leader tether).
4. **Re-forming & Stances:**
   - Re-forming into formation remains unchanged via `[B]` key or formation radial order.
   - Operative stances are preserved as-is. Stealth mode rules remain unchanged (stealth is allowed only in solo; boxed operatives transitioning to solo may immediately toggle stealth).

---

## 2. Exact Files to Modify & Functions Touched

### 2.1 Rules & Subsystem Core
1. **`Source/CodexTactics/Public/Characters/SquadOrderRules.h`**:
   - Update `IsBoxSelectionAllowed(bool bSoloMode, bool bTurnBasedCombat)` documentation: box selection is allowed in both Solo and Formation modes, restricted only by turn-based combat (`!bTurnBasedCombat`).
   - Add pure helper:
     ```cpp
     /**
      * A box selection made in formation mode switches the squad to Solo if at least one operative was selected.
      */
     CODEXTACTICS_API bool ShouldSwitchToSoloOnBoxSelect(bool bSoloMode, int32 BoxedCount);
     ```
2. **`Source/CodexTactics/Private/Characters/SquadOrderRules.cpp`**:
   - Implement `IsBoxSelectionAllowed`:
     ```cpp
     bool IsBoxSelectionAllowed(bool bSoloMode, bool bTurnBasedCombat)
     {
         return !bTurnBasedCombat;
     }
     ```
   - Implement `ShouldSwitchToSoloOnBoxSelect`:
     ```cpp
     bool ShouldSwitchToSoloOnBoxSelect(bool bSoloMode, int32 BoxedCount)
     {
         return !bSoloMode && BoxedCount > 0;
     }
     ```
3. **`Source/CodexTactics/Public/Characters/SquadSubsystem.h`**:
   - Declare method:
     ```cpp
     /**
      * Designer 2026-11-15 (Task 054): Disbands formation into Solo mode with the boxed operatives as the active multi-selection.
      * Non-boxed operatives hold where they stand as independent solo operatives.
      */
     void DisbandFormationToSoloBox(const TArray<AOperativeCharacter*>& BoxedOperatives);
     ```
4. **`Source/CodexTactics/Private/Characters/SquadSubsystem.cpp`**:
   - Implement `DisbandFormationToSoloBox`:
     - Calls `EnterSoloModeInternal(false, true)`.
     - Calls `SetSelectedGroup(BoxedOperatives, true)` to assign the boxed units as the active multi-selection group and update leader if necessary.
     - Logs `[Squad] Box select in formation disbanded squad to Solo mode (N operatives selected)`.
     - Dispatches HUD message feed announcement:
       `Messages->PostMessage(LeaderRef->DisplayName, LOCTEXT("SoloModeOnBox", "👤 [SOLO MODE: ON] Box selection disbanded formation into Solo mode."));`

### 2.2 Player Controller & Selection Flow
5. **`Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`**:
   - In `PlayerTick`:
     ```cpp
     if (bLmbDown)
     {
         GetMousePosition(BoxCurrent.X, BoxCurrent.Y);
         const USquadSubsystem* Squad = GetSquad();
         const bool bSoloMode = Squad && Squad->IsSoloMode();
         const bool bTurnBased = GetActiveTurnBased() != nullptr;
         if (!bBoxSelecting && SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBased)
             && FVector2D::Distance(BoxStart, BoxCurrent) > BoxSelectThreshold)
         {
             bBoxSelecting = true;
         }
     }
     ```
     (Now seamlessly activates in formation mode while standing or moving).
6. **`Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp`**:
   - In `OnClickReleased()`:
     - Keep `if (bBoxSelecting) { bBoxSelecting = false; SelectInBox(...); return; }`.
     - Remove / adjust the obsolete `bDragged` early-out that discarded drags in formation mode.
   - In `SelectInBox(const FVector2D& Min, const FVector2D& Max)`:
     - Check `SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBased)`.
     - Collect valid alive operatives inside screen box into `Selected`.
     - If `SquadOrderRules::ShouldSwitchToSoloOnBoxSelect(bSoloMode, Selected.Num())`:
       - Call `Squad->DisbandFormationToSoloBox(Selected);`
     - Else:
       - Standard group assignment `Squad->SetSelectedGroup(Selected);`
     - Report `PlayerCommands::SelectBox`.

### 2.3 Tests & Verification
7. **`Source/CodexTacticsTests/Private/Characters/SquadOrderRulesTest.cpp`**:
   - Update `FSquadOrderBoxSelectionTest`:
     - Assert `IsBoxSelectionAllowed(true, false) == true` (Solo + RealTime).
     - Assert `IsBoxSelectionAllowed(false, false) == true` (Formation + RealTime).
     - Assert `IsBoxSelectionAllowed(true, true) == false` (Solo + TurnBased).
     - Assert `IsBoxSelectionAllowed(false, true) == false` (Formation + TurnBased).
   - Add `FSquadOrderBoxSelectToSoloTransitionTest`:
     - Assert `ShouldSwitchToSoloOnBoxSelect(false, 2) == true` (Formation + 2 operatives).
     - Assert `ShouldSwitchToSoloOnBoxSelect(false, 0) == false` (Formation + empty box).
     - Assert `ShouldSwitchToSoloOnBoxSelect(true, 2) == false` (Already in Solo).

---

## 3. Verification Protocol

1. **Compilation:**
   - `powershell .\Scripts\build.ps1`
2. **Unit Tests:**
   - `powershell .\Scripts\test.ps1 -Filter "CodexTactics.Characters.SquadOrder"`
3. **Smoke Tests:**
   - `powershell .\Scripts\smoke_parallel.ps1 -Commands "GroupSelectSmoke,RealtimeSelectSmoke,SquadControlSmoke,FormationLabSmoke,SoloStartSmoke,FastRegroupSmoke"`
4. **Smart Test & TypeSafe Triage:**
   - `powershell .\Scripts\test.ps1 -Smart -Quick`
   - `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
5. **Report & Review:**
   - Deliver `docs/port/gemini/outbox/054-box-select-solo.report.md`.
