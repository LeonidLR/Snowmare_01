#include "Misc/AutomationTest.h"
#include "AI/PmcCommanderRules.h"
#include "AI/PmcPolicyBrain.h"
#include "AI/PmcPolicyRules.h"
#include "Characters/OperativeCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// The PMC order policy (Learning Agents probe P0, 2026-11-23, docs/systems/pmc-policy-probe.md). Pure rules: the action set, the mask
// that keeps the scorer's gates, the observation vector built from the commander's views, the reward of a step. BrainStep: the policy
// brain's inference step through the Learning Agents plugin in a scratch world (no arena, no trainer, no Python).

namespace PmcPolicyTestLocal
{
	TArray<FPmcOrderScore> PolicyScores(std::initializer_list<FPmcOrderScore> Entries)
	{
		// Every order scored (as ScoreAll gives them), the listed ones above 0, best first.
		TArray<FPmcOrderScore> Out;
		for (int32 Index = 0; Index < static_cast<int32>(EPmcOrder::Count); ++Index)
		{
			Out.Add({ static_cast<EPmcOrder>(Index), 0.f });
		}
		for (const FPmcOrderScore& Entry : Entries)
		{
			Out[static_cast<int32>(Entry.Order)].Score = Entry.Score;
		}
		Out.StableSort([](const FPmcOrderScore& A, const FPmcOrderScore& B) { return A.Score > B.Score; });
		return Out;
	}

	TArray<EPmcOrder> SampleableOrders(uint32 Mask, bool bCanAct = true)
	{
		TArray<int32> Masked;
		PmcPolicyRules::MaskedActions(Mask, bCanAct, Masked);
		TArray<EPmcOrder> Out;
		for (int32 Index = 0; Index < PmcPolicyRules::ActionCount; ++Index)
		{
			if (!Masked.Contains(Index))
			{
				Out.Add(PmcPolicyRules::OrderOfAction(Index));
			}
		}
		return Out;
	}

	FPmcFighterView Defender(const FVector& Location, const FVector& Point)
	{
		FPmcFighterView View;
		View.Location = Location;
		View.Objective.Role = EPmcRole::Defender;
		View.Objective.Location = Point;
		View.Objective.RadiusCm = 600.f;
		View.Objective.DistanceCm = static_cast<float>(FVector::Dist2D(Location, Point));
		return View;
	}

	TArray<float> Observe(const FPmcFighterView& Fighter, const FPmcSquadView& Squad)
	{
		TArray<float> Out;
		Out.Init(-7.f, PmcPolicyRules::ObservationSize);
		PmcPolicyRules::BuildObservation(Fighter, Squad, Out);
		return Out;
	}

	/** A scratch game world for the brain's host actor and a few fighter bodies. */
	struct FScopedPolicyTestWorld
	{
		UWorld* World = nullptr;
		FWorldContext* Context = nullptr;

		explicit FScopedPolicyTestWorld(const TCHAR* WorldName)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
			if (World && GEngine)
			{
				Context = &GEngine->CreateNewWorldContext(EWorldType::Game);
				Context->SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedPolicyTestWorld()
		{
			if (World && GEngine)
			{
				if (Context)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(false);
			}
		}
	};

	/** A fighter body without a controller: the brain only asks whether he lives. */
	AOperativeCharacter* SpawnPolicyTestFighter(UWorld& World, int32 Index)
	{
		const FTransform Transform(FVector(Index * 300.f, 0.f, 200.f));
		AOperativeCharacter* Fighter = World.SpawnActorDeferred<AOperativeCharacter>(AOperativeCharacter::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Fighter)
		{
			Fighter->AutoPossessAI = EAutoPossessAI::Disabled;
			Fighter->FinishSpawning(Transform);
		}
		return Fighter;
	}

