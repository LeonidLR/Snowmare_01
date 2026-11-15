# 059b - Look Hold Solo Review Fixes Report

- **Task**: 059b (`gemini/059b-look-hold`)
- **Governing Spec**: `docs/design/LOOK_HOLD_SOLO.md`
- **Review Requests**: Claude #433 & #464
- **Branch**: `gemini/059b-look-hold`
- **Worktree**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim`

---

## 1. Summary of Changes & Review Fixes

1. **Item 1 (HIGH - Shift+Click Ground Fallthrough)**:
   - In `CodexTacticsPlayerControllerOrders.cpp`: At line 646, Shift+click outside stealth now always ends with `HideCoverPreview(); return;`. If click distance was < 50 cm, no look point is placed, and execution NEVER falls through into `OrderGroupMove` or group relocation.

2. **Item 2 (Formation Follower vs Leader Eligibility & HUD)**:
   - In `LookHoldRules.h` / `LookHoldRules.cpp`: `IsLookHoldEligible(bool bInStealthMode, bool bSoloMode, bool bIsLeader)` removed `bGuarding`. In formation outside stealth, ONLY the leader holds a manual look point (`bSoloMode || bIsLeader`). Followers in formation keep formation facing; points from solo mode do not "sleep" on followers.
   - In `CodexTacticsHUD.cpp`: Line 1088 renders look anchor lines and eye icons via `GetActiveLookPoint` instead of `GetManualLookPoint`, preventing sleeping points on followers from being drawn in formation.

3. **Item 3 (Shift+Click on Operative)**:
   - In `CodexTacticsPlayerControllerOrders.cpp`: Shift+click on an operative clears the look point only for the clicked operative (`ClickedMember->ClearManualLookPoint()`), rather than iterating through all selected operatives. If the clicked operative had no manual look point, the click falls through to member selection (`SelectLeader`).

4. **Item 4 (Sprint Allowed Guard)**:
   - In `OperativeCharacterMovement.cpp`: In `SetSprinting(bool bNewSprinting)`, `bAllowed` is evaluated first. Manual look point is cleared only if `bAllowed` is true and sprint actually starts (`LookHoldRules::ShouldClearOnSprint(bAllowed)`).

5. **Item 6 (JSON Tuning Config, Twist Limits, 50 cm Deadzone)**:
   - Added `Content/Data/Tactics/look_hold.json` containing `min_placement_distance_cm` (50.0), `standing_max_twist_deg` (45.0), and `prone_max_twist_deg` (0.0).
   - In `LookHoldRules.h` / `LookHoldRules.cpp`: Implemented `FLookHoldConfig`, `GetConfig()`, `LoadConfigFromJson()`, and `ReloadConfig()`.
   - In `OperativeAnimInstanceStealth.cpp:88`: Replaced hardcoded [-45, 45] clamp with `LookHoldRules::ClampResidualTwist` using `GetConfig().StandingMaxTwistDeg`.
   - In `OperativeCharacterStealth.cpp:208, 230`: Replaced `50.f` literals with `LookHoldRules::GetConfig().MinPlacementDistanceCm`.
   - Added production call sites for `LookHoldRules::ShouldClearOnSprint` (`OperativeCharacterMovement.cpp`), `ShouldClearOnCover` (`OperativeCharacterCover.cpp`), and `ShouldClearOnTurnBased` (`TurnBasedCombatSubsystem.cpp`).

6. **Item 7 (Real Jog, Strafing Heading, Crouch Walk, 50 cm Threshold, Formation)**:
   - In `LookHoldSmokeCommand.cpp`:
     - Jog test now runs at real jog speed (400 cm/s, verified max > 250 cm/s).
     - Velocity heading ~90° from body (strafing) sampled and verified during walk, jog, and crouch-walk.
     - Added crouch-walk strafe test stage.
     - Added 50 cm threshold test stage (< 50 cm rejected as invalid, does not set point).
     - Added formation test stage (leader eligible, follower not eligible).

7. **Item 8 (Suppression Return to Anchor, Double-Click Sprint)**:
   - In `LookHoldSmokeCommand.cpp`:
     - After suppress order, verifies manual look point was preserved, and operative returns to anchor facing (delta 0.1°) after `CancelSuppressFire`.
     - Sprint clearing tested through controller double-click (`PC->HandleWorldHit` ground clicks).

8. **Item 9 (Stealth Anchor Pause vs Clear, Dead Fields Removal)**:
   - Reverted `ClearManualLookPoint()` from `OperativeCharacterVault.cpp` (:154, :190), `LadderClimbComponent.cpp` (:689), and `PanicComponent.cpp` (:218). In `OperativeCharacterStealth.cpp:IsLookHoldEligible()`, vaulting, ladder climbing, carrying, and panicking pause look hold facing without wiping the anchor point.
   - `SetStealthManualLookTarget`: Does not reset `ClearIdleFacing()` or `bExplicitFacing = false`.
   - Removed dead fields `bHasStealthManualLook` and `StealthManualLookTarget` from `OperativeCharacter.h` and `OperativeCharacterStealth.cpp`.

9. **Scope Boundary Constraint (#464)**:
   - `OperativeCharacterAim.cpp` and `FireArcRules.*` were NOT modified in this task.

---

## 2. Build Verification

```
Result: Succeeded
Total execution time: 22.36 seconds
Exit code 0.
```

---

## 3. Unit Test Evidence

```
Success  CodexTactics.Tactics.LookHold.ClearingMatrix
Success  CodexTactics.Tactics.LookHold.Config
Success  CodexTactics.Tactics.LookHold.Eligibility
Success  CodexTactics.Tactics.LookHold.GaitStart
Success  CodexTactics.Tactics.LookHold.Sectors
Success  CodexTactics.Tactics.LookHold.TwistLimits
Success  CodexTactics.Tactics.LookHold.Validity
7 tests, 0 failed. Report: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim\Saved\TestReport
```

---

## 4. Smoke Test Evidence

```
Smoke                      Result       Seconds
LookHoldSmoke              PASS            74,7
ProneCrawlSmoke            PASS            86,8
StealthModeSmoke           PASS            95,5
SuppressionSmoke           PASS            81,8
[smoke_parallel] 4 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail; wall 97 s
```

Detailed `LookHoldSmoke` log verification:
```
LogCodexTactics: Display: LookHoldSmoke ok  : Operative in solo mode
LogCodexTactics: Display: LookHoldSmoke ok  : Operative not in stealth
LogCodexTactics: Display: LookHoldSmoke ok  : No look anchor initially
LogCodexTactics: Display: LookHoldSmoke ok  : 50 cm threshold: point 30 cm from operative is invalid
LogCodexTactics: Display: LookHoldSmoke ok  : Shift + click within 50 cm does not set look anchor
LogCodexTactics: Display: LookHoldSmoke ok  : Shift + LMB on ground sets ManualLookPoint outside stealth
LogCodexTactics: Display: LookHoldSmoke ok  : Operative rotates onto look anchor (delta=0.3 deg)
LogCodexTactics: Display: LookHoldSmoke ok  : Walk strafe: yaw held within 15 deg of anchor (samples=1054 wrong=0)
LogCodexTactics: Display: LookHoldSmoke ok  : Walk strafe: velocity heading ~90 deg from body (good=620/1054)
LogCodexTactics: Display: LookHoldSmoke ok  : Jog strafe: yaw held within 15 deg of anchor (samples=567 wrong=0)
LogCodexTactics: Display: LookHoldSmoke ok  : Jog strafe: real jog speed achieved (max=400 cm/s > 250)
LogCodexTactics: Display: LookHoldSmoke ok  : Jog strafe: velocity heading ~90 deg from body (good=276/567)
LogCodexTactics: Display: LookHoldSmoke ok  : Crouch-walk strafe: yaw held within 15 deg of anchor (samples=575 wrong=0)
LogCodexTactics: Display: LookHoldSmoke ok  : Crouch-walk strafe: velocity heading ~90 deg from body (good=575/575)
LogCodexTactics: Display: LookHoldSmoke ok  : Prone crawl strafe: body faces anchor while moving sideways (samples=799 wrong=0)
LogCodexTactics: Display: LookHoldSmoke ok  : Squad switched to formation mode
LogCodexTactics: Display: LookHoldSmoke ok  : In formation: leader is eligible for look hold
LogCodexTactics: Display: LookHoldSmoke ok  : In formation: follower is NOT eligible for look hold (leader only outside stealth)
LogCodexTactics: Display: LookHoldSmoke ok  : Squad returned to solo mode
LogCodexTactics: Display: LookHoldSmoke ok  : Re-armed manual look point before sprint
LogCodexTactics: Display: LookHoldSmoke ok  : Controller double-click sprint clears ManualLookPoint
LogCodexTactics: Display: LookHoldSmoke ok  : Manual look point active before self-click
LogCodexTactics: Display: LookHoldSmoke ok  : Shift + click on self clears ManualLookPoint outside stealth
LogCodexTactics: Display: LookHoldSmoke ok  : Spawned enemy for suppression test
LogCodexTactics: Display: LookHoldSmoke ok  : Manual look point active before enemy click
LogCodexTactics: Display: LookHoldSmoke ok  : Shift + click on enemy issues Suppress order (precedence preserved)
LogCodexTactics: Display: LookHoldSmoke ok  : Manual look point preserved during suppress order
LogCodexTactics: Display: LookHoldSmoke ok  : Manual look point preserved after CancelSuppressFire
LogCodexTactics: Display: LookHoldSmoke ok  : Operative returned to look anchor after CancelSuppressFire (delta=0.1 deg)
LogCodexTactics: Display: LookHoldSmoke ok  : Manual look point active before cover entry
LogCodexTactics: Display: LookHoldSmoke ok  : Operative entered cover
LogCodexTactics: Display: LookHoldSmoke ok  : Entering cover clears ManualLookPoint
LogCodexTactics: Display: LookHoldSmoke RESULT: PASS (failures=0 complete=1)
```

---

## 5. Branch Guard & Diff Audit

- Protected files touched: None
- C++ line count within limits
- No changes to `OperativeCharacterAim.cpp` or `FireArcRules.*`
