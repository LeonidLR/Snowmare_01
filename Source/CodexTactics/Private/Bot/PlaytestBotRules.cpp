#include "Bot/PlaytestBotRules.h"

EBotProfile PlaytestBotRules::ParseProfile(const FString& Name)
{
	const FString Upper = Name.ToUpper();
	if (Upper == TEXT("CASUAL"))
	{
		return EBotProfile::Casual;
	}
	if (Upper == TEXT("VETERAN"))
	{
		return EBotProfile::Veteran;
	}
	return EBotProfile::Normal; // NORMAL / REGULAR / anything else
}

FString PlaytestBotRules::ProfileName(EBotProfile Profile)
{
	switch (Profile)
	{
	case EBotProfile::Casual: return TEXT("CASUAL");
	case EBotProfile::Veteran: return TEXT("VETERAN");
	default: return TEXT("NORMAL");
	}
}

FBotProfileConfig PlaytestBotRules::GetProfileConfig(EBotProfile Profile)
{
	FBotProfileConfig Config;
	switch (Profile)
	{
	case EBotProfile::Veteran: // _veteran_deploy_defenses / _veteran_combat_assist
		Config.Turrets = 1;
		Config.Barricades = 2;
		Config.Mines = 1;
		Config.bGuardAfterDeploy = true;
		Config.HealBelowHealthFraction = 0.45f;
		Config.WarmAboveCold = 70.f;
		break;
	case EBotProfile::Normal: // _normal_deploy_defenses / _normal_combat_assist
		Config.Turrets = 1;
		Config.Barricades = 1;
		Config.Mines = 1;
		Config.HealBelowHealthFraction = 0.38f;
		Config.WarmAboveCold = 80.f;
		break;
	default: // casual: nothing deployed, no assists
		break;
	}
	return Config;
}

int32 PlaytestBotRules::FindCluster(const TArray<FVector>& Enemies, float Radius, FVector& OutCenter)
{
	int32 Best = 0;
	OutCenter = FVector::ZeroVector;
	for (const FVector& A : Enemies)
	{
		int32 Count = 0;
		FVector Sum = FVector::ZeroVector;
		for (const FVector& B : Enemies)
		{
			if (FVector::Dist(A, B) <= Radius)
			{
				++Count;
				Sum += B;
			}
		}
		if (Count > Best)
		{
			Best = Count;
			OutCenter = Sum / Count;
		}
	}
	return Best;
}

bool PlaytestBotRules::ShouldThrowGrenade(int32 ClusterCount, float DistanceCm, float ThrowRangeCm)
{
	return ClusterCount >= 3 && DistanceCm >= 300.f && DistanceCm <= ThrowRangeCm;
}

FVector PlaytestBotRules::FallbackPosition(const FVector& Position, const FVector& Threat, const FVector& RearDirection)
{
	const FVector Away = (Position - Threat).GetSafeNormal2D();
	const FVector Rear = FVector(RearDirection.X, RearDirection.Y, 0.f).GetSafeNormal();
	return Position + (Away + Rear * 0.7f).GetSafeNormal2D() * 500.f;
}

float PlaytestBotRules::CoverScore(float DistanceToStandCm, float CoverHealthFraction, bool bElevated)
{
	return 100.f + (bElevated ? 25.f : 0.f) + FMath::Clamp(CoverHealthFraction, 0.f, 1.f) * 30.f - DistanceToStandCm / 100.f * 2.5f;
}

FVector PlaytestBotRules::CoverStandPoint(const FVector& Cover, const FVector& ThreatCenter)
{
	return Cover - (ThreatCenter - Cover).GetSafeNormal2D() * 150.f;
}
