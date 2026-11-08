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
	// The plasma carbine check was retired with the designer's roster (2026-11-08): the weapon is out of play, its asset unreferenced.
	if (!TestNotNull(TEXT("M16 asset"), M16) || !TestNotNull(TEXT("Pistol asset"), Pistol))
	{
		return false;
	}
	// rifle_m16.tres
	TestEqual(TEXT("M16 name"), M16->WeaponName.ToString(), FString(TEXT("M16A2 Assault Rifle")));
	TestEqual(TEXT("M16 damage"), M16->Stats.Damage.BaseDamage, 18.f);
	// Range 14 -> 25 m, grid 5 -> 8 cells, hit table over 25 m (user decision 2026-10-11, weapons_tuning.json written onto
	// the asset by Scripts/Editor/apply_weapon_tuning.py).
	// Since c8af1c6d (user 2026-11-04, ranged fights): 60 m.
	TestEqual(TEXT("M16 range 60 m"), M16->Stats.Range.MaxRangeCm, 6000.f);
	TestEqual(TEXT("M16 grid range 8 cells"), M16->Stats.TurnBased.MaxRangeCells, 8);
	TestEqual(TEXT("M16 clip"), M16->Stats.Ammo.MagazineSize, 30);
	TestEqual(TEXT("M16 reload"), M16->Stats.Ammo.ReloadSeconds, 2.68f);
	TestEqual(TEXT("M16 hit curve"), M16->Stats.Accuracy.HitChanceByCell.Num(), 17);
	TestEqual(TEXT("M16 3 cells"), M16->GetHitChanceForDistance(3), 0.75f);
	TestTrue(TEXT("M16 8 rays"), M16->Stats.TurnBased.AttackShape == EAttackShape::Rays8);
	// pistol_beretta.tres: 4 rays, infinite reserve (-1)
	TestTrue(TEXT("Pistol 4 rays"), Pistol->Stats.TurnBased.AttackShape == EAttackShape::Rays4);
	TestEqual(TEXT("Pistol reserve -1"), Pistol->Stats.Ammo.ReserveAmmo, -1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
