#include "Core/LevelFlowRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"

FGameFlowConfig LevelFlowRules::ApplyLevel(const FGameFlowConfig& Base, const FLevelCombatConfig* Level, const UGodotBalanceAsset* Balance)
{
	FGameFlowConfig Result = Base;
	const bool bLevelWaves = Level && Level->Waves.Num() > 0;
	if (Level)
	{
		Result.PreparationDuration = Level->PrepPhaseDuration;
		Result.WaveRestDuration = Level->WaveRestDuration;
		Result.bAmbushSkirmish = Level->bAmbushSkirmish;
	}
	else if (Balance)
	{
		Result.PreparationDuration = Balance->GetNumber(TEXT("preparation_phase_duration"), Result.PreparationDuration);
	}
	if (bLevelWaves)
	{
		Result.TotalWaves = Level->Waves.Num();
		// Per-wave "sudden_start": false keeps that wave's old prepared start (user decision 2026-10-11).
		Result.PreparedWaveIndices.Reset();
		for (int32 Index = 0; Index < Level->Waves.Num(); ++Index)
		{
			if (!Level->Waves[Index].bSuddenStart)
			{
				Result.PreparedWaveIndices.Add(Index + 1);
			}
		}
	}
	else if (Balance)
	{
		Result.TotalWaves = Balance->GetInt(TEXT("max_campaign_waves"), 10);
	}
	return Result;
}
