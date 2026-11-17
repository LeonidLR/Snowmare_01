#include "LevelGen/MissionGoalsRules.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const TCHAR* const MissionGoalsRules::Schema = TEXT("codex.mission_goals/1");

namespace MissionGoalsRulesLocal
{
	bool ReadGoalNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Number)
		{
			return false;
		}
		Out = Value->AsNumber();
		return FMath::IsFinite(Out);
	}

	/** [x, y] or [x, y, z] -> a vector (z 0 when absent). */
	bool ReadGoalVector(const TArray<TSharedPtr<FJsonValue>>& Array, FVector& Out)
	{
		if (Array.Num() < 2 || Array.Num() > 3)
		{
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!ReadGoalNumber(Array[0], X) || !ReadGoalNumber(Array[1], Y) || (Array.Num() == 3 && !ReadGoalNumber(Array[2], Z)))
		{
			return false;
		}
		Out = FVector(X, Y, Z);
		return true;
	}
}

EMissionGoalKind MissionGoalsRules::ParseKind(const FString& Kind)
{
	if (Kind.Equals(TEXT("room"), ESearchCase::IgnoreCase))
	{
		return EMissionGoalKind::Room;
	}
	if (Kind.Equals(TEXT("yard"), ESearchCase::IgnoreCase))
	{
		return EMissionGoalKind::Yard;
	}
	if (Kind.Equals(TEXT("street"), ESearchCase::IgnoreCase))
	{
		return EMissionGoalKind::Street;
	}
	return EMissionGoalKind::Other;
}

const TCHAR* MissionGoalsRules::KindToString(EMissionGoalKind Kind)
{
	switch (Kind)
	{
	case EMissionGoalKind::Room: return TEXT("room");
	case EMissionGoalKind::Yard: return TEXT("yard");
	case EMissionGoalKind::Street: return TEXT("street");
	default: return TEXT("other");
	}
}

bool MissionGoalsRules::Parse(const FString& Json, FMissionGoalsFile& Out, TArray<FString>& OutErrors)
{
	using namespace MissionGoalsRulesLocal;
	Out = FMissionGoalsFile();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("not a JSON object"));
		return false;
	}
	FString SchemaId;
	if (!Root->TryGetStringField(TEXT("schema"), SchemaId) || !SchemaId.StartsWith(TEXT("codex.mission_goals/")))
	{
		OutErrors.Add(FString::Printf(TEXT("schema: expected %s, got '%s'"), Schema, *SchemaId));
		return false;
	}
	if (SchemaId != Schema)
	{
		OutErrors.Add(FString::Printf(TEXT("schema %s is not supported (this build reads %s)"), *SchemaId, Schema));
		return false;
	}
	Root->TryGetStringField(TEXT("map"), Out.Map);
	Root->TryGetStringField(TEXT("layout"), Out.Layout);
	FString Order;
	if (Root->TryGetStringField(TEXT("order"), Order))
	{
		if (Order.Equals(TEXT("sequence"), ESearchCase::IgnoreCase))
		{
			Out.Order = EMissionGoalOrder::Sequence;
		}
		else if (!Order.Equals(TEXT("free"), ESearchCase::IgnoreCase))
		{
			OutErrors.Add(FString::Printf(TEXT("order '%s' unknown: free order used"), *Order));
		}
	}
	double Number = 0.0;
	if (Root->TryGetNumberField(TEXT("default_radius_cm"), Number) && FMath::IsFinite(Number) && Number > 0.0)
	{
		Out.DefaultRadiusCm = static_cast<float>(Number);
	}
	else
	{
		Out.DefaultRadiusCm = DefaultRadiusCm;
	}
	if (Root->TryGetNumberField(TEXT("floor_tolerance_cm"), Number) && FMath::IsFinite(Number) && Number > 0.0)
	{
		Out.FloorToleranceCm = static_cast<float>(Number);
	}
	if (Root->TryGetNumberField(TEXT("label_height_cm"), Number) && FMath::IsFinite(Number) && Number >= 0.0 && Number <= TNumericLimits<float>::Max())
	{
		Out.LabelHeightCm = static_cast<float>(Number);
	}
	if (Root->TryGetNumberField(TEXT("label_world_size_cm"), Number) && FMath::IsFinite(Number) && Number > 0.0 && Number <= TNumericLimits<float>::Max())
	{
		Out.LabelWorldSizeCm = static_cast<float>(Number);
	}
	else
	{
		Out.FloorToleranceCm = DefaultFloorToleranceCm;
	}

	const TArray<TSharedPtr<FJsonValue>>* Goals = nullptr;
	if (!Root->TryGetArrayField(TEXT("goals"), Goals) || !Goals)
	{
		OutErrors.Add(TEXT("goals: expected an array"));
		return false;
	}
	TSet<FString> Ids;
	for (int32 Index = 0; Index < Goals->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Item = (*Goals)[Index].IsValid() ? (*Goals)[Index]->AsObject() : nullptr;
		if (!Item.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("goals[%d]: expected an object"), Index));
			continue;
		}
		FMissionGoal Goal;
		if (!Item->TryGetStringField(TEXT("id"), Goal.Id) || Goal.Id.IsEmpty() || Ids.Contains(Goal.Id))
		{
			OutErrors.Add(FString::Printf(TEXT("goals[%d]: id must be a unique non-empty string"), Index));
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
		if (!Item->TryGetArrayField(TEXT("pos"), Pos) || !Pos || !ReadGoalVector(*Pos, Goal.Location))
		{
			OutErrors.Add(FString::Printf(TEXT("goal %s: pos must be [x, y, z]"), *Goal.Id));
			continue;
		}
		FString Kind;
		Item->TryGetStringField(TEXT("kind"), Kind);
		Goal.Kind = ParseKind(Kind);
		if (!Item->TryGetStringField(TEXT("layer"), Goal.Layer) || Goal.Layer.IsEmpty())
		{
			Goal.Layer = TEXT("ground");
		}
		Item->TryGetStringField(TEXT("building"), Goal.Building);
		Item->TryGetStringField(TEXT("zone"), Goal.Zone);
		Goal.RadiusCm = Item->TryGetNumberField(TEXT("radius_cm"), Number) && FMath::IsFinite(Number) && Number > 0.0
			? static_cast<float>(Number) : Out.DefaultRadiusCm;
		const TArray<TSharedPtr<FJsonValue>>* Area = nullptr;
		if (Item->TryGetArrayField(TEXT("area"), Area) && Area)
		{
			for (const TSharedPtr<FJsonValue>& Corner : *Area)
			{
				const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
				FVector Point;
				if (Corner.IsValid() && Corner->TryGetArray(XY) && XY && ReadGoalVector(*XY, Point))
				{
					Goal.Area.Add(FVector2D(Point.X, Point.Y));
				}
			}
			if (Goal.Area.Num() < 3)
			{
				OutErrors.Add(FString::Printf(TEXT("goal %s: area needs >= 3 corners - the radius is used"), *Goal.Id));
				Goal.Area.Reset();
			}
		}
		Ids.Add(Goal.Id);
		Out.Goals.Add(MoveTemp(Goal));
	}
	if (Out.Goals.IsEmpty())
	{
		OutErrors.Add(TEXT("goals: no valid goal"));
		return false;
	}
	return true;
}

