#include "Interactables/LootCrateActor.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/ItemStashComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "LootCrateActor"

namespace
{
	/** Godot loot_single_item result lines. */
	FText TakeLine(ELootItem Item, int32 Count, const FString& Id)
	{
		switch (Item)
		{
		case ELootItem::Medkit: return FText::Format(LOCTEXT("TookMedkit", "+{0} 🩹 Medkit"), Count);
		case ELootItem::CannedFood: return FText::Format(LOCTEXT("TookFood", "+{0} 🥫 Canned food"), Count);
		case ELootItem::Bread: return FText::Format(LOCTEXT("TookBread", "+{0} 🍞 Bread"), Count);
		case ELootItem::Chocolate: return FText::Format(LOCTEXT("TookChocolate", "+{0} 🍫 Chocolate"), Count);
		case ELootItem::Matches: return FText::Format(LOCTEXT("TookMatches", "+{0} 🪵 Matches"), Count);
		case ELootItem::RifleAmmo: return FText::Format(LOCTEXT("TookRifle", "+{0} 🔫 M16 rounds"), Count);
		case ELootItem::PistolAmmo: return FText::Format(LOCTEXT("TookPistol", "+{0} 🔫 9mm rounds"), Count);
		case ELootItem::ShotgunAmmo: return FText::Format(LOCTEXT("TookShotgun", "+{0} 💥 12g shells"), Count);
		case ELootItem::FlameFuel: return FText::Format(LOCTEXT("TookFuel", "+{0} 🔥 Fuel"), Count);
		case ELootItem::CryoAmmo: return FText::Format(LOCTEXT("TookCryo", "+{0} ❄️ Coolant"), Count);
		case ELootItem::PlasmaAmmo: return FText::Format(LOCTEXT("TookPlasma", "+{0} ⚡ Plasma"), Count);
		case ELootItem::Turret: return FText::Format(LOCTEXT("TookTurret", "+{0} 🎯 Turret"), Count);
		case ELootItem::Barricade: return FText::Format(LOCTEXT("TookBarricade", "+{0} 🧱 Barricade"), Count);
		case ELootItem::Mine: return FText::Format(LOCTEXT("TookMine", "+{0} 💣 Mine"), Count);
		case ELootItem::BonusWeapon: return FText::Format(LOCTEXT("TookWeapon", "⭐ Weapon: {0}"), FText::FromString(Id));
		default: return FText::Format(LOCTEXT("TookClothing", "🧥 Gear: {0}"), FText::FromString(Id));
		}
	}

	/** Ammo of weapons the squad does not carry yet is kept by type (Godot keeps it per weapon inventory). */
	FName AmmoKey(ELootItem Item)
	{
		switch (Item)
		{
		case ELootItem::PistolAmmo: return TEXT("pistol");
		case ELootItem::ShotgunAmmo: return TEXT("shotgun");
		case ELootItem::FlameFuel: return TEXT("flamethrower");
		case ELootItem::CryoAmmo: return TEXT("cryo_emitter");
		default: return TEXT("plasma_carbine");
		}
	}
}

ALootCrateActor::ALootCrateActor()
{
	PrimaryActorTick.bCanEverTick = true;
	CrateName = LOCTEXT("DefaultName", "📦 Army supply crate");
	DisplayName = CrateName;
	bCanBeRelocated = true;
	TrapDamage = 95.f; // Godot loot_crate.gd trap_damage
	TrapRadius = 350.f;

	// Godot box 1.2 x 0.8 x 0.8 m.
	Box->SetBoxExtent(FVector(60.f, 40.f, 40.f));
	Stash = CreateDefaultSubobject<UItemStashComponent>(TEXT("Stash"));
	Stash->Capacity = 500; // Sprint 13: generous default, tunable per crate
	Mesh->SetRelativeScale3D(FVector(1.2f, 0.8f, 0.8f));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}
}

void ALootCrateActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	DisplayName = CrateName;
	UpdateVisuals();
}

void ALootCrateActor::BeginPlay()
{
	Super::BeginPlay();
	Stash->OnChanged.AddUObject(this, &ALootCrateActor::UpdateVisuals); // stored / taken items: empty colour on / off
}

