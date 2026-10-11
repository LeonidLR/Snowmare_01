// CodexTactics.AI.Repath.* — when an enemy re-issues its navigation move (audit 2026-10-11 #1; UE-only, no Godot
// reference): RepathRules::ClassifyFollowState and RepathRules::ShouldRepath.

#include "AI/RepathRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#define REPATH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.AI.Repath." TestPath, EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace RepathTest
{
	FRepathGoal Location(const FVector& Where, float Radius = 50.f)
	{
		FRepathGoal Goal;
		Goal.Location = Where;
		Goal.AcceptanceRadius = Radius;
		return Goal;
	}

	FRepathGoal Actor(uint32 Id, const FVector& Where, float Radius = 90.f)
	{
		FRepathGoal Goal = Location(Where, Radius);
		Goal.GoalActorId = Id;
		return Goal;
	}
}

REPATH_TEST(FRepathFollowStateTest, "FollowState")
bool FRepathFollowStateTest::RunTest(const FString&)
{
	using namespace RepathRules;
	TestTrue(TEXT("No move yet"), ClassifyFollowState(false, true, false, false, false, false, false) == ERepathFollowState::None);
	TestTrue(TEXT("Walking our move"), ClassifyFollowState(true, false, true, false, false, false, false) == ERepathFollowState::Following);
	TestTrue(TEXT("Walking someone else's move (patrol walk / script)"), ClassifyFollowState(true, false, false, false, false, false, false) ==
		ERepathFollowState::Stopped);
	TestTrue(TEXT("MoveTo failed (no path, refused)"), ClassifyFollowState(true, true, false, true, true, false, false) == ERepathFollowState::Failed);
	TestTrue(TEXT("Arrived / already at the goal"), ClassifyFollowState(true, true, false, false, true, true, false) == ERepathFollowState::Reached);
	TestTrue(TEXT("StopMovement / aborted"), ClassifyFollowState(true, true, false, false, true, false, true) == ERepathFollowState::Stopped);
	TestTrue(TEXT("Blocked / off the path"), ClassifyFollowState(true, true, false, false, true, false, false) == ERepathFollowState::Failed);
	TestTrue(TEXT("Idle, our move's end not recorded"), ClassifyFollowState(true, true, false, false, false, false, false) == ERepathFollowState::Stopped);
	return true;
}

REPATH_TEST(FRepathGoalChangeTest, "GoalChange")
bool FRepathGoalChangeTest::RunTest(const FString&)
{
	using namespace RepathRules;
	using namespace RepathTest;
	const FRepathSettings Settings; // 0.3 s, 75 cm
	const FRepathGoal Spot = Location(FVector(1000.f, 0.f, 0.f));
	TestTrue(TEXT("First ask is issued"), ShouldRepath(nullptr, Spot, ERepathFollowState::None, 0.0, false, Settings));
	TestFalse(TEXT("Same spot while walking: no new pathfind"), ShouldRepath(&Spot, Spot, ERepathFollowState::Following, 5.0, false, Settings));
	TestFalse(TEXT("Spot moved 50 cm (< 75): kept"), ShouldRepath(&Spot, Location(FVector(1050.f, 0.f, 0.f)), ERepathFollowState::Following, 5.0, false, Settings));
	TestTrue(TEXT("Spot moved 2 m, interval passed: re-issued"), ShouldRepath(&Spot, Location(FVector(1200.f, 0.f, 0.f)), ERepathFollowState::Following, 0.3, false, Settings));
	TestFalse(TEXT("Spot moved 2 m within 0.3 s: waits for the interval"),
		ShouldRepath(&Spot, Location(FVector(1200.f, 0.f, 0.f)), ERepathFollowState::Following, 0.1, false, Settings));
	TestTrue(TEXT("Other acceptance radius: another move"), ShouldRepath(&Spot, Location(FVector(1000.f, 0.f, 0.f), 80.f), ERepathFollowState::Following, 0.0, false, Settings));

	const FRepathGoal Hound = Actor(7, FVector(500.f, 0.f, 0.f));
	TestTrue(TEXT("Location -> actor goal: at once"), ShouldRepath(&Spot, Hound, ERepathFollowState::Following, 0.0, false, Settings));
	TestTrue(TEXT("Actor -> location goal: at once"), ShouldRepath(&Hound, Spot, ERepathFollowState::Following, 0.0, false, Settings));
	TestTrue(TEXT("Another target actor: at once"), ShouldRepath(&Hound, Actor(8, FVector(500.f, 0.f, 0.f)), ERepathFollowState::Following, 0.0, false, Settings));
	TestFalse(TEXT("Chasing the same actor that ran 5 m: the engine updates the path"),
		ShouldRepath(&Hound, Actor(7, FVector(1000.f, 0.f, 0.f)), ERepathFollowState::Following, 5.0, false, Settings));
	return true;
}

