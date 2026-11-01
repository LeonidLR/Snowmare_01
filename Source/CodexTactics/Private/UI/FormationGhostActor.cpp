#include "UI/FormationGhostActor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "CodexTactics.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/FormationPlacementRules.h"
#include "UObject/ConstructorHelpers.h"

namespace FormationGhostLocal
{
	/** The see-through overlay's alpha (M_Silhouette: core 0.2 x alpha, rim up to 0.85): a faint outline where a wall hides the ghost. */
	constexpr float OverlayAlpha = 0.6f;
}

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
	// The placement pause dilates the world clock to almost 0: the ghost always evaluates its (held) pose.
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Hologram(TEXT("/Game/UI/Materials/M_FormationGhost.M_FormationGhost"));
	if (Hologram.Succeeded())
	{
		HologramMaterial = Hologram.Object;
	}
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
	UMaterialInterface* SeeThrough = Operative.SilhouetteMaterial ? Operative.SilhouetteMaterial.Get() : GhostMaterial.Get();
	UMaterialInterface* Base = HologramMaterial ? HologramMaterial.Get() : SeeThrough;
	if (Base && (!Material || Material->Parent != Base))
	{
		Material = UMaterialInstanceDynamic::Create(Base, this);
	}
	if (HologramMaterial && SeeThrough && (!Overlay || Overlay->Parent != SeeThrough))
	{
		Overlay = UMaterialInstanceDynamic::Create(SeeThrough, this);
	}
	HalfHeight = Operative.GetCapsuleComponent() ? Operative.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
	USkeletalMeshComponent* Source = Operative.GetMesh();
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;
	PoseSource = EFormationGhostPose::None;
	PoseOffRefDeg = 0.f;
	PoseClipName.Reset();
	if (Asset)
	{
		Body->SetLeaderPoseComponent(nullptr);
		Body->SetSkeletalMesh(Asset);
		Body->SetRelativeTransform(Source->GetRelativeTransform());
		for (int32 Index = 0; Material && Index < Body->GetNumMaterials(); ++Index)
		{
			Body->SetMaterial(Index, Material); // only the hologram, no opaque body
		}
		Body->SetOverlayMaterial(HologramMaterial ? Overlay.Get() : Material.Get());
		// The pose needs the ghost's transform first (the evaluation runs at once, before the actor is shown).
		SetPlacement(InFeet, Yaw);
		ApplyPose(Operative, *Source);
	}
	bShown = true;
	FlashRemaining = 0.f;
	SetPlacement(InFeet, Yaw);
	ApplyColor();
	SetActorHiddenInGame(false);
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationCustom] ghost of %s: pose %s %s (%.0f deg off the reference pose), material %s"), *Operative.DisplayName.ToString(),
		*UEnum::GetValueAsString(PoseSource), *PoseClipName, PoseOffRefDeg, *GetNameSafe(Base));
}

void AFormationGhostActor::ApplyPose(const AOperativeCharacter& Operative, USkeletalMeshComponent& Source)
{
	// 1. His idle clip held at one frame: single node, play rate 0, the pose evaluated now (the placement pause leaves no tick time to wait for).
	// The baseline set's Idle when the ABP has one; ABP_Operative plays its idle from its own graph (the Rifle_2 locomotion) and leaves the
	// baseline set empty - the bug_05 T-pose: no clip at all - so the ready clip of that graph is the default.
	const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source.GetAnimInstance());
	UAnimSequenceBase* Clip = Anim ? Anim->Animations.Idle.Get() : nullptr;
	if (!Clip && !ReadyPoseClip.IsNull())
	{
		Clip = ReadyPoseClip.LoadSynchronous();
	}
	if (Clip)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetAnimation(Clip);
		Body->SetPlayRate(0.f);
		Body->SetPosition(Clip->GetPlayLength() * FMath::Clamp(IdlePoseFraction, 0.f, 1.f), /*bFireNotifies*/ false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		PoseOffRefDeg = OffRefDeg(Body->GetBoneSpaceTransformsView());
		if (FormationPlacementRules::IsPosed(PoseOffRefDeg))
		{
			PoseSource = EFormationGhostPose::IdleClip;
			PoseClipName = Clip->GetName();
			return;
		}
		UE_LOG(LogCodexTactics, Warning, TEXT("[FormationCustom] ghost of %s: the idle clip %s left the reference pose (%.1f deg) - the ghost follows his own pose"),
			*Operative.DisplayName.ToString(), *Clip->GetName(), PoseOffRefDeg);
	}
	// 2. The fallback: his own current pose (the same mesh asset), followed.
	Body->SetLeaderPoseComponent(&Source, /*bForceUpdate*/ true);
	PoseOffRefDeg = OffRefDeg(Source.GetBoneSpaceTransformsView());
	PoseSource = FormationPlacementRules::IsPosed(PoseOffRefDeg) ? EFormationGhostPose::OperativePose : EFormationGhostPose::None;
}

float AFormationGhostActor::OffRefDeg(TConstArrayView<FTransform> Pose) const
{
	const USkinnedAsset* Asset = Body ? Body->GetSkinnedAsset() : nullptr;
	return Asset ? FormationPlacementRules::PoseOffRefDeg(Pose, Asset->GetRefSkeleton().GetRefBonePose()) : 0.f;
}

const UMaterialInterface* AFormationGhostActor::GetHologramMaterial() const
{
	return Material ? Material->Parent.Get() : nullptr;
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
	PoseSource = EFormationGhostPose::None;
	if (Body)
	{
		Body->SetLeaderPoseComponent(nullptr); // never keep following an operative after the mode
	}
	SetActorHiddenInGame(true);
}

void AFormationGhostActor::ApplyColor()
{
	const FLinearColor Tint = FlashRemaining > 0.f ? RefusedColor : bSelected ? SelectedColor : HoloColor;
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("Color"), Tint);
		Material->SetScalarParameterValue(TEXT("EmissiveIntensity"), bSelected || FlashRemaining > 0.f ? SelectedIntensity : Intensity);
	}
	if (Overlay)
	{
		Overlay->SetVectorParameterValue(TEXT("Color"), FLinearColor(Tint.R, Tint.G, Tint.B, FormationGhostLocal::OverlayAlpha));
	}
}
