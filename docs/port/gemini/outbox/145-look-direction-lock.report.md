# 145: Look Direction Lock (`Shift+Click` World Yaw+Pitch) & Part B Arrival Facing

- **Branch / Worktree**: `gemini/145-look-direction-lock` (`C:\Work\Lanes\CodexTactics-145`)
- **Code Commit Tested**: `402745c7` (merged `origin/main` up to `d3f6dfae`)

## Summary of Changes

### Part A — Fixed World Look Direction (`Shift+Click` variant B) & Multi-select Shared Yaw
- **`LookHoldRules`** ([LookHoldRules.h](Source/CodexTactics/Public/Tactics/LookHoldRules.h), [LookHoldRules.cpp](Source/CodexTactics/Private/Tactics/LookHoldRules.cpp)):
  - Added `FLookDirection { float YawDeg; float PitchDeg; }`, `DirectionFromClick(OperativeLocation, ClickPoint)`, `ProjectLookPoint(OperativeLocation, YawDeg, PitchDeg, DistanceCm = 1000.f)`, and `AdjustClickForFlatGround(OperativeLocation, HalfHeightCm, ClickPoint, FlatBandCm = 65.f)` so clicks near standing feet elevation remain horizontal (`0 deg` pitch) while steeper elevation clicks preserve true pitch.
- **`AOperativeCharacter`** ([OperativeCharacter.h](Source/CodexTactics/Public/Characters/OperativeCharacter.h), [OperativeCharacterStealth.cpp](Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp)):
  - Replaced point-based lock tracking with `(ManualLookYawDeg, ManualLookPitchDeg)` via `SetManualLookDirection(YawDeg, PitchDeg)`, projecting `1000 cm` from `GetActorLocation()` consistently across `SetManualLookPoint`, `SetStealthManualLookTarget`, `GetStealthManualLookTarget`, and `GetManualLookPoint`.
  - `SetStealthMode(true)` clears `bExplicitFacing = false`.
- **`ACodexTacticsPlayerController`** ([CodexTacticsPlayerControllerOrders.cpp](Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp)):
  - Both stealth and non-stealth multi-select `Shift+click` compute one `SharedDir` (using `AdjustClickForFlatGround`) from the first valid operative (`LookHoldRules::IsValidLookPoint`) and apply it uniformly to all valid selected operatives.

### Roof Downward Aim Offset Pitch (`#1398`)
- **`UOperativeAnimInstance::UpdateAimOffset`** ([OperativeAnimInstance.cpp](Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp)):
  - Added `bLookHoldPitch` (`!bCornerLookPoint && !bInCover && !Operative.IsInStealthMode() && !Operative.HasOwnFacing() && !bIsSprinting && Operative.GetActiveLookPoint(LookPoint)`) driving `AimPitchTarget` from `ManualLookPitchDeg` with `AimYawTarget = 0`, clamped to `ActiveAimOffset` BlendSpace axis limits.

### Part B — Arrival Facing Log, Ramp-to-Roof Fix & Round Reset
- **Arrival Facing Log & Threat Decay** ([OperativeCharacterAim.cpp](Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp), [OperativeCharacterMovement.cpp](Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp)):
  - Added once-per-move `[ArrivalFacing]` log and `2D` horizontal velocity check (`SizeSquared2D() > 100.f`) in `HandleMoveFinished`.
  - Cleared `IdleFacing`, `bExplicitFacing`, sprint-look (including immediate clear on prone -> double-click sprint before `RiseDelay`, with `LOOK CLEARED` gated on `IsLookHoldEligible()`), and stale `bFoughtThisFight` (`> 5 s` out of active contact on non-autonomous moves; `20 s` stationary decay past `LastThreatContactTime`; `!WaveCombat` reset moved above early cover/interaction returns).
  - Guarded idle facing with `!bFacingWall`.
