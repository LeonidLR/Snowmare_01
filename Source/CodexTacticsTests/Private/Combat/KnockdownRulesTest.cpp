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

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownChanceTest, "CodexTactics.Combat.Knockdown.Chance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownChanceTest::RunTest(const FString&)
{
	// User decision 2026-10-11: a qualifying heavy hit / heavy blow knocks down with 50 % (the roll is passed in);
	// a blast within 2.5 m and a pounce always do.
	const FKnockdownConfig Config;
	TestEqual(TEXT("Default chance 0.5"), Config.KnockdownChance, 0.5f);
	const FKnockdownTarget Operative;
	FKnockdownHit Hit;
	Hit.Cause = EKnockdownCause::HeavyHit;
	Hit.Damage = 45.f;
	Hit.ChanceRoll = 0.49f;
	TestTrue(TEXT("Heavy hit, roll 0.49 < 0.5: down"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));
	Hit.ChanceRoll = 0.51f;
	TestFalse(TEXT("Heavy hit, roll 0.51: stays up"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));
	Hit.ChanceRoll = 0.5f;
	TestFalse(TEXT("Heavy hit, roll exactly 0.5: stays up (roll < chance)"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));
	Hit.Damage = 39.f;
	Hit.ChanceRoll = 0.f;
	TestFalse(TEXT("Below the threshold the roll does not matter"), KnockdownRules::ShouldKnockDown(Config, Hit, Operative));

	FKnockdownHit Blow;
	Blow.Cause = EKnockdownCause::HeavyMelee;
	Blow.ChanceRoll = 0.49f;
	TestTrue(TEXT("Brute heavy blow, roll 0.49: down"), KnockdownRules::ShouldKnockDown(Config, Blow, Operative));
	Blow.ChanceRoll = 0.51f;
	TestFalse(TEXT("Brute heavy blow, roll 0.51: stays up"), KnockdownRules::ShouldKnockDown(Config, Blow, Operative));

	FKnockdownHit Blast;
	Blast.Cause = EKnockdownCause::Explosion;
	Blast.ExplosionDistance = 200.f;
	Blast.ChanceRoll = 0.99f;
	TestTrue(TEXT("Explosion within 2.5 m: always down (roll ignored)"), KnockdownRules::ShouldKnockDown(Config, Blast, Operative));
	FKnockdownHit Pounce;
	Pounce.Cause = EKnockdownCause::Pounce;
	Pounce.ChanceRoll = 0.99f;
	TestTrue(TEXT("Hound / cutter pounce: always down (dedicated knockdown attack)"), KnockdownRules::ShouldKnockDown(Config, Pounce, Operative));
	TestTrue(TEXT("Heavy hit rolls"), KnockdownRules::UsesKnockdownChance(EKnockdownCause::HeavyHit));
	TestTrue(TEXT("Heavy melee rolls"), KnockdownRules::UsesKnockdownChance(EKnockdownCause::HeavyMelee));
	TestFalse(TEXT("Explosion never rolls"), KnockdownRules::UsesKnockdownChance(EKnockdownCause::Explosion));
	TestFalse(TEXT("Pounce never rolls"), KnockdownRules::UsesKnockdownChance(EKnockdownCause::Pounce));

	FKnockdownConfig Always = Config;
	Always.KnockdownChance = 1.f; // what the smokes force (Codex.Knockdown.Chance 1)
	Hit.Damage = 45.f;
	Hit.ChanceRoll = 0.999f;
	TestTrue(TEXT("Chance 1: every qualifying hit"), KnockdownRules::ShouldKnockDown(Always, Hit, Operative));
	FKnockdownConfig Never = Config;
	Never.KnockdownChance = 0.f;
	Hit.ChanceRoll = 0.f;
	TestFalse(TEXT("Chance 0: none"), KnockdownRules::ShouldKnockDown(Never, Hit, Operative));
	TestTrue(TEXT("Chance 0: blasts still floor him"), KnockdownRules::ShouldKnockDown(Never, Blast, Operative));

	// The brute's poise still decides before the roll.
	FKnockdownTarget Brute;
	Brute.bHeavyPoise = true;
	Hit.ChanceRoll = 0.1f;
	Hit.bCritical = false;
	TestFalse(TEXT("Brute: plain heavy hit with a good roll: poise holds"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	Hit.bCritical = true;
	TestTrue(TEXT("Brute: critical heavy hit with a good roll: down"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	Hit.ChanceRoll = 0.9f;
	TestFalse(TEXT("Brute: critical heavy hit with a bad roll: up"), KnockdownRules::ShouldKnockDown(Config, Hit, Brute));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownDownedDamageTest, "CodexTactics.Combat.Knockdown.DownedDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKnockdownDownedDamageTest::RunTest(const FString& Parameters)
{
	// User decision 2026-10-11: any blow (ranged, melee, explosion) on a unit lying after a knockdown lands x1.33, real
	// time and turn-based (one helper: AOperativeCharacter::TakeHit incl. the grid path, UHealthComponent::TakeDamage).
	const FKnockdownConfig Config;
	TestEqual(TEXT("Default x1.33"), Config.DownedDamageMultiplier, 1.33f);
	TestEqual(TEXT("Down: x1.33"), KnockdownRules::DownedDamageMultiplier(Config, true), 1.33f);
	TestEqual(TEXT("Up: x1"), KnockdownRules::DownedDamageMultiplier(Config, false), 1.f);
	// The turn-based grid hits are whole numbers (TakeHit's bypass path rounds): 30 -> 40, 20 -> 27, 1 -> 1.
	TestEqual(TEXT("Shot / bite / blast 30 -> 39.9"), 30.f * KnockdownRules::DownedDamageMultiplier(Config, true), 39.9f, 0.001f);
	TestEqual(TEXT("Grid 30 -> 40"), FMath::RoundToInt(30.f * KnockdownRules::DownedDamageMultiplier(Config, true)), 40);
	TestEqual(TEXT("Grid 20 -> 27"), FMath::RoundToInt(20.f * KnockdownRules::DownedDamageMultiplier(Config, true)), 27);
	TestEqual(TEXT("Grid 1 -> 1"), FMath::Max(1, FMath::RoundToInt(1.f * KnockdownRules::DownedDamageMultiplier(Config, true))), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownPhaseTest, "CodexTactics.Combat.Knockdown.PhasesAndPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownPhaseTest::RunTest(const FString&)
{
	const FKnockdownConfig Config;
	// User decision 2026-10-11: faster get-up — downed 0.6 s (was 1.5), get-up 0.8 s (was 1.0), max play rate 2.1x.
	TestEqual(TEXT("Downed 0.6 s"), Config.DownedSeconds, 0.6f);
	TestEqual(TEXT("Get-up 0.8 s"), Config.GetUpSeconds, 0.8f);
	TestEqual(TEXT("Fall fallback unchanged 0.8 s"), Config.FallSeconds, 0.8f);
	TestEqual(TEXT("Revive_Back 1.667 s -> 0.8 s at 2.08x"), KnockdownRules::ClipPlayRate(1.667f, Config.GetUpSeconds, Config.GetUpMaxPlayRate), 1.667f / 0.8f, 0.001f);
	TestEqual(TEXT("Revive_Front 2.5 s capped at 2.1x"), KnockdownRules::ClipPlayRate(2.5f, Config.GetUpSeconds, Config.GetUpMaxPlayRate), 2.1f, 0.001f);
	TestEqual(TEXT("A short clip is never slowed down"), KnockdownRules::ClipPlayRate(0.5f, 1.f, 1.7f), 1.f, 0.001f);

	FKnockdownState State = KnockdownRules::Start(Config, EKnockdownDirection::Back, EKnockdownCause::Pounce, 0.867f, 1.667f, false);
	TestEqual(TEXT("Starts falling"), State.Phase, EKnockdownPhase::Falling);
	TestEqual(TEXT("Fall = clip length"), State.FallDuration, 0.867f, 0.001f);
	TestEqual(TEXT("Get-up = 0.8 s"), State.GetUpDuration, 0.8f, 0.001f);
	TestEqual(TEXT("Downed = 0.6 s"), State.DownedDuration, 0.6f, 0.001f);
	TestTrue(TEXT("On the ground ~2.2 s in all (fall 0.87 + 0.6 + 0.8), was ~3.3 s"),
		FMath::IsNearlyEqual(State.FallDuration + State.DownedDuration + State.GetUpDuration, 2.267f, 0.01f));
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

	KnockdownRules::Advance(State, 0.267f, false);
	TestEqual(TEXT("Bar half full at 0.3 s of 0.6"), KnockdownRules::RecoveryFraction(State), 0.5f, 0.01f);
	TestEqual(TEXT("Still lying at 0.58 s"), KnockdownRules::Advance(State, 0.28f, false), EKnockdownStep::None);
	TestEqual(TEXT("Gets up after 0.6 s"), KnockdownRules::Advance(State, 0.03f, false), EKnockdownStep::StartGetUp);
	TestEqual(TEXT("Bar full while getting up"), KnockdownRules::RecoveryFraction(State), 1.f);
	TestEqual(TEXT("Still getting up at 0.75 s"), KnockdownRules::Advance(State, 0.75f, false), EKnockdownStep::None);
	TestEqual(TEXT("Recovers after the 0.8 s get-up"), KnockdownRules::Advance(State, 0.06f, false), EKnockdownStep::Recovered);
	TestFalse(TEXT("Up again"), KnockdownRules::IsDown(State));

	// No clip known: spec fallback durations.
	const FKnockdownState NoClip = KnockdownRules::Start(Config, EKnockdownDirection::None, EKnockdownCause::Explosion, 0.f, 0.f, false);
	TestEqual(TEXT("Fallback fall 0.8 s"), NoClip.FallDuration, 0.8f);
	TestEqual(TEXT("Fallback get-up 0.8 s"), NoClip.GetUpDuration, 0.8f);
	TestEqual(TEXT("No direction = back"), NoClip.Direction, EKnockdownDirection::Back);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKnockdownTurnBasedTest, "CodexTactics.Combat.Knockdown.TurnBasedGetUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKnockdownTurnBasedTest::RunTest(const FString&)
{
	const FKnockdownConfig Config;
	FKnockdownState State = KnockdownRules::Start(Config, EKnockdownDirection::Front, EKnockdownCause::HeavyMelee, 0.933f, 2.5f, true);
	TestEqual(TEXT("Get-up costs 2 AP"), Config.GetUpActionPoints, 2);
	TestFalse(TEXT("Still falling: no get-up yet"), KnockdownRules::DecideTurnGetUp(Config, State, 8).bGetUp || KnockdownRules::DecideTurnGetUp(Config, State, 8).bNotEnoughAP);
	KnockdownRules::Advance(State, 1.f, false);
	TestEqual(TEXT("Downed"), State.Phase, EKnockdownPhase::Downed);
	// No free auto-stand: however long the turn-based wait (his turn starting included), only the paid order gets him up.
	TestEqual(TEXT("Turn-based: the timer does not get him up"), KnockdownRules::Advance(State, 5.f, false), EKnockdownStep::None);
	TestEqual(TEXT("... nor a whole round later"), KnockdownRules::Advance(State, 60.f, false), EKnockdownStep::None);
	TestEqual(TEXT("Still downed (he may stay down)"), State.Phase, EKnockdownPhase::Downed);
	TestTrue(TEXT("Get-up due"), State.bGetUpReady);
	TestEqual(TEXT("Bar full when due"), KnockdownRules::RecoveryFraction(State), 1.f);

	const FKnockdownTurnDecision Full = KnockdownRules::DecideTurnGetUp(Config, State, 8);
	TestTrue(TEXT("8 AP: gets up"), Full.bGetUp);
	TestEqual(TEXT("Costs 2 AP"), Full.ActionPointsSpent, 2);
	TestFalse(TEXT("Enough AP"), Full.bNotEnoughAP);
	const FKnockdownTurnDecision Two = KnockdownRules::DecideTurnGetUp(Config, State, 2);
	TestTrue(TEXT("Exactly 2 AP: gets up"), Two.bGetUp);
	const FKnockdownTurnDecision One = KnockdownRules::DecideTurnGetUp(Config, State, 1);
	TestFalse(TEXT("1 AP: stays down"), One.bGetUp);
	TestTrue(TEXT("1 AP: not enough AP (feed line, the turn goes on)"), One.bNotEnoughAP);
	TestEqual(TEXT("1 AP: nothing spent"), One.ActionPointsSpent, 0);
	TestEqual(TEXT("A refused get-up leaves him lying"), State.Phase, EKnockdownPhase::Downed);

	TestTrue(TEXT("Paid: get-up starts"), KnockdownRules::BeginTurnGetUp(State));
	TestEqual(TEXT("Getting up"), State.Phase, EKnockdownPhase::GettingUp);
	TestEqual(TEXT("Revive_Front at 2.1x"), State.GetUpDuration, 2.5f / 2.1f, 0.01f);
	const FKnockdownTurnDecision Again = KnockdownRules::DecideTurnGetUp(Config, State, 8);
	TestFalse(TEXT("Already getting up: no second payment"), Again.bGetUp || Again.ActionPointsSpent > 0);
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
