# 135 Report — Corner Look Body Turn, Move Off Corner Peek, Left Corner Slot, Watch Sector Dedupe

**Branch**: `gemini/135-corner-look-body` (from `origin/main` + `gemini/G023-fight2-facing-roof`)  
**Model**: Hydrogen  
**Review**: Neon review completed (3 findings fixed in `0fbc4383` and `6340bed9`: `OperativeCharacter.h` `CoverStandOffStandPackLCm=34.f` / `CoverStandOffCrouchPackLCm=56.f` + `CornerLeanExtraPackLCm=-26.f` + `BP_Operative.uasset` verified no override; `bAtCoverCorner` preserved on `CornerHold` cross-normal look; `UpdateCoverSetAimOffset` uses `bCoverInFirePose ? CoverFirePoseFacing : CoverFacing`).

## 1. Changes Implemented

### Part A — Operative at a corner peek ignores a move order to a cover on the other side
- `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp` (`LeaveCover`): explicitly calls `Anim->ReleasePeekStanceYawLock()` when leaving cover so any active peek-stance yaw lock is released immediately on a move/cover order off the corner.
- `Source/CodexTacticsDev/Private/Debug/CornerSlotSmokeCommand.cpp`: added `EStep::MoveOffCorner` verifying that from a corner peek, an `OrderTakeCover` to another cover >= 5 m away is accepted, leaves the corner, and arrives (`moved 2200 cm`).

### Part B — Corner look turns the body both ways (including standing at a corner)
- `Source/CodexTactics/Public/Characters/OperativeCharacter.h`, `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp`:
  - Added `IsAtPeekableCorner()` (`bInCover && !bShimmying && !bCoverSnapPending && CurrentCoverHeight != ECoverHeight::None && (IsManualCornerPeekSpot() || IsCornerHoldSpot() || (bAtCoverCorner && CoverSlot.IsEdgeExposed(CoverFacing)))`).
  - Updated `IsCornerHoldSpot()` to stay active when `bCornerLookPose && bHasManualLookPoint && bAtCoverCorner`, preventing `UpdateCornerAim` from dropping the corner pose when a Shift-click look crosses the wall normal to the opposite side.
  - Updated `WantsCornerLookPose()` to accept `IsPeekingRoundCorner() || IsAtPeekableCorner()`.
- `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp` (`SetManualLookPoint`): arms `bCornerLookPose = true` when `IsPeekingRoundCorner() || IsAtPeekableCorner()`.
- `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp` (`GetStanceAimBaseDirection`): includes `WantsCornerLookPose()` so standing at a corner without `bCornerAimActive` uses `CornerAimDirection`.
- `Source/CodexTactics/Private/Characters/OperativeAnimInstanceCoverSet.cpp` (`UpdateCoverSetAimOffset`): allows `ActiveAimOffset` binding when `bCornerLookPoseApplied` is true even if `!bCoverInFirePose` (standing idle at a corner).
- `Source/CodexTacticsDev/Private/Debug/CoverIndoorSmokeCommand.cpp`: split `bSawYawOff` into `bSawYawRight` and `bSawYawLeft` and asserted both.

### Part C — Left corner slot too far from the edge
- `Source/CodexTactics/Public/Characters/OperativeCharacter.h`: aligned `CoverStandOffStandPackLCm = 34.f` and `CoverStandOffCrouchPackLCm = 56.f` (verified `BP_Operative.uasset` has no `CoverStandOff` override) and set `CornerLeanExtraPackLCm = -26.f` so the `_L` clip's root step stays within the `25 cm` shoulder gate while the capsule stands at `34 cm`.
- `Source/CodexTactics/Public/Tactics/CoverFacingRules.h`: aligned `StandStandOffPackLCm = 34.f` (matching `StandStandOffPackRCm = 34.f <= 35 cm`) and `CrouchStandOffPackLCm = 56.f` (matching `CrouchStandOffPackRCm = 56.f`).
- `Source/CodexTacticsTests/Private/Tactics/CoverFacingTest.cpp`, `Source/CodexTacticsTests/Private/Tactics/CoverCrouchTest.cpp`: updated `CornerPoseAndSnap`, `CornerStandOffPerClipSide`, and `CrouchClipSides` for symmetric `34 cm / 56 cm` stand-offs and asserted left/right equality `<= 35 cm`.

### Part D — Dedupe `"Watch sector locked!"`
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp`: posts `"👁️ [{0}]: Watch sector locked!"` only when at least one target operative transitions into a manual look lock (`!Op->HasManualLookPoint()`), avoiding repeated spam on consecutive Shift+clicks.

## 2. Verification
- `Scripts/test.ps1 -CheckMap`: **PASS** (`Impact map OK: every smoke command is registered and mapped.`)
- Worker build (`machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-135`): **PASS** (`573.96s` on `9b2e1c99`, incremental rebuild on `07d003c0`).
- Worker smokes (`machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-135 --smokes CornerSlotSmoke,CoverIndoorSmoke,TorsoDriftSmoke --jobs 2`):
  - `CornerSlotSmoke`: **PASS** (`42.1s`), incl. `135 Part A: cover order from corner peek accepted (0)` and `135 Part A: left corner peek and arrived at target cover (moved 2200 cm)`.
  - `CoverIndoorSmoke`: **PASS** (`45.7s`), incl. `the look drives aim yaw both right and left` (both standing and crouched).
  - `TorsoDriftSmoke`: **PASS** (`38.0s`).
- Unit tests (`Scripts/test.ps1 -Filter "CodexTactics.Tactics.Cover+CodexTactics.Tactics.Torso+CodexTactics.Combat.Sight.Peek" -SkipSmokes`): **83 tests, 0 failed** (`CodexTactics.Tactics.Cover.CornerPoseAndSnap`, `CornerStandOffPerClipSide`, `CrouchClipSides`, `LookAo`, `Torso.Reset`, `Combat.Sight.Peek*`).