- **Mantle & Round/Region/Load Resets** ([OperativeCharacterMantle.cpp](Source/CodexTactics/Private/Characters/OperativeCharacterMantle.cpp), [OperativeCharacterHealth.cpp](Source/CodexTactics/Private/Characters/OperativeCharacterHealth.cpp), [MissionSubsystem.cpp](Source/CodexTactics/Private/Core/MissionSubsystem.cpp), [TrainingSectorSubsystem.cpp](Source/CodexTactics/Private/Training/TrainingSectorSubsystem.cpp)):
  - Added `AOperativeCharacter::ClearLookAndFacingState()` (clearing manual look, idle/explicit facing, `bFoughtThisFight`, `LastThreat`, `LastThreatContactTime = -1000.0`) and wired it into `ResetTransientStateForLoad`, `UMissionSubsystem::StartCombatMode`, `UMissionSubsystem::HandleGameFlowChanged` (`bCutsceneEnded`), and `UTrainingSectorSubsystem::MovePlayersToSector`; cleared `IdleFacing` and `bExplicitFacing` in `OrderMantleTop` / `FinishMantle`.
- **Tests & Smoke Updates** ([LookHoldRulesTest.cpp](Source/CodexTacticsTests/Private/Tactics/LookHoldRulesTest.cpp), [LookDirectionSmokeCommand.cpp](Source/CodexTacticsDev/Private/Debug/LookDirectionSmokeCommand.cpp), [LookHoldSmokeCommand.cpp](Source/CodexTacticsDev/Private/Debug/LookHoldSmokeCommand.cpp), [StealthModeSmokeCommand.cpp](Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp), [test_map.json](Scripts/test_map.json)).

## Verification (on `402745c7`)
- **Build**: `machine_dispatch.py build` on `codex-worker`: `Result: Succeeded` (`397.15 s` compile; exit `0`).
- **Impact Map**: `Scripts/test.ps1 -CheckMap`: `Impact map OK: every smoke command is registered and mapped.`
- **Unit Tests** (`Scripts/test.ps1 -Filter "CodexTactics.Tactics.LookHold+CodexTactics.Squad.ThreatFacing+CodexTactics.Characters.StealthMode+CodexTactics.Anim+CodexTactics.Movement+CodexTactics.Cover+CodexTactics.Characters.Mantle+CodexTactics.Mission+CodexTactics.Training.Sector" -SkipSmokes`): **89 tests, 0 failed**.
- **Smokes** (`machine_dispatch.py smoke` on `codex-worker`, `L_Gen_Depot_1_X6`):
  - `LookDirectionSmoke`: **PASS** (`43.7 s`, zero re-runs) — multi-select `HandleWorldHit` level `0.0 deg` pitch & shared `90.0 deg` yaw (`diff 0.00 deg`); both `Leader` (`0/43`, `0/30`) and `Mate` (`0/44`, `0/30`) hold locked yaw along both L-path legs; roof downward Shift+click pitches real skeleton (`AimPitch -47.5 deg`, `alpha 1.00`, `spine->head 72.3 -> 52.8`, `muzzle 1.4 -> -43.0`); `B_ruin` ramp climb to roof (`feet Z 602 vs roof 600`) -> roof sprint `[ArrivalFacing] rule=travel yaw=85.2` held past `3.5 s` (`off 4.4 deg`, `meshDrift 0.0`); `ClearLookAndFacingState()` clears look state; `SetFacingPoint` (`off 0.1`) & `ManualLook` (`off 0.2`) clear on sprint.
  - `TorsoDriftSmoke`: **PASS** (`39.2 s`).
  - `RoofFightSmoke`: **PASS** (`40.3 s`).
  - `CoverIndoorSmoke`: **PASS** (`40.7 s`).
- **Pro Self-Review**: `research` subagent (`Model: pro`) reviewed both the initial diff and `4c4124c5..402745c7`, returning verdict `MERGE-READY` with 0 Blocking/Important findings.

## What Was NOT Checked
- Full `verify_all.ps1` across every unmapped smoke in the project (the 4 targeted X6 smokes ran and passed).
- Retired/archived map smokes (`StealthModeSmoke`, `LookHoldSmoke`, `RetreatFacingSmoke`), which `smoke_parallel.ps1` rejects unless given `-Map` with `-ForceMap`.
