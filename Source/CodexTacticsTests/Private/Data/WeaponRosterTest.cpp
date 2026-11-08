// CodexTactics.Weapons.Roster.* - the game designer's roster (2026-11-08: "The only weapons in the game are the ones I name"):
// the hammer, flamethrower, knife, cryo emitter and plasma carbine are out of play everywhere (arsenal, switching, loot, ammo items, saves).

#include "Characters/OperativeCharacter.h"
#include "Characters/TransferRules.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponRoster.h"
#include "Data/WeaponTuning.h"
#include "Dom/JsonObject.h"
#include "Interactables/CorpseLootRules.h"
#include "Interactables/LootRules.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace WeaponRosterTest
{
	static const TCHAR* const OutIds[] = { TEXT("hammer"), TEXT("flamethrower"), TEXT("knife"), TEXT("cryo_emitter"), TEXT("plasma_carbine") };
	static const TCHAR* const KeptIds[] = { TEXT("m16"), TEXT("grenade"), TEXT("shotgun"), TEXT("pistol"), TEXT("sniper_rifle") };

	UWeaponDataAsset* LoadRosterWeapon(const TCHAR* Id)
	{
		return LoadObject<UWeaponDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Data/Weapons/DA_Weapon_%s.DA_Weapon_%s"), Id, Id));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponRosterFileTest, "CodexTactics.Weapons.Roster.FileRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRosterFileTest::RunTest(const FString&)
{
	using namespace WeaponRosterTest;
	WeaponTuning::ApplyFile(WeaponTuning::GetDefaultPath());
	const FWeaponRoster& Roster = WeaponRoster::Get();
	TestTrue(TEXT("in play: the M16 and the grenade"), Roster.InPlay == TArray<FString>({ TEXT("m16"), TEXT("grenade") }));
	TestTrue(TEXT("next: shotgun, pistol, sniper rifle, RPG-7 (in this order)"),
		Roster.Next == TArray<FString>({ TEXT("shotgun"), TEXT("pistol"), TEXT("sniper_rifle"), TEXT("rpg7") }));
	TestEqual(TEXT("five out"), Roster.Out.Num(), 5);
	for (const TCHAR* Id : OutIds)
	{
		TestTrue(FString::Printf(TEXT("%s out of play"), Id), WeaponRoster::IsOutOfPlay(Id));
	}
	for (const TCHAR* Id : KeptIds)
	{
		TestFalse(FString::Printf(TEXT("%s stays"), Id), WeaponRoster::IsOutOfPlay(Id));
	}
	TestFalse(TEXT("an empty id is not out"), WeaponRoster::IsOutOfPlay(FString()));
	TestEqual(TEXT("the fallback in hands: the M16"), WeaponRoster::FallbackWeaponId(), FString(TEXT("m16")));

	// Bad roster data is reported; a weapon both in play and out is an error.
	TSharedPtr<FJsonObject> Bad;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT("{\"in_play\": [\"m16\", 3], \"out\": [\"m16\"]}")), Bad);
	FWeaponRoster Parsed;
	TArray<FString> Errors;
	TestFalse(TEXT("bad roster rejected"), Bad.IsValid() && WeaponRoster::Parse(*Bad, Parsed, &Errors));
	TestEqual(TEXT("two problems named"), Errors.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponRosterArsenalTest, "CodexTactics.Weapons.Roster.ArsenalAndSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRosterArsenalTest::RunTest(const FString&)
{
	using namespace WeaponRosterTest;
	TArray<UWeaponDataAsset*> Everything;
	for (const TCHAR* Id : KeptIds)
	{
		Everything.Add(LoadRosterWeapon(Id));
	}
	for (const TCHAR* Id : OutIds)
	{
		Everything.Add(LoadRosterWeapon(Id)); // the assets stay on disk
	}
	if (!TestFalse(TEXT("all ten assets load"), Everything.Contains(nullptr)))
	{
		return false;
	}
	// An old loadout listing every weapon: only the roster's survive, the M16 in hands.
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	Operative->GrenadesCount = 2;
	Operative->InitArsenal(Everything, 60);
	TestEqual(TEXT("five weapons kept"), Operative->AvailableWeapons.Num(), 5);
	for (const UWeaponDataAsset* Weapon : Operative->AvailableWeapons)
	{
		TestFalse(FString::Printf(TEXT("%s is not out of play"), *Weapon->WeaponId), WeaponRoster::IsOutOfPlay(Weapon->WeaponId));
	}
	TestTrue(TEXT("the M16 in hands"), Operative->CurrentWeapon && Operative->CurrentWeapon->WeaponId == TEXT("m16"));
	for (const TCHAR* Id : OutIds)
	{
		TestFalse(FString::Printf(TEXT("no switch to %s"), Id), Operative->SwitchToWeaponById(Id));
	}

	// An old save restored onto him: the knife in hands, its ammo state, flamethrower fuel, a plasma carbine bonus item.
	UWeaponDataAsset* Knife = LoadRosterWeapon(TEXT("knife"));
	Operative->CurrentWeapon = Knife;
	Operative->AmmoInventory.Add(TEXT("knife"), { 0, 0 });
	Operative->AmmoInventory.Add(TEXT("cryo_emitter"), { 30, 60 });
	Operative->ExtraAmmo.Add(TEXT("flamethrower"), 50);
	Operative->ExtraAmmo.Add(TEXT("shotgun"), 8);
	Operative->BonusItems.Add(TEXT("plasma_carbine"));
	Operative->BonusItems.Add(TEXT("warm_parka"));
	TestEqual(TEXT("fuel is no transfer item"), TransferRules::GetAvailable(*Operative, ETransferItem::FlameFuel), 0);
	Operative->DropOutOfPlayWeapons();
	TestTrue(TEXT("the save loads with the M16 in hands"), Operative->CurrentWeapon && Operative->CurrentWeapon->WeaponId == TEXT("m16"));
	TestFalse(TEXT("knife ammo state gone"), Operative->AmmoInventory.Contains(TEXT("knife")));
	TestFalse(TEXT("cryo ammo state gone"), Operative->AmmoInventory.Contains(TEXT("cryo_emitter")));
	TestFalse(TEXT("fuel gone"), Operative->ExtraAmmo.Contains(TEXT("flamethrower")));
	TestEqual(TEXT("shotgun shells kept"), Operative->ExtraAmmo.FindRef(TEXT("shotgun")), 8);
	TestTrue(TEXT("plasma bonus gone, the parka kept"), !Operative->BonusItems.Contains(TEXT("plasma_carbine")) && Operative->BonusItems.Contains(TEXT("warm_parka")));
	Operative->AddArsenalWeapon(LoadRosterWeapon(TEXT("hammer")));
	TestEqual(TEXT("AddArsenalWeapon refuses the hammer"), Operative->AvailableWeapons.Num(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponRosterLootTest, "CodexTactics.Weapons.Roster.Loot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeaponRosterLootTest::RunTest(const FString&)
{
	// A crate placed with the Godot defaults (fuel 50, coolant 30, plasma cells 20) and a plasma carbine as its bonus: none of it is loot.
	FLootContents Crate;
	Crate.BonusWeaponId = TEXT("plasma_carbine");
	TestEqual(TEXT("no fuel"), Crate.GetCount(ELootItem::FlameFuel), 0);
	TestEqual(TEXT("no coolant"), Crate.GetCount(ELootItem::CryoAmmo), 0);
	TestEqual(TEXT("no plasma cells"), Crate.GetCount(ELootItem::PlasmaAmmo), 0);
	TestEqual(TEXT("no plasma carbine"), Crate.GetCount(ELootItem::BonusWeapon), 0);
	TestEqual(TEXT("taking it gives nothing"), Crate.Take(ELootItem::FlameFuel), 0);
	bool bListed = false;
	for (const FLootEntry& Entry : Crate.GetItems())
	{
		bListed |= Entry.Item == ELootItem::FlameFuel || Entry.Item == ELootItem::CryoAmmo || Entry.Item == ELootItem::PlasmaAmmo
			|| Entry.Item == ELootItem::BonusWeapon;
	}
	TestFalse(TEXT("not in the loot dialog"), bListed);
	Crate.BonusWeaponId = TEXT("shotgun");
	TestEqual(TEXT("a roster weapon still is loot"), Crate.GetCount(ELootItem::BonusWeapon), 1);
	TestEqual(TEXT("rifle rounds untouched"), Crate.GetCount(ELootItem::RifleAmmo), 60);
	// A fallen mate's remains never offer an out-of-play weapon.
	TestFalse(TEXT("plasma carbine not lootable"), CorpseLootRules::IsLootableWeapon(TEXT("plasma_carbine")));
	TestTrue(TEXT("the M16 lootable"), CorpseLootRules::IsLootableWeapon(TEXT("m16")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
