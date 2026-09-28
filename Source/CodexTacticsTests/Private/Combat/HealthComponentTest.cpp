#include "Misc/AutomationTest.h"
#include "Combat/HealthComponent.h"
#include "Data/CombatTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#define HEALTH_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Health." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

HEALTH_TEST(FHealthArmorReducesDamageTest, "ArmorReducesDamage")
bool FHealthArmorReducesDamageTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(200.0f);
	Health->BaseArmorReduction = 0.10f; // 10% armor

	FDamageSpec Spec;
	Spec.Amount = 100.0f;
	Spec.DamageType = EDamageType::Kinetic;
	Spec.ArmorPenetration = 0.0f;

	const float Dealt = Health->TakeDamage(Spec);
	TestEqual(TEXT("Dealt 90 damage after 10% armor"), Dealt, 90.0f);
	TestEqual(TEXT("Remaining health is 110"), Health->GetCurrentHealth(), 110.0f);
	return true;
}

HEALTH_TEST(FHealthArmorPenetrationTest, "ArmorPenetrationReducesEffectiveArmor")
bool FHealthArmorPenetrationTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(200.0f);
	Health->BaseArmorReduction = 0.20f; // 20% armor

	FDamageSpec Spec;
	Spec.Amount = 100.0f;
	Spec.DamageType = EDamageType::Kinetic;
	Spec.ArmorPenetration = 0.50f; // 50% pen -> 10% effective armor

	const float Dealt = Health->TakeDamage(Spec);
	TestEqual(TEXT("Dealt 90 damage with 50% penetration of 20% armor"), Dealt, 90.0f);
	return true;
}

HEALTH_TEST(FHealthArmorShredStatusTest, "ArmorShredCutsArmorBySeventyPercent")
bool FHealthArmorShredStatusTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(300.0f);
	Health->BaseArmorReduction = 0.20f; // 20% armor

	// Apply Armor Shred status
	FDamageSpec ShredSpec;
	ShredSpec.Amount = 10.0f;
	ShredSpec.StatusEffect = EStatusEffect::ArmorShred;
	ShredSpec.StatusDuration = 6.0f;
	Health->TakeDamage(ShredSpec);

	TestTrue(TEXT("Has Armor Shred effect"), Health->HasStatusEffect(EStatusEffect::ArmorShred));

	// Next hit should face effective armor = 20% * 0.3 = 6%
	FDamageSpec NextSpec;
	NextSpec.Amount = 100.0f;
	NextSpec.DamageType = EDamageType::Kinetic;
	const float Dealt = Health->TakeDamage(NextSpec);

	TestEqual(TEXT("Dealt 94 damage after shredded armor (6%)"), Dealt, 94.0f);
	return true;
}

HEALTH_TEST(FHealthElementalAffinityTest, "ElementalAffinityMultiplier")
bool FHealthElementalAffinityTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(300.0f);
	Health->BaseArmorReduction = 0.10f;
	Health->ElementalAffinities.Fire = 1.50f; // 1.5x fire vulnerability

	FDamageSpec FireSpec;
	FireSpec.Amount = 100.0f;
	FireSpec.DamageType = EDamageType::Fire;

	// (100 * (1 - 0.10)) * 1.5 = 90 * 1.5 = 135
	const float Dealt = Health->TakeDamage(FireSpec);
	TestEqual(TEXT("Dealt 135 damage with 1.5x fire affinity"), Dealt, 135.0f);
	return true;
}

HEALTH_TEST(FHealthElementalImmunityTest, "ElementalImmunityDealsZero")
bool FHealthElementalImmunityTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(100.0f);
	Health->ElementalAffinities.Cryo = 0.0f; // Cryo immunity

	FDamageSpec CryoSpec;
	CryoSpec.Amount = 50.0f;
	CryoSpec.DamageType = EDamageType::Cryo;

	const float Dealt = Health->TakeDamage(CryoSpec);
	TestEqual(TEXT("Immunity deals 0 damage"), Dealt, 0.0f);
	TestEqual(TEXT("Health unchanged"), Health->GetCurrentHealth(), 100.0f);
	return true;
}

HEALTH_TEST(FHealthStanceDefenseTest, "StanceDefenseMultiplierReducesDamage")
bool FHealthStanceDefenseTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(200.0f);
	Health->BaseArmorReduction = 0.0f;
	Health->SetDefenseMultiplier(0.75f); // Crouching defense: 0.75x

	FDamageSpec Spec;
	Spec.Amount = 100.0f;
	Spec.DamageType = EDamageType::Kinetic;

	const float Dealt = Health->TakeDamage(Spec);
	TestEqual(TEXT("Dealt 75 damage in crouching stance"), Dealt, 75.0f);
	return true;
}

HEALTH_TEST(FHealthLethalDamageFiresDeathTest, "LethalDamageFiresDeathEvent")
bool FHealthLethalDamageFiresDeathTest::RunTest(const FString&)
{
	UHealthComponent* Health = NewObject<UHealthComponent>();
	Health->SetMaxHealth(50.0f);
	Health->BaseArmorReduction = 0.0f;

	bool bDeathEventFired = false;
	FString Attacker;
	Health->OnDiedNative.AddLambda([&](AActor*, const FString& Source)
	{
		bDeathEventFired = true;
		Attacker = Source;
	});

	FDamageSpec Lethal;
	Lethal.Amount = 60.0f;
	Lethal.AttackerSource = TEXT("Sniper");

	Health->TakeDamage(Lethal);
	TestFalse(TEXT("Target is not alive"), Health->IsAlive());
	TestEqual(TEXT("Health clamped to 0"), Health->GetCurrentHealth(), 0.0f);
	TestTrue(TEXT("OnDied fired"), bDeathEventFired);
	TestEqual(TEXT("Attacker passed correctly"), Attacker, FString(TEXT("Sniper")));
	return true;
}

#undef HEALTH_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
