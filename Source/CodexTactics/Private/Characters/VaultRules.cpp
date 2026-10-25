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
