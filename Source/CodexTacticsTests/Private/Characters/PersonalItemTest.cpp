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
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
