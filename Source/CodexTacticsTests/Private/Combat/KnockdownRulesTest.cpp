#include "Misc/AutomationTest.h"
#include "Combat/KnockdownRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Sprint 14 Knockdown & Recovery (TANDEM request #12, Gemini spec 2026-10-08; UE-only, no Godot reference).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownDirectionTest, "CodexTactics.Combat.Knockdown.Direction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownDirectionTest::RunTest(const FString&)
{
	const FVector Forward(1.f, 0.f, 0.f);
	TestEqual(TEXT("Attacker straight ahead: falls on his back"), KnockdownRules::DirectionFromAttack(Forward, FVector(500.f, 0.f, 0.f)), EKnockdownDirection::Back);
	TestEqual(TEXT("Attacker 60 deg off the front: back"), KnockdownRules::DirectionFromAttack(Forward, FVector(100.f, 173.f, 0.f)), EKnockdownDirection::Back);
	TestEqual(TEXT("Attacker 89 deg (front side): back"), KnockdownRules::DirectionFromAttack(Forward, FVector(1.7f, 100.f, 0.f)), EKnockdownDirection::Back);
	TestEqual(TEXT("Exactly 90 deg counts as behind: face"), KnockdownRules::DirectionFromAttack(Forward, FVector(0.f, 100.f, 0.f)), EKnockdownDirection::Front);
	TestEqual(TEXT("Attacker behind: falls on his face"), KnockdownRules::DirectionFromAttack(Forward, FVector(-300.f, 20.f, 0.f)), EKnockdownDirection::Front);
	TestEqual(TEXT("Height ignored (blast below / above)"), KnockdownRules::DirectionFromAttack(Forward, FVector(50.f, 0.f, -900.f)), EKnockdownDirection::Back);
	TestEqual(TEXT("No direction = frontal"), KnockdownRules::DirectionFromAttack(Forward, FVector::ZeroVector), EKnockdownDirection::Back);
	TestEqual(TEXT("From locations: source behind a unit facing +Y"),
		KnockdownRules::DirectionFromLocations(FVector(0.f, 1.f, 0.f), FVector(100.f, 100.f, 0.f), FVector(100.f, -200.f, 0.f)), EKnockdownDirection::Front);
	TestEqual(TEXT("Impact angle 180"), KnockdownRules::ImpactAngleDeg(Forward, FVector(-1.f, 0.f, 0.f)), 180.f, 0.01f);
	TestEqual(TEXT("Clips: back"), FString(KnockdownRules::FallClipName(EKnockdownDirection::Back)), FString(TEXT("Knocked_Back")));
	TestEqual(TEXT("Clips: face get-up"), FString(KnockdownRules::GetUpClipName(EKnockdownDirection::Front)), FString(TEXT("Revive_Front")));
	TestEqual(TEXT("Clips: back death"), FString(KnockdownRules::DeathClipName(EKnockdownDirection::Back)), FString(TEXT("Death_Back")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownTriggerTest, "CodexTactics.Combat.Knockdown.Triggers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownTriggerTest::RunTest(const FString&)
{
	const FKnockdownConfig Config;
	FKnockdownTarget Operative;
	FKnockdownHit Hit;
	Hit.Cause = EKnockdownCause::HeavyHit;

	Hit.Damage = 39.9f;
	TestFalse(TEXT("39.9 HP: stays up"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));
	Hit.Damage = 40.f;
	TestTrue(TEXT("40 HP: knocked down"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));

	FKnockdownHit Blast;
	Blast.Cause = EKnockdownCause::Explosion;
	Blast.ExplosionDistance = 240.f;
	TestTrue(TEXT("Blast 2.4 m: down"), KnockdownRules::ShouldKnockDown(Config, Blast, Operative));
	Blast.ExplosionDistance = 250.f;
	TestFalse(TEXT("Blast 2.5 m: up (< 2.5 m only)"), KnockdownRules::ShouldKnockDown(Config, Blast, Operative));

	FKnockdownHit Pounce;
	Pounce.Cause = EKnockdownCause::Pounce;
	TestTrue(TEXT("Pounce landing: always down"), KnockdownRules::ShouldKnockDown(Config, Pounce, Operative));

	// Frost Brute poise.
	FKnockdownTarget Brute;
	Brute.bHeavyPoise = true;
	Hit.Damage = 80.f;
	Hit.bCritical = false;
	TestFalse(TEXT("Brute: a plain 80 HP shot does not floor him"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	Hit.bCritical = true;
	TestTrue(TEXT("Brute: a critical 80 HP hit does"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	Hit.Damage = 30.f;
	TestFalse(TEXT("Brute: a weak crit does not"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	Blast.ExplosionDistance = 100.f;
	TestTrue(TEXT("Brute: a close blast floors him"), KnockdownRules::ShouldKnockDown(Config, Blast, Brute));
	TestFalse(TEXT("Brute: a pounce does not"), KnockdownRules::ShouldKnockDown(Config, Pounce, Brute));

	FKnockdownTarget Hound;
	Hound.bCanBeKnockedDown = false;
	TestFalse(TEXT("Hound / cutter: never"), KnockdownRules::ShouldKnockDown(Config, Blast, Hound));
	FKnockdownTarget Down;
	Down.bAlreadyDown = true;
	TestFalse(TEXT("Already down: no second knockdown"), KnockdownRules::ShouldKnockDown(Config, Blast, Down));
	FKnockdownTarget JustUp;
	JustUp.SecondsSinceGetUp = 0.5f;
	TestFalse(TEXT("Re-knock immunity right after getting up"), KnockdownRules::ShouldKnockDown(Config, Pounce, JustUp));
	JustUp.SecondsSinceGetUp = 2.f;
	TestTrue(TEXT("Immunity over"), KnockdownRules::ShouldKnockDown(Config, Pounce, JustUp));

	TestTrue(TEXT("Melee enemy: downed man 4 m off, target 3 m: switch"), KnockdownRules::PreferDownedTarget(Config, true, 400.f, 300.f));
	TestFalse(TEXT("Melee enemy: downed man 7 m off: keep"), KnockdownRules::PreferDownedTarget(Config, true, 700.f, 800.f));
	TestFalse(TEXT("Melee enemy: downed man 5 m, target 1 m: keep"), KnockdownRules::PreferDownedTarget(Config, true, 500.f, 100.f));
	TestFalse(TEXT("Ranged enemy: keeps its pick"), KnockdownRules::PreferDownedTarget(Config, false, 100.f, 900.f));
	TestTrue(TEXT("Hound: 1.2 s run-up, cooldown over = pounce"), KnockdownRules::IsHoundPounce(Config, 1.2f, 30.f));
	TestFalse(TEXT("Hound: standing bite"), KnockdownRules::IsHoundPounce(Config, 0.3f, 30.f));
	TestFalse(TEXT("Hound: pounce on cooldown"), KnockdownRules::IsHoundPounce(Config, 2.f, 5.f));

	TestEqual(TEXT("Downed: ranged damage reduced"), KnockdownRules::DownedDamageMultiplier(Config, false), 0.6f);
	TestEqual(TEXT("Downed: melee damage bonus"), KnockdownRules::DownedDamageMultiplier(Config, true), 1.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownPhaseTest, "CodexTactics.Combat.Knockdown.PhasesAndPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownPhaseTest::RunTest(const FString&)
{
	const FKnockdownConfig Config;
	TestEqual(TEXT("Revive_Back 1.667 s -> 1.0 s at 1.667x"), KnockdownRules::ClipPlayRate(1.667f, 1.f, 1.7f), 1.667f, 0.001f);
	TestEqual(TEXT("Revive_Front 2.5 s capped at 1.7x"), KnockdownRules::ClipPlayRate(2.5f, 1.f, 1.7f), 1.7f, 0.001f);
	TestEqual(TEXT("A short clip is never slowed down"), KnockdownRules::ClipPlayRate(0.5f, 1.f, 1.7f), 1.f, 0.001f);

	FKnockdownState State = KnockdownRules::Start(Config, EKnockdownDirection::Back, EKnockdownCause::Pounce, 0.867f, 1.667f, false);
	TestEqual(TEXT("Starts falling"), State.Phase, EKnockdownPhase::Falling);
	TestEqual(TEXT("Fall = clip length"), State.FallDuration, 0.867f, 0.001f);
	TestEqual(TEXT("Get-up = 1.0 s"), State.GetUpDuration, 1.f, 0.001f);
	TestTrue(TEXT("Down"), KnockdownRules::IsDown(State));
	TestEqual(TEXT("Bar empty while falling"), KnockdownRules::RecoveryFraction(State), 0.f);

	TestEqual(TEXT("Still falling at 0.8 s"), KnockdownRules::Advance(State, 0.8f, false), EKnockdownStep::None);
	TestEqual(TEXT("Lands at 0.867 s"), KnockdownRules::Advance(State, 0.1f, false), EKnockdownStep::Landed);
	TestEqual(TEXT("Downed"), State.Phase, EKnockdownPhase::Downed);
	TestEqual(TEXT("Overshoot kept"), State.PhaseElapsed, 0.033f, 0.002f);

	// Tactical pause / dialogue: the bar stops.
	const float Before = KnockdownRules::RecoveryFraction(State);
	TestTrue(TEXT("Tactical pause freezes"), KnockdownRules::AreTimersFrozen(true, false));
	TestTrue(TEXT("Dialogue freezes"), KnockdownRules::AreTimersFrozen(false, true));
	TestFalse(TEXT("Real time runs"), KnockdownRules::AreTimersFrozen(false, false));
	TestEqual(TEXT("Frozen: no step"), KnockdownRules::Advance(State, 10.f, true), EKnockdownStep::None);
	TestEqual(TEXT("Frozen: bar unchanged"), KnockdownRules::RecoveryFraction(State), Before);

	KnockdownRules::Advance(State, 0.717f, false);
	TestEqual(TEXT("Bar half full at 0.75 s of 1.5"), KnockdownRules::RecoveryFraction(State), 0.5f, 0.01f);
	TestEqual(TEXT("Gets up after 1.5 s"), KnockdownRules::Advance(State, 0.76f, false), EKnockdownStep::StartGetUp);
	TestEqual(TEXT("Bar full while getting up"), KnockdownRules::RecoveryFraction(State), 1.f);
	TestEqual(TEXT("Recovers after the get-up"), KnockdownRules::Advance(State, 1.f, false), EKnockdownStep::Recovered);
	TestFalse(TEXT("Up again"), KnockdownRules::IsDown(State));

	// No clip known: spec fallback durations.
	const FKnockdownState NoClip = KnockdownRules::Start(Config, EKnockdownDirection::None, EKnockdownCause::Explosion, 0.f, 0.f, false);
	TestEqual(TEXT("Fallback fall 0.8 s"), NoClip.FallDuration, 0.8f);
	TestEqual(TEXT("Fallback get-up 1.0 s"), NoClip.GetUpDuration, 1.f);
	TestEqual(TEXT("No direction = back"), NoClip.Direction, EKnockdownDirection::Back);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownTurnBasedTest, "CodexTactics.Combat.Knockdown.TurnBasedGetUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownTurnBasedTest::RunTest(const FString&)
{
	const FKnockdownConfig Config;
	FKnockdownState State = KnockdownRules::Start(Config, EKnockdownDirection::Front, EKnockdownCause::HeavyMelee, 0.933f, 2.5f, true);
	TestFalse(TEXT("Still falling: no get-up decision yet"), KnockdownRules::DecideTurnGetUp(Config, State, 8).bGetUp || KnockdownRules::DecideTurnGetUp(Config, State, 8).bSkipTurn);
	KnockdownRules::Advance(State, 1.f, false);
	TestEqual(TEXT("Downed"), State.Phase, EKnockdownPhase::Downed);
	TestEqual(TEXT("Turn-based: the timer does not get him up"), KnockdownRules::Advance(State, 5.f, false), EKnockdownStep::None);
	TestEqual(TEXT("Still downed"), State.Phase, EKnockdownPhase::Downed);
	TestTrue(TEXT("Get-up due"), State.bGetUpReady);
	TestEqual(TEXT("Bar full when due"), KnockdownRules::RecoveryFraction(State), 1.f);

	const FKnockdownTurnDecision Full = KnockdownRules::DecideTurnGetUp(Config, State, 8);
	TestTrue(TEXT("8 AP: gets up"), Full.bGetUp);
	TestEqual(TEXT("Costs 2 AP"), Full.ActionPointsSpent, 2);
	TestFalse(TEXT("Turn not skipped"), Full.bSkipTurn);
	const FKnockdownTurnDecision Two = KnockdownRules::DecideTurnGetUp(Config, State, 2);
	TestTrue(TEXT("Exactly 2 AP: gets up"), Two.bGetUp);
	const FKnockdownTurnDecision One = KnockdownRules::DecideTurnGetUp(Config, State, 1);
	TestFalse(TEXT("1 AP: stays down"), One.bGetUp);
	TestTrue(TEXT("1 AP: turn skipped"), One.bSkipTurn);
	TestEqual(TEXT("1 AP: nothing spent"), One.ActionPointsSpent, 0);

	TestTrue(TEXT("Paid: get-up starts"), KnockdownRules::BeginTurnGetUp(State));
	TestEqual(TEXT("Getting up"), State.Phase, EKnockdownPhase::GettingUp);
	TestEqual(TEXT("Revive_Front at 1.7x"), State.GetUpDuration, 2.5f / 1.7f, 0.01f);
	TestEqual(TEXT("Recovers"), KnockdownRules::Advance(State, 2.f, false), EKnockdownStep::Recovered);

	FKnockdownState Up;
	TestFalse(TEXT("Standing unit: no get-up decision"), KnockdownRules::DecideTurnGetUp(Config, Up, 8).bGetUp);
	TestFalse(TEXT("Standing unit: nothing to begin"), KnockdownRules::BeginTurnGetUp(Up));

	// Leaving turn-based mid-wait: the timer takes over again.
	FKnockdownState Switch = KnockdownRules::Start(Config, EKnockdownDirection::Back, EKnockdownCause::Pounce, 0.5f, 1.f, true);
	KnockdownRules::Advance(Switch, 0.5f, false);
	KnockdownRules::Advance(Switch, 3.f, false);
	KnockdownRules::SetTurnBased(Switch, false);
	TestEqual(TEXT("Back in real time: the get-up starts"), KnockdownRules::Advance(Switch, 0.01f, false), EKnockdownStep::StartGetUp);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
