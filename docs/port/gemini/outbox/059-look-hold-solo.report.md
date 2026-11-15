# 059 - Look Hold Outside Stealth (Solo Walk / Jog / Crouch / Prone Strafe) Report

- **Task**: 059 (`gemini/059-look-hold-solo`)
- **Governing Spec**: `docs/design/LOOK_HOLD_SOLO.md` & `docs/port/codex/outbox/059-lookhold-scout.report.md`
- **Approved Directives**: Claude #418 (Real-time combat does not clear look anchor; fire orders temporarily aim at target and resume look anchor; rebase cleanly onto `origin/main`; preserve stealth backward compatibility)
- **Base Commit**: `f3f07f1b` (`origin/main`)
- **Commit**: `b906a500`
- **Branch**: `gemini/059-look-hold-solo`
- **Worktree**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim`

---

## 1. Summary of Changes

Task 059 implements unified manual look hold outside stealth for solo movement (walk, jog, crouch, prone strafe):

1. **Pure Rules Layer (`LookHoldRules.h` / `LookHoldRules.cpp`)**:
   - `GetDirectionSector(RelativeMoveDeg)`: Quantizes 8 direction sectors (45° slices: 0=F, 1=FR, 2=R, 3=BR, 4=B, 5=BL, 6=L, 7=FL) with half-slice boundary handling.
   - `GetGaitStartSector(LeadSector, bBodyHeldByLook)`: Returns `LeadSector` if body is held by look anchor, else 0 (Forward).
   - `IsOffGaitStartSector(StartSector, LegDirectionDeg, ToleranceDeg)`: Verifies leg movement relative to initial gait sector within specified tolerance.
   - `IsValidLookPoint(LookPoint, OperativeLocation, MinDistanceCm)`: Enforces 2D minimum distance deadzone (default 50 cm) to prevent erratic body spin when passing directly over the look point.
   - `ShouldClearOnSprint()`, `ShouldClearOnCover()`, `ShouldClearOnTurnBased()`, `ShouldClearOnFireOrder()`: Decoupled clearing logic matrix. In real-time combat, fire orders do **NOT** clear look hold; they only temporarily aim at the target, resuming the look point once the order concludes. Only turn-based combat clears look hold.
   - `ShouldClearOnIncapacitated(bDead, bKnockedDown, bPanicking, bClimbingLadder, bVaulting, bCarrying)`: Clears look hold whenever control is compromised.
   - `IsLookHoldEligible(bInStealthMode, bSoloMode, bIsLeader, bGuarding)`: Grants look hold eligibility in stealth mode, in solo mode, or for an active squad leader who is not guarding.
   - `ClampResidualTwist(TwistDeg, LimitDeg)`: Clamps residual spine twist angles within mechanical limits.
   - Refactored `StealthModeRules.cpp`: Delegated shared sector and gait calculations directly to `LookHoldRules`.

2. **Operative Character Integration (`OperativeCharacter.h`, `OperativeCharacterStealth.cpp`, `OperativeCharacterAim.cpp`)**:
   - Added `bHasManualLookPoint` and `ManualLookPoint` properties.
   - Added unified look anchor API: `SetManualLookPoint(Point)`, `ClearManualLookPoint()`, `HasManualLookPoint()`, `GetManualLookPoint(OutPoint)`, `GetActiveLookPoint(OutPoint)`, and `IsLookHoldEligible()`.
   - Preserved legacy stealth wrappers (`SetStealthManualLookTarget`, `ClearStealthManualLookTarget`, `HasStealthManualLookTarget`, `GetStealthManualLookTarget`, `GetStealthLookPoint`) delegating to the unified look anchor, ensuring 100% backward compatibility.
   - In `AOperativeCharacter::UpdateCombatFacing`:
     - Evaluated `bLookHold` combining stealth look and solo look hold via `GetActiveLookPoint`.
     - When `bLookHold` is active, steps actor yaw toward `ActiveLookPoint` at stance turn speed (`OperativeMovementRules::GetTurnRate`).
     - Higher priority combat fire orders (quickfire/suppression/target shot) temporarily take facing; once ended, facing returns cleanly to `ActiveLookPoint`.
     - In `IsOrderedCrawl()`, guarded `bRemembered` threat facing with `!bLookHoldActive`.

3. **Clearing Triggers**:
   - **Sprint**: `AOperativeCharacter::OrderMoveTo` and `AOperativeCharacter::SetSprinting(true)` clear manual look point and display floating feedback `"LOOK CLEARED"`.
   - **Cover**: `AOperativeCharacter::EnterCover` clears manual look point.
   - **Incapacitated**: `UKnockdownComponent::StartKnockdown`, `AOperativeCharacter::BecomeCorpse`, `UPanicComponent`, `AOperativeCharacter::StartVault`, `ULadderClimbComponent`, and `SetCarrying(true)` clear manual look point.
   - **Turn-based**: `UTurnBasedCombatSubsystem::StartCombat` clears manual look point across all squad members.

4. **Player Controller Orders & HUD Feedback**:
   - `ACodexTacticsPlayerControllerOrders::HandleMoveOrderGroundClick`:
     - Outside stealth, Shift + LMB on ground sets `ManualLookPoint` on the operative and displays `"LOOK ANCHOR"` floating text.
     - Shift + LMB on self clears `ManualLookPoint` and displays `"LOOK CLEARED"` floating text.
     - Shift + LMB on enemy, silhouette ghost, or suppress marker strictly preserves precedence and dispatches `IssueSuppress(Hit)` without capturing look anchor.
   - `ACodexTacticsPlayerControllerCamera`: Shift + RMB clears `ManualLookPoint`.
   - `ACodexTacticsHUD`: Renders eye marker icon and anchor connecting line for any active `ManualLookPoint` on selected operatives.

5. **Test Coverage & Automated Smoke**:
   - Added `Source/CodexTacticsTests/Private/Tactics/LookHoldRulesTest.cpp`: 6/6 tests covering sectors, gait start, point validity, clearing matrix, eligibility, and twist clamp.
   - Added `Source/CodexTacticsDev/Private/Debug/LookHoldSmokeCommand.cpp` (`CodexTactics.LookHoldSmoke`): 10-stage end-to-end smoke test validating solo mode activation, ground anchor placement, yaw alignment (<0.5° delta), walking strafe over 6 m (2200+ samples, 0 wrong yaw), jogging strafe over 6 m (2100+ samples, 0 wrong yaw), prone crawl strafe (5500+ samples, 0 wrong yaw), sprint order clearing, self-click clearing, suppression priority precedence, and cover entry clearing.
   - Registered `LookHoldSmoke` in `Scripts/test_map.json` under registry, smoke options, and rules (`test.ps1 -CheckMap` PASS).

---

## 2. Modified & Created Files

### Created:
1. `Source/CodexTactics/Public/Tactics/LookHoldRules.h`: Declarations of look hold pure rules and sector math.
2. `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp`: Implementations of direction sector, gait, validity, and eligibility rules.
3. `Source/CodexTacticsTests/Private/Tactics/LookHoldRulesTest.cpp`: Unit tests (`CodexTactics.Tactics.LookHold`).
4. `Source/CodexTacticsDev/Private/Debug/LookHoldSmokeCommand.cpp`: Dedicated smoke test (`CodexTactics.LookHoldSmoke`).

### Modified:
5. `Source/CodexTactics/Public/Characters/OperativeCharacter.h`: Unified manual look point API and properties.
6. `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp`: Unified look methods and stealth wrappers.
7. `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp`: `UpdateCombatFacing` look hold integration.
8. `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp`: Sprint order clearing and floating text.
9. `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp`: Cover entry clearing.
10. `Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp`: Corpse transition clearing.
11. `Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp`: Vault start clearing.
12. `Source/CodexTactics/Private/Characters/PanicComponent.cpp`: Panic start clearing.
13. `Source/CodexTactics/Private/Characters/StealthModeRules.cpp`: Delegated shared math to `LookHoldRules`.
14. `Source/CodexTactics/Private/Combat/KnockdownComponent.cpp`: Knockdown start clearing.
15. `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp`: Shift+LMB ground anchor and self clearing.
16. `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCamera.cpp`: Shift+RMB clearing.
17. `Source/CodexTactics/Private/Ladders/LadderClimbComponent.cpp`: Ladder climb clearing.
18. `Source/CodexTactics/Private/Tactics/TurnBasedCombatSubsystem.cpp`: Turn-based start clearing.
19. `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp`: HUD anchor line and eye icon rendering.
20. `Scripts/test_map.json`: Registered `LookHoldSmoke`.

---

## 3. Verification Evidence Table

| Claim | Evidence |
|---|---|
| Pure rules quantize 8 direction sectors and calculate gait starts | `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp:11`; verified by `LookHoldRulesTest.cpp:15` (`CodexTactics.Tactics.LookHold.Sectors` PASS) |
| Deadzone radius 50 cm prevents invalid look points | `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp:45`; verified by `LookHoldRulesTest.cpp:52` (`CodexTactics.Tactics.LookHold.Validity` PASS) |
| Look hold eligibility triggers for stealth, solo, or leader non-guarding | `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp:78`; verified by `LookHoldRulesTest.cpp:76` (`CodexTactics.Tactics.LookHold.Eligibility` PASS) |
| Shift+LMB on ground sets look anchor and rotates operative | `CodexTacticsPlayerControllerOrders.cpp:191`; verified by `LookHoldSmokeCommand.cpp:180` (`LookHoldSmoke` Stage 1 & 2 PASS) |
| Operative walks 6 m strafing with body locked onto anchor point | `OperativeCharacterAim.cpp:248`; verified by `LookHoldSmokeCommand.cpp:230` (`LookHoldSmoke` Stage 3 PASS: 2288 samples, 0 wrong yaw) |
| Operative jogs 6 m strafing with body locked onto anchor point | `OperativeCharacterAim.cpp:248`; verified by `LookHoldSmokeCommand.cpp:270` (`LookHoldSmoke` Stage 4 PASS: 2196 samples, 0 wrong yaw) |
| Operative prone crawls strafing with body locked onto anchor point | `OperativeCharacterAim.cpp:174`; verified by `LookHoldSmokeCommand.cpp:315` (`LookHoldSmoke` Stage 5 PASS: 5513 samples, 0 wrong yaw) |
| Sprint order clears look anchor | `OperativeCharacterMovement.cpp:328`; verified by `LookHoldSmokeCommand.cpp:370` (`LookHoldSmoke` Stage 6 PASS) |
| Shift+LMB on self clears look anchor | `CodexTacticsPlayerControllerOrders.cpp:180`; verified by `LookHoldSmokeCommand.cpp:400` (`LookHoldSmoke` Stage 7 PASS) |
| Shift+LMB on enemy preserves Suppress order precedence | `CodexTacticsPlayerControllerOrders.cpp:165`; verified by `LookHoldSmokeCommand.cpp:425` (`LookHoldSmoke` Stage 8 PASS) |
| Cover entry clears look anchor | `OperativeCharacterCover.cpp:577`; verified by `LookHoldSmokeCommand.cpp:455` (`LookHoldSmoke` Stage 9 PASS) |
| Compilation succeeds cleanly with zero errors | `powershell .\Scripts\build.ps1` exit code 0 |
| Unit tests: 6/6 LookHold tests pass | `powershell .\Scripts\test.ps1 -Filter "CodexTactics.Tactics.LookHold"` -> 6 passed, 0 failed |
| Stealth unit tests: 10/10 tests pass | `powershell .\Scripts\test.ps1 -Filter "CodexTactics.Characters.StealthMode"` -> 10 passed, 0 failed |
| Stealth regression smoke passes | `powershell .\Scripts\smoke.ps1 -Command CodexTactics.StealthModeSmoke` -> PASS |
| Prone crawl regression smoke passes | `powershell -ExecutionPolicy Bypass -File Scripts\smoke_parallel.ps1 -Commands ProneCrawlSmoke` -> PASS (44.4s) |
| Solo start regression smoke passes | `powershell .\Scripts\smoke.ps1 -Command CodexTactics.SoloStartSmoke` -> PASS |
| Squad control regression smoke passes | `powershell .\Scripts\smoke.ps1 -Command CodexTactics.SquadControlSmoke` -> PASS |
| Test map metadata valid | `powershell .\Scripts\test.ps1 -CheckMap` -> PASS |
| TypeSafe Jev safety audit passes | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` -> Boundary Breach: PASS, Risk: 2.6 / 4 |

---

## 4. Safety & Boundary Integrity

- Zero changes to engine internals, level geometry, or audio configs.
- No modifications to `CLAUDE.md`, `typesafe_triage.py`, or asset binaries.
- Strict isolation preserved: stealth backward compatibility wrappers maintained for all existing call sites.
- Clean compilation and verified regression suite across movement, stealth, combat, and squad control.
