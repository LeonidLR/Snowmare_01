#include "Misc/AutomationTest.h"
#include "Bot/BotStealthRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Playtest bot stealth decisions (Bot/BotStealthRules.h; UE-only, user plan 2026-10-07). Default perception params:
// sight 25 m, half-angle 50 deg, crouch x0.7, prone x0.4, proximity 3 m, footsteps walk 5.5 / crouch 2.5 / crawl 1 m.

namespace BotStealthTest
{
	FBotPatrolView PatrolAtOrigin()
	{
		FBotPatrolView View;
		View.Location = FVector::ZeroVector;
		View.Forward = FVector::ForwardVector; // looks along +X
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotStealthRiskTest, "CodexTactics.Bot.Stealth.Risk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotStealthRiskTest::RunTest(const FString&)
{
	const FBotStealthConfig Config;
	const FEnemyPerceptionParams Params;
	const FVector Eye = FVector::ZeroVector;
	const FVector Forward = FVector::ForwardVector;
	// In front at 20 m: a standing man is inside 25 m x 1.15, a prone one (x0.4 = 11.5 m) is not.
	TestTrue(TEXT("standing in view at 20 m is at risk"), BotStealthRules::SightRisk(Params, Eye, Forward, FVector(2000, 0, 0), EOperativeStance::Standing, true, Config) >= 1.f);
	TestTrue(TEXT("prone at 20 m is safe"), BotStealthRules::SightRisk(Params, Eye, Forward, FVector(2000, 0, 0), EOperativeStance::Prone, true, Config) < 1.f);
	TestEqual(TEXT("a blocked eye line sees nothing"), BotStealthRules::SightRisk(Params, Eye, Forward, FVector(500, 0, 0), EOperativeStance::Standing, false, Config), 0.f);
	TestEqual(TEXT("behind it (outside the widened view, beyond 3 m) is unseen"),
		BotStealthRules::SightRisk(Params, Eye, Forward, FVector(-800, 0, 0), EOperativeStance::Standing, true, Config), 0.f);
	TestTrue(TEXT("but within the proximity radius it is seen all round"),
		BotStealthRules::SightRisk(Params, Eye, Forward, FVector(-250, 0, 0), EOperativeStance::Standing, true, Config) >= 1.f);
	// 50 + 20 deg widened view: 60 deg off the facing still counts (patrols turn).
	TestTrue(TEXT("60 deg off the facing at 10 m is in the widened view"),
		BotStealthRules::SightRisk(Params, Eye, Forward, FVector(500, 866, 0), EOperativeStance::Standing, true, Config) >= 1.f);

	TestEqual(TEXT("standing still is silent"), BotStealthRules::HearingRisk(Params, ESquadMovementNoise::Still, 100.f, Config), 0.f);
	TestTrue(TEXT("walking 6 m away is heard (5.5 m x 1.35)"), BotStealthRules::HearingRisk(Params, ESquadMovementNoise::Walk, 600.f, Config) >= 1.f);
	TestTrue(TEXT("crouch-walking 6 m away is not"), BotStealthRules::HearingRisk(Params, ESquadMovementNoise::CrouchWalk, 600.f, Config) < 1.f);
	TestTrue(TEXT("gaits by stance"), BotStealthRules::MovingNoise(EOperativeStance::Prone) == ESquadMovementNoise::Crawl
		&& BotStealthRules::MovingNoise(EOperativeStance::Crouching) == ESquadMovementNoise::CrouchWalk
		&& BotStealthRules::MovingNoise(EOperativeStance::Standing) == ESquadMovementNoise::Walk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotStealthStanceTest, "CodexTactics.Bot.Stealth.Stance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotStealthStanceTest::RunTest(const FString&)
{
	const FBotStealthConfig Config;
	const TArray<FBotPatrolView> Patrols = { BotStealthTest::PatrolAtOrigin() };
	const TArray<bool> Clear = { true };
	// Behind the patrol only the footsteps count: 9 m walk, 6 m crouch, 2 m crawl.
	TestTrue(TEXT("9 m behind: stand"), BotStealthRules::ChooseSneakStance(Patrols, Clear, FVector(-900, 0, 0), Config) == EOperativeStance::Standing);
	TestTrue(TEXT("6 m behind: crouch"), BotStealthRules::ChooseSneakStance(Patrols, Clear, FVector(-600, 0, 0), Config) == EOperativeStance::Crouching);
	TestTrue(TEXT("2 m behind: crawl"), BotStealthRules::ChooseSneakStance(Patrols, Clear, FVector(-200, 0, 0), Config) == EOperativeStance::Prone);
	// In front: 30 m stand (beyond 28.75 m), 20 m crawl (crouched is still seen: 25 x 0.7 x 1.15 = 20.1 m).
	TestTrue(TEXT("30 m in front: stand"), BotStealthRules::ChooseSneakStance(Patrols, Clear, FVector(3000, 0, 0), Config) == EOperativeStance::Standing);
	TestTrue(TEXT("20 m in front: crawl"), BotStealthRules::ChooseSneakStance(Patrols, Clear, FVector(2000, 0, 0), Config) == EOperativeStance::Prone);
	TestTrue(TEXT("20 m in front behind a wall: stand"),
		BotStealthRules::ChooseSneakStance(Patrols, { false }, FVector(2000, 0, 0), Config) == EOperativeStance::Standing);
	TestTrue(TEXT("no patrols: stand"), BotStealthRules::ChooseSneakStance({}, {}, FVector::ZeroVector, Config) == EOperativeStance::Standing);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotStealthDecideTest, "CodexTactics.Bot.Stealth.Decide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotStealthDecideTest::RunTest(const FString&)
{
	FBotStealthConfig Config;
	Config.AmbushRangeFraction = 0.75f; // ambush range 10.5 m with a 14 m rifle
	Config.AlarmSuspicion = 0.5f;
	Config.MaxStealthSeconds = 200.f;
	FBotStealthInput Base;
	Base.bHasTarget = true;
	Base.RifleRangeCm = 1400.f;
	Base.TargetDistanceCm = 3000.f;
	EBotAmbushReason Reason = EBotAmbushReason::None;

	TestTrue(TEXT("far from the target: sneak"), BotStealthRules::Decide(Base, Config, Reason) == EBotStealthAction::Sneak && Reason == EBotAmbushReason::None);

	FBotStealthInput In = Base;
	In.TargetDistanceCm = 900.f;
	In.bSquadUnseen = true;
	TestTrue(TEXT("in range and unseen: ambush in position"),
		BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::InPosition);
	In.bSquadUnseen = false;
	TestTrue(TEXT("in range but exposed: hold (take cover)"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Hold);
	In.bLeaderInCover = true;
	TestTrue(TEXT("in range, exposed but in cover: ambush"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush);

	In = Base;
	In.MaxSuspicion = 0.6f;
	In.SuspiciousDistanceCm = 1300.f;
	TestTrue(TEXT("a suspicious patrol within rifle range: strike first"),
		BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::PreEmptive);
	In.SuspiciousDistanceCm = 2500.f;
	TestTrue(TEXT("a suspicious patrol out of reach: hide"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Hide);

	In = Base;
	In.SearcherDistanceCm = 2000.f;
	TestTrue(TEXT("a search far away: keep still"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Hide);
	In.SearcherDistanceCm = 1000.f;
	TestTrue(TEXT("a searcher walks into the ambush range: strike"),
		BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::SearchContact);

	In = Base;
	In.bTrapPending = true;
	TestTrue(TEXT("waiting for the trap: hide"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Hide);

	In = Base;
	In.ElapsedSeconds = 200.f;
	TestTrue(TEXT("sneaking too long: forced strike"),
		BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::Forced);

	// Hounds: the ambush range grows to their smell reach (capped at the rifle), a nose about to work forces the strike.
	In = Base;
	In.MinAmbushRangeCm = 1300.f;
	TestEqual(TEXT("smell reach widens the ambush range"), BotStealthRules::AmbushRange(In, Config), 1300.f);
	In.MinAmbushRangeCm = 1620.f;
	TestEqual(TEXT("but never beyond the rifle"), BotStealthRules::AmbushRange(In, Config), 1400.f);
	In.TargetDistanceCm = 1350.f;
	In.bSquadUnseen = true;
	TestTrue(TEXT("unseen at 13.5 m with a hound around: ambush"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush);
	In = Base;
	In.bSmellImminent = true;
	In.TargetDistanceCm = 1200.f;
	TestTrue(TEXT("a nose about to smell the squad: strike first"),
		BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::PreEmptive);
	{
		TArray<FBotPatrolView> Patrols = { BotStealthTest::PatrolAtOrigin(), BotStealthTest::PatrolAtOrigin() };
		Patrols[1].Params.SmellRadiusCm = 1200.f;
		TestEqual(TEXT("smell reach = radius x hearing margin"), BotStealthRules::SmellReach(Patrols, Config), 1200.f * Config.HearingMargin, 0.01f);
		TestEqual(TEXT("no nose, no reach"), BotStealthRules::SmellReach({ BotStealthTest::PatrolAtOrigin() }, Config), 0.f);
	}

	// Patience (MinSneakSeconds): no planned ambush yet, dodge noses; a suspicious patrol is still struck at once.
	FBotStealthConfig Patient = Config;
	Patient.MinSneakSeconds = 60.f;
	In = Base;
	In.ElapsedSeconds = 30.f;
	In.TargetDistanceCm = 900.f;
	In.bSquadUnseen = true;
	TestTrue(TEXT("patient: in position but holds"), BotStealthRules::Decide(In, Patient, Reason) == EBotStealthAction::Hold);
	In.ElapsedSeconds = 61.f;
	TestTrue(TEXT("patience over: ambush"), BotStealthRules::Decide(In, Patient, Reason) == EBotStealthAction::Ambush);
	In.ElapsedSeconds = 30.f;
	In.bSmellImminent = true;
	TestTrue(TEXT("patient: backs away from a nose"), BotStealthRules::Decide(In, Patient, Reason) == EBotStealthAction::Evade);
	In.MaxSuspicion = 0.9f;
	In.SuspiciousDistanceCm = 900.f;
	TestTrue(TEXT("patient but nearly spotted: strike first"),
		BotStealthRules::Decide(In, Patient, Reason) == EBotStealthAction::Ambush && Reason == EBotAmbushReason::PreEmptive);
	TestTrue(TEXT("seeded patience 20..120 s"), BotStealthRules::MakeSeededConfig(5, false).MinSneakSeconds >= 20.f
		&& BotStealthRules::MakeSeededConfig(5, false).MinSneakSeconds <= 120.f);

	In = Base;
	In.bHasTarget = false;
	TestTrue(TEXT("no target: sneak on"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Sneak);
	In.SearcherDistanceCm = 500.f;
	TestTrue(TEXT("no target but a search: hide"), BotStealthRules::Decide(In, Config, Reason) == EBotStealthAction::Hide);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotStealthApproachTest, "CodexTactics.Bot.Stealth.Approach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotStealthApproachTest::RunTest(const FString&)
{
	// Patrol at the origin looking +X, the squad 30 m to its left-rear: the approach point lies behind it, on the squad's side.
	const FVector Point = BotStealthRules::ApproachPoint(FVector(0, 0, 50), FVector::ForwardVector, FVector(-2000, 2000, 0), 1000.f);
	TestEqual(TEXT("at the stand-off"), static_cast<float>(Point.Size2D()), 1000.f, 1.f);
	TestTrue(TEXT("behind the patrol"), Point.X < 0.f);
	TestTrue(TEXT("on the squad's side"), Point.Y > 0.f);
	TestEqual(TEXT("at the patrol's height"), static_cast<float>(Point.Z), 50.f);
	const FVector Ahead = BotStealthRules::ApproachPoint(FVector::ZeroVector, FVector::ForwardVector, FVector(3000, 0, 0), 800.f);
	TestTrue(TEXT("a squad straight ahead still circles behind"), Ahead.Size2D() > 799.f);

	TestTrue(TEXT("sneaking holds the fire"), BotStealthRules::PostureFor(false) == ESquadFirePosture::Passive);
	TestTrue(TEXT("the fight fires at will"), BotStealthRules::PostureFor(true) == ESquadFirePosture::Aggressive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotStealthSeedTest, "CodexTactics.Bot.Stealth.Seed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBotStealthSeedTest::RunTest(const FString&)
{
	bool bAnyTrap = false;
	bool bAnyNoTrap = false;
	bool bVaries = false;
	const FBotStealthConfig First = BotStealthRules::MakeSeededConfig(1, true);
	for (int32 Seed = 1; Seed <= 16; ++Seed)
	{
		const FBotStealthConfig A = BotStealthRules::MakeSeededConfig(Seed, true);
		const FBotStealthConfig B = BotStealthRules::MakeSeededConfig(Seed, true);
		TestTrue(FString::Printf(TEXT("seed %d is deterministic"), Seed), A.AmbushRangeFraction == B.AmbushRangeFraction
			&& A.AlarmSuspicion == B.AlarmSuspicion && A.MaxStealthSeconds == B.MaxStealthSeconds && A.bUseTrap == B.bUseTrap);
		TestTrue(TEXT("ambush range 0.55..0.9"), A.AmbushRangeFraction >= 0.55f && A.AmbushRangeFraction <= 0.9f);
		TestTrue(TEXT("alarm 0.35..0.65"), A.AlarmSuspicion >= 0.35f && A.AlarmSuspicion <= 0.65f);
		TestTrue(TEXT("limit 180..300 s"), A.MaxStealthSeconds >= 180.f && A.MaxStealthSeconds <= 300.f);
		TestFalse(TEXT("no mine, no trap"), BotStealthRules::MakeSeededConfig(Seed, false).bUseTrap);
		bAnyTrap |= A.bUseTrap;
		bAnyNoTrap |= !A.bUseTrap;
		bVaries |= A.AmbushRangeFraction != First.AmbushRangeFraction;
	}
	TestTrue(TEXT("seeds vary the plan (some lay a trap, some do not)"), bAnyTrap && bAnyNoTrap && bVaries);
	return true;
}

#endif
