# 144 — Wall Hug Along a Wall With Windows: Duck Under, Fire Through (Report)

- **Branch / Worktree**: `gemini/144-wall-hug-window` (`C:\Work\Lanes\CodexTactics-144`), merged `origin/main` (`18b17f3d`).
- **Task / Fix Round**: `docs/port/gemini/inbox/144-wall-hug-window.md` + `144 FIX ROUND` (2026-11-28).

## Root Causes & Fix Summary

1. **`WallHugWindowSmoke` failing at Setup (`found 2 adjacent windows on X6 B_x1_10 wall`)**:
   - **Cause**: Main checkout had `Saved/Training/active_sector.json` selecting Sector 3 on `L_Gen_Depot_1_X6`. `UTrainingSectorSubsystem::BuildBoundary` places `UNavArea_Null` outside the active sector, removing navmesh at `B_x1_10_t7_store` (`X ~ -11667..-7833, Y ~ -11701`), so `FindB_x1_10_Windows` (`Nav->ProjectPointToNavigation`) failed (reproduced locally with `-ExtraArgs "-Sector=3"`, `FAIL 35.4s`).
   - **Fix**: Added `-Sector=0` to `WallHugWindowSmoke` in `Scripts/test_map.json:70`, and in `Source/CodexTacticsDev/Private/Debug/WallHugWindowSmokeCommand.cpp:353-396` cleared any active sector via `Training->SelectSector(0)` and waited up to `8.0 s` for navmesh rebuild before asserting. Verified locally both with `-Sector=0` and with `-ExtraArgs "-Sector=3"`.
2. **`FormationFenceCrossSmoke` part 1 failing in main (`144_fail_logs`)**:
   - **Cause**: All logged `FormationFenceCrossSmoke` failures in `144_fail_logs` ran with `[TrainingSector] Sector 3` active (`Saved/Training/active_sector.json` in main checkout). Under Sector 3 with random seed (`seed 16394` in `committedjson.log`, reproduced locally with `-Sector=3` `seed 24027`), the squad spawns at `(7000, 11500)` where `PMCTripwireActor_4` sits across the nearest fence (`Trap spotted (Tripwire) by Commander -> squad holds, Solo on`); with `seed 9166` (`FormationFenceCrossSmoke.log`), `places by travel` swapped wedge slots after vaulting (`Engineer` 2173 cm vs 1.5x of 1250 cm). None of 144's changes (`SquadSubsystemFormation`, `SquadSubsystemFormationSlide`, `CodexTacticsPlayerControllerOrders`, `OperativeCharacterCoverEntry`) execute during `FormationFenceCrossSmoke` part 1 (all gated on `Sectors.bWallOrder && Sectors.bSlide`, `WindowBreakage::FindWindowSillAt`, or adjacent window-edge exposure).
   - **Verification**: `FormationFenceCrossSmoke` passed `2/2` after the final edit (and `1/1` prior = `3/3` total whole-map) without loosening the smoke.
3. **`RoofFightSmoke` failing (`gateOk 0 [fire gate: the cover pose does not reach the target]`)**:
   - **Cause**: `GetCoverFireOrigin`, `IsCoverFireReady`, and `EvaluateShotLine` already kept base-main behavior for non-window `LowCover` (`IsWindowCoverConfirmed() == false`). However, in `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:448-460`, `GetStanceAimBaseDirection()` checked only `HighCover` (`bCornerAimActive || IsCornerHoldSpot()`), `IsManualCornerPeekSpot()`, and `WantsCornerLookPose()`, falling through to `GetActorRotation().Yaw` (`+WallNormal`, 180 deg away from the ground target) for non-corner `LowCover` with `bCornerAimActive`. That caused `UOperativeAnimInstance::UpdateAimOffset` (`Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp:1503-1506`) to mark the target out-of-reach (`AimYawTarget = 0`), leaving the `riffle` stand fire pose at `residual 30 deg` (`> AimConeDeg = 30.f`) whenever the first blend-in round missed.
   - **Fix**: Added `LowCover && bCornerAimActive && !IsCoverShotBlind() && !HasWindowCoverAim() && Stance != EOperativeStance::Prone` to `GetStanceAimBaseDirection()` (`Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:451-456`), using `ECoverFacing::Left` when `Anim->IsCoverOverTopFireSet()` (`Aim/2` always uses the left over-top clip/AO). Roof shooter `residual` dropped from `30 deg` to `7 deg` and `RoofFightSmoke` passed `2/2`.

## Claims & Evidence

