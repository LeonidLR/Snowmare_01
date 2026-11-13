# Review: Task 034 / Bug #324 (`034_lookdir.patch`)

**Verdict: No HIGH findings.** The fix correctly addresses all four root-cause paths, and the non-stealth paths are untouched. Several MEDIUM items should be addressed, mainly around turn-rate scope, idle-facing scope, and test rigor.

---

## Mandate 1: Non-stealth regression check

Every behavioral change is gated on `IsInStealthMode()` except the `SetStealthMode(false)` exit clears (see M3).

| Change | Non-stealth effect |
|---|---|
| `SmoothRate` | Falls back to `10.f`, which is identical to the old hard-coded value. |
| `BaseTurnRate` | The else branch is the original `GetTurnRate(MovementConfig, Stance)`. |
| `bFacingFormationSector` | The added term `!IsInStealthMode()` is true outside stealth, so the expression is unchanged. |
| Idle-facing branch | The added term `&& !IsInStealthMode()` is a no-op outside stealth. |
| `RequestMove` | The new block runs only in stealth. |
| `ApplyFormationFrame` / `UpdateFollower` | The new guards apply only in stealth. |
| Wall facing | Uses its own rate, `2 × GetTurnRate(Stance)`, so it is unaffected. |

Cover approach, formation sectors, real-time combat facing and the turn-based grid are unaffected outside stealth.

## Mandate 2: Safety

- **`SmoothVelocity` default argument:** `float Rate = 10.f` is source-compatible with all existing 3-argument callers. Because it is `inline` in the header, there is no ABI concern within the module.
- **`ApplyFormationFrame`:** The new block sits after the existing `continue` guard that validates `Operative`. `LookFilters.Remove(Member)` on a missing key is a safe no-op.
- **`UpdateFollower`:** `Operative` is already dereferenced earlier in this function, so the new `Operative->IsInStealthMode()` call adds no new null risk.
- **Arrays and lifetime:** There is no array indexing and there are no new lifetime hazards. The smoke test uses the existing `Op` weak pointer pattern.

## Mandate 3: Turn-timing math

- 18 rad/s equals 1031.3 °/s.
  - A 90° turn takes about 0.087 s.
  - A 180° turn takes about 0.175 s.
- The smoothing filter at rate 20 has a time constant of 0.05 s.
- Resetting `SmoothedVelocity` to zero on `RequestMove` is a good choice. `Lerp(0, V, α)` preserves V's direction, so the filter heading equals the new velocity heading on the first moving frame, with no lag from the old direction.
- The total stays within the 0.3 s budget, with some margin.

---

## [MEDIUM] findings

### M1: Fast turn rate also applies to anchor and threat locks during fast stealth
The condition `IsInStealthMode() && (!bStealthLook || IsStealthFast())` gives the 1031 °/s rate to the `bStealthLook` branch whenever the operative is running fast.

- **Problem 1:** A manual anchor or auto-lock while running fast will snap at sprint rate instead of the controlled crouch/strafe rate. Spec 008 §5 treats locks as deliberate, smooth aiming.
- **Problem 2:** The fast rate also leaks into combat-target and `bTurnToThreat` facing while stealthed without a look target, because those branches consume the same `TurnSpeed`.

**Fix:** Scope the fast rate to the movement-direction branch only:
```cpp
const float MoveTurnSpeed = (IsInStealthMode() && !bStealthLook)
    ? FMath::Max(GetTurnRate(MovementConfig, EOperativeStance::Standing), StealthFastTurnRateDeg) / 57.2958f
    : TurnSpeed;
```
Use `MoveTurnSpeed` only in the `SmoothedVelocity` facing `SetActorRotation`.

If the design wants faster lock acquisition during fast stealth, make that an explicit, separately tuned value.

### M2: Idle-facing guard in `UpdateCombatFacing` is too broad
`bHasIdleFacing && ... && !IsInStealthMode()` suppresses all idle facing in stealth. The code comment says `IdleFacingYaw` also carries "an order's idle facing", i.e. a player-issued facing order on arrival. Those orders are now silently ignored in stealth.

The bug's actual source is the parked-follower write, which is already fixed at its origin in `SquadSubsystem.cpp:1860`. `SetStealthMode` and `RequestMove` also clear stale values.

**Fix:** Drop the consumer-side guard and rely on the source-side guards. Alternatively, tag idle-facing origin (`EIdleFacingSource::Follower` vs `Order`) and suppress only the follower source in stealth.

If the spec intends "no ordered facing in stealth", document that in Spec 008 explicitly.

### M3: `SetStealthMode(false)` clears state on exit
Clearing `IdleFacing` and `FormationLookYaw` when leaving stealth goes beyond the bug.

- For parked followers this is harmless: `UpdateFollower` and `ApplyFormationFrame` repopulate on the next tick, and `LookFilters` is re-seeded.
- It does drop a pending order-facing on exit, and it is the one change that touches the non-stealth state machine.

**Fix:** Clearing on enter is sufficient. Remove the three lines from the disable path, or add a short comment justifying them.

### M4: `SmoothedVelocity` is reset on every stealth `RequestMove`
Followers are re-pathed frequently (formation repath about every 0.2 s). Each reset re-ramps the filter from zero.

