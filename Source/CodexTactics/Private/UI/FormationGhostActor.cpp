#include "UI/FormationGhostActor.h"

#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AFormationGhostActor::AFormationGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// The placement pause dilates the world clock to almost 0: the ghost always evaluates its pose (its idle's first frame at least).
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Silhouette(TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
	if (Silhouette.Succeeded())
	{
		GhostMaterial = Silhouette.Object;
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AFormationGhostActor::ShowFor(const AOperativeCharacter& Operative, const FVector& InFeet, float Yaw)
{
	UMaterialInterface* Base = Operative.SilhouetteMaterial ? Operative.SilhouetteMaterial.Get() : GhostMaterial.Get();
	if (Base && (!Material || Material->Parent != Base))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
	}
	HalfHeight = Operative.GetCapsuleComponent() ? Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	const USkeletalMeshComponent* Source = Operative.GetMesh();
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;
	if (Asset)
	{
		Body->SetSkeletalMesh(Asset);
		Body->SetRelativeTransform(Source->GetRelativeTransform());
		for (int32 Index = 0; Material && Index < Body->GetNumMaterials(); ++Index)
		{
			Body->SetMaterial(Index, Material); // only the hologram, no opaque body
		}
		Body->SetOverlayMaterial(Material);
		const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source->GetAnimInstance());
		UAnimSequenceBase* Clip = Anim ? Anim->Animations.Idle.Get() : nullptr;
		if (Clip)
		{
			Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Body->SetAnimation(Clip);
			Body->Play(true);
		}
	}
	bShown = true;
	FlashRemaining = 0.f;
	SetPlacement(InFeet, Yaw);
	ApplyColor();
	SetActorHiddenInGame(false);
}

void AFormationGhostActor::SetPlacement(const FVector& InFeet, float Yaw)
{
	Feet = InFeet;
	GhostYaw = Yaw;
	SetActorLocationAndRotation(InFeet + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Yaw, 0.f));
}

void AFormationGhostActor::SetSelected(bool bInSelected)
{
	if (bSelected != bInSelected)
	{
		bSelected = bInSelected;
		ApplyColor();
	}
}

void AFormationGhostActor::FlashRefused(float Seconds)
{
	FlashRemaining = FMath::Max(FlashRemaining, Seconds);
	ApplyColor();
}

void AFormationGhostActor::TickFlash(float RealDeltaSeconds)
{
	if (FlashRemaining > 0.f)
	{
		FlashRemaining -= RealDeltaSeconds;
		if (FlashRemaining <= 0.f)
		{
			FlashRemaining = 0.f;
			ApplyColor();
		}
	}
}

void AFormationGhostActor::Hide()
{
	bShown = false;
	bSelected = false;
	FlashRemaining = 0.f;
	SetActorHiddenInGame(true);
}

void AFormationGhostActor::ApplyColor()
{
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), FlashRemaining > 0.f ? RefusedColor : bSelected ? SelectedColor : HoloColor);
	}
}
