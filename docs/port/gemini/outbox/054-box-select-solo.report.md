# 054 - Box Select in Formation Switches Squad to Solo Report

- **Task**: 054 (`squad-select`)
- **Governing Spec**: `docs/port/gemini/inbox/054-box-select-formation-to-solo.md` (Designer directive 2026-11-15)
- **Base Commit**: `c3c34af4` (`origin/main`)
- **Branch**: `gemini/054-box-select-solo`
- **Worktree**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim`

---

## 1. Summary of Changes

Task 054 restores and refines box selection behavior in accordance with the game designer's 2026-11-15 directive:
1. **Box Selection Allowed in Formation Mode**:
   - `SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBasedCombat)` updated to return `!bTurnBasedCombat`. Box selection (LMB drag past threshold) is now fully enabled in both Solo and Formation modes, both when standing and while actively walking or running in formation. It remains restricted only during turn-based combat.
   - `ACodexTacticsPlayerController::PlayerTick` and `ACodexTacticsPlayerControllerSelection::OnClickReleased` no longer drop or suppress mouse drag rectangles in formation mode.
2. **Transition Decision Rule & Disband to Solo**:
   - Added pure rule `SquadOrderRules::ShouldSwitchToSoloOnBoxSelect(bSoloMode, BoxedCount)` returning `!bSoloMode && BoxedCount > 0`.
   - Added `USquadSubsystem::DisbandFormationToSoloBox(const TArray<AOperativeCharacter*>& BoxedOperatives)`:
     - Disbands formation and activates **SOLO** mode (`bIsSoloMode = true`).
     - Sets the boxed operatives as the active multi-selection group (`SelectedGroup`), promoting the first boxed operative to leader if the prior leader was outside the box.
     - Stops the previous leader if moving in formation.
     - Ensures non-boxed operatives hold where they stand as independent solo operatives (`Codex.Squad.SoloIndependent`), stopping any lingering formation follow.
     - Logs `[Squad] Box select in formation disbanded squad to Solo mode (N operatives selected)`.
     - Dispatches HUD message: `👤 [SOLO MODE: ON] Box selection disbanded formation into Solo mode (N operatives selected).`
3. **Smoke & Automated Verification**:
   - Updated `FSquadOrderBoxSelectionTest` to assert real-time box selection in both solo and formation, blocked during turn-based combat, and verifies `ShouldSwitchToSoloOnBoxSelect`.
   - Extended `SquadControlSmokeCommand.cpp` (Stage 4) to directly test:
     - Initial state in formation mode.
     - Box selecting operatives disbands formation to SOLO mode.
     - Boxed units become `SelectedGroup` with multi-selection active.
     - Non-boxed operative is excluded from `SelectedGroup`.
     - Order group move dispatches distinct move orders to boxed operatives while non-boxed operative holds in place with no move order.
     - Re-forming via `[B]` (`ToggleSoloMode`) cleanly restores formation mode and resets selection to `{leader}`.

---

## 2. Modified Files

1. `Source/CodexTactics/Public/Characters/SquadOrderRules.h`: Declared `ShouldSwitchToSoloOnBoxSelect`, updated `IsBoxSelectionAllowed` spec.
2. `Source/CodexTactics/Private/Characters/SquadOrderRules.cpp`: Implemented `IsBoxSelectionAllowed` and `ShouldSwitchToSoloOnBoxSelect`.
3. `Source/CodexTactics/Public/Characters/SquadSubsystem.h`: Declared `DisbandFormationToSoloBox`.
4. `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp`: Implemented `DisbandFormationToSoloBox`.
5. `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp`: Enabled drag box selection regardless of formation mode.
6. `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerSelection.cpp`: Disband formation to solo on boxed selection release.
7. `Source/CodexTacticsDev/Private/Debug/SquadControlSmokeCommand.cpp`: Extended Stage 4 for 054 box-select formation-to-solo and move dispatch verification.
8. `Source/CodexTacticsTests/Private/Characters/SquadOrderRulesTest.cpp`: Added unit tests for selection permissions and mode switch decisions.

---

## 3. Verification Evidence

| Verification Step | Command / Scope | Result | Details |
|---|---|---|---|
| **Compilation** | `powershell .\Scripts\build.ps1` | **PASS** | Exit code 0, 23.78s |
| **Unit Tests (Rules)** | `powershell .\Scripts\test.ps1 -Filter "CodexTactics.Squad.Orders"` | **PASS** | 12/12 passed (0 failed) |
| **Squad Control Smoke** | `powershell .\Scripts\smoke.ps1 -Command CodexTactics.SquadControlSmoke` | **PASS** | Stage 4 box-select in formation -> Solo transition, group move dispatch, non-boxed hold, and re-forming PASS |
| **Smart Smoke Suite** | `powershell .\Scripts\test.ps1 -Smart -Quick` | **PASS** | 286/286 unit tests passed, 15/15 parallel smokes PASS (0 failed, 0 needed re-run) |
| **Selection Smokes** | `smoke_parallel.ps1 -Commands "GroupSelectSmoke,RealtimeSelectSmoke,SquadControlSmoke,FormationLabSmoke,SoloStartSmoke,FastRegroupSmoke"` | **PASS** | All 6 core smokes PASS (0 failed) |
| **TypeSafe (JEV) Audit** | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` | **PASS** | Boundary Breach: PASS, Risk: 2.8 / 4 (Audited with `--agent claude` per Chat #375 instructions) |

---

## 4. Safety & Boundary Integrity

- `Scripts/Tools/typesafe_triage.py` was **NOT** modified (clean against `origin/main`).
- Zero changes to designer maps, content packages, or audio files.
- All C++ modifications reside strictly within `Source/CodexTactics/`, `Source/CodexTacticsDev/`, and `Source/CodexTacticsTests/`.
- 100% backward compatibility preserved for standard Solo toggle (`[B]`) and formation orders.
