#include "Misc/AutomationTest.h"
#include "../GodotBalanceFixture.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/GodotBalanceAsset.h"
#include "Data/WaveConfigTypes.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot enemy_base.gd / enemy_frost_*.gd apply_balance_config parity against the Godot balance fixture (GodotBalanceFixture.h).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceApplyTest, "CodexTactics.Combat.Enemy.BalanceApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyBalanceApplyTest::RunTest(const FString&)
{
	const UGodotBalanceAsset* Config = GodotBalanceFixture::MakeGameBalanceConfig();
	if (!TestNotNull(TEXT("DA_GameBalanceConfig"), Config))
	{
		return false;
	}
	AEnemyCharacter* Hound = NewObject<AEnemyCharacter>();
	Hound->InitializeArchetype(EEnemyArchetype::FrostHound);
	Hound->ApplyBalance(*Config);
	TestEqual(TEXT("Hound health"), Hound->GetHealthComponent()->GetMaxHealth(), 45.f);
	TestEqual(TEXT("Hound speed 5.4 m/s"), Hound->GetCharacterMovement()->MaxWalkSpeed, 540.f, 0.01f);
	TestEqual(TEXT("Hound damage"), Hound->GetAttackDamage(), 12.f);

	AEnemyCharacter* Brute = NewObject<AEnemyCharacter>();
	Brute->InitializeArchetype(EEnemyArchetype::Brute);
	Brute->ApplyBalance(*Config);
	TestEqual(TEXT("Brute health"), Brute->GetHealthComponent()->GetMaxHealth(), 220.f);
	TestEqual(TEXT("Brute damage"), Brute->GetAttackDamage(), 35.f);
	return true;
}

// Godot main.gd _spawn_custom_json_wave: wave_modifiers and custom_stats.health.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyWaveModifiersTest, "CodexTactics.Combat.Enemy.WaveModifiers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyWaveModifiersTest::RunTest(const FString&)
{
	AEnemyCharacter* Hound = NewObject<AEnemyCharacter>();
	Hound->InitializeArchetype(EEnemyArchetype::FrostHound);
	const float Health = Hound->GetHealthComponent()->GetMaxHealth();
	const float Damage = Hound->GetAttackDamage();
	const float Speed = Hound->GetCharacterMovement()->MaxWalkSpeed;
	Hound->ApplyWaveModifiers(1.1f, 1.05f, 1.2f);
	TestEqual(TEXT("Health x hp_mult"), Hound->GetHealthComponent()->GetMaxHealth(), Health * 1.1f, 0.01f);
	TestEqual(TEXT("Full health"), Hound->GetHealthComponent()->GetCurrentHealth(), Health * 1.1f, 0.01f);
	TestEqual(TEXT("Damage x damage_mult"), Hound->GetAttackDamage(), Damage * 1.05f, 0.01f);
	TestEqual(TEXT("Speed x speed_mult"), Hound->GetCharacterMovement()->MaxWalkSpeed, Speed * 1.2f, 0.01f);

	AEnemyCharacter* Brute = NewObject<AEnemyCharacter>();
	Brute->InitializeArchetype(EEnemyArchetype::Brute);
	Brute->ApplyWaveModifiers(1.1f, 1.f, 1.f, 100.f);
	TestEqual(TEXT("custom_stats.health x hp_mult"), Brute->GetHealthComponent()->GetMaxHealth(), 110.f, 0.01f);

	// The other custom_stats (Godot units: m/s, m, s): damage / speed times the wave multiplier, range / cooldown as given.
	AEnemyCharacter* Custom = NewObject<AEnemyCharacter>();
	Custom->InitializeArchetype(EEnemyArchetype::Frostbitten);
	FWaveModifiers Mods;
	Mods.EnemyDamageMult = 1.5f;
	Mods.EnemySpeedMult = 2.f;
	FEnemySpawnEntry Entry;
	Entry.CustomDamage = 20.f;
	Entry.CustomSpeed = 3.f;
	Entry.CustomAttackRange = 2.5f;
	Entry.CustomAttackCooldown = 0.8f;
	Custom->ApplySpawnEntry(Mods, Entry);
	TestEqual(TEXT("custom_stats.damage x damage_mult"), Custom->GetAttackDamage(), 30.f, 0.01f);
	TestEqual(TEXT("custom_stats.speed (m/s) x speed_mult"), Custom->GetCharacterMovement()->MaxWalkSpeed, 600.f, 0.01f);
	TestEqual(TEXT("custom_stats.attack_range (m)"), Custom->GetAttackRange(), 250.f, 0.01f);
	TestEqual(TEXT("custom_stats.attack_cooldown (s)"), Custom->GetAttackCooldown(), 0.8f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
