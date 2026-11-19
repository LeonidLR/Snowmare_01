#include "AI/PmcArenaScriptRules.h"

#include "Dom/JsonObject.h"

namespace PmcArenaScriptRules
{
	bool ParseMode(const FString& Text, EMode& Out)
	{
		if (Text.Equals(TEXT("run"), ESearchCase::IgnoreCase))
		{
			Out = EMode::Run;
			return true;
		}
		if (Text.Equals(TEXT("bound"), ESearchCase::IgnoreCase))
		{
			Out = EMode::Bound;
			return true;
		}
		return false;
	}

	const TCHAR* ModeName(EMode Mode)
	{
		return Mode == EMode::Bound ? TEXT("bound") : TEXT("run");
	}

	EMode ResolveMode(EMode Requested, bool& bFellBack)
	{
		// T-07 fills Bound; until then it plays as Run.
		bFellBack = Requested == EMode::Bound;
		return EMode::Run;
	}

	void ReadJson(const FJsonObject& Arena, FConfig& Out)
	{
		auto Read = [&Arena](const TCHAR* Key, float& Value, float Min, float Max)
		{
			double Number = 0.0;
			if (Arena.TryGetNumberField(Key, Number))
			{
				Value = FMath::Clamp(static_cast<float>(Number) * 100.f, Min, Max);
			}
		};
		Read(TEXT("script_waypoint_m"), Out.WaypointCm, 100.f, 5000.f);
		Read(TEXT("script_arrive_m"), Out.ArriveCm, 50.f, 1000.f);
		Read(TEXT("script_level_m"), Out.LevelCm, 0.f, 1000.f);
		bool bSprint = false;
		if (Arena.TryGetBoolField(TEXT("script_sprint"), bSprint))
		{
			Out.bSprint = bSprint;
		}
	}

	float RouteLength(const TArray<FVector>& Route)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Route.Num(); ++Index)
		{
			Length += FVector::Dist(Route[Index - 1], Route[Index]);
		}
		return static_cast<float>(Length);
	}

	TArray<FVector> WaypointsFromRoute(const TArray<FVector>& Route, float SpacingCm)
	{
		TArray<FVector> Out;
		// Drop repeated points first (a path with a doubled corner must not make a zero-length leg).
		TArray<FVector> Path;
		for (const FVector& Point : Route)
		{
			if (Path.IsEmpty() || !Point.Equals(Path.Last(), 0.5))
			{
				Path.Add(Point);
			}
		}
		if (Path.Num() < 2)
		{
			Out = Path;
			return Out;
		}
		const FVector End = Path.Last();
		const double Total = RouteLength(Path);
		if (SpacingCm > 0.f)
		{
			// Every SpacingCm along the path, never closer than half a spacing to the end (that stub merges into the goal).
			double Next = SpacingCm;
			double Walked = 0.0;
			for (int32 Index = 1; Index < Path.Num(); ++Index)
			{
				const double Leg = FVector::Dist(Path[Index - 1], Path[Index]);
				while (Next <= Walked + Leg && Total - Next >= 0.5 * SpacingCm)
				{
					const double Alpha = Leg > UE_DOUBLE_SMALL_NUMBER ? (Next - Walked) / Leg : 0.0;
					Out.Add(FMath::Lerp(Path[Index - 1], Path[Index], Alpha));
					Next += SpacingCm;
				}
				Walked += Leg;
			}
		}
		Out.Add(End);
		return Out;
	}

	int32 AdvanceWaypoint(const TArray<FVector>& Waypoints, int32 Index, const FVector& Feet, const FConfig& Config)
	{
		Index = FMath::Max(0, Index);
		while (Index < Waypoints.Num())
		{
			const FVector& Point = Waypoints[Index];
			const bool bNear = FVector::Dist2D(Feet, Point) <= Config.ArriveCm && FMath::Abs(Feet.Z - Point.Z) <= Config.LevelCm;
			if (!bNear)
			{
				break;
			}
			++Index;
		}
		return Index;
	}
}
