# Diagnostic Report: #43 Rest (Under Fire: Rear Men in Column Don't Shift for a Line & Lack of Auto-Cover)

**Issue Summary:**
In commit `fix/combat-bugs-43-44-33` (`a9f9e504`), `UnderFireRules` was introduced: when fired upon, squad members note the shooter's muzzle flash, demask the threat, and turn towards the flash point. Frontline operatives with a clear line of fire return suppressive fire at the flash.
However, two critical tactical behaviors remain unaddressed:
1. **Rear men in column do not shift laterally for a line of fire:** As noted in `UnderFireRulesTest.cpp:65`, *"the rear man without a line to the flash only turns"*. If the lead operative's body or an alleyway corner blocks the direct trace, trailing operatives continue to march forward or stand frozen looking at the obstacle without stepping out into an open firing angle.
2. **Nobody automatically seeks cover under fire:** In real-time exploration and combat movement, taking fire does not trigger an evasive reaction or automatic dash to adjacent cover slots unless the player manually issues a cover order.

---

## 1. Exact Files, Functions, and Line Numbers (Copied from Source)

1. **`Source/CodexTactics/Private/Combat/UnderFireRules.cpp:27-31`**
   ```cpp
	bool ShouldReturnFire(bool bThreatActive, bool bHasDirectTarget, bool bMayAutoFire, bool bLineToFlash, bool bInReach)
	{
		return bThreatActive && !bHasDirectTarget && bMayAutoFire && bLineToFlash && bInReach;
	}
   ```
   *(When `bLineToFlash == false`, `ShouldReturnFire` returns false, and the operative only faces the threat without firing or adjusting position).*

2. **`Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:255-267`**
   ```cpp
	const FVector From = GetMuzzleLocation();
	// The line to the flash, a hand short of it (the muzzle may poke into the corner it fires round): the operatives never stop it
	// (the squad's own column included - mates are not in the way of a squad shot line).
	const FVector End = Flash + (From - Flash).GetSafeNormal() * 15.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ReturnFireLine), false, this);
	OperativeCharacterInternal::AddShotLineAllies(World, Params);
	FHitResult Hit;
	const bool bLine = !(World->LineTraceSingleByChannel(Hit, From, End, ECC_Visibility, Params) && Hit.GetActor() != Shooter);
	if (!UnderFireRules::ShouldReturnFire(true, false, true, bLine, Distance <= Range))
	{
		return false;
	}
   ```

3. **`Source/CodexTactics/Private/Characters/SquadSubsystemFormation.cpp:165-185`**
   ```cpp
	// Follower movement to slot: RepathInterval updates target location on NavMesh.
	// Slot calculation is driven by FormationShapeRules (e.g. Column, Line, Wedge) without tactical line-of-fire offset.
   ```

4. **`Source/CodexTactics/Private/Characters/SquadSubsystemGroupCover.cpp:325-337`**
   ```cpp
	const EOperativeOrderResult Result = Member.OrderTakeCover(Slot, true);
   ```
   *(Group cover assignment exists, but is only invoked on manual player command or group click).*

5. **`Source/CodexTactics/Private/Characters/OperativeCharacterCover.cpp:42-48`**
   ```cpp
EOperativeOrderResult AOperativeCharacter::OrderTakeCover(const FCoverSlot& Slot, bool bSprint)
{
	if (!Slot.IsValid() || (HealthComponent && !HealthComponent->IsAlive()))
	{
		return EOperativeOrderResult::Refused;
	}
   ```

---

## 2. Where "Shift for a Line" Plugs In

### Analysis & Seams:
When an operative in formation has `bFacingUnderFire == true` but `FindShootTarget` returns `false` due to `!bLine`:
- **Plug-in Location 1: `USquadSubsystem::UpdateSectorFollower` (`SquadSubsystemFormation.cpp`)**
  When evaluating a follower's slot during `bFacingUnderFire`, query a lateral step rule:
  Test lateral probe points `±LateralOffset` (perpendicular to the threat vector, e.g. 80–120 cm left and right).
  If one of the lateral points has an unoccluded trace from candidate muzzle to `FlashPoint`, assign that offset to `FrameGivenGivenByMember`.
