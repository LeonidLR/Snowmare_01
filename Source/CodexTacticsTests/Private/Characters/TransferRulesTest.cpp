#include "Misc/AutomationTest.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/TransferRules.h"
#include "Data/WeaponDataAsset.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/ItemStashComponent.h"

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

// --- Sprint 13: drag & drop hand-over (quantity split dialog, 2 m range, partial stacks) ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferQuantityStepAmmoTest, "CodexTactics.Transfer.QuantityStep.AmmoQuantizedByFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferQuantityStepAmmoTest::RunTest(const FString&)
{
	for (const ETransferItem Ammo : { ETransferItem::RifleAmmo, ETransferItem::PistolAmmo, ETransferItem::ShotgunAmmo, ETransferItem::FlameFuel,
		ETransferItem::CryoAmmo, ETransferItem::PlasmaAmmo })
	{
		TestEqual(TEXT("every ammo type steps by 5"), TransferRules::GetItemQuantityStep(Ammo), 5);
	}
	const ETransferItem M16 = ETransferItem::RifleAmmo;
	TestEqual(TEXT("min 5"), TransferRules::GetMinQuantity(M16, 30), 5);
	TestEqual(TEXT("17 snaps down to 15"), TransferRules::QuantizeQuantity(M16, 17, 30), 15);
	TestEqual(TEXT("below the minimum -> 5"), TransferRules::QuantizeQuantity(M16, 2, 30), 5);
	TestEqual(TEXT("above the stack -> the stack"), TransferRules::QuantizeQuantity(M16, 99, 30), 30);
	TestEqual(TEXT("[+] from 15"), TransferRules::StepQuantity(M16, 15, 1, 30), 20);
	TestEqual(TEXT("[-] from 15"), TransferRules::StepQuantity(M16, 15, -1, 30), 10);
	TestEqual(TEXT("[-] at the minimum stays"), TransferRules::StepQuantity(M16, 5, -1, 30), 5);
	// An off-grid stack (13): the grid is 5, 10 and the whole stack.
	TestEqual(TEXT("[+] from 10 reaches the whole 13"), TransferRules::StepQuantity(M16, 10, 1, 13), 13);
	TestEqual(TEXT("[-] from 13 lands on 10"), TransferRules::StepQuantity(M16, 13, -1, 13), 10);
	TestEqual(TEXT("12 of 13 snaps to 10"), TransferRules::QuantizeQuantity(M16, 12, 13), 10);
	TestTrue(TEXT("30 rounds: a choice -> dialog"), TransferRules::NeedsQuantityDialog(M16, 30));
	TestTrue(TEXT("7 rounds: 5 or 7 -> dialog"), TransferRules::NeedsQuantityDialog(M16, 7));
	TestFalse(TEXT("5 rounds: one choice -> instant"), TransferRules::NeedsQuantityDialog(M16, 5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferAmmoRemainderTest, "CodexTactics.Transfer.QuantityStep.AmmoRemainderBelowFive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferAmmoRemainderTest::RunTest(const FString&)
{
	const ETransferItem M16 = ETransferItem::RifleAmmo;
	TestEqual(TEXT("3 left: the minimum is the remainder"), TransferRules::GetMinQuantity(M16, 3), 3);
	TestEqual(TEXT("3 left: any request -> 3"), TransferRules::QuantizeQuantity(M16, 1, 3), 3);
	TestEqual(TEXT("[-] with 3 left stays 3"), TransferRules::StepQuantity(M16, 3, -1, 3), 3);
	TestFalse(TEXT("3 left: no dialog"), TransferRules::NeedsQuantityDialog(M16, 3));
	TestEqual(TEXT("nothing left"), TransferRules::QuantizeQuantity(M16, 5, 0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferQuantityStepMedkitTest, "CodexTactics.Transfer.QuantityStep.MedkitsQuantizedByOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferQuantityStepMedkitTest::RunTest(const FString&)
{
	for (const ETransferItem Item : { ETransferItem::Medkit, ETransferItem::Mine, ETransferItem::Barricade, ETransferItem::Turret,
		ETransferItem::CannedFood, ETransferItem::Bread, ETransferItem::Chocolate, ETransferItem::Matches })
	{
		TestEqual(TEXT("countable items step by 1"), TransferRules::GetItemQuantityStep(Item), 1);
		TestFalse(TEXT("countable items are not ammo"), TransferRules::IsAmmoItem(Item));
	}
	const ETransferItem Medkit = ETransferItem::Medkit;
	TestEqual(TEXT("min 1"), TransferRules::GetMinQuantity(Medkit, 3), 1);
	TestEqual(TEXT("2 of 3 stays 2"), TransferRules::QuantizeQuantity(Medkit, 2, 3), 2);
	TestEqual(TEXT("[+] 2 -> 3"), TransferRules::StepQuantity(Medkit, 2, 1, 3), 3);
	TestEqual(TEXT("[+] at the max stays"), TransferRules::StepQuantity(Medkit, 3, 1, 3), 3);
	TestEqual(TEXT("[-] 2 -> 1"), TransferRules::StepQuantity(Medkit, 2, -1, 3), 1);
	TestFalse(TEXT("a single medkit: no dialog"), TransferRules::NeedsQuantityDialog(Medkit, 1));
	TestTrue(TEXT("two medkits: dialog"), TransferRules::NeedsQuantityDialog(Medkit, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferDistanceTest, "CodexTactics.Transfer.DistanceCheck.RejectsBeyondTwoMeters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferDistanceTest::RunTest(const FString&)
{
	TestTrue(TEXT("1.5 m"), TransferRules::IsWithinTransferRange(FVector::ZeroVector, FVector(150.f, 0.f, 0.f)));
	TestTrue(TEXT("exactly 2 m"), TransferRules::IsWithinTransferRange(FVector::ZeroVector, FVector(0.f, 200.f, 0.f)));
	TestFalse(TEXT("2.01 m"), TransferRules::IsWithinTransferRange(FVector::ZeroVector, FVector(201.f, 0.f, 0.f)));
	TestTrue(TEXT("height (stance) does not count"), TransferRules::IsWithinTransferRange(FVector::ZeroVector, FVector(190.f, 0.f, 60.f)));

	AOperativeCharacter* Sender = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Recipient = NewObject<AOperativeCharacter>();
	Sender->SetActorLocation(FVector::ZeroVector);
	Recipient->SetActorLocation(FVector(180.f, 0.f, 0.f));
	TestTrue(TEXT("operatives 1.8 m apart"), TransferRules::CanTransferTo(*Sender, *Recipient));
	Recipient->SetActorLocation(FVector(250.f, 0.f, 0.f));
	TestFalse(TEXT("operatives 2.5 m apart"), TransferRules::CanTransferTo(*Sender, *Recipient));
	TestFalse(TEXT("not to himself"), TransferRules::CanTransferTo(*Sender, *Sender));

	FTransferRangeContext Context;
	Context.Distance = 150.f;
	Context.bInCombat = true;
	TestTrue(TEXT("in range even in combat"), TransferRules::DecideRange(Context) == ETransferRangeDecision::InRange);
	Context.Distance = 600.f;
	TestTrue(TEXT("far in a wave fight: blocked"), TransferRules::DecideRange(Context) == ETransferRangeDecision::Blocked);
	Context.bInCombat = false;
	TestTrue(TEXT("far out of combat: approach"), TransferRules::DecideRange(Context) == ETransferRangeDecision::Approach);
	Context.bTurnBased = true;
	TestTrue(TEXT("far in turn-based: blocked"), TransferRules::DecideRange(Context) == ETransferRangeDecision::Blocked);
	Context.bTurnBased = false;
	Context.bUnderFire = true;
	TestTrue(TEXT("far under fire: blocked"), TransferRules::DecideRange(Context) == ETransferRangeDecision::Blocked);
	Context.bUnderFire = false;
	Context.bCanMove = false;
	TestTrue(TEXT("far and immobile: blocked"), TransferRules::DecideRange(Context) == ETransferRangeDecision::Blocked);
	TestTrue(TEXT("the walk stops inside the hand-over range"), TransferRules::ApproachStopDistance < TransferRules::MaxTransferDistance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferPartialStackTest, "CodexTactics.Transfer.PartialStackTransferDeductsCorrectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferPartialStackTest::RunTest(const FString&)
{
	UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16"));
	UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol"));
	if (!TestTrue(TEXT("weapon assets"), M16 && Pistol))
	{
		return false;
	}
	AOperativeCharacter* Sender = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Recipient = NewObject<AOperativeCharacter>();
	Sender->InitArsenal({ M16, Pistol }, 30);
	Recipient->InitArsenal({ M16, Pistol }, 60);

	FTransferResult Result = TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::RifleAmmo, 15);
	TestTrue(TEXT("15 of 30 rounds"), Result.bDone && Result.Moved == 15 && Sender->GetReserve(TEXT("m16")) == 15
		&& Recipient->GetReserve(TEXT("m16")) == 75);
	TestEqual(TEXT("feedback"), Result.Feedback, FString(TEXT("+15 Патроны M16")));
	Result = TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::RifleAmmo, 40);
	TestTrue(TEXT("asking for more than there is: the rest"), Result.Moved == 15 && Sender->GetReserve(TEXT("m16")) == 0 && !Result.bClampedByCapacity);
	TestFalse(TEXT("empty: nothing"), TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::RifleAmmo, 5).bDone);

	Sender->MedkitsCount = 3;
	Recipient->MedkitsCount = 0;
	Result = TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::Medkit, 2);
	TestTrue(TEXT("2 of 3 medkits"), Result.Moved == 2 && Sender->MedkitsCount == 1 && Recipient->MedkitsCount == 2);
	TestEqual(TEXT("unlimited room for medkits"), TransferRules::GetRecipientCapacity(*Recipient, ETransferItem::Medkit), MAX_int32);
	TestEqual(TEXT("max = sender stock"), TransferRules::GetMaxTransferQuantity(*Sender, *Recipient, ETransferItem::Medkit), 1);
	TestFalse(TEXT("zero asked: nothing"), TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::Medkit, 0).bDone);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferCapacityClampTest, "CodexTactics.Transfer.CapacityClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferCapacityClampTest::RunTest(const FString&)
{
	AOperativeCharacter* Sender = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Recipient = NewObject<AOperativeCharacter>();
	const int32 MaxMines = DeployableRules::GetMaxCarried(EDeployableType::Mine);
	if (!TestTrue(TEXT("mines can be carried"), MaxMines >= 2))
	{
		return false;
	}
	Sender->AddDeployable(EDeployableType::Mine, MaxMines - Sender->GetDeployableCount(EDeployableType::Mine));
	Recipient->AddDeployable(EDeployableType::Mine, MaxMines - 1 - Recipient->GetDeployableCount(EDeployableType::Mine));
	TestEqual(TEXT("room for one mine"), TransferRules::GetRecipientCapacity(*Recipient, ETransferItem::Mine), 1);
	TestEqual(TEXT("max = capacity"), TransferRules::GetMaxTransferQuantity(*Sender, *Recipient, ETransferItem::Mine), 1);
	FTransferResult Result = TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::Mine, 2);
	TestTrue(TEXT("partial: one moved, clamped"), Result.bDone && Result.Moved == 1 && Result.bClampedByCapacity
		&& Sender->GetDeployableCount(EDeployableType::Mine) == MaxMines - 1 && Recipient->GetDeployableCount(EDeployableType::Mine) == MaxMines);
	Result = TransferRules::TransferQuantity(*Sender, *Recipient, ETransferItem::Mine, 1);
	TestTrue(TEXT("full recipient: refused, nothing deducted"), Result.bRecipientFull && !Result.bDone
		&& Sender->GetDeployableCount(EDeployableType::Mine) == MaxMines - 1);
	return true;
}

// --- Sprint 13: ground piles and two-way crates share UItemStashComponent ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferStashCapacityTest, "CodexTactics.Transfer.Stash.CapacityAndTake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferStashCapacityTest::RunTest(const FString&)
{
	UItemStashComponent* Stash = NewObject<UItemStashComponent>();
	TestTrue(TEXT("new stash empty, unlimited"), Stash->IsEmpty() && Stash->GetFreeSpace() == MAX_int32);
	Stash->Capacity = 20;
	TestEqual(TEXT("15 rounds go in"), Stash->Add(ETransferItem::RifleAmmo, 15), 15);
	TestEqual(TEXT("only 5 more fit"), Stash->Add(ETransferItem::Medkit, 8), 5);
	TestEqual(TEXT("full"), Stash->GetFreeSpace(), 0);
	TestEqual(TEXT("authored loot ignores the capacity"), Stash->Add(ETransferItem::Bread, 2, true), 2);
	TestEqual(TEXT("take 10 of 15"), Stash->Take(ETransferItem::RifleAmmo, 10), 10);
	TestEqual(TEXT("5 rounds left"), Stash->GetCount(ETransferItem::RifleAmmo), 5);
	TestEqual(TEXT("taking more than there is: the rest"), Stash->Take(ETransferItem::RifleAmmo, 99), 5);
	TestFalse(TEXT("an emptied line disappears"), Stash->GetItemTypes().Contains(ETransferItem::RifleAmmo));
	const int32 Revision = Stash->GetRevision();
	Stash->Clear();
	TestTrue(TEXT("cleared, revision bumped"), Stash->IsEmpty() && Stash->GetRevision() > Revision);
	for (uint8 Index = 0; Index <= static_cast<uint8>(ETransferItem::PlasmaAmmo); ++Index)
	{
		const ETransferItem Item = static_cast<ETransferItem>(Index);
		ETransferItem Back = ETransferItem::Medkit;
		TestTrue(TEXT("every item maps to a loot kind and back"), ItemStash::FromLootItem(ItemStash::ToLootItem(Item), Back) && Back == Item);
	}
	ETransferItem Unused = ETransferItem::Medkit;
	TestFalse(TEXT("the bonus weapon is not storable"), ItemStash::FromLootItem(ELootItem::BonusWeapon, Unused));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferStashStoreTakeTest, "CodexTactics.Transfer.Stash.StoreAndTakePartial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferStashStoreTakeTest::RunTest(const FString&)
{
	UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16"));
	UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol"));
	if (!TestTrue(TEXT("weapon assets"), M16 && Pistol))
	{
		return false;
	}
	AOperativeCharacter* Sender = NewObject<AOperativeCharacter>();
	AOperativeCharacter* Taker = NewObject<AOperativeCharacter>();
	Sender->InitArsenal({ M16, Pistol }, 30);
	Taker->InitArsenal({ M16, Pistol }, 60);
	UItemStashComponent* Crate = NewObject<UItemStashComponent>();
	Crate->Capacity = 20;

	FTransferResult Result = TransferRules::StoreInStash(*Sender, *Crate, ETransferItem::RifleAmmo, 10);
	TestTrue(TEXT("store 10 rounds: sender -10, crate +10"), Result.Moved == 10 && Sender->GetReserve(TEXT("m16")) == 20
		&& Crate->GetCount(ETransferItem::RifleAmmo) == 10);
	Result = TransferRules::TakeFromStash(*Crate, *Taker, ETransferItem::RifleAmmo, 5);
	TestTrue(TEXT("take 5: taker +5, crate 5"), Result.Moved == 5 && Taker->GetReserve(TEXT("m16")) == 65 && Crate->GetCount(ETransferItem::RifleAmmo) == 5);
	Result = TransferRules::StoreInStash(*Sender, *Crate, ETransferItem::RifleAmmo, 20);
	TestTrue(TEXT("crate nearly full: partial (15 free of 20 asked)"), Result.Moved == 15 && Result.bClampedByCapacity && Sender->GetReserve(TEXT("m16")) == 5);
	Sender->MedkitsCount = 2;
	Result = TransferRules::StoreInStash(*Sender, *Crate, ETransferItem::Medkit, 1);
	TestTrue(TEXT("crate full: refused, nothing deducted"), Result.bRecipientFull && !Result.bDone && Sender->MedkitsCount == 2);

	const int32 MaxTurrets = DeployableRules::GetMaxCarried(EDeployableType::Turret);
	Taker->AddDeployable(EDeployableType::Turret, MaxTurrets - 1 - Taker->GetDeployableCount(EDeployableType::Turret));
	UItemStashComponent* Pile = NewObject<UItemStashComponent>();
	Pile->Add(ETransferItem::Turret, 2);
	Result = TransferRules::TakeFromStash(*Pile, *Taker, ETransferItem::Turret, 2);
	TestTrue(TEXT("pick-up into a nearly full engineer: 1 taken, 1 stays"), Result.Moved == 1 && Result.bClampedByCapacity
		&& Pile->GetCount(ETransferItem::Turret) == 1 && Taker->GetDeployableCount(EDeployableType::Turret) == MaxTurrets);
	Result = TransferRules::TakeFromStash(*Pile, *Taker, ETransferItem::Turret, 1);
	TestTrue(TEXT("full engineer: refused, the turret stays"), Result.bRecipientFull && Pile->GetCount(ETransferItem::Turret) == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferDropRemoveTest, "CodexTactics.Transfer.DropToGround.RemovesExactQuantity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTransferDropRemoveTest::RunTest(const FString&)
{
	UWeaponDataAsset* M16 = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_m16.DA_Weapon_m16"));
	UWeaponDataAsset* Pistol = LoadObject<UWeaponDataAsset>(nullptr, TEXT("/Game/Data/Weapons/DA_Weapon_pistol.DA_Weapon_pistol"));
	if (!TestTrue(TEXT("weapon assets"), M16 && Pistol))
	{
		return false;
	}
	AOperativeCharacter* Operative = NewObject<AOperativeCharacter>();
	Operative->InitArsenal({ M16, Pistol }, 30);
	TestEqual(TEXT("15 rounds out"), TransferRules::RemoveFromOperative(*Operative, ETransferItem::RifleAmmo, 15), 15);
	TestEqual(TEXT("15 left"), Operative->GetReserve(TEXT("m16")), 15);
	Operative->MedkitsCount = 1;
	TestEqual(TEXT("asking for 3 medkits gives the 1 there is"), TransferRules::RemoveFromOperative(*Operative, ETransferItem::Medkit, 3), 1);
	TestEqual(TEXT("none left"), Operative->MedkitsCount, 0);
	Operative->AddDeployable(EDeployableType::Mine, 2 - Operative->GetDeployableCount(EDeployableType::Mine));
	TestEqual(TEXT("mines leave as items"), TransferRules::RemoveFromOperative(*Operative, ETransferItem::Mine, 1), 1);
	TestEqual(TEXT("one mine left"), Operative->GetDeployableCount(EDeployableType::Mine), 1);
	TransferRules::AddToOperative(*Operative, ETransferItem::RifleAmmo, 5);
	TestEqual(TEXT("picked up rounds go to the reserve"), Operative->GetReserve(TEXT("m16")), 20);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
