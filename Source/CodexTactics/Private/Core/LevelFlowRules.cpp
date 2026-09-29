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
	}
	else if (Balance)
	{
		Result.PreparationDuration = Balance->GetNumber(TEXT("preparation_phase_duration"), Result.PreparationDuration);
	}
	if (bLevelWaves)
	{
		Result.TotalWaves = Level->Waves.Num();
	}
	else if (Balance)
	{
		Result.TotalWaves = Balance->GetInt(TEXT("max_campaign_waves"), 10);
	}
	return Result;
}
