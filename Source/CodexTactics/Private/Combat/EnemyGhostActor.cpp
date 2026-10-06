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
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // after the enemy's mesh evaluated its pose this frame
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
	TArray<UMeshComponent*> Meshes;
	Enemy.GetComponents<UMeshComponent>(Meshes);
	bool bSameMeshes = Source.Get() == &Enemy && Copies.Num() == CopySources.Num() && Copies.Num() > 0;
	for (int32 Index = 0; bSameMeshes && Index < CopySources.Num(); ++Index)
	{
		bSameMeshes = Copies[Index] && CopySources[Index].IsValid() && Meshes.Contains(CopySources[Index].Get());
	}
	if (bSameMeshes)
	{
		SyncFromSource(true);
	}
	else
	{
		Source = &Enemy;
		CopyMeshes(Enemy);
	}
}

void AEnemyGhostActor::SetHeard(bool bInHeard)
{
	bHeard = bInHeard;
	UpdateTickEnabled();
}

void AEnemyGhostActor::UpdateTickEnabled()
{
	SetActorTickEnabled(Source.IsValid() && (bHeard || PendingPoseFrames > 0));
}

void AEnemyGhostActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Source.IsValid())
	{
		SyncFromSource(bHeard);
		PendingPoseFrames = FMath::Max(0, PendingPoseFrames - 1);
	}
	UpdateTickEnabled();
}

void AEnemyGhostActor::SyncFromSource(bool bMove)
{
	AEnemyCharacter* Enemy = Source.Get();
	if (!Enemy)
	{
		return;
	}
	if (bMove)
	{
		SetActorTransform(Enemy->GetActorTransform());
		LastKnownFeet = Enemy->GetActorLocation() - FVector(0.f, 0.f, Enemy->GetSimpleCollisionHalfHeight());
	}
	for (int32 Index = 0; Index < Copies.Num() && Index < CopySources.Num(); ++Index)
	{
		UMeshComponent* Copy = Copies[Index];
		UMeshComponent* From = CopySources[Index].Get();
		if (!Copy || !From)
		{
			continue;
		}
		if (bMove)
		{
			Copy->SetWorldTransform(From->GetComponentTransform());
		}
		UPoseableMeshComponent* Posed = Cast<UPoseableMeshComponent>(Copy);
		USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(From);
		if (Posed && Skeletal && Skeletal->GetSkinnedAsset() == Posed->GetSkinnedAsset())
		{
			Posed->CopyPoseFromSkeletalComponent(Skeletal);
		}
	}
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
	CopySources.Reset();
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
		CopySources.Add(Mesh);
	}
	// The enemy was just hidden: its bones were stale (never rendered: the reference pose) until its mesh refreshes them
	// on the next frame (AlwaysTickPoseAndRefreshBones while hidden), so the pose is copied again then.
	PendingPoseFrames = 2;
	UpdateTickEnabled();
}
