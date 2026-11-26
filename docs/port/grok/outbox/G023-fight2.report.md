# G023 — Fight 2: Roof Shooting, Grenade Throw Facing, First Aid Facing

**Branch**: `gemini/G023-fight2-facing-roof` (base `bfab9dfe`, `origin/main`)  
**Model**: `Hydrogen`  
**Review**: self-review only (Neon model switch unavailable)  

## 1. Root Causes & Fixes

### 1) Roof Cannot Shoot (`L_Gen_Depot_1_X6`)
- **Cause 1 (Over-top fire origin)**: X6 walkable roofs have `95–100 cm` solid `WorldStatic` parapets classified as `ECoverHeight::LowCover`. `EnterCover` puts operatives in `EOperativeStance::Crouching`, where `GetMuzzleLocation()` is `Feet.Z + 85 cm`. `GetCoverFireOrigin()` previously returned `GetMuzzleLocation()` for `LowCover`, and `EvaluateShotLine` / `EvaluateBlindLine` / `GetStanceAimOrigin()` / `EvaluateReturnFire()` traced from `85 cm` (and without forward lean over the parapet), hitting the `95–100 cm` parapet and failing downward shots from roofs.
- **Cause 2 (Low-cover parapet peek sight)**: Behind `<= 105 cm` parapets (`95–100 cm`), crouched eyes (`95 cm`) were blocked by the parapet and facing sideways along the wall while `GetPeekSightPose()` required `bCornerAimActive` (creating a catch-22 where a lone roof scout never saw enemies below in order to begin aiming). Raising non-stealth peek eye height over `<= 105 cm` low cover lets roof lookouts spot PMCs below without breaking the `120 cm` low-wall crouch stealth rule.
- **Cause 3 (`SquadAutonomySubsystem` blind to hostile PMCs in cover)**: `USquadAutonomySubsystem::RunDecisions()` only populated `FEnemyView` from `UEnemyRegistrySubsystem::GetEnemyCharacters()` (`AEnemyCharacter` monsters) and ignored live visible hostile PMCs (`UPmcSquadSubsystem`). When `AutonomousSquadCombat` was enabled on X6, `DecideInCover` saw `Nearest == nullptr` against PMCs and forced `Hold` (`Operative.bCoverHoldFire = true`), suppressing all automatic fire from cover.
- **Fix**:
  - Added `CoverFacingRules::OverTopFirePoint` (`Source/CodexTactics/Public/Tactics/CoverFacingRules.h`, `Source/CodexTactics/Private/Tactics/CoverFacingRules.cpp`) rising to at least standing height / `WallTopCm + ClearanceCm` (clamped) and leaning `LeanCm` into `-WallNormal`, wired into `GetCoverFireOrigin()`, `GetStanceAimOrigin()`, `EvaluateShotLine()`, `EvaluateBlindLine()`, and `EvaluateReturnFire()`.
  - Enabled non-stealth lookout `OverTop` peek sight and elevated eye origin over `<= 105 cm` low-cover parapets in `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp` (preserving `120 cm` crouch hiding and all `PeekSightRules` unit tests).
  - Updated `USquadAutonomySubsystem` (`Source/CodexTactics/Public/Characters/SquadAutonomySubsystem.h`, `Source/CodexTactics/Private/Characters/SquadAutonomySubsystem.cpp`) to include live hostile PMCs visible to the player's squad (`UPmcSquadSubsystem::IsVisibleToTeam(PmcTeam::Player, Pmc)`).

### 2) Grenade Throw Facing (Operative & PMC `G014`)
- **Cause**: `UGrenadeSubsystem::ThrowAt` (shared by player cursor throws, squad AI `TryAIGrenadeThrow`, and PMC `G014` in `PmcSquadSubsystem.cpp`) immediately broadcast `OnGrenadeThrowNative` and started `ReleaseDelay` without turning the thrower toward `Target`, while `AOperativeCharacter::UpdateCombatFacing` kept turning the actor toward combat targets / sector facing during the throw.
- **Fix**:
  - Added pure alignment rules in `GrenadeRules` (`ThrowYaw`, `ThrowErrorDeg`, `NeedsPreTurn`, `StepThrowYaw`, `TurnRateDegPerSec = 900.f`, `PreTurnTriggerDeg = 15.f`, `PreTurnDoneDeg = 5.f`).
  - Added `SetGrenadeFacingLock` / `GrenadeFacingYaw` on `AOperativeCharacter` enforced in `AOperativeCharacter::UpdateCombatFacing`.
  - Upgraded `UGrenadeSubsystem` to `UTickableWorldSubsystem` and added `SetPreTurnPending` / `StartRelease` / `HasReleased` on `AGrenadeActor` so misaligned throws (`> 15 deg`) smoothly turn toward `Target` BEFORE broadcasting `OnGrenadeThrowNative` and starting the release countdown, holding throw facing until the grenade leaves the hand (with cleanup on death, `DoorFacingLock`, `ClearThrowFacing`, and transition to `TurnBased`).

