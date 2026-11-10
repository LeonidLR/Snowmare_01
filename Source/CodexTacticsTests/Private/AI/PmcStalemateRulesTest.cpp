#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

// fix/pmc-arena-stalemate (the designer 2026-11-08, docs/systems/pmc-squad-ai.md 11): order commitment (minimum hold, failed-order
// cooldown), the push after a quiet spell, no cover from a stand-in threat, the new genes - pure rules.

namespace PmcStalemateTestLocal
{
	TArray<FPmcOrderScore> Scores(std::initializer_list<FPmcOrderScore> Entries)
	{
		TArray<FPmcOrderScore> Out(Entries);
		Out.StableSort([](const FPmcOrderScore& A, const FPmcOrderScore& B) { return A.Score > B.Score; });
		return Out;
	}

	FPmcFighterView Attacker(float DistanceCm)
	{
		FPmcFighterView View;
		View.Objective.Role = EPmcRole::Attacker;
		View.Objective.Location = FVector(DistanceCm, 0.f, 0.f);
		View.Objective.DistanceCm = DistanceCm;
		View.Objective.RadiusCm = 600.f;
		// In good cover against a remembered (not seen) threat: the corner sit of the pmc-v3 night.
		View.bInCover = true;
		View.CoverQuality = 0.9f;
		View.bHasThreat = true;
		View.Threat.Location = FVector(4000.f, 2000.f, 0.f);
		View.Threat.DistanceCm = 4500.f;
		View.Threat.SecondsSince = 40.f;
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcCommitmentTest, "CodexTactics.AI.PmcCommander.Commitment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcCommitmentTest::RunTest(const FString&)
{
	using namespace PmcStalemateTestLocal;
	const FPmcWeights W;
	TestEqual(TEXT("minimum hold 3 s"), W.OrderMinHoldSeconds, 3.f);
	const TArray<FPmcOrderScore> SuppressBest = Scores({ { EPmcOrder::Flank, 0.3f }, { EPmcOrder::Suppress, 1.2f }, { EPmcOrder::Hold, 0.2f } });

	// A running flank 1 s old keeps going against a much better order (the minimum hold).
	FPmcCommitment C;
	C.Current = EPmcOrder::Flank;
	C.bRunning = true;
	C.AgeSeconds = 1.f;
	EPmcPickReason Reason = EPmcPickReason::Best;
	TestEqual(TEXT("inside the minimum hold: the flank goes on"), PmcCommanderRules::PickCommitted(SuppressBest, C, W, &Reason), EPmcOrder::Flank);
	TestTrue(TEXT("... reason keep"), Reason == EPmcPickReason::Keep);
	// After the minimum hold a clearly better one (more than move_switch_margin 0.45) replaces it; a slightly better one does not.
	C.AgeSeconds = 4.f;
	TestEqual(TEXT("after the hold: preempted by a clearly better order"), PmcCommanderRules::PickCommitted(SuppressBest, C, W, &Reason), EPmcOrder::Suppress);
	TestTrue(TEXT("... reason preempt"), Reason == EPmcPickReason::Preempt);
	const TArray<FPmcOrderScore> Close = Scores({ { EPmcOrder::Flank, 0.5f }, { EPmcOrder::Suppress, 0.8f } });
	TestEqual(TEXT("after the hold: a walk is kept within its margin"), PmcCommanderRules::PickCommitted(Close, C, W), EPmcOrder::Flank);
	// Invalid (scores 0) inside the hold: replaced at once.
	C.AgeSeconds = 0.5f;
	const TArray<FPmcOrderScore> Invalid = Scores({ { EPmcOrder::Flank, 0.f }, { EPmcOrder::TakeCover, 0.6f } });
	TestEqual(TEXT("an invalid order goes at once"), PmcCommanderRules::PickCommitted(Invalid, C, W, &Reason), EPmcOrder::TakeCover);
	TestTrue(TEXT("... reason invalid"), Reason == EPmcPickReason::Invalid);
	// Static orders are no commitment: the best order wins at once.
	C.Current = EPmcOrder::Hold;
	TestEqual(TEXT("from Hold: the best order"), PmcCommanderRules::PickCommitted(SuppressBest, C, W, &Reason), EPmcOrder::Suppress);
	TestTrue(TEXT("... reason best"), Reason == EPmcPickReason::Best);

	// A failed order sits out its cooldown (5 s): the pmc-v3 flank re-decided twice a second.
	C = FPmcCommitment();
	C.Current = EPmcOrder::Flank;
	C.Failed = EPmcOrder::Flank;
	C.FailedAgeSeconds = 1.f;
	const TArray<FPmcOrderScore> FlankBest = Scores({ { EPmcOrder::Flank, 0.9f }, { EPmcOrder::Suppress, 0.4f } });
	TestEqual(TEXT("the failed flank cools down: the next best order"), PmcCommanderRules::PickCommitted(FlankBest, C, W), EPmcOrder::Suppress);
	C.FailedAgeSeconds = 6.f;
	TestEqual(TEXT("after the cooldown the flank may come back"), PmcCommanderRules::PickCommitted(FlankBest, C, W), EPmcOrder::Flank);
	C.FailedAgeSeconds = 1.f;
	const TArray<FPmcOrderScore> OnlyFlank = Scores({ { EPmcOrder::Flank, 0.9f } });
	TestEqual(TEXT("nothing else scores: Hold"), PmcCommanderRules::PickCommitted(OnlyFlank, C, W), EPmcOrder::Hold);
	TestTrue(TEXT("static orders"), PmcCommanderRules::IsStaticOrder(EPmcOrder::Hold) && PmcCommanderRules::IsStaticOrder(EPmcOrder::Ambush)
		&& PmcCommanderRules::IsStaticOrder(EPmcOrder::Crouch) && !PmcCommanderRules::IsStaticOrder(EPmcOrder::Suppress));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPushTest, "CodexTactics.AI.PmcCommander.Push",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPushTest::RunTest(const FString&)
{
	using namespace PmcStalemateTestLocal;
	const FPmcWeights W;
	FPmcSquadView Squad;
	Squad.Alive = 3;
	Squad.EnemiesKnown = 3;
	Squad.bEverContact = true;
	FPmcFighterView View = Attacker(3000.f);

	// 10 s quiet: no push yet.
	Squad.QuietSeconds = 10.f;
	TestFalse(TEXT("10 s quiet: no push"), PmcCommanderRules::IsPushing(View, Squad, W));
	const float ObjectiveCalm = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, View, Squad, W);
	const float HoldCalm = PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, View, Squad, W);
	// 30 s without contact: the attacker leaves his corner for the point (Objective x push 2, Hold / 2).
	Squad.QuietSeconds = 31.f;
	TestTrue(TEXT("31 s quiet in an objective round: the push"), PmcCommanderRules::IsPushing(View, Squad, W));
	const float ObjectivePush = PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, View, Squad, W);
	const float HoldPush = PmcCommanderRules::ScoreOrder(EPmcOrder::Hold, View, Squad, W);
	TestEqual(TEXT("push: Objective = weight x push"), ObjectivePush, W.Objective * W.Push, 0.001f);
	TestTrue(TEXT("push: Objective grows"), ObjectivePush > ObjectiveCalm);
	TestEqual(TEXT("push: Hold off the point / push"), HoldPush, HoldCalm / W.Push, 0.001f);
	const TArray<FPmcOrderScore> Scored = PmcCommanderRules::ScoreAll(View, Squad, W);
	TestEqual(TEXT("push: the attacker walks to the point"), Scored[0].Order, EPmcOrder::Objective);
	// On the point he holds it (no push walk, no damping).
	FPmcFighterView OnPoint = Attacker(200.f);
	TestEqual(TEXT("on the point: no Objective walk"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, OnPoint, Squad, W), 0.f);
	// Off the arena (no objective) nothing pushes.
	FPmcFighterView Plain = View;
	Plain.Objective = FPmcObjectiveView();
	TestFalse(TEXT("no objective: no push"), PmcCommanderRules::IsPushing(Plain, Squad, W));

