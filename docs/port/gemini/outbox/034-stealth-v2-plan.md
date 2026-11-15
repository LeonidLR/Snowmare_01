# Plan: Stealth v2 (`gemini-034` / `008-stealth-8dir-fast-lean`)

Branch: `gemini/034-stealth-v2` (from `interim/2026-11-12` `da8c7199`)

## Execution Order

1. **Commit 1 (this file):** Architecture plan, approved file allowlist, and verification checklist.
2. **Commit 2 — Section 4 (Bug: cover order exits stealth):**
   - Preserve stealth and crouched stance on single-click (`Slow`) and double-click (`Fast`) cover confirm, group cover, and wall-hug / run-peek cover orders (`CodexTacticsPlayerControllerCover.cpp`, `CodexTacticsPlayerControllerRunPeek.cpp`, `OperativeCharacterMovement.cpp`, `OperativeCharacterStealth.cpp`, `OperativeCharacterCover.cpp`, `OperativeCharacterCoverEntry.cpp`).
   - Never stand up or set combat `bSprinting` when issuing a cover/move order in stealth; enter `Stealth_{Left,Right}Wall_Idle_IP` via `Stealth_*Wall_{Slow,Fast}_Start_IP` / `Stop_IP`.
   - Show the hover `ACoverGhostActor` on any valid wall in stealth (`Stealth_LeftWall_Idle_IP` / `Stealth_RightWall_Idle_IP`), and stealth `Lean` preview at corners (`CoverGhostActor.{h,cpp}`, `CodexTacticsPlayerController.cpp`, `CodexTacticsPlayerControllerCover.cpp`).
   - Extend `StealthModeSmoke` & unit tests, verify build + smoke.
3. **Commit 3 — Section 5 (`Shift + LMB` manual look anchor):**
   - Keep `Shift + RMB` as Hold Line / Suppress; bind `Shift + LMB` in stealth to set a manual world look anchor, and `Shift + LMB` on self (selected operative) or leaving stealth to clear it (`CodexTacticsPlayerControllerOrders.cpp`, `OperativeCharacter.h`, `OperativeCharacterStealth.cpp`, `PlayerCommands.{h,cpp}`).
   - Stealth facing priority in `UpdateCombatFacing` (`OperativeCharacterAim.cpp` hook -> `OperativeCharacterStealth.cpp`): manual anchor > automatic threat lock > movement direction.
   - Draw thin look line and eye/target marker for selected stealthed operative(s) with an active anchor (`CodexTacticsHUD.cpp`).
   - Unit test + smoke (`yaw <= 10 deg`, side-step clip, enemy at anchor stays visible).
4. **Commit 5 — Section 1 (8-dir stealth + auto threat lock):**
   - Select nearest visible non-detecting threat within `look_lock_fov_deg` (`100°` half-angle / `±100°`), retain for `look_lock_keep_s` (`2.0s`) after sight loss, hysteresis switch only when closer than `70%` (`look_lock_switch_ratio`), move 8-directionally relative to locked look with `Start` / `Stop` / `Turn_90` / `Turn_180` clips (`StealthModeRules.{h,cpp}`, `Content/Data/AI/stealth_mode.json`, `OperativeAnimInstanceStealth.cpp`, `OperativeCharacterStealth.cpp`, `OperativeCharacterAim.cpp`).
5. **Commit 6 — Section 2 (Fast stealth):**
   - Double-click move/cover in stealth enters Fast stealth (`stealth.fast_speed = 300.0`), stays crouched, never breaks stealth (fire still breaks stealth).
   - Apply Fast stealth perception scaling (`fast_detection_range_mult = 0.6`, `fast_buildup_mult = 0.75`) and jog footstep noise (`StealthModeRules.{h,cpp}`, `EnemyCharacterPerception.cpp`, `MarksmanEnemyCharacter.cpp`, `PmcSquadSubsystem.cpp`).
   - `OperativeAnimInstanceStealth.cpp` uses `Stealth_Fast_*_IP` / `Stealth_*Wall_Fast_IP` and `UI/ActionBarWidget.cpp` reflects Fast state if shown.
