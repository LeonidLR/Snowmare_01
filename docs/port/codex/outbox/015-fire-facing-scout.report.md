# 015 fire-facing scout (READ-ONLY, Claude Sonnet 5.5)

Paths are relative to `Source/CodexTactics/Private/Characters/` unless noted. Debug smokes live in `Source/CodexTacticsDev/Private/Debug/`.

## 1. Fire paths and the gate each one passes

All operative rifle rounds end in `ShootAtTarget` (OperativeCharacterCombat.cpp:1005). It has NO facing or raise check: only target, ammo, `SniperShotAllowed`, `LauncherShotAllowed` (:1007). Every gate therefore lives in the callers.

| Path | Caller | Facing / raise gate before the round |
|---|---|---|
| Auto-fire, open (standing / crouched / prone, PMC too) | `RunCombatShooting` (Combat.cpp:591), `ShootAtTarget` at :931 | `TurnAimTowards` + `GetShotAimResidualDeg(AimAt) > AimConeDeg` (:869-875; AimConeDeg=30, OperativeCharacter.h:1864). The residual includes the 058c body arc (Aim.cpp:365-385). Then `HoldShotForInjuredRaise` (:913). |
| Auto-fire from cover (high / low, corner) | same, `bInCover` branch :878-909 | Only `IsFacingAligned` (:889; Tactics/CoverFacingRules.cpp:77), which compares the body yaw with the cover slot's facing, NOT with the target. Residual only for `IsCornerHoldSpot()` (:907). Then `HoldShotForInjuredRaise` (:913). No body-arc gate. |
| Auto-fire, "StepOut" from cover | `BeginOpenShotFromCover` (:844-849) | Falls into the open branch, but if `bInCover` is still true (shimmying) the cover gates apply. |
| Quick fire (Ctrl+click), open | QuickFire.cpp:238-253 | `TurnAimTowards`, residual > cone, `HoldShotForInjuredRaise` (:241), then `ShootAtTarget` at :269. |
| Quick fire from cover | QuickFire.cpp:236-247 | `UpdateCoverFacing` + `BeginCoverShot` only. No residual, no `IsFacingAligned`, no raise hold. |
| Suppress (Shift+click), open | Suppression.cpp:354-367 | Same as quick fire open (raise hold at :359). |
| Suppress from cover | Suppression.cpp:348-353 | `UpdateCoverFacing` + `BeginCoverShot`. No residual, no raise hold. |
| Targeted shot at an object (barrel, mine, crate) | TargetedShots.cpp:91-160 (`ShootAtObject`); callers Core/CodexTacticsPlayerControllerOrders.cpp:833-865 and Sniper.cpp:134 | `StopAndFace` = instant `SetActorRotation` (Aim.cpp:316-325), no residual, no raise hold. It does NOT go through `ShootAtTarget`: it broadcasts `OnWeaponFired` itself (TargetedShots.cpp ~148). In cover only `UpdateCoverFacing` + `BeginCoverShot`. |
| Sniper | `PrepareSniperShot` (Combat.cpp:826-833, QuickFire.cpp:208) | The sniper residual is body-forward based (Aim.cpp:446-452). The same cover holes apply. |
| Launcher (RPG-7) | `ShootAtTarget` -> `FireLauncherAt` (Combat.cpp:1043-1044; Launcher.cpp:62) | The gates of whichever path called it. |
| Prone | open branch | `GetShotAimResidualDeg` uses `bProneArc` (Aim.cpp:370-372): max(pose residual, body error). |
| PMC operatives | same `RunCombatShooting` (OperativeCharacter.cpp:368) | No separate path: same gates, same cover holes. |

Enemy classes (`AEnemyCharacter`, `AMarksmanEnemyCharacter::Fire`, Public/Characters/MarksmanEnemyCharacter.h:170) are a separate path and were not scouted (needs deeper look: MarksmanEnemyCharacter.cpp, if "enemies shoot first" is also wanted).

## 2. Corner case: crouched + badly wounded + cube

Code reading only, not reproduced. Three holes stack:

