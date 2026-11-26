# 145: Look Direction Lock (`Shift+Click` World Yaw+Pitch) & Part B Arrival Facing

- **Branch / Worktree**: `gemini/145-look-direction-lock` (`C:\Work\Lanes\CodexTactics-145`)
- **Base Merged**: `origin/main` (including `135`, `142`, and `b65ec5f7` / `d3f6dfae`) at `fa48efaf`.

## Summary of Changes

### Part A — Fixed World Look Direction (`Shift+Click` variant B) & Multi-select Shared Yaw
- **`LookHoldRules` (`Source/CodexTactics/Public/Tactics/LookHoldRules.h`, `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp`)**:
  - Added `FLookDirection { float YawDeg; float PitchDeg; }`, `DirectionFromClick(OperativeLocation, ClickPoint)`, and `ProjectLookPoint(OperativeLocation, YawDeg, PitchDeg, DistanceCm = 1000.f)`.
- **`AOperativeCharacter` (`Source/CodexTactics/Public/Characters/OperativeCharacter.h`, `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp`)**:
  - Replaced point-based lock tracking with `(ManualLookYawDeg, ManualLookPitchDeg)` via `SetManualLookDirection(YawDeg, PitchDeg)`, preserving `SetManualLookPoint`, `SetStealthManualLookTarget`, `GetStealthManualLookTarget`, and `ClearManualLookPoint` compatibility.
  - `GetManualLookPoint()` projects `1000 cm` from `GetActorLocation()` along the stored `(ManualLookYawDeg, ManualLookPitchDeg)` so turns along L-shaped paths or cover corners preserve a constant compass direction.
  - `SetStealthMode(true)` now clears `bExplicitFacing = false` so a previous facing order cannot block stealth look hold.
- **`ACodexTacticsPlayerController` (`Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp`)**:
  - Both non-stealth and stealth multi-select `Shift+click` compute `SharedDir` once from the first valid operative (`LookHoldRules::IsValidLookPoint`) and apply it uniformly across all valid selected operatives, reporting HUD/callout messages on that valid reference operative.

### Roof Downward Aim Offset Pitch (`#1398`)
- **`UOperativeAnimInstance::UpdateAimOffset` (`Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp`)**:
  - Added `bLookHoldPitch` (gated on `!bInCover && !Operative.HasOwnFacing() && !bIsSprinting && Operative.GetActiveLookPoint(LookPoint)`) so Shift+clicking ground below a roof drives `AimPitchTarget` from `ManualLookPitchDeg` with `AimYawTarget = 0` (body yaw stays controlled by `UpdateCombatFacing`).
  - Both `bCornerLookPoint` and `bLookHoldPitch` read `GetManualLookPitchDeg()` directly when `HasManualLookPoint()` is true, eliminating muzzle-height Z parallax.

### Part B — Arrival Facing Log & Stale State Fixes (`#1394`, `#1397`)
- **Arrival Facing Log (`OperativeCharacterMovement.cpp`, `OperativeCharacterAim.cpp`)**:
  - Added once-per-move `[ArrivalFacing]` log (`rule=<manual look|watch sector|F-1 return|idle facing|threat turn|cover facing|combat target|door lock|grenade lock|first aid|travel> yaw=... actor=... travel=...`).
- **Stale Sources Fixed**:
  - `OrderMoveTo`: clears `ClearIdleFacing()` and `bExplicitFacing = false` before `ShouldDeferMove()` / early climb returns; clears stale `bFoughtThisFight` & `LastThreat` when issuing a non-autonomous move out of active contact (`CurrentCombatTarget` invalid/dead, not under fire, `>5 s` since last shot/contact); gates sprint look-clear on `CanSprint()`.
  - `HandleMoveFinished`: computes `ArrivalTravelYaw` using 2D horizontal velocity (`SizeSquared2D() > 100.f`).
  - `UpdateCombatFacing`: keeps `LastThreatContactTime` fresh during cover firefights (`bInCover`), expires stale `bFoughtThisFight` after `20 s` without threat contact (preserving `RetreatFacingSmoke`), and adds `!bFacingWall` to `bHasIdleFacing`.
  - `OrderMantleTop` / `FinishMantle` (`OperativeCharacterMantle.cpp`) and `ResetTransientStateForLoad` (`OperativeCharacterHealth.cpp`): clear `IdleFacing`, `bExplicitFacing`, `ManualLookPoint`, and `bFoughtThisFight` cleanly.
  - `StealthModeSmokeCommand.cpp`: updated section 5 orthogonal stealth move yaw check to `GetManualLookYawDeg()`.

## Verification
- **Build**: `machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-145`: `Result: Succeeded` (`72.26s`, Exit code 0 after final edits).
- **CheckMap**: `Scripts/test.ps1 -CheckMap`: `Impact map OK: every smoke command is registered and mapped.`
- **Unit Tests** (`Scripts/test.ps1 -Filter "CodexTactics.Tactics.LookHold+CodexTactics.Squad.ThreatFacing+CodexTactics.Characters.StealthMode" -SkipSmokes -NoBuild`): **22 tests, 0 failed** (`11/11` StealthMode, `3/3` ThreatFacing, `8/8` LookHold incl. `DirectionLock`).
- **Dispatched Smokes** (`machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-145`, post-final-edit):
  - `LookDirectionSmoke`: **PASS** (`69.8s` on `L_Gen_Depot_1_X6`):
    - Multi-select `HandleWorldHit` Shift+click diff `0.00 deg`;
    - L-shaped walk Leg 1 + Leg 2 hold world look yaw within `+-10 deg`;
    - Roof downward Shift+click pitch -> `AnimInstance AimPitch` + skeletal bones pitched down (`spine->head` & `muzzle`);
    - Real `B_ruin` external ramp climb to roof, then roof sprint -> `[ArrivalFacing] rule=travel` held `+-15 deg` past `3.5s`;
    - `SetFacingPoint` and `ManualLookDirection` cleared on plain sprint.
  - `CoverIndoorSmoke`: **PASS** (`80.7s`).
  - `FormationScanPitchSmoke`: **PASS** (`72.4s`).
  - `TorsoDriftSmoke`: **PASS** (`73.4s`).
- **Pro Self-Review**: `research` subagent (`Model: pro`) full review (`750a290c`) + delta review (`162e2b0d`) completed; all findings (stealth multi-select/smoke, non-stealth skipped RefOp, disallowed sprint clear, AO cover/own-facing gating & parallax, 2D arrival yaw / cover contact / mantle / load resets, team-symmetric `OperativeCharacterInternal::IsLiveTargetOf`, `RoofSprintClear` `StageTime > 0.5f` guard, unused headers in `LookDirectionSmokeCommand.cpp`, zero `Content/` files committed) resolved and re-verified.
