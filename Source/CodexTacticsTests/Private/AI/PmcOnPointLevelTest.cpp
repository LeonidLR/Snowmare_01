#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// fix/pmc-onpoint-3d-detour 2026-11-11 (Gemini review 009 of the pmc-v3 night, recommendations 1-2, approved by the game designer;
// docs/systems/pmc-squad-ai.md section 13): the on-point / arrival tests are level-aware (a roof point at z 910 no longer counts the men
// standing on the ground under it), and a stalled walk re-routes by a side waypoint, then the other side, then a short step back.

namespace PmcOnPointLevelTestLocal
{
	/** A roof objective: the navmesh point on a roof 9 m up (the Depot's level 3), radius 6 m. */
	FPmcObjectiveView OnPointRoof(EPmcRole Role)
	{
		FPmcObjectiveView Objective;
		Objective.Role = Role;
		Objective.Location = FVector(1000.f, 0.f, 910.f);
		Objective.RadiusCm = 600.f;
		Objective.LevelToleranceCm = 150.f;
		return Objective;
	}

	FPmcSquadView OnPointSquad()
	{
		FPmcSquadView View;
		View.Alive = 3;
		View.EnemiesKnown = 3;
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaOnPointLevelTest, "CodexTactics.AI.PmcArena.OnPointLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaOnPointLevelTest::RunTest(const FString&)
{
	using namespace PmcOnPointLevelTestLocal;
	const FVector Point(1000.f, 0.f, 910.f);
	// Same level: within one level up or down (feet vs the navmesh point; a stair step, the navmesh's few cm above the floor).
	TestTrue(TEXT("the same floor"), PmcCommanderRules::IsSameLevel(905.f, 910.f, 150.f));
	TestTrue(TEXT("150 cm is still the same level"), PmcCommanderRules::IsSameLevel(760.f, 910.f, 150.f));
	TestFalse(TEXT("one floor (3 m) below is not"), PmcCommanderRules::IsSameLevel(610.f, 910.f, 150.f));
	TestFalse(TEXT("the ground under a roof is not"), PmcCommanderRules::IsSameLevel(10.f, 910.f, 150.f));
	TestTrue(TEXT("tolerance <= 0: planar (the old test)"), PmcCommanderRules::IsSameLevel(10.f, 910.f, 0.f));
	// On the point: within the radius AND on its level.
	TestTrue(TEXT("on the roof within the radius: on the point"), PmcCommanderRules::IsOnPoint(FVector(1200.f, 100.f, 905.f), Point, 600.f, 150.f));
	TestFalse(TEXT("on the roof outside the radius: off"), PmcCommanderRules::IsOnPoint(FVector(1700.f, 0.f, 905.f), Point, 600.f, 150.f));
	TestFalse(TEXT("on the ground right under the roof point (the 155 night draws): off"),
		PmcCommanderRules::IsOnPoint(FVector(1100.f, 0.f, 10.f), Point, 600.f, 150.f));
	TestFalse(TEXT("one floor below: off"), PmcCommanderRules::IsOnPoint(FVector(1000.f, 0.f, 610.f), Point, 600.f, 150.f));
	// The level-aware distance: planar on the level; off it at least the floor given and at least planar + the climb.
	TestEqual(TEXT("same level: planar"), PmcCommanderRules::LevelAwareDistanceCm(FVector(1300.f, 400.f, 900.f), Point, 150.f, 601.f), 500.f, 0.01f);
	TestEqual(TEXT("under the roof: planar + the climb"), PmcCommanderRules::LevelAwareDistanceCm(FVector(1100.f, 0.f, 10.f), Point, 150.f, 601.f), 1000.f, 0.01f);
	TestEqual(TEXT("one floor below, 1 m off: at least the floor (never inside the radius)"),
		PmcCommanderRules::LevelAwareDistanceCm(FVector(1100.f, 0.f, 610.f), Point, 150.f, 601.f), 601.f, 0.01f);
	TestEqual(TEXT("tolerance <= 0: planar"), PmcCommanderRules::LevelAwareDistanceCm(FVector(1100.f, 0.f, 10.f), Point, 0.f, 601.f), 100.f, 0.01f);
	// The commander's objective distance: a man under the roof is outside the radius.
	const FPmcObjectiveView Roof = OnPointRoof(EPmcRole::Attacker);
	const float Under = PmcCommanderRules::ObjectiveDistanceCm(FVector(1100.f, 0.f, 10.f), Roof);
	TestTrue(TEXT("under the roof: the objective distance is outside the radius"), Under > Roof.RadiusCm);
	TestEqual(TEXT("on the roof: planar"), PmcCommanderRules::ObjectiveDistanceCm(FVector(1100.f, 0.f, 905.f), Roof), 100.f, 0.01f);

	// The commander: the attacker under the roof point still walks onto it (the Objective order scores) and gets no on-point Hold bonus.
	const FPmcWeights Weights;
	FPmcFighterView Below;
	Below.Objective = Roof;
	Below.Objective.TimeFrac = 1.f;
	Below.Objective.DistanceCm = Under;
	FPmcFighterView OnRoof = Below;
	OnRoof.Objective.DistanceCm = 100.f;
	FPmcFighterView Plain;
	TestTrue(TEXT("an attacker under a roof point: the Objective walk scores"),
		PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Below, OnPointSquad(), Weights) > 0.5f);
	TestEqual(TEXT("an attacker on the roof point: no walk"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, OnRoof, OnPointSquad(), Weights), 0.f);
	TestEqual(TEXT("under the roof: Hold without the on-point bonus"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Below, OnPointSquad(), Weights),
		PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Plain, OnPointSquad(), Weights), 0.001f);
	// A defender under his roof point late in the round goes back up (Occupy) instead of counting as on it.
	FPmcFighterView Defender = Below;
	Defender.Objective.Role = EPmcRole::Defender;
	Defender.Objective.TimeFrac = 0.9f;
	TestTrue(TEXT("a defender under his roof point late: back onto it"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Defender, OnPointSquad(), Weights) > 0.5f);

