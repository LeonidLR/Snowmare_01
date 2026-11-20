#include "Survival/ColdShelterRules.h"

#include "Dom/JsonObject.h"

namespace ColdShelterRules
{
	namespace
	{
		bool IsOneOf(const FString& Text, std::initializer_list<const TCHAR*> Names)
		{
			for (const TCHAR* Name : Names)
			{
				if (Text == Name)
				{
					return true;
				}
			}
			return false;
		}

		void ReadFloat(const FJsonObject& Json, const TCHAR* Key, float& InOut, float Scale = 1.f)
		{
			double Value = 0.0;
			if (Json.TryGetNumberField(Key, Value))
			{
				InOut = static_cast<float>(Value) * Scale;
			}
		}
	}

	FColdShelterConfig ConfigFromJson(const FJsonObject& Json, const FColdShelterConfig& Base)
	{
		FColdShelterConfig Config = Base;
		ReadFloat(Json, TEXT("roof_margin_cm"), Config.RoofMarginCm);
		ReadFloat(Json, TEXT("ground_margin_cm"), Config.GroundMarginCm);
		ReadFloat(Json, TEXT("storey_tolerance_cm"), Config.StoreyToleranceCm);
		ReadFloat(Json, TEXT("leak_to_cold"), Config.LeakToCold);
		ReadFloat(Json, TEXT("draught_full_m"), Config.DraughtFullCm, 100.f);
		ReadFloat(Json, TEXT("draught_fade_m"), Config.DraughtFadeCm, 100.f);
		const TSharedPtr<FJsonObject>* Weights = nullptr;
		if (Json.TryGetObjectField(TEXT("weights"), Weights) && Weights && Weights->IsValid())
		{
			ReadFloat(**Weights, TEXT("door"), Config.WeightDoor);
			ReadFloat(**Weights, TEXT("window"), Config.WeightWindow);
			ReadFloat(**Weights, TEXT("loophole"), Config.WeightLoophole);
			ReadFloat(**Weights, TEXT("breach"), Config.WeightBreach);
		}
		Config.DraughtFullCm = FMath::Max(0.f, Config.DraughtFullCm);
		Config.DraughtFadeCm = FMath::Max(Config.DraughtFullCm, Config.DraughtFadeCm);
		return Config;
	}

	bool IsStateOpen(const FString& State)
	{
		return IsOneOf(State, { TEXT("open"), TEXT("opened"), TEXT("ajar"), TEXT("broken"), TEXT("smashed"), TEXT("kicked"),
			TEXT("breached"), TEXT("destroyed"), TEXT("blown"), TEXT("missing") });
	}

	float GetOpeningWeight(const FPmcLayoutOpening& Opening, const FColdShelterConfig& Config)
	{
		if (Opening.Kind == TEXT("door"))
		{
			return FMath::Max(0.f, Config.WeightDoor);
		}
		if (Opening.Kind == TEXT("loophole"))
		{
			return FMath::Max(0.f, Config.WeightLoophole);
		}
		if (Opening.Kind == TEXT("breach") || Opening.Kind == TEXT("roof_hole"))
		{
			return FMath::Max(0.f, Config.WeightBreach);
		}
		return FMath::Max(0.f, Config.WeightWindow);
	}

	bool IsOpeningInitiallyOpen(const FPmcLayoutOpening& Opening)
	{
		// Designer decision 2026-11-19: a doorway gap with no leaf (the generator writes no state for doors) is OPEN, with a draught.
		if (Opening.Kind == TEXT("door") && Opening.State.IsEmpty())
		{
			return true;
		}
		return IsStateOpen(Opening.State);
	}

	TArray<bool> InitialOpenFlags(const FPmcLayoutBuilding& Building)
	{
		TArray<bool> Flags;
		Flags.Reserve(Building.Openings.Num());
		for (const FPmcLayoutOpening& Opening : Building.Openings)
		{
			Flags.Add(IsOpeningInitiallyOpen(Opening));
		}
		return Flags;
	}

	float ComputeLeak(const FPmcLayoutBuilding& Building, TConstArrayView<bool> OpenFlags, const FColdShelterConfig& Config)
	{
		float All = 0.f;
		float Open = 0.f;
		for (int32 Index = 0; Index < Building.Openings.Num(); ++Index)
		{
			const float Weight = GetOpeningWeight(Building.Openings[Index], Config);
			All += Weight;
			if (OpenFlags.IsValidIndex(Index) && OpenFlags[Index])
			{
				Open += Weight;
			}
		}
		return All > KINDA_SMALL_NUMBER ? FMath::Clamp(Open / All, 0.f, 1.f) : 0.f;
	}

