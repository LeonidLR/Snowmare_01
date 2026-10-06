#include "Tactics/CoverGhostActor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverRules.h"
#include "UObject/ConstructorHelpers.h"

ACoverGhostActor::ACoverGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));
	Placeholder->SetupAttachment(Root);
	Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Placeholder->SetCastShadow(false);
	Placeholder->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		Placeholder->SetStaticMesh(Cylinder.Object);
	}
	// The see-through silhouette of the operatives (AOperativeCharacter::SetSilhouetteVisible): translucent, unlit, depth
	// test off, skeletal-mesh usage. M_GhostHologram has no skeletal-mesh usage flag, so on a skeletal mesh it fell back to
	// the default material — the ghost never looked holographic (user report 2026-10-06).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Silhouette(TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
	if (Silhouette.Succeeded())
	{
		GhostMaterial = Silhouette.Object;
	}
	SetActorHiddenInGame(true);
}

UMaterialInterface* ACoverGhostActor::ResolveBaseMaterial(const AOperativeCharacter& InOperative) const
{
	// Exactly the operative's own see-through material when it has one set, else the shared M_Silhouette.
	return InOperative.SilhouetteMaterial ? InOperative.SilhouetteMaterial.Get() : GhostMaterial.Get();
}

void ACoverGhostActor::ShowFor(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot)
{
	Operative = const_cast<AOperativeCharacter*>(&InOperative);
	Slot = InSlot;
	bShown = true;
	UMaterialInterface* Base = ResolveBaseMaterial(InOperative);
	if (Base && (!Material || Material->Parent != Base))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
	}
	if (Material)
	{
		// The operative's silhouette colour (leader cyan 0.55) — the same look as his see-through outline.
		Material->SetVectorParameterValue(TEXT("Color"), InOperative.GetSilhouetteColor());
	}
	// Back to the wall, facing along it towards the side he would face there (CoverFacingRules, user rule 2026-10-06).
	const ECoverFacing Facing = InOperative.PredictCoverFacing(InSlot);
	const float HalfHeight = InOperative.GetSimpleCollisionHalfHeight();
	SetActorLocationAndRotation(InSlot.WorldLocation + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, CoverFacingRules::FacingYaw(InSlot, Facing), 0.f));

	const USkeletalMeshComponent* Source = InOperative.GetMesh();
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;
	if (Asset)
	{
		Body->SetSkeletalMesh(Asset);
		Body->SetRelativeTransform(Source->GetRelativeTransform());
		Body->SetVisibility(true);
		Placeholder->SetVisibility(false);
		for (int32 Index = 0; Material && Index < Body->GetNumMaterials(); ++Index)
		{
			Body->SetMaterial(Index, Material); // no opaque body: only the hologram
		}
		Body->SetOverlayMaterial(Material); // the same overlay technique as the operatives' see-through silhouette
		// The cover idle of the slot's height and facing side (the plain idle while no cover clips are assigned).
		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source->GetAnimInstance());
		const bool bCrouched = CoverRules::DefaultStanceFor(InSlot.Height) == EOperativeStance::Crouching;
		UAnimSequenceBase* Clip = nullptr;
		if (Anim)
		{
			const TArray<TObjectPtr<UAnimSequenceBase>>& Idles = bCrouched ? Anim->CoverCrouchIdle : Anim->CoverStandIdle;
			const int32 Wanted = CoverFacingRules::ClipIndex(Facing);
			Clip = Idles.IsValidIndex(Wanted) && Idles[Wanted] ? Idles[Wanted].Get() : nullptr;
			for (int32 Index = 0; !Clip && Index < Idles.Num(); ++Index)
			{
				Clip = Idles[Index].Get();
			}
			if (!Clip)
			{
				Clip = bCrouched ? Anim->Animations.CrouchIdle.Get() : Anim->Animations.Idle.Get();
			}
		}
		if (Clip)
		{
			Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Body->SetAnimation(Clip);
			Body->Play(true);
		}
	}
	else
	{
		Body->SetVisibility(false);
		Placeholder->SetVisibility(true);
		const bool bCrouchedBody = CoverRules::DefaultStanceFor(InSlot.Height) == EOperativeStance::Crouching;
		Placeholder->SetRelativeScale3D(FVector(0.7f, 0.7f, bCrouchedBody ? 1.2f : 1.8f));
		Placeholder->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
		if (Material)
		{
			Placeholder->SetMaterial(0, Material);
			Placeholder->SetOverlayMaterial(Material);
		}
	}
	SetActorHiddenInGame(false);
}

void ACoverGhostActor::Hide()
{
	bShown = false;
	SetActorHiddenInGame(true);
	if (Body)
	{
		Body->Stop();
	}
}
