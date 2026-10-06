#include "Tactics/CoverGhostActor.h"

#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Tactics/CoverRules.h"
#include "Tactics/CoverTraceRules.h"
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hologram(TEXT("/Game/VFX/Materials/M_GhostHologram.M_GhostHologram"));
	if (Hologram.Succeeded())
	{
		GhostMaterial = Hologram.Object;
	}
	SetActorHiddenInGame(true);
}

void ACoverGhostActor::ShowFor(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot)
{
	Operative = const_cast<AOperativeCharacter*>(&InOperative);
	Slot = InSlot;
	bShown = true;
	if (!Material && GhostMaterial)
	{
		Material = UMaterialInstanceDynamic::Create(GhostMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), GhostColor);
	}
	const float HalfHeight = InOperative.GetSimpleCollisionHalfHeight();
	SetActorLocationAndRotation(InSlot.WorldLocation + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, CoverTraceRules::FacingYaw(InSlot), 0.f));

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
			Body->SetMaterial(Index, Material);
		}
		// The cover idle of the slot's height (the plain idle while no cover clips are assigned).
		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source->GetAnimInstance());
		const bool bCrouched = CoverRules::DefaultStanceFor(InSlot.Height) == EOperativeStance::Crouching;
		UAnimSequenceBase* Clip = nullptr;
		if (Anim)
		{
			const TArray<TObjectPtr<UAnimSequenceBase>>& Idles = bCrouched ? Anim->CoverCrouchIdle : Anim->CoverStandIdle;
			for (const TObjectPtr<UAnimSequenceBase>& Candidate : Idles)
			{
				if (Candidate)
				{
					Clip = Candidate;
					break;
				}
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