| Claim | Evidence |
|---|---|
| Wall-hug shimmy crouches under window spans (+ margin) and stands on solid gaps, both in single-operative cover and formation wall slide | `Source/CodexTactics/Private/Characters/OperativeCharacterWindowCover.cpp:230-379`, `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp:1304-1313`, `Source/CodexTactics/Private/Characters/SquadSubsystemFormationSlide.cpp:369-401`, `Source/CodexTactics/Private/Tactics/WindowCoverRules.cpp:121-220`; `WallHugWindowSmoke` PASS 2/2. |
| Under-window slot acts as low-cover-like over-sill slot, breaks intact glass once, respects PAS/DEF/AGG autonomy, and supports under-sill `BlindFire` | `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:705-760`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:1149-1155`, `Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:158-220`, `Source/CodexTactics/Private/Characters/SquadAutonomySubsystem.cpp:819-874`, `Source/CodexTactics/Private/Characters/OperativeAnimInstance.cpp:542-570`; `WallHugWindowSmoke` PASS 2/2, `WindowCoverFireSmoke` PASS. |
| Adjacent window frame edge acts as a cover corner (standing and crouched where sill allows) and returns cleanly when calm | `Source/CodexTactics/Private/Characters/OperativeCharacterWindowCover.cpp:68-154`, `Source/CodexTactics/Private/Characters/OperativeCharacterWindowCover.cpp:698-718`, `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:204-226`, `Source/CodexTactics/Private/Characters/OperativeCharacterCoverEntry.cpp:418-467`; `WallHugWindowSmoke` PASS 2/2, `CornerSlotSmoke` PASS, `CoverIndoorSmoke` PASS. |
| `WallHugWindowSmoke` clears active training sectors and waits for navmesh rebuild (`-Sector=0` in `test_map.json` + runtime fallback) | `Scripts/test_map.json:70`, `Source/CodexTacticsDev/Private/Debug/WallHugWindowSmokeCommand.cpp:353-396`; verified `WallHugWindowSmoke` PASS 2/2 (`-Sector=0`) and PASS 1/1 (`-ExtraArgs "-Sector=3"` prior to final `pro`-review polish). |
| `LowCover` over-the-top `GetStanceAimBaseDirection()` aligns `UpdateAimOffset` for roof parapets without altering non-window `LowCover` fire origin/gate | `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:448-460`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:735-760`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:1149-1155`; `RoofFightSmoke` PASS 2/2 (`residual 7 deg corner`, `HP 500 -> 481`). |
| `pro` subagent review MEDIUM findings resolved (OverTop `Aim/2` left facing, `LowCover` confirmed window exclusion, `BlindFire` ending active `CornerAim`, `RefreshWindowCover` corner/facing invalidation, `GetCoverFireOrigin` manual peek guard) | `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:451-456`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:705-712`, `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:744-747`, `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp:199-203`, `Source/CodexTactics/Public/Characters/OperativeCharacter.h:3102-3103`, `Source/CodexTactics/Private/Characters/OperativeCharacterWindowCover.cpp:463-470`, `Source/CodexTactics/Private/Characters/OperativeCharacterWindowCover.cpp:720-725`. |
| Local build, Smart unit tests, and all 13 post-final-edit smoke runs pass without failures or retries | `Scripts/build.ps1`: exit 0 (222.12 s); `Scripts/test.ps1 -Smart -Base origin/main -SkipSmokes`: 805/805 passed; `smoke_parallel.ps1`: 6/6 target (`Repeat 2`) + 7/7 mapped window/cover smokes PASS. |

## Self-Review (`pro` subagent) & Gate Summary

- **`pro` subagent review**: 0 HIGH, 4 MEDIUM, 5 LOW (code-quality / smoke-defensive notes). All 4 MEDIUM findings and the `WallHugWindowSmoke` `SelectSector(0)` / null-guard LOW finding were fixed before the final build and verification run.
- **Boundary / map checks**: `Scripts/test.ps1 -CheckMap` -> `Impact map OK: every smoke command is registered and mapped.` No maps, `.uasset`, `Config/`, or designer JSON modified.

## Verification Output (after final edit)

```text
> powershell -ExecutionPolicy Bypass -File Scripts/build.ps1
Result: Succeeded (222.12 seconds, exit 0)

> powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Smart -Base origin/main -SkipSmokes
805 tests, 0 failed. Report: C:\Work\Lanes\CodexTactics-144\Saved\TestReport

> powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WallHugWindowSmoke,FormationFenceCrossSmoke,RoofFightSmoke -Remote off -Jobs 1 -Repeat 2
WallHugWindowSmoke         PASS 2/2 (39,5s / 37,4s)
FormationFenceCrossSmoke   PASS 2/2 (38,6s / 38,7s)
RoofFightSmoke             PASS 2/2 (37,9s / 37,1s)
[smoke_parallel] 6 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail

> powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WindowCoverFireSmoke,FormationWindowSmoke,FormationWallHugSmoke,CoverIndoorSmoke,CornerSlotSmoke,CutawayClickSmoke,InteriorExitSmoke -Remote off -Jobs 1
WindowCoverFireSmoke       PASS (38,3s)
FormationWindowSmoke       PASS (37,4s)
FormationWallHugSmoke      PASS (44,4s)
CoverIndoorSmoke           PASS (39,9s)
CornerSlotSmoke            PASS (44,4s)
CutawayClickSmoke          PASS (41,1s)
InteriorExitSmoke          PASS (35,0s)
[smoke_parallel] 7 run, 0 failed, 0 passed only on the re-run (PASS*), 0 known-fail
```
