// CodexTactics.Formation.Runtime.Yield.* - a formation follower out of the leader's own walk (bug 2026-10-28: CoverSmoke's cover walk along
// the wall ended Blocked at velocity 0 once the squad kept its formation in the fight - a follower stood on the leader's way):
// FormationRuntime::YieldFromLeaderWay. In-game check: CoverSmoke (default CVars; Codex.Formation.YieldToLeader 0 = the old follow).
// UE-only (no Godot reference).

#include "Misc/AutomationTest.h"
#include "Formation/FormationRuntimeRules.h"

#if WITH_DEV_AUTOMATION_TESTS

#define FORMATION_YIELD_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Formation.Runtime.Yield." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FORMATION_YIELD_TEST(FFormationYieldInTheWayTest, "FollowerOnTheWayStepsAside")
bool FFormationYieldInTheWayTest::RunTest(const FString&)
{
	// The leader walks +X 550 cm along a wall; a follower stands 200 cm ahead, 30 cm left of the way.
	const FVector2D Leader(0.0, 0.0);
	const FVector2D Goal(550.0, 0.0);
	const TArray<FVector2D> Aside = FormationRuntime::YieldFromLeaderWay(Leader, Goal, FVector2D(200.0, 30.0), FVector2D(200.0, 30.0));
	TestEqual(TEXT("two points: his own side first, then the other"), Aside.Num(), 2);
	if (Aside.Num() == 2)
	{
		TestTrue(TEXT("his side (left): beside him, ClearCm off the way"), Aside[0].Equals(FVector2D(200.0, FormationRuntime::YieldClearCm), 0.01));
		TestTrue(TEXT("the other side"), Aside[1].Equals(FVector2D(200.0, -FormationRuntime::YieldClearCm), 0.01));
	}
	// Standing clear of his way, but his slot lies on it: he waits beside the slot, not on it.
	const TArray<FVector2D> SlotOnWay = FormationRuntime::YieldFromLeaderWay(Leader, Goal, FVector2D(300.0, -300.0), FVector2D(300.0, -10.0));
	TestTrue(TEXT("slot on the way: beside it on its side (right)"), SlotOnWay.Num() == 2 && SlotOnWay[0].Equals(FVector2D(300.0, -FormationRuntime::YieldClearCm), 0.01));
	return true;
}

FORMATION_YIELD_TEST(FFormationYieldClearTest, "ClearOfTheWayNoYield")
bool FFormationYieldClearTest::RunTest(const FString&)
{
	const FVector2D Leader(0.0, 0.0);
	const FVector2D Goal(550.0, 0.0);
	TestEqual(TEXT("behind the leader: no yield"), FormationRuntime::YieldFromLeaderWay(Leader, Goal, FVector2D(-100.0, 0.0), FVector2D(-150.0, 0.0)).Num(), 0);
	TestEqual(TEXT("beside the way (beyond the corridor): no yield"),
		FormationRuntime::YieldFromLeaderWay(Leader, Goal, FVector2D(200.0, 100.0), FVector2D(250.0, -120.0)).Num(), 0);
	TestEqual(TEXT("past his goal and the corridor's margin: no yield"),
		FormationRuntime::YieldFromLeaderWay(Leader, Goal, FVector2D(700.0, 0.0), FVector2D(700.0, 0.0)).Num(), 0);
	TestEqual(TEXT("far ahead on a long walk (beyond AheadCm): not yet"),
		FormationRuntime::YieldFromLeaderWay(Leader, FVector2D(3000.0, 0.0), FVector2D(1500.0, 0.0), FVector2D(1500.0, 0.0)).Num(), 0);
	TestEqual(TEXT("no walk (goal = the leader): no yield"), FormationRuntime::YieldFromLeaderWay(Leader, Leader, FVector2D(50.0, 0.0), FVector2D(50.0, 0.0)).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
