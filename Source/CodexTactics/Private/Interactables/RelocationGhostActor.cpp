#include "Interactables/RelocationGhostActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ARelocationGhostActor::ARelocationGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->SetCanEverAffectNavigation(false);

	// Godot _set_ghost_material_valid: a see-through glowing hologram (M_GhostHologram from
	// Scripts/Editor/create_ghost_hologram_material.py); the opaque engine material when it is missing.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hologram(TEXT("/Game/VFX/Materials/M_GhostHologram.M_GhostHologram"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	GhostBaseMaterial = Hologram.Succeeded() ? Hologram.Object : BaseMaterial.Object;
}

void ARelocationGhostActor::CopyFrom(const UStaticMeshComponent* SourceMesh, const AActor* SourceActor)
{
	if (!SourceMesh || !SourceActor)
	{
		return;
	}
	Mesh->SetStaticMesh(SourceMesh->GetStaticMesh());
	// Keep the mesh offset / rotation / scale relative to the actor so the ghost matches the real object.
	Mesh->SetRelativeTransform(SourceMesh->GetComponentTransform().GetRelativeTransform(SourceActor->GetActorTransform()));
	ApplyMaterial();
}

void ARelocationGhostActor::CopyFromTemplate(const UStaticMeshComponent* TemplateMesh)
{
	if (!TemplateMesh)
	{
		return;
	}
	Mesh->SetStaticMesh(TemplateMesh->GetStaticMesh());
	Mesh->SetRelativeTransform(TemplateMesh->GetRelativeTransform());
	ApplyMaterial();
}

void ARelocationGhostActor::ApplyMaterial()
{
	Material = UMaterialInstanceDynamic::Create(GhostBaseMaterial, this);
	for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
	{
		Mesh->SetMaterial(Slot, Material);
	}
	SetValid(true);
}

void ARelocationGhostActor::SetColors(const FLinearColor& Valid, const FLinearColor& Invalid)
{
	ValidColor = Valid;
	InvalidColor = Invalid;
	SetValid(bLastValid);
}

void ARelocationGhostActor::SetValid(bool bValid)
{
	bLastValid = bValid;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), bValid ? ValidColor : InvalidColor);
	}
}