	/** Two deciding fighters: [0] may take cover or hold, [1] is committed to a young suppression. */
	TArray<FPmcOrderRequest> PolicyTestRequests(const TArray<AOperativeCharacter*>& Fighters)
	{
		TArray<FPmcOrderRequest> Requests;
		Requests.SetNum(2);
		Requests[0].Fighter = Fighters[0];
		Requests[0].View = Defender(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f));
		Requests[0].Squad.Alive = 3;
		Requests[0].Scored = PolicyScores({ { EPmcOrder::TakeCover, 1.f }, { EPmcOrder::Hold, 0.2f } });
		Requests[0].Commitment.Current = EPmcOrder::Hold;
		Requests[0].Commitment.NowSeconds = 10.0;
		Requests[0].ScorerPick = Requests[0].Pick = EPmcOrder::TakeCover;
		Requests[1].Fighter = Fighters[1];
		Requests[1].View = Defender(FVector(300.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f));
		Requests[1].View.RunningOrder = EPmcOrder::Suppress;
		Requests[1].Squad.Alive = 3;
		Requests[1].Scored = PolicyScores({ { EPmcOrder::TakeCover, 1.f }, { EPmcOrder::Suppress, 0.8f }, { EPmcOrder::Hold, 0.2f } });
		Requests[1].Commitment.Current = EPmcOrder::Suppress;
		Requests[1].Commitment.bRunning = true;
		Requests[1].Commitment.AgeSeconds = 1.f;
		Requests[1].Commitment.NowSeconds = 10.0;
		Requests[1].ScorerPick = Requests[1].Pick = EPmcOrder::Suppress;
		return Requests;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyActionsTest, "CodexTactics.AI.PmcPolicy.Actions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyActionsTest::RunTest(const FString&)
{
	using namespace PmcPolicyRules;
	TestEqual(TEXT("nine defender orders"), ActionCount, 9);
	TestEqual(TEXT("Hold is action 0"), ActionIndexOf(EPmcOrder::Hold), HoldAction);
	for (const EPmcOrder Order : { EPmcOrder::Hold, EPmcOrder::TakeCover, EPmcOrder::Suppress, EPmcOrder::Reload, EPmcOrder::FallBack, EPmcOrder::Objective,
			EPmcOrder::Ambush, EPmcOrder::Crouch, EPmcOrder::RpgStrike })
	{
		const int32 Index = ActionIndexOf(Order);
		TestTrue(FString::Printf(TEXT("%s is an action"), PmcCommanderRules::OrderName(Order)), Index >= 0 && Index < ActionCount);
		TestTrue(FString::Printf(TEXT("%s round-trips"), PmcCommanderRules::OrderName(Order)), OrderOfAction(Index) == Order);
	}
	for (const EPmcOrder Order : { EPmcOrder::Advance, EPmcOrder::Flank, EPmcOrder::Heal, EPmcOrder::Relocate, EPmcOrder::Sidearm, EPmcOrder::Count })
	{
		TestEqual(FString::Printf(TEXT("%s is no action"), PmcCommanderRules::OrderName(Order)), ActionIndexOf(Order), static_cast<int32>(INDEX_NONE));
	}
	TestTrue(TEXT("an index out of range is Hold"), OrderOfAction(-1) == EPmcOrder::Hold && OrderOfAction(ActionCount) == EPmcOrder::Hold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyMaskTest, "CodexTactics.AI.PmcPolicy.Mask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyMaskTest::RunTest(const FString&)
{
	using namespace PmcPolicyRules;
	using namespace PmcPolicyTestLocal;
	const FPmcWeights W;
	const TArray<FPmcOrderScore> Scored = PolicyScores({ { EPmcOrder::TakeCover, 1.f }, { EPmcOrder::Suppress, 0.5f }, { EPmcOrder::Hold, 0.2f } });

	// Nothing running: every order the scorer scores above 0, nothing else.
	FPmcCommitment Idle;
	Idle.Current = EPmcOrder::Hold;
	Idle.NowSeconds = 100.0;
	uint32 Mask = AllowedOrderMask(Scored, Idle, W);
	TestTrue(TEXT("scored orders allowed"), IsOrderAllowed(Mask, EPmcOrder::TakeCover) && IsOrderAllowed(Mask, EPmcOrder::Suppress) && IsOrderAllowed(Mask, EPmcOrder::Hold));
	TestFalse(TEXT("an order scored 0 is ruled out"), IsOrderAllowed(Mask, EPmcOrder::FallBack));
	TestFalse(TEXT("Count is never allowed"), IsOrderAllowed(Mask, EPmcOrder::Count));
	TestEqual(TEXT("three orders to sample"), SampleableOrders(Mask).Num(), 3);
	TestTrue(TEXT("a ruled-out pick becomes Hold"), GatePick(EPmcOrder::FallBack, Mask, Idle) == EPmcOrder::Hold);
	TestTrue(TEXT("an allowed pick stays"), GatePick(EPmcOrder::Suppress, Mask, Idle) == EPmcOrder::Suppress);

	// A failed order sits out its cooldown.
	FPmcCommitment Cooling = Idle;
	Cooling.FailedUntil.Add(EPmcOrder::TakeCover, 105.0);
	Mask = AllowedOrderMask(Scored, Cooling, W);
	TestFalse(TEXT("cooling down: ruled out"), IsOrderAllowed(Mask, EPmcOrder::TakeCover));
	Cooling.NowSeconds = 106.0;
	TestTrue(TEXT("cooldown over: allowed again"), IsOrderAllowed(AllowedOrderMask(Scored, Cooling, W), EPmcOrder::TakeCover));

	// The commitment: a running valid active order inside its minimum hold is the only choice.
	FPmcCommitment Walking = Idle;
	Walking.Current = EPmcOrder::TakeCover;
	Walking.bRunning = true;
	Walking.AgeSeconds = 1.f;
	Mask = AllowedOrderMask(Scored, Walking, W);
	const TArray<EPmcOrder> Committed = SampleableOrders(Mask);
	TestTrue(TEXT("inside the minimum hold only the running order"), Committed.Num() == 1 && Committed[0] == EPmcOrder::TakeCover);
	TestTrue(TEXT("another pick keeps the running order"), GatePick(EPmcOrder::Suppress, Mask, Walking) == EPmcOrder::TakeCover);
	Walking.AgeSeconds = W.OrderMinHoldSeconds + 0.1f;
	TestTrue(TEXT("after the minimum hold the others are back"), IsOrderAllowed(AllowedOrderMask(Scored, Walking, W), EPmcOrder::Suppress));

	// A running order that turned invalid (scores 0) commits to nothing.
	FPmcCommitment Invalid = Idle;
	Invalid.Current = EPmcOrder::FallBack;
	Invalid.bRunning = true;
	Invalid.AgeSeconds = 0.5f;
	Mask = AllowedOrderMask(Scored, Invalid, W);
	TestTrue(TEXT("an invalid running order is not kept"), !IsOrderAllowed(Mask, EPmcOrder::FallBack) && IsOrderAllowed(Mask, EPmcOrder::TakeCover));

	// A static order (Ambush) is a posture, not a commitment.
	FPmcCommitment Waiting = Idle;
	Waiting.Current = EPmcOrder::Ambush;
	Waiting.bRunning = true;
	Waiting.AgeSeconds = 0.5f;
	TestTrue(TEXT("a static order does not commit"), IsOrderAllowed(AllowedOrderMask(Scored, Waiting, W), EPmcOrder::TakeCover));

	// Nothing scores: Hold (PickCommitted's fallback), and only Hold can be sampled.
	Mask = AllowedOrderMask(PolicyScores({}), Idle, W);
	TestTrue(TEXT("nothing scores: Hold"), IsOrderAllowed(Mask, EPmcOrder::Hold));
	const TArray<EPmcOrder> OnlyHold = SampleableOrders(Mask);
	TestTrue(TEXT("nothing scores: only Hold to sample"), OnlyHold.Num() == 1 && OnlyHold[0] == EPmcOrder::Hold);

	// Hold scoring 0 next to a scored order is ruled out too (the posted defender's walk to his post before contact).
	Mask = AllowedOrderMask(PolicyScores({ { EPmcOrder::Ambush, 1.05f } }), Idle, W);
	const TArray<EPmcOrder> PostWalk = SampleableOrders(Mask);
	TestTrue(TEXT("only the post walk"), PostWalk.Num() == 1 && PostWalk[0] == EPmcOrder::Ambush && !IsOrderAllowed(Mask, EPmcOrder::Hold));

	// A fighter that does not decide this tick samples Hold whatever the mask says.
	const TArray<EPmcOrder> NoDecision = SampleableOrders(AllowedOrderMask(Scored, Idle, W), false);
	TestTrue(TEXT("no decision: only Hold"), NoDecision.Num() == 1 && NoDecision[0] == EPmcOrder::Hold);

	// Committed to an order outside the action set: Hold stands in, the gate keeps the running order.
	FPmcCommitment Flanking = Idle;
	Flanking.Current = EPmcOrder::Flank;
	Flanking.bRunning = true;
	Flanking.AgeSeconds = 1.f;
	const TArray<FPmcOrderScore> FlankScored = PolicyScores({ { EPmcOrder::Flank, 0.9f }, { EPmcOrder::Hold, 0.2f } });
	Mask = AllowedOrderMask(FlankScored, Flanking, W);
	const TArray<EPmcOrder> StandIn = SampleableOrders(Mask);
	TestTrue(TEXT("outside the action set: Hold stands in"), StandIn.Num() == 1 && StandIn[0] == EPmcOrder::Hold);
	TestTrue(TEXT("... and the gate keeps the flank"), GatePick(EPmcOrder::Hold, Mask, Flanking) == EPmcOrder::Flank);

	// The scorer's own pick always passes its own gates.
	for (const FPmcCommitment& Commitment : { Idle, Cooling, Walking, Invalid, Waiting, Flanking })
	{
		for (const TArray<FPmcOrderScore>& Case : { Scored, FlankScored, PolicyScores({}), PolicyScores({ { EPmcOrder::Reload, 0.8f }, { EPmcOrder::TakeCover, 0.75f } }) })
		{
			const EPmcOrder ScorerPick = PmcCommanderRules::PickCommitted(Case, Commitment, W);
			TestTrue(TEXT("PickCommitted's order is allowed"), IsOrderAllowed(AllowedOrderMask(Case, Commitment, W), ScorerPick));
		}
	}

	// The reason of a provider's pick, in PickCommitted's terms.
	Walking.AgeSeconds = 5.f;
	TestTrue(TEXT("nothing running: best"), PickReason(EPmcOrder::Suppress, Scored, Idle) == EPmcPickReason::Best);
	TestTrue(TEXT("the running order again: keep"), PickReason(EPmcOrder::TakeCover, Scored, Walking) == EPmcPickReason::Keep);
	TestTrue(TEXT("another order over a running one: preempt"), PickReason(EPmcOrder::Suppress, Scored, Walking) == EPmcPickReason::Preempt);
	TestTrue(TEXT("the running order scored 0: invalid"), PickReason(EPmcOrder::TakeCover, Scored, Invalid) == EPmcPickReason::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyObservationTest, "CodexTactics.AI.PmcPolicy.Observation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyObservationTest::RunTest(const FString&)
{
	using namespace PmcPolicyRules;
	using namespace PmcPolicyTestLocal;
	TestEqual(TEXT("the vector's size"), ObservationSize, static_cast<int32>(ObsRunningOrder) + ActionCount);

	// A wounded defender 10 m short of the point, a seen threat 30 m beyond it, crouched in cover, reloading mates.
	FPmcFighterView Fighter = Defender(FVector(0.f, 0.f, 0.f), FVector(1000.f, 0.f, 0.f));
	Fighter.HealthFrac = 0.4f;
	Fighter.ClipFrac = 0.25f;
	Fighter.AmmoFrac = 0.5f;
	Fighter.bHasReserve = true;
	Fighter.bInCover = true;
	Fighter.CoverQuality = 0.8f;
	Fighter.bCoverAvailable = true;
	Fighter.bExposed = true;
	Fighter.Stance = EOperativeStance::Crouching;
	Fighter.bMoving = false;
	Fighter.bUnderFire = true;
	Fighter.bSuppressed = true;
	Fighter.bHasThreat = true;
	Fighter.Threat.Location = FVector(4000.f, 0.f, 0.f);
	Fighter.Threat.DistanceCm = 4000.f;
	Fighter.Threat.bSeenNow = true;
	Fighter.Threat.AttentionOff = 0.25f;
	Fighter.WeaponRangeCm = 6000.f;
	Fighter.bFireLine = true;
	Fighter.Objective.TimeFrac = 0.5f;
	Fighter.Objective.HoldFrac = 0.2f;
	Fighter.Objective.bEnemyOnPoint = true;
	Fighter.RunningOrder = EPmcOrder::Suppress;
	FPmcSquadView Squad;
	Squad.Alive = 3;
	Squad.EnemiesKnown = 3;
	Squad.EnemiesSeenNow = 2;
	Squad.MatesReloading = 1;
	Squad.MatesSuppressing = 2;
	Squad.bContact = true;
	Squad.bShotsExchanged = true;
	Squad.QuietSeconds = 15.f;
	TArray<float> Obs = Observe(Fighter, Squad);

	TestEqual(TEXT("health"), Obs[ObsHealth], 0.4f);
	TestEqual(TEXT("clip"), Obs[ObsClip], 0.25f);
	TestEqual(TEXT("ammo"), Obs[ObsAmmo], 0.5f);
	TestEqual(TEXT("reserve"), Obs[ObsHasReserve], 1.f);
	TestEqual(TEXT("in cover"), Obs[ObsInCover], 1.f);
	TestEqual(TEXT("cover quality"), Obs[ObsCoverQuality], 0.8f);
	TestEqual(TEXT("exposed"), Obs[ObsExposed], 1.f);
	TestEqual(TEXT("crouching"), Obs[ObsCrouching], 1.f);
	TestEqual(TEXT("not prone"), Obs[ObsProne], 0.f);
	TestEqual(TEXT("not moving"), Obs[ObsMoving], 0.f);
	TestEqual(TEXT("under fire"), Obs[ObsUnderFire], 1.f);
	TestEqual(TEXT("suppressed"), Obs[ObsSuppressed], 1.f);
	TestEqual(TEXT("fire line"), Obs[ObsFireLine], 1.f);
	TestEqual(TEXT("no launcher"), Obs[ObsLauncherScore], 0.f);
	TestEqual(TEXT("point distance 10 m of 50"), Obs[ObsObjectiveDistance], 0.2f);
	TestEqual(TEXT("off the point (6 m radius)"), Obs[ObsOnPoint], 0.f);
	TestEqual(TEXT("inside the zone (radius x 2)"), Obs[ObsInsideZone], 1.f);
	TestEqual(TEXT("threat known"), Obs[ObsThreatKnown], 1.f);
	TestEqual(TEXT("threat seen"), Obs[ObsThreatSeen], 1.f);
	TestEqual(TEXT("threat 40 m of 100"), Obs[ObsThreatDistance], 0.4f);
	TestEqual(TEXT("threat in range"), Obs[ObsThreatInRange], 1.f);
	TestEqual(TEXT("seen now: age 0"), Obs[ObsThreatAge], 0.f);
	TestEqual(TEXT("threat straight beyond the point: cos 1"), Obs[ObsThreatBearingCos], 1.f);
	TestEqual(TEXT("... sin 0"), Obs[ObsThreatBearingSin], 0.f);
	TestEqual(TEXT("attention"), Obs[ObsAttentionOff], 0.25f);
	TestEqual(TEXT("enemies known 3 of 6"), Obs[ObsEnemiesKnown], 0.5f);
	TestEqual(TEXT("enemies seen 2 of 6"), Obs[ObsEnemiesSeen], 2.f / 6.f);
	TestEqual(TEXT("two mates of 5"), Obs[ObsMatesAlive], 0.4f);
	TestEqual(TEXT("a mate reloading"), Obs[ObsMatesReloading], 1.f / 3.f);
	TestEqual(TEXT("two mates suppressing"), Obs[ObsMatesSuppressing], 2.f / 3.f);
	TestEqual(TEXT("contact"), Obs[ObsContact], 1.f);
	TestEqual(TEXT("shots exchanged"), Obs[ObsShotsExchanged], 1.f);
	TestEqual(TEXT("quiet 15 s of 30"), Obs[ObsQuiet], 0.5f);
	TestEqual(TEXT("time"), Obs[ObsTimeFrac], 0.5f);
	TestEqual(TEXT("hold"), Obs[ObsHoldFrac], 0.2f);
	TestEqual(TEXT("enemy on the point"), Obs[ObsEnemyOnPoint], 1.f);
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("running order one-hot %d"), Index), Obs[ObsRunningOrder + Index], Index == ActionIndexOf(EPmcOrder::Suppress) ? 1.f : 0.f);
	}
	for (int32 Index = 0; Index < ObservationSize; ++Index)
	{
		TestTrue(FString::Printf(TEXT("value %d written and in -1..1"), Index), Obs[Index] >= -1.f && Obs[Index] <= 1.f);
	}

	// The bearing: a threat behind him (away from the point) / to his left of the line to the point.
	Fighter.Threat.Location = FVector(-3000.f, 0.f, 0.f);
	Obs = Observe(Fighter, Squad);
	TestEqual(TEXT("threat behind: cos -1"), Obs[ObsThreatBearingCos], -1.f);
	Fighter.Threat.Location = FVector(0.f, 3000.f, 0.f);
	Obs = Observe(Fighter, Squad);
	TestTrue(TEXT("threat abeam: cos 0, sin +-1"), FMath::IsNearlyZero(Obs[ObsThreatBearingCos], 1e-4f) && FMath::IsNearlyEqual(FMath::Abs(Obs[ObsThreatBearingSin]), 1.f, 1e-4f));
	const float SinLeft = Obs[ObsThreatBearingSin];
	Fighter.Threat.Location = FVector(0.f, -3000.f, 0.f);
	Obs = Observe(Fighter, Squad);
	TestTrue(TEXT("the other side flips the sine"), FMath::IsNearlyEqual(Obs[ObsThreatBearingSin], -SinLeft, 1e-4f));

	// A remembered threat ages; out of range; clamps.
	Fighter.Threat.bSeenNow = false;
	Fighter.Threat.SecondsSince = 5.f;
	Fighter.Threat.DistanceCm = 25000.f;
	Fighter.HealthFrac = 1.7f;
	Squad.EnemiesKnown = 40;
	Squad.QuietSeconds = 500.f;
	Obs = Observe(Fighter, Squad);
	TestEqual(TEXT("remembered: not seen"), Obs[ObsThreatSeen], 0.f);
	TestEqual(TEXT("5 s of 10"), Obs[ObsThreatAge], 0.5f);
	TestEqual(TEXT("distance clamped"), Obs[ObsThreatDistance], 1.f);
	TestEqual(TEXT("out of range"), Obs[ObsThreatInRange], 0.f);
	TestEqual(TEXT("health clamped"), Obs[ObsHealth], 1.f);
	TestEqual(TEXT("enemies clamped"), Obs[ObsEnemiesKnown], 1.f);
	TestEqual(TEXT("quiet clamped"), Obs[ObsQuiet], 1.f);

	// No threat (and the attackers' stand-in point is none): the "none" values.
	FPmcFighterView Calm = Defender(FVector(300.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f));
	Calm.Stance = EOperativeStance::Prone;
	Calm.bHasLauncher = true;
	Calm.LauncherScore = 0.6f;
	Calm.RunningOrder = EPmcOrder::Count;
	Obs = Observe(Calm, FPmcSquadView());
	TestEqual(TEXT("no threat known"), Obs[ObsThreatKnown], 0.f);
	TestEqual(TEXT("no threat: distance 1"), Obs[ObsThreatDistance], 1.f);
	TestEqual(TEXT("no threat: age 1"), Obs[ObsThreatAge], 1.f);
	TestEqual(TEXT("no threat: no fire line"), Obs[ObsFireLine], 0.f);
	TestEqual(TEXT("no threat: bearing 0"), Obs[ObsThreatBearingCos], 0.f);
	TestEqual(TEXT("on the point"), Obs[ObsOnPoint], 1.f);
	TestEqual(TEXT("prone"), Obs[ObsProne], 1.f);
	TestEqual(TEXT("launcher score"), Obs[ObsLauncherScore], 0.6f);
	TestEqual(TEXT("alone: no mates"), Obs[ObsMatesAlive], 0.f);
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		TestEqual(TEXT("no running order"), Obs[ObsRunningOrder + Index], 0.f);
	}
	Calm.bHasThreat = true;
	Calm.Threat.bStandIn = true;
	Calm.Threat.DistanceCm = 500.f;
	Obs = Observe(Calm, FPmcSquadView());
	TestEqual(TEXT("a stand-in point is no threat"), Obs[ObsThreatKnown], 0.f);

	// The measured defended area replaces the radius fallback; a running order outside the action set shows as none.
	FPmcFighterView Far = Defender(FVector(5000.f, 0.f, 0.f), FVector(0.f, 0.f, 0.f));
	TestEqual(TEXT("50 m off: outside the fallback zone"), Observe(Far, FPmcSquadView())[ObsInsideZone], 0.f);
	Far.Objective.bDefenceAreaSet = true;
	Far.Objective.bInsideDefenceArea = true;
	Far.RunningOrder = EPmcOrder::Flank;
	Obs = Observe(Far, FPmcSquadView());
	TestEqual(TEXT("the measured area decides"), Obs[ObsInsideZone], 1.f);
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		TestEqual(TEXT("a flank is outside the action set"), Obs[ObsRunningOrder + Index], 0.f);
	}

