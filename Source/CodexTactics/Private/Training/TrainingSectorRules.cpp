#include "Training/TrainingSectorRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"

namespace TrainingSectorParse
{
	const FJsonObject* ObjectField(const FJsonObject& Json, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Json.TryGetObjectField(Key, Value) && Value ? Value->Get() : nullptr;
	}

	bool ReadSectorNumber(const FJsonObject& Json, const TCHAR* Key, double& Out)
	{
		return Json.TryGetNumberField(Key, Out) && FMath::IsFinite(Out);
	}

	bool VectorField(const FJsonObject& Json, const TCHAR* Key, int32 Dimensions, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Json.TryGetArrayField(Key, Values) || !Values || Values->Num() != Dimensions)
		{
			return false;
		}
		Out = FVector::ZeroVector;
		for (int32 Axis = 0; Axis < Dimensions; ++Axis)
		{
			const TSharedPtr<FJsonValue>& Value = (*Values)[Axis];
			if (!Value || !Value->TryGetNumber(Out[Axis]) || !FMath::IsFinite(Out[Axis]))
			{
				return false;
			}
		}
		return true;
	}

	TOptional<int32> NumberId(const FJsonObject& Json, const TCHAR* Key)
	{
		double Id = 0.0;
		if (!ReadSectorNumber(Json, Key, Id) || Id < 0.0 || Id > 6.0 || Id != static_cast<int32>(Id))
		{
			return {};
		}
		return static_cast<int32>(Id);
	}

	bool ReadLocation(const FJsonObject& Json, const FTrainingSector& Sector, FTrainingSectorStart& Out)
	{
		return VectorField(Json, TEXT("pos"), 3, Out.Position)
			&& TrainingSectorRules::Contains(Sector.Area, Out.Position)
			&& FMath::IsWithinInclusive(Out.Position.Z, Sector.Heights.X, Sector.Heights.Y)
			&& ReadSectorNumber(Json, TEXT("yaw"), Out.Yaw);
	}

	const TArray<TSharedPtr<FJsonValue>>* CandidateList(const FJsonObject& Json, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		return Json.TryGetArrayField(Key, List) && List && FMath::IsWithinInclusive(List->Num(), 2, 3) ? List : nullptr;
	}

	bool ReadSector(const FJsonObject& Json, FTrainingSector& Out, FString& Error)
	{
		Out.Id = NumberId(Json, TEXT("id")).Get(0);
		const FJsonObject* Box = ObjectField(Json, TEXT("box_cm"));
		const FJsonObject* Z = ObjectField(Json, TEXT("z_cm"));
		FVector Min;
		FVector Max;
		if (!Out.Id || !Box || !Z || !VectorField(*Box, TEXT("min"), 2, Min) || !VectorField(*Box, TEXT("max"), 2, Max)
			|| !FMath::IsFinite(Max.X - Min.X) || !FMath::IsFinite(Max.Y - Min.Y)
			|| Max.X - Min.X < TrainingSectorRules::MinSizeCm || Max.Y - Min.Y < TrainingSectorRules::MinSizeCm
			|| !ReadSectorNumber(*Z, TEXT("min"), Out.Heights.X) || !ReadSectorNumber(*Z, TEXT("max"), Out.Heights.Y) || Out.Heights.X >= Out.Heights.Y)
		{
			Error = TEXT("expected an id 1–6, a box at least 60 m on each side and a height interval");
			return false;
		}
		Out.Area = FBox2D(FVector2D(Min.X, Min.Y), FVector2D(Max.X, Max.Y));
		const TArray<TSharedPtr<FJsonValue>>* Starts = CandidateList(Json, TEXT("starts"));
		const TArray<TSharedPtr<FJsonValue>>* Posts = CandidateList(Json, TEXT("pmc_posts"));
		const FJsonObject* Squad = ObjectField(Json, TEXT("pmc_squad"));
		if (!Starts || !Posts || !Squad)
		{
			Error = TEXT("expected two or three starts and posts, and a PMC squad");
			return false;
		}
		if (!PmcEncounterRules::ParseSquad(*Squad, Out.Squad, &Error))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Starts)
		{
			const TSharedPtr<FJsonObject>* Start = nullptr;
			FTrainingSectorStart Candidate;
			if (!Entry || !Entry->TryGetObject(Start) || !Start || !Start->IsValid() || !ReadLocation(**Start, Out, Candidate))
			{
				Error = TEXT("start requires pos and yaw within sector bounds");
				return false;
			}
			Out.Starts.Add(Candidate);
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Posts)
		{
			const TSharedPtr<FJsonObject>* Post = nullptr;
			FTrainingSectorStart Location;
			FPmcEncounterPost Candidate;
			if (!Entry || !Entry->TryGetObject(Post) || !Post || !Post->IsValid() || !ReadLocation(**Post, Out, Location)
				|| !(*Post)->TryGetStringField(TEXT("id"), Candidate.Id) || Candidate.Id.IsEmpty()
				|| !(*Post)->TryGetStringField(TEXT("kind"), Candidate.Kind) || Candidate.Kind.IsEmpty())
			{
				Error = TEXT("post requires id, kind, pos and yaw within sector bounds");
				return false;
			}
			Candidate.Pos = Location.Position;
			Candidate.YawDeg = static_cast<float>(FRotator::NormalizeAxis(Location.Yaw));
			Out.Posts.Add(MoveTemp(Candidate));
		}
		return true;
	}
}

