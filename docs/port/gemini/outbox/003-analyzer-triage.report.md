# Triage Report: 74 High Analyzer Warnings (MSVC /analyze)

**Task:** 003 - Triage of the 74 high analyzer warnings (READ-ONLY review)  
**Baseline source:** `docs/port/audit/static_baseline.json` (Severity: High, 70 C6011 + 4 C28182 null dereferences)  
**Review Type:** READ-ONLY static flow analysis across `Source/CodexTactics/`, `Source/CodexTacticsDev/`, and `Source/CodexTacticsTests/`.  
**Date:** 2026-11-10  
**Author:** Gemini (Codex Pair Programmer)

---

## 1. Summary Counts

| Category | Warnings Analyzed | REAL | FALSE POSITIVE | UNSURE |
| :--- | :---: | :---: | :---: | :---: |
| **Game Runtime** (`Source/CodexTactics/`) | 20 (#01 - #20) | 1 | 19 | 0 |
| **Dev Smoke Commands** (`Source/CodexTacticsDev/`) | 23 (#21 - #43) | 15 | 8 | 0 |
| **Automation Tests** (`Source/CodexTacticsTests/`) | 31 (#44 - #74) | 1 | 30 | 0 |
| **Total** | **74** | **17** | **57** | **0** |

### Key Architectural Takeaways:
1. **Runtime Game Code is remarkably robust (95% False Positives):** Only 1 warning out of 20 in `Source/CodexTactics/` represents a real defect (#13 in `DropLinkSubsystem.cpp`). The other 19 warnings are false positives caused by MSVC `/analyze` being unable to trace cross-translation-unit helper functions (`ShouldShowExitArrows`, `DecideGroup`, `DecideLeaderAfterFall`), dependent boolean flags, or collection membership guarantees.
2. **Smoke Commands have high real defect density (65% Real):** In debug smoke commands, developers frequently assume subsystems, controllers, anim instances, and squad members are non-null throughout multi-frame asynchronous tick states, or perform null-checks in one stage while omitting them in adjacent stages.
3. **Test Code False Positives dominate (97% False Positives):** In `CodexTacticsTests`, 30 of 31 warnings stem from MSVC analyzer treating Unreal's `TestTrue(const TCHAR*, bool)` and `TestNotNull(const TCHAR*, const void*)` as opaque functions rather than contract guards that return their boolean argument. Exactly 1 test warning (#51 in `WeaponAnimLayerTest.cpp`) is a real fatal bug where execution continues unconditionally past a test assertion and dereferences a null pointer.

---

## 2. Ranked List of REAL Warnings (Crash Risk)

Ranked by likelihood of crashing normal gameplay, level transitions, or automated test pipelines:

### Top 10 High Priority

1. **#13 — `DropLinkSubsystem.cpp:147` [Runtime Code]**
   - **Severity:** Critical (Game Client/Server Crash).
   - **Failure State:** In `UDropLinkSubsystem::RegenerateLinks`, `World` is retrieved via `GetWorld()` and guarded at line 139 with `if (World)`, but at lines 147 and 151, `World->GetTimerManager().SetTimer(...)` is called unconditionally. If `UDropLinkSubsystem` regenerates links while the level is unloading or world is tearing down, `World` is null, resulting in an immediate crash during normal play.
   - **Fix Idea:** Add `if (!World) return;` before timer registration or wrap timer calls inside `if (World)`.

2. **#51 — `WeaponAnimLayerTest.cpp:339` [Test Pipeline]**
   - **Severity:** High (Test Suite Process Crash).
   - **Failure State:** Line 305 tests whether `retargeted` JSON block exists with `TestTrue(...)`, but does NOT return on failure. Execution proceeds unconditionally to line 339 where `(*Retargeted)->Values` dereferences null. If `weapon_anim_sets.json` is missing the field or malformed, the test crashes the entire automation runner rather than cleanly failing.
   - **Fix Idea:** Add `if (!Retargeted || !Retargeted->IsValid()) return false;` before line 339.

3. **#25 — `CoverSwitchSmokeCommand.cpp:344` [Dev / Smoke]**
   - **Severity:** High (Smoke Command Crash).
   - **Failure State:** `Anim = AnimOf(Op)` is treated as nullable with defensive ternaries across L333, L341, and L346 (`Anim ? ... : ...`), and guarded with `Anim && ...` at L347. However, on line 344, `Anim->GetCoverClipLog()` is called unconditionally. If the operative has no anim instance during the dash, the smoke test crashes.
   - **Fix Idea:** Add `Anim &&` before `Anim->GetCoverClipLog().Contains(Played)` at line 344.

4. **#42 — `TurnBasedSmokeCommand.cpp:163` [Dev / Smoke]**
   - **Severity:** High (Smoke Command Crash).
   - **Failure State:** `Active = TurnBased->GetUnitState(...)` is null-checked on line 157 via `Check(State, Active && Active->AP == 8, ...)`. However, `Check()` does NOT abort execution on failure (it only logs and increments failure count). At line 163, `Active->AP == 7` dereferences `Active` without a null check. If unit registration fails, line 163 crashes immediately.
   - **Fix Idea:** Change condition to `Active && Active->AP == 7` or return early if `!Active`.

5. **#43 — `TurnRulesSmokeCommand.cpp:100` [Dev / Smoke]**
   - **Severity:** High (Smoke Command Crash).
   - **Failure State:** `UnitState = TurnBased->GetUnitState(Unit)` is treated as nullable at line 94 (`State.APBefore = UnitState ? UnitState->AP : 0`). On lines 100 and 102, `UnitState->GridPos` is dereferenced unconditionally inside the direction loop. If the active unit is not in `TurnBased`, the loop immediately dereferences null.
   - **Fix Idea:** Add `if (!UnitState) { Check(State, false, TEXT("missing unit state")); return Finish(State, false); }` after line 94.

6. **#34 — `GroupCoverPrioritySmokeCommand.cpp:210` [Dev / Smoke]**
   - **Severity:** High (Smoke Command Crash).
   - **Failure State:** In `CheckSettled`, line 209 checks `Three[I] && Three[I]->GetCoverSlot()...`, but line 210 immediately calls `Along(State, Three[I]->GetActorLocation())` and line 213 calls `Dist2D(Three[I]->GetActorLocation(), Three[J]->GetActorLocation())` without null checks. If any squad member is missing or destroyed, line 210 crashes.
   - **Fix Idea:** Check `if (!Three[0] || !Three[1] || !Three[2]) { Check(State, false, TEXT("missing operative")); return; }` at the beginning of `CheckSettled`.

7. **#36 — `HitFlashOverlaySmokeCommand.cpp:72` [Dev / Smoke]**
   - **Severity:** Medium-High (Asynchronous Teardown Crash).
   - **Failure State:** In a delayed timer lambda, `UWorld* W2 = WeakWorld.Get()`. Line 67 checks `W2 ? ... : nullptr` for the feedback subsystem, but line 72 immediately calls `W2->GetTimerManager().SetTimer(...)` unconditionally. If the world is destroyed or PIE stopped during the first timer delay, line 72 dereferences null.
   - **Fix Idea:** Add `if (!W2) return;` right after `UWorld* W2 = WeakWorld.Get();`.

8. **#40 — `SaveRoundTripSmokeCommand.cpp:812` [Dev / Smoke]**
   - **Severity:** Medium (Smoke Command Crash).
   - **Failure State:** `Medic = FindRole(Squad, EOperativeRole::MedicSapper)` is checked with `Medic && ...` at line 672. After in-place reload via `Saves->LoadFromSlotWithMessage`, line 812 calls `Medic->IsSniperWeaponEquipped()` without checking if `Medic` is null (e.g. if the squad had no medic role or role actor wasn't restored).
   - **Fix Idea:** Add `Medic &&` at the beginning of the check condition at line 812.

9. **#21 — `AimOffsetSmokeCommand.cpp:198` [Dev / Smoke]**
   - **Severity:** Medium (Smoke Command Crash).
   - **Failure State:** In `case 4:`, `if (State.StageTime < Op->AimTargetHoldSeconds + 1.5f || !Anim)` evaluates `Op->AimTargetHoldSeconds` before checking `!Anim`. If `State.Op` was killed or GC'd, `Op = State.Op.Get()` is null, causing a crash on the property access.
   - **Fix Idea:** Reorder check to `if (!Op || !Anim || State.StageTime < Op->AimTargetHoldSeconds + 1.5f)`.

10. **#26 — `DefendSmokeCommand.cpp:155` [Dev / Smoke]**
    - **Severity:** Medium (Smoke Command Crash).
    - **Failure State:** `Defender = State.Defender.Get()` is guarded at line 108 (`if (Defender && ...)`), but in `case 1:` at line 155, `Defender->GetActorLocation()` is called unconditionally. If the leader died or was destroyed during the 8-second stage wait, line 155 crashes.
    - **Fix Idea:** Guard stage 1 with `if (!Defender) { Check(State, false, TEXT("missing defender")); return Finish(State, false); }`.

---

### Remaining REAL Warnings (11 to 17)

11. **#22 — `CornerEntrySmokeCommand.cpp:357` [Dev / Smoke]**
    - **Failure State:** Line 328 checks `if (Squad)`, but line 357 calls `Op = Squad->GetLeader();` unconditionally in `State.Stage == 0`.
    - **Fix Idea:** Check `if (Squad) { Op = Squad->GetLeader(); }`.

12. **#23 — `CornerEntrySmokeCommand.cpp:415` [Dev / Smoke]**
    - **Failure State:** In `case 0:` of path execution, line 415 accesses `Op->bInCover` without verifying `Op = State.Op.Get()`. If leader died, crashes.
    - **Fix Idea:** Add `if (!Op) return Finish(State);`.

13. **#24 — `CoverReverseSmokeCommand.cpp:311` [Dev / Smoke]**
    - **Failure State:** In `case 1:`, lines 302, 308, 311, and 312 call methods on `Op` without verifying that `Op = State.Op.Get()` is valid. If operative GC'd, crashes.
    - **Fix Idea:** Add `if (!Op) return Finish(State, false);` at the beginning of `case 1:`.

14. **#28 — `DropSoftlockSmokeCommand.cpp:607` [Dev / Smoke]**
    - **Failure State:** In `case EPhase::Click1:`, line 608 calls `*Leader->DisplayName.ToString()`. If `Leader = Squad ? Squad->GetLeader() : nullptr` returned null, line 608 crashes.
    - **Fix Idea:** Guard `Click1` with `if (!Leader) return Finish(State, false);`.

15. **#27 — `DropSoftlockSmokeCommand.cpp:579` [Dev / Smoke]**
    - **Failure State:** In `case EPhase::Setup:`, line 579 calls `Squad->IsSoloMode()` without checking if `Squad` is non-null.
    - **Fix Idea:** Wrap with `if (Squad && Squad->IsSoloMode())`.

16. **#38 — `LevelGenLaddersSmokeCommand.cpp:650` [Dev / Smoke]**
    - **Failure State:** In `case EPhase::Order:`, lines 649 and 650 call `Leader->OrderMoveTo` and `Leader->GetActorLocation()` without null check, unlike `Setup` which explicitly checked it. If the leader was destroyed during setup, line 649 crashes.
    - **Fix Idea:** Add `if (!Leader) return Finish(State, false);` at the start of `case EPhase::Order:`.

17. **#32 — `FallDropSmokeCommand.cpp:747` [Dev / Smoke]**
    - **Failure State:** In `Run()`, `Leader = Squad.GetLeader()` is called at line 725. Line 747 calls `Leader->GetActorLocation()` without checking if `Leader` is null. If all operatives died during the drop test, `Leader` is null and crashes.
    - **Fix Idea:** Add `if (!Leader) return;` at the top of `Run()`.

---

## 3. Complete Triage Table (All 74 Warnings)

| ID | File:Line | Code | Verdict | Concrete Null State (REAL) or Reason (FALSE POSITIVE) | Fix Idea (REAL only) |
| :---: | :--- | :---: | :---: | :--- | :--- |
| **#01** | `PmcSquadSubsystem.cpp:618` | C6011 | **FALSE POSITIVE** | Guarded by `bRulesActive`; if `Sight` is null, `bRulesActive` is false, `bEye` becomes true, and line 617 breaks out of the loop before line 618 can be reached. | — |
| **#02** | `InteriorExitSubsystem.cpp:125` | C6011 | **FALSE POSITIVE** | Lines 98-104 call `ShouldShowExitArrows(*Flow, *Exit, *Shown, State)` which early-returns false if `Shown == nullptr`. Line 125 is only reached when `Shown != nullptr`. | — |
| **#03** | `OperativeCharacterStance.cpp:123` | C6011 | **FALSE POSITIVE** | The switch `default:` is only reached for `DiveFromRun` / `DiveFromWalk`. If `Clip` or `Anim` is null, line 96 sets `Entry = EStanceTransitionEntry::StopThenProne`, which is handled in an earlier case and never falls through to `default:`. | — |
| **#04** | `OperativeCharacterStance.cpp:126` | C6011 | **FALSE POSITIVE** | Same branch logic as #03; `default:` cannot execute if `Anim == nullptr`. | — |
| **#05** | `OperativeCharacterSuppression.cpp:199` | C6011 | **FALSE POSITIVE** | C++ short-circuit evaluation: `bAlive && !Target->IsHidden()` where `bAlive` is defined as `IsValid(Target) && !Target->IsDying()`. If `Target` is null, `bAlive` is false and `Target->IsHidden()` is never evaluated. | — |
| **#06** | `SquadAutonomySubsystem.cpp:508` | C6011 | **FALSE POSITIVE** | `Enemies` array is populated exclusively with valid enemies that were already dereferenced at line 150. MSVC flags the struct member default initializer `Enemy = nullptr`. | — |
| **#07** | `SquadSubsystemCornerHold.cpp:422` | C6011 | **FALSE POSITIVE** | `DecideLeaderAfterFall` returns `PreviousLeader` only if `bPreviousKnown` is true, in which case `Previous` is guaranteed non-null. | — |
| **#08** | `SquadSubsystemCornerHold.cpp:467` | C6011 | **FALSE POSITIVE** | `DecideLeaderAfterFall` returns `Holder` only if `HoldsCorner(*Holder)` evaluated to true, guaranteeing `Holder` is non-null. | — |
| **#09** | `HordeIntroSubsystem.cpp:406` | C6011 | **FALSE POSITIVE** | `Shots` is only non-empty if `bFlyover && Camera` succeeded. If `Camera` is null, `Shots.Reset()` runs and line 381 early-returns before line 406. | — |
| **#10** | `CodexTacticsPlayerControllerPeekCarry.cpp:153` | C6011 | **FALSE POSITIVE** | `DecideFormationCarry` returns `Drop` if `Squad == nullptr` via `!bSameLeader || !bWallOrder`, causing an early-return at line 146. | — |
| **#11** | `CodexTacticsPlayerControllerRunPeek.cpp:458` | C6011 | **FALSE POSITIVE** | `DecideGroup` returns `Single` if `Squad == nullptr`; line 458 is located inside the `Formation` branch which is unreachable when `Squad` is null. | — |
| **#12** | `SaveGameSubsystem.cpp:1721` | C28182 | **FALSE POSITIVE** | `Squad->GetMembers()` filters with `Member.IsValid()` and never yields null elements. MSVC analyzer confuses the outer default initialization `Member = nullptr` with the range-for variable `Candidate`. | — |
| **#13** | `DropLinkSubsystem.cpp:147` | C6011 | **REAL** | In `RegenerateLinks`, line 139 checks `if (World)`, but lines 147 and 151 call `World->GetTimerManager().SetTimer(...)` unconditionally. If called during world teardown / transition, crashes. | Add `if (!World) return;` before timer registration at line 146. |
| **#14** | `RelocationSubsystem.cpp:447` | C28182 | **FALSE POSITIVE** | Same range-for pattern as #12: MSVC confuses `Carrier = nullptr` with non-null elements returned by `Squad->GetMembers()`. | — |
| **#15** | `TutorialSubsystem.cpp:1990` | C6011 | **FALSE POSITIVE** | Guarded by `Points.IsEmpty()`; if `Route == nullptr`, `Points` remains empty and line 1986 calls `continue;`, skipping line 1990 entirely. | — |
| **#16** | `ActionBarWidget.cpp:532` | C6011 | **FALSE POSITIVE** | Guarded by `!Badge.IsEmpty()`; `Badge` is only non-empty if `Member != nullptr` was verified during member badge resolution. | — |
| **#17** | `CodexTacticsHUD.cpp:1582` | C6011 | **FALSE POSITIVE** | Guarded by `!PanicBadge.IsEmpty()`; `PanicBadge` is only populated when `Panic != nullptr` in earlier HUD resolution logic. | — |
| **#18** | `PhaseBannersWidget.cpp:259` | C6011 | **FALSE POSITIVE** | `bCutscene` is derived from `IsCutsceneShown()`, which returns `Flow && Flow->GetPhase() == ECodexGamePhase::Cutscene`, guaranteeing `Flow != nullptr`. | — |
| **#19** | `TutorialOverlayWidget.cpp:434` | C6011 | **FALSE POSITIVE** | Iterates over `Renderer ? MarkerLabels : TArray<FMarkerLabel>()`; if `Renderer` is null, the temporary array is empty and loop body never executes. | — |
| **#20** | `VehicleSubsystem.cpp:1324` | C28182 | **FALSE POSITIVE** | Same range-for pattern as #12 and #14; `Squad->GetMembers()` elements are strictly non-null. | — |
| **#21** | `AimOffsetSmokeCommand.cpp:198` | C6011 | **REAL** | In `case 4:`, `State.StageTime < Op->AimTargetHoldSeconds + 1.5f || !Anim` accesses `Op->` before `!Anim`. If `State.Op` died/destroyed, `Op` is null and crashes before checking `!Anim`. | Reorder expression: `if (!Op || !Anim || State.StageTime < Op->AimTargetHoldSeconds + 1.5f)`. |
| **#22** | `CornerEntrySmokeCommand.cpp:357` | C6011 | **REAL** | In `Tick()`, line 328 checks `if (Squad)`, but line 357 calls `Op = Squad->GetLeader();` unconditionally in `Stage == 0`. If `Squad` is null, crashes. | Check `if (Squad) { Op = Squad->GetLeader(); }`. |
| **#23** | `CornerEntrySmokeCommand.cpp:415` | C6011 | **REAL** | In `case 0:` of path execution, line 415 accesses `Op->bInCover` without verifying `Op = State.Op.Get()`. If leader died, crashes. | Add `if (!Op) return Finish(State);`. |
| **#24** | `CoverReverseSmokeCommand.cpp:311` | C6011 | **REAL** | In `case 1:`, lines 302, 308, 311, 312 call methods on `Op` without verifying that `Op = State.Op.Get()` is valid. If operative GC'd, crashes. | Add `if (!Op) return Finish(State, false);` at the beginning of `case 1:`. |
| **#25** | `CoverSwitchSmokeCommand.cpp:344` | C6011 | **REAL** | `Anim = AnimOf(Op)` is checked as nullable at lines 333, 341, 346, 347, but line 344 calls `Anim->GetCoverClipLog()` unconditionally. If anim is null, crashes. | Add `Anim &&` before `Anim->GetCoverClipLog()`. |
| **#26** | `DefendSmokeCommand.cpp:155` | C6011 | **REAL** | `Defender` is guarded at line 108, but in `case 1:` line 155 calls `Defender->GetActorLocation()` unconditionally. If defender dies during 8 s wait, crashes. | Add `if (!Defender) return Finish(State, false);` before line 155. |
| **#27** | `DropSoftlockSmokeCommand.cpp:579` | C6011 | **REAL** | In `case EPhase::Setup:`, line 579 calls `Squad->IsSoloMode()` without checking if `Squad` is non-null. | Wrap in `if (Squad && Squad->IsSoloMode())`. |
| **#28** | `DropSoftlockSmokeCommand.cpp:607` | C6011 | **REAL** | In `case EPhase::Click1:`, line 608 dereferences `*Leader->DisplayName.ToString()`. If `Leader` is null on this frame, crashes. | Add `if (!Leader) return Finish(State, false);` before line 608. |
| **#29** | `EncounterProbeCommand.cpp:56` | C6011 | **FALSE POSITIVE** | `Squad->GetMembers()` elements are strictly non-null. MSVC flags line 56 because line 55 defensively used ternary `Member ? Member->HealthComponent : nullptr`. | — |
| **#30** | `EnvironmentRainShotsCommand.cpp:468` | C6011 | **FALSE POSITIVE** | `Rain` is validated non-null during `EStep::Warmup` (lines 406-413) before state can advance to `EStep::Setup`. | — |
| **#31** | `EnvironmentRainShotsCommand.cpp:560` | C6011 | **FALSE POSITIVE** | Same as #30: state cannot transition from `Warmup` to `Measure` without `Rain != nullptr`. | — |
| **#32** | `FallDropSmokeCommand.cpp:747` | C28182 | **REAL** | In `Run()`, `Leader = Squad.GetLeader()` is fetched at line 725. Line 747 calls `Leader->GetActorLocation()` without null check. If all squad members died, crashes. | Add `if (!Leader) return;` at top of `Run()`. |
| **#33** | `FallDropSmokeCommand.cpp:1100` | C6011 | **FALSE POSITIVE** | `*Squad` is guarded by `Leader` check in `EPhase::Setup` (line 1084), and `Leader` is only non-null when `Squad` is non-null. | — |
| **#34** | `GroupCoverPrioritySmokeCommand.cpp:210` | C6011 | **REAL** | Line 209 checks `Three[I] && Three[I]->GetCoverSlot()...`, but line 210 and 213 immediately dereference `Three[I]->GetActorLocation()` without check. Crashes if any member missing. | Add early exit in `CheckSettled` if `!Three[0] || !Three[1] || !Three[2]`. |
| **#35** | `GroupSelectSmokeCommand.cpp:90` | C6011 | **FALSE POSITIVE** | `Squad` is non-null because line 75 returns early if `Members.Num() < 3`, which is populated via `Squad ? Squad->GetMembers() : TArray<>()`. | — |
| **#36** | `HitFlashOverlaySmokeCommand.cpp:72` | C6011 | **REAL** | Inside timer lambda, line 67 checks `W2 ? ... : nullptr`, but line 72 calls `W2->GetTimerManager().SetTimer(...)` unconditionally. If world torn down, crashes. | Add `if (!W2) return;` at start of lambda (line 67). |
| **#37** | `LeanPoseSmokeCommand.cpp:337` | C6011 | **FALSE POSITIVE** | `Case` is validated non-null in `case 1:` before calling `Next(State, 2)`. `CaseIndex` does not change between stage 1 and stage 2. | — |
| **#38** | `LevelGenLaddersSmokeCommand.cpp:650` | C6011 | **REAL** | In `case EPhase::Order:`, lines 649-650 call `Leader->OrderMoveTo` and `Leader->GetActorLocation()` without null check, unlike `Setup` which explicitly checked it. | Add `if (!Leader) return Finish(State, false);` in `case EPhase::Order:`. |
| **#39** | `LevelGenRampSmokeCommand.cpp:541` | C6011 | **FALSE POSITIVE** | `Squad` is guaranteed non-null by `Squad->GetMembers().Num() >= 3` check in `EPhase::Settle` before reaching `EPhase::Order`. | — |
| **#40** | `SaveRoundTripSmokeCommand.cpp:812` | C6011 | **REAL** | `Medic` is checked with `Medic && ...` at line 672, but after reload at line 812 `Medic->IsSniperWeaponEquipped()` is called unconditionally. Crashes if medic absent. | Add `Medic &&` to condition at line 812. |
| **#41** | `SnowIndoorSmokeCommand.cpp:471` | C6011 | **FALSE POSITIVE** | `SnowFx` is validated in `EStage::Settle` (`if (!SnowFx || !SnowFx->IsSnowActive())`) before `EStage::Phase` can be entered. | — |
| **#42** | `TurnBasedSmokeCommand.cpp:163` | C6011 | **REAL** | Line 157 checks `Active && Active->AP == 8` in non-aborting `Check()`. Line 163 dereferences `Active->AP == 7` without check. Crashes if `GetUnitState` returned null. | Change to `Active && Active->AP == 7`. |
| **#43** | `TurnRulesSmokeCommand.cpp:100` | C6011 | **REAL** | Line 94 handles null via `UnitState ? UnitState->AP : 0`. Lines 100 and 102 dereference `UnitState->GridPos` without null check. Crashes if unit not in turn system. | Add `if (!UnitState) return Finish(State, false);` after line 94. |
| **#44** | `BotCoverRulesTest.cpp:160` | C6011 | **FALSE POSITIVE** | `(*Cover)` is inside `if (TestTrue(..., TryGetObjectField(...) && Cover))`. In Unreal, `TestTrue` returns the boolean parameter, so the block only runs when `Cover != nullptr`. | — |
| **#45** | `ArsenalTest.cpp:40` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., M16 && Pistol && Grenade && Knife)) return false;`. `M16` is guaranteed non-null. | — |
| **#46** | `ArsenalTest.cpp:49` | C6011 | **FALSE POSITIVE** | Same guard as #45; `Pistol` is guaranteed non-null. | — |
| **#47** | `EnemyAIRulesTest.cpp:162` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., TryGetObjectField(..., CVars) && CVars)) return false;`. `CVars` is guaranteed non-null. | — |
| **#48** | `EnemyTargetPriorityTest.cpp:152` | C6011 | **FALSE POSITIVE** | Same guard pattern as #47; `CVars` is guaranteed non-null. | — |
| **#49** | `MilitaryZombieAssetTest.cpp:100` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestNotNull(TEXT("skeletal mesh set"), Mesh)) return false;`. In Unreal, `TestNotNull` returns true only when pointer is non-null. | — |
| **#50** | `MilitaryZombieAssetTest.cpp:107` | C6011 | **FALSE POSITIVE** | `Body` is guaranteed non-null because `Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr`, and line 96 verified `Mesh != nullptr`. | — |
| **#51** | `WeaponAnimLayerTest.cpp:339` | C6011 | **REAL** | Line 305 tests `Retargeted && ...` with `TestTrue(...)`, but has NO `if` guard or early return! Line 339 dereferences `(*Retargeted)->Values` unconditionally. Crashes test runner. | Add `if (!Retargeted || !Retargeted->IsValid()) return false;` before line 339. |
| **#52** | `WeaponLayerHandoffTest.cpp:78` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., TryGetObjectField(..., Pistol) && Pistol)) return false;`. `Pistol` is guaranteed non-null. | — |
| **#53** | `LauncherRulesTest.cpp:102` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(..., TryGetObjectField(..., RosterBlock) && RosterBlock))`. Only executes when `RosterBlock != nullptr`. | — |
| **#54** | `LauncherRulesTest.cpp:111` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(..., TryGetObjectField(..., LoadoutBlock) && LoadoutBlock))`. Only executes when `LoadoutBlock != nullptr`. | — |
| **#55** | `MortarRulesTest.cpp:42` | C6011 | **FALSE POSITIVE** | Guarded by `if (!Test.TestTrue(..., TryGetObjectField(..., Block) && Block)) return ...;`. `Block` is guaranteed non-null. | — |
| **#56** | `PistolRulesTest.cpp:83` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., TryGetObjectField(..., Pistol) && Pistol)) return false;`. `Pistol` is guaranteed non-null. | — |
| **#57** | `WeaponBallisticsTest.cpp:175` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(TEXT("pistol + shotgun"), Pistol && Shotgun))`. Only executes when both pointers are non-null. | — |
| **#58** | `WeaponBallisticsTest.cpp:176` | C6011 | **FALSE POSITIVE** | Same guard as #57; `Shotgun` is guaranteed non-null. | — |
| **#59** | `WeaponSchemaTest.cpp:248` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., TryGetObjectField(..., Block) && Block)) continue;`. `Block` is guaranteed non-null. | — |
| **#60** | `WeaponSchemaTest.cpp:342` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(TEXT("assets"), M16 && Sniper))`. `Sniper` is guaranteed non-null. | — |
| **#61** | `WeaponSchemaTest.cpp:365` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(TEXT("shotgun + pistol"), Shotgun && Pistol && M16))`. `Shotgun` is guaranteed non-null. | — |
| **#62** | `WeaponSchemaTest.cpp:371` | C6011 | **FALSE POSITIVE** | Same guard as #61; `Pistol` is guaranteed non-null. | — |
| **#63** | `WeaponSchemaTest.cpp:372` | C6011 | **FALSE POSITIVE** | Same guard as #61; `M16` is guaranteed non-null. | — |
| **#64** | `WeaponTuningTest.cpp:110` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., M16Json && ...)) return false;`. `M16Json` is guaranteed non-null. | — |
| **#65** | `WeaponTuningTest.cpp:111` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(..., SniperJson && ...)) return false;`. `SniperJson` is guaranteed non-null. | — |
| **#66** | `EnvironmentClimateTest.cpp:113` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(..., TryGetObjectField(..., Environment)))`. `Environment` is only non-null when `TryGetObjectField` succeeds. | — |
| **#67** | `EnvironmentClimateTest.cpp:422` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(..., TryGetObjectField(..., Environment)))`. `Environment` is guaranteed non-null. | — |
| **#68** | `FormationWallEndRulesTest.cpp:102` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(TEXT("Every place"), P && K && Z)) return false;`. `P` is guaranteed non-null. | — |
| **#69** | `FormationWallEndRulesTest.cpp:103` | C6011 | **FALSE POSITIVE** | Same guard as #68; `K` is guaranteed non-null. | — |
| **#70** | `FormationWallEndRulesTest.cpp:104` | C6011 | **FALSE POSITIVE** | Same guard as #68; `Z` is guaranteed non-null. | — |
| **#71** | `FormationWallGroupRulesTest.cpp:88` | C6011 | **FALSE POSITIVE** | Guarded by `if (!TestTrue(TEXT("every member has a look"), K && Looks.Num() == 3)) return false;`. `K` is guaranteed non-null. | — |
| **#72** | `FormationWallLeadershipRulesTest.cpp:143` | C6011 | **FALSE POSITIVE** | Inside `if (TestTrue(TEXT("East: every role placed"), EK && EP && EZ))`. `EK` is guaranteed non-null. | — |
| **#73** | `FormationWallLeadershipRulesTest.cpp:144` | C6011 | **FALSE POSITIVE** | Same guard as #72; `EP` is guaranteed non-null. | — |
| **#74** | `FormationWallLeadershipRulesTest.cpp:144` | C6011 | **FALSE POSITIVE** | Same guard as #72; `EZ` is guaranteed non-null. | — |

---

## 4. Recommendations for Safe Remediation

1. **Prioritize Runtime Defect #13 immediately:** `DropLinkSubsystem.cpp:147` is the only crash bug present in player-facing runtime code. Adding an early null guard on `World` prevents client/server crashes on level teardown.
2. **Prioritize Test Defect #51:** `WeaponAnimLayerTest.cpp:339` is the only fatal flaw in the unit test suite. Adding an early return if `Retargeted` is null ensures that automated CI/CD runs fail gracefully with clear diagnostic messages instead of crashing the runner process.
3. **Batch-fix Smoke Commands (#21-#43) in a dedicated task:** All 15 smoke command defects share common patterns (omitted `Op = State.Op.Get()` checks, assuming `Anim` is valid after an earlier null-aware ternary, or relying on non-aborting `Check()` calls). A focused refactor of `Source/CodexTacticsDev/` will eliminate all 15 real crash vectors in debug commands.