void ALootCrateActor::UpdateVisuals()
{
	// Godot _update_visuals colours (sRGB): wrecked, empty, trapped, golden stash, blue army crate.
	FColor Color(46, 107, 217);
	if (bDestroyed)
	{
		Color = FColor(31, 31, 31);
	}
	else if (IsLooted())
	{
		Color = FColor(56, 56, 61);
	}
	else if (bTrapped)
	{
		Color = FColor(191, 56, 46);
	}
	else if (Tier == ELootTier::Maximal)
	{
		Color = FColor(235, 184, 46);
	}
	if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
	{
		Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(Color));
	}
}

void ALootCrateActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bTrapped || bDestroyed)
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(FName(TEXT("Enemy"))))
		{
			continue;
		}
		const UHealthComponent* EnemyHealth = It->FindComponentByClass<UHealthComponent>();
		if (EnemyHealth && EnemyHealth->IsAlive() && FVector::Dist(GetActorLocation(), It->GetActorLocation()) <= TrapContactDistance)
		{
			DetonateTrap(false, LOCTEXT("EnemyContact", "Enemy contact"));
			return;
		}
	}
}

bool ALootCrateActor::HandleDirectInteraction(AOperativeCharacter* Leader)
{
	// Godot: an intact, untrapped crate with loot opens right away (no action menu).
	if (bDestroyed || IsLooted() || bTrapped || !Leader)
	{
		return false;
	}
	StartOpening(Leader);
	return true;
}

FActionMenuRequest ALootCrateActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const FText Squad = LOCTEXT("SquadSpeaker", "SQUAD");
	if (bDestroyed)
	{
		return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("Wrecked", "This crate was torn apart by the trap blast. Everything inside is destroyed."));
	}
	if (IsLooted())
	{
		return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("Empty", "This crate is empty. Everything useful has been taken."));
	}
	return FActionMenuRequest::MakeMenu(LOCTEXT("TrappedTitle", "📦 Booby-trapped crate"),
		FText::Format(LOCTEXT("TrappedDesc", "⚠️ WARNING: The crate is rigged with a tripwire charge!\nAny mistake or blast will destroy all supplies inside!\n{0}"),
			DescribeDefusal(Leader)),
		LOCTEXT("Defuse", "Defuse"), LOCTEXT("Cancel", "Cancel"), false, bCanBeRelocated, LOCTEXT("Relocate", "Relocate"));
}

void ALootCrateActor::ExecuteAction(AOperativeCharacter* User)
{
	if (!User)
	{
		return;
	}
	if (!bTrapped)
	{
		StartOpening(User);
		return;
	}
	// Godot _on_action_confirmed branch 1: face the crate, crouch, 2 s of careful work, then the attempt.
	const FRotator Facing = (GetActorLocation() - User->GetActorLocation()).Rotation();
	User->StopOperative();
	User->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	User->SetStance(EOperativeStance::Crouching);
	PostLine(User->DisplayName, FText::Format(LOCTEXT("Defusing", "🔧 {0}: \"Carefully disarming the tripwire on the crate...\""), User->DisplayName));
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ALootCrateActor::FinishDefusal,
		TWeakObjectPtr<AOperativeCharacter>(User)), FMath::Max(DefuseSeconds, 0.01f), false);
}

void ALootCrateActor::FinishDefusal(TWeakObjectPtr<AOperativeCharacter> WeakUser)
{
	AOperativeCharacter* User = WeakUser.Get();
	if (!User || bDestroyed)
	{
		return;
	}
	if (AttemptDefusal(User) == EDefusalResult::Success)
	{
		UpdateVisuals();
		StartOpening(User);
	}
}

void ALootCrateActor::StartOpening(AOperativeCharacter* Leader)
{
	if (!Leader || bDestroyed)
	{
		return;
	}
	const FRotator Facing = (GetActorLocation() - Leader->GetActorLocation()).Rotation();
	Leader->StopOperative();
	Leader->SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
	ReceiveOpening();
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &ALootCrateActor::FinishOpening,
		TWeakObjectPtr<AOperativeCharacter>(Leader)), FMath::Max(OpenSeconds, 0.01f), false);
}

void ALootCrateActor::FinishOpening(TWeakObjectPtr<AOperativeCharacter> WeakLeader)
{
	if (bDestroyed)
	{
		return;
	}
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->OpenLootDialog(this);
	}
}

