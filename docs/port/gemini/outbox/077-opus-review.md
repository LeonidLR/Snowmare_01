# Review: Gemini C++ task 077 (CodexTactics, UE 5.8.2)

## HIGH

- **[HIGH] The completed-cover cooldown lands on `Hold`, not on `TakeCover`/`Relocate`** (`UpdateTeamRunning`, success branch).
  - Both cover branches set `State.Order = EPmcOrder::Hold;` first, then call `State.FailedUntil.Add(State.Order, ...)`.
  - The in-cover path therefore puts `Hold` (the baseline order) on a 4 s cooldown. `TakeCover`/`Relocate` get no cooldown at all.
  - The interrupted path also blocks `Hold` for 3 s. It only adds `TakeCover` explicitly, so an interrupted `Relocate` can be re-issued immediately.
  - The fighter has just been "transitioned" to `Hold`, which is now blacklisted. The next `Decide` is pushed toward a move order, often `TakeCover`/`Relocate` again. This recreates the same-spot flapping the patch is meant to fix.
  - Fix: capture `const EPmcOrder Completed = State.Order;` before overwriting. Apply the cooldown to `Completed`, and never to `Hold`.

## MEDIUM

- **[MEDIUM] Heal arrival can check the wrong patient.**
  - `BuildFighterView` now does `State.Patient.Reset()` and re-picks the nearest wounded mate on every `Decide`. That includes `Decide` calls made while a heal approach is still walking.
  - If another mate becomes nearer or more wounded mid-walk, `State.Patient` changes. The arrival block then measures distance to, and heals, a different patient than the one the walk targeted.
  - This produces spurious "patient out of reach -> cancel" with `FailedOrderCooldownSeconds`, or a heal on an unintended mate.
  - The patient of a running Heal order should be preserved, not reset.

- **[MEDIUM] Heal can now be reported "not running" while the fighter is stationary.**
  - `IsOrderRunning(Heal)` now requires `HasOwnMoveOrder()` or link traversal.
  - Previously a Heal counted as running for as long as the patient was valid. That suggests the catalog Heal may cover approach plus treatment.
  - If treatment is a timed action, or if the catalog auto-heals on arrival, two problems follow:
    - The commander can reassign the fighter mid-heal.
    - The new arrival block calls `SquadOrderCatalog::Execute(ESquadOrder::Heal, ...)` again. That risks a double heal or double medkit use, and `Team.Stats.Heals` counted twice.
  - The arrival path also never checks `IsLiveOperative(State.Patient)`, so a dead patient (health fraction 0 < 1) passes the condition.

- **[MEDIUM] A new threat wipes failure cooldowns as well as completion cooldowns.**
  - `Decide` removes the `TakeCover`/`Relocate`/`FallBack` entries whenever `ThreatActor != LastThreat`. Those entries are shared with `MarkFailed`, so "no path" and dropped-walk cooldowns are erased too.
  - If the chosen threat alternates between two enemies across decisions (the nearest-known enemy flips), the cooldowns are cleared every tick. This defeats both the failure and completion anti-churn mechanisms.

- **[MEDIUM] Unconditional fire-posture reset after any successful walk.**
  - The re-engagement block clears `bHoldingFire` and forces `ESquadFirePosture::Aggressive` after every non-failed walk. That covers Flank and Objective approaches, and any order with no explicit transition.
  - Hold-fire discipline set deliberately for an approach is dropped as soon as the walk ends, possibly revealing the fighter.
  - It also forces a `PriorityTarget` on `State.Target` without checking that the target is still alive, known, or visible.

- **[MEDIUM] `Hold` now sets a manual priority target in the open.**
  - The `bInCover` guard was removed, so any known threat, including a remembered or non-visible one, becomes the manual priority.
  - In the open, this can pin the fighter's aim and fire to a stale memory position. Meanwhile a different, visible enemy shooting him is ignored by auto-targeting.

- **[MEDIUM] The running-Objective score ignores conditions it used to apply.**
  - When `F.RunningOrder == Objective`, the score becomes `W.Objective * PushGain`. This bypasses the attacker/defender logic: clock urgency, the under-fire damping, and the "men on it" damping.
  - An attacker walking onto the point keeps a flat score even under heavy fire. Depending on the weights, that is a large jump up or down from the computed value, so this hysteresis can lock in or drop the order unpredictably.

- **[MEDIUM] Cover completion is judged on `bInCover` at the instant the walk ends.**
  - If cover entry (pose or animation) lags the end of the move by a frame or more, a correct arrival is classified as "interrupted approach".
  - That applies the `TakeCover` cooldown and logs a false interruption.

## LOW

- **[LOW] `TakeCover`/`Relocate` use `return false` when the fighter is already in the slot.**
  - This skips the end-of-function bookkeeping that `bTaken = false; break;` would run.
  - The caller likely treats it as a failure, triggering `MarkFailed` with a cooldown and a failure log, even though the fighter is already where the order wanted him.

- **[LOW] Lost-sight cleanup can desync `State.Order` from what the fighter is doing.**
  - When the memory ends, `State.Order` is set from `Suppress` to `Hold` even when `bOurOrder` is false.
  - In that case the fighter's active suppress order was issued by someone else and was not cancelled, so the state says `Hold` while he is still suppressing.

- **[LOW] Transitions to `Hold` never execute or broadcast the order.**
  - The completion transitions and the Objective "already on spot" path set `Order`/`State.Order = Hold` without running the Hold execution (priority target, stop) and without broadcasting `OnFighterOrder`.
  - Listeners and logs therefore keep showing the stale order.
  - In `ExecuteOrder`, `Order = EPmcOrder::Hold` only takes effect if `Order` is a reference or is written back to `State.Order`. Please verify.

- **[LOW] The Objective "already on spot" check targets a moving spot.**
  - It compares against the freshly chosen `Spot`. If that spot is re-randomized within the radius each decision, the check rarely triggers.
  - When it does trigger, the next decision can pick Objective again and churn Objective -> Hold every cycle.

- **[LOW] Test coverage is missing for the risky changes.**
  - Tests cover only `CanReissueOrderAt` and JSON parsing.
  - Nothing covers the `UpdateTeamRunning` transitions (which would have caught the HIGH bug), the `OnPointDamp` / stand-in Advance scoring, or the threat-change cooldown reset.