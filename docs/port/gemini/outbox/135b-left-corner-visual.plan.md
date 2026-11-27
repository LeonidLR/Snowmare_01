# 135b Plan — Corner Body Edge Alignment & Shift-Look Aim / Step-Out

## Root Causes Found
1. **Part 1 (Idle/Return & Riffle Set Corner Body Distance)**:
   - Capsule stand-off is fixed at `34 cm` standing / `56 cm` crouched (`CoverStandOffStandPack*Cm`, `CoverStandOffCrouchPack*Cm`).
   - `UpdateCoverEntryBlend` only applied fixed M4 fire-idle offsets (`CornerLeanExtraPackLCm = -26`, `CornerLeanExtraPackRCm = +11`) while `IsPeekingRoundCorner()` was active.
   - Outside the M4 fire-idle pose — specifically in indoor `'riffle'` clips (baked in place) AND in return-to-idle cover (`ReturnNoTargets`) / crouch — the skeleton stays `34..100 cm` inside the exposed edge (and for `'riffle'` right-side peeks `-26 cm` pulls it *even further* away from the edge).
   - `CornerSlotSmoke` only measured active `'m4'` peeks, not idle return (`ReturnNoTargets`) or `'riffle'`.

2. **Part 2 (Shift+Click Look Ignoring Pose / Beyond AO Range)**:
   - In `CornerHold` (`!bManualCornerPeek`) after `ReturnNoTargets`, `UpdateCoverFacing()` flips `CoverFacing` when Shift+clicking across `WallNormal`, then resets `bAtCoverCorner = false` (only `bManualCornerPeek` preserved it), which immediately drops `WantsCornerLookPose()` to `false` and zeroes `AimYawTarget`.
   - Even when `WantsCornerLookPose()` stays true after `ReturnNoTargets`, `UpdateCoverLayer` (`bWantFirePose`) stays false and plays wall-hug `_Idle` instead of the `_Aim_Idle` / fire-idle pose that `AO_CoverIndoor_*` and corner AO aim offsets are authored over, so the rifle and `spine_03` do not rotate cleanly to the look yaw.
   - When the Shift+click look yaw exceeds the corner AO's effective reach (e.g. `~90 deg` right from a left corner / `WallNormal` straight out or behind), the wall-back actor rotation lock (`bInCover`) prevents the body from turning unless the operative steps out of the wall hug.

## Changes
1. **Closed-loop corner edge alignment (`OperativeCharacter.h`, `OperativeCharacterCoverEntry.cpp`, `OperativeCharacterCover.cpp`)**:
   - Add tracked `CoverCornerAlignAppliedLocal` (alongside `CoverWallPushAppliedLocal`, included in `EnforceMeshBaseOffset` and cleared on `LeaveCover`/`ResetBodyOffsets`, keeping `GetCoverEntryMeshOffset()` zero in non-peeking wall hugs so wrap/switch/group smokes stay intact).
   - In `UpdateCoverEntryBlend`, measure un-aligned outer clavicle (`clavicle_l` / `clavicle_r`) to the exposed corner edge (`EdgeSide` prefers `CoverFacing` when exposed, else the active corner edge so cross-normal looks don't jump away from the corner).
   - When settled in an active corner fire/peek pose (`bLeansOut && (IsCoverInFirePose() || IsCoverEntryPeekStance())`), target outer shoulder at `-21 cm` (past edge; satisfies `abs <= 25 cm`, `ShoulderBeyond >= 0`, `HeadBeyond >= 20`, symmetric L/R, `'m4'` and `'riffle'`, stand & crouch).
   - When settled at a corner in idle/return pose (`!bLeansOut || !IsCoverInFirePose()`), fade `CornerLeanExtraAlpha` to `0` and ease `CoverCornerAlignAppliedLocal` so outer shoulder rests at `+14 cm` inward from the edge (`<= 25 cm`, symmetric L/R, stand & crouch, `'m4'` & `'riffle'`, zero wall clipping).

2. **Corner Shift-Look Pose & Out-of-Range Step-Out (`CoverLookAoRules.h/.cpp`, `OperativeCharacterCover.cpp`, `OperativeCharacterCornerAim.cpp`, `OperativeCharacterStealth.cpp`, `OperativeAnimInstance.h/.cpp/.CoverSet.cpp`)**:
   - Preserve `bAtCoverCorner` during `WantsCornerLookPose()` for both `bManualCornerPeek` and `CornerHold`.
   - Enable `bWantFirePose` when `bCornerLookPoseApplied && Operative.IsAtPeekableCorner() && !bShimmying` so `'riffle'` and `'m4'` enter the aim pose matching their AO.
   - Add `CoverLookAoRules::IsWithinCornerAoReach` (clamping `AO` yaw reach by active BlendSpace / `AimYawClampDegrees`): if a manual corner Shift+click look exceeds the corner AO's reach (`> MaxCornerAoReachDeg`, e.g. `~45-55 deg` off `CornerAimDirection`, such as `90 deg` right at a left corner when outside exposed corner reach), step out of the wall hug (`LeaveCover` while keeping `ManualLookPoint`) so `UpdateCombatFacing` turns the actor/body & rifle directly to `LookYaw`.

3. **Tests & Smokes (`CoverLookAoRulesTest.cpp`, `CornerSlotSmokeCommand.cpp`, `CoverIndoorSmokeCommand.cpp`)**:
   - Unit test `CoverLookAoRules::IsWithinCornerAoReach` in `CodexTactics.Tactics.Cover.LookAo`.
   - Extend `CornerSlotSmoke` (`CoverAnimSetOverrideForTesting`) to log + assert outer shoulder `<= 25 cm` and penetration `<= 2 cm` across **peek aim idle**, **idle return (`ReturnNoTargets`)**, **standing & crouched**, **Left & Right**, and **both `'m4'` & `'riffle'`**.
   - Extend `CornerSlotSmoke` / `CoverIndoorSmoke` to assert real world yaw of `spine_03` and weapon barrel vs Shift+click look yaw (`<= 15 deg`) right & left, within & beyond AO range, in **`corner hold` after `ReturnNoTargets`** and active peek, across **both `'m4'` & `'riffle'`**.
