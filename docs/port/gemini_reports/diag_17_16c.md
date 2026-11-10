# Diagnostic Report: #17 / 16c (Ladders and Ramps: Reversals, Combat Stalls, and Slope Velocity)

**Issue Summary:**
Investigate three interrelated verticality and traversal issues on ladders and ramps:
1. **Reversing direction mid-ladder and mid-ramp:** Abrupt cancellation or direction reversals mid-climb/mid-ascent can trigger desynchronization, transition lockouts, or head-on deadlock between followers.
2. **Squad stuck on a ramp when combat starts:** When a firefight begins while the squad traverses a ramp, followers remain locked in single-file traffic holds (`SetRampHold`), unable to take evasive action.
3. **Running up ramps too fast:** Operatives ascend steep ramps at the same speed as flat terrain without physical slope deceleration.

---

## 1. Exact Files, Functions, and Line Numbers (Copied from Source)

1. **`Source/CodexTactics/Private/Ladders/LadderClimbComponent.cpp:495-513`**
   ```cpp
void ULadderClimbComponent::OnMoveOrderChanged(bool bUp)
{
	if (bUp == (State == ELadderClimbState::ClimbingUp))
	{
		if (bReversePending)
		{
			bReversePending = false; // ordered back the way he climbs: no turn-round
			UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: turn-round cancelled (going %s again)"), *GetNameSafe(GetOwner()), bUp ? TEXT("up") : TEXT("down"));
		}
		return;
	}
	if (bReversePending)
	{
		return;
	}
	bReversePending = true;
	ReverseAt = LadderRules::NextRungBoundary(LoopPos, bUp ? 1 : -1, LoopLength);
	UE_LOG(LogCodexTactics, Display, TEXT("[Ladder] %s: new order behind him (top %.0f cm / bottom %.0f cm away) - turns %s at rung %d t %.2f"), *GetNameSafe(GetOwner()),
		FromTop, FromBottom, bUp ? TEXT("down") : TEXT("up"), ReverseAt.Rung, ReverseAt.Time);
}
   ```

2. **`Source/CodexTactics/Private/Characters/SquadSubsystemRampFile.cpp:493-506`**
   ```cpp
		const bool bWait = Wait.Contains(Candidate.Operative) && Hold.CooldownSeconds <= 0.f;
		if (Hold.bHeld && RampFileRules::HoldExpired(Hold.HeldSeconds, Tune))
		{
			SetRampHold(*Candidate.Operative, false, 0, TEXT("waited too long - the man ahead does not move on"));
			Hold.CooldownSeconds = Tune.RetryAfterSeconds;
		}
		else if (bWait && !Hold.bHeld)
		{
			SetRampHold(*Candidate.Operative, true, Candidate.Direction, *WaitWhy.FindRef(Candidate.Operative));
		}
		else if (!bWait && Hold.bHeld)
		{
			SetRampHold(*Candidate.Operative, false, 0, TEXT("the way is free"));
		}
   ```

3. **`Source/CodexTactics/Private/Formation/RampFileRules.cpp:7-15`**
   ```cpp
	bool IsRampSurface(const FVector& FloorNormal, bool bTerrain, float MinSlopeDeg)
	{
		if (bTerrain || FloorNormal.Z <= 0.0)
		{
			return false;
		}
		const double Up = FloorNormal.GetSafeNormal().Z;
		return Up < FMath::Cos(FMath::DegreesToRadians(static_cast<double>(MinSlopeDeg)));
	}
   ```

4. **`Source/CodexTactics/Private/Formation/RampFileRules.cpp:38-66`**
   ```cpp
	int32 FindWaitBehind(int32 Me, TArrayView<const FRampWalker> Walkers, const FRampFileConfig& Config, bool bMeWaiting)
	{
		if (!Walkers.IsValidIndex(Me) || Walkers[Me].Direction == 0)
		{
			return INDEX_NONE;
		}
		const FRampWalker& Self = Walkers[Me];
		const float Gap = Config.FileGapCm + (bMeWaiting ? Config.ReleaseMarginCm : 0.f);
		int32 Best = INDEX_NONE;
		float BestAhead = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Walkers.Num(); ++Index)
		{
			const FRampWalker& Other = Walkers[Index];
			if (Index == Me || Other.Direction != Self.Direction || !Other.bProgressing)
			{
				continue;
			}
			// How far the other one is ahead of him in their walking direction; level with him the earlier priority counts as ahead.
			const float Ahead = (Other.Along - Self.Along) * static_cast<float>(Self.Direction);
			const bool bOtherAhead = Ahead > Config.SideBySideCm || (FMath::Abs(Ahead) <= Config.SideBySideCm && Other.Priority < Self.Priority);
			if (bOtherAhead && Ahead < Gap && Ahead < BestAhead)
			{
				Best = Index;
				BestAhead = Ahead;
			}
		}
		return Best;
	}
   ```

