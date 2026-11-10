// CodexTactics.Characters.CriticalWound.* - the critical wound (the game designer 2026-11-10, CriticalWoundRules): under critical_from (0.2) of his
// max health an operative goes down by himself, only crawls, cannot stand / crouch / sprint; healed back to the threshold he may stand again.

#include "Misc/AutomationTest.h"
#include "Characters/CriticalWoundRules.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundThresholdTest, "CodexTactics.Characters.CriticalWound.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundThresholdTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	const FCriticalWoundConfig Config;
	TestEqual(TEXT("default critical_from = 0.2 (the designer)"), Config.CriticalFrom, 0.2f);
	TestTrue(TEXT("15 % HP -> critical"), IsCritical(Config, 21.f, 140.f));
	TestTrue(TEXT("19.9 % -> critical"), IsCritical(Config, 19.9f, 100.f));
	TestFalse(TEXT("exactly 20 % -> not critical (healing to 20 % lets him stand)"), IsCritical(Config, 20.f, 100.f));
	TestFalse(TEXT("45 % (wounded, not critical)"), IsCritical(Config, 63.f, 140.f));
	TestFalse(TEXT("dead (0 HP) -> not critical: the death owns him"), IsCritical(Config, 0.f, 100.f));
	TestFalse(TEXT("no max health -> not critical"), IsCritical(Config, 5.f, 0.f));
	FCriticalWoundConfig Off;
	Off.CriticalFrom = 0.f;
	TestFalse(TEXT("critical_from 0 = the rule off"), IsCritical(Off, 1.f, 100.f));
	TestTrue(TEXT("the squad always"), AppliesTo(Config, true));
	TestFalse(TEXT("a hostile PMC fighter: off by default"), AppliesTo(Config, false));
	FCriticalWoundConfig Hostiles;
	Hostiles.bApplyToHostiles = true;
	TestTrue(TEXT("apply_to_hostiles: the PMC fighters too"), AppliesTo(Hostiles, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundStanceTest, "CodexTactics.Characters.CriticalWound.StanceLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundStanceTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	TestTrue(TEXT("critical: prone allowed"), AllowsStance(true, EOperativeStance::Prone));
	TestFalse(TEXT("critical: stand refused"), AllowsStance(true, EOperativeStance::Standing));
	TestFalse(TEXT("critical: crouch refused"), AllowsStance(true, EOperativeStance::Crouching));
	TestTrue(TEXT("not critical: stand allowed"), AllowsStance(false, EOperativeStance::Standing));
	TestTrue(TEXT("not critical: crouch allowed"), AllowsStance(false, EOperativeStance::Crouching));
	TestFalse(TEXT("critical: no sprint"), AllowsSprint(true));
	TestTrue(TEXT("not critical: sprint (the other rules decide)"), AllowsSprint(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundStepTest, "CodexTactics.Characters.CriticalWound.Step",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundStepTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	// StepWound(bDown, bCritical, bProne, bBusy)
	TestTrue(TEXT("healthy, not down -> nothing"), StepWound(false, false, false, false) == ECriticalWoundAction::None);
	TestTrue(TEXT("becomes critical standing, free -> goes down"), StepWound(false, true, false, false) == ECriticalWoundAction::GoDown);
	TestTrue(TEXT("becomes critical on a ladder / knocked down -> waits"), StepWound(false, true, false, true) == ECriticalWoundAction::WaitToGoDown);
	TestTrue(TEXT("becomes critical already prone -> down (the lock and the line, no stance change)"), StepWound(false, true, true, false) == ECriticalWoundAction::GoDown);
	TestTrue(TEXT("held down, prone -> nothing"), StepWound(true, true, true, false) == ECriticalWoundAction::None);
	TestTrue(TEXT("held down but stood up somehow (a get-up clip) -> down again"), StepWound(true, true, false, false) == ECriticalWoundAction::GoDown);
	TestTrue(TEXT("held down, healed -> may stand"), StepWound(true, false, true, false) == ECriticalWoundAction::MayStand);
	TestTrue(TEXT("healed while busy -> may stand all the same"), StepWound(true, false, false, true) == ECriticalWoundAction::MayStand);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundParseTest, "CodexTactics.Characters.CriticalWound.ParseConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundParseTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	FCriticalWoundConfig Config;
	TestTrue(TEXT("parses"), ParseConfig(TEXT("{\"critical_from\": 0.25, \"apply_to_hostiles\": true, \"comment\": \"x\"}"), Config));
	TestEqual(TEXT("critical_from read"), Config.CriticalFrom, 0.25f);
	TestTrue(TEXT("apply_to_hostiles read"), Config.bApplyToHostiles);
	FCriticalWoundConfig Percent;
	TestTrue(TEXT("a percent parses"), ParseConfig(TEXT("{\"critical_from\": 15}"), Percent));
	TestEqual(TEXT("15 -> 0.15"), Percent.CriticalFrom, 0.15f);
	FCriticalWoundConfig Kept;
	TestFalse(TEXT("not JSON -> false"), ParseConfig(TEXT("nope"), Kept));
	TestEqual(TEXT("... and the config kept"), Kept.CriticalFrom, 0.2f);
	TestEqual(TEXT("SanitizeShare: negative = off"), SanitizeShare(-1.0), 0.f);
	TestEqual(TEXT("SanitizeShare: > 100 = off"), SanitizeShare(150.0), 0.f);
	TestEqual(TEXT("SanitizeShare: NaN = off"), SanitizeShare(FMath::Sqrt(-1.0)), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundDataFileTest, "CodexTactics.Characters.CriticalWound.DataFileMirrorsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundDataFileTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	FString Text;
	if (!TestTrue(TEXT("critical_wound.json exists"), FFileHelper::LoadFileToString(Text, *GetWoundConfigPath())))
	{
		return false;
	}
	FCriticalWoundConfig FromFile;
	FromFile.CriticalFrom = -1.f;
	TestTrue(TEXT("critical_wound.json parses"), ParseConfig(Text, FromFile));
	const FCriticalWoundConfig Defaults;
	TestEqual(TEXT("critical_from: the file = the code default"), FromFile.CriticalFrom, Defaults.CriticalFrom);
	TestEqual(TEXT("apply_to_hostiles: the file = the code default"), FromFile.bApplyToHostiles, Defaults.bApplyToHostiles);
	TestEqual(TEXT("heavy_from: the file = the code default"), FromFile.HeavyFrom, Defaults.HeavyFrom);
	TestEqual(TEXT("heavy_speed_multiplier: the file = the code default"), FromFile.HeavySpeedMultiplier, Defaults.HeavySpeedMultiplier);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCriticalWoundTierTest, "CodexTactics.Characters.CriticalWound.Tiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCriticalWoundTierTest::RunTest(const FString&)
{
	using namespace CriticalWoundRules;
	const FCriticalWoundConfig Config;
	TestEqual(TEXT("default heavy_from 0.35, heavy speed x0.45"), Config.HeavyFrom == 0.35f && Config.HeavySpeedMultiplier == 0.45f, true);
	TestTrue(TEXT("full HP -> none"), ChooseWoundTier(Config, 140.f, 140.f, 0.5f, true) == EWoundTier::None);
	TestTrue(TEXT("exactly 50 % -> none (wounded is under it)"), ChooseWoundTier(Config, 50.f, 100.f, 0.5f, true) == EWoundTier::None);
	TestTrue(TEXT("45 % -> light"), ChooseWoundTier(Config, 63.f, 140.f, 0.5f, true) == EWoundTier::Light);
	TestTrue(TEXT("exactly 35 % -> light"), ChooseWoundTier(Config, 35.f, 100.f, 0.5f, true) == EWoundTier::Light);
	TestTrue(TEXT("30 % -> heavy"), ChooseWoundTier(Config, 30.f, 100.f, 0.5f, true) == EWoundTier::Heavy);
	TestTrue(TEXT("exactly 20 % -> heavy"), ChooseWoundTier(Config, 20.f, 100.f, 0.5f, true) == EWoundTier::Heavy);
	TestTrue(TEXT("15 % -> critical"), ChooseWoundTier(Config, 15.f, 100.f, 0.5f, true) == EWoundTier::Critical);
	TestTrue(TEXT("dead -> none"), ChooseWoundTier(Config, 0.f, 100.f, 0.5f, true) == EWoundTier::None);
	TestTrue(TEXT("no max health -> none"), ChooseWoundTier(Config, 5.f, 0.f, 0.5f, true) == EWoundTier::None);
	TestTrue(TEXT("a hostile (the rule not applied): 15 % stays light (today's behaviour)"), ChooseWoundTier(Config, 15.f, 100.f, 0.5f, false) == EWoundTier::Light);
	FCriticalWoundConfig NoHeavy;
	NoHeavy.HeavyFrom = 0.f;
	TestTrue(TEXT("heavy_from 0: 30 % stays light"), ChooseWoundTier(NoHeavy, 30.f, 100.f, 0.5f, true) == EWoundTier::Light);
	TestTrue(TEXT("a wounded threshold under heavy_from still makes a heavy body heavy"), ChooseWoundTier(Config, 30.f, 100.f, 0.1f, true) == EWoundTier::Heavy);
	// The speed: the light tier's x0.65 stays; the heavy tier standing walks x0.45 (the extra factor on top of 0.65).
	TestEqual(TEXT("heavy standing: 0.45 / 0.65"), TierSpeedScale(Config, EWoundTier::Heavy, true, 0.65f), 0.45f / 0.65f, 0.0001f);
	TestEqual(TEXT("heavy standing at 220 cm/s: 220 x 0.65 x scale = 99 cm/s"), 220.f * 0.65f * TierSpeedScale(Config, EWoundTier::Heavy, true, 0.65f), 99.f, 0.01f);
	TestEqual(TEXT("heavy crouched / prone: the light speed"), TierSpeedScale(Config, EWoundTier::Heavy, false, 0.65f), 1.f);
	TestEqual(TEXT("light: x1 (0.65 stays)"), TierSpeedScale(Config, EWoundTier::Light, true, 0.65f), 1.f);
	TestEqual(TEXT("critical (crawls): x1 (today's crawl)"), TierSpeedScale(Config, EWoundTier::Critical, true, 0.65f), 1.f);
	TestEqual(TEXT("no wounded multiplier: x1"), TierSpeedScale(Config, EWoundTier::Heavy, true, 0.f), 1.f);
	FCriticalWoundConfig Parsed;
	TestTrue(TEXT("parses heavy keys"), ParseConfig(TEXT("{\"heavy_from\": 30, \"heavy_speed_multiplier\": 2.5}"), Parsed));
	TestEqual(TEXT("heavy_from 30 (a percent) -> 0.3"), Parsed.HeavyFrom, 0.3f, 0.0001f);
	TestEqual(TEXT("heavy_speed_multiplier clamped to 1"), Parsed.HeavySpeedMultiplier, 1.f);
	return true;
}

#endif
