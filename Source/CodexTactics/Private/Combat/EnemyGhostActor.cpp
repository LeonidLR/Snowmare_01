#include "Combat/EnemyGhostActor.h"

#include "Characters/EnemyCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkinnedAsset.h"
#include "Materials/MaterialInterface.h"

AEnemyGhostActor::AEnemyGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetActorEnableCollision(false);
}

void AEnemyGhostActor::InitFrom(AEnemyCharacter& Enemy, UMaterialInterface* Material)
{
	GhostMaterial = Material;
	Source = &Enemy;
	CopyMeshes(Enemy);
}

void AEnemyGhostActor::SnapTo(AEnemyCharacter& Enemy)
{
	CopyMeshes(Enemy);
}

FVector AEnemyGhostActor::GetAimPoint() const
{
	return LastKnownFeet + FVector(0.f, 0.f, ProfileHeight * 0.6f);
}

void AEnemyGhostActor::CopyMeshes(AEnemyCharacter& Enemy)
{
	for (UMeshComponent* Old : Copies)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	Copies.Reset();
	SetActorTransform(Enemy.GetActorTransform());
	LastKnownFeet = Enemy.GetActorLocation() - FVector(0.f, 0.f, Enemy.GetSimpleCollisionHalfHeight());
	ProfileHeight = FMath::Min(150.f, Enemy.GetSimpleCollisionHalfHeight() * 2.f * 0.85f);

	TArray<UMeshComponent*> Meshes;
	Enemy.GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->IsVisible() || Mesh->bHiddenInGame)
		{
			continue;
		}
		UMeshComponent* Copy = nullptr;
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Mesh))
		{
			if (!Skeletal->GetSkinnedAsset())
			{
				continue;
			}
			UPoseableMeshComponent* Posed = NewObject<UPoseableMeshComponent>(this);
			Posed->SetSkinnedAssetAndUpdate(Skeletal->GetSkinnedAsset());
			Posed->RegisterComponent();
			Posed->SetWorldTransform(Skeletal->GetComponentTransform());
			Posed->CopyPoseFromSkeletalComponent(Skeletal);
			Copy = Posed;
		}
		else if (const UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Mesh))
		{
			if (!Static->GetStaticMesh())
			{
				continue;
			}
			UStaticMeshComponent* StaticCopy = NewObject<UStaticMeshComponent>(this);
			StaticCopy->SetStaticMesh(Static->GetStaticMesh());
			StaticCopy->RegisterComponent();
			StaticCopy->SetWorldTransform(Static->GetComponentTransform());
			Copy = StaticCopy;
		}
		if (!Copy)
		{
			continue;
		}
		Copy->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetCastShadow(false);
		for (int32 Slot = 0; GhostMaterial && Slot < Copy->GetNumMaterials(); ++Slot)
		{
			Copy->SetMaterial(Slot, GhostMaterial);
		}
		Copies.Add(Copy);
	}
}
