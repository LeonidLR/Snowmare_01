#include "Misc/AutomationTest.h"
#include "Formation/FormationSectorRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationScanPitchTest, "CodexTactics.Formation.ScanPitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFormationScanPitchTest::RunTest(const FString&)
{
	const FFormationConfig Config;
	float MinPitch = 90.f;
	float MaxPitch = -90.f;
	const int32 Steps = 36;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Time = Config.ScanPeriod * static_cast<float>(Step) / static_cast<float>(Steps);
		const float Pitch = FormationSectorRules::LookPitch(EFormationRole::Commander, Time, 0.f, Config);
		MinPitch = FMath::Min(MinPitch, Pitch);
		MaxPitch = FMath::Max(MaxPitch, Pitch);
	}
	TestTrue(TEXT("open ground spans at least 20 deg"), MaxPitch - MinPitch >= 20.f);
	TestTrue(TEXT("open ground reaches above the horizon"), MaxPitch > 5.f);
	TestTrue(TEXT("open ground reaches below the horizon"), MinPitch < -5.f);

	const float Commander = FormationSectorRules::LookPitch(EFormationRole::Commander, 0.f, 0.f, Config);
	const float Right = FormationSectorRules::LookPitch(EFormationRole::Right, 0.f, 0.f, Config);
	TestTrue(TEXT("two roles are not in step at t = 0"), !FMath::IsNearlyEqual(Commander, Right, 1.f));

	float BiasedMax = -90.f;
	float BiasedMin = 90.f;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Time = Config.ScanPeriod * static_cast<float>(Step) / static_cast<float>(Steps);
		const float Pitch = FormationSectorRules::LookPitch(EFormationRole::Left, Time, Config.ScanPitchHighBias, Config);
		BiasedMax = FMath::Max(BiasedMax, Pitch);
		BiasedMin = FMath::Min(BiasedMin, Pitch);
	}
	TestTrue(TEXT("high geometry lifts the scan above +10 deg"), BiasedMax > 10.f);
	TestTrue(TEXT("the high bias keeps the sweep up"), BiasedMin > 0.f);

	TestEqual(TEXT("a wild bias stays inside the aim-offset range"), FormationSectorRules::LookPitch(EFormationRole::Commander, 0.f, 200.f, Config), 90.f, 0.01f);

	// LookYaw is untouched: the commander at t = 0 on a diamond walk is still his search, not a pitch.
	const float Yaw = FormationSectorRules::LookYaw(EFormationRole::Commander, EFormationKind::Diamond, EFormationMode::Walk, 4, 0.f, 0.f, TOptional<float>(), Config);
	TestTrue(TEXT("LookYaw is still the horizontal search"), FMath::Abs(Yaw) <= Config.ScanAmpLead + 1e-3f);
	return true;
}

#endif
