# Plan 077: PMC Order Lifecycle: Completed Orders, Heal Approach, Silent Suppress, Cover Stop, Firing Readiness

- **Task:** 077 (`077-pmc-order-lifecycle`) / Codex 030 diagnosis
- **Governing Spec:** `docs/port/gemini/inbox/077-pmc-order-lifecycle.md`, `docs/port/codex/outbox/030-pmc-residual-churn.report.md`
- **Base Commit:** `46e2f083fd8536eab8bf401aeb0d518e63bab18a` (`main`)
- **Branch:** `gemini/077-pmc-order-lifecycle`
- **Worktree:** `CodexTactics-gemday`
- **Allowed Paths:**
  - `Source/CodexTactics/Public/AI/Pmc*.h`
  - `Source/CodexTactics/Private/AI/Pmc*.cpp`
  - `Content/Data/AI/pmc_commander.json`
  - `Content/Data/AI/pmc_commander.bounds.json`
  - `Source/CodexTacticsTests/Private/AI/Pmc*`
  - `Source/CodexTacticsDev/Private/Debug/Pmc*Smoke*.cpp` (read state / add checks only)
  - `Source/CodexTacticsDev/Private/Debug/RpgPmcSmokeCommand.cpp` (read state / add checks only)
  - `docs/port/gemini/outbox/077-pmc-order-lifecycle.plan.md`
  - `docs/port/gemini/outbox/077-pmc-order-lifecycle.report.md`

---

## 1. Diagnosis of the 5 Order Lifecycle Defects (Codex 030 Reference)

Codex 030 diagnosed 64 historical same-order repeats (task 028) and 10 new repeats under `PmcStalemateSmoke`, as well as quiet spells lasting up to 54.4 s and 72.5 s. The root causes in order lifecycle are:

1. **Cause A: Completed FallBack / TakeCover not reported DONE (Causes 1 & 4 in 030):**
   - In `PmcSquadSubsystem.cpp:1089-1095`, when `State.bWalkPending && !IsOrderRunning(*Fighter, State)` triggers, it only tests for failure (`IsWalkShort(..., FailShortCm)`). Succeeded moves leave `State.bWalkPending = false` while `State.Order` remains `TakeCover` or `FallBack`.
   - In `Decide()`, `IsOrderRunning` evaluates to `false` (the move finished). Because neither `TakeCover` nor `FallBack` is a static order (`IsStaticOrder` is only `Hold/Ambush/Crouch`), and because no completion cooldown exists, `PickCommitted` re-selects `TakeCover` or `FallBack` every 0.5 s.
   - For `TakeCover`, if the fighter arrived at the slot, `SquadOrderCatalog::Execute(ESquadOrder::TakeCover)` still returns `true`, broadcasting `OnFighterOrder` again.
   - For `FallBack`, `ScoreFallBack` remains high (due to low health or outnumbered status), re-triggering `FallBack` repeatedly on the same spot (up to 35 times in 030 run 6).

2. **Cause B: Heal approach stays running after its move ends (Cause 3 in 030):**
   - In `PmcSquadSubsystem.cpp:988-992`, `IsOrderRunning` for `EPmcOrder::Heal` returns `true` as long as `State.Patient` is valid, alive, health < 1.0, and fighter has medkits.
   - When the fighter is walking towards the patient (`Dist2D > AidReachCm`), if the move ends (or the patient moves away), `IsOrderRunning` still returns `true`.
   - `Decide()` line 1399 sees `Pick == State.Order && bRunning` and continues without executing anything, causing the medic to freeze in place for 10-30 s while `Heal` is falsely considered running.

3. **Cause C: Suppress from sight memory silently overwrites order (Cause 5 in 030):**
   - In `PmcSquadSubsystem.cpp:800-815`, when a recently engaged target is lost, `UpdateSight` directly sets `FighterState.Order = EPmcOrder::Suppress`.
   - It fails to reset `FighterState.OrderSince = Now`, fails to clear `bWalkPending` or movement goals, and does not broadcast `OnFighterOrder`.
   - The commander and smoke telemetry miss this order transition, resulting in unrecorded detours and perceived duplicate order spikes (e.g. Flank -> Suppress -> Flank in 1.5 s).

