#include "Combat/FallbackWaveRules.h"
#include "Data/GodotBalanceAsset.h"
#include "Math/RandomStream.h"

FFallbackWaveCounts FallbackWaveRules::Compute(const UGodotBalanceAsset* Config, int32 WaveIndex, FRandomStream& Random)
{
	auto Number = [Config](const TCHAR* Key, float Default)
	{
		return FMath::RoundToInt(Config ? Config->GetNumber(Key, Default) : Default);
	};
	const int32 MinTotal = Number(TEXT("min_enemies_per_wave"), 3.f);
	const int32 BaseCount = Number(TEXT("base_wave_enemy_count"), 3.f);
	const int32 Growth = Number(TEXT("enemy_count_per_wave_growth"), 2.f);
	const int32 MaxTotal = Number(TEXT("max_enemies_per_wave"), 16.f);
	const int32 BruteStart = Number(TEXT("brute_start_wave"), 3.f);
	const bool bBrutes = WaveIndex >= BruteStart;

	FFallbackWaveCounts Counts;
	Counts.Hounds = Number(TEXT("min_hounds_per_wave"), 2.f);
	Counts.Spitters = Number(TEXT("min_spitters_per_wave"), 1.f);
	Counts.Brutes = bBrutes ? Number(TEXT("min_brutes_per_wave"), 1.f) : 0;

	const int32 Target = FMath::Clamp(BaseCount + (WaveIndex - 1) * Growth, MinTotal, MaxTotal);
	for (int32 Remaining = FMath::Max(0, Target - Counts.Total()); Remaining > 0; --Remaining)
	{
		const float Roll = Random.FRand();
		if (bBrutes && Roll < 0.20f && Counts.Brutes < 3)
		{
			++Counts.Brutes;
		}
		else if (Roll < 0.65f)
		{
			++Counts.Hounds;
		}
		else
		{
			++Counts.Spitters;
		}
	}
	return Counts;
}