At rate 20 and 60 fps the first-frame magnitude is about 28% of velocity, so it currently stays above the 0.35 m/s hold threshold. At low framerates, or at slow-stealth speeds near the threshold, facing can freeze for a frame per repath, producing micro-stutter.

**Fix:** Reset only when the new move direction differs significantly from the current smoothed direction, for example:
```cpp
if (SmoothedVelocity.IsNearlyZero() || FVector::DotProduct(SmoothedVelocity.GetSafeNormal2D(), (InDestination - GetActorLocation()).GetSafeNormal2D()) < 0.5f)
{
    SmoothedVelocity = FVector::ZeroVector;
}
```
Alternatively, gate the reset on player-issued orders only.

### M5: Smoke test may not exercise the consumer-side guards
In stage 620 the test calls `SetIdleFacingYaw` and `SetFormationLookYaw` *before* `SetStealthMode(true)`. Whether those stale values survive depends on whether `SetStealthMode` early-outs when already enabled (the operative was put in stealth at the previous stage):

- **If it early-outs:** the values survive and the `UpdateCombatFacing` guards are tested.
- **If it does not:** the new clear wipes them, and the test only validates the clear, not the `bFacingFormationSector` and idle-facing guards.

**Fix:** Set the right-facing `IdleFacingYaw` and `FormationLookYaw` *after* `SetStealthMode(true)`. Also re-inject `SetFormationLookYaw(Right)` every frame during 621 and 622 to simulate the squad subsystem writing it.

### M6: Squad-subsystem paths are untested
The designer's scenario came from squad flow (`ApplyFormationFrame` sectors, `UpdateFollower` parked yaw). Stages 620–624 drive a single operative directly, so fixes #3 and #4 have no coverage.

**Fix:** Add a stage with a leader and at least one follower in stealth. Fast-move the squad, park it, and assert per frame that:
- `GetFormationLookYaw()` is unset,
- `HasIdleFacing()` is false,
- each follower's yaw matches its arrival heading.

### M7: Test rigor gaps
- **(a) No 180° case.** Both turns (Right→Up, Up→Left) are 90°. The mandate explicitly lists the 180° reversal, which is the worst case for both the filter and the turn time. Add Left→Right.
- **(b) Single-sample timing check.** The check samples once at the first frame at or after 0.3 s. Also record the time when |Δyaw| first drops below 5°, assert it is ≤ 0.3 s, and assert the yaw never moves *away* from the target (no initial wrong-way swing).
- **(c) Single-sample stability check.** Stages 622 and 624 sample drift once at 0.5 s. Track max drift every frame instead, and extend the hold to at least 1.0 s. The crouch idle-turn rate could still sit under 1° at 0.5 s if a slow pull exists.
- **(d) Timeout falls through silently.** The `StageTime < 8.f` timeout proceeds as if arrived. Add `Check(State, StageTime < 8.f && Dist2D(Op, Target) < AcceptRadius, "arrived")` so a stuck path fails loudly instead of producing misleading yaw results.
- **(e) Order-latency flakiness.** The 0.3 s window is measured from stage entry, which includes path request and acceleration. If pathfinding is async, this becomes flaky. Measure from the first frame with `Velocity.Size2D() > 10`, consistent with the spec ("from movement start").
- **(f) Fast-mode check on the wrong frame.** `IsStealthFast()` is checked on the same frame as `OrderMoveTo`. Confirm it is set synchronously; otherwise check it in 621.

---

## [LOW] findings

- **L1: Magic number.** Move `1031.3f` into `MovementConfig` as `StealthFastTurnRate`, so it is tunable and not tied to an inline comment.
- **L2: Repeated literal.** `57.2958f` appears repeatedly. Use `FMath::DegreesToRadians` or a named constant.
- **L3: Encoding.** The patch file is UTF-16 and the `§` in the new comment renders as `�`. Ensure the source files are saved as UTF-8, matching repo convention, or use ASCII `S1/S5`.
- **L4: Stale docstring.** The `FacingRules::SmoothVelocity` comment still says "time constant ~0.1 s". Update it to "time constant = 1/Rate (default 0.1 s)".
- **L5: Duplicated block.** The clear block (`ClearIdleFacing` + `SetFormationLookYaw({})` + zero `SmoothedVelocity`) appears in three places. Extract it into `ResetStealthFacingState()`.
- **L6: Stage 2 reset ordering.** Stage 2 now sets the manual anchor every frame. The cleanup correctly calls `ClearStealthManualLookTarget()`, but it does so after `StopOperative()`. Move the clear first so that no anchored facing tick runs on a stopped body.

---

## Summary

| Severity | Count | Items |
|---|---|---|
| HIGH | 0 | — |
| MEDIUM | 7 | Turn-rate scope (M1), idle-facing over-suppression (M2), exit clears (M3), repath velocity reset (M4), test exercises clears rather than guards (M5), no squad-path test (M6), test rigor (M7) |
| LOW | 6 | Constants, encoding, docstring, deduplication, cleanup order |

The fix is mergeable from a correctness and safety standpoint. I recommend addressing M1, M2, M5 and M7a/b before closing #324, so that the acceptance criteria (≤ 0.3 s alignment, no snap-back, locks unaffected) are actually proven by the smoke test.