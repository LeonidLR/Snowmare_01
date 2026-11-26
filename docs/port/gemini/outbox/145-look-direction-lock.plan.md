# 145 Plan — Shift+Click Look Direction Lock (Variant B)

**Branch**: `gemini/145-look-direction-lock` (from `origin/main`)  
**Model**: Hydrogen

## 1. Pure Rules (`LookHoldRules`)
- Add `FLookDirection { float YawDeg; float PitchDeg; }`, `DirectionFromClick(OperativeLocation, ClickPoint)`, and `ProjectLookPoint(OperativeLocation, YawDeg, PitchDeg, DistanceCm)`.

## 2. Operative & Controller Integration
- `AOperativeCharacter`: store `ManualLookYawDeg` / `ManualLookPitchDeg` on `SetManualLookPoint` / `SetStealthManualLookTarget`, add `SetManualLookDirection(YawDeg, PitchDeg)`, and have `GetManualLookPoint` project along `(ManualLookYawDeg, ManualLookPitchDeg)` from `GetActorLocation()` so the world compass direction stays fixed while walking or at a cover corner.
- `CodexTacticsPlayerControllerOrders.cpp`: when multiple operatives are selected, compute the locked direction from the primary operative (`Targets[0]`) and apply the same world yaw to all selected operatives.

## 3. Verification
- Unit test `CodexTactics.Tactics.LookHold.DirectionLock` in `LookHoldRulesTest.cpp`.
- Extend `LookHoldSmokeCommand.cpp` with an L-shaped walk after Shift+click east, checking look yaw stays east `+-10 deg` on both legs.
- Worker build + `CodexTactics.Tactics.LookHold` + `LookHoldSmoke`, `TorsoDriftSmoke`, `CoverIndoorSmoke`.
