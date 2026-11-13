# 034 - Stealth v2 Report

- **Task**: `034-stealth-v2` (spec: `docs/port/codex/inbox/008-stealth-8dir-fast-lean.md`)
- **Base commit**: `da8c7199f37998fda753359ed94cf9340c696e8f`
- **Branch**: `gemini/034-stealth-v2`
- **Worktree**: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemc5`

---

## Overview

Task 034 delivers full Stealth v2 for `CodexTactics`:
1. **Section 4 (Cover in Stealth)**: Single-click and double-click cover confirmations, wall-hug, and run-peek keep stealth and crouched stance; wall hover ghost displays stealth wall pose on any valid wall (`Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp:100`).
2. **Solo-Only Stealth Invariant**: Stealth is strictly disabled in formation mode (button disabled with tooltip, key 7 ignored); gathering into formation via `B` terminates stealth for all joining members (`Source/CodexTactics/Private/Characters/SquadSubsystem.cpp:639`).
3. **Section 5 (Manual Look Anchor)**: `Shift + LMB` on ground sets a world look anchor point; operatives face and strafe relative to the anchor; cleared with `Shift + LMB` on self, `Shift + RMB`, or stealth exit; visual line and marker rendered on HUD (`Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:320`, `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp:1420`).
4. **Section 1 (Automatic Threat Look-Lock)**: Operatives lock look onto nearest visible non-detecting threat within ±100°, hold memory for 2.0 s, switch only if new threat is >30% closer; torso and head twist toward threat (`Source/CodexTactics/Private/Characters/StealthModeRules.cpp:170`, `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:84`).
5. **Section 2 (Fast Stealth Locomotion)**: Double-click move orders activate Fast stealth (`460 cm/s`, native jog pace); scaled detection range (`0.6`) and buildup (`0.75`); footstep noise scaled as jog; moving fast in stealth does not break stealth (`Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:160`, `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp:935`).
6. **Section 3 (Real Corner Lean & Aim-Ghost Suppression)**: Corner peek in stealth plays `Stealth_{Left,Right}Wall_Lean_Start_IP` -> `Lean_IP` -> `Lean_Stop_IP` with weapon lowered; corner hover ghost shows stealth lean pose instead of weapon-aiming pose (`Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp:100`, `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:138`).

---

## Commits on `gemini/034-stealth-v2`

1. `f342c515`: `gemini-034: stealth v2 plan and approved file list`
2. `d4a72d35`: `gemini-034: keep stealth and crouch on cover order with wall hover ghost` (§4)
3. `5d26c121`: `gemini-034: fix 034a HIGH findings (fast perception scales, hover ghost cache/yaw, leave cover pending clear)`
4. `a535a461`: `gemini-034: disallow stealth in formation mode and exit stealth on B gather`
5. `43419d25`: `gemini-034: fix 034b review findings and add Shift+LMB stealth manual look anchor` (§5)
6. `d3c5d312`: `gemini-034: fix 034c-s5 Opus review HIGH and MEDIUM findings`
7. `f9030842`: `gemini-034: implement Section 1 automatic threat look-lock and torso-head look blend` (§1)
8. `b30f295e`: `gemini-034: fix 034d review findings H1-H3 and M1-M6`
9. `157dc381`: `gemini-034: implement Section 2 fast stealth cover switch/wrap and hearing consistency` (§2)
10. `68d887c8`: `gemini-034: fix 034e review findings for section 2`
11. `a00d962a`: `gemini-034: implement Section 3 real corner lean, aim-ghost suppression, and rules tests` (§3)
12. `8da692f9`: `gemini-034: fix 034f-s3 Opus review findings H1-H3 and M4-M8`

---

## Verification Evidence Table

| Claim | Evidence |
|---|---|
| Section 4: Cover order keeps stealth and crouched stance | `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:144`, `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:780`; verified in `StealthModeSmoke` Stage 41-43 |
| Section 4: Hovering wall in stealth displays stealth wall idle ghost | `Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp:100`; verified in `StealthModeSmoke` Stage 41 |
| Formation mode disallows stealth and B gather exits stealth | `Source/CodexTactics/Private/Characters/SquadSubsystem.cpp:639`, `Source/CodexTactics/Private/UI/ActionBarWidget.cpp:240`; verified in `StealthModeSmoke` Stage 1 |
| Section 5: Shift+LMB sets manual look anchor in stealth | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:320`, `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:210`; verified in `StealthModeSmoke` Stage 60 |
| Section 5: Manual anchor beats auto threat lock and faces anchor | `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp:172`, `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:84`; verified in `StealthModeSmoke` Stage 60 |
| Section 5: Shift+RMB / Shift+LMB on self clears manual anchor | `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp:340`; verified in `StealthModeSmoke` Stage 60 |
| Section 1: Automatic threat lock acquires nearest visible non-detecting enemy within ±100° | `Source/CodexTactics/Private/Characters/StealthModeRules.cpp:170`, `Source/CodexTacticsTests/Private/Characters/StealthModeRulesTest.cpp:170`; verified in `StealthModeRules.LookLock` test |
| Section 1: Threat lock retains memory for 2.0 s after sight loss and hysteresis switches at 30% | `Source/CodexTactics/Private/Characters/StealthModeRules.cpp:185`, `Source/CodexTacticsTests/Private/Characters/StealthModeRulesTest.cpp:180`; verified in `StealthModeRules.LookLock` test |
| Section 1: Torso and head twist toward threat target | `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:84`; verified in `StealthModeSmoke` Stage 614 |
| Section 2: Double-click move triggers Fast stealth at 460 cm/s | `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp:160`, `Content/Data/AI/stealth_mode.json:10`; verified in `StealthModeSmoke` Stage 31 |
| Section 2: Fast stealth perception multipliers (0.6 range, 0.75 buildup) applied | `Source/CodexTactics/Private/Characters/EnemyCharacterPerception.cpp:97`, `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:65`; verified in `StealthModeRules.Config` test |
| Section 2: Fast stealth does not break stealth; stationary operatives not heard at standing radius | `Source/CodexTactics/Private/Characters/StealthModeRules.cpp:32`, `Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp:935`; verified in `StealthModeSmoke` Stage 31 |
| Section 3: Corner peek plays Stealth_*Wall_Lean_IP without aim-ghost montage | `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:210`, `Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp:100`; verified in `StealthModeSmoke` Stage 5 |
| Section 3: Wall lean head passes physical wall edge and weapon lowered | `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:837`; verified in `StealthModeSmoke` Stage 5 |
| Section 3: Corner hover ghost displays stealth lean clip instead of fire_idle | `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:815`; verified in `StealthModeSmoke` Stage 43 |
| Unit test suite: 40 automation tests pass with 0 failures | `powershell Scripts/test.ps1 -Filter "CodexTactics.Characters.StealthMode+CodexTactics.Formation.Guard+CodexTactics.Formation.Runtime"` -> 40 tests, 0 failed |
| Female operative and retargeted stealth animation verification | `powershell Scripts/smoke.ps1 -Command "CodexTactics.StealthModeSmoke female" -Extra "-UseFixedTimeStep -FPS=60 -FixedSeed"` -> RESULT: PASS |
| Game-camera frame capture verification | `powershell Scripts/smoke.ps1 -Command "CodexTactics.StealthModeSmoke shots" -Extra "-UseFixedTimeStep -FPS=60 -FixedSeed"` -> RESULT: PASS |
| Full parallel regression suite: 6 smokes pass with 0 failures | `powershell Scripts/smoke_parallel.ps1 -Commands StealthModeSmoke,StealthDetectionSmoke,InjuredLocomotionSmoke,ProneCrawlSmoke,FormationLabSmoke,SoloStartSmoke` -> 6 run, 0 failed, PASS |
| Clean Jev audit | `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini` -> Clean workspace (0 files) |

