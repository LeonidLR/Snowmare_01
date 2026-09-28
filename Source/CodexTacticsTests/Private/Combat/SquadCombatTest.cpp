#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Data/CombatTypes.h"
#include "Data/WeaponDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SQUAD_COMBAT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Combat.Squad." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

SQUAD_COMBAT_TEST(FSquadCombatWeaponEquipTest, "EquipWeaponInitializesAmmo")
bool FSquadCombatWeaponEquipTest::RunTest(const FString&)
{
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->MaxClipSize = 25;
	Weapon->DefaultReserveAmmo = 100;
	Weapon->BaseDamage = 22.0f;

	Operative->EquipWeapon(Weapon);

	TestEqual(TEXT("Clip size initialized"), Operative->CurrentClip, 25);
	TestEqual(TEXT("Reserve ammo initialized"), Operative->ReserveAmmo, 100);
	TestFalse(TEXT("Not reloading initially"), Operative->bIsReloading);
	return true;
}

SQUAD_COMBAT_TEST(FSquadCombatShootDepletesClipTest, "ShootDepletesClip")
bool FSquadCombatShootDepletesClipTest::RunTest(const FString&)
{
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	Operative->CurrentClip = 10;
	Operative->bForceHitForTesting = true;

	AActor* DummyTarget = NewObject<AActor>();
	UHealthComponent* TargetHealth = NewObject<UHealthComponent>(DummyTarget);
	TargetHealth->SetMaxHealth(100.0f);
	TargetHealth->SetBaseArmorReduction(0.0f);
	DummyTarget->AddOwnedComponent(TargetHealth);

	const bool bFired = Operative->ShootAtTarget(DummyTarget);

	TestTrue(TEXT("Shot fired successfully"), bFired);
	TestEqual(TEXT("Clip decremented to 9"), Operative->CurrentClip, 9);
	TestTrue(TEXT("Target took damage"), TargetHealth->GetCurrentHealth() < 100.0f);
	return true;
}

SQUAD_COMBAT_TEST(FSquadCombatColdMisfireTest, "ColdMisfireAtOrAboveSixtyPercent")
bool FSquadCombatColdMisfireTest::RunTest(const FString&)
{
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	Operative->CurrentClip = 10;
	Operative->ColdLevel = 75.0f;
	Operative->bForceMisfireForTesting = true;

	AActor* DummyTarget = NewObject<AActor>();
	UHealthComponent* TargetHealth = NewObject<UHealthComponent>(DummyTarget);
	TargetHealth->SetMaxHealth(100.0f);
	DummyTarget->AddOwnedComponent(TargetHealth);

	bool bMisfireEventFired = false;
	Operative->OnWeaponMisfiredNative.AddLambda([&bMisfireEventFired](AOperativeCharacter*)
	{
		bMisfireEventFired = true;
	});

	const bool bHit = Operative->ShootAtTarget(DummyTarget);

	TestFalse(TEXT("Misfire does not hit"), bHit);
	TestTrue(TEXT("Misfire event was broadcast"), bMisfireEventFired);
	// Godot player.gd misfire_delay 0.45 s (not the directive's 1.5 s).
	TestEqual(TEXT("Misfire sets 0.45s cooldown"), Operative->MisfireCooldownTimer, 0.45f);
	TestEqual(TEXT("Clip still decremented on misfire chamber attempt"), Operative->CurrentClip, 9);
	TestEqual(TEXT("Target took no damage on misfire"), TargetHealth->GetCurrentHealth(), 100.0f);
	return true;
}

SQUAD_COMBAT_TEST(FSquadCombatReloadRefillsClipTest, "ReloadRefillsClipFromReserve")
bool FSquadCombatReloadRefillsClipTest::RunTest(const FString&)
{
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	UWeaponDataAsset* Weapon = NewObject<UWeaponDataAsset>();
	Weapon->MaxClipSize = 30;
	Weapon->ReloadTime = 1.0f;
	Operative->EquipWeapon(Weapon);

	Operative->CurrentClip = 5;
	Operative->ReserveAmmo = 50;

	Operative->StartReload();
	TestTrue(TEXT("Reload started"), Operative->bIsReloading);
	TestEqual(TEXT("Reload timer matches weapon"), Operative->ReloadTimer, 1.0f);

	// Advance time past reload duration
	Operative->ProcessCombatShooting(1.2f);

	TestFalse(TEXT("Reload finished"), Operative->bIsReloading);
	TestEqual(TEXT("Clip restored to 30"), Operative->CurrentClip, 30);
	TestEqual(TEXT("Reserve ammo deducted by 25"), Operative->ReserveAmmo, 25);
	return true;
}

#endif
