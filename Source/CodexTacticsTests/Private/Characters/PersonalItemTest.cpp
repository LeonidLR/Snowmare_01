#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PersonalItemRules.h"
#include "Combat/HealthComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot player.gd heal_with_item / use_personal_item parity.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonalItemTest, "CodexTactics.Characters.PersonalItems.UseAndEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPersonalItemTest::RunTest(const FString&)
{
	TestEqual(TEXT("Medkit 80 HP"), PersonalItemRules::GetEffect(EPersonalItem::Medkit).Heal, 80.f);
	TestEqual(TEXT("Canned food 45 HP"), PersonalItemRules::GetEffect(EPersonalItem::CannedFood).Heal, 45.f);
	TestEqual(TEXT("Canned food 25 warmth"), PersonalItemRules::GetEffect(EPersonalItem::CannedFood).Warmth, 25.f);
	TestEqual(TEXT("Bread 30 / 15"), PersonalItemRules::GetEffect(EPersonalItem::Bread).Warmth, 15.f);
	TestEqual(TEXT("Chocolate 20 / 10"), PersonalItemRules::GetEffect(EPersonalItem::Chocolate).Heal, 20.f);
	TestFalse(TEXT("Full health, no cold: refused"), PersonalItemRules::CanUse(100.f, 100.f, 0.f));
	TestTrue(TEXT("Cold only: allowed"), PersonalItemRules::CanUse(100.f, 100.f, 5.f));

	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	UHealthComponent* Health = Operative->HealthComponent;
	if (!TestNotNull(TEXT("health component"), Health))
	{
		return false;
	}
	Health->SetMaxHealth(140.f, true);
	Operative->MedkitsCount = 1;
	Operative->CannedFoodCount = 1;
	TestFalse(TEXT("nothing to heal"), Operative->UsePersonalItem(EPersonalItem::Medkit));
	TestEqual(TEXT("medkit kept"), Operative->MedkitsCount, 1);

	Health->ApplyDirectHealthLoss(100.f, TEXT("Test"));
	TestTrue(TEXT("medkit used"), Operative->UsePersonalItem(EPersonalItem::Medkit));
	TestEqual(TEXT("+80 HP"), Health->GetCurrentHealth(), 120.f);
	TestEqual(TEXT("medkit spent"), Operative->MedkitsCount, 0);
	TestFalse(TEXT("no medkit left"), Operative->UsePersonalItem(EPersonalItem::Medkit));

	Operative->ColdLevel = 40.f;
	TestTrue(TEXT("canned food used"), Operative->UsePersonalItem(EPersonalItem::CannedFood));
	TestEqual(TEXT("health capped"), Health->GetCurrentHealth(), 140.f);
	TestEqual(TEXT("cold -25"), Operative->ColdLevel, 15.f);

	// 125B: food must not heal HP or clear/reduce Heavy or Critical wound states.
	TestTrue(TEXT("CannedFood is food"), PersonalItemRules::IsFood(EPersonalItem::CannedFood));
	TestTrue(TEXT("Chocolate is food"), PersonalItemRules::IsFood(EPersonalItem::Chocolate));
	TestFalse(TEXT("Medkit is not food"), PersonalItemRules::IsFood(EPersonalItem::Medkit));
	TestEqual(TEXT("Heavy wound: food heal=0"), PersonalItemRules::GetEffectiveEffect(EPersonalItem::CannedFood, CriticalWoundRules::EWoundTier::Heavy).Heal, 0.f);
	TestEqual(TEXT("Critical wound: food heal=0"), PersonalItemRules::GetEffectiveEffect(EPersonalItem::Chocolate, CriticalWoundRules::EWoundTier::Critical).Heal, 0.f);
	TestEqual(TEXT("Critical wound: food keeps warmth"), PersonalItemRules::GetEffectiveEffect(EPersonalItem::Chocolate, CriticalWoundRules::EWoundTier::Critical).Warmth, 10.f);
	TestFalse(TEXT("Critical wound, no cold: food refused"), PersonalItemRules::CanUse(15.f, 100.f, 0.f, EPersonalItem::CannedFood, CriticalWoundRules::EWoundTier::Critical));
	TestTrue(TEXT("Critical wound, with cold: food allowed for warmth"), PersonalItemRules::CanUse(15.f, 100.f, 20.f, EPersonalItem::CannedFood, CriticalWoundRules::EWoundTier::Critical));

	Health->SetMaxHealth(100.f, true);
	Health->ApplyDirectHealthLoss(85.f, TEXT("CriticalTest")); // 15 HP (< 20% = Critical)
	Operative->ColdLevel = 0.f;
	Operative->CannedFoodCount = 1;
	Operative->ChocolateCount = 1;
	TestEqual(TEXT("operative is Critical"), Operative->GetWoundTier(), CriticalWoundRules::EWoundTier::Critical);
	TestFalse(TEXT("Critical, warm: chocolate refused"), Operative->UsePersonalItem(EPersonalItem::Chocolate));
	TestEqual(TEXT("chocolate kept"), Operative->ChocolateCount, 1);
	TestEqual(TEXT("HP unchanged"), Health->GetCurrentHealth(), 15.f);

	Operative->ColdLevel = 40.f;
	TestTrue(TEXT("Critical, cold: canned food gives warmth only"), Operative->UsePersonalItem(EPersonalItem::CannedFood));
	TestEqual(TEXT("canned food spent"), Operative->CannedFoodCount, 0);
	TestEqual(TEXT("Critical HP still 15"), Health->GetCurrentHealth(), 15.f);
	TestEqual(TEXT("cold reduced to 15"), Operative->ColdLevel, 15.f);
	TestEqual(TEXT("still Critical"), Operative->GetWoundTier(), CriticalWoundRules::EWoundTier::Critical);

	// Heavy wound (25 HP >= 20% critical_from, < 35% heavy_from): food refused when warm.
	Health->RestoreHealth(25.f, 100.f);
	Operative->ColdLevel = 0.f;
	TestEqual(TEXT("operative is Heavy"), Operative->GetWoundTier(), CriticalWoundRules::EWoundTier::Heavy);
	TestFalse(TEXT("Heavy, warm: chocolate refused"), Operative->UsePersonalItem(EPersonalItem::Chocolate));
	TestEqual(TEXT("Heavy HP still 25"), Health->GetCurrentHealth(), 25.f);

	// Light wound (40 HP >= 35% heavy_from, < 50% wounded_from): food keeps small HP heal.
	Health->RestoreHealth(40.f, 100.f);
	Operative->ColdLevel = 0.f;
	TestEqual(TEXT("operative is Light"), Operative->GetWoundTier(), CriticalWoundRules::EWoundTier::Light);
	TestTrue(TEXT("Light wound: chocolate heals"), Operative->UsePersonalItem(EPersonalItem::Chocolate));
	TestEqual(TEXT("Light wound HP +20"), Health->GetCurrentHealth(), 60.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