FText ALootCrateActor::TakeItem(ELootItem Item, AOperativeCharacter* Collector)
{
	if (!Collector || bDestroyed)
	{
		return FText::GetEmpty();
	}
	SyncContentsIntoStash();
	const FString Id = Item == ELootItem::BonusWeapon ? Contents.BonusWeaponId
		: (Item == ELootItem::BonusClothing ? Contents.BonusClothingId : FString());
	ETransferItem Storable = ETransferItem::Medkit;
	const int32 Count = ItemStash::FromLootItem(Item, Storable) ? Stash->Take(Storable, MAX_int32) : Contents.Take(Item);
	if (Count <= 0)
	{
		return FText::GetEmpty();
	}
	switch (Item)
	{
	case ELootItem::Medkit: Collector->MedkitsCount += Count; break;
	case ELootItem::CannedFood: Collector->CannedFoodCount += Count; break;
	case ELootItem::Bread: Collector->BreadCount += Count; break;
	case ELootItem::Chocolate: Collector->ChocolateCount += Count; break;
	case ELootItem::Matches: Collector->MatchesCount += Count; break;
	case ELootItem::RifleAmmo: Collector->AddAmmo(TEXT("m16"), Count); break;
	// Godot adds engineering items without the carry limit.
	case ELootItem::Turret: Collector->TurretsCount += Count; break;
	case ELootItem::Barricade: Collector->BarricadesCount += Count; break;
	case ELootItem::Mine: Collector->MinesCount += Count; break;
	case ELootItem::BonusWeapon:
	case ELootItem::BonusClothing: Collector->BonusItems.Add(Id); break;
	default: Collector->AddAmmo(AmmoKey(Item).ToString(), Count); break;
	}
	UpdateVisuals();
	return TakeLine(Item, Count, Id);
}

void ALootCrateActor::TakeAll(AOperativeCharacter* Collector)
{
	if (IsLooted() || bDestroyed || !Collector)
	{
		return;
	}
	for (const FLootEntry& Entry : GetItems())
	{
		TakeItem(Entry.Item, Collector);
	}
	UpdateVisuals();
}

void ALootCrateActor::DetonateTrap(bool bByShot, const FText& InstigatorName)
{
	if (bDestroyed)
	{
		return;
	}
	bDestroyed = true;
	bTrapped = false;
	Contents.DestroyAll();
	Stash->Clear();
	UpdateVisuals();
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Marksman") : InstigatorName) : LOCTEXT("TrapBlast", "TRAP BLAST"),
		bByShot ? LOCTEXT("ShotBoom", "💥 A shot set off the crate trap! The crate is blown to splinters, everything inside burned!")
			: LOCTEXT("TrapBoom", "💥 The crate tripwire went off! The contents are destroyed by the blast!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("Source", "Crate trap"), LOCTEXT("SquadHit", "💥 Caught in the crate tripwire blast (-{0} HP)!"));
}

void ALootCrateActor::RestoreSaved(bool bInLooted, bool bInDefused, bool bInDestroyed)
{
	const bool bEmpty = bInLooted || bInDestroyed;
	bDestroyed = bInDestroyed;
	if (bInDefused || bInDestroyed)
	{
		bDefused = bInDefused;
		bTrapped = false;
	}
	if (bEmpty)
	{
		Contents.DestroyAll();
		Stash->Clear();
	}
	UpdateVisuals();
	if (bDestroyed)
	{
		// Godot: a destroyed crate is hidden and has no collision after a load.
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}
}

void ALootCrateActor::SyncContentsIntoStash()
{
	for (uint8 Index = 0; Index <= static_cast<uint8>(ETransferItem::PlasmaAmmo); ++Index)
	{
		const ETransferItem Item = static_cast<ETransferItem>(Index);
		const int32 Count = Contents.Take(ItemStash::ToLootItem(Item));
		if (Count > 0)
		{
			Stash->Add(Item, Count, true); // the authored loot never counts against the capacity
		}
	}
}

UItemStashComponent* ALootCrateActor::GetSyncedStash()
{
	SyncContentsIntoStash();
	return Stash;
}

TArray<FLootEntry> ALootCrateActor::GetItems() const
{
	FLootContents Merged = Contents;
	for (const TPair<ETransferItem, int32>& Pair : Stash->GetItems())
	{
		Merged.AddCount(ItemStash::ToLootItem(Pair.Key), Pair.Value);
	}
	return Merged.GetItems();
}

bool ALootCrateActor::IsLooted() const
{
	return bDestroyed || (Contents.IsEmpty() && Stash->IsEmpty());
}

int32 ALootCrateActor::GetStoredCount(ETransferItem Item) const
{
	return Stash->GetCount(Item) + Contents.GetCount(ItemStash::ToLootItem(Item));
}

#undef LOCTEXT_NAMESPACE