---

## Opus Review Resolutions

1. **H1 (Aim-ghost suppression in stealth)**: Resolved in `8da692f9` (`Source/CodexTactics/Private/Tactics/CoverGhostActor.cpp:100`). In stealth, corner peek hover ghost displays `Stealth_{Left,Right}Wall_Lean_IP`, completely suppressing the weapon-aiming `cvr_*_fire_idle` pose. Verified non-vacuously in `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:815`.
2. **H2 (Runtime `ShouldBreak` wiring)**: Resolved in `8da692f9` (`Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp:282`). `UpdateStealthMode` and `HandleStealthWeaponFired` now pass `IsStealthFast()`.
3. **H3 (`ShouldBreak` semantics)**: Resolved in `8da692f9` (`Source/CodexTactics/Private/Characters/StealthModeRules.cpp:32`). Clarified that fast stealth movement does not break stealth; normal sprint outside fast stealth breaks it; firing breaks it. Added unit tests for all combinations in `Source/CodexTacticsTests/Private/Characters/StealthModeRulesTest.cpp:41`.
4. **M4 (`Lean_Stop` side consistency)**: Resolved in `8da692f9` (`Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp:138`). Exit/stop clip preserves `StealthLastSide` to avoid side mismatch when leaving the wall.
5. **M5 & M7 (Enum `EStealthWallLeanPhase` and complete enum coverage)**: Resolved in `8da692f9` (`Source/CodexTactics/Public/Characters/StealthModeRules.h:8`, `Source/CodexTactics/Private/Characters/StealthModeRules.cpp:40`). Added strongly-typed enum `EStealthWallLeanPhase { Start, Hold, Stop }` with explicit exhaustive switch handling `Left`, `Right`, and `default` (returns `NAME_None`).
6. **M8 (Non-vacuous smoke check)**: Resolved in `8da692f9` (`Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp:815`). Explicitly asserts `Ghost != nullptr && Ghost->IsShown() && Ghost->IsCornerPeekPose()`, checking that `GhostClip` contains `Wall_Lean` and does NOT contain `fire_idle`.

