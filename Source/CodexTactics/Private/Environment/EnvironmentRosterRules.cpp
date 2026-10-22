#include "Environment/EnvironmentRosterRules.h"

// Phase 2b: the enemy roster by time of day and weather (UE-only, no Godot reference). The approved rule and its reference
// algorithm: docs/design/roster-by-weather.html (resolve()), docs/systems/environment-weather.md §4.2 (roster v2, decisions 13 / 14 / 18).

namespace
{
	/** Level-JSON name of a type (the Wave Editor / page names), for the log lines. */
	const TCHAR* RosterTypeName(EEnemyArchetype Type)
	{
		switch (Type)
		{
		case EEnemyArchetype::FrostHound: return TEXT("HOUND");
		case EEnemyArchetype::Spitter: return TEXT("SPITTER");
		case EEnemyArchetype::Brute: return TEXT("BRUTE");
		case EEnemyArchetype::Frostbitten: return TEXT("FROSTBITTEN");
		case EEnemyArchetype::Cutter: return TEXT("CUTTER");
		case EEnemyArchetype::CryoDrone: return TEXT("CRYO_DRONE");
		case EEnemyArchetype::Marksman: return TEXT("MARKSMAN");
		default: return TEXT("BASE");
		}
	}

	/** Round half up (the page's Math.floor(x + 0.5)); counts are never negative. */
	int32 RoundHalfUp(float Value)
	{
		return FMath::Max(0, FMath::FloorToInt(Value + 0.5f));
	}

	const FEnvironmentRosterPair* FindPair(const FEnvironmentRosterConfig& Roster, EEnemyArchetype From)
	{
		return Roster.Pairs.FindByPredicate([From](const FEnvironmentRosterPair& Pair) { return Pair.From == From; });
	}
}

bool EnvironmentRosterRules::GetSuitability(const FEnvironmentRosterConfig& Roster, EEnemyArchetype Type, EWeatherState Weather,
	ETimeOfDayPhase Phase, float& OutScore)
{
	const FEnvironmentSuitabilityRow* Row = Roster.Suitability.FindByPredicate(
		[Type](const FEnvironmentSuitabilityRow& Candidate) { return Candidate.Type == Type; });
	const int32 Index = static_cast<int32>(Weather) * 4 + static_cast<int32>(Phase);
	if (!Row || !Row->Scores.IsValidIndex(Index))
	{
		return false;
	}
	OutScore = Row->Scores[Index];
	return true;
}

float EnvironmentRosterRules::GetSwapShare(const FEnvironmentRosterConfig& Roster, float Score)
{
	return Score < Roster.AllBelow ? 1.f : Score < Roster.HalfBelow ? 0.5f : 0.f;
}

TArray<FEnemySpawnEntry> EnvironmentRosterRules::ResolveSpawns(const FEnvironmentRosterConfig& Roster, const TArray<FEnemySpawnEntry>& Spawns,
	EWeatherState Weather, ETimeOfDayPhase Phase, TArray<FString>* OutLog)
{
	if (!Roster.bEnabled)
	{
		return Spawns;
	}
	TArray<FEnemySpawnEntry> Result;
	Result.Reserve(Spawns.Num() * 2);
	for (const FEnemySpawnEntry& Entry : Spawns)
	{
		if (Entry.bEnvironmentLocked)
		{
			Result.Add(Entry); // Wave Editor row lock: as written, whatever the conditions
			continue;
		}
		FEnemySpawnEntry Source = Entry;
		TOptional<FEnemySpawnEntry> Replacement;

		// 1. The pair: a share of the good-conditions form swaps (round half up per entry), the rest stays.
		float Score = 0.f;
		if (const FEnvironmentRosterPair* Pair = FindPair(Roster, Entry.EnemyType);
			Pair && Source.Count > 0 && GetSuitability(Roster, Entry.EnemyType, Weather, Phase, Score))
		{
			const float Share = GetSwapShare(Roster, Score);
			const int32 Moved = FMath::Min(Source.Count, RoundHalfUp(Source.Count * Share));
			if (Moved > 0)
			{
				const int32 Ratio = FMath::Max(1, Pair->Ratio);
				FEnemySpawnEntry Swap; // the type's own stats: custom stats belong to the written type
				Swap.EnemyType = Pair->To;
				Swap.Count = Moved * Ratio;
				Swap.SpawnLane = Source.SpawnLane;
				Swap.SpawnDelaySec = Source.SpawnDelaySec;
				Swap.InitialDelaySec = Source.InitialDelaySec;
				Swap.bEnvironmentLocked = false;
				Replacement = Swap;
				Source.Count -= Moved;
				if (OutLog)
				{
					OutLog->Add(FString::Printf(TEXT("%s %.2f < %.1f: %d -> %d %s"), RosterTypeName(Entry.EnemyType), Score,
						Share >= 1.f ? Roster.AllBelow : Roster.HalfBelow, Moved, Swap.Count, RosterTypeName(Pair->To)));
				}
			}
		}

		// 2. The bonus ("hunting weather" reinforcements) on a BonusTypes entry as written (never on a replacement).
		if (Source.Count > 0 && Roster.BonusTypes.Contains(Source.EnemyType)
			&& GetSuitability(Roster, Source.EnemyType, Weather, Phase, Score) && Score > Roster.BonusAbove)
		{
			const int32 Boosted = RoundHalfUp(Source.Count * Roster.BonusMultiplier);
			if (Boosted != Source.Count && OutLog)
			{
				OutLog->Add(FString::Printf(TEXT("%s %.2f > %.1f: %d -> %d"), RosterTypeName(Source.EnemyType), Score, Roster.BonusAbove,
					Source.Count, Boosted));
			}
			Source.Count = Boosted;
		}

		if (Source.Count > 0)
		{
			Result.Add(Source);
		}
		if (Replacement.IsSet() && Replacement->Count > 0)
		{
			Result.Add(Replacement.GetValue());
		}
	}
	return Result;
}