1. A cube means `bInCover`. `GetShotAimResidualDeg` returns early for `bInCover` (Aim.cpp:373-376, "the cover poses have their own facing gates"), so the 058c body arc (Aim.cpp:377-385) never applies in cover.
2. The cover branch of `RunCombatShooting` gates the residual only for `bMayExpose && IsCornerHoldSpot()` (Combat.cpp:907). A cube in the open is not a corner-hold spot, so no target residual check runs. Only `IsFacingAligned` runs (wall facing, not the target). The body stays turned to the wall while `ShootAtTarget` / `NoteShotAim` aims the round at the enemy.
3. The wounded raise hold is blind here. `HoldShotForInjuredRaise` (Combat.cpp:935) reads `IsInjuredRaisePending` (OperativeAnimInstanceInjured.cpp:140), which is `InjuredUpperShare > 0.02` (InjuredLocomotionRules.cpp:125). `InjuredUpperShare = LayerWeight * (1-Raise) * ...` (InjuredLocomotionRules.cpp:116-124; AnimInstanceInjured.cpp:368). `LayerWeight` (`InjuredWeight`) comes from `IsInjuredLayerEligible`, which is false when `bIsCrouching` or `bInCover` (AnimInstanceInjured.cpp:86). So crouched or in cover `InjuredWeight` is 0, the raise is never "pending", and the round is not held. Crouched + wounded only gets the "cue only" hunch (AnimInstanceInjured.cpp:255-260). `IsInjuredRaisePending` also returns false when the anim did not tick for 0.25 s (:142-144).

Not verified: where the barrel actually points in cover. Needs deeper look: `GetPoseAimResidualDeg` (Aim.cpp:437) and `DecideCoverFireAtPoint` (OperativeCharacterCornerAim.cpp:311).

## 3. Where "weapon raised" lives

- Only the wounded case has a real raise state: `UOperativeAnimInstance::InjuredRaise` (Public/Characters/OperativeAnimInstance.h:2195), stepped by `StepRaiseShare` with `raise_to_fire_s` (InjuredLocomotionRules.cpp:104-114). Demand: `DemandInjuredRaise` (AnimInstanceInjured.cpp:130). Query: `IsInjuredRaisePending` (:140).
- Fire paths that wait for it: auto-fire (Combat.cpp:913), quick fire open (QuickFire.cpp:241), suppress open (Suppression.cpp:359). Not waiting: every cover fire path, `ShootAtObject`.
- Healthy operatives have no raise state at all. `IsCoverFireReady` (Cover.cpp:923-945) feeds only the anim and overlays (AnimInstance.cpp:1192, Overlays.cpp:62), never a fire gate. `bIsAiming && AimTimer <= 0` (AnimInstanceInjured.cpp:231) is an anim signal, not a gate.

## 4. Proposed single gate

One new `AOperativeCharacter` function, e.g. `bool IsBarrelOnTargetAndRaised(const FVector& AimAt)`, next to `GetShotAimResidualDeg` (Aim.cpp:365). It does the following:
- Barrel on target: split the `bInCover` early return in `GetShotAimResidualDeg` so cover keeps its pose residual and also checks the body error to the target (the check `IsFacingAligned` cannot give).
- Raised: `HoldShotForInjuredRaise()`, extended so that crouched / in-cover wounded also count (use `InjuredRaise < 1` or the cue-only pose, not `InjuredUpperShare`).

Callers: replace the ad-hoc checks (Combat.cpp:872, :907, :913; QuickFire.cpp:241-248; Suppression.cpp:359-363) with the helper and add it to the cover branches that have none (QuickFire.cpp:236, Suppression.cpp:348, `ShootAtObject`, auto-fire cover at Combat.cpp:921 before `BeginCoverShot`). A last-resort veto inside `ShootAtTarget` (:1005) would catch any future caller, but QuickFire.cpp:269 ignores the result, so the callers still need to see the reason.

Uncovered today: quick fire from cover, suppress from cover, `ShootAtObject` (open and cover), auto-fire from cover on a non-corner spot, any wounded crouched or in-cover shot.

## Smokes (Scripts/test_map.json smokeRegistry; commands in Source/CodexTacticsDev/Private/Debug/)

| Path | Smoke | Note |
|---|---|---|
| Open auto-fire, body arc, prone | `FireArcSmoke` (PmcCombatBugsSmokeCommand.cpp:1663), `ProneCrawlSmoke` | prone focus; no standing / crouched 058c smoke found by name (needs deeper look: grep 058c in Debug) |
| Quick fire | `QuickFireSmoke` | |
| Suppress | `SuppressionSmoke` | |
| Cover, corner hold | `CornerHoldSmoke`, `CornerEntrySmoke`, `CornerStandoffSmoke`, `TwoCornerHoldSmoke`, `CoverSmoke` | corner spots only |
| Targeted shots | `TargetedShotSmoke` | no facing assertion known |
| Sniper | `SniperSmoke` | |
| Wounded raise | `InjuredLocomotionSmoke`, `InjuredLocomotionDepotSmoke` | standing only: layer off crouched / in cover |
| PMC fire | `PmcFireLineSmoke`, `PmcStandoffSmoke`, `PmcUnderFireSmoke` | |
| NO smoke | wounded crouched / low-cover shot at a target to the side; quick fire / suppress from cover facing; `ShootAtObject` facing | the holes above |