---

## Fast Stealth Multi-Directional PlayRate Tuning & Verification (#317 / #321)

### 1. Native Clip Speeds (8 Directions)
All 8 clips have a duration of exactly **0.800 s** (48 frames @ 60 FPS):

| Direction | Clip Name | Angle | Source Mannequin | Male (UE4) | Female Soldier |
|---|---|:---:|:---:|:---:|:---:|
| **F** | `Stealth_Fast_F_IP` | 0° | 463.51 cm/s | 503.78 cm/s | 473.87 cm/s |
| **FR** | `Stealth_Fast_FR_IP` | +45° | 456.04 cm/s | 503.75 cm/s | 473.41 cm/s |
| **R** | `Stealth_Fast_R_IP` | +90° | 414.97 cm/s | 465.75 cm/s | 450.49 cm/s |
| **BR** | `Stealth_Fast_BR_IP` | +135° | 436.87 cm/s | 494.33 cm/s | 458.26 cm/s |
| **B** | `Stealth_Fast_B_IP` | 180° | 462.16 cm/s | 502.04 cm/s | 469.72 cm/s |
| **BL** | `Stealth_Fast_BL_IP` | -135° | 443.91 cm/s | 497.36 cm/s | 470.61 cm/s |
| **L** | `Stealth_Fast_L_IP` | -90° | 419.23 cm/s | 490.07 cm/s | 462.99 cm/s |
| **FL** | `Stealth_Fast_FL_IP` | -45° | 456.58 cm/s | 503.15 cm/s | 472.37 cm/s |

### 2. Runtime PlayRate Verification Table (Speed = 460.0 cm/s)
PlayRate is computed directly as $\text{PlayRate} = \frac{\text{MoveSpeed}}{\text{BlendedNominalSpeed}}$ for the current angle:

| Move Angle | Male Blended Nominal | Male PlayRate | Female Blended Nominal | Female PlayRate | Status |
|:---:|:---:|:---:|:---:|:---:|:---:|
| **0°** (Forward) | 503.78 cm/s | **0.913x** | 473.87 cm/s | **0.971x** | PASS |
| **20°** (Fwd-Right Blend) | 503.77 cm/s | **0.913x** | 473.67 cm/s | **0.971x** | PASS |
| **45°** (Diagonal FR) | 503.75 cm/s | **0.913x** | 473.41 cm/s | **0.972x** | PASS |
| **90°** (Lateral R Strafe)| 465.75 cm/s | **0.988x** | 450.49 cm/s | **1.021x** | PASS |

