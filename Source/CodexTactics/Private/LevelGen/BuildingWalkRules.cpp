#include "LevelGen/BuildingWalkRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace BuildingWalkRulesLocal
{
	using FJsonValues = TArray<TSharedPtr<FJsonValue>>;

	const FJsonValues& Array(const FJsonObject& Object, const TCHAR* Field)
	{
		static const FJsonValues Empty;
		const FJsonValues* Values = nullptr;
		return Object.TryGetArrayField(Field, Values) ? *Values : Empty;
	}

	void IndexOpenings(const FJsonValues& Openings, TMap<FString, TSharedPtr<FJsonObject>>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Openings)
		{
			const TSharedPtr<FJsonObject> Opening = Value.IsValid() ? Value->AsObject() : nullptr;
			FString Id;
			if (Opening.IsValid() && Opening->TryGetStringField(TEXT("id"), Id) && !Out.Contains(Id))
			{
				Out.Add(Id, Opening);
			}
		}
	}
}

void FBuildingWalkProgress::Reset(double Now, const FVector& Position)
{
	Mark = Position;
	MarkTime = Now;
	bStarted = true;
}

bool FBuildingWalkProgress::IsStuck(double Now, const FVector& Position, double MoveCm, double Seconds)
{
	if (!bStarted || FVector::Dist(Position, Mark) >= MoveCm)
	{
		Reset(Now, Position);
		return false;
	}
	return Now - MarkTime >= Seconds;
}

FBuildingFlatEntries BuildingWalkRules::ScanFlatEntries(const FJsonObject& Building)
{
	using namespace BuildingWalkRulesLocal;
	FBuildingFlatEntries Out;
	TMap<FString, TSharedPtr<FJsonObject>> Openings;
	for (const TSharedPtr<FJsonValue>& Value : Array(Building, TEXT("partitions")))
	{
		if (const TSharedPtr<FJsonObject> Partition = Value.IsValid() ? Value->AsObject() : nullptr)
		{
			IndexOpenings(Array(*Partition, TEXT("openings")), Openings);
		}
	}
	IndexOpenings(Array(Building, TEXT("openings")), Openings);

	TSet<FString> Listed;
	for (const TSharedPtr<FJsonValue>& Value : Array(Building, TEXT("units")))
	{
		const TSharedPtr<FJsonObject> Unit = Value.IsValid() ? Value->AsObject() : nullptr;
		FString Kind;
		if (!Unit.IsValid() || !Unit->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("flat"))
		{
			continue;
		}
		const FString UnitId = Unit->GetStringField(TEXT("id"));
		TSet<FString> Rooms;
		for (const TSharedPtr<FJsonValue>& Room : Array(*Unit, TEXT("rooms")))
		{
			Rooms.Add(Room->AsString());
		}
		for (const TSharedPtr<FJsonValue>& EntryValue : Array(*Unit, TEXT("entries")))
		{
			const FString EntryId = EntryValue->AsString();
			Listed.Add(EntryId);
			const TSharedPtr<FJsonObject>* Opening = Openings.Find(EntryId);
			if (!Opening)
			{
				Out.Unresolved.Add(EntryId);
				continue;
			}
			TArray<FString> Inside, Outside;
			for (const TSharedPtr<FJsonValue>& Link : Array(**Opening, TEXT("links")))
			{
				(Rooms.Contains(Link->AsString()) ? Inside : Outside).Add(Link->AsString());
			}
			if (Inside.Num() != 1 || Outside.Num() != 1)
			{
				Out.Unresolved.Add(EntryId);
				continue;
			}
			FBuildingFlatEntry& Entry = Out.Entries.AddDefaulted_GetRef();
			Entry.Entry = EntryId;
			Entry.Unit = UnitId;
			Entry.FromRoom = Outside[0];
			Entry.ToRoom = Inside[0];
			(*Opening)->TryGetStringField(TEXT("role"), Entry.Role);
			double Floor = 0.0;
			if ((*Opening)->TryGetNumberField(TEXT("floor"), Floor) || Unit->TryGetNumberField(TEXT("floor"), Floor))
			{
				Entry.Floor = FMath::RoundToInt(Floor);
			}
			if (Entry.Role != TEXT("unit_entry"))
			{
				Out.EntriesWithoutRole.Add(EntryId);
			}
		}
	}
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : Openings)
	{
		FString Role;
		if (Pair.Value->TryGetStringField(TEXT("role"), Role) && Role == TEXT("unit_entry") && !Listed.Contains(Pair.Key))
		{
			Out.RoleNotAnEntry.Add(Pair.Key);
		}
	}
	Out.RoleNotAnEntry.Sort();
	return Out;
}

bool BuildingWalkRules::IsArrived(const FVector& Feet, const FVector& Goal, double RadiusCm, double ZToleranceCm)
{
	return FVector::Dist2D(Feet, Goal) <= RadiusCm && FMath::Abs(Feet.Z - Goal.Z) <= ZToleranceCm;
}

bool BuildingWalkRules::SquadWithinRatio(double SquadSeconds, double SoloSeconds, double MaxRatio)
{
	return SoloSeconds > 0.0 && SquadSeconds >= 0.0 && SquadSeconds <= MaxRatio * SoloSeconds;
}

bool BuildingWalkRules::PassesShare(int32 Passed, int32 Checked, double MinPass)
{
	return Checked > 0 && Passed >= 0 && static_cast<double>(Passed) + UE_KINDA_SMALL_NUMBER >= MinPass * static_cast<double>(Checked);
}

double BuildingWalkRules::LegLimitSeconds(double PathCm)
{
	return 20.0 + FMath::Max(0.0, PathCm) / 100.0;
}