4. **Cause D: Interrupted cover approach ("stops to take shot") treated as completed (Cause 6 in 030):**
   - In `PmcSquadSubsystem.cpp:1092`, `FailShortCm` is 400 cm. When a sniper or operative stops along the way to shoot (`stops to take the shot`), residual distance is often < 400 cm (e.g. 179 cm in 030 run 2), but `Fighter.bInCover` is `false`.
   - Because distance is < 400 cm, it does not hit `MarkFailed`. Because `Fighter.bInCover` is `false`, the next decision cycle scores `TakeCover` high again. The commander issues `TakeCover` again 0.5 s later, the fighter steps, stops to shoot, and repeats this cycle 5 times.

5. **Cause E: Firing readiness and target re-engagement after order completion (Cause 2 in 030):**
   - In `PmcSquadSubsystem.cpp:1553`, `PriorityTarget` is only set if `Fighter.bInCover`. Operatives who end moves in the open (e.g. after FallBack standoff walk, Advance, or Heal) never set `PriorityTarget` on the threat.
   - If the operative was holding fire (`bHoldingFire = true`) or facing away, no order completion hook realigns facing or restores Aggressive posture towards visible threats. This produces long quiet spells (54.4 s - 72.5 s) where fighters stand idle despite having enemies in sight and range.

---

## 2. Solutions by Cause (One Commit Per Cause)

### Commit 1 (Cause A): Completed FallBack / TakeCover Reported DONE & Same-Spot Re-issue Guard
- **Fix in one sentence:** When a `TakeCover` or `FallBack` walk finishes successfully, transition `State.Order` to `EPmcOrder::Hold` (reported DONE) and apply a completion cooldown (`completed_order_cooldown_s`) in `State.FailedUntil[Order]`, while `ExecuteOrder` rejects re-issuing the same order within arrival tolerance of the destination.
- **Files & Functions:**
  - `Content/Data/AI/pmc_commander.json`: add `"completed_order_cooldown_s": 4.0`.
  - `Content/Data/AI/pmc_commander.bounds.json`: add `"/completed_order_cooldown_s": { "min": 2.0, "max": 6.0, "step": 0.5 }`.
  - `Source/CodexTactics/Public/AI/PmcCommanderRules.h` & `Private/AI/PmcCommanderRules.cpp`:
    - Add `float CompletedOrderCooldownSeconds = 4.0f;` to `FPmcWeights` and parse in `FromJson`.
    - Add pure function `bool CanReissueOrderAt(EPmcOrder Order, const FVector& CurrentFeet, const FVector& TargetLocation, float ArrivalToleranceCm, float LevelToleranceCm)`.
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`:
    - In `UpdateTeamRunning()`: when `State.bWalkPending && !IsOrderRunning(*Fighter, State)` and walk did not fail short:
      - If `State.Order == EPmcOrder::TakeCover && Fighter->bInCover`: transition `State.Order = EPmcOrder::Hold`, record completion cooldown `State.FailedUntil.Add(EPmcOrder::TakeCover, Now + Team.Weights.CompletedOrderCooldownSeconds)`.
      - If `State.Order == EPmcOrder::FallBack`: transition `State.Order = EPmcOrder::Hold`, record completion cooldown `State.FailedUntil.Add(EPmcOrder::FallBack, Now + Team.Weights.CompletedOrderCooldownSeconds)`.
    - In `ExecuteOrder()`: prevent re-issuing `TakeCover` if the fighter is already in cover at that exact slot, and prevent `FallBack` if already at the standoff destination (`!PmcCommanderRules::IsWalkShort(Feet, Goal, PmcSquadLocal::ArriveCm, ...)`).
- **Telemetry / Proof:**
  - Log: `[PmcAI] team %d: %s completed %s at %s -> Hold`.
  - Verification: `Flow.QuickSameOrder` in `PmcStalemateSmoke` drops from 17-35 to <= 6.

### Commit 2 (Cause B): Heal Approach Lifecycle and Completion Validation
- **Fix in one sentence:** `IsOrderRunning` for `EPmcOrder::Heal` requires that the fighter is actively applying first aid within `AidReachCm` or actively moving towards the patient (`Fighter.HasOwnMoveOrder()`); if the approach move ends and the patient is not within reach, the heal approach is cancelled.
- **Files & Functions:**
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`:
    - Update `IsOrderRunning()` for `EPmcOrder::Heal`:
      ```cpp
      case EPmcOrder::Heal:
      {
          const bool bPatientValid = State.Patient.IsValid() && PmcSquadLocal::IsLiveOperative(State.Patient.Get())
              && PmcSquadLocal::HealthFractionOf(*State.Patient) < 1.f && Fighter.MedkitsCount > 0;
          if (!bPatientValid) { return false; }
          const float Dist = FVector::Dist2D(Fighter.GetActorLocation(), State.Patient->GetActorLocation());
          return (Dist <= PmcSquadLocal::AidReachCm) || Fighter.HasOwnMoveOrder() || PmcSquadLocal::IsTraversingLink(Fighter);
      }
      ```
    - In `UpdateTeamRunning()`: if `State.Order == EPmcOrder::Heal` and approach walk ends without patient in reach (`Dist > AidReachCm`): cancel heal order, log reason, and apply short retry cooldown (`failed_order_cooldown_s`).
