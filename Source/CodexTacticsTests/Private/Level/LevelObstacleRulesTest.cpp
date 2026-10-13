#include "Misc/AutomationTest.h"
#include "Level/LevelObstacleRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Demo 01 level obstacles (user flow 2026-10-13): the one-at-a-time shaky bridge and the low fence a body crawls under.

#define LEVEL_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Level." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

LEVEL_TEST(FBridgeRulesTest, "Bridge.Rules")
bool FBridgeRulesTest::RunTest(const FString&)
{
	using namespace BridgeRules;
	TestEqual(TEXT("empty span admits"), Evaluate(0, 1, false), EBridgeEntry::Admit);
	TestEqual(TEXT("full span refuses (capacity 1 = the demo default: no second operative while one crosses)"), Evaluate(1, 1, false), EBridgeEntry::Refuse);
	TestEqual(TEXT("an occupant is never refused"), Evaluate(1, 1, true), EBridgeEntry::AlreadyInside);
	TestEqual(TEXT("capacity 2: a second one fits"), Evaluate(1, 2, false), EBridgeEntry::Admit);
	TestEqual(TEXT("capacity 2: a third is refused"), Evaluate(2, 2, false), EBridgeEntry::Refuse);
	TestEqual(TEXT("capacity 0 = unlimited"), Evaluate(7, 0, false), EBridgeEntry::Admit);

	// Span coordinate: the volume's local X; a yawed bridge still measures along its own axis.
	const FTransform Bridge(FRotator(0.f, 90.f, 0.f), FVector(1000.f, 2000.f, 0.f));
	TestTrue(TEXT("a point 3 m along the yawed span reads +300"), FMath::IsNearlyEqual(SpanCoordinate(Bridge, FVector(1000.f, 2300.f, 0.f)), 300.f, 1.f));
	TestTrue(TEXT("the other end reads -300"), FMath::IsNearlyEqual(SpanCoordinate(Bridge, FVector(1000.f, 1700.f, 0.f)), -300.f, 1.f));
	TestTrue(TEXT("entered at -280, left at +310: crossed"), DidCross(-280.f, 310.f));
	TestFalse(TEXT("entered at -280, left at -295: stepped back"), DidCross(-280.f, -295.f));
	TestFalse(TEXT("exit on the span line is not a crossing"), DidCross(-280.f, 0.f));

	TestTrue(TEXT("first refusal always speaks"), ShouldRepeatRefusal(-1.f, 10.f, 4.f));
	TestFalse(TEXT("within the cooldown: silent"), ShouldRepeatRefusal(10.f, 12.f, 4.f));
	TestTrue(TEXT("after the cooldown: speaks again"), ShouldRepeatRefusal(10.f, 14.f, 4.f));
	return true;
}

LEVEL_TEST(FCrawlGapRulesTest, "CrawlGap.Rules")
bool FCrawlGapRulesTest::RunTest(const FString&)
{
	using namespace CrawlGapRules;
	TestTrue(TEXT("prone crawls under"), CanPassUnder(EOperativeStance::Prone));
	TestFalse(TEXT("crouched does not (manual prone, user default 2026-10-13)"), CanPassUnder(EOperativeStance::Crouching));
	TestFalse(TEXT("standing does not"), CanPassUnder(EOperativeStance::Standing));
	// Capsules: standing 180, crouched 117, prone 80 cm (half height 40 = the radius) against the default 90 cm clearance.
	TestTrue(TEXT("prone capsule 80 cm fits under 90"), FitsUnder(80.f, DefaultClearanceCm));
	TestFalse(TEXT("crouched capsule 117 cm does not"), FitsUnder(117.f, DefaultClearanceCm));
	TestFalse(TEXT("standing 180 does not"), FitsUnder(180.f, DefaultClearanceCm));
	TestFalse(TEXT("no body, no pass"), FitsUnder(0.f, DefaultClearanceCm));

	TestTrue(TEXT("walking at the fence approaches"), IsApproaching(FVector(200.f, 0.f, 0.f), FVector(150.f, 10.f, 0.f)));
	TestFalse(TEXT("walking away does not"), IsApproaching(FVector(200.f, 0.f, 0.f), FVector(-150.f, 0.f, 0.f)));
	TestFalse(TEXT("standing still does not"), IsApproaching(FVector(200.f, 0.f, 0.f), FVector(5.f, 0.f, 0.f)));
	TestFalse(TEXT("walking along the fence does not"), IsApproaching(FVector(200.f, 0.f, 0.f), FVector(0.f, 150.f, 0.f)));

	TestEqual(TEXT("side -"), Side(-120.f), -1);
	TestEqual(TEXT("side +"), Side(80.f), 1);
	TestEqual(TEXT("on the line"), Side(0.4f), 0);
	TestTrue(TEXT("in at -, out at +: passed"), DidPass(-1, 1));
	TestFalse(TEXT("in at -, out at -: turned back"), DidPass(-1, -1));
	TestFalse(TEXT("unknown entry side never passes"), DidPass(0, 1));
	return true;
}

#undef LEVEL_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