	// No objective at all: distance 1, clocks 0.
	FPmcFighterView NoObjective;
	Obs = Observe(NoObjective, FPmcSquadView());
	TestEqual(TEXT("no objective: distance 1"), Obs[ObsObjectiveDistance], 1.f);
	TestEqual(TEXT("no objective: not inside"), Obs[ObsInsideZone], 0.f);
	TestEqual(TEXT("no objective: no clock"), Obs[ObsTimeFrac], 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyRewardTest, "CodexTactics.AI.PmcPolicy.Reward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyRewardTest::RunTest(const FString&)
{
	using namespace PmcPolicyRules;
	FPmcPolicyEvents Events;
	TestEqual(TEXT("nothing happened"), StepReward(Events), 0.f);
	Events.AttackersKilled = 2;
	TestEqual(TEXT("+1 per attacker killed"), StepReward(Events), 2.f);
	Events = FPmcPolicyEvents();
	Events.bDied = true;
	TestEqual(TEXT("-1 own death"), StepReward(Events), -1.f);
	Events = FPmcPolicyEvents();
	Events.bRoundWon = true;
	TestEqual(TEXT("+3 round won"), StepReward(Events), 3.f);
	Events = FPmcPolicyEvents();
	Events.bPointCaptured = true;
	TestEqual(TEXT("-3 point captured"), StepReward(Events), -3.f);
	Events = FPmcPolicyEvents();
	Events.EnemyOnPointSeconds = 0.5f;
	TestEqual(TEXT("-0.02 per second on the point"), StepReward(Events), -0.01f);
	Events.AttackersKilled = 1;
	Events.bRoundWon = true;
	TestEqual(TEXT("the terms add up"), StepReward(Events), 1.f + 3.f - 0.01f);
	Events = FPmcPolicyEvents();
	Events.AttackersKilled = -3;
	Events.EnemyOnPointSeconds = -2.f;
	TestEqual(TEXT("negative counts are no reward"), StepReward(Events), 0.f);
	FPmcPolicyRewards Custom;
	Custom.AttackerKilled = 0.5f;
	Events = FPmcPolicyEvents();
	Events.AttackersKilled = 2;
	TestEqual(TEXT("custom terms"), StepReward(Events, Custom), 1.f);

	// The round's end for the defending side.
	bool bWon = false;
	bool bCaptured = false;
	FPmcObjectiveClock Clock;
	// The attackers (B) eliminated: defender A wins.
	OutcomeEvents(PmcCommanderRules::ResolveRound(2, 0, false, true, Clock, false), true, bWon, bCaptured);
	TestTrue(TEXT("defender A wins by elimination"), bWon && !bCaptured);
	// The same round seen from a defending B: not his win.
	OutcomeEvents(PmcCommanderRules::ResolveRound(2, 0, false, true, Clock, true), false, bWon, bCaptured);
	TestTrue(TEXT("defender B was eliminated"), !bWon && !bCaptured);
	// The attackers (A) capture the point from defender B.
	Clock.bCaptured = true;
	OutcomeEvents(PmcCommanderRules::ResolveRound(2, 2, false, true, Clock, true), false, bWon, bCaptured);
	TestTrue(TEXT("captured: defender B loses the point"), !bWon && bCaptured);
	// Time up with the defenders alone on the point: held.
	Clock = FPmcObjectiveClock();
	Clock.bDefendersHolding = true;
	OutcomeEvents(PmcCommanderRules::ResolveRound(1, 2, true, true, Clock, false), true, bWon, bCaptured);
	TestTrue(TEXT("held at the time limit: defender A wins"), bWon && !bCaptured);
	// Time up, nobody on the point: a draw is neither.
	Clock = FPmcObjectiveClock();
	OutcomeEvents(PmcCommanderRules::ResolveRound(2, 2, true, true, Clock, false), true, bWon, bCaptured);
	TestTrue(TEXT("a draw is neither"), !bWon && !bCaptured);
	// A round still running is neither.
	OutcomeEvents(PmcCommanderRules::ResolveRound(2, 2, false, true, Clock, false), true, bWon, bCaptured);
	TestTrue(TEXT("a running round is neither"), !bWon && !bCaptured);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPmcPolicyBrainStepTest, "CodexTactics.AI.PmcPolicy.BrainStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPmcPolicyBrainStepTest::RunTest(const FString&)
{
	using namespace PmcPolicyTestLocal;
	FScopedPolicyTestWorld Scope(TEXT("PmcPolicyBrainStepWorld"));
	if (!TestNotNull(TEXT("a scratch game world"), Scope.World))
	{
		return false;
	}
	UWorld& World = *Scope.World;

	FPmcPolicyBrainSettings Settings;
	Settings.MaxAgents = 3;
	Settings.Seed = 11;
	Settings.ActionNoiseScale = 1.f;
	FPmcPolicyBrain Brain;
	if (!TestTrue(TEXT("the brain sets up (manager, interactor, policy)"), Brain.Setup(World, Settings)))
	{
		return false;
	}
	TArray<AOperativeCharacter*> Fighters;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Fighters.Add(SpawnPolicyTestFighter(World, Index));
	}
	if (!TestFalse(TEXT("three fighter bodies"), Fighters.Contains(nullptr)))
	{
		return false;
	}
	Brain.BeginRound(Fighters);
	TestEqual(TEXT("three agents"), Brain.GetAgentNum(), 3);
	const int32 ThirdAgent = Brain.FindAgentId(Fighters[2]);
	TestTrue(TEXT("the third fighter is an agent"), ThirdAgent != INDEX_NONE);

	// The third fighter never decides (no request): he steps along on Hold. The hook runs once per step that ended.
	const FPmcWeights W;
	TArray<FPmcOrderRequest> Requests = PolicyTestRequests(Fighters);
	int32 StepEnds = 0;
	int32 RoundEnds = 0;
	int32 DeadSeen = 0;
	Brain.OnStepEnded = [&](bool bRoundOver)
	{
		(bRoundOver ? RoundEnds : StepEnds) += 1;
		DeadSeen += !bRoundOver && Brain.IsAgentDead(ThirdAgent) ? 1 : 0;
	};
	bool bAllowed = true;
	bool bCommitted = true;
	for (int32 Step = 0; Step < 12; ++Step)
	{
		Brain.PickOrders(1, W, Requests);
		bAllowed &= Requests[0].Pick == EPmcOrder::TakeCover || Requests[0].Pick == EPmcOrder::Hold;
		bCommitted &= Requests[1].Pick == EPmcOrder::Suppress;
	}
	TestTrue(TEXT("sampled picks stay inside the mask"), bAllowed);
	TestTrue(TEXT("a young running order is the only pick"), bCommitted);
	TestEqual(TEXT("one inference per decision"), Brain.GetStepsTotal(), 12);
	TestEqual(TEXT("the step hook runs from the second decision on"), StepEnds, 11);
	int32 Picked = 0;
	for (const int32 Count : Brain.GetPickCounts())
	{
		Picked += Count;
	}
	TestEqual(TEXT("every request's pick is counted"), Picked, 24);

	// Without action noise the policy takes its most likely order: the same request, the same pick.
	Brain.SetActionNoiseScale(0.f);
	Brain.PickOrders(1, W, Requests);
	const EPmcOrder Greedy = Requests[0].Pick;
	Brain.PickOrders(1, W, Requests);
	TestTrue(TEXT("greedy picks repeat"), Requests[0].Pick == Greedy);

	// Save / load: another brain (other seed, other weights) plays the saved policy; another network shape refuses the files.
	const FString PolicyDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("PmcPolicyTest"));
	const FString PolicyBase = PolicyDir / TEXT("policy");
	TestTrue(TEXT("the policy saves"), Brain.SavePolicy(PolicyBase));
	TestTrue(TEXT("three snapshot files"), FPmcPolicyBrain::HasPolicyFiles(PolicyBase));
	{
		FPmcPolicyBrainSettings OtherSettings = Settings;
		OtherSettings.Seed = 999;
		OtherSettings.ActionNoiseScale = 0.f;
		FPmcPolicyBrain Other;
		TestTrue(TEXT("a second brain sets up"), Other.Setup(World, OtherSettings));
		TestTrue(TEXT("... and loads the saved policy"), Other.LoadPolicy(PolicyBase));
		Other.BeginRound(Fighters);
		TArray<FPmcOrderRequest> OtherRequests = PolicyTestRequests(Fighters);
		Other.PickOrders(1, W, OtherRequests);
		TestTrue(TEXT("the loaded policy picks what the saved one picks"), OtherRequests[0].Pick == Greedy);
		Other.EndRound();

		FPmcPolicyBrainSettings WideSettings = Settings;
		WideSettings.HiddenLayerSize = 32;
		FPmcPolicyBrain Wide;
		TestTrue(TEXT("a brain of another shape sets up"), Wide.Setup(World, WideSettings));
		AddExpectedError(TEXT("does not fit this build's policy"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("... and refuses the other shape's files"), Wide.LoadPolicy(PolicyBase));
	}
	AddExpectedError(TEXT("no policy at"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("a missing policy does not load"), Brain.LoadPolicy(PolicyDir / TEXT("nothing_here")));
	IFileManager::Get().DeleteDirectory(*PolicyDir, false, true);

	// A fighter falls: the hook still sees his agent (dead) once, then he leaves the policy.
	Fighters[2]->Destroy();
	Brain.PickOrders(1, W, Requests);
	TestEqual(TEXT("the hook saw the fallen agent"), DeadSeen, 1);
	TestEqual(TEXT("the fallen agent left the policy"), Brain.GetAgentNum(), 2);
	TestEqual(TEXT("... and has no id any more"), Brain.FindAgentId(Fighters[2]), static_cast<int32>(INDEX_NONE));

	Brain.EndRound();
	TestEqual(TEXT("the round's end closes the last step once"), RoundEnds, 1);
	TestEqual(TEXT("no agents after the round"), Brain.GetAgentNum(), 0);
	Brain.Shutdown();
	TestFalse(TEXT("shut down"), Brain.IsSetup());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
