#include "Misc/AutomationTest.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Data/CombatTypes.h"
#include "Data/WaveConfigTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#define ENEMY_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Enemy." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

ENEMY_TEST(FEnemyHoundArchetypeTest, "HoundArchetypeStats")
bool FEnemyHoundArchetypeTest::RunTest(const FString&)
{
	AEnemyCharacter* Hound = NewObject<AEnemyCharacter>();
	Hound->InitializeArchetype(EEnemyArchetype::FrostHound);

	TestNotNull(TEXT("Hound has HealthComponent"), Hound->GetHealthComponent());
	TestEqual(TEXT("Hound max health is 45"), Hound->GetHealthComponent()->GetMaxHealth(), 45.0f);
	TestTrue(TEXT("Hound armor tier is Light"), Hound->GetHealthComponent()->GetArmorTier() == EArmorTier::Light);
	TestEqual(TEXT("Hound base armor reduction is 0.10"), Hound->GetHealthComponent()->GetBaseArmorReduction(), 0.10f);
	TestEqual(TEXT("Hound attack damage is 12"), Hound->GetAttackDamage(), 12.0f);
	TestEqual(TEXT("Hound attack range is 180 cm"), Hound->GetAttackRange(), 180.0f);
	TestEqual(TEXT("Hound attack cooldown is 1.0s"), Hound->GetAttackCooldown(), 1.0f);
	return true;
}

ENEMY_TEST(FEnemySpitterArchetypeTest, "SpitterArchetypeStats")
bool FEnemySpitterArchetypeTest::RunTest(const FString&)
{
	AEnemyCharacter* Spitter = NewObject<AEnemyCharacter>();
	Spitter->InitializeArchetype(EEnemyArchetype::Spitter);

	TestNotNull(TEXT("Spitter has HealthComponent"), Spitter->GetHealthComponent());
	TestEqual(TEXT("Spitter max health is 70"), Spitter->GetHealthComponent()->GetMaxHealth(), 70.0f);
	TestTrue(TEXT("Spitter armor tier is Medium"), Spitter->GetHealthComponent()->GetArmorTier() == EArmorTier::Medium);
	TestEqual(TEXT("Spitter base armor reduction is 0.40"), Spitter->GetHealthComponent()->GetBaseArmorReduction(), 0.40f);
	TestEqual(TEXT("Spitter attack damage is 18"), Spitter->GetAttackDamage(), 18.0f);
	TestEqual(TEXT("Spitter attack range is 1500 cm"), Spitter->GetAttackRange(), 1500.0f);
	TestEqual(TEXT("Spitter attack cooldown is 2.2s"), Spitter->GetAttackCooldown(), 2.2f);
	return true;
}

ENEMY_TEST(FEnemyBruteArchetypeTest, "BruteArchetypeStats")
bool FEnemyBruteArchetypeTest::RunTest(const FString&)
{
	AEnemyCharacter* Brute = NewObject<AEnemyCharacter>();
	Brute->InitializeArchetype(EEnemyArchetype::Brute);

	TestNotNull(TEXT("Brute has HealthComponent"), Brute->GetHealthComponent());
	TestEqual(TEXT("Brute max health is 220"), Brute->GetHealthComponent()->GetMaxHealth(), 220.0f);
	TestTrue(TEXT("Brute armor tier is Heavy"), Brute->GetHealthComponent()->GetArmorTier() == EArmorTier::Heavy);
	TestEqual(TEXT("Brute base armor reduction is 0.75"), Brute->GetHealthComponent()->GetBaseArmorReduction(), 0.75f);
	TestEqual(TEXT("Brute attack damage is 35"), Brute->GetAttackDamage(), 35.0f);
	TestEqual(TEXT("Brute attack range is 240 cm"), Brute->GetAttackRange(), 240.0f);
	TestEqual(TEXT("Brute attack cooldown is 2.0s"), Brute->GetAttackCooldown(), 2.0f);
	return true;
}

ENEMY_TEST(FEnemyAttackDealsDamageTest, "AttackDealsDamageToTarget")
bool FEnemyAttackDealsDamageTest::RunTest(const FString&)
{
	AEnemyCharacter* Hound = NewObject<AEnemyCharacter>();
	Hound->InitializeArchetype(EEnemyArchetype::FrostHound);

	// Target with health component
	AActor* DummyTarget = NewObject<AActor>();
	UHealthComponent* TargetHealth = NewObject<UHealthComponent>(DummyTarget);
	TargetHealth->SetMaxHealth(100.0f);
	TargetHealth->SetBaseArmorReduction(0.0f);
	DummyTarget->AddOwnedComponent(TargetHealth);

	Hound->AttackTarget(DummyTarget);

	TestTrue(TEXT("Target took damage from enemy attack"), TargetHealth->GetCurrentHealth() < 100.0f);
	TestTrue(TEXT("Target health reduced by at least base damage"), TargetHealth->GetCurrentHealth() <= 88.0f);
	return true;
}

ENEMY_TEST(FWaveDefinitionCalculationTest, "WaveDefinitionTotalCounts")
bool FWaveDefinitionCalculationTest::RunTest(const FString&)
{
	FWaveDefinition Wave;
	Wave.WaveIndex = 1;

	FEnemySpawnEntry Hounds;
	Hounds.EnemyType = EEnemyArchetype::FrostHound;
	Hounds.Count = 5;
	Wave.Spawns.Add(Hounds);

	FEnemySpawnEntry Spitters;
	Spitters.EnemyType = EEnemyArchetype::Spitter;
	Spitters.Count = 3;
	Wave.Spawns.Add(Spitters);

	TestEqual(TEXT("Total enemies calculated"), Wave.GetTotalEnemyCount(), 8);
	return true;
}

#endif
