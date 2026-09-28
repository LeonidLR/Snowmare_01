#include "Interactables/LootCrateActor.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/HealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Interactables/InteractionSubsystem.h"
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
		case ELootItem::Medkit: return FText::Format(LOCTEXT("TookMedkit", "+{0} 🩹 Аптечка"), Count);
		case ELootItem::CannedFood: return FText::Format(LOCTEXT("TookFood", "+{0} 🥫 Консервы"), Count);
		case ELootItem::Bread: return FText::Format(LOCTEXT("TookBread", "+{0} 🍞 Хлеб"), Count);
		case ELootItem::Chocolate: return FText::Format(LOCTEXT("TookChocolate", "+{0} 🍫 Шоколад"), Count);
		case ELootItem::Matches: return FText::Format(LOCTEXT("TookMatches", "+{0} 🪵 Спички"), Count);
		case ELootItem::RifleAmmo: return FText::Format(LOCTEXT("TookRifle", "+{0} 🔫 Патроны M16"), Count);
		case ELootItem::PistolAmmo: return FText::Format(LOCTEXT("TookPistol", "+{0} 🔫 Патроны к пистолету"), Count);
		case ELootItem::ShotgunAmmo: return FText::Format(LOCTEXT("TookShotgun", "+{0} 💥 Дробь 12k"), Count);
		case ELootItem::FlameFuel: return FText::Format(LOCTEXT("TookFuel", "+{0} 🔥 Топливо"), Count);
		case ELootItem::CryoAmmo: return FText::Format(LOCTEXT("TookCryo", "+{0} ❄️ Хладагент"), Count);
		case ELootItem::PlasmaAmmo: return FText::Format(LOCTEXT("TookPlasma", "+{0} ⚡ Плазма"), Count);
		case ELootItem::Turret: return FText::Format(LOCTEXT("TookTurret", "+{0} 🎯 Турель"), Count);
		case ELootItem::Barricade: return FText::Format(LOCTEXT("TookBarricade", "+{0} 🧱 Баррикада"), Count);
		case ELootItem::Mine: return FText::Format(LOCTEXT("TookMine", "+{0} 💣 Мина"), Count);
		case ELootItem::BonusWeapon: return FText::Format(LOCTEXT("TookWeapon", "⭐ Оружие: {0}"), FText::FromString(Id));
		default: return FText::Format(LOCTEXT("TookClothing", "🧥 Снаряжение: {0}"), FText::FromString(Id));
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
	CrateName = LOCTEXT("DefaultName", "📦 Армейский ящик снабжения");
	DisplayName = CrateName;
	bCanBeRelocated = true;
	TrapDamage = 95.f; // Godot loot_crate.gd trap_damage
	TrapRadius = 350.f;

	// Godot box 1.2 x 0.8 x 0.8 m.
	Box->SetBoxExtent(FVector(60.f, 40.f, 40.f));
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

void ALootCrateActor::UpdateVisuals()
{
	// Godot _update_visuals colours (sRGB): wrecked, empty, trapped, golden stash, blue army crate.
	FColor Color(46, 107, 217);
	if (bDestroyed)
	{
		Color = FColor(31, 31, 31);
	}
	else if (bLooted)
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
			DetonateTrap(false, LOCTEXT("EnemyContact", "Контакт с противником"));
			return;
		}
	}
}

bool ALootCrateActor::HandleDirectInteraction(AOperativeCharacter* Leader)
{
	// Godot: an intact, untrapped crate with loot opens right away (no action menu).
	if (bDestroyed || bLooted || bTrapped || !Leader)
	{
		return false;
	}
	StartOpening(Leader);
	return true;
}

FActionMenuRequest ALootCrateActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	const FText Squad = LOCTEXT("SquadSpeaker", "Отряд");
	if (bDestroyed)
	{
		return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("Wrecked", "Этот ящик разорван взрывом ловушки. Всё содержимое уничтожено."));
	}
	if (bLooted)
	{
		return FActionMenuRequest::MakeMessage(Squad, LOCTEXT("Empty", "Этот ящик уже пуст. Всё полезное забрали."));
	}
	return FActionMenuRequest::MakeMenu(LOCTEXT("TrappedTitle", "📦 Заминированный ящик"),
		FText::Format(LOCTEXT("TrappedDesc", "⚠️ ВНИМАНИЕ: Ящик заминирован взрывной растяжкой!\nЛюбая ошибка или взрыв уничтожит все припасы внутри!\n{0}"),
			DescribeDefusal(Leader)),
		LOCTEXT("Defuse", "Разминировать"), LOCTEXT("Cancel", "Отмена"), false, bCanBeRelocated, LOCTEXT("Relocate", "Переместить"));
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
	PostLine(User->DisplayName, FText::Format(LOCTEXT("Defusing", "🔧 {0}: «Осторожно обезвреживаю растяжку на ящике...»"), User->DisplayName));
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
	const FString Id = Item == ELootItem::BonusWeapon ? Contents.BonusWeaponId
		: (Item == ELootItem::BonusClothing ? Contents.BonusClothingId : FString());
	const int32 Count = Contents.Take(Item);
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
	case ELootItem::RifleAmmo: Collector->ReserveAmmo += Count; break; // the M16 reserve
	// Godot adds engineering items without the carry limit.
	case ELootItem::Turret: Collector->TurretsCount += Count; break;
	case ELootItem::Barricade: Collector->BarricadesCount += Count; break;
	case ELootItem::Mine: Collector->MinesCount += Count; break;
	case ELootItem::BonusWeapon:
	case ELootItem::BonusClothing: Collector->BonusItems.Add(Id); break;
	default: Collector->ExtraAmmo.FindOrAdd(AmmoKey(Item)) += Count; break;
	}
	if (Contents.IsEmpty())
	{
		bLooted = true;
		UpdateVisuals();
	}
	return TakeLine(Item, Count, Id);
}

void ALootCrateActor::TakeAll(AOperativeCharacter* Collector)
{
	if (bLooted || bDestroyed || !Collector)
	{
		return;
	}
	for (const FLootEntry& Entry : Contents.GetItems())
	{
		TakeItem(Entry.Item, Collector);
	}
	bLooted = true;
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
	bLooted = true;
	Contents.DestroyAll();
	UpdateVisuals();
	PostLine(bByShot ? (InstigatorName.IsEmpty() ? LOCTEXT("Sniper", "Снайпер") : InstigatorName) : LOCTEXT("TrapBlast", "ВЗРЫВ ЛОВУШКИ"),
		bByShot ? LOCTEXT("ShotBoom", "💥 Взрыв ловушки ящика от выстрела! Ящик разорван в щепки, всё содержимое сгорело!")
			: LOCTEXT("TrapBoom", "💥 Растяжка на ящике сдетонировала! Содержимое ящика уничтожено взрывом!"));
	ApplyBlast(TrapDamage, TrapDamage * DeployableRules::SquadDamageScale, TrapRadius, 0.45f, EDamageType::Explosive,
		LOCTEXT("Source", "Ловушка ящика"), LOCTEXT("SquadHit", "💥 Задело взрывом растяжки ящика (-{0} HP)!"));
}

#undef LOCTEXT_NAMESPACE