### 3. Root Cause Resolution
1. **Uncapped PhaseRate**: In `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp`, `LeadLength` now uses frequency interpolation (`InvLen = Lerp(1/WalkLen, 1/JogLen, Fast)`), so during Fast stealth `LeadLength = 0.8s` (instead of walk 1.33s), raising the upper clamp limit to 2.0 cycles/s and eliminating artificial dragging.
2. **Directional Speed Maps**: Added explicit `"walk_speeds"` and `"jog_speeds"` maps to `"stealth"` in `Content/Data/Weapons/weapon_anim_sets.json` so every angle has authored nominal speeds.
3. **Smooth Fast Stride Ramp & Floor (Opus H1)**: Replaced discontinuous step function with smooth mapped floor `FastFloor = MappedRange(0.35 * FastCfg, 0.6 * FastCfg, 0.0, 1.0)` and `FInterpTo(10.f)`. During start/stop acceleration and deceleration, the character smoothly blends walk and jog without pose-popping or 0.05x jog strides. Once above mid-speed (0.6x FastConfigSpeed), it locks to pure jog, preventing slow-walk poisoning during directional steering and speed fluctuations.
4. **Smoke Verification & Regression Coverage (Opus H2, M5, M6)**: Added automated 8-directional angle checks (0°, 20°, 45°, 90°, 135°, 180°, -45°, -90°) with 10-frame unwrapped phase integration, continuity check across acceleration ramp, strict regression guard (`PlayRate > 0.97` at +90° R strafe), and sprint break from full 460 cm/s fast stealth in `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp`. Verified: `StealthModeSmoke` **PASS**.

---

## Opus Review Resolution Summary (#323)
- **H1 (Fast blend discontinuity at start/stop/toggles)**: Fixed via smooth mapped floor `FastFloor = MappedRange(0.35 * FastCfg, 0.6 * FastCfg, 0.0, 1.0)` smoothed by `StealthFastBlend = FMath::FInterpTo(..., 10.f)` and reset on stealth entry/stop.
- **H2 (Smoke check strictness & 0.96 ceiling detection)**: Fixed via multi-frame unwrapped delta phase integration over 10 frames (0.5s), strict check `PlayRate > 0.97f` at +90° (Male R strafe = 0.988x), and 8-directional testing.
- **M1 & M2 (LeadLength null safety & harmonic frequency cap)**: Fixed via safe null fallbacks and frequency interpolation `InvLen = Lerp(1/WalkLen, 1/JogLen, Fast)` with `Clamp(1/InvLen, 0.1f, 10.f)`.
- **M5 (Asymmetric negative and backward angles)**: Verified across all 8 cardinal/intercardinal angles including -45°, -90°, 135°, 180°.
- **M6 (Sprint cancellation from active fast-stealth)**: Added test asserting immediate stealth drop from 460 cm/s fast locomotion.
- **L1 (ASCII in source)**: Cleaned non-ASCII characters in smoke command.

---

## Bug #324 Resolution Summary (`gemini-034-lookdir`)

### 1. Problem Description (Designer video `stelthrun_bug_0.avi`)
1. Player walks operative Right -> enters stealth -> double-clicks another point (Up/Left).
2. Operative initially ran sideways (боком), slowly turned during the run, and only then faced forward.
3. Upon reaching the destination point and stopping, the operative turned back to the right.
4. Next order: behavior repeated. No enemies, no manual look anchor.

