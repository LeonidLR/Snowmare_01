// CodexTactics.Formation.LookFilter - the formation look a member's body turns to, filtered against one-frame flips (the game designer's video
// 09.11.2026_ Trembling_Bug_01, FormationJitterSmoke 2026-11-09: ClearLook flipped -77 -> 57 -> -59 along a wall, the bodies spun).

#include "Formation/FormationLookFilterRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationLookFilterTest, "CodexTactics.Formation.LookFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFormationLookFilterTest::RunTest(const FString&)
{
	using namespace FormationLookFilterRules;
	constexpr float Dt = 1.f / 60.f;
	FFormationLookFilter Filter;
	TestFalse(TEXT("no look: none"), Step(Filter, TOptional<float>(), Dt).IsSet());
	TestEqual(TEXT("the first look is taken at once"), Step(Filter, TOptional<float>(-77.f), Dt).Get(0.f), -77.f);
	TestEqual(TEXT("a small change passes at once (a sweeping corner look)"), Step(Filter, TOptional<float>(-60.f), Dt).Get(0.f), -60.f);
	// The recorded flip: -77 -> 57 -> -59 -> -73 frame by frame.
	TestEqual(TEXT("a one-frame flip to the far side is held back"), Step(Filter, TOptional<float>(57.f), Dt).Get(0.f), -60.f);
	TestEqual(TEXT("... the next frame's pick near the old look passes"), Step(Filter, TOptional<float>(-59.f), Dt).Get(0.f), -59.f);
	TestFalse(TEXT("... and the far pick is forgotten"), Filter.Pending.IsSet());
	// Flipping between the two sides every frame for 2 s: the body never turns to the far side.
	float Worst = 0.f;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		const float Applied = Step(Filter, TOptional<float>(Frame % 2 == 0 ? 120.f : -70.f), Dt).Get(0.f);
		Worst = FMath::Max(Worst, FMath::Abs(FMath::FindDeltaAngleDegrees(-59.f, Applied)));
	}
	TestTrue(FString::Printf(TEXT("a flip every frame never reaches the body (worst %.1f deg off)"), Worst), Worst <= DefaultJumpDeg);
	// A real change (the formation turned round): taken once it has held 0.15 s.
	float Applied = 0.f;
	int32 Frames = 0;
	do
	{
		Applied = Step(Filter, TOptional<float>(150.f), Dt).Get(0.f);
		++Frames;
	} while (!FMath::IsNearlyEqual(Applied, 150.f) && Frames < 100);
	TestTrue(FString::Printf(TEXT("a lasting jump is taken after ~0.15 s (%d frames)"), Frames), Frames >= 8 && Frames <= 11);
	// The short way round +-180.
	TestEqual(TEXT("150 -> -178: 32 deg the short way round, at once"), Step(Filter, TOptional<float>(-178.f), Dt).Get(0.f), -178.f);
	TestFalse(TEXT("the look cleared: none at once"), Step(Filter, TOptional<float>(), Dt).IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