	// The defender: onto the point when it goes quiet, and as the time limit nears (from 60 % of the round, full at 85 %).
	FPmcFighterView Defender = Attacker(900.f); // within his overwatch (1200 cm without a band)
	Defender.Objective.Role = EPmcRole::Defender;
	Squad.QuietSeconds = 0.f;
	Defender.Objective.TimeFrac = 0.3f;
	TestEqual(TEXT("defender early, in contact, within overwatch: stays"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Defender, Squad, W), 0.f);
	Defender.Objective.TimeFrac = 0.9f;
	TestEqual(TEXT("defender near the limit: onto the point 0.9"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Defender, Squad, W), 0.9f, 0.001f);
	Defender.Objective.TimeFrac = 0.3f;
	Squad.QuietSeconds = 31.f;
	TestEqual(TEXT("defender, quiet 31 s: onto the point 0.8"), PmcCommanderRules::ScoreOrder(EPmcOrder::Objective, Defender, Squad, W), 0.8f, 0.001f);

	// No cover from a stand-in (the objective point with no enemy known): it flapped with the Objective walk.
	FPmcFighterView Open = Attacker(3000.f);
	Open.bInCover = false;
	Open.CoverQuality = 0.f;
	Open.bCoverAvailable = true;
	Squad.QuietSeconds = 0.f;
	TestTrue(TEXT("cover against a real threat"), PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, Open, Squad, W) > 0.f);
	Open.Threat.bStandIn = true;
	TestEqual(TEXT("no cover against a stand-in"), PmcCommanderRules::ScoreOrder(EPmcOrder::TakeCover, Open, Squad, W), 0.f);

	// The genes read from the weights file.
	TSharedPtr<FJsonObject> Json;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"order_min_hold_s\": 4, \"failed_order_cooldown_s\": 6, \"repath_after_s\": 5, \"push_after_s\": 30, \"push\": 2.5, \"rpg_gunner_share\": 0.3}")), Json);
	if (TestTrue(TEXT("json parsed"), Json.IsValid()))
	{
		const FPmcWeights Read = PmcCommanderRules::FromJson(*Json);
		TestTrue(TEXT("the stalemate genes read"), Read.OrderMinHoldSeconds == 4.f && Read.FailedOrderCooldownSeconds == 6.f && Read.RepathAfterSeconds == 5.f
			&& Read.PushAfterSeconds == 30.f && Read.Push == 2.5f && FMath::IsNearlyEqual(Read.RpgGunnerShare, 0.3f));
	}
	TestTrue(TEXT("no gunner override by default"), W.RpgGunnerShare < 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
