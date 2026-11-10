#include "Misc/AutomationTest.h"
#include "AI/PmcArenaRoundRules.h"
#include "AI/PmcCommanderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// fix/arena-rpg-contact-roof 2026-11-10 (the PMC arena's two known gaps after fix/pmc-arena-stalemate, docs/systems/pmc-squad-ai.md 11):
// an RPG-7 rocket is contact (a round of rockets only was a no_contact draw), and an objective nobody can walk to (a roof without a
// ladder route) is never placed. Pure rules (AI/PmcArenaRoundRules.h).

namespace PmcArenaRoundRulesTest
{
	/** suppression.json launcher.radius_cm (the burst's suppression reach = the contact reach). */
	constexpr float LauncherRadiusCm = 900.f;

	/**
	 * A toy navmesh: the ground (Z < 200) is one connected area; a roof (Z >= 200) is reached only from the roof itself - except a
	 * one-way drop link from the roof at X >= 5000 down to the ground (roof -> ground, never ground -> roof).
	 */
	bool ToyFullPath(const FVector& From, const FVector& To)
	{
		const bool bFromRoof = From.Z >= 200.f;
		const bool bToRoof = To.Z >= 200.f;
		if (bFromRoof == bToRoof)
		{
			return true;
		}
		return bFromRoof && !bToRoof && From.X >= 5000.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaRocketContactTest, "CodexTactics.AI.PmcArena.RocketContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaRocketContactTest::RunTest(const FString&)
{
	using namespace PmcArenaRoundRulesTest;
	const TArray<FVector> Enemies = { FVector(3000.f, 0.f, 0.f), FVector(-4000.f, 2000.f, 0.f) };
	TestTrue(TEXT("a burst 5 m from an enemy is contact"), PmcArenaRoundRules::IsLauncherContact(FVector(3500.f, 0.f, 0.f), Enemies, LauncherRadiusCm));
	TestTrue(TEXT("exactly at the radius is contact"), PmcArenaRoundRules::IsLauncherContact(FVector(3000.f, 900.f, 0.f), Enemies, LauncherRadiusCm));
	TestFalse(TEXT("a burst 9.5 m from the nearest enemy is not"), PmcArenaRoundRules::IsLauncherContact(FVector(3000.f, 950.f, 0.f), Enemies, LauncherRadiusCm));
	TestFalse(TEXT("the radius is 3D: a burst on a roof 10 m above him is not"), PmcArenaRoundRules::IsLauncherContact(FVector(3000.f, 0.f, 1000.f), Enemies, LauncherRadiusCm));
	TestTrue(TEXT("any enemy counts (the second one)"), PmcArenaRoundRules::IsLauncherContact(FVector(-4100.f, 2300.f, 0.f), Enemies, LauncherRadiusCm));
	TestFalse(TEXT("no live enemy: no contact"), PmcArenaRoundRules::IsLauncherContact(FVector::ZeroVector, TArray<FVector>(), LauncherRadiusCm));
	TestFalse(TEXT("radius 0: never"), PmcArenaRoundRules::IsLauncherContact(FVector(3000.f, 0.f, 0.f), Enemies, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaContactFlowTest, "CodexTactics.AI.PmcArena.ContactFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaContactFlowTest::RunTest(const FString&)
{
	// No contact moment at all: no contact, the whole round one quiet spell.
	{
		const FPmcArenaFlow Flow = PmcArenaRoundRules::ComputeFlow(TArray<float>(), 120.f);
		TestFalse(TEXT("empty: no contact"), Flow.bContact);
		TestEqual(TEXT("empty: first contact -1"), Flow.FirstContactS, -1.f);
		TestEqual(TEXT("empty: the round is one quiet spell"), Flow.LongestQuietS, 120.f);
		TestEqual(TEXT("empty: 0 per minute"), Flow.EventsPerMin, 0.f);
	}
	// The RPG-only round (the gap): the shots list is empty, the rocket bursts near an enemy are the contact moments - merged unsorted.
	{
		const TArray<float> Shots;
		const TArray<float> RocketBursts = { 70.f, 40.f };
		TArray<float> Moments = Shots;
		Moments.Append(RocketBursts);
		const FPmcArenaFlow Flow = PmcArenaRoundRules::ComputeFlow(Moments, 120.f);
		TestTrue(TEXT("rockets only: contact"), Flow.bContact);
		TestEqual(TEXT("rockets only: first contact at the first burst"), Flow.FirstContactS, 40.f);
		TestEqual(TEXT("rockets only: the longest quiet spell = the tail 70 -> 120 s"), Flow.LongestQuietS, 50.f);
		TestEqual(TEXT("rockets only: 2 moments"), Flow.Events, 2);
		TestEqual(TEXT("rockets only: 1 per minute over 120 s"), Flow.EventsPerMin, 1.f);
		const FPmcObjectiveClock Clock;
		const FPmcRoundOutcome NoContact = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true, false);
		const FPmcRoundOutcome RocketContact = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true, Flow.bContact);
		TestEqual(TEXT("without the rockets the time limit is a no_contact draw"), NoContact.ObjectiveResult, FString(TEXT("no_contact")));
		TestNotEqual(TEXT("with the rocket contact it is not a no_contact round"), RocketContact.ObjectiveResult, FString(TEXT("no_contact")));
	}
	// Shots and a burst fill a quiet spell: 10, 20 (shots), 60 (a burst), 65 (a shot) over 90 s - the longest gap 20 -> 60 = 40 s.
	{
		const FPmcArenaFlow Flow = PmcArenaRoundRules::ComputeFlow({ 65.f, 10.f, 60.f, 20.f }, 90.f);
		TestEqual(TEXT("mixed: first contact 10 s"), Flow.FirstContactS, 10.f);
		TestEqual(TEXT("mixed: longest quiet 40 s"), Flow.LongestQuietS, 40.f);
		const FPmcArenaFlow WithoutBurst = PmcArenaRoundRules::ComputeFlow({ 65.f, 10.f, 20.f }, 90.f);
		TestEqual(TEXT("mixed without the burst: longest quiet 45 s"), WithoutBurst.LongestQuietS, 45.f);
	}
	// A very short round: per minute over at least 1 s.
	{
		const FPmcArenaFlow Flow = PmcArenaRoundRules::ComputeFlow({ 0.2f }, 0.5f);
		TestEqual(TEXT("short: 60 per minute (1 s floor)"), Flow.EventsPerMin, 60.f);
		TestEqual(TEXT("short: the tail"), Flow.LongestQuietS, 0.3f, 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaObjectiveReachTest, "CodexTactics.AI.PmcArena.ObjectiveReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaObjectiveReachTest::RunTest(const FString&)
{
	using namespace PmcArenaRoundRulesTest;
	TestTrue(TEXT("found, not partial = a full path"), PmcArenaRoundRules::IsFullPath(true, false));
	TestFalse(TEXT("a partial path is no route"), PmcArenaRoundRules::IsFullPath(true, true));
	TestFalse(TEXT("no path"), PmcArenaRoundRules::IsFullPath(false, false));

	const FVector Ground(0.f, 0.f, 0.f);
	const FVector GroundFar(6000.f, 0.f, 0.f);
	const FVector Roof(2000.f, 0.f, 600.f);
	const FVector RoofDrop(5500.f, 0.f, 600.f);
	TestTrue(TEXT("a ground point, both spawns on the ground"), PmcArenaRoundRules::IsReachableFromBoth(FVector(3000.f, 0.f, 0.f), Ground, GroundFar, ToyFullPath));
	TestFalse(TEXT("a roof without a ladder route: the ground spawns cannot reach it"), PmcArenaRoundRules::IsReachableFromBoth(Roof, Ground, GroundFar, ToyFullPath));
	TestFalse(TEXT("one side on that roof is not enough - the other side must reach it too"), PmcArenaRoundRules::IsReachableFromBoth(Roof, Roof, GroundFar, ToyFullPath));
	TestTrue(TEXT("both spawns on the roof"), PmcArenaRoundRules::IsReachableFromBoth(Roof, Roof, FVector(2500.f, 300.f, 600.f), ToyFullPath));
	// The direction matters: a drop link leads off the roof, never up it.
	TestTrue(TEXT("roof -> ground over the drop link"), ToyFullPath(RoofDrop, Ground));
	TestFalse(TEXT("a ground point is reached from a drop-link roof spawn, but the roof is not reached from the ground"),
		PmcArenaRoundRules::IsReachableFromBoth(RoofDrop, RoofDrop, Ground, ToyFullPath));
	TestTrue(TEXT("a ground point from a drop-link roof spawn and a ground spawn"), PmcArenaRoundRules::IsReachableFromBoth(GroundFar, RoofDrop, Ground, ToyFullPath));

	// The placement: the first candidate reachable from both wins; the roof ones are skipped; none -> INDEX_NONE.
	const TArray<FVector> Candidates = { Roof, RoofDrop, FVector(3000.f, 500.f, 0.f), FVector(1000.f, 0.f, 0.f) };
	TestEqual(TEXT("the roof candidates are passed over"), PmcArenaRoundRules::PickReachable(Candidates, Ground, GroundFar, ToyFullPath), 2);
	TestEqual(TEXT("only roofs: none"), PmcArenaRoundRules::PickReachable({ Roof, RoofDrop }, Ground, GroundFar, ToyFullPath), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("no candidates: none"), PmcArenaRoundRules::PickReachable(TArray<FVector>(), Ground, GroundFar, ToyFullPath), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("both on the roof: the first roof point"), PmcArenaRoundRules::PickReachable(Candidates, Roof, FVector(2500.f, 300.f, 600.f), ToyFullPath), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
