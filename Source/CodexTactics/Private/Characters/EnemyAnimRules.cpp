// Pure rules of the enemy animation extras (game designer 2026-10-20, Military Zombie). UE-only, no Godot reference.

#include "Characters/EnemyAnimRules.h"

namespace EnemyAnimRules
{
	uint32 MakeVariantSeed(const FString& ActorName, const FVector& SpawnLocation, int32 Override)
	{
		if (Override >= 0)
		{
			return static_cast<uint32>(Override);
		}
		const FIntVector Cell(FMath::RoundToInt(SpawnLocation.X / 10.0), FMath::RoundToInt(SpawnLocation.Y / 10.0), FMath::RoundToInt(SpawnLocation.Z / 10.0));
		return HashCombine(GetTypeHash(ActorName), GetTypeHash(Cell));
	}

	int32 PickVariantIndex(uint32 Seed, uint32 Salt, int32 Count)
	{
		if (Count <= 0)
		{
			return -1;
		}
		// Murmur3 finaliser over seed ^ salt: neighbouring seeds / salts spread over the whole range.
		uint32 Hash = Seed ^ (Salt * 0x9E3779B9u);
		Hash ^= Hash >> 16;
		Hash *= 0x85EBCA6Bu;
		Hash ^= Hash >> 13;
		Hash *= 0xC2B2AE35u;
		Hash ^= Hash >> 16;
		return static_cast<int32>(Hash % static_cast<uint32>(Count));
	}

	bool IsCalmIdle(bool bMoving, bool bDead, bool bKnockedDown, bool bOneShotPlaying, bool bTurnBased)
	{
		return !bMoving && !bDead && !bKnockedDown && !bOneShotPlaying && !bTurnBased;
	}

	EEnemyIdleBreak ChooseIdleBreak(float Roll, float ActivityChance, int32 NumFidgets, int32 NumActivities, bool bHunting)
	{
		const bool bActivityAllowed = NumActivities > 0 && !bHunting;
		if (bActivityAllowed && (Roll < ActivityChance || NumFidgets <= 0))
		{
			return EEnemyIdleBreak::Activity;
		}
		return NumFidgets > 0 ? EEnemyIdleBreak::Fidget : EEnemyIdleBreak::None;
	}

	int32 PickFidget(float Roll, int32 NumFidgets, int32 Current)
	{
		if (NumFidgets <= 0)
		{
			return -1;
		}
		const bool bSkipCurrent = NumFidgets > 1 && Current >= 0 && Current < NumFidgets;
		const int32 Choices = bSkipCurrent ? NumFidgets - 1 : NumFidgets;
		int32 Index = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(Roll, 0.f, 1.f) * Choices), 0, Choices - 1);
		if (bSkipCurrent && Index >= Current)
		{
			++Index;
		}
		return Index;
	}

	float PerformanceLockSeconds(float ClipSeconds, float PlayRate, float MaxSeconds)
	{
		if (ClipSeconds <= 0.f)
		{
			return 0.f;
		}
		const float Seconds = ClipSeconds / FMath::Max(PlayRate, 0.1f);
		return MaxSeconds > 0.f ? FMath::Min(Seconds, MaxSeconds) : Seconds;
	}

	float NextIdleBreakDelay(float Roll, float MinSeconds, float MaxSeconds)
	{
		const float Low = FMath::Min(MinSeconds, MaxSeconds);
		const float High = FMath::Max(MinSeconds, MaxSeconds);
		return FMath::Max(0.5f, FMath::Lerp(Low, High, FMath::Clamp(Roll, 0.f, 1.f)));
	}
}
