# Review: commit `9e9e6cff` (CodexTactics task 034, fast stealth angular PlayRate fix)

**Verdict:** Request changes. There are two HIGH findings:

- **H1:** The new `Fast = 1.f` override adds a gait discontinuity, and it brings back sluggish legs at low speed.
- **H2:** The new smoke check cannot detect the original bug, so the acceptance verification is vacuous.

The `PhaseRate` lead-length fix (root cause #1) is directionally correct.

---

## [HIGH]

### H1. `Fast = 1.f` override causes jog-at-crawl-speed and a hard snap on stop
`OperativeAnimInstanceStealth.cpp:~119`

```cpp
const float Fast = (Operative->IsStealthFast() && bMoving) ? 1.f : SpeedBlend;
```

The old code was a smooth floor: `Max(SpeedBlend, MappedRange(0.65*FastCfg → FastCfg))`. The new code is a step function gated only on `bMoving`. That causes three problems.

1. **Acceleration from rest.**
   - With `move_start_speed = 3`, `bMoving` becomes true at a few cm/s.
   - `Fast` is then immediately 1.0, so only the jog cycle is weighted.
   - `PhaseRate` is roughly `MoveSpeed / BlendedNominal`. At 30 cm/s that is `30 / 463 ≈ 0.065 cycles·s⁻¹` of jog, which plays as about 0.05x.
   - This is a full jog stride in slow motion: the exact "viscous legs" symptom the task is meant to remove, now moved to the start of every fast-stealth move.
2. **Sudden stop or deceleration.**
   - `Fast` holds at 1.0 while speed falls. When `bMoving` flips false, `Fast` drops in one frame to `SpeedBlend`, which is about 0 near rest.
   - The weights snap from 100% jog to 100% walk/idle with no smoothing.
   - `LeadLength` also jumps from 0.8 s to 1.333 s on that frame, which changes the `PhaseRate` cap and the cycle mapping.
   - Result: a visible pose pop and a phase discontinuity.
3. **Fast to slow transition.**
   - Toggling `SetStealthFast(false)` while moving at 460 cm/s is fine, because `SpeedBlend` is about 1.
   - Toggling it at mid speed produces the same one-frame step.

The original problem (#2) was `Fast` dipping slightly below 1.0 at full speed during turns. That needs a floor near full speed, not a global override.

**Recommended fix:** Keep a speed-dependent ramp, but make it insensitive to small dips and smooth the output.

```cpp
const float FastFloor = FMath::GetMappedRangeValueClamped(
    FVector2D(FastConfigSpeed * 0.35f, FastConfigSpeed * 0.6f), FVector2D(0.f, 1.f), LocomotionSpeed);
const float FastTarget = (Operative->IsStealthFast() && bMoving) ? FMath::Max(SpeedBlend, FastFloor) : SpeedBlend;
StealthFastBlend = FMath::FInterpTo(StealthFastBlend, FastTarget, DeltaSeconds, 10.f); // new member, reset on stealth enter
const float Fast = StealthFastBlend;
```

Also add smoke checks for:
- a ramp from 0 to 460 cm/s, asserting no single-frame `|ΔFast| > 0.25`;
- a 460 to 0 stop, asserting the same.

### H2. The multi-angle smoke check does not catch the original bug
`StealthModeSmokeCommand.cpp:~250`

```cpp
Check(State, PlayRate >= 0.85f && PlayRate <= 1.25f, ...)
```

- The pre-fix code was clamped to 0.96x. That value lies inside `[0.85, 1.25]`, so this check passes on the buggy build at every angle.
- The comment states the actual acceptance criterion: "PlayRate must equal Speed / BlendedNominalSpeed". The test never asserts it.
- For 90°, the expected rate is about 1.02–1.11x depending on the authored speed source. The pre-fix 0.96 should fail.
- `PlayRate = Rate * 0.8f` hard-codes the jog clip length. If the clip length or skeleton changes, the check is silently wrong.

**Recommended fix:**
- Compute the expected value from the live set:
  `Expected = 460 / BlendedNominalSpeed(Dir, Set.JogSpeeds, Set.JogSpeed)`.
- Derive `PlayRate` as `Rate * Length(Set.Jog[LeadIndex])`, exposing it via a dev-only accessor if needed.
- Assert `FMath::IsNearlyEqual(PlayRate, Expected, 0.02f)`.
- Add one explicit regression guard at 90°: `PlayRate > 0.97f`, which documents that the 0.96 clamp is gone.

---

## [MEDIUM]

### M1. `LeadLength` can reach zero, risking division by zero or NaN
- Before this change, only `Walk[LeadIndex]` was required to exist. Now `Jog[LeadIndex]` is also read.
- If a set lacks a jog clip in that slot, `Length(nullptr)` (assuming it returns 0) makes `LeadLength` 0 or near 0 when `Fast ≈ 1`. Then `MaxPlayRate / LeadLength` gives Inf or NaN.
- That value goes into `PhaseRate`, then `StealthPhase = Fmod(...)`. A NaN phase permanently poisons sampling.

**Fix:**
- Fall back to the walk length when the jog clip is null.
- Clamp the result: `FMath::Max(LeadLength, 0.1f)`.
- Add an `ensureMsgf` in the loader for missing jog slots in stealth sets.

### M2. Blending `Length` linearly gives the wrong cap at partial `Fast`
- The cap is `MaxPlayRate / LeadLength`, which is a frequency.
- Lerping the lengths and then inverting is not the same as lerping the frequencies. At `Fast = 0.5`:
  - lerped length = 1.067 s, giving a cap of 1.50 cycles/s;
  - lerped frequency = (1.20 + 2.00) / 2, giving 1.60 cycles/s.
- The cap is about 6% too low through the blend region.

**Fix:** Compute the cap from interpolated frequencies:

```cpp
const float InvLen = FMath::Lerp(1.f / WalkLen, 1.f / JogLen, Fast);
```

Then pass `1.f / InvLen`, or refactor `PhaseRate` to take the cap directly.

### M3. The report's PlayRate table contradicts the authored JSON and the background analysis
- The JSON authors fwd = 463.51 and right = 414.97, which the report labels as "Source Mannequin".
- The runtime table instead uses Male (UE4) nominals (503.78 / 465.75) and reports 0.913x forward and 0.988x lateral.
- Under the Male numbers, lateral is below 1.0x. That does not reach the 1.02–1.11x "natural rate" cited as the fix target, and forward at 0.913x is noticeably under-rate (foot sliding).

**Clarify which table is authoritative:**
- If the per-skeleton `ClipSpeeds` override the JSON (which the Shotgun loader suggests), say so explicitly.
- Confirm whether 0.913x forward is acceptable to design.
- If it is not, the JSON fallback values are cosmetic for those skeletons.

### M4. Shotgun loader change has non-stealth blast radius
`OperativeAnimInstanceShotgun.cpp:548–561`

This change applies to all weapon sets, not only stealth. Previously, a missing key wrote 0, and `BlendedNominalSpeed` presumably treated 0 as "use scalar fallback". Now the prior array value survives.

Two risks:
1. **Stale data.** If the `FShotgunAnimSet` instance is reused across skeleton or weapon reloads and `WalkSpeeds`/`JogSpeeds` are not reset before parsing, values from a previous skeleton persist.
   - **Fix:** Verify the arrays are re-initialized from JSON or the scalar at the start of `LoadWeaponAnimSet`.
2. **Behaviour change for other sets.** A set that previously fell back to scalar `jog_speed` because of a missing key will now use JSON directional values, if present, or whatever default the array holds.
   - **Fix:** Audit every block in `weapon_anim_sets.json` for skeleton keys missing from `ClipSpeeds`.
   - Add a smoke assert that a non-stealth set's (e.g. rifle) directional nominals are unchanged against a golden list.

Also consider logging a `Verbose` warning when a key is missing. The silent fallback hides data gaps.

### M5. Angle coverage is one-sided and skips backward motion
- The test covers only 0°, 20°, 45°, and 90° to the right.
- The data is asymmetric: L = 419.23 vs R = 414.97, and BL = 443.91 vs BR = 436.87.
- The direction blend for negative angles and the 135°–180° wrap is a classic source of index bugs.

**Fix:** Add −20°, −45°, −90°, 135°, 180°, and −135°.

### M6. Sprint-cancel coverage regressed
- `SetStealthFast(false)` is called before `SetSprinting(true)`, so the sprint-breaks-stealth check now runs from slow stealth only.
- Sprint cancellation from an active fast-stealth move is the more relevant path, and it goes untested.

**Fix:** Add a case that keeps fast-stealth active at 460 cm/s, then sprints. Assert:
- `!IsInStealthMode()`;
- stealth clip weights reach 0 within N frames;
- `StealthPhase` is finite.

### M7. Fast/slow classification by clip-name substring is fragile and possibly vacuous
- `GetName().Contains("Fast")` / `Contains("Slow")` depends on naming conventions.
- If the walk clips are not named `*Slow*`, `SlowWeight` is always 0 and the "< 0.05" assert passes vacuously.

**Fix:** Classify clips by pointer membership in `Set.Jog` / `Set.Walk`. Assert `FastWeight + SlowWeight ≈ 1` so the classification is shown to be exhaustive.

### M8. Single-frame phase delta is a noisy measurement
- The test derives `Rate` from one 0.05 s step, which is sensitive to wrap handling and any per-frame smoothing.

**Fix:** Accumulate the unwrapped phase over about 10 frames and divide by the total time.

---

## [LOW]

### L1. Corrupted non-ASCII characters in the source comment
- The patch shows `0�, 20�…` in `StealthModeSmokeCommand.cpp`, a degree sign mangled by encoding.
- Non-ASCII characters in `.cpp` files risk MSVC C4819 warnings and toolchain encoding issues.
- **Fix:** Use "deg". The `.md` report has the same mojibake in its tables; save it as UTF-8.

### L2. Stale comment in `OperativeAnimInstanceStealth.cpp`
- The comment says "0.8s for jog vs 1.33s for walk". These are asset-specific numbers that will rot.
- **Fix:** Describe the intent ("blended gait lead clip length") without hard-coded durations.

### L3. Inaccurate wording in the new `Fast` comment
- "commit purely to the fast gait; never blend back in the 157 cm/s slow walk" is not accurate once H1 is fixed.
- **Fix:** Update the comment to match the final ramp logic.

### L4. Report line references will drift
- The report cites `OperativeAnimInstanceStealth.cpp:252` and `:119`.
- **Fix:** Reference function names instead.

### L5. Smoke test does not restore state on failure
- `Op->SetStealthFast(true)` is not undone if a later check aborts early, which could happen if `Check` ever becomes fatal.
- **Fix:** Use an `ON_SCOPE_EXIT { Op->SetStealthFast(false); };` guard.

---

## Safety summary
- **GC / UObject lifetime:** No new UObject ownership, and no raw pointers are cached across frames. OK.
- **Null handling:** Clip access in the smoke loop is null-checked. The loader guards `Walk[Index]` and `Jog[Index]`.
- **Bounds:** `LeadIndex` is used identically for `Walk` and `Jog`, both `TStaticArray<…, 8>`. OK if `LeadIndex` is already in [0, 7], as it was before.
- **NaN:** `SpeedValue > 0.f` correctly rejects NaN in the loader. The remaining NaN risk is M1.