#include "Interactables/RelocationGhostActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ARelocationGhostActor::ARelocationGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	GhostBaseMaterial = BaseMaterial.Object;
}

void ARelocationGhostActor::CopyFrom(const UStaticMeshComponent* SourceMesh, const AActor* SourceActor)
{
	if (!SourceMesh || !SourceActor)
	{
		return;
	}
	Mesh->SetStaticMesh(SourceMesh->GetStaticMesh());
	// Keep the mesh offset / scale relative to the actor so the ghost matches the real object.
	const FTransform Relative = SourceMesh->GetComponentTransform().GetRelativeTransform(SourceActor->GetActorTransform());
	Mesh->SetWorldScale3D(Relative.GetScale3D());
	Material = UMaterialInstanceDynamic::Create(GhostBaseMaterial, this);
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		Mesh->SetMaterial(Slot, Material);
	}
	SetValid(true);
}

void ARelocationGhostActor::SetValid(bool bValid)
{
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), bValid ? ValidColor : InvalidColor);
	}
}