### 2. Root Cause Analysis
1. **Formation Sector Hijack**: In `OperativeCharacterAim.cpp:245`, `bFacingFormationSector` lacked `!IsInStealthMode()`. In stealth mode `bSprinting` is false (`!bSprinting` is true), so whenever `FormationLookYaw` was set (e.g. flank sector or leader sector), `bFacingFormationSector` evaluated to `true`, actively overriding velocity facing during movement and locking the character's facing to the formation look sector (Right).
2. **Parked Follower Snap-back**: In `OperativeCharacterAim.cpp:260`, `bHasIdleFacing` lacked `!IsInStealthMode()`. When stopping at the destination point, velocity dropped below 10 cm/s and `bHasIdleFacing` snapped the body back to `IdleFacingYaw` (Right).
3. **Squad Subsystem Sector Assignments in Stealth**: `USquadSubsystem::ApplyFormationFrame()` in `SquadSubsystemFormation.cpp` lacked `!Operative->IsInStealthMode()`, continuing to assign formation sector looks to stealth operatives every frame. Furthermore, `SquadSubsystem.cpp:1860` set `IdleFacingYaw` on parked followers in stealth.
4. **Turn Rate & Velocity Smoothing Lag**: In crouched stealth, `TurnRateCrouching = 430 deg/s` (7.5 rad/s) combined with `FacingRules::SmoothVelocity` filter rate (10 1/s) caused slow alignment (>0.6-1.0s) during high-speed (460 cm/s) fast stealth.

### 3. Implementation
1. `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp`:
   - Gated `bFacingFormationSector` on `!IsInStealthMode()`.
   - Gated `bHasIdleFacing` stop turn on `!IsInStealthMode()`.
   - Isolated `MoveTurnSpeed` for movement-direction alignment (`SmoothedVelocity`) during stealth without look target / fast stealth, using `FMath::Max(GetTurnRate(Standing), 1031.3f) / 57.2958f` (18.0 rad/s = 1031 deg/s), while keeping deliberate aiming / manual look anchors (`bStealthLook`) on controlled rate (Opus M1).
   - Increased `SmoothedVelocity` smoothing rate to 20.0 1/s for fast stealth.
2. `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp`:
   - In `RequestMove()`, cleared `FormationLookYaw` and reset `SmoothedVelocity` if direction change > 60° (Opus M4).
3. `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp`:
   - `SetStealthMode(true)` clears stale `IdleFacing`, `FormationLookYaw`, and `SmoothedVelocity`.
4. `Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp` & `SquadSubsystem.cpp`:
   - Prevented squad subsystem from assigning formation look sectors or parked follower idle facing to operatives in stealth.
5. `Source/CodexTactics/Public/Characters/FacingRules.h`:
   - Added optional `Rate = 10.f` parameter to `SmoothVelocity` with updated docstring (Opus L4).
6. `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp`:
   - Stages 620-626: Added comprehensive verification reproducing designer flow:
     - Enter stealth facing right (+90°), inject stale idle facing and formation look.
     - Move Up (0°): body reaches movement direction in **0.200s** (<= 0.30s budget).
     - Stop at destination: yaw remains stable across 1.0s (max drift = 0.23°), does NOT snap back to right (delta = 87.7°).
     - Move Left (-90°): body reaches movement direction in **0.200s** (<= 0.30s).
     - Stop at destination: yaw remains stable across 1.0s (max drift = 0.01°).
     - 180° Reversal Move Right (+90°): body reaches reversed direction in **0.200s** (<= 0.30s).
     - Stop at destination: yaw remains stable across 1.0s (max drift = 0.07°).
   - Updated stage 2 to use explicit `SetStealthManualLookTarget` instead of hijacking formation look (Opus L6).

### 4. Opus Review Resolution
- Opus 5.5 ran on the diff: **Verdict: No HIGH findings**.
- Implemented Opus recommendations M1 (isolated `MoveTurnSpeed`), M3 (clean exit path), M4 (conditional `SmoothedVelocity` reset on repath), M5 (injected formation look during active test to prove consumer-side guard), M7 (added 180° reversal, movement-start timing, 1.0s per-frame drift tracking, explicit arrival assertions), L4, L6.

### 5. Verification
- 7/7 `CodexTactics.Characters.StealthMode` unit tests **PASS**.
- `CodexTactics.StealthModeSmoke` **PASS** (100% checks pass including 034-lookdir 90°/180° <=0.200s and 1.0s stability).
- `CodexTactics.ShotgunCoverSmoke` **PASS** (10/10 stages pass).
- `CodexTactics.StealthDetectionSmoke` **PASS**.