---

## 2. Ranked Hypotheses with Code Evidence

### Problem 1: Reversing Direction Mid-Ladder / Mid-Ramp
- **Ladder Cause (Rank 1):** In `LadderClimbComponent.cpp:495`, reversal logic only functions while in `ELadderClimbState::ClimbingUp` or `ClimbingDown`. If the character is in `Entry` (mounting the ladder) or `Exit` (stepping onto roof), `OnMoveOrderChanged` is not evaluated or cannot reverse root-motion montages. Issuing an opposite move order during entry/exit either drops the character via `AbortClimb` or forces completion of the mount animation before reversing.
- **Ramp Cause (Rank 2):** In `RampFileRules.cpp:51`, `Other.Direction != Self.Direction` causes `FindWaitBehind` to ignore walkers moving in the opposite direction. If the leader turns around while a follower is climbing up, they walk directly into each other. Because narrow ramps do not have width for passing, capsule collision stops them, resulting in mutual blockage until `HoldExpired` (6 s) triggers.

### Problem 2: Squad Stuck on Ramp When Combat Starts
- **Root Cause (Rank 1):** In `SquadSubsystemRampFile.cpp:494–506`, followers on a ramp are held by `SetRampHold` (`Hold.bHeld = true`). When combat starts, the leader halts or acquires a target. The followers behind the leader evaluate `bWait = true` because the leader is not `bProgressing`. `SetRampHold` forces followers to remain stationary for up to `MaxHoldSeconds = 6.f` before `HoldExpired` releases them. During these 6 seconds, operatives stand immobilized on the open incline.

### Problem 3: Running Up Ramps Too Fast
- **Root Cause (Rank 1):** While `RampFileRules::IsRampSurface` correctly detects ramp inclination from `FloorNormal.Z`, neither `UCharacterMovementComponent` nor `USquadSubsystem` applies a velocity scale based on the slope gradient. Characters maintain flat-ground sprint speeds (`MaxWalkSpeed = 600 cm/s`) even when moving against steep 30°–40° inclines.

---

## 3. How to Verify

- **Automated Smoke Tests:**
  - `powershell Scripts/test.ps1 -Smoke LadderClimbSmoke`
  - `powershell Scripts/test.ps1 -Smoke LevelGenRampSmoke`
  - `powershell Scripts/test.ps1 -Smoke LadderQueueSmoke`
  - `powershell Scripts/test.ps1 -Smoke RampRoofShots`
- **Console Variables:**
  - `Codex.Squad.RampFile 1` (toggles single-file ramp management)
  - `Codex.Squad.RampFile.Debug 1` (logs per-frame hold reasons)
  - `Codex.Debug.Movement 1` (shows velocity vectors and speeds on slopes)
- **Log Lines to Watch:**
  - `LogCodexTactics: Display: [Ladder] %s: climb aborted in %s (reason %d)`
  - `LogCodexTactics: Display: [Ramp] %s held by %s on ramp`

---

## 4. Smallest Proposed Fix (Description Only)

1. **Break Ramp Holds on Combat Entry:**
   In `USquadSubsystem::TickRampFile`, check `GameFlowSubsystem->GetPhase() == ECodexGamePhase::Combat` or `Operative->IsFoughtThisFight()`. If combat is active, immediately clear `Hold.bHeld` (`SetRampHold(..., false, ...)`), allowing operatives to scatter, seek cover, or return fire.
2. **Handle Opposing Traffic on Ramps:**
   In `RampFileRules::FindWaitBehind`, if an operative detects another walker with opposite direction (`Other.Direction == -Self.Direction`), designate the downhill walker to yield (`YieldSpots`), stepping aside at the mouth or ramp landing.
3. **Slope Speed Modifier:**
   In `OperativeCharacterMovement` or `CharacterMovementComponent`, multiply `MaxWalkSpeed` by `FMath::Clamp(FloorNormal.Z, 0.6f, 1.0f)` when `UphillSpeed > 0`, reducing sprint speed on steep ascents by 25–40%.