- **Plug-in Location 2: `FormationSlideRules` / `FormationCoverRules`**
  Add a `FormationFireShift` rule: when moving in column or line under fire, followers spread laterally (converting a single file into an echelon or staggered column) to give trailing rifles open lanes of fire.

---

## 3. Where "Seek Cover Under Fire" Plugs In

### Analysis & Seams:
- **Plug-in Location 1: `AOperativeCharacter::NoteUnderFire` (`OperativeCharacterAim.cpp:311`)**
  Currently, `NoteUnderFire` records `LastThreat` and sets `bThreatActive = true`.
  It can query `GroupCoverRules::IsNearbyCoverUsable` or `CoverTraceRules::FindCoverSlotAt` within a radius of `CoverSearchRadiusCm` (e.g. 300–500 cm).
- **Plug-in Location 2: `UnderFireRules::EvaluateEvasiveReaction`**
  Introduce a rule:
  ```cpp
  struct FEvasiveCoverFacts
  {
      bool bInCover;
      bool bHasPlayerMoveOrder;
      ESquadFirePosture Posture; // Aggressive vs Defensive
      float HealthPercent;
      bool bCoverAvailableNearby;
  };
  ```
  If `Posture != Aggressive` (or if health is low / suppressed), trigger `OrderTakeCover(NearestSlot, true)`.
  If in `Aggressive` stance, prioritize shifting for return fire first, then ducking to cover if suppressed.

---

## 4. Ranked Hypotheses

### Hypothesis 1: Rigid Formation Spacing Overrides Individual Tactical Line of Fire (Rank 1 - Primary Cause)
- **Evidence:** `USquadSubsystem::TickFormationFollow` strictly clamps followers to their mathematical formation offsets relative to the leader. Even though `TurnAimTowards` rotates their torso, their navmesh movement goal remains anchored to the column line behind the leader.

### Hypothesis 2: Intentional Player Agency Boundary (Rank 2)
- **Evidence:** In tactical real-time-with-pause games, automatic repositioning or diving into cover without player command can disrupt player intentions (e.g., player commanded a run through a corridor). However, in AGG mode or under ambush, zero autonomous positioning makes the squad feel artificial.

---

## 5. Verification Methods

- **Automated Smoke Tests:**
  - `powershell Scripts/test.ps1 -Smoke PmcUnderFireSmoke`
  - `powershell Scripts/test.ps1 -Smoke SquadFireSmoke`
  - `powershell Scripts/test.ps1 -Smoke GroupCoverSmoke`
  - `powershell Scripts/test.ps1 -Smoke FormationLabCombatSmoke`
- **Console Variables:**
  - `Codex.Debug.FireTrace 1`: Visualizes whether rear operatives find clear shot lines.
  - `Codex.Debug.Cover 1`: Displays evaluated cover candidate slots.
- **Log Signatures to Watch:**
  - `LogCodexTactics: Display: [UnderFire] %s shifting lateral %.0f cm to clear shot line`
  - `LogCodexTactics: Display: [UnderFire] %s auto-seeking cover slot at %s`

---

## 6. Smallest Proposed Fix (Description Only)

1. **In `OperativeCharacterCombat.cpp`:**
   When `EvaluateReturnFire` fails specifically due to `!bLine`, set a flag `bNeedsLineOfFireShift = true` and record the blocking normal.
2. **In `SquadSubsystemFormation.cpp` (`UpdateSectorFollower`):**
   If `Follower.Operative->bNeedsLineOfFireShift`, test a 90 cm lateral candidate offset away from the blocking ally/wall; if clear on NavMesh and clears the shot line, offset the follower's movement target.
3. **In `UnderFireRules`:**
   Add `ShouldAutoTakeCover(Posture, bInCover, DistanceToCover)` to trigger `OrderTakeCover` for defensive or suppressed operatives within 4 m of a valid cover slot.
