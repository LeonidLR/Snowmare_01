#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// Task #53 (docs/systems/pmc-squad-ai.md 5): the arena's round objective, its scenarios and the commander's objective considerations - pure rules.

namespace PmcArenaObjectiveTestLocal
{
	FPmcFighterView ObjectiveFighter(EPmcRole Role, float DistanceCm, float TimeFrac, bool bEnemyOnPoint = false, float HoldFrac = 0.f)
	{
		FPmcFighterView View;
		View.Objective.Role = Role;
		View.Objective.Location = FVector(DistanceCm, 0.f, 0.f);
		View.Objective.DistanceCm = DistanceCm;
		View.Objective.RadiusCm = 600.f;
		View.Objective.TimeFrac = TimeFrac;
		View.Objective.HoldFrac = HoldFrac;
		View.Objective.bEnemyOnPoint = bEnemyOnPoint;
		return View;
	}

	FPmcSquadView ObjectiveSquad()
	{
		FPmcSquadView View;
		View.Alive = 3;
		View.EnemiesKnown = 3;
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaObjectiveClockTest, "CodexTactics.AI.PmcArena.ObjectiveClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaObjectiveClockTest::RunTest(const FString&)
{
	FPmcObjectiveClock Clock;
	Clock.HoldSeconds = 2.f;
	// An attacker on the point, no defender: the clock runs; 1 s in, not captured.
	for (int32 Step = 0; Step < 10; ++Step)
	{
		TestFalse(TEXT("not captured in the first second"), Clock.Step(1, 0, 0.1f));
	}
	TestTrue(TEXT("the attackers hold it"), Clock.bAttackersHolding);
	TestEqual(TEXT("1 s held"), Clock.Held, 1.f, 0.001f);
	TestEqual(TEXT("half the clock"), Clock.HoldFrac(), 0.5f, 0.001f);
	// Cumulative with a reset (user 2026-11-07): contested or empty only pauses the clock.
	Clock.Step(2, 1, 0.1f);
	TestFalse(TEXT("contested: not held"), Clock.bAttackersHolding);
	TestEqual(TEXT("contested: the clock paused, not reset"), Clock.Held, 1.f, 0.001f);
	Clock.Step(0, 0, 0.3f);
	TestEqual(TEXT("empty point: still paused"), Clock.Held, 1.f, 0.001f);
	TestEqual(TEXT("attacker control 1 s"), Clock.ControlSeconds[0], 1.f, 0.001f);
	// The defenders alone on it: their control grows and the attackers' hold resets.
	Clock.Step(0, 2, 0.5f);
	TestEqual(TEXT("defender control 0.5 s"), Clock.ControlSeconds[1], 0.5f, 0.001f);
	TestEqual(TEXT("defenders retook it: the clock reset"), Clock.Held, 0.f, 0.001f);
	// A 2 s hold (cumulative) captures it - exactly once.
	bool bCapturedNow = false;
	for (int32 Step = 0; Step < 25; ++Step)
	{
		bCapturedNow |= Clock.Step(1, 0, 0.1f);
	}
	TestTrue(TEXT("captured after the hold"), bCapturedNow && Clock.bCaptured);
	TestFalse(TEXT("captured only once"), Clock.Step(1, 0, 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaRoundOutcomeTest, "CodexTactics.AI.PmcArena.RoundOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaRoundOutcomeTest::RunTest(const FString&)
{
	FPmcObjectiveClock Clock;
	// The fight goes on.
	TestFalse(TEXT("both alive, time left: not over"), PmcCommanderRules::ResolveRound(3, 3, false, true, Clock, true).bOver);
	// Elimination wins whatever the objective.
	const FPmcRoundOutcome Wiped = PmcCommanderRules::ResolveRound(2, 0, false, true, Clock, false);
	TestTrue(TEXT("elimination: A wins"), Wiped.bOver && Wiped.Winner == TEXT("A") && Wiped.Reason == TEXT("elimination"));
	TestEqual(TEXT("both down: a draw"), PmcCommanderRules::ResolveRound(0, 0, false, true, Clock, true).Winner, FString(TEXT("draw")));
	// The capture: the attacker wins by objective.
	Clock.bCaptured = true;
	const FPmcRoundOutcome Captured = PmcCommanderRules::ResolveRound(1, 3, false, true, Clock, false);
	TestTrue(TEXT("captured: B (the attacker) wins by objective"), Captured.bOver && Captured.Winner == TEXT("B") && Captured.Reason == TEXT("objective")
		&& Captured.ObjectiveResult == TEXT("captured"));
	// The time limit (fix/pmc-arena-stalemate): the defenders win only standing on the point alone, a draw while the attackers stand on it
	// or nobody does, a no-contact draw when no shot was fired, a draw without an objective.
	Clock = FPmcObjectiveClock();
	const FPmcRoundOutcome Empty = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true);
	TestTrue(TEXT("time limit, nobody on the point: an empty draw (the defender hid off it)"), Empty.bOver && Empty.Winner == TEXT("draw")
		&& Empty.ObjectiveResult == TEXT("empty"));
	Clock.Step(0, 2, 0.1f);
	TestTrue(TEXT("the defenders alone on it: holding"), Clock.bDefendersHolding);
	const FPmcRoundOutcome NoContact = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true, false);
	TestTrue(TEXT("time limit without contact: a no_contact draw even on the point"), NoContact.Winner == TEXT("draw") && NoContact.ObjectiveResult == TEXT("no_contact"));
	const FPmcRoundOutcome Held = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true);
	TestTrue(TEXT("time limit, point held: B (the defender) wins"), Held.bOver && Held.Winner == TEXT("B") && Held.Reason == TEXT("time_limit")
		&& Held.ObjectiveResult == TEXT("held"));
	Clock.Step(1, 0, 0.1f);
	const FPmcRoundOutcome Contested = PmcCommanderRules::ResolveRound(3, 3, true, true, Clock, true);
	TestTrue(TEXT("time limit, attackers on it: contested draw"), Contested.Winner == TEXT("draw") && Contested.ObjectiveResult == TEXT("contested"));
	const FPmcRoundOutcome NoObjective = PmcCommanderRules::ResolveRound(3, 3, true, false, Clock, true);
	TestTrue(TEXT("no objective: a time-limit draw"), NoObjective.bOver && NoObjective.Winner == TEXT("draw") && NoObjective.ObjectiveResult == TEXT("none"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaObjectiveOrderTest, "CodexTactics.AI.PmcCommander.Objective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaObjectiveOrderTest::RunTest(const FString&)
{
	using namespace PmcArenaObjectiveTestLocal;
	const FPmcWeights Weights;
	const FPmcSquadView Squad = ObjectiveSquad();
	// No objective: the order never scores, Hold is unchanged.
	FPmcFighterView Plain;
	TestEqual(TEXT("inactive: no Objective order"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Plain, Squad, Weights), 0.f);
	const float PlainHold = PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, Plain, Squad, Weights);
	// Attacker: urgency grows with the clock (1 - u + u x t), none once on the point.
	const float Early = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Attacker, 3000.f, 0.f), Squad, Weights);
	const float Late = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Attacker, 3000.f, 1.f), Squad, Weights);
	TestEqual(TEXT("attacker at the start: 1 x (1 - 0.65)"), Early, 0.35f, 0.001f);
	TestEqual(TEXT("attacker at the time limit: the full weight"), Late, 1.f, 0.001f);
	TestEqual(TEXT("attacker on the point: no walk"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Attacker, 300.f, 1.f), Squad, Weights), 0.f);
	// On the point the attacker values holding: Hold x (1 + objective_approach).
	TestEqual(TEXT("attacker holds on the point x 1.5"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, ObjectiveFighter(EPmcRole::Attacker, 300.f, 0.f), Squad, Weights),
		PlainHold * 1.5f, 0.001f);
	// Defender: back onto the point while the attackers stand on it (more urgent as their clock runs), back into the overwatch when far.
	const float RetakeEarly = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Defender, 1500.f, 0.f, true, 0.f), Squad, Weights);
	const float RetakeLate = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Defender, 1500.f, 0.f, true, 1.f), Squad, Weights);
	TestEqual(TEXT("defender retake at clock 0: 0.2"), RetakeEarly, 0.2f, 0.001f);
	TestEqual(TEXT("defender retake at the full clock: 1"), RetakeLate, 1.f, 0.001f);
	TestEqual(TEXT("defender within overwatch, nobody on it: stays"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Defender, 900.f, 0.f), Squad, Weights), 0.f);
	TestEqual(TEXT("defender strayed past the overwatch: 0.7"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, ObjectiveFighter(EPmcRole::Defender, 5000.f, 0.f), Squad, Weights), 0.7f, 0.001f);
	TestEqual(TEXT("defender within overwatch holds x 1.5"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, ObjectiveFighter(EPmcRole::Defender, 900.f, 0.f), Squad, Weights),
		PlainHold * 1.5f, 0.001f);
	// The assault point standing in for a threat (no enemy known): a rifleman on it is not "too close" and holds it without the band penalty.
	FPmcFighterView OnPoint = ObjectiveFighter(EPmcRole::Attacker, 200.f, 0.5f);
	OnPoint.bHasThreat = true;
	OnPoint.Threat.Location = OnPoint.Objective.Location;
	OnPoint.Threat.DistanceCm = 200.f;
	OnPoint.Threat.SecondsSince = 999.f;
	OnPoint.Threat.bStandIn = true;
	OnPoint.Band.MinCm = 1900.f;
	OnPoint.Band.MaxCm = 2375.f;
	TestEqual(TEXT("stand-in threat: no standoff"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, OnPoint, Squad, Weights), 0.f);
	TestEqual(TEXT("stand-in threat: Hold without the band penalty"), PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, OnPoint, Squad, Weights),
		Weights.Hold * 0.3f * 0.5f * 1.5f, 0.001f);
	OnPoint.Threat.bStandIn = false;
	TestTrue(TEXT("a real threat that close: the standoff"), PmcCommanderRules::ScoreOrder(EPmcOrder::FallBack, OnPoint, Squad, Weights) > 0.f);
	TestTrue(TEXT("Objective is a committed walk"), PmcCommanderRules::IsMoveOrder(EPmcOrder::Objective));
	TestEqual(TEXT("order name"), FString(PmcCommanderRules::OrderName(EPmcOrder::Objective)), FString(TEXT("Objective")));

	// Positions: an attacker's slot one radius nearer x 1.5, one radius further x 0.5, on the point x 1.5; inactive x 1.
	FPmcObjectiveView Objective;
	Objective.Role = EPmcRole::Attacker;
	Objective.Location = FVector::ZeroVector;
	Objective.RadiusCm = 600.f;
	const FVector Fighter(3000.f, 0.f, 0.f);
	const FPmcEngagementBand NoBand;
	TestEqual(TEXT("attacker: a slot nearer"), PmcCommanderRules::ObjectivePositionFactor(FVector(2400.f, 0.f, 0.f), Fighter, Objective, NoBand, 2500.f, Weights), 1.5f, 0.001f);
	TestEqual(TEXT("attacker: a slot further"), PmcCommanderRules::ObjectivePositionFactor(FVector(3600.f, 0.f, 0.f), Fighter, Objective, NoBand, 2500.f, Weights), 0.5f, 0.001f);
	TestEqual(TEXT("attacker: a slot on the point"), PmcCommanderRules::ObjectivePositionFactor(FVector(300.f, 0.f, 0.f), Fighter, Objective, NoBand, 2500.f, Weights), 1.5f, 0.001f);
	// Defender: within the overwatch (the band's max, at least 2 x the radius) x 1.5, falling to x 1 at twice that.
	Objective.Role = EPmcRole::Defender;
	FPmcEngagementBand Band;
	Band.MinCm = 1900.f;
	Band.MaxCm = 2375.f;
	TestEqual(TEXT("overwatch range = the band's max"), PmcCommanderRules::OverwatchRangeCm(Objective, Band, 2500.f), 2375.f, 0.001f);
	TestEqual(TEXT("overwatch range without a band: 0.8 x reach, >= 2 x radius"), PmcCommanderRules::OverwatchRangeCm(Objective, NoBand, 1000.f), 1200.f, 0.001f);
	TestEqual(TEXT("defender: a slot overwatching the point"), PmcCommanderRules::ObjectivePositionFactor(FVector(2000.f, 0.f, 0.f), Fighter, Objective, Band, 2500.f, Weights), 1.5f, 0.001f);
	TestEqual(TEXT("defender: a slot far off"), PmcCommanderRules::ObjectivePositionFactor(FVector(4750.f, 0.f, 0.f), Fighter, Objective, Band, 2500.f, Weights), 1.f, 0.001f);
	Objective.Role = EPmcRole::None;
	TestEqual(TEXT("inactive: x 1"), PmcCommanderRules::ObjectivePositionFactor(FVector(2400.f, 0.f, 0.f), Fighter, Objective, Band, 2500.f, Weights), 1.f);

	// The weights file reads the four genes.
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"objective\": 1.2, \"objective_urgency\": 0.4, \"objective_approach\": 0.3, \"overwatch\": 0.7}")), Json);
	if (!TestTrue(TEXT("json parsed"), Json.IsValid()))
	{
		return false;
	}
	const FPmcWeights Loaded = PmcCommanderRules::FromJson(*Json);
	TestTrue(TEXT("objective genes read"), FMath::IsNearlyEqual(Loaded.Objective, 1.2f) && FMath::IsNearlyEqual(Loaded.ObjectiveUrgency, 0.4f)
		&& FMath::IsNearlyEqual(Loaded.ObjectiveApproach, 0.3f) && FMath::IsNearlyEqual(Loaded.Overwatch, 0.7f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaScenarioTest, "CodexTactics.AI.PmcArena.Scenario",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaScenarioTest::RunTest(const FString&)
{
	// Scenario=random: deterministic per round seed, every scenario comes up.
	TSet<int32> Seen;
	for (int32 Seed = 1; Seed <= 60; ++Seed)
	{
		const EPmcScenario Scenario = PmcCommanderRules::PickScenario(Seed);
		TestEqual(TEXT("deterministic"), static_cast<int32>(PmcCommanderRules::PickScenario(Seed)), static_cast<int32>(Scenario));
		Seen.Add(static_cast<int32>(Scenario));
	}
	TestEqual(TEXT("all five scenarios over 60 seeds"), Seen.Num(), static_cast<int32>(EPmcScenario::Count));
	EPmcScenario Parsed = EPmcScenario::Skirmish;
	TestTrue(TEXT("parse chokepoint"), PmcCommanderRules::ParseScenario(TEXT("Chokepoint"), Parsed) && Parsed == EPmcScenario::Chokepoint);
	TestTrue(TEXT("parse blizzard"), PmcCommanderRules::ParseScenario(TEXT("blizzard"), Parsed) && Parsed == EPmcScenario::Blizzard);
	TestFalse(TEXT("random is no fixed scenario"), PmcCommanderRules::ParseScenario(TEXT("random"), Parsed));

	// The chokepoint test: walls 1 m left and 1.2 m right (rays 2 / 6), tall, the passage free along rays 0 / 4.
	float Hits[8] = { 0.f, 0.f, 100.f, 0.f, 0.f, 0.f, 120.f, 0.f };
	bool Tall[8] = { false, false, true, false, false, false, true, false };
	int32 Axis = INDEX_NONE;
	float Width = 0.f;
	TestTrue(TEXT("a 2.2 m passage"), PmcCommanderRules::IsPassage(Hits, Tall, 100.f, 300.f, Axis, Width));
	TestTrue(TEXT("... along ray 0 / 4"), Axis == 0 || Axis == 4);
	TestEqual(TEXT("... 2.2 m wide"), Width, 220.f, 0.001f);
	Tall[6] = false;
	TestFalse(TEXT("a low wall on one side: no passage"), PmcCommanderRules::IsPassage(Hits, Tall, 100.f, 300.f, Axis, Width));
	Tall[6] = true;
	Hits[0] = 250.f;
	TestFalse(TEXT("blocked along the axis: a dead end, no passage"), PmcCommanderRules::IsPassage(Hits, Tall, 100.f, 300.f, Axis, Width));
	Hits[0] = 0.f;
	Hits[2] = 250.f;
	Hits[6] = 250.f;
	TestFalse(TEXT("5 m between the walls: too wide"), PmcCommanderRules::IsPassage(Hits, Tall, 100.f, 300.f, Axis, Width));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommanderAttentionTest, "CodexTactics.AI.PmcCommander.Attention",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommanderAttentionTest::RunTest(const FString&)
{
	using namespace PmcArenaObjectiveTestLocal;
	// One seen enemy at the origin looking along +X: a point ahead is watched, 90 deg off is his blind side, behind him fully.
	const TArray<FVector2D> Positions = { FVector2D::ZeroVector };
	const TArray<float> LookingX = { 0.f };
	TestEqual(TEXT("in his look sector: 0"), PmcCommanderRules::AttentionOff(Positions, LookingX, FVector2D(2000.f, 300.f)), 0.f);
	TestEqual(TEXT("67.5 deg off: half"), PmcCommanderRules::AttentionOff(Positions, LookingX, FVector2D(1000.f, 1000.f * FMath::Tan(FMath::DegreesToRadians(67.5f)))), 0.5f, 0.01f);
	TestEqual(TEXT("90 deg off: unobserved"), PmcCommanderRules::AttentionOff(Positions, LookingX, FVector2D(0.f, 2000.f)), 1.f, 0.001f);
	TestEqual(TEXT("out of range: nobody looks"), PmcCommanderRules::AttentionOff(Positions, LookingX, FVector2D(9000.f, 0.f)), 1.f);
	// The most attentive watcher decides: a second man looking at the point.
	const TArray<FVector2D> Two = { FVector2D::ZeroVector, FVector2D(0.f, 4000.f) };
	const TArray<float> Looks = { 0.f, -90.f };
	TestEqual(TEXT("a second watcher covers the blind side"), PmcCommanderRules::AttentionOff(Two, Looks, FVector2D(0.f, 2000.f)), 0.f);

	// The move consideration: a flank while the seen threat looks away beats one under his eyes (bounded by attention).
	const FPmcWeights Weights;
	FPmcSquadView Squad = ObjectiveSquad();
	Squad.bContact = true;
	FPmcFighterView Flanker;
	Flanker.bHasThreat = true;
	Flanker.Threat.DistanceCm = 1500.f;
	Flanker.Threat.bSeenNow = true;
	Flanker.Threat.AttentionOff = 1.f;
	const float Blind = PmcCommanderRules::ScoreOrder(EPmcOrder::Flank, Flanker, Squad, Weights);
	Flanker.Threat.AttentionOff = 0.f;
	const float Watched = PmcCommanderRules::ScoreOrder(EPmcOrder::Flank, Flanker, Squad, Weights);
	TestEqual(TEXT("flank through the blind zone x 1.3"), Blind / FMath::Max(Watched, 1e-6f), 1.3f / 0.7f, 0.001f);
	Flanker.Threat.bSeenNow = false;
	TestEqual(TEXT("no seen threat: neutral"), PmcCommanderRules::AttentionFactor(Flanker, Weights), 1.f);

	// Fire discipline: an objective defender in ambush holds while the seen target looks his way, strikes when it looks away / is close.
	FPmcFighterView Ambusher = ObjectiveFighter(EPmcRole::Defender, 500.f, 0.1f);
	Ambusher.bHasThreat = true;
	Ambusher.Threat.bSeenNow = true;
	Ambusher.Threat.DistanceCm = 2200.f;
	Ambusher.Threat.AttentionOff = 0.f;
	Ambusher.WeaponRangeCm = 2500.f;
	FPmcSquadView Quiet = ObjectiveSquad();
	Quiet.ContactSeconds = 3.f;
	TestTrue(TEXT("watched by the target: hold fire"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	Ambusher.Threat.AttentionOff = 0.8f;
	TestFalse(TEXT("the target looks away: strike"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	Ambusher.Threat.AttentionOff = 0.f;
	Ambusher.Threat.DistanceCm = 1200.f;
	TestFalse(TEXT("deep in the kill zone (< 0.6 x reach): strike"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	Ambusher.Threat.DistanceCm = 2200.f;
	FPmcSquadView Blown = Quiet;
	Blown.bShotsExchanged = true;
	TestFalse(TEXT("shots exchanged: the ambush is blown"), PmcCommanderRules::ShouldHoldFire(Ambusher, Blown, Weights));
	FPmcSquadView Long = Quiet;
	Long.ContactSeconds = 25.f;
	TestFalse(TEXT("held past ambush_max_hold_s: strike"), PmcCommanderRules::ShouldHoldFire(Ambusher, Long, Weights));
	Ambusher.bExposed = true;
	TestFalse(TEXT("seen by the enemy: fire back"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	Ambusher.bExposed = false;
	Ambusher.Threat.bSeenNow = false;
	TestTrue(TEXT("lying in wait, nobody seen now: the discipline stays armed"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	Ambusher.Objective.Role = EPmcRole::Attacker;
	TestFalse(TEXT("only defenders hold fire"), PmcCommanderRules::ShouldHoldFire(Ambusher, Quiet, Weights));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
