# 135 Plan — Corner Look Body Turn, Move Off Corner Peek, Left Corner Slot, Watch Sector Dedupe

**Branch**: `gemini/135-corner-look-body` (from `origin/main` + `gemini/G023-fight2-facing-roof` merged)  
**Model**: Hydrogen

## Part A — Corner peek -> cover order ignored
- Ensure `LeaveCover` and `OrderMoveTo` release `PeekStanceYawLock` and any corner-peek hold cleanly.
- Add repro in `TorsoDriftSmokeCommand.cpp` (or `CornerSlotSmokeCommand.cpp`): corner peek -> 2-click / cover order >= 5 m away -> must leave corner and arrive at target cover.

## Part B — Corner look turns body both ways (incl. standing at corner)
- `OperativeCharacterCornerAim.cpp`: add `IsAtPeekableCorner()`, keep `IsCornerHoldSpot()` alive while `bCornerLookPose && bHasManualLookPoint` (matching `IsManualCornerPeekSpot()`), and include `IsAtPeekableCorner()` in `WantsCornerLookPose()`.
- `OperativeCharacterStealth.cpp`: `SetManualLookPoint` arms `bCornerLookPose` when `IsPeekingRoundCorner() || IsAtPeekableCorner()`.
- `OperativeCharacterAim.cpp`: `GetStanceAimBaseDirection()` uses `CornerAimDirection` when `WantsCornerLookPose()` is true.
- `OperativeAnimInstanceCoverSet.cpp`: `UpdateCoverSetAimOffset()` runs when `bCornerLookPoseApplied` is true even if `!bCoverInFirePose` (standing at a corner).
- Extend `CoverIndoorSmokeCommand.cpp` to assert `AimYaw` on BOTH right and left looks separately.

## Part C — Left corner slot too far from edge
- `CoverFacingRules.h`: align `StandStandOffPackLCm = 34.f` (matching `StandStandOffPackRCm = 34.f <= 35 cm`) and `CrouchStandOffPackLCm = 56.f` (matching `CrouchStandOffPackRCm = 56.f`).
- Unit test in `CoverFacingTest.cpp` verifying left/right `CornerStandOff` equality `<= 35 cm`.

## Part D — Dedupe "Watch sector locked!"
- `CodexTacticsPlayerControllerOrders.cpp`: post `"Watch sector locked!"` only when transitioning into a look lock (`!bHadManualLookPoint`), not on every repeated Shift+click.