REPATH_TEST(FRepathPathStatusTest, "PathStatus")
bool FRepathPathStatusTest::RunTest(const FString&)
{
	using namespace RepathRules;
	using namespace RepathTest;
	const FRepathSettings Settings;
	const FRepathGoal Spot = Location(FVector(1000.f, 0.f, 0.f));
	const FRepathGoal Prey = Actor(7, FVector(500.f, 0.f, 0.f));
	TestTrue(TEXT("Stopped (attack lock, barricade, stagger): resumes at once"), ShouldRepath(&Spot, Spot, ERepathFollowState::Stopped, 0.0, false, Settings));
	TestTrue(TEXT("Stopped chase: resumes at once"), ShouldRepath(&Prey, Prey, ERepathFollowState::Stopped, 0.0, false, Settings));
	TestFalse(TEXT("Failed: no retry within the interval"), ShouldRepath(&Spot, Spot, ERepathFollowState::Failed, 0.1, false, Settings));
	TestFalse(TEXT("Failed: a moved goal still waits for the interval"),
		ShouldRepath(&Spot, Location(FVector(3000.f, 0.f, 0.f)), ERepathFollowState::Failed, 0.1, false, Settings));
	TestTrue(TEXT("Failed: retried after the interval"), ShouldRepath(&Spot, Spot, ERepathFollowState::Failed, 0.3, false, Settings));
	TestFalse(TEXT("Reached, pushed out of reach, same goal: waits for the interval"), ShouldRepath(&Spot, Spot, ERepathFollowState::Reached, 0.1, false, Settings));
	TestTrue(TEXT("Reached, the interval passed: asks again (pushed off the spot)"), ShouldRepath(&Spot, Spot, ERepathFollowState::Reached, 0.3, false, Settings));
	TestTrue(TEXT("Walking an actor chase, now within its reach: issued (already-at-goal finish, no pathfind)"),
		ShouldRepath(&Prey, Prey, ERepathFollowState::Following, 0.0, true, Settings));
	TestTrue(TEXT("Reached and still within reach: asked every tick as before (no pathfind)"),
		ShouldRepath(&Spot, Spot, ERepathFollowState::Reached, 0.0, true, Settings));
	TestTrue(TEXT("Reached, the prey walked 1 m off: at once"),
		ShouldRepath(&Prey, Actor(7, FVector(600.f, 0.f, 0.f)), ERepathFollowState::Reached, 0.0, false, Settings));
	return true;
}

REPATH_TEST(FRepathSettingsTest, "Settings")
bool FRepathSettingsTest::RunTest(const FString&)
{
	using namespace RepathRules;
	using namespace RepathTest;
	const FRepathGoal Spot = Location(FVector(1000.f, 0.f, 0.f));
	FRepathSettings Off;
	Off.bEnabled = false;
	TestTrue(TEXT("Throttle off: every ask issued (old behaviour)"), ShouldRepath(&Spot, Spot, ERepathFollowState::Following, 0.0, false, Off));
	FRepathSettings Slow;
	Slow.IntervalSeconds = 1.f;
	Slow.MinGoalDeltaCm = 200.f;
	TestFalse(TEXT("1 s interval: 0.5 s is too early"), ShouldRepath(&Spot, Spot, ERepathFollowState::Failed, 0.5, false, Slow));
	TestFalse(TEXT("200 cm delta: a 150 cm move is the same goal"),
		ShouldRepath(&Spot, Location(FVector(1150.f, 0.f, 0.f)), ERepathFollowState::Following, 5.0, false, Slow));
	FRepathSettings Eager;
	Eager.IntervalSeconds = 0.f;
	Eager.MinGoalDeltaCm = 0.f;
	TestTrue(TEXT("Zero interval / delta: any moved location goal re-issued"),
		ShouldRepath(&Spot, Location(FVector(1001.f, 0.f, 0.f)), ERepathFollowState::Following, 0.0, false, Eager));
	TestFalse(TEXT("Zero interval / delta: an unchanged walked goal still is not"), ShouldRepath(&Spot, Spot, ERepathFollowState::Following, 0.0, false, Eager));
	return true;
}

#undef REPATH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
