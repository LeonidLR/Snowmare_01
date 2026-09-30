#include "Misc/AutomationTest.h"
#include "Combat/FallbackWaveRules.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot main.gd _start_next_wave fallback branch (no level wave) parity, game_balance_config.gd defaults.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFallbackWaveRulesTest, "CodexTactics.Combat.FallbackWave.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFallbackWaveRulesTest::RunTest(const FString&)
{
	FRandomStream Random(1234);
	const FFallbackWaveCounts First = FallbackWaveRules::Compute(nullptr, 1, Random);
	TestTrue(TEXT("Wave 1: the minimums only (2 hounds, 1 spitter)"), First.Hounds == 2 && First.Spitters == 1 && First.Brutes == 0);

	for (int32 Seed = 0; Seed < 50; ++Seed)
	{
		FRandomStream Stream(Seed);
		const FFallbackWaveCounts Second = FallbackWaveRules::Compute(nullptr, 2, Stream);
		const FFallbackWaveCounts Third = FallbackWaveRules::Compute(nullptr, 3, Stream);
		const FFallbackWaveCounts Late = FallbackWaveRules::Compute(nullptr, 12, Stream);
		if (Second.Total() != 5 || Second.Brutes != 0 || Second.Hounds < 2 || Second.Spitters < 1)
		{
			AddError(FString::Printf(TEXT("Wave 2 (seed %d): 5 enemies, no brutes before brute_start_wave"), Seed));
		}
		if (Third.Total() != 7 || Third.Brutes < 1 || Third.Brutes > 3)
		{
			AddError(FString::Printf(TEXT("Wave 3 (seed %d): 7 enemies, 1..3 brutes"), Seed));
		}
		if (Late.Total() != 16 || Late.Brutes > 3)
		{
			AddError(FString::Printf(TEXT("Wave 12 (seed %d): capped at max_enemies_per_wave 16, at most 3 brutes"), Seed));
		}
	}
	return true;
}

#endif
