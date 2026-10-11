#include "Misc/AutomationTest.h"
#include "Characters/OperativeHitRules.h"
#include "Combat/HealthComponent.h"
#include "Data/CombatTypes.h"
#include "Tactics/TurnBasedRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Audit 2026-10-11 area 2 (#2, #3): every damage modifier of a turn-based hit applies exactly once.
// Godot turn_based_combat_manager.gd subtracted the grid armour and then enemy_base.gd take_damage its % armour again,
// and _detonate_barrel / _detonate_mine pre-multiplied the stance before player.gd take_damage applied its stance defence.

#define TURN_ONCE_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Tactics.TurnBased.DamageOnce." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

TURN_ONCE_TEST(FTurnShotArmorOnceTest, "SquadShotArmorOnce")
bool FTurnShotArmorOnceTest::RunTest(const FString&)
{
	// Brute-like target: grid armour 5, health component armour 20 %.
	const int32 GridDamage = TurnBasedRules::SquadAttackDamage(30.f, 1.f, 5.f, 1.f);
	TestEqual(TEXT("grid damage = 30 - 5"), GridDamage, 25);

	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(200.f);
	Health->BaseArmorReduction = 0.20f;
	FDamageSpec Spec;
	Spec.Amount = GridDamage;
	Spec.DamageType = EDamageType::Kinetic;
	Spec.ArmorPenetration = TurnBasedRules::SquadShotArmorPenetration;
	TestEqual(TEXT("the health lost is the logged grid damage (no second armour cut)"), Health->TakeDamage(Spec), 25.f);
	TestEqual(TEXT("health 200 - 25"), Health->GetCurrentHealth(), 175.f);

	// The turret / a blast has no grid armour: its single cut is the health component's % armour.
	FDamageSpec Turret;
	Turret.Amount = 25.f;
	TestEqual(TEXT("the turret keeps the % armour (once)"), Health->TakeDamage(Turret), 20.f);
	return true;
}

TURN_ONCE_TEST(FTurnBlastStanceOnceTest, "BlastStanceOnce")
bool FTurnBlastStanceOnceTest::RunTest(const FString&)
{
	const FTurnBasedBalance Balance;
	const float Fortitude = 20.f; // Medic-Sapper: 30 % cut
	const auto GridBlast = [&](EOperativeStance Stance, float Downed)
	{
		const float Amount = FMath::Max(1, FMath::RoundToInt(Balance.BarrelDamage * TurnBasedRules::StanceDamageMultiplier(Stance, Balance)));
		// The real-time stance defence (crouch 0.75) is passed in and must be ignored: the grid factor is already in Amount.
		const float RealTimeDefense = Stance == EOperativeStance::Crouching ? 0.75f : (Stance == EOperativeStance::Prone ? 0.5f : 1.f);
		return OperativeHitRules::FinalDamage(EOperativeHitKind::GridBlast, Amount, RealTimeDefense, Fortitude, Downed);
	};
	TestEqual(TEXT("standing barrel: 80 x 0.7 fortitude"), GridBlast(EOperativeStance::Standing, 1.f), 56.f, 0.01f);
	TestEqual(TEXT("crouched barrel: round(80 x 0.70) = 56, x 0.7 fortitude, no 0.75 again"), GridBlast(EOperativeStance::Crouching, 1.f), 39.2f, 0.01f);
	TestEqual(TEXT("prone barrel: 40 x 0.7, no 0.5 again"), GridBlast(EOperativeStance::Prone, 1.f), 28.f, 0.01f);
	TestEqual(TEXT("downed x1.33 once"), GridBlast(EOperativeStance::Standing, 1.33f), 56.f * 1.33f, 0.01f);
	TestEqual(TEXT("at least 1"), OperativeHitRules::FinalDamage(EOperativeHitKind::GridBlast, 1.f, 1.f, 33.f, 1.f), 1.f);

	TestFalse(TEXT("grid blast: no stance defence"), OperativeHitRules::UsesStanceDefense(EOperativeHitKind::GridBlast));
	TestTrue(TEXT("grid blast: fortitude"), OperativeHitRules::UsesFortitude(EOperativeHitKind::GridBlast));
	TestFalse(TEXT("grid blast: no cover"), OperativeHitRules::UsesCover(EOperativeHitKind::GridBlast));
	TestFalse(TEXT("grid blast: no heavy-hit knockdown (NotifyExplosion does it)"), OperativeHitRules::AllowsHeavyHitKnockdown(EOperativeHitKind::GridBlast));
	return true;
}

TURN_ONCE_TEST(FTurnBlastDodgeTest, "BlastDodgeKeptFromGodot")
bool FTurnBlastDodgeTest::RunTest(const FString&)
{
	// Godot _detonate_* -> player.gd take_damage(dmg) without bypass_avoidance: luck * 0.4 % dodge (luck 35 -> 14 %).
	TestTrue(TEXT("grid blast can dodge (Godot)"), OperativeHitRules::IsDodged(EOperativeHitKind::GridBlast, 13.9f, 35.f));
	TestFalse(TEXT("grid blast: roll above the chance lands"), OperativeHitRules::IsDodged(EOperativeHitKind::GridBlast, 14.f, 35.f));
	TestTrue(TEXT("attack can dodge"), OperativeHitRules::IsDodged(EOperativeHitKind::Attack, 0.f, 25.f));
	TestFalse(TEXT("unavoidable (real-time blast, grid shot / bite) never dodges"), OperativeHitRules::IsDodged(EOperativeHitKind::Unavoidable, -1.f, 100.f));
	return true;
}

TURN_ONCE_TEST(FTurnHitKindsUnchangedTest, "AttackAndUnavoidableUnchanged")
bool FTurnHitKindsUnchangedTest::RunTest(const FString&)
{
	// Real-time attack: amount x stance defence x (1 - fortitude cut) x downed (player.gd take_damage).
	TestEqual(TEXT("crouched hit 40 x 0.75 x 0.775"), OperativeHitRules::FinalDamage(EOperativeHitKind::Attack, 40.f, 0.75f, 15.f, 1.f), 23.25f, 0.01f);
	TestEqual(TEXT("fortitude cut capped at 50 %"), OperativeHitRules::FortitudeCut(60.f), 0.5f);
	TestTrue(TEXT("attack: stance defence, cover, heavy-hit knockdown"), OperativeHitRules::UsesStanceDefense(EOperativeHitKind::Attack)
		&& OperativeHitRules::UsesCover(EOperativeHitKind::Attack) && OperativeHitRules::AllowsHeavyHitKnockdown(EOperativeHitKind::Attack));
	// Unavoidable (bBypassAvoidance): the grid shot / bite already carries the grid stance; only the downed x1.33, rounded.
	TestEqual(TEXT("grid bite 20 while down -> 27"), OperativeHitRules::FinalDamage(EOperativeHitKind::Unavoidable, 20.f, 0.75f, 15.f, 1.33f), 27.f);
	TestEqual(TEXT("grid shot 30 while down -> 40"), OperativeHitRules::FinalDamage(EOperativeHitKind::Unavoidable, 30.f, 0.5f, 50.f, 1.33f), 40.f);
	TestEqual(TEXT("unavoidable standing: as given"), OperativeHitRules::FinalDamage(EOperativeHitKind::Unavoidable, 13.f, 0.75f, 15.f, 1.f), 13.f);
	TestEqual(TEXT("unavoidable: at least 1"), OperativeHitRules::FinalDamage(EOperativeHitKind::Unavoidable, 0.4f, 1.f, 15.f, 1.f), 1.f);
	return true;
}

#endif
