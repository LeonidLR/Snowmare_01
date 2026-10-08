#include "Interactables/DroppedItemActor.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interactables/ItemStashComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "UI/OverheadLabel.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "DroppedItemActor"

namespace
{
	FColor DroppedColor(ETransferItem Item)
	{
		if (TransferRules::IsAmmoItem(Item))
		{
			return FColor(230, 190, 40);
		}
		switch (Item)
		{
		case ETransferItem::Medkit: return FColor(210, 40, 40);
		case ETransferItem::Turret:
		case ETransferItem::Barricade:
		case ETransferItem::Mine: return FColor(120, 125, 130);
		case ETransferItem::Matches: return FColor(235, 120, 30);
		default: return FColor(140, 95, 55);
		}
	}
}

ADroppedItemActor::ADroppedItemActor()
{
	DisplayName = LOCTEXT("Name", "Items on the ground");
	ObjectType = EInteractableType::GateTerminal; // unused: the pile never opens the quest flow (HandleDirectInteraction)
	InteractionDistance = 120.f;
	Box->SetBoxExtent(FVector(PileHalfSize));
	// Clickable (visibility traces) but neither blocks pawns nor cuts the navmesh.
	Box->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCanEverAffectNavigation(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		Mesh->SetMaterial(0, BaseMaterial.Object);
	}
	Stash = CreateDefaultSubobject<UItemStashComponent>(TEXT("Stash"));
}

void ADroppedItemActor::OnConstruction(const FTransform& Transform)
{
	Box->SetBoxExtent(FVector(PileHalfSize));
	Super::OnConstruction(Transform);
}

void ADroppedItemActor::BeginPlay()
{
	Super::BeginPlay();
	Stash->OnChanged.AddUObject(this, &ADroppedItemActor::HandleStashChanged);
	UpdateVisuals();
}

void ADroppedItemActor::HandleStashChanged()
{
	if (Stash->IsEmpty())
	{
		Destroy();
		return;
	}
	UpdateVisuals();
}

void ADroppedItemActor::UpdateVisuals()
{
	const TArray<ETransferItem> Types = Stash->GetItemTypes();
	if (!Types.IsEmpty())
	{
		if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(DroppedColor(Types[0])));
		}
	}
}

FString ADroppedItemActor::GetContentsText() const
{
	TArray<FString> Lines;
	for (const ETransferItem Item : Stash->GetItemTypes())
	{
		Lines.Add(FString::Printf(TEXT("%s x%d"), *TransferRules::GetItemName(Item), Stash->GetCount(Item)));
	}
	return FString::Join(Lines, TEXT("\n"));
}

bool ADroppedItemActor::GetOverheadLabel(FOverheadLabel& OutLabel) const
{
	if (Stash->IsEmpty())
	{
		return false;
	}
	OutLabel.Text = GetContentsText();
	OutLabel.Color = FLinearColor(1.f, 0.9f, 0.55f);
	OutLabel.HeightCm = 70.f;
	return true;
}

bool ADroppedItemActor::HandleDirectInteraction(AOperativeCharacter* Leader)
{
	USquadTransferSubsystem* Transfer = GetWorld() ? GetWorld()->GetSubsystem<USquadTransferSubsystem>() : nullptr;
	if (!Leader || !Transfer)
	{
		return false;
	}
	Transfer->PickUpAll(Leader, this);
	return true;
}

FActionMenuRequest ADroppedItemActor::BuildActionMenu(const AOperativeCharacter* Leader) const
{
	return FActionMenuRequest::MakeMessage(DisplayName, FText::FromString(GetContentsText()));
}

FVector ADroppedItemActor::FindGroundPoint(UWorld* World, const FVector& Point)
{
	FVector Ground = Point;
	if (!World)
	{
		return Ground;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DroppedItemGround), false);
	for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	if (World->LineTraceSingleByObjectType(Hit, Point + FVector(0.f, 0.f, 150.f), Point - FVector(0.f, 0.f, 600.f), Objects, Params))
	{
		Ground = Hit.ImpactPoint;
	}
	if (const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Ground, OnNav, FVector(150.f, 150.f, 250.f)))
		{
			// Keep the traced height (the navmesh floats a little above the floor), take the reachable XY.
			Ground = FVector(OnNav.Location.X, OnNav.Location.Y, Ground.Z);
		}
	}
	return Ground;
}

ADroppedItemActor* ADroppedItemActor::SpawnOrMerge(UWorld* World, const FVector& Point, ETransferItem Item, int32 Count)
{
	if (!World || Count <= 0)
	{
		return nullptr;
	}
	const FVector Ground = FindGroundPoint(World, Point);
	for (TActorIterator<ADroppedItemActor> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && FVector::Dist2D(It->GetActorLocation(), Ground) <= MergeRadius)
		{
			It->Stash->Add(Item, Count, true);
			return *It;
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADroppedItemActor* Pile = World->SpawnActorDeferred<ADroppedItemActor>(ADroppedItemActor::StaticClass(), FTransform(Ground),
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pile)
	{
		return nullptr;
	}
	Pile->Stash->Add(Item, Count, true);
	Pile->FinishSpawning(FTransform(Ground + FVector(0.f, 0.f, Pile->PileHalfSize)));
	return Pile;
}

#undef LOCTEXT_NAMESPACE