	float GetBaseMultiplier(float Leak, const FColdShelterConfig& Config)
	{
		return FMath::Max(0.f, Leak * Config.LeakToCold);
	}

	float GetDraughtMultiplier(float DistanceCm, float Base, const FColdShelterConfig& Config)
	{
		if (DistanceCm <= Config.DraughtFullCm)
		{
			return FMath::Max(1.f, Base);
		}
		if (DistanceCm >= Config.DraughtFadeCm)
		{
			return Base;
		}
		const float Alpha = (DistanceCm - Config.DraughtFullCm) / (Config.DraughtFadeCm - Config.DraughtFullCm);
		return FMath::Max(Base, FMath::Lerp(1.f, Base, Alpha));
	}

	int32 GetStoreyIndex(const FPmcLayoutAccess& Layout, float FeetZ, const FColdShelterConfig& Config)
	{
		const float Height = FMath::Max(Layout.LevelHeightCm, 1.f);
		return FMath::Max(0, FMath::FloorToInt((FeetZ - Layout.GroundZ + Config.StoreyToleranceCm) / Height));
	}

	bool IsInside(const FPmcLayoutAccess& Layout, const FPmcLayoutBuilding& Building, const FVector& Feet, const FColdShelterConfig& Config)
	{
		return Feet.Z >= Layout.GroundZ - Config.GroundMarginCm && Feet.Z < PmcDefenceRules::RoofZ(Layout, Building) - Config.RoofMarginCm;
	}

	FColdShelterSample Sample(const FPmcLayoutAccess& Layout, const FVector& Feet, const FColdShelterConfig& Config,
		TFunctionRef<TConstArrayView<bool>(int32 BuildingIndex)> OpenFlagsOf, TFunctionRef<float(int32 BuildingIndex)> LeakOf)
	{
		FColdShelterSample Result;
		const int32 Index = PmcDefenceRules::BuildingAt(Layout, Feet);
		if (!Layout.Buildings.IsValidIndex(Index) || !IsInside(Layout, Layout.Buildings[Index], Feet, Config))
		{
			return Result;
		}
		const FPmcLayoutBuilding& Building = Layout.Buildings[Index];
		Result.Building = Index;
		Result.Leak = LeakOf(Index);
		Result.Base = GetBaseMultiplier(Result.Leak, Config);
		Result.Multiplier = Result.Base;
		if (Result.Leak <= 0.f)
		{
			return Result; // sealed: no open opening, no draught
		}
		const TConstArrayView<bool> Flags = OpenFlagsOf(Index);
		const int32 Storey = GetStoreyIndex(Layout, Feet.Z, Config);
		float Nearest = -1.f;
		for (int32 OpeningIndex = 0; OpeningIndex < Building.Openings.Num(); ++OpeningIndex)
		{
			const FPmcLayoutOpening& Opening = Building.Openings[OpeningIndex];
			if (!Flags.IsValidIndex(OpeningIndex) || !Flags[OpeningIndex] || !Opening.bHasPos || Opening.Floor != Storey)
			{
				continue;
			}
			const float Distance = FVector2D::Distance(Opening.Pos, FVector2D(Feet.X, Feet.Y));
			if (Nearest < 0.f || Distance < Nearest)
			{
				Nearest = Distance;
			}
		}
		Result.NearestOpenCm = Nearest;
		if (Nearest >= 0.f)
		{
			Result.Multiplier = FMath::Max(Result.Base, GetDraughtMultiplier(Nearest, Result.Base, Config));
		}
		return Result;
	}

	FColdShelterSample SampleLayout(const FPmcLayoutAccess& Layout, const FVector& Feet, const FColdShelterConfig& Config)
	{
		TArray<TArray<bool>> Flags;
		TArray<float> Leaks;
		Flags.Reserve(Layout.Buildings.Num());
		for (const FPmcLayoutBuilding& Building : Layout.Buildings)
		{
			Flags.Add(InitialOpenFlags(Building));
			Leaks.Add(ComputeLeak(Building, Flags.Last(), Config));
		}
		return Sample(Layout, Feet, Config,
			[&Flags](int32 Index) { return TConstArrayView<bool>(Flags[Index]); },
			[&Leaks](int32 Index) { return Leaks[Index]; });
	}

	float ResolveZoneMultiplier(bool bInCameraZone, float CameraZoneMultiplier, const FColdShelterSample& Shelter, float WaveMultiplier)
	{
		if (bInCameraZone)
		{
			return CameraZoneMultiplier;
		}
		// Indoors: a share of the outdoor cold (G012) - by an open door exactly the street's multiplier, a sealed building 0.
		return Shelter.IsIndoors() ? Shelter.Multiplier * FMath::Max(0.f, WaveMultiplier) : WaveMultiplier;
	}
}
