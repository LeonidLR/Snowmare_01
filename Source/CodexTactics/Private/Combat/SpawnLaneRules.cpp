#include "Combat/SpawnLaneRules.h"

FString SpawnLaneRules::CanonicalLane(const FString& Lane)
{
	struct FAlias
	{
		const TCHAR* Key;
		const TCHAR* Russian;
	};
	static const FAlias Aliases[] = {
		{ TEXT("NORTH_GATE"), TEXT("Северные ворота") },
		{ TEXT("WEST_FLANK"), TEXT("Левый фланг") },
		{ TEXT("EAST_FLANK"), TEXT("Правый фланг") },
		{ TEXT("FAR_PERIMETER"), TEXT("Дальний периметр") },
	};
	const FString Trimmed = Lane.TrimStartAndEnd();
	for (const FAlias& Alias : Aliases)
	{
		if (Trimmed.Equals(Alias.Key, ESearchCase::IgnoreCase) || Trimmed.Contains(Alias.Russian, ESearchCase::IgnoreCase))
		{
			return Alias.Key;
		}
	}
	return Trimmed.ToUpper();
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
