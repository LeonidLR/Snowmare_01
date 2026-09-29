#include "Misc/AutomationTest.h"
#include "Characters/EnemyCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/GodotBalanceAsset.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot enemy_base.gd / enemy_frost_*.gd apply_balance_config parity against the imported DA_GameBalanceConfig.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyBalanceApplyTest, "CodexTactics.Combat.Enemy.BalanceApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEnemyBalanceApplyTest::RunTest(const FString&)
{
	const UGodotBalanceAsset* Config = LoadObject<UGodotBalanceAsset>(nullptr, TEXT("/Game/Data/Balance/DA_GameBalanceConfig.DA_GameBalanceConfig"));
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

#endif // WITH_DEV_AUTOMATION_TESTS