6. **Commit 7 — Section 3 (Real corner lean in stealth):**
   - Corner lean in stealth plays `Stealth_{Left,Right}Wall_Lean_Start_IP` -> `Lean_IP` -> `Lean_Stop_IP`, keeps weapon lowered (no aim-ghost `Cover*FireIdle` pose in stealth; non-stealth aim-ghost unchanged), verifies head bone past wall edge (`OperativeCharacterCover.cpp`, `OperativeCharacterCornerAim.cpp`, `OperativeAnimInstanceStealth.cpp`, `CoverGhostActor.{h,cpp}`).
7. **Final Verification & Report:**
   - `Scripts/build.ps1`
   - Unit tests: `CodexTactics.Characters.StealthMode`, `CodexTactics.Stealth`, `CodexTactics.Animation`
   - Smokes: `StealthModeSmoke` (male + female, + shots to `MAIN/Saved/Orchestrator/stealth/v2/`), `StealthDetectionSmoke`, `InjuredLocomotionSmoke`, `ProneCrawlSmoke`, `FormationLabSmoke`, `SoloStartSmoke`
   - `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`
   - Final report in `docs/port/gemini/outbox/034-stealth-v2.report.md` (+ copy to `MAIN`).

## Approved File List (`034-stealth-v2.md` + `#292`)

### Initial Allowlist
- `Source/CodexTactics/Private/Characters/OperativeCharacterStealth.cpp`
- `Source/CodexTactics/Public/Characters/OperativeAnimInstance.h` (stealth layer only)
- `Source/CodexTactics/Private/Characters/OperativeAnimInstanceStealth.cpp` (and stealth-only branches in `OperativeAnimInstance.cpp`)
- `Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp` (stealth branches only)
- `Source/CodexTactics/Private/Characters/OperativeCharacterStance.cpp` (stealth branches only)
- `Source/CodexTactics/Public/Core/CodexTacticsPlayerController.h` (stealth input/orders only)
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerController.cpp` (stealth hover/input only)
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerOrders.cpp` (stealth orders/anchor only)
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerCover.cpp` (stealth cover/hover only)
- `Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerRunPeek.cpp` (stealth run-peek orders only)
- `Source/CodexTactics/Public/Core/PlayerCommands.h`, `Private/Core/PlayerCommands.cpp` (stealth command metadata only)
- `Source/CodexTactics/Private/UI/ActionBarWidget.cpp` (stealth only)
- `Content/Data/Weapons/weapon_anim_sets.json` (stealth block if needed)
- `Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp`
- `Source/CodexTacticsTests/Private/Characters/StealthModeRulesTest.cpp`

### Approved Additions (`#292`)
- `Content/Data/AI/stealth_mode.json`
- `Source/CodexTactics/Public/Characters/StealthModeRules.h`
- `Source/CodexTactics/Private/Characters/StealthModeRules.cpp`
- `Source/CodexTactics/Public/Characters/OperativeCharacter.h` (stealth v2 fields & methods)
- `Source/CodexTactics/Private/Characters/OperativeCharacterAim.cpp` (stealth look-lock/anchor hook + lowered weapon in stealth)
- `Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp` (preserve stealth on cover + stealth lean branch)
- `Source/CodexTactics/Private/Characters/OperativeCharacterCoverEntry.cpp` (preserve stealth on cover switch/entry)
- `Source/CodexTactics/Private/Characters/OperativeCharacterCornerAim.cpp` (stealth lean without aim-ghost fire pose)
- `Source/CodexTactics/Public/Tactics/CoverGhostActor.h`, `Private/Tactics/CoverGhostActor.cpp` (stealth wall idle hover & lean ghost poses)
- `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp` (Fast stealth perception multipliers via `StealthModeRules`)
- `Source/CodexTactics/Private/Characters/EnemyCharacterPerception.cpp` (Fast stealth detection/noise multipliers via `StealthModeRules`)
- `Source/CodexTactics/Private/Characters/MarksmanEnemyCharacter.cpp` (Fast stealth detection multiplier via `StealthModeRules`)
- `Source/CodexTactics/Private/UI/CodexTacticsHUD.cpp` (Shift+LMB manual look anchor thin line & eye marker only)

## Strict Invariants
- Non-stealth behavior remains byte-for-byte identical (guarded behind `IsInStealthMode()`).
- Enemy perception edits touch only `StealthModeRules` Fast/Slow scaling and noise classification.
- No edits to `004` (`50de2e98`), `MAIN/Source/**`, `Config/**`, `Content/Maps/**`, or `.uasset` files.