TArray<FTrainingSector> TrainingSectorRules::ParseSectors(const FJsonObject& Json, TArray<FString>& Errors)
{
	using namespace TrainingSectorParse;
	FString Map;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Json.TryGetStringField(TEXT("map"), Map) || Map != MapName || !Json.TryGetArrayField(TEXT("sectors"), List) || !List)
	{
		Errors.Add(TEXT("expected the X6 map and a sectors array"));
		return {};
	}
	TArray<FTrainingSector> Result;
	for (int32 Index = 0; Index < List->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		const TSharedPtr<FJsonValue>& Value = (*List)[Index];
		FTrainingSector Sector;
		FString Error = TEXT("invalid sector object");
		if (!Value || !Value->TryGetObject(Entry) || !Entry || !Entry->IsValid() || !ReadSector(**Entry, Sector, Error))
		{
			Errors.Add(FString::Printf(TEXT("sectors[%d]: %s"), Index, *Error));
			continue;
		}
		if (Result.ContainsByPredicate([&Sector](const FTrainingSector& Seen) { return Seen.Id == Sector.Id; }))
		{
			Errors.Add(FString::Printf(TEXT("duplicate sector id %d"), Sector.Id));
			continue;
		}
		Result.Add(MoveTemp(Sector));
	}
	return Result;
}

TOptional<int32> TrainingSectorRules::ParseId(const FString& Text)
{
	if (Text.Len() == 1 && Text[0] >= '0' && Text[0] <= '6')
	{
		return static_cast<int32>(Text[0] - '0');
	}
	return {};
}

int32 TrainingSectorRules::SavedId(const FJsonObject& Json)
{
	FString Map;
	return Json.TryGetStringField(TEXT("map"), Map) && Map == MapName ? TrainingSectorParse::NumberId(Json, TEXT("sector")).Get(0) : 0;
}

FVector TrainingSectorRules::Clamp(const FBox2D& Box, const FVector& Point, double Inset)
{
	const FVector2D Limit = Box.GetExtent();
	const FVector2D Pad(FMath::Clamp(Inset, 0.0, Limit.X), FMath::Clamp(Inset, 0.0, Limit.Y));
	return FVector(FMath::Clamp(Point.X, Box.Min.X + Pad.X, Box.Max.X - Pad.X),
		FMath::Clamp(Point.Y, Box.Min.Y + Pad.Y, Box.Max.Y - Pad.Y), Point.Z);
}

bool TrainingSectorRules::Contains(const FBox2D& Box, const FVector& Point)
{
	return Point.X >= Box.Min.X && Point.X <= Box.Max.X && Point.Y >= Box.Min.Y && Point.Y <= Box.Max.Y;
}

void TrainingSectorRules::Pick(const FTrainingSector& Sector, int32 Seed, FTrainingSectorStart& Start, FPmcEncounterSpec& Squad)
{
	FRandomStream Stream(Seed);
	Start = Sector.Starts[Stream.RandRange(0, Sector.Starts.Num() - 1)];
	Squad = Sector.Squad;
	Squad.Post = Sector.Posts[Stream.RandRange(0, Sector.Posts.Num() - 1)];
	Squad.bHasPost = true;
	Squad.bRandomSpawn = false;
	Squad.FixedSpot = Squad.Post.Pos;
	Squad.DelaySeconds = 0.f;
	Squad.Seed = Seed != 0 ? Seed : 1;
}
