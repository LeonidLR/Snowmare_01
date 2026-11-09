#include "Combat/MortarRules.h"

#include "Data/WeaponStatsRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MortarRules
{
	namespace MortarLocal
	{
		FMortarNestConfig& Current()
		{
			static FMortarNestConfig Config = Defaults();
			return Config;
		}

		void ReadMortarNumber(const FJsonObject& Block, const TCHAR* Prefix, const TCHAR* Key, float& Value, float Scale, float Min, float Max, TArray<FString>* OutErrors)
		{
			double Read = 0.0;
			if (!Block.TryGetNumberField(Key, Read))
			{
				return;
			}
			const double Scaled = Read * Scale;
			if (Scaled < Min || Scaled > Max)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("%s%s %.2f is out of range - kept %.2f"), Prefix, Key, Read, Value / Scale));
				}
				return;
			}
			Value = static_cast<float>(Scaled);
		}

		void ReadMortarInteger(const FJsonObject& Block, const TCHAR* Key, int32& Value, int32 Min, int32 Max, TArray<FString>* OutErrors)
		{
			double Read = 0.0;
			if (!Block.TryGetNumberField(Key, Read))
			{
				return;
			}
			const int32 Rounded = FMath::RoundToInt(Read);
			if (Rounded < Min || Rounded > Max)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("mortar_nest.%s %d is out of range - kept %d"), Key, Rounded, Value));
				}
				return;
			}
			Value = Rounded;
		}
	}

	EMortarRefusal Check(int32 CrewAtNest, int32 Ammo, float HorizontalCm, bool bSpotted, const FMortarNestConfig& Config)
	{
		if (CrewAtNest < Config.CrewNeeded)
		{
			return EMortarRefusal::Crew;
		}
		if (Ammo <= 0)
		{
			return EMortarRefusal::NoAmmo;
		}
		if (HorizontalCm < Config.MinRangeCm)
		{
			return EMortarRefusal::TooClose;
		}
		if (Config.MaxRangeCm > 0.f && HorizontalCm > Config.MaxRangeCm)
		{
			return EMortarRefusal::TooFar;
		}
		return bSpotted ? EMortarRefusal::None : EMortarRefusal::NotSpotted;
	}

	FString RefusalText(EMortarRefusal Refusal)
	{
		switch (Refusal)
		{
		case EMortarRefusal::Crew: return TEXT("Mortar: needs a crew of three at the nest");
		case EMortarRefusal::NoAmmo: return TEXT("Mortar: no rounds left");
		case EMortarRefusal::TooClose: return TEXT("Mortar: too close - inside the minimum range");
		case EMortarRefusal::TooFar: return TEXT("Mortar: out of range");
		case EMortarRefusal::NotSpotted: return TEXT("Mortar: nobody sees that spot - no spotter, no fire");
		default: return FString();
		}
	}

	float BracketErrorCm(int32 RoundOnTarget, const FMortarBracketing& Bracketing)
	{
		const float Walked = Bracketing.FirstErrorCm * FMath::Pow(FMath::Clamp(Bracketing.Shrink, 0.f, 1.f), static_cast<float>(FMath::Max(0, RoundOnTarget)));
		return FMath::Max(Walked, FMath::Min(Bracketing.MinErrorCm, Bracketing.FirstErrorCm));
	}

	FVector BracketPoint(const FVector& Target, float ErrorCm, float AngleRoll01)
	{
		const float Angle = FMath::Clamp(AngleRoll01, 0.f, 1.f) * 2.f * UE_PI;
		return Target + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * FMath::Max(0.f, ErrorCm);
	}

	bool IsSameTarget(const FVector& Last, const FVector& New, const FMortarBracketing& Bracketing)
	{
		return FVector::Dist2D(Last, New) <= Bracketing.SameTargetCm;
	}

	FVector CrewPlace(const FVector& Centre, float YawDeg, int32 Index, const FMortarNestConfig& Config)
	{
		const int32 Count = FMath::Max(1, Config.CrewNeeded);
		// Behind and beside the tube (the round leaves forward): spread over the back half-circle.
		const float Spread = Count > 1 ? 180.f / (Count - 1) : 0.f;
		const float Angle = YawDeg + 90.f + Spread * FMath::Clamp(Index, 0, Count - 1);
		const float Radians = FMath::DegreesToRadians(Angle);
		return Centre + FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.f) * Config.CrewRadiusCm;
	}

	bool BestCluster(const TArray<FVector>& Points, float RadiusCm, const FVector& Nest, FVector& OutCentre, int32& OutCount)
	{
		OutCount = 0;
		int32 Best = INDEX_NONE;
		float BestNestDistance = 0.f;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			int32 Count = 0;
			for (const FVector& Other : Points)
			{
				Count += FVector::Dist2D(Points[Index], Other) <= RadiusCm ? 1 : 0;
			}
			const float NestDistance = static_cast<float>(FVector::Dist2D(Points[Index], Nest));
			if (Count > OutCount || (Count == OutCount && NestDistance < BestNestDistance))
			{
				OutCount = Count;
				Best = Index;
				BestNestDistance = NestDistance;
			}
		}
		if (Best == INDEX_NONE)
		{
			return false;
		}
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Other : Points)
		{
			Sum += FVector::Dist2D(Points[Best], Other) <= RadiusCm ? Other : FVector::ZeroVector;
		}
		OutCentre = Sum / OutCount;
		return true;
	}

	FMortarNestConfig Defaults()
	{
		FMortarNestConfig Config;
		FWeaponStats& Round = Config.Round;
		Round.Kind = EWeaponKind::Projectile;
		Round.Damage.BaseDamage = 110.f;
		Round.Damage.DamageType = EDamageType::Explosive;
		Round.Damage.ArmorPenetration = 0.6f;
		Round.Damage.PenetrationClass = EArmorClass::Vehicle;
		Round.Projectile.SpeedCmS = 11000.f;
		Round.Projectile.GravityScale = 4.f;
		Round.Projectile.Trajectory = EProjectileTrajectory::HighAngle;
		Round.Projectile.ArmingDistanceCm = 1000.f;
		Round.Projectile.MaxFlightSeconds = 15.f;
		Round.Explosion.RadiusCm = 600.f;
		Round.Explosion.FullDamageRadiusCm = 250.f;
		Round.Explosion.EdgeDamageMultiplier = 0.2f;
		return Config;
	}

	FMortarNestConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors)
	{
		using namespace MortarLocal;
		FMortarNestConfig Out = Defaults();
		const TCHAR* P = TEXT("mortar_nest.");
		ReadMortarInteger(Block, TEXT("crew_needed"), Out.CrewNeeded, 1, 6, OutErrors);
		ReadMortarNumber(Block, P, TEXT("crew_radius_m"), Out.CrewRadiusCm, 100.f, 50.f, 1000.f, OutErrors);
		ReadMortarNumber(Block, P, TEXT("crew_leave_m"), Out.CrewLeaveCm, 100.f, 50.f, 2000.f, OutErrors);
		ReadMortarNumber(Block, P, TEXT("min_range_m"), Out.MinRangeCm, 100.f, 0.f, 100000.f, OutErrors);
		ReadMortarNumber(Block, P, TEXT("max_range_m"), Out.MaxRangeCm, 100.f, 0.f, 500000.f, OutErrors);
		ReadMortarNumber(Block, P, TEXT("interval_s"), Out.IntervalSeconds, 1.f, 0.2f, 60.f, OutErrors);
		ReadMortarInteger(Block, TEXT("rounds_per_order"), Out.RoundsPerOrder, 1, 100, OutErrors);
		ReadMortarInteger(Block, TEXT("ammo"), Out.Ammo, 0, 1000, OutErrors);
		ReadMortarNumber(Block, P, TEXT("spot_height_m"), Out.SpotHeightCm, 100.f, 0.f, 500.f, OutErrors);
		if (Out.MaxRangeCm > 0.f && Out.MaxRangeCm < Out.MinRangeCm)
		{
			if (OutErrors)
			{
				OutErrors->Add(TEXT("mortar_nest.max_range_m is below min_range_m - the defaults kept"));
			}
			Out.MinRangeCm = Defaults().MinRangeCm;
			Out.MaxRangeCm = Defaults().MaxRangeCm;
		}
		const TSharedPtr<FJsonObject>* Bracket = nullptr;
		if (Block.TryGetObjectField(TEXT("bracketing"), Bracket) && Bracket)
		{
			const TCHAR* B = TEXT("mortar_nest.bracketing.");
			ReadMortarNumber(**Bracket, B, TEXT("first_error_m"), Out.Bracketing.FirstErrorCm, 100.f, 0.f, 10000.f, OutErrors);
			ReadMortarNumber(**Bracket, B, TEXT("shrink"), Out.Bracketing.Shrink, 1.f, 0.f, 1.f, OutErrors);
			ReadMortarNumber(**Bracket, B, TEXT("min_error_m"), Out.Bracketing.MinErrorCm, 100.f, 0.f, 10000.f, OutErrors);
			ReadMortarNumber(**Bracket, B, TEXT("same_target_m"), Out.Bracketing.SameTargetCm, 100.f, 0.f, 100000.f, OutErrors);
		}
		const TSharedPtr<FJsonObject>* RoundBlock = nullptr;
		if (Block.TryGetObjectField(TEXT("round"), RoundBlock) && RoundBlock)
		{
			FWeaponStats Round = Out.Round;
			TArray<FString> Errors;
			TArray<FString> Unknown;
			const bool bRead = WeaponStatsRules::ApplyJson(**RoundBlock, Round, &Unknown, &Errors);
			WeaponStatsRules::Validate(Round, Errors, TEXT("mortar_nest.round"));
			if (Round.Kind != EWeaponKind::Projectile || !Round.Projectile.IsHighAngle())
			{
				Errors.Add(TEXT("mortar_nest.round must be kind projectile with trajectory high_angle"));
			}
			for (const FString& Key : Unknown)
			{
				Errors.Add(FString::Printf(TEXT("mortar_nest.round: unknown key \"%s\""), *Key));
			}
			if (bRead && Errors.IsEmpty())
			{
				Out.Round = Round;
			}
			else if (OutErrors)
			{
				OutErrors->Append(Errors);
				OutErrors->Add(TEXT("mortar_nest.round rejected - the default round kept"));
			}
		}
		return Out;
	}

	void Set(const FMortarNestConfig& Config)
	{
		MortarLocal::Current() = Config;
	}

	const FMortarNestConfig& Get()
	{
		return MortarLocal::Current();
	}

	TArray<FMortarNestPlacement> ParsePlacements(const FJsonObject& LevelRoot, TArray<FString>* OutErrors)
	{
		TArray<FMortarNestPlacement> Out;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!LevelRoot.TryGetArrayField(TEXT("mortar_nests"), Entries) || !Entries)
		{
			return Out;
		}
		for (int32 Index = 0; Index < Entries->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!(*Entries)[Index].IsValid() || !(*Entries)[Index]->TryGetObject(Entry) || !Entry)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("mortar_nests[%d] is not an object"), Index));
				}
				continue;
			}
			FMortarNestPlacement Nest;
			Nest.Id = (*Entry)->HasField(TEXT("id")) ? (*Entry)->GetStringField(TEXT("id")) : FString::Printf(TEXT("MORTAR_%d"), Index);
			const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
			if (!(*Entry)->TryGetArrayField(TEXT("pos"), Pos) || !Pos || Pos->Num() < 2)
			{
				if (OutErrors)
				{
					OutErrors->Add(FString::Printf(TEXT("mortar_nests[%d] (%s) needs \"pos\": [x, y(, z)] in cm"), Index, *Nest.Id));
				}
				continue;
			}
			Nest.Location = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), Pos->Num() > 2 ? (*Pos)[2]->AsNumber() : 0.0);
			Nest.bHasZ = Pos->Num() > 2;
			double Read = 0.0;
			Nest.YawDeg = (*Entry)->TryGetNumberField(TEXT("yaw"), Read) ? static_cast<float>(Read) : 0.f;
			FString Side;
			if ((*Entry)->TryGetStringField(TEXT("side"), Side))
			{
				Nest.TeamId = Side.Equals(TEXT("squad"), ESearchCase::IgnoreCase) ? 0 : -1;
			}
			if ((*Entry)->TryGetNumberField(TEXT("team"), Read))
			{
				Nest.TeamId = FMath::Max(0, FMath::RoundToInt(Read));
			}
			Nest.Crew = (*Entry)->TryGetNumberField(TEXT("crew"), Read) ? FMath::Clamp(FMath::RoundToInt(Read), 0, 6) : 0;
			Nest.Ammo = (*Entry)->TryGetNumberField(TEXT("ammo"), Read) ? FMath::Max(0, FMath::RoundToInt(Read)) : -1;
			if (Nest.TeamId == 0 && Nest.Crew > 0 && OutErrors)
			{
				OutErrors->Add(FString::Printf(TEXT("mortar_nests %s: a squad nest takes its crew from the squad (\"crew\" ignored)"), *Nest.Id));
			}
			Out.Add(Nest);
		}
		return Out;
	}
}
