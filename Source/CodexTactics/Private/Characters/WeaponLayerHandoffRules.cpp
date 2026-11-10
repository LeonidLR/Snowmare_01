#include "Characters/WeaponLayerHandoffRules.h"

namespace WeaponLayerHandoffRules
{
	bool DefersToAbp(const FWeaponLayerHandoffFacts& Facts, float CoverApproachCm)
	{
		if (Facts.bCrouchAbpSet && Facts.bCrouching)
		{
			return true; // the designer: the set's crouch looked "hunched over in three folds" - the squad's rifle crouch instead
		}
		if (Facts.bInCover)
		{
			return true; // pressed, low, sliding, the corner peek and its fire pose, the corner holds: the ABP's cover clips for every set
		}
		// "cover_mode": "abp" (the D-1, the designer 2026-11-09): the shot off the wall and the run-in to the slot look like an M16 operative's too.
		return Facts.bCoverAbpSet && (Facts.bCoverOpenShot || (Facts.bPendingCover && Facts.PendingCoverDistanceCm <= FMath::Max(0.f, CoverApproachCm)));
	}

	float SmoothLayerDirection(float Current, float Target, float DeltaSeconds, float SmoothSeconds, float DeadZoneDeg)
	{
		if (SmoothSeconds <= KINDA_SMALL_NUMBER)
		{
			return FRotator::NormalizeAxis(Target);
		}
		const float Delta = FMath::FindDeltaAngleDegrees(Current, Target);
		const float Excess = FMath::Sign(Delta) * FMath::Max(0.f, FMath::Abs(Delta) - FMath::Max(0.f, DeadZoneDeg));
		const float Alpha = 1.f - FMath::Exp(-FMath::Max(0.f, DeltaSeconds) / SmoothSeconds);
		return FRotator::NormalizeAxis(Current + Excess * Alpha);
	}

	void StepLayerDirection(FWeaponLayerDirectionState& State, float Target, float DeltaSeconds, float SmoothSeconds, float DeadZoneDeg, float SnapDeg,
		float FadeSeconds)
	{
		if (!State.bValid)
		{
			State.Current = State.From = FRotator::NormalizeAxis(Target);
			State.Fade = 1.f;
			State.bValid = true;
			return;
		}
		const bool bCrossFade = SnapDeg > 0.f && FadeSeconds > KINDA_SMALL_NUMBER;
		State.Fade = bCrossFade ? FMath::Min(1.f, State.Fade + FMath::Max(0.f, DeltaSeconds) / FadeSeconds) : 1.f;
		if (bCrossFade && FMath::Abs(FMath::FindDeltaAngleDegrees(State.Current, Target)) > SnapDeg)
		{
			// A reversal: the legs fade from what shows most now to the new direction's clips (mid-fade: the heavier side is kept as From).
			State.From = State.Fade >= 0.5f ? State.Current : State.From;
			State.Current = FRotator::NormalizeAxis(Target);
			State.Fade = 0.f;
			return;
		}
		State.Current = SmoothLayerDirection(State.Current, Target, DeltaSeconds, SmoothSeconds, DeadZoneDeg);
	}
}