### 3) First Aid Facing
- **Cause**: `UFirstAidSubsystem::BeginTreating` immediately started `PlayClips` even when `Helper` faced up to `180 deg` away (`HelperTurnDegPerSecond = 180`), `Tick` cleared `bHelperFacing` after reaching the target angle once instead of continuously holding it, `Face` was not continuously updated from `Treatment.PatientBaseYaw`, and `UpdateCombatFacing` / `CanShoot` still rotated the medic toward combat threats mid-montage.
- **Fix**:
  - Added `FirstAidRules::HelperFaceYaw` (using `Treatment.PatientBaseYaw`, unaffected by lying-patient capsule counter-rotation) and `FirstAidRules::IsHelperFacingReady`.
  - Added `UFirstAidSubsystem::IsActivelyTreating` and `FTreatment::bWaitingToFace` so `BeginTreating` smoothly pre-turns `Helper` to within `<= 15 deg` of the patient's chest before `PlayClips` starts, continuously maintains `HelperFaceYaw` for the full montage duration, and suppresses combat facing (`UpdateCombatFacing`) and weapon firing (`CanShoot`) while actively treating.

## 2. Files Changed
- `Scripts/test_map.json`
- `Source/CodexTactics/Public/Tactics/CoverFacingRules.h`
- `Source/CodexTactics/Private/Tactics/CoverFacingRules.cpp`
- `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp`
- `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp`
- `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp`
- `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp`
- `Source/CodexTactics/Private/Characters/OperativeCharacterWeapons.cpp`
- `Source/CodexTactics/Public/Characters/OperativeCharacter.h`
- `Source/CodexTactics/Public/Characters/SquadAutonomySubsystem.h`
- `Source/CodexTactics/Private/Characters/SquadAutonomySubsystem.cpp`
- `Source/CodexTactics/Public/Combat/GrenadeRules.h`
- `Source/CodexTactics/Private/Combat/GrenadeRules.cpp`
- `Source/CodexTactics/Public/Combat/GrenadeActor.h`
- `Source/CodexTactics/Private/Combat/GrenadeActor.cpp`
- `Source/CodexTactics/Public/Combat/GrenadeSubsystem.h`
- `Source/CodexTactics/Private/Combat/GrenadeSubsystem.cpp`
- `Source/CodexTactics/Public/Survival/FirstAidRules.h`
- `Source/CodexTactics/Private/Survival/FirstAidRules.cpp`
- `Source/CodexTactics/Public/Survival/FirstAidSubsystem.h`
- `Source/CodexTactics/Private/Survival/FirstAidSubsystem.cpp`
- `Source/CodexTacticsTests/Private/Tactics/CoverFacingTest.cpp` (`FCoverOverTopFirePointTest`)
- `Source/CodexTacticsTests/Private/Combat/GrenadeRulesTest.cpp`
- `Source/CodexTacticsTests/Private/Survival/FirstAidRulesTest.cpp`
- `Source/CodexTacticsDev/Private/Debug/FirstAidSmokeCommand.cpp` (max chest yaw error check during montage)
- `Source/CodexTacticsDev/Private/Debug/RoofFightSmokeCommand.cpp` (new X6 repro smoke covering all 3 fixes)

## 3. Verification (after final edit)
- `Scripts/test.ps1 -CheckMap`: **PASS** (`Impact map OK: every smoke command is registered and mapped.`)
- `python Scripts/Tools/machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-G023`: **PASS** (`Result: Succeeded`, `561.45 s` on worker, 0 errors).
- `Scripts/test.ps1 -Filter "CodexTactics.Tactics.Cover+CodexTactics.Combat.Grenade+CodexTactics.Survival.FirstAid+CodexTactics.Combat.FireGate+CodexTactics.Combat.SquadFire+CodexTactics.Combat.Sight+CodexTactics.Squad" -SkipSmokes`: **164 tests, 0 failed**.
- `python Scripts/Tools/machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-G023 --smokes RoofFightSmoke --jobs 1`: **PASS** (`62.5 s`, `/Game/Maps/Generated/L_Gen_Depot_1_X6`):
  - Walkable X6 roof (`B_hall_n`) `LowCover` parapet -> `HasUsefulFireLine` clear to `24–36 m` ground PMC, still blocked by full wall; roof shooter fired and hit ground PMC (`HP 500 -> 462`, clip `25/30`).
  - Operative grenade throw (`153 deg` off initial yaw) release error: `0.0 deg` (`<= 20 deg`).
  - PMC `G014` grenade throw (`113 deg` off initial yaw) release error: `0.0 deg` (`<= 20 deg`).
  - FirstAid helper vs patient chest max yaw error across montage (`359` frames each): behind (`180 deg` start) `9.0 deg` (`<= 30 deg`), beside (`90 deg` start) `9.0 deg` (`<= 30 deg`).
- Archived map smokes (`FirstAidSmoke`, `GrenadeSmoke`, `GrenadeAnimSmoke`, `AIGrenadeSmoke`, `FireGateSmoke`, `CoverSmoke`): not run because `Content/Maps/L_MovementTest.umap` is absent/archived and `smoke_parallel.ps1` rejects archived-map smokes without `-ForceMap` under kept-map-only rules; all 3 scenarios are covered end-to-end on kept map `L_Gen_Depot_1_X6` via `RoofFightSmoke` plus unit tests.