bool MissionGoalsRules::IsInsidePolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon)
{
	const int32 Count = Polygon.Num();
	if (Count < 3)
	{
		return false;
	}
	bool bInside = false;
	for (int32 I = 0, J = Count - 1; I < Count; J = I++)
	{
		const FVector2D& A = Polygon[I];
		const FVector2D& B = Polygon[J];
		if ((A.Y > Point.Y) != (B.Y > Point.Y))
		{
			const double CrossX = (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X;
			if (Point.X < CrossX)
			{
				bInside = !bInside;
			}
		}
	}
	return bInside;
}

bool MissionGoalsRules::IsAtGoal(const FMissionGoal& Goal, const FVector& FeetLocation, float FloorToleranceCm)
{
	if (FMath::Abs(FeetLocation.Z - Goal.Location.Z) > FMath::Max(0.f, FloorToleranceCm))
	{
		return false;
	}
	const FVector2D Feet(FeetLocation.X, FeetLocation.Y);
	if (Goal.Kind == EMissionGoalKind::Room && Goal.Area.Num() >= 3)
	{
		return IsInsidePolygon(Feet, Goal.Area);
	}
	return FVector2D::DistSquared(Feet, FVector2D(Goal.Location.X, Goal.Location.Y)) <= FMath::Square(FMath::Max(0.f, Goal.RadiusCm));
}

bool MissionGoalsRules::CanCount(EMissionGoalOrder Order, TConstArrayView<bool> Reached, int32 GoalIndex)
{
	if (!Reached.IsValidIndex(GoalIndex) || Reached[GoalIndex])
	{
		return false;
	}
	if (Order == EMissionGoalOrder::Free)
	{
		return true;
	}
	for (int32 Index = 0; Index < GoalIndex; ++Index)
	{
		if (!Reached[Index])
		{
			return false;
		}
	}
	return true;
}

TArray<FMissionGoalHit> MissionGoalsRules::UpdateReached(const FMissionGoalsFile& File, TArray<bool>& Reached, TConstArrayView<FVector> OperativeFeet)
{
	TArray<FMissionGoalHit> Hits;
	Reached.SetNum(File.Goals.Num());
	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (int32 GoalIndex = 0; GoalIndex < File.Goals.Num(); ++GoalIndex)
		{
			if (!CanCount(File.Order, Reached, GoalIndex))
			{
				continue;
			}
			for (int32 Operative = 0; Operative < OperativeFeet.Num(); ++Operative)
			{
				if (IsAtGoal(File.Goals[GoalIndex], OperativeFeet[Operative], File.FloorToleranceCm))
				{
					Reached[GoalIndex] = true;
					Hits.Add({ GoalIndex, Operative });
					bChanged = true;
					break;
				}
			}
		}
	}
	return Hits;
}

int32 MissionGoalsRules::CountReached(TConstArrayView<bool> Reached)
{
	int32 Count = 0;
	for (const bool bReached : Reached)
	{
		Count += bReached ? 1 : 0;
	}
	return Count;
}

bool MissionGoalsRules::AreAllReached(TConstArrayView<bool> Reached)
{
	return !Reached.IsEmpty() && CountReached(Reached) == Reached.Num();
}

FString MissionGoalsRules::FormatHudLine(int32 ReachedCount, int32 Total)
{
	return FString::Printf(TEXT("GOALS: %d/%d"), FMath::Clamp(ReachedCount, 0, FMath::Max(0, Total)), FMath::Max(0, Total));
}

FString MissionGoalsRules::GetDefaultPathForMap(const FString& MapName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Data/LevelGen/objectives") / (MapName + TEXT(".objectives.json")));
}
