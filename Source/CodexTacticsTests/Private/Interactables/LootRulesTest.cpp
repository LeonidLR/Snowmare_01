#include "Misc/AutomationTest.h"
#include "Interactables/LootRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Supply crate contents parity with Godot Scenes/movements/loot_crate.gd.

#define LOOT_TEST(TestClass, TestPath) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass, "CodexTactics.Loot." TestPath, \
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

LOOT_TEST(FLootDefaultsTest, "DefaultCrateItemsInGodotOrder")
bool FLootDefaultsTest::RunTest(const FString&)
{
	const FLootContents Crate;
	const TArray<FLootEntry> Items = Crate.GetItems();
	// Defaults: medkit 1, food 1, bread 1, chocolate 2, matches 2, rifle 60, pistol 24, shotgun 16, fuel 50, cryo 30, plasma 20.
	TestEqual(TEXT("Eleven stacks"), Items.Num(), 11);
	TestTrue(TEXT("Medkit first"), Items[0].Item == ELootItem::Medkit);
	TestEqual(TEXT("Rifle ammo 60"), Crate.GetCount(ELootItem::RifleAmmo), 60);
	TestEqual(TEXT("Label"), Items[0].GetLabel().ToString(), FString(TEXT("🩹 Medkit: x1")));
	TestEqual(TEXT("Fuel in units"), Items[8].GetLabel().ToString(), FString(TEXT("🔥 Flamethrower fuel: 50 units")));
	return true;
}

LOOT_TEST(FLootTakeTest, "TakeStacksUntilEmpty")
bool FLootTakeTest::RunTest(const FString&)
{
	FLootContents Crate;
	Crate.Turrets = 1;
	Crate.Barricades = 2;
	Crate.Mines = 2;
	Crate.BonusWeaponId = TEXT("shotgun_remington");
	TestEqual(TEXT("Take barricades"), Crate.Take(ELootItem::Barricade), 2);
	TestEqual(TEXT("Stack gone"), Crate.Take(ELootItem::Barricade), 0);
	TestEqual(TEXT("Bonus weapon counts as one"), Crate.Take(ELootItem::BonusWeapon), 1);
	TestFalse(TEXT("Not empty yet"), Crate.IsEmpty());
	for (const FLootEntry& Entry : Crate.GetItems())
	{
		if (Entry.Item != ELootItem::PistolAmmo)
		{
			Crate.Take(Entry.Item);
		}
	}
	// Godot is_empty_crate ignores pistol ammo.
	TestTrue(TEXT("Empty without pistol ammo"), Crate.IsEmpty());
	TestEqual(TEXT("Pistol ammo still listed"), Crate.GetItems().Num(), 1);
	return true;
}

LOOT_TEST(FLootDestroyTest, "DetonationBurnsEverything")
bool FLootDestroyTest::RunTest(const FString&)
{
	FLootContents Crate;
	Crate.Mines = 2;
	Crate.BonusClothingId = TEXT("warm_parka");
	Crate.DestroyAll();
	TestTrue(TEXT("Empty"), Crate.IsEmpty());
	TestEqual(TEXT("Nothing listed"), Crate.GetItems().Num(), 0);
	return true;
}

#undef LOOT_TEST

#endif // WITH_DEV_AUTOMATION_TESTS
