#include "Environment/CharacterSnowRules.h"

// UE-only (no Godot reference). Design: docs/systems/environment-weather.md section 15 (user decisions 2026-10-23).

namespace CharacterSnowRules
{
	FVector2D ToLocalWind(const FVector& WorldWindDirection, const FQuat& BodyRotation)
	{
		const FVector Local = BodyRotation.UnrotateVector(FVector(WorldWindDirection.X, WorldWindDirection.Y, 0.0));
		return FVector2D(Local.X, Local.Y).GetSafeNormal();
	}

	FDirectionalSnowInputs MakeInputs(const FEnvironmentConfig& Config, const FCharacterSnowConditions& Conditions)
	{
		FDirectionalSnowInputs Inputs;
		Inputs.SideRateScale = Config.ObjectSnowSideRateScale;
		Inputs.WindDirection = Conditions.LocalWind;
		Inputs.WindStrength = Conditions.WindStrength;
		Inputs.TopFloor = 0.f;
		if (Conditions.bSheltered)
		{
			Inputs.ClearRate = FMath::Max(0.f, Config.CharacterSnowClearRate);
			return Inputs;
		}
		Inputs.GrowRate = FMath::Max(0.f, Conditions.SnowCoverRate) * FMath::Max(0.f, Config.CharacterSnowFillScale);
		Inputs.MeltRate = FMath::Max(0.f, Conditions.ObjectMeltRate) * FMath::Max(0.f, Config.CharacterSnowMeltScale);
		return Inputs;
	}

	FDirectionalSnow RotateToLocal(const FDirectionalSnow& World, const FQuat& BodyRotation)
	{
		FDirectionalSnow Local;
		Local.Top = World.Top;
		for (int32 LocalIndex = 1; LocalIndex < FDirectionalSnow::PocketCount; ++LocalIndex)
		{
			const FVector LocalSideInWorld = BodyRotation.RotateVector(FDirectionalSnow::GetPocketDirection(LocalIndex));
			float Sum = 0.f;
			for (int32 WorldIndex = 1; WorldIndex < FDirectionalSnow::PocketCount; ++WorldIndex)
			{
				const float Dot = FMath::Max(0.f, static_cast<float>(FVector::DotProduct(LocalSideInWorld, FDirectionalSnow::GetPocketDirection(WorldIndex))));
				Sum += World.Get(WorldIndex) * Dot * Dot;
			}
			Local.Set(LocalIndex, FMath::Clamp(Sum, 0.f, 1.f));
		}
		return Local;
	}

	FDirectionalSnow MakeSpawnCover(const FEnvironmentConfig& Config, const FDirectionalSnow& ObjectSnow, float SnowingAmount, const FQuat& BodyRotation)
	{
		const float Share = FMath::Clamp(Config.CharacterSnowSpawnShare, 0.f, 1.f) * FMath::Clamp(SnowingAmount, 0.f, 1.f);
		FDirectionalSnow Cover = RotateToLocal(ObjectSnow.Clamped(), BodyRotation);
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			Cover.Set(Index, Cover.Get(Index) * Share);
		}
		return Cover;
	}

	float MaxDifference(const FDirectionalSnow& A, const FDirectionalSnow& B)
	{
		float Max = 0.f;
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			Max = FMath::Max(Max, FMath::Abs(A.Get(Index) - B.Get(Index)));
		}
		return Max;
	}

	bool ShouldPush(const FDirectionalSnow& Pushed, const FDirectionalSnow& Current, float SecondsSincePush, float IntervalSeconds, float Threshold)
	{
		if (SecondsSincePush < IntervalSeconds)
		{
			return false;
		}
		if (MaxDifference(Pushed, Current) > Threshold)
		{
			return true;
		}
		// The last bit to exactly 0 is pushed even below the threshold: an empty character must render exactly as without the layer.
		for (int32 Index = 0; Index < FDirectionalSnow::PocketCount; ++Index)
		{
			if (Current.Get(Index) == 0.f && Pushed.Get(Index) != 0.f)
			{
				return true;
			}
		}
		return false;
	}
}