	// A cover slot under the roof is no "slot on the point" for an attacker; one on the roof is.
	const FVector Far(4000.f, 0.f, 10.f);
	const float SlotUnder = PmcCommanderRules::ObjectivePositionFactor(FVector(1100.f, 0.f, 10.f), Far, Roof, FPmcEngagementBand(), 2500.f, Weights);
	const float SlotOnRoof = PmcCommanderRules::ObjectivePositionFactor(FVector(1100.f, 0.f, 910.f), Far, Roof, FPmcEngagementBand(), 2500.f, Weights);
	TestEqual(TEXT("a slot on the roof point: x (1 + objective_approach)"), SlotOnRoof, 1.f + Weights.ObjectiveApproach, 0.001f);
	TestTrue(TEXT("a slot under the roof point scores below one on it"), SlotUnder < SlotOnRoof - 0.1f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderWalkArrivalLevelTest, "CodexTactics.AI.PmcCommander.WalkArrivalLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderWalkArrivalLevelTest::RunTest(const FString&)
{
	const FVector RoofGoal(1000.f, 0.f, 910.f);
	// The walk's "ended short" (4 m) and "goes nowhere" (2.5 m) tests: planar AND the level.
	TestFalse(TEXT("arrived on the roof"), PmcCommanderRules::IsWalkShort(FVector(1200.f, 0.f, 905.f), RoofGoal, 400.f, 150.f));
	TestTrue(TEXT("ended 5 m off on the roof: short"), PmcCommanderRules::IsWalkShort(FVector(1500.f, 0.f, 905.f), RoofGoal, 400.f, 150.f));
	TestTrue(TEXT("ended on the ground under the roof goal: short (the night's 'arrived' attackers)"),
		PmcCommanderRules::IsWalkShort(FVector(1100.f, 0.f, 10.f), RoofGoal, 400.f, 150.f));
	TestTrue(TEXT("a roof goal over his head is a walk to take (not 'goes nowhere')"),
		PmcCommanderRules::IsWalkShort(FVector(1000.f, 100.f, 10.f), RoofGoal, 250.f, 150.f));
	TestFalse(TEXT("tolerance <= 0: planar (the old test)"), PmcCommanderRules::IsWalkShort(FVector(1100.f, 0.f, 10.f), RoofGoal, 400.f, 0.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderDetourTest, "CodexTactics.AI.PmcCommander.Detour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderDetourTest::RunTest(const FString&)
{
	FPmcWeights Weights;
	// The steps: a side waypoint, then the other side, then a short step back; then the order fails.
	TestTrue(TEXT("stall 1: a side"), PmcCommanderRules::NextDetourStep(0, 0, Weights.DetourMax) == EPmcDetourStep::Side);
	TestTrue(TEXT("stall 2 after the left side: the other side"), PmcCommanderRules::NextDetourStep(1, 1, Weights.DetourMax) == EPmcDetourStep::OtherSide);
	TestTrue(TEXT("stall 3: the step back"), PmcCommanderRules::NextDetourStep(2, 1, Weights.DetourMax) == EPmcDetourStep::StepBack);
	TestTrue(TEXT("stall 4: none (the order fails)"), PmcCommanderRules::NextDetourStep(3, 1, Weights.DetourMax) == EPmcDetourStep::None);
	TestTrue(TEXT("no side found at all: straight to the step back"), PmcCommanderRules::NextDetourStep(1, 0, Weights.DetourMax) == EPmcDetourStep::StepBack);
	TestTrue(TEXT("no side found, step back done: none"), PmcCommanderRules::NextDetourStep(2, 0, Weights.DetourMax) == EPmcDetourStep::None);
	TestTrue(TEXT("detour_max 1: one side detour only (the old rule)"), PmcCommanderRules::NextDetourStep(1, 1, 1) == EPmcDetourStep::None);
	TestTrue(TEXT("detour_max 0: no detour"), PmcCommanderRules::NextDetourStep(0, 0, 0) == EPmcDetourStep::None);

	// The candidates. Travel +X: the left side is +Y.
	const FVector ToGoal(1.f, 0.f, 0.f);
	TArray<FPmcDetourCandidate> Sides;
	PmcCommanderRules::DetourCandidates(ToGoal, ToGoal, EPmcDetourStep::Side, 0, Weights, Sides);
	TestEqual(TEXT("six side candidates (the old ring)"), Sides.Num(), 6);
	if (Sides.Num() == 6)
	{
		TestTrue(TEXT("first: 6 m left, leaning 3 m to the goal"), Sides[0].Offset.Equals(FVector(300.f, 600.f, 0.f), 0.5f) && Sides[0].Side == 1);
		TestTrue(TEXT("second: the same on the right"), Sides[1].Offset.Equals(FVector(300.f, -600.f, 0.f), 0.5f) && Sides[1].Side == -1);
		TestTrue(TEXT("then 9 m off"), Sides[2].Offset.Equals(FVector(0.f, 900.f, 0.f), 0.5f));
		TestTrue(TEXT("then back-diagonal"), Sides[4].Offset.Equals(FVector(-500.f, 500.f, 0.f), 1.f));
	}
	TArray<FPmcDetourCandidate> Other;
	PmcCommanderRules::DetourCandidates(ToGoal, ToGoal, EPmcDetourStep::OtherSide, 1, Weights, Other);
	TestEqual(TEXT("the other side: three candidates"), Other.Num(), 3);
	bool bAllRight = Other.Num() > 0;
	for (const FPmcDetourCandidate& Candidate : Other)
	{
		bAllRight &= Candidate.Side == -1 && Candidate.Offset.Y < 0.f;
	}
	TestTrue(TEXT("after the left side every candidate lies right of the travel"), bAllRight);
	// The step back: along the way he came (the arrival vector, not the goal's direction), 50-100 cm.
	const FVector Arrival(0.f, 1.f, 0.f);
	TArray<FPmcDetourCandidate> Back;
	PmcCommanderRules::DetourCandidates(ToGoal, Arrival, EPmcDetourStep::StepBack, 1, Weights, Back);
	TestTrue(TEXT("step back candidates"), Back.Num() >= 1);
	if (Back.Num() >= 1)
	{
		TestTrue(TEXT("first: 75 cm straight back along the arrival"), Back[0].Offset.Equals(FVector(0.f, -75.f, 0.f), 0.5f));
		bool bShort = true;
		for (const FPmcDetourCandidate& Candidate : Back)
		{
			bShort &= Candidate.Offset.Size2D() >= 49.f && Candidate.Offset.Size2D() <= 101.f && Candidate.Offset.Y < 0.f;
		}
		TestTrue(TEXT("every step back 50-100 cm, backwards"), bShort);
	}
	Weights.DetourSideCm = 400.f;
	TArray<FPmcDetourCandidate> Narrow;
	PmcCommanderRules::DetourCandidates(ToGoal, ToGoal, EPmcDetourStep::Side, 0, Weights, Narrow);
	TestTrue(TEXT("detour_side_cm scales the ring"), Narrow.Num() == 6 && Narrow[0].Offset.Equals(FVector(200.f, 400.f, 0.f), 0.5f));
	TArray<FPmcDetourCandidate> None;
	PmcCommanderRules::DetourCandidates(ToGoal, ToGoal, EPmcDetourStep::None, 0, Weights, None);
	TestEqual(TEXT("no step: no candidates"), None.Num(), 0);
	return true;
}

#endif
