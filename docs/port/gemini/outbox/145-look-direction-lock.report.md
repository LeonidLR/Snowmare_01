# 145: Look Direction Lock (`Shift+Click` fixed world yaw+pitch)

- **Branch / Worktree**: `gemini/145-look-direction-lock` (`C:\Work\Lanes\CodexTactics-145`)
- **Base**: `origin/main` (`10963d3a`, includes `135`), merged at `c44b4a23`.

## Changes
1. **`LookHoldRules` (`Source/CodexTactics/Public/Tactics/LookHoldRules.h`, `Source/CodexTactics/Private/Tactics/LookHoldRules.cpp`)**:
   - Added `FLookDirection { float YawDeg; float PitchDeg; }`, `DirectionFromClick(From, ClickWorld)` (safe horizontal fallback when click is directly above/below, `NormalizeAxis` cast to `float`), and `ProjectLookPoint(From, Dir, DistanceCm = 3000.f)`.
2. **Operative Look Lock (`Source/CodexTactics/Public/Characters/OperativeCharacter.h`, `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp`)**:
   - Replaced world-point anchor lock with `ManualLookYawDeg` / `ManualLookPitchDeg` (`SetManualLookDirection(YawDeg, PitchDeg)`), while keeping `SetManualLookPoint` / `SetStealthManualLookTarget` / `ClearManualLookPoint` / `HasManualLookPoint` compatible.
   - `GetManualLookPoint()` now projects `3000 cm` from `GetActorLocation()` along `(ManualLookYawDeg, ManualLookPitchDeg)` so the operative maintains a constant world facing direction regardless of lateral/L-shaped movement or corner distance.
3. **Multi-select Shared Yaw (`Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp`)**:
   - Computes `SharedDir` once from `Targets[0]->GetActorLocation()` to `Hit.ImpactPoint` and applies `SetManualLookDirection(SharedDir.YawDeg, SharedDir.PitchDeg)` to all selected operatives, preserving `135`'s `bNewlyLocked` message deduplication.
4. **Tests (`Source/CodexTacticsTests/Private/Tactics/LookHoldRulesTest.cpp`, `Source/CodexTacticsDev/Private/Debug/LookHoldSmokeCommand.cpp`)**:
   - Added `CodexTactics.Tactics.LookHold.DirectionLock` unit test verifying `DirectionFromClick` + `ProjectLookPoint` constant world yaw across two moved positions.
   - Updated `LookHoldSmokeCommand.cpp` walk & jog stages to follow an L-shaped path and gate `ManualLookYawDeg` within `+-10 deg` of the initial locked yaw.

## Verification
- **Worker Build** (`machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-145` at `c44b4a23`): `Result: Succeeded` (`387.23s`).
- **Unit Tests** (`Scripts/test.ps1 -Filter "CodexTactics.Tactics.LookHold" -SkipSmokes`): `8 tests, 0 failed` (`ClearingMatrix`, `Config`, `DirectionLock`, `Eligibility`, `GaitStart`, `Sectors`, `TwistLimits`, `Validity`).
- **Dispatched Smokes** (`machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-145 --smokes TorsoDriftSmoke,CoverIndoorSmoke --jobs 2`):
  - `TorsoDriftSmoke`: `PASS` (`45.3s`)
  - `CoverIndoorSmoke`: `PASS` (`48.9s`)
- **Note on `LookHoldSmoke`**: `LookHoldSmoke` is listed as an archived-map smoke (`L_MovementTest`) in `Scripts/smoke_parallel.ps1` and requires `-Map ... -ForceMap` (which `machine_dispatch.py smoke` does not expose).
