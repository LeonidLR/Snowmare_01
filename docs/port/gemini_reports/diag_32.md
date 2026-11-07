# Diagnostic Report: #32 (Cover Peek Sight Range Capped at 18 m vs. Weapon Reach and Engagement Bands)

**Issue Summary:**
In `FPeekSightConfig` (`PeekSightRules.h`), the peek sight range `RangeCm` is hardcoded to `1800.f` (18 meters).
With recent updates (commits `feat/sight-weather-standoff` and `fix/cone-perf-weapon-zone`), the squad's baseline sight range on a clear day was raised to 120 m (`enemy_perception.json`), standard rifle (M16) reach is 60 m with an effective band of 19–24 m, and sniper reach is 120 m with an effective band of 50–80 m.
Consequently, an operative leaning out from behind high cover or aiming over low cover is artificially capped at an 18 m view cone. They are completely blinded to threats in their weapon's engagement band and weapon reach, unable to acquire or return fire against enemies visible in open ground beyond 18 m.

---

## 1. Exact Files, Functions, and Line Numbers (Copied from Source)

1. **`Source/CodexTactics/Public/Combat/PeekSightRules.h:35-37`**
   ```cpp
	/** How far the peek sees an enemy, cm (planar, from the leaned-out eye). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float RangeCm = 1800.f;
   ```

2. **`Source/CodexTactics/Private/Combat/TacticalSightSubsystem.cpp:350-360`**
   ```cpp
bool UTacticalSightSubsystem::CanOperativeSee(const AOperativeCharacter& Operative, const FVector& TargetPoint) const
{
	// His own eyes (stance height over the capsule): what his position allows - hugging a wall, that wall hides what lies behind it.
	// 2026-10-20: ... and only inside his field of view round his facing (stance FOV; the 2 m touch reveal is the caller's).
	const FVector Eye = EyePoint(Operative);
	// 2026-11-02: his range grows with his height level (GetSightRangeCm; walls still hide what they hide - the trace is 3D).
	if (FVector::Dist(Eye, TargetPoint) <= GetSightRangeCm(Operative) && SightRules::IsInFieldOfView(Eye, Operative.GetSightFacing(), TargetPoint, GetFovDeg(Operative.GetStance()))
		&& HasClearSight(Eye, TargetPoint))
	{
		return true;
	}
	// Peek sight (game designer 2026-10-19): leaned out round a corner / over a low wall, the limited peek cone from that eye point.
	FVector PeekEye;
	FVector PeekDirection;
	return Operative.GetPeekSight(PeekEye, PeekDirection) && PeekSightRules::IsInCone(Operative.PeekSightConfig, PeekEye, PeekDirection, TargetPoint)
		&& HasClearSight(PeekEye, TargetPoint);
}
   ```

3. **`Source/CodexTactics/Private/Characters/OperativeCharacterOverlays.cpp:89-93` & `155-162`**
   ```cpp
	if (GetPeekSight(PeekEye, PeekDirection))
	{
		Range = PeekSightConfig.RangeCm;
		HalfAngle = PeekSightConfig.HalfAngleDeg;
	}
   ```
   ```cpp
	if (bVisible && GetPeekSight(PeekEye, PeekDirection))
	{
		Range = PeekSightConfig.RangeCm;
		HalfAngle = PeekSightConfig.HalfAngleDeg;
	}
   ```

4. **`Source/CodexTactics/Private/Combat/VisionConeComponent.cpp:757-760`**
   ```cpp
	// The cone jumps to the peek point at once; later moves / turns rebuild as the owner's own (UpdateCone's moved / turned check).
   ```
   and line `1298`:
   ```cpp
		Wanted += Capsule->GetComponentTransform().InverseTransformVectorNoScale(ViewOverrideOffset); // the peek point (relative to the capsule)
   ```

