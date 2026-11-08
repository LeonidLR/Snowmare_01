#include "Combat/ExplosionRules.h"

#include "Data/WeaponStats.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace ExplosionRules
{
	namespace ExplosionLocal
	{
		FExplosionConfig& Current()
		{
			static FExplosionConfig Config = Defaults();
			return Config;
		}
	}

	FName ProjectileKind()
	{
		static const FName Kind(TEXT("projectile"));
		return Kind;
	}

	float Falloff(const FBlastFalloff& Curve, float DistanceCm, float RadiusCm)
	{
		if (RadiusCm <= 0.f || DistanceCm > RadiusCm)
		{
			return 0.f;
		}
		const float T = FMath::Max(0.f, DistanceCm) / RadiusCm;
		switch (Curve.Model)
		{
		case EBlastFalloff::HalfAtEdge:
			return FMath::Clamp(1.f - T * 0.5f, 0.5f, 1.f); // GrenadeRules::DamageFalloff
		case EBlastFalloff::FullCore:
		{
			const float Core = FMath::Clamp(Curve.FullRadiusCm, 0.f, RadiusCm);
			if (DistanceCm <= Core || RadiusCm - Core <= KINDA_SMALL_NUMBER)
			{
				return 1.f;
			}
			return FMath::Lerp(1.f, FMath::Clamp(Curve.EdgeMultiplier, 0.f, 1.f), (DistanceCm - Core) / (RadiusCm - Core));
		}
		case EBlastFalloff::Linear:
		default:
			return 1.f - T * Curve.Factor; // DeployableRules::GetBlastDamage
		}
	}

	float VictimDamage(float Base, const FBlastFalloff& Curve, float DistanceCm, float RadiusCm, float Scale, float CoverMultiplier)
	{
		return Base * Falloff(Curve, DistanceCm, RadiusCm) * Scale * CoverMultiplier;
	}

	float CoverMultiplier(bool bPlunging, bool bWallBetween, bool bRoofAbove, const FBlastOcclusionConfig& Config)
	{
		if (bPlunging)
		{
			return bRoofAbove ? FMath::Clamp(Config.RoofMultiplier, 0.f, 1.f) : 1.f;
		}
		return bWallBetween ? FMath::Clamp(Config.WallMultiplier, 0.f, 1.f) : 1.f;
	}

	FBlastFalloff FromWeapon(const FWeaponExplosionStats& Stats)
	{
		FBlastFalloff Curve;
		Curve.Model = EBlastFalloff::FullCore;
		Curve.FullRadiusCm = Stats.FullDamageRadiusCm;
		Curve.EdgeMultiplier = Stats.EdgeDamageMultiplier;
		return Curve;
	}

	float BackblastMultiplier(const FVector& Launcher, const FVector& AimDir, const FVector& Point, float LengthCm, float HalfAngleDeg)
	{
		const FVector Back = -AimDir.GetSafeNormal();
		if (LengthCm <= 0.f || Back.IsNearlyZero())
		{
			return 0.f;
		}
		const FVector ToPoint = Point - Launcher;
		const float Along = static_cast<float>(FVector::DotProduct(ToPoint, Back));
		if (Along < 0.f || Along > LengthCm)
		{
			return 0.f;
		}
		const float Distance = static_cast<float>(ToPoint.Size());
		if (Distance > KINDA_SMALL_NUMBER)
		{
			const float CosAngle = Along / Distance;
			if (CosAngle < FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 0.f, 90.f))))
			{
				return 0.f;
			}
		}
		return 1.f - Along / LengthCm;
	}

	float CameraTrauma(float BaseTrauma, float DistanceCm, float RadiusCm)
	{
		const float Reach = FMath::Max(1.f, RadiusCm * 4.f);
		return BaseTrauma <= 0.f ? 0.f : BaseTrauma * FMath::Clamp(1.f - DistanceCm / Reach, 0.f, 1.f);
	}

	FExplosionConfig Defaults()
	{
		FExplosionConfig Config;
		Config.OcclusionByKind.Add(ProjectileKind(), true);
		return Config;
	}

	FExplosionConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors)
	{
		FExplosionConfig Out = Defaults();
		auto Number = [&Block, OutErrors](const TCHAR* Key, float& Value, float Min, float Max)
		{
			double Read = 0.0;
			if (!Block.TryGetNumberField(Key, Read))
			{
				return;
			}
			if (Read < Min || Read > Max)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("explosions.%s %.2f is outside %.2f .. %.2f - kept %.2f"), Key, Read, Min, Max, Value));
				}
				return;
			}
			Value = static_cast<float>(Read);
		};
		Number(TEXT("wall_multiplier"), Out.Occlusion.WallMultiplier, 0.f, 1.f);
		Number(TEXT("roof_multiplier"), Out.Occlusion.RoofMultiplier, 0.f, 1.f);
		Number(TEXT("camera_trauma"), Out.CameraTrauma, 0.f, 1.f);
		const TSharedPtr<FJsonObject>* Occlusion = nullptr;
		if (Block.TryGetObjectField(TEXT("occlusion"), Occlusion) && Occlusion)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Occlusion)->Values)
			{
				bool bOn = false;
				if (Entry.Value.IsValid() && Entry.Value->Type == EJson::Boolean && Entry.Value->TryGetBool(bOn))
				{
					Out.OcclusionByKind.Add(FName(*Entry.Key), bOn);
				}
				else if (OutErrors && !Entry.Key.StartsWith(TEXT("comment")))
				{
					OutErrors->Add(FString::Printf(TEXT("explosions.occlusion.%s is not true / false"), *Entry.Key));
				}
			}
		}
		return Out;
	}

	void Set(const FExplosionConfig& Config)
	{
		ExplosionLocal::Current() = Config;
	}

	const FExplosionConfig& Get()
	{
		return ExplosionLocal::Current();
	}
}