- **Telemetry / Proof:**
  - Log: `[PmcAI] team %d: %s heal approach ended: patient out of reach (%.0f cm > %.0f cm)`.
  - Verification: No stalled fighters with `Order == Heal` standing idle for > 3 s without moving or healing.

### Commit 3 (Cause C): Lost Sight Memory Suppression Telemetry and Lifecycle
- **Fix in one sentence:** When point suppression starts from lost sight memory, update `OrderSince = Now`, set `Goal = Point`, clear `bWalkPending`, broadcast `OnFighterOrder` to notify commander/smoke tracking, and transition to `Hold` when suppression ends.
- **Files & Functions:**
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`:
    - In `UpdateSight()`:
      ```cpp
      if (bRecent && !Observer->HasSuppressOrder()
          && Observer->HasUsefulFireLine(Point + FVector(0.f, 0.f, SuppressionRules::GetConfig().OrderAimHeightCm))
          && Observer->OrderSuppress(nullptr, Point))
      {
          FighterState.Order = EPmcOrder::Suppress;
          FighterState.OrderSince = Now;
          FighterState.Goal = Point;
          FighterState.bWalkPending = false;
          FighterState.LostFireTarget = Recent;
          FighterState.LostFirePoint = Point;
          FighterState.LostFireUntil = Known->LastSeenTime + Team.Weights.ContactMemorySeconds;
          OnFighterOrder.Broadcast(Pair.Key, Observer, EPmcOrder::Suppress, Team.Weights.Suppress, EPmcPickReason::Best);
          UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s -> Suppress (sight memory point %s)"),
              Pair.Key, *Observer->DisplayName.ToString(), *Point.ToCompactString());
      }
      ```
    - When lost sight suppression ends (line 788): transition `FighterState.Order = EPmcOrder::Hold;`.
- **Telemetry / Proof:**
  - Log: `[PmcAI] team %d: %s -> Suppress (sight memory point ...)`.
  - Verification: Telemetry shows explicit Suppress events; no back-to-back Flank or Objective events without intervening order record.

### Commit 4 (Cause D): Interrupted Cover Approach Guard
- **Fix in one sentence:** A cover approach walk that ends without entering cover (`!Fighter->bInCover`) is classified as interrupted, does not count as completed `TakeCover`, and receives a cooldown (`cover_interrupted_cooldown_s`) so the fighter does not loop into `TakeCover` while actively firing.
- **Files & Functions:**
  - `Content/Data/AI/pmc_commander.json`: add `"cover_interrupted_cooldown_s": 3.0`.
  - `Content/Data/AI/pmc_commander.bounds.json`: add `"/cover_interrupted_cooldown_s": { "min": 1.5, "max": 5.0, "step": 0.5 }`.
  - `Source/CodexTactics/Public/AI/PmcCommanderRules.h` & `Private/AI/PmcCommanderRules.cpp`:
    - Add `float CoverInterruptedCooldownSeconds = 3.0f;` to `FPmcWeights` and parse in `FromJson`.
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`:
    - In `UpdateTeamRunning()`:
      ```cpp
      if (State.bWalkPending && !IsOrderRunning(*Fighter, State))
      {
          State.bWalkPending = false;
          if (State.Order == EPmcOrder::TakeCover || State.Order == EPmcOrder::Relocate)
          {
              if (!Fighter->bInCover)
              {
                  // Interrupted (e.g. stopped to take a shot, or blocked before cover).
                  State.FailedUntil.Add(State.Order, Now + Team.Weights.CoverInterruptedCooldownSeconds);
                  UE_LOG(LogCodexTactics, Display, TEXT("[PmcAI] team %d: %s %s interrupted before entering cover - cooldown %.1f s"),
                      TeamId, *Fighter->DisplayName.ToString(), PmcCommanderRules::OrderName(State.Order),
                      Team.Weights.CoverInterruptedCooldownSeconds);
                  State.Order = EPmcOrder::Hold;
              }
          }
      }
      ```
