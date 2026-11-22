#include "Characters/VaultRules.h"

bool VaultRules::CanVault(float Height, bool bHasLanding, float LandingStep)
{
	return Height >= MinHeight && Height <= MaxHeight && bHasLanding && FMath::Abs(LandingStep) <= MaxLandingStep;
}

float VaultRules::Duration(bool bRunning)
{
	return (bRunning ? 0.95f : 1.25f) / 1.15f;
}

FVector VaultRules::Position(const FVector& Start, const FVector& Landing, float Height, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	const float Smooth = FMath::SmoothStep(0.f, 1.f, T);
	const float Apex = FMath::Max(MinApex, Height + ApexOffset);
	FVector Result = FMath::Lerp(Start, Landing, Smooth);
	Result.Z += FMath::Sin(T * PI) * Apex;
	return Result;
}

FVector VaultRules::NavAreaExtent(const FBox& LocalBounds, const FVector& Scale, float PaddingCm)
{
	if (!LocalBounds.IsValid)
	{
		return FVector(100.f, 100.f, 100.f); // UNavModifierComponent's own failsafe
	}
	const FVector Abs = Scale.GetAbs();
	const FVector Half(FMath::Max(FMath::Abs(LocalBounds.Min.X), FMath::Abs(LocalBounds.Max.X)) * Abs.X,
		FMath::Max(FMath::Abs(LocalBounds.Min.Y), FMath::Abs(LocalBounds.Max.Y)) * Abs.Y,
		FMath::Max(FMath::Abs(LocalBounds.Min.Z), FMath::Abs(LocalBounds.Max.Z)) * Abs.Z);
	const float Pad = FMath::Max(0.f, PaddingCm);
	return FVector(Half.X + Pad, Half.Y + Pad, Half.Z + 50.f);
}

bool VaultRules::IsVaultableCoverKind(FName Kind)
{
	// Barrels are NOT vaulted (game designer 2026-11-15, mantling slice 1: explosive) - they stay cover, 90 cm.
	static const FName Kinds[] = { FName(TEXT("sandbag")), FName(TEXT("jersey")), FName(TEXT("low_wall")), FName(TEXT("window_sill")) };
	for (const FName& Each : Kinds)
	{
		if (Kind == Each)
		{
			return true;
		}
	}
	return false;
}

bool VaultRules::IsVaultableLevelObject(FName Kind, float HeightCm, float ThinSideCm)
{
	return IsVaultableCoverKind(Kind) && HeightCm >= MinHeight && HeightCm <= MaxHeight && ThinSideCm > 0.f && ThinSideCm <= MaxVaultableDepthCm;
}

VaultRules::EVaultResume VaultRules::DecideResume(const FVaultResumeFacts& Facts)
{
	if (!Facts.bHasOrder)
	{
		return EVaultResume::NoOrder;
	}
	const FVector ToGoal = Facts.Goal - Facts.Landing;
	if (ToGoal.Size2D() <= ResumeArrivedCm)
	{
		return EVaultResume::AtGoal;
	}
	if (Facts.bRecrossing)
	{
		const FVector Direction = (Facts.Landing - Facts.Start).GetSafeNormal2D();
		if (!Direction.IsNearlyZero() && FVector::DotProduct(Direction, ToGoal.GetSafeNormal2D()) < ResumeBehindDot)
		{
			return EVaultResume::PingPong;
		}
	}
	return EVaultResume::Resume;
}

bool VaultRules::IsRecrossing(bool bSameObstacle, bool bPreviousResumed, const FVector& PreviousDirection, const FVector& Direction,
	const FVector& PreviousGoal, const FVector& Goal, float SecondsSincePrevious)
{
	return bSameObstacle && bPreviousResumed && SecondsSincePrevious >= 0.f && SecondsSincePrevious <= RecrossWindowSeconds
		&& FVector::DotProduct(PreviousDirection.GetSafeNormal2D(), Direction.GetSafeNormal2D()) < 0.f
		&& FVector::Dist2D(PreviousGoal, Goal) <= RecrossGoalToleranceCm;
}
