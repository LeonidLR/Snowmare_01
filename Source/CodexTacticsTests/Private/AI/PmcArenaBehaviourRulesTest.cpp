#include "Misc/AutomationTest.h"
#include "AI/PmcArenaBehaviourRules.h"
#include "AI/PmcCommanderRules.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "LevelGen/TacticalMapRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// TACTICAL_MAP.md P3 (designer 2026-11-17: the data must feed the trainer): the arena round's behaviour metrics per side in Grok's G002
// schema codex.pmc_arena_behaviour/1 (AI/PmcArenaBehaviourRules.h). Pure rules.

namespace PmcArenaBehaviourRulesTest
{
	/** A 10 m x 4 m chokepoint round the origin with one hiding spot at its exit, a window and an overwatch roof. */
	FTacticalMapFile ToyMap()
	{
		FTacticalMapFile File;
		FTacticalChokepoint& Zone = File.Chokepoints.AddDefaulted_GetRef();
		Zone.Id = TEXT("CP_01");
		Zone.Score = 0.8f;
		Zone.Location = FVector::ZeroVector;
		Zone.Poly = { FVector2D(-500.f, -200.f), FVector2D(500.f, -200.f), FVector2D(500.f, 200.f), FVector2D(-500.f, 200.f) };
		FTacticalHidingSpot& Spot = File.HidingSpots.AddDefaulted_GetRef();
		Spot.Id = TEXT("HS_01");
		Spot.Location = FVector(900.f, 600.f, 0.f);
		Spot.ThreatLocation = FVector(600.f, 0.f, 0.f);
		Spot.Place = TEXT("CP_01");
		FTacticalHidingSpot& Loose = File.HidingSpots.AddDefaulted_GetRef();
		Loose.Id = TEXT("HS_02");
		Loose.Location = FVector(-3000.f, 0.f, 0.f);
		Loose.ThreatLocation = FVector(-2800.f, 0.f, 0.f);
		FTacticalWindow& Window = File.Windows.AddDefaulted_GetRef();
		Window.Id = TEXT("W_01");
		Window.Stand = FVector(2000.f, 2000.f, 0.f);
		FTacticalOverwatchRoof& Roof = File.OverwatchRoofs.AddDefaulted_GetRef();
		Roof.Building = TEXT("B_tower");
		Roof.Z = 900.f;
		Roof.Spots.Add({ FVector(0.f, 4000.f, 900.f), 180.f, 500.f, 110.f });
		TacticalMapRules::UpdateDerived(File);
		return File;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaBehaviourContactTest, "CodexTactics.AI.PmcArena.BehaviourContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaBehaviourContactTest::RunTest(const FString&)
{
	using namespace PmcArenaBehaviourRules;
	TestEqual(TEXT("no contact: 0 s"), TimeInContact({}, 120.f), 0.f);
	TestNearlyEqual(TEXT("three overlapping moments 4 / 9 / 10 s: 4..15 = 11 s"), TimeInContact({ 10.f, 4.f, 9.f }, 120.f), 11.f, 1e-3f);
	TestNearlyEqual(TEXT("a moment at the end is clipped"), TimeInContact({ 118.f }, 120.f), 2.f, 1e-3f);
	TestNearlyEqual(TEXT("moments every 2 s from 0 to 20: 25 s"), TimeInContact({ 0.f, 2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f, 18.f, 20.f }, 120.f), 25.f, 1e-3f);
	TestEqual(TEXT("no contact: no quiet spell (the stalemate share covers it)"), QuietSpells({}, 120.f), 0);
	TestEqual(TEXT("a 20 s gap and a 30 s quiet tail"), QuietSpells({ 10.f, 12.f, 32.f, 90.f }, 120.f), 3);
	TestEqual(TEXT("gaps under 15 s are no spells"), QuietSpells({ 10.f, 20.f, 30.f }, 40.f), 0);
	TestEqual(TEXT("stalemate: no contact"), StalemateShare(false, TEXT("A"), TEXT("elimination")), 1.f);
	TestEqual(TEXT("stalemate: a time-limit draw"), StalemateShare(true, TEXT("draw"), TEXT("time_limit")), 1.f);
	TestEqual(TEXT("no stalemate: a decided round"), StalemateShare(true, TEXT("B"), TEXT("time_limit")), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaBehaviourOrdersTest, "CodexTactics.AI.PmcArena.BehaviourOrders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaBehaviourOrdersTest::RunTest(const FString&)
{
	using namespace PmcArenaBehaviourRules;
	TArray<FPmcOrderEvent> Events = {
		{ 1.f, 0, EPmcOrder::Ambush }, { 1.f, 1, EPmcOrder::Ambush },
		{ 10.f, 0, EPmcOrder::Suppress }, { 12.f, 1, EPmcOrder::Flank },     // suppress then move: 1
		{ 20.f, 0, EPmcOrder::Suppress }, { 21.f, 0, EPmcOrder::Hold },      // the suppressor stopped at 21 ...
		{ 22.f, 1, EPmcOrder::Advance },                                      // ... so this move is not under it
		{ 30.f, 0, EPmcOrder::Suppress }, { 39.f, 1, EPmcOrder::TakeCover }, // 9 s later: out of the 8 s window
		{ 40.f, 1, EPmcOrder::TakeCover },
	};
	TestEqual(TEXT("suppress-then-move counts the covered moves only"), SuppressThenMove(Events), 1);
	// Churn: fighter 0 Ambush -> Suppress -> Suppress(no) -> Hold -> Suppress = 3; fighter 1 Ambush -> Flank -> Advance -> TakeCover -> TakeCover(no) = 3.
	TestEqual(TEXT("order churn = changes of a man's order"), OrderChurn(Events), 6);
	TestTrue(TEXT("a walk 4 m long is a move"), IsCompletedMove(EPmcOrder::Flank, FVector::ZeroVector, FVector(400.f, 0.f, 0.f)));
	TestFalse(TEXT("a walk that ended 1 m away is none"), IsCompletedMove(EPmcOrder::Advance, FVector::ZeroVector, FVector(100.f, 0.f, 0.f)));
	TestFalse(TEXT("a static order is no move"), IsCompletedMove(EPmcOrder::Hold, FVector::ZeroVector, FVector(900.f, 0.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaBehaviourMapTest, "CodexTactics.AI.PmcArena.BehaviourTacticalMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaBehaviourMapTest::RunTest(const FString&)
{
	using namespace PmcArenaBehaviourRulesTest;
	using namespace PmcArenaBehaviourRules;
	const FTacticalMapFile File = ToyMap();
	const FTacticalRuntimeConfig Config;
	TestTrue(TEXT("inside the chokepoint"), IsInChokepoint(File, Config, FVector(100.f, 50.f, 0.f)));
	TestFalse(TEXT("on its floor but outside"), IsInChokepoint(File, Config, FVector(800.f, 0.f, 0.f)));
	TestFalse(TEXT("a floor above it"), IsInChokepoint(File, Config, FVector(100.f, 50.f, 300.f)));
	TestEqual(TEXT("at the window's stand point"), WindowAt(File, Config, FVector(2050.f, 2000.f, 10.f)), 0);
	TestEqual(TEXT("away from it"), WindowAt(File, Config, FVector(2500.f, 2000.f, 0.f)), static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("an ambush hit from the chokepoint's exit spot on a man at its threatened point"), IsAmbushHit(File, Config, FVector(950.f, 650.f, 0.f), FVector(400.f, 0.f, 0.f)));
	TestFalse(TEXT("the target far from the threatened point"), IsAmbushHit(File, Config, FVector(950.f, 650.f, 0.f), FVector(-2000.f, 0.f, 0.f)));
	TestFalse(TEXT("a hiding spot with no chokepoint is no chokepoint ambush"), IsAmbushHit(File, Config, FVector(-3000.f, 0.f, 0.f), FVector(-2800.f, 0.f, 0.f)));
	TestTrue(TEXT("at the overwatch roof spot"), IsAtOverwatchSpot(File, Config, FVector(100.f, 3900.f, 900.f)));
	TestFalse(TEXT("on the street under it"), IsAtOverwatchSpot(File, Config, FVector(100.f, 3900.f, 0.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaBehaviourJsonTest, "CodexTactics.AI.PmcArena.BehaviourJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaBehaviourJsonTest::RunTest(const FString&)
{
	using namespace PmcArenaBehaviourRules;
	FPmcBehaviour Behaviour;
	Behaviour.TimeInContactS = 42.f;
	Behaviour.FlanksCompleted = 1;
	Behaviour.TimeInChokepointS = 3.25f;
	Behaviour.StalemateShare = 1.f;
	const TSharedRef<FJsonObject> Json = ToJson(Behaviour, TEXT("A"), true);
	TestEqual(TEXT("schema"), Json->GetStringField(TEXT("schema")), FString(TEXT("codex.pmc_arena_behaviour/1")));
	TestEqual(TEXT("side"), Json->GetStringField(TEXT("side")), FString(TEXT("A")));
	TestTrue(TEXT("tactical_map flag"), Json->GetBoolField(TEXT("tactical_map")));
	const TSharedPtr<FJsonObject> Metrics = Json->GetObjectField(TEXT("metrics"));
	TestEqual(TEXT("the 13 G002 metrics"), Metrics->Values.Num(), 13);
	TestEqual(TEXT("catalog of 13"), Catalog().Num(), 13);
	for (const FPmcBehaviourMetricSpec& Spec : Catalog())
	{
		const TSharedPtr<FJsonObject>* Metric = nullptr;
		const bool bThere = Metrics->TryGetObjectField(Spec.Key, Metric) && Metric && (*Metric)->HasField(TEXT("name")) && (*Metric)->HasField(TEXT("unit"))
			&& (*Metric)->HasField(TEXT("direction")) && (*Metric)->HasField(TEXT("value")) && (*Metric)->GetArrayField(TEXT("range")).Num() == 2;
		TestTrue(FString::Printf(TEXT("metric %s complete"), Spec.Key), bThere);
	}
	TestEqual(TEXT("time_in_contact_s value"), Metrics->GetObjectField(TEXT("time_in_contact_s"))->GetNumberField(TEXT("value")), 42.0);
	TestEqual(TEXT("time_in_chokepoint_s rounded to 0.1"), Metrics->GetObjectField(TEXT("time_in_chokepoint_s"))->GetNumberField(TEXT("value")), 3.3, 1e-4);
	TestEqual(TEXT("choke is a 'lower' metric"), Metrics->GetObjectField(TEXT("time_in_chokepoint_s"))->GetStringField(TEXT("direction")), FString(TEXT("lower")));
	TestEqual(TEXT("quiet spells are 'context'"), Metrics->GetObjectField(TEXT("quiet_spells"))->GetStringField(TEXT("direction")), FString(TEXT("context")));
	TestEqual(TEXT("an absent metric is 0"), Metrics->GetObjectField(TEXT("overwatch_kills"))->GetNumberField(TEXT("value")), 0.0);
	const TSharedPtr<FJsonValue> AbsentGuard = Metrics->GetObjectField(TEXT("guard_near_access_share"))->TryGetField(TEXT("value"));
	TestTrue(TEXT("no access: guard_near_access_share is JSON null, not 0"), AbsentGuard.IsValid() && AbsentGuard->Type == EJson::Null);
	TestEqual(TEXT("Value() of an n/a guard share is 0 for numeric callers"), Behaviour.Value(TEXT("guard_near_access_share")), 0.f);
	FPmcBehaviour Applicable;
	Applicable.bGuardShareApplicable = true;
	Applicable.GuardNearAccessShare = 0.8f;
	Applicable.DefenderSpreadM = 7.5f;
	const TSharedRef<FJsonObject> ApplicableJson = ToJson(Applicable, TEXT("B"), false);
	const TSharedPtr<FJsonObject> ApplicableMetrics = ApplicableJson->GetObjectField(TEXT("metrics"));
	TestEqual(TEXT("a real guard share is a number"), ApplicableMetrics->GetObjectField(TEXT("guard_near_access_share"))->GetNumberField(TEXT("value")), 0.8);
	TestEqual(TEXT("defender spread is a number"), ApplicableMetrics->GetObjectField(TEXT("defender_spread_m"))->GetNumberField(TEXT("value")), 7.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaPositionTimeTest, "CodexTactics.AI.PmcArenaBehaviour.PositionTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaPositionTimeTest::RunTest(const FString&)
{
	FPmcArenaPositionSample Idle;
	Idle.bAlive = true;
	FPmcArenaPositionSample Covered = Idle;
	Covered.bOnPosition = true;
	FPmcArenaPositionSample Walking = Idle;
	Walking.bMoving = true;
	Walking.bWalking = true;
	FPmcArenaPositionTime Time;
	Time.AddInterval(Idle, Covered, 1.25f, true);
	Time.AddInterval(Covered, Walking, 2.f, true);
	Time.AddInterval(Idle, Walking, 3.f, false);
	Time.AddInterval({}, Idle, 4.f, false);
	TestEqual(TEXT("Only observed living open idle contributes"), Time.OpenIdleS, 4.25f);
	TestEqual(TEXT("Open idle before contact is retained separately"), Time.OpenIdleBeforeContactS, 1.25f);
	FPmcArenaPositionSample Next = Walking;
	Next.Feet.X = 100.f;
	Time.AddInterval(Walking, Next, 1.f, false);
	FPmcArenaPositionSample Turn = Next;
	Turn.Feet.Y = 150.f;
	Time.AddInterval(Next, Turn, 1.f, false);
	TestEqual(TEXT("Exposure sums distance along a turn"), Time.MaxExposedRunCm, 250.0);
	FPmcArenaPositionSample NearCover = Turn;
	NearCover.bNearCoverSlot = true;
	Time.AddInterval(Turn, NearCover, 1.f, false);
	TestEqual(TEXT("A nearby slot breaks the uncovered segment"), Time.ExposedRunCm, 0.0);
	Time.AddInterval(Turn, Next, 1.f, false);
	Time.AddInterval(Next, Idle, 1.f, false);
	TestEqual(TEXT("Stopping breaks the uncovered segment"), Time.ExposedRunCm, 0.0);
	TestEqual(TEXT("Separate running segments do not merge"), Time.MaxExposedRunCm, 250.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaPositionContactTest, "CodexTactics.AI.PmcArenaBehaviour.PositionContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaPositionContactTest::RunTest(const FString&)
{
	using namespace PmcArenaBehaviourRules;
	FPmcArenaPositionSample Defender;
	Defender.bAlive = true;
	Defender.WeaponRangeCm = 500.f;
	Defender.bOnPosition = true;
	FPmcArenaPositionSample Unarmed;
	Unarmed.bAlive = true;
	TArray<FPmcArenaPositionSample> Defenders = { Defender, Unarmed, {} };
	FPmcArenaPositionSample Attacker;
	Attacker.bAlive = true;
	Attacker.Feet = FVector(300.f, 0.f, 400.f);
	TArray<FPmcArenaPositionSample> Attackers = { Attacker };
	TestTrue(TEXT("An attacker at the exact weapon reach is in range"), AttackersInRange(Defenders, Attackers));
	Attackers[0].Feet.Z += 1.f;
	TestFalse(TEXT("3D height contributes to range"), AttackersInRange(Defenders, Attackers));
	Attackers[0].bAlive = false;
	Attackers[0].Feet = FVector::ZeroVector;
	TestFalse(TEXT("A casualty does not trigger range"), AttackersInRange(Defenders, Attackers));
	TestEqual(TEXT("One of two live defenders is in position"), OnPositionShare(Defenders).Get(-1.f), 0.5f);
	TestFalse(TEXT("Empty team has no position share"), OnPositionShare({}).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcArenaOpeningGeometryTest, "CodexTactics.AI.PmcArenaBehaviour.OpeningGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcArenaOpeningGeometryTest::RunTest(const FString&)
{
	const TArray<FVector> Defenders = { FVector(100.f, 0.f, 0.f), FVector(0.f, 0.f, 100.f), FVector(50.f, 0.f, 50.f) };
	const TArray<FVector> Attackers = { FVector(0.f, 10.f, 0.f), FVector(0.f, -10.f, 0.f) };
	TestNearlyEqual(TEXT("Maximum angle to the attackers' centroid includes elevation"),
		PmcArenaBehaviourRules::CrossfireDeg(Defenders, Attackers).Get(-1.f), 90.f, 0.01f);
	TestFalse(TEXT("No attackers makes crossfire unavailable"), PmcArenaBehaviourRules::CrossfireDeg(Defenders, {}).IsSet());

	FPmcArenaOpeningShots Shots;
	Shots.Record(1, 3.f);
	Shots.Record(1, 3.1f);
	TestFalse(TEXT("Two shots by the same man do not establish a gap"), Shots.GapS().IsSet());
	Shots.Record(2, 3.5f);
	Shots.Record(3, 9.f);
	TestEqual(TEXT("Second distinct defender fixes the gap"), Shots.GapS().Get(-1.f), 0.5f);

	FPmcArenaOpeningShots Simultaneous;
	Simultaneous.Record(1, 1.f);
	Simultaneous.Record(2, 1.f);
	TestEqual(TEXT("Two defenders opening together is a measured zero gap"), Simultaneous.GapS().Get(-1.f), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
