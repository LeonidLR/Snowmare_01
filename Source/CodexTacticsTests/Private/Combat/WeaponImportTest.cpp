#include "Misc/AutomationTest.h"
#include "Data/WeaponDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Imported weapon assets (Scripts/Editor/import_weapons.py) match Godot resources/weapons/*.tres.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponImportParityTest, "CodexTactics.Data.WeaponImportParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponImportParityTest::RunTest(const FString&)
{
	const UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16"));
	const UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol"));
	const UWeaponDataAsset* Plasma = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_plasma_carbine.DA_Weapon_plasma_carbine"));
	if (!TestNotNull(TEXT("M16 asset"), M16) || !TestNotNull(TEXT("Pistol asset"), Pistol) || !TestNotNull(TEXT("Plasma asset"), Plasma))
	{
		return false;
	}
	// rifle_m16.tres
	TestEqual(TEXT("M16 name"), M16->WeaponName.ToString(), FString(TEXT("Автомат МТКМ-16")));
	TestEqual(TEXT("M16 damage"), M16->BaseDamage, 18.f);
	TestEqual(TEXT("M16 range 14 m"), M16->AttackRangeCm, 1400.f);
	TestEqual(TEXT("M16 clip"), M16->MaxClipSize, 30);
	TestEqual(TEXT("M16 reload"), M16->ReloadTime, 2.68f);
	TestEqual(TEXT("M16 hit curve"), M16->BaseHitChances.Num(), 5);
	TestEqual(TEXT("M16 3 cells"), M16->GetHitChanceForDistance(3), 0.75f);
	TestTrue(TEXT("M16 8 rays"), M16->AttackShape == EAttackShape::Rays8);
	// pistol_beretta.tres: 4 rays, infinite reserve (-1)
	TestTrue(TEXT("Pistol 4 rays"), Pistol->AttackShape == EAttackShape::Rays4);
	TestEqual(TEXT("Pistol reserve -1"), Pistol->DefaultReserveAmmo, -1);
	// plasma_carbine.tres: energy + SHOCKED (mapped by name)
	TestTrue(TEXT("Plasma energy"), Plasma->DamageType == EDamageType::Energy);
	TestTrue(TEXT("Plasma shocked"), Plasma->StatusEffect == EStatusEffect::Shocked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
