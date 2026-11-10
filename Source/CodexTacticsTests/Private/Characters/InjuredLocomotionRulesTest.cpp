// CodexTactics.Animation.Injured.* - the injured locomotion (the game designer 2026-11-10, InjuredLocomotionRules): a wounded operative walks /
// stands / crawls injured; a freezing one (cold >= cold_from 0.7) walks the injured walk, a frostbitten one (cold >= cold_slow_from 0.9) the slow
// injured walk forward, both keep the shiver idle standing; wounded first.

#include "Misc/AutomationTest.h"
#include "Characters/InjuredLocomotionRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredGaitTest, "CodexTactics.Animation.Injured.GaitFromTriggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredGaitTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	FInjuredTriggers Triggers;
	TestEqual(TEXT("defaults: the wounded trigger on, the cold off (the schema's default)"), Triggers.bWounded && Triggers.ColdFrom <= 0.f, true);
	TestTrue(TEXT("wounded -> the injured walk"), ChooseGait(Triggers, true, 0.f) == EInjuredGait::Wounded);
	TestTrue(TEXT("healthy, warm -> off"), ChooseGait(Triggers, false, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("healthy and frozen, the cold trigger off -> off"), ChooseGait(Triggers, false, 0.95f) == EInjuredGait::None);
	Triggers.ColdFrom = 0.7f; // the designer's decisions 2026-11-10: Freezing = the injured walk, Frostbite = the slow walk
	Triggers.ColdSlowFrom = 0.9f;
	TestTrue(TEXT("cold 75 % (Freezing) -> the injured walk"), ChooseGait(Triggers, false, 0.75f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold exactly 70 % -> the injured walk"), ChooseGait(Triggers, false, 0.7f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold 89 % -> still the injured walk"), ChooseGait(Triggers, false, 0.89f) == EInjuredGait::Cold);
	TestTrue(TEXT("cold exactly 90 % (Frostbite) -> the slow injured walk"), ChooseGait(Triggers, false, 0.9f) == EInjuredGait::ColdSlow);
	TestTrue(TEXT("cold 100 % -> the slow walk"), ChooseGait(Triggers, false, 1.f) == EInjuredGait::ColdSlow);
	TestTrue(TEXT("cold 69 % -> off"), ChooseGait(Triggers, false, 0.69f) == EInjuredGait::None);
	TestTrue(TEXT("wounded AND frostbitten -> wounded takes priority"), ChooseGait(Triggers, true, 0.95f) == EInjuredGait::Wounded);
	FInjuredTriggers SlowOnly;
	SlowOnly.ColdSlowFrom = 0.9f;
	TestTrue(TEXT("only cold_slow_from set: freezing -> off"), ChooseGait(SlowOnly, false, 0.75f) == EInjuredGait::None);
	TestTrue(TEXT("only cold_slow_from set: frostbite -> the slow walk"), ChooseGait(SlowOnly, false, 0.95f) == EInjuredGait::ColdSlow);
	Triggers.bWounded = false;
	TestTrue(TEXT("the wounded trigger off: a wounded, warm body -> off"), ChooseGait(Triggers, true, 0.f) == EInjuredGait::None);
	TestTrue(TEXT("the wounded trigger off: wounded and freezing -> the cold walk"), ChooseGait(Triggers, true, 0.8f) == EInjuredGait::Cold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredColdFromTest, "CodexTactics.Animation.Injured.SanitizeColdFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredColdFromTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("a share stays"), SanitizeColdFrom(0.7), 0.7f);
	TestEqual(TEXT("a percent is taken as a percent"), SanitizeColdFrom(70.0), 0.7f);
	TestEqual(TEXT("1 = 100 %"), SanitizeColdFrom(1.0), 1.f);
	TestEqual(TEXT("0 = off"), SanitizeColdFrom(0.0), 0.f);
	TestEqual(TEXT("negative = off"), SanitizeColdFrom(-0.5), 0.f);
	TestEqual(TEXT("over 100 = off"), SanitizeColdFrom(150.0), 0.f);
	TestEqual(TEXT("NaN = off"), SanitizeColdFrom(FMath::Sqrt(-1.0)), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredLayerTargetTest, "CodexTactics.Animation.Injured.LayerTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredLayerTargetTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("wounded, standing still: the injured idle (the layer's)"), LayerTarget(EInjuredGait::Wounded, true, 0.f), 1.f);
	TestEqual(TEXT("wounded, walking: the directional injured walk"), LayerTarget(EInjuredGait::Wounded, true, 1.f), 1.f);
	TestEqual(TEXT("freezing, standing still: the ABP's shiver idle (layer off)"), LayerTarget(EInjuredGait::Cold, true, 0.f), 0.f);
	TestEqual(TEXT("freezing, walking: the injured walk"), LayerTarget(EInjuredGait::Cold, true, 1.f), 1.f);
	TestEqual(TEXT("frostbite, standing still: the shiver idle"), LayerTarget(EInjuredGait::ColdSlow, true, 0.f), 0.f);
	TestEqual(TEXT("frostbite, half way into the walk"), LayerTarget(EInjuredGait::ColdSlow, true, 0.5f), 0.5f);
	TestEqual(TEXT("healthy: off"), LayerTarget(EInjuredGait::None, true, 1.f), 0.f);
	TestEqual(TEXT("not eligible (crouched / prone / in cover / a full-body clip): off"), LayerTarget(EInjuredGait::Wounded, false, 1.f), 0.f);
	TestEqual(TEXT("frostbite walking forward: the slow walk"), SlowWalkShare(EInjuredGait::ColdSlow, 1.f), 1.f);
	TestEqual(TEXT("frostbite strafing (outside the cone): the injured walk"), SlowWalkShare(EInjuredGait::ColdSlow, 0.f), 0.f);
	TestEqual(TEXT("freezing: never the slow walk"), SlowWalkShare(EInjuredGait::Cold, 1.f), 0.f);
	TestEqual(TEXT("wounded: never the slow walk"), SlowWalkShare(EInjuredGait::Wounded, 1.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredConeTest, "CodexTactics.Animation.Injured.ForwardCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredConeTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestEqual(TEXT("straight ahead: the full slow walk"), ForwardConeShare(0.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("30 deg left: inside the full cone"), ForwardConeShare(-30.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("half way between the edges (57.5 deg)"), ForwardConeShare(57.5f, 35.f, 80.f), 0.5f, 1.e-4f);
	TestEqual(TEXT("a strafe (90 deg): none"), ForwardConeShare(90.f, 35.f, 80.f), 0.f);
	TestEqual(TEXT("backwards: none"), ForwardConeShare(180.f, 35.f, 80.f), 0.f);
	TestEqual(TEXT("an unnormalised angle (360 = ahead)"), ForwardConeShare(360.f, 35.f, 80.f), 1.f);
	TestEqual(TEXT("zero edge below the full edge: a hard cone"), ForwardConeShare(40.f, 35.f, 10.f), 0.f);
	TestEqual(TEXT("clip band 10 deg: 3 deg off a clip (alpha 3/45) plays it pure"), BandedAlpha(3.f / 45.f, 10.f), 0.f);
	TestEqual(TEXT("... 3 deg off the next clip: the next clip pure"), BandedAlpha(42.f / 45.f, 10.f), 1.f);
	TestEqual(TEXT("... half way: still half"), BandedAlpha(0.5f, 10.f), 0.5f, 1.e-4f);
	TestEqual(TEXT("... 20 deg past a clip: (20 - 10) / 25"), BandedAlpha(20.f / 45.f, 10.f), 0.4f, 1.e-4f);
	TestEqual(TEXT("no band: unchanged"), BandedAlpha(0.3f, 0.f), 0.3f);
	TestEqual(TEXT("... inside it"), ForwardConeShare(20.f, 35.f, 10.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInjuredCrawlAndTorsoTest, "CodexTactics.Animation.Injured.CrawlAndTorso",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInjuredCrawlAndTorsoTest::RunTest(const FString&)
{
	using namespace InjuredLocomotionRules;
	TestTrue(TEXT("wounded and prone with the clips: the injured crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, true, true));
	TestFalse(TEXT("wounded standing: no crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, false, true));
	TestFalse(TEXT("freezing and prone: the mocap crawl (the cold is not a wound)"), UsesInjuredCrawl(EInjuredGait::Cold, true, true));
	TestFalse(TEXT("frostbitten and prone: the mocap crawl"), UsesInjuredCrawl(EInjuredGait::ColdSlow, true, true));
	TestFalse(TEXT("no injured crawl clips for this body (the PMC's UE5 skeleton): the mocap crawl"), UsesInjuredCrawl(EInjuredGait::Wounded, true, false));
	TestEqual(TEXT("aiming: the torso keeps the ABP's rotation exactly"), TorsoPelvisShare(0.3f, true), 0.f);
	TestEqual(TEXT("not aiming: the data's share of the limp"), TorsoPelvisShare(0.3f, false), 0.3f);
	TestEqual(TEXT("clamped to 1"), TorsoPelvisShare(4.f, false), 1.f);
	TestEqual(TEXT("clamped to 0"), TorsoPelvisShare(-1.f, false), 0.f);
	return true;
}

#endif
