#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Data/WeaponDataAsset.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot player.gd _init_weapons / switch_to_weapon_by_id / ammo_inventory parity with the imported weapon assets.

namespace ArsenalTest
{
	UWeaponDataAsset* Load(const TCHAR* Id)
	{
		return LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), Id, Id));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArsenalSwitchTest, "CodexTactics.Characters.Arsenal.InitAndSwitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArsenalSwitchTest::RunTest(const FString&)
{
	using namespace ArsenalTest;
	UWeaponDataAsset* M16 = Load(TEXT("m16"));
	UWeaponDataAsset* Pistol = Load(TEXT("pistol"));
	UWeaponDataAsset* Grenade = Load(TEXT("grenade"));
	UWeaponDataAsset* Knife = Load(TEXT("knife"));
	if (!TestTrue(TEXT("weapon assets"), M16 && Pistol && Grenade && Knife))
	{
		return false;
	}
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	Operative->GrenadesCount = 2;
	Operative->ExtraAmmo.Add(TEXT("pistol"), 6); // picked up earlier
	Operative->InitArsenal({ M16, Pistol, Grenade, Knife }, 60);

	TestEqual(TEXT("4 weapons"), Operative->AvailableWeapons.Num(), 4);
	TestTrue(TEXT("M16 in hands"), Operative->CurrentWeapon == M16);
	TestEqual(TEXT("M16 30 / 60"), Operative->CurrentClip, M16->MaxClipSize);
	TestEqual(TEXT("M16 reserve 60"), Operative->ReserveAmmo, 60);
	TestEqual(TEXT("pistol reserve 24 + 6 looted"), Operative->GetAmmoState(TEXT("pistol")).Reserve, 30);
	TestFalse(TEXT("pistol ammo left ExtraAmmo"), Operative->ExtraAmmo.Contains(TEXT("pistol")));
	TestEqual(TEXT("grenade clip 1"), Operative->GetAmmoState(TEXT("grenade")).Clip, 1);
	TestEqual(TEXT("grenade reserve 1"), Operative->GetAmmoState(TEXT("grenade")).Reserve, 1);

	Operative->CurrentClip -= 5; // five rounds fired
	TestTrue(TEXT("switch to the pistol"), Operative->SwitchToWeaponById(TEXT("pistol")));
	TestEqual(TEXT("pistol full clip"), Operative->CurrentClip, Pistol->MaxClipSize);
	TestEqual(TEXT("pistol reserve"), Operative->ReserveAmmo, 30);
	TestEqual(TEXT("M16 state kept"), Operative->GetAmmoState(TEXT("m16")).Clip, M16->MaxClipSize - 5);
	TestTrue(TEXT("back to the M16"), Operative->SwitchToWeaponById(TEXT("m16")));
	TestEqual(TEXT("M16 clip restored"), Operative->CurrentClip, M16->MaxClipSize - 5);

	Operative->AddAmmo(TEXT("pistol"), 10);
	Operative->AddAmmo(TEXT("m16"), 30);
	Operative->AddAmmo(TEXT("shotgun"), 8);
	TestEqual(TEXT("loot to the stowed pistol"), Operative->GetAmmoState(TEXT("pistol")).Reserve, 40);
	TestEqual(TEXT("loot to the M16 in hands"), Operative->ReserveAmmo, 90);
	TestEqual(TEXT("shotgun outside the arsenal"), Operative->ExtraAmmo.FindRef(TEXT("shotgun")), 8);

	TestTrue(TEXT("knife"), Operative->SwitchToWeaponById(TEXT("knife")));
	TestFalse(TEXT("knife uses no ammo"), Operative->UsesAmmo());
	TestFalse(TEXT("unknown weapon refused"), Operative->SwitchToWeaponById(TEXT("shotgun")));
	TestTrue(TEXT("still the knife"), Operative->CurrentWeapon == Knife);

	Operative->GrenadesCount = 1; // one used on a trap
	TestTrue(TEXT("grenade"), Operative->SwitchToWeaponById(TEXT("grenade")));
	TestEqual(TEXT("grenade clip from the count"), Operative->CurrentClip, 1);
	TestEqual(TEXT("grenade reserve from the count"), Operative->ReserveAmmo, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
