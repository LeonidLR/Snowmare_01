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