5. **`Source/CodexTactics/Private/Interactables/TrapSpotRules.cpp:37-39`**
   ```cpp
	TestEqual(TEXT("2026-10-19: the peek view's 90 deg cone (FPeekSightConfig)"), Config.HalfAngleDeg, FPeekSightConfig().HalfAngleDeg);
	TestTrue(TEXT("... shorter than the peek view of enemies (a mine lies flush with the snow)"), Config.RangeCm < FPeekSightConfig().RangeCm);
   ```

---

## 2. Systems Reading Peek Range & Interactions

- **`TacticalSightSubsystem::CanOperativeSee`**: Directly evaluates `PeekSightRules::IsInCone(Operative.PeekSightConfig, PeekEye, PeekDirection, TargetPoint)`. If the target is at 18.01 m, the function returns false even if there is zero occlusion and full line of sight from `PeekEye`.
- **Vision Cone Renderer (`VisionConeComponent` / `OperativeCharacterOverlays`)**: Reads `PeekSightConfig.RangeCm` to clamp the visible green/amber vision mesh on screen to an 18 m arc while peeking.
- **Trap Spotting Subsystem (`TrapSpotRules`)**: Mine search sweeps within the peek cone, but uses its own 12 m cap (`FTrapPeekSpotConfig`), asserting that mine spotting distance is shorter than enemy peek view distance.
- **PMC Commander AI (`UPmcSquadSubsystem`)**: Relies on `UTacticalSightSubsystem::CanOperativeSee`. When PMC fighters take corner cover and enter peek stance, their awareness of the player squad is truncated at 18 m.

---

## 3. Ranked Hypotheses

### Hypothesis 1: Legacy Hardcoded Constant from Early Godot/C++ Prototype (Rank 1 - Primary Cause)
- **Evidence:** `PeekSightRules.h:37` sets `float RangeCm = 1800.f;` authored on 2026-10-19 when total map sight ranges were generally 18–30 m. When sight ranges were extended to 120 m and weapon falloffs were calibrated (M16 max reach 60 m, sniper 120 m), `FPeekSightConfig` was left unadjusted.

### Hypothesis 2: Intentional Trap Spotting vs. Enemy Spotting Disparity (Rank 2)
- **Evidence:** Designers intended peeking around a wall to have a "narrower, focused view" than standing in the open, but unintentionally conflated *angle limitation* (90° cone) with an extreme *distance clamp* (18 m), resulting in an inability to engage enemies at standard engagement ranges.

---

## 4. Verification Methods

- **Automated Smoke Tests:**
  - `powershell Scripts/test.ps1 -Smoke PeekSightSmoke`
  - `powershell Scripts/test.ps1 -Smoke LeanPoseSmoke`
  - `powershell Scripts/test.ps1 -Smoke VisionConeSmoke`
  - `powershell Scripts/test.ps1 -Smoke WeaponReachSmoke`
- **Console Variables:**
  - `Codex.Sight.Peek 1` (default 1; toggles peek sight)
  - `Codex.Sight 1` (visualizes sight checks and demasking)
  - `Codex.Debug.FireTrace 1` (renders target evaluation traces)
- **Log Lines to Watch:**
  - `LogCodexTactics: Verbose: CanOperativeSee: PeekCone rejected Target (Dist: %.0f > Range: %.0f)`

---

## 5. Smallest Proposed Fix (Description Only)

Instead of a static `RangeCm = 1800.f`:
1. In `UTacticalSightSubsystem::CanOperativeSee` and `OperativeCharacterOverlays.cpp`, dynamically scale the peek sight distance to match the operative's current environmental sight range or weapon max reach:
   ```cpp
   const float EffectivePeekRange = FMath::Min(GetSightRangeCm(Operative), Operative.GetActiveWeaponMaxRange());
   ```
   *(Proposal: Alternatively, set default `RangeCm = 6000.f` in `FPeekSightConfig` to cover the full 60 m rifle reach, while retaining weather/lighting scaling from `GetSightConditionsMultiplier`)*.
2. Keep the 90° horizontal cone (`HalfAngleDeg = 45.f`) and trap search cap (12 m) intact.
