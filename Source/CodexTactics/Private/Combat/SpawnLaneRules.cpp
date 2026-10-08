#include "Combat/SpawnLaneRules.h"

namespace
{
	struct FLaneAlias
	{
		const TCHAR* Key;
		/** Godot / map data spelling (kept: spawn points and LevelJson carry it). */
		const TCHAR* Russian;
		/** English player-facing name (also accepted as an alias). */
		const TCHAR* English;
	};

	const FLaneAlias* FindLaneAlias(const FString& Trimmed)
	{
		static const FLaneAlias Aliases[] = {
			{ TEXT("NORTH_GATE"), TEXT("Северные ворота"), TEXT("North gate") }, // cyrillic-ok: legacy Russian data
			{ TEXT("WEST_FLANK"), TEXT("Левый фланг"), TEXT("West flank") }, // cyrillic-ok: legacy Russian data
			{ TEXT("EAST_FLANK"), TEXT("Правый фланг"), TEXT("East flank") }, // cyrillic-ok: legacy Russian data
			{ TEXT("FAR_PERIMETER"), TEXT("Дальний периметр"), TEXT("Far perimeter") }, // cyrillic-ok: legacy Russian data
		};
		for (const FLaneAlias& Alias : Aliases)
		{
			if (Trimmed.Equals(Alias.Key, ESearchCase::IgnoreCase) || Trimmed.Contains(Alias.Russian, ESearchCase::IgnoreCase)
				|| Trimmed.Contains(Alias.English, ESearchCase::IgnoreCase))
			{
				return &Alias;
			}
		}
		return nullptr;
	}
}

FString SpawnLaneRules::CanonicalLane(const FString& Lane)
{
	const FString Trimmed = Lane.TrimStartAndEnd();
	const FLaneAlias* Alias = FindLaneAlias(Trimmed);
	return Alias ? FString(Alias->Key) : Trimmed.ToUpper();
}

FString SpawnLaneRules::GetLaneDisplayName(const FString& Lane)
{
	const FString Trimmed = Lane.TrimStartAndEnd();
	const FLaneAlias* Alias = FindLaneAlias(Trimmed);
	return Alias ? FString(Alias->English) : Trimmed;
}

bool SpawnLaneRules::LanesMatch(const FString& PointLane, const FString& RequestedLane)
{
	if (RequestedLane.IsEmpty() || RequestedLane.Equals(TEXT("ANY"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	// Godot's rule (either name contains the other) plus the aliases.
	return PointLane.Contains(RequestedLane, ESearchCase::IgnoreCase) || RequestedLane.Contains(PointLane, ESearchCase::IgnoreCase)
		|| CanonicalLane(PointLane) == CanonicalLane(RequestedLane);
}
