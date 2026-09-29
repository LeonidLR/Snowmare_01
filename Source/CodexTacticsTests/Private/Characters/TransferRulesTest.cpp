#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/TransferRules.h"
#include "Data/WeaponDataAsset.h"
#include "Interactables/DeployableRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// Godot main.gd _transfer_item_to_target parity (plus the pistol pack Godot forgot).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferRulesTest, "CodexTactics.Characters.Transfer.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferRulesTest::RunTest(const FString&)
{
	UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16"));
	UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol"));
	if (!TestTrue(TEXT("weapon assets"), M16 && Pistol))
	{
		return false;
	}
	AOperativeCharacter* Sender = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Recipient = NewObject<AOperativeCharacter>();
	Sender->InitArsenal({ M16, Pistol }, 60);
	Recipient->InitArsenal({ M16, Pistol }, 60);

	Sender->MedkitsCount = 1;
	Recipient->MedkitsCount = 0;
	FTransferResult Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::Medkit);
	TestTrue(TEXT("medkit handed over"), Result.bDone && Sender->MedkitsCount == 0 && Recipient->MedkitsCount == 1);
	TestEqual(TEXT("feedback"), Result.Feedback, FString(TEXT("+1 Аптечка")));
	TestFalse(TEXT("nothing left"), TransferRules::Transfer(*Sender, *Recipient, ETransferItem::Medkit).bDone);

	Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::RifleAmmo);
	TestTrue(TEXT("M16 pack of 30"), Result.bDone && Sender->ReserveAmmo == 30 && Recipient->ReserveAmmo == 90);
	TestEqual(TEXT("M16 feedback"), Result.Feedback, FString(TEXT("+30 Патроны M16")));
	Sender->ReserveAmmo = 10;
	Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::RifleAmmo);
	TestTrue(TEXT("last 10 rounds"), Result.bDone && Sender->ReserveAmmo == 0 && Result.Feedback.StartsWith(TEXT("+10")));

	Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::PistolAmmo);
	TestTrue(TEXT("pistol pack of 12 from the stowed pistol"), Result.bDone && Sender->GetReserve(TEXT("pistol")) == 12
		&& Recipient->GetReserve(TEXT("pistol")) == 36);

	Sender->AddDeployable(EDeployableType::Turret, 1);
	Recipient->AddDeployable(EDeployableType::Turret, DeployableRules::GetMaxCarried(EDeployableType::Turret));
	Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::Turret);
	TestTrue(TEXT("recipient full of turrets"), Result.bRecipientFull && !Result.bDone && Sender->TurretsCount == 1);
	Recipient->AddDeployable(EDeployableType::Turret, -1);
	Result = TransferRules::Transfer(*Sender, *Recipient, ETransferItem::Turret);
	TestTrue(TEXT("turret handed over"), Result.bDone && Sender->TurretsCount == 0 && Result.Feedback == TEXT("+1 Турель"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