- **Telemetry / Proof:**
  - Log: `[PmcAI] team %d: %s TakeCover interrupted before entering cover - cooldown %.1f s`.
  - Verification: Vance / sniper stop-to-shoot events do not produce 5 consecutive `TakeCover` re-issues every 0.5 s.

### Commit 5 (Cause E): Immediate Target Re-engagement & Firing Readiness After Order Completion
- **Fix in one sentence:** When any move order finishes or transitions to Hold, the operative immediately re-engages the best visible threat by setting `PriorityTarget` (independent of cover), restoring Aggressive fire posture, and turning towards the target if in the open.
- **Files & Functions:**
  - `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp`:
    - In `ExecuteOrder()` for `EPmcOrder::Hold`:
      - Remove `if (Fighter.bInCover && ThreatActor ...)` restriction: execute `ESquadOrder::PriorityTarget` whenever `ThreatActor` is valid, even in the open.
      - If `View.bHasThreat && View.Threat.bSeenNow && !Fighter.bInCover`: turn fighter to face `View.Threat.Location` via `ESquadOrder::Hold`.
      - Reset `State.bHoldingFire = false` and ensure `ESquadFirePosture::Aggressive` when target is in line of fire.
    - In `UpdateTeamRunning()` upon order completion: immediately call re-engagement helper to align facing and assign priority target.
- **Telemetry / Proof:**
  - Log: `[PmcAI] team %d: %s re-engages threat %s (distance %.0f cm)`.
  - Verification: `LongestQuietS` in `PmcStalemateSmoke` drops to < 30 s (threshold is 45 s).

---

## 3. Unit Tests & Smokes

### Automation Tests (`Source/CodexTacticsTests/Private/AI/PmcCommanderRulesTest.cpp` or new test file)
1. `CodexTactics.AI.PmcCommander.CompletedOrderCooldown`: Verifies `PickCommitted` respects completion cooldown in `FailedUntil` for completed orders.
2. `CodexTactics.AI.PmcCommander.CanReissueOrderAt`: Verifies pure rule rejects same order at destination within arrival tolerance.
3. `CodexTactics.AI.PmcCommander.HealApproachActive`: Verifies heal approach running check respects distance vs movement state.
4. `CodexTactics.AI.PmcCommander.CoverInterruptedCooldown`: Verifies interrupted cover receives appropriate cooldown.

### Smokes to Execute
1. Target Smokes (3/3 required):
   - `PmcStalemateSmoke` x 3
   - `PmcEncounterHuntSmoke` x 3
   - `RpgPmcSmoke` x 3
2. Full PMC Suite (1x each):
   - `PmcEncounterSmoke`
   - `PmcTeamSmoke`
   - `PmcCommanderSmoke`
   - `PmcArenaSmoke`
   - `PmcArenaObjectiveSmoke`
   - `PmcStandoffSmoke`
   - `PmcUnderFireSmoke`
   - `PmcFireLineSmoke`
3. Parallel execution command:
   ```powershell
   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Commands <Name>
   ```

---

## 4. Verification & Quality Gates
- Build verification: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`
- Unit tests: `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.AI.Pmc`
- Gemini Gate: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 077 -SonnetReview`
- Final Report: `docs/port/gemini/outbox/077-pmc-order-lifecycle.report.md`
