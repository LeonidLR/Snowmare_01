// CodexTactics.Characters.Corpse.* — corpses stay (game designer 2026-11-08, UE-only; CorpseRules, UCorpseSubsystem): when a body is
// frozen (settled death pose, on the ground first), the cap (the oldest body farthest from the squad, out of the camera view first) and
// the fade, plus the level JSON key corpse_cap.

#include "Misc/AutomationTest.h"
#include "Characters/CorpseRules.h"
#include "Data/LevelJsonRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define CORPSE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Characters.Corpse." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CorpseTest
{
	FCorpseCandidate Body(double DeathTime, float DistanceM, bool bInView = false, bool bFrozen = true, bool bFading = false)
	{
		FCorpseCandidate Out;
		Out.DeathTime = DeathTime;
		Out.SquadDistanceCm = DistanceM * 100.f;
		Out.bInView = bInView;
		Out.bFrozen = bFrozen;
		Out.bFading = bFading;
		return Out;
	}
}

CORPSE_TEST(FCorpseFreezeTest, "FreezeOnceSettledOnTheGround")
bool FCorpseFreezeTest::RunTest(const FString&)
{
	using namespace CorpseRules;
	const FCorpseConfig Config; // min 0.6 s, hold 0.25 s, max 8 s
	TestFalse(TEXT("The death clip still plays"), ShouldFreeze(1.0f, -1.f, false, false, Config));
	TestFalse(TEXT("Settled, not held long enough"), ShouldFreeze(1.0f, 0.1f, false, false, Config));
	TestTrue(TEXT("Settled and held: frozen"), ShouldFreeze(1.0f, 0.3f, false, false, Config));
	TestFalse(TEXT("Never before the minimum"), ShouldFreeze(0.4f, 0.3f, false, false, Config));
	TestTrue(TEXT("A clip that never settles: frozen at the maximum"), ShouldFreeze(8.0f, -1.f, false, false, Config));
	// Killed in the air / on a ladder: it falls to the ground first (FallRules::ShouldCorpseFall's bodies), then freezes.
	TestFalse(TEXT("Falling: not yet, pose or not"), ShouldFreeze(3.0f, 2.f, true, false, Config));
	TestFalse(TEXT("On the ladder's drop: not yet"), ShouldFreeze(9.0f, 2.f, false, true, Config));
	TestFalse(TEXT("Falling past the max: still waits"), ShouldFreeze(12.0f, 2.f, true, false, Config));
	TestTrue(TEXT("Stuck falling 2 x max: frozen where it is"), ShouldFreeze(16.0f, 2.f, true, false, Config));
	return true;
}

CORPSE_TEST(FCorpseCapTest, "CapFadesTheOldestFarthestOutOfView")
bool FCorpseCapTest::RunTest(const FString&)
{
	using namespace CorpseRules;
	using CorpseTest::Body;
	FCorpseConfig Config;
	Config.OldestPool = 3;
	TArray<FCorpseCandidate> Bodies = { Body(10.0, 5.f), Body(20.0, 50.f), Body(30.0, 80.f), Body(40.0, 200.f), Body(5.0, 20.f) };
	TestEqual(TEXT("Cap 5: none over"), CountOverCap(Bodies, 5), 0);
	TestEqual(TEXT("Cap 3: two over"), CountOverCap(Bodies, 3), 2);
	// The 3 oldest: t5 (20 m), t10 (5 m), t20 (50 m) -> the farthest of them, t20. The newest at 200 m is not among the oldest.
	TestEqual(TEXT("Oldest pool, farthest"), PickCorpseToFade(Bodies, Config), 1);
	Bodies[1].bFading = true;
	TestEqual(TEXT("Fading ones no longer count"), CountOverCap(Bodies, 3), 1);
	TestEqual(TEXT("Next: pool t5, t10, t30 -> t30 at 80 m"), PickCorpseToFade(Bodies, Config), 2);

	// Never inside the camera view if avoidable: an in-view body is skipped while a hidden one is left.
	TArray<FCorpseCandidate> Viewed = { Body(1.0, 90.f, true), Body(2.0, 10.f, false), Body(3.0, 60.f, true) };
	TestEqual(TEXT("The hidden one, though nearer"), PickCorpseToFade(Viewed, Config), 1);
	TArray<FCorpseCandidate> AllInView = { Body(1.0, 10.f, true), Body(2.0, 30.f, true) };
	TestEqual(TEXT("All in view: the farthest old one anyway"), PickCorpseToFade(AllInView, Config), 1);

	// Bodies still settling are never faded; with none frozen nothing goes.
	TArray<FCorpseCandidate> Settling = { Body(1.0, 100.f, false, false), Body(2.0, 10.f, false, true) };
	TestEqual(TEXT("Only the frozen one"), PickCorpseToFade(Settling, Config), 1);
	TArray<FCorpseCandidate> NoneFrozen = { Body(1.0, 100.f, false, false) };
	TestEqual(TEXT("None frozen: wait"), PickCorpseToFade(NoneFrozen, Config), INDEX_NONE);

	// A tie in distance keeps the older; pool 1 = the oldest.
	TArray<FCorpseCandidate> Tie = { Body(9.0, 40.f), Body(3.0, 40.f) };
	TestEqual(TEXT("Tie: the older"), PickCorpseToFade(Tie, Config), 1);
	FCorpseConfig PoolOne;
	PoolOne.OldestPool = 1;
	TestEqual(TEXT("Pool 1: strictly the oldest"), PickCorpseToFade(Bodies, PoolOne), 4);
	return true;
}

CORPSE_TEST(FCorpseFadeTest, "FadeAndLevelCap")
bool FCorpseFadeTest::RunTest(const FString&)
{
	using namespace CorpseRules;
	TestEqual(TEXT("Full size at the start"), FadeScale(0.f, 1.f), 1.f);
	TestTrue(TEXT("Half way: shrinking"), FadeScale(0.5f, 1.f) > 0.f && FadeScale(0.5f, 1.f) < 1.f);
	TestTrue(TEXT("Monotonic"), FadeScale(0.75f, 1.f) < FadeScale(0.25f, 1.f));
	TestEqual(TEXT("Gone at the end"), FadeScale(1.f, 1.f), 0.f);
	TestEqual(TEXT("No fade time: gone at once"), FadeScale(0.f, 0.f), 0.f);
	TestEqual(TEXT("Console cap wins"), ResolveCap(150, 3), 3);
	TestEqual(TEXT("Console -1: the level's"), ResolveCap(80, -1), 80);
	TestEqual(TEXT("At least 1"), ResolveCap(0, -1), 1);

	FLevelCombatConfig Level;
	FString Error;
	TestTrue(TEXT("Level parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"t\", \"corpse_cap\": 40, \"waves\": []}"), TEXT("t"), Level, Error));
	TestEqual(TEXT("corpse_cap 40"), Level.CorpseCap, 40);
	FLevelCombatConfig Plain;
	TestTrue(TEXT("Plain parsed"), LevelJsonRules::ParseLevel(TEXT("{\"level_id\": \"p\", \"waves\": []}"), TEXT("p"), Plain, Error));
	TestEqual(TEXT("Default 150"), Plain.CorpseCap, 150);
	return true;
}

#undef CORPSE_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
