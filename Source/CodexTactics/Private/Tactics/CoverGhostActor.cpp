#include "Tactics/CoverGhostActor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/StealthModeRules.h"
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
	// The hologram is drawn through walls (depth test off), but the renderer's occlusion culling still dropped a ghost whose bounds lay wholly
	// behind a wall - the window silhouette inside the room past the window was never drawn (designer 2026-11-22, WindowStairHoverSmoke
	// shots). Grown bounds keep a part of them in view, so the ghost is not culled.
	Body->SetBoundsScale(4.f);
	Placeholder->SetBoundsScale(4.f);
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
	ShowPose(InOperative, InSlot, InOperative.PredictCoverFacing(InSlot), false);
}

void ACoverGhostActor::ShowForFacing(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot, ECoverFacing Facing)
{
	ShowPose(InOperative, InSlot, Facing, false);
}

void ACoverGhostActor::ShowCornerPeekFor(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot, ECoverFacing Side, TOptional<bool> CrouchedOverride)
{
	ShowPose(InOperative, InSlot, Side, true, CrouchedOverride);
}

namespace
{
	bool AreSlotsEquivalent(const FCoverSlot& A, const FCoverSlot& B)
	{
		return A.WallActor == B.WallActor
			&& A.WallComponent == B.WallComponent
			&& A.Height == B.Height
			&& A.bLeftEdgeExposed == B.bLeftEdgeExposed
			&& A.bRightEdgeExposed == B.bRightEdgeExposed
			&& FVector::DistSquared(A.WallNormal, B.WallNormal) <= 1e-4f
			&& FVector::DistSquared(A.WorldLocation, B.WorldLocation) <= 1.f;
	}
}

void ACoverGhostActor::ShowPose(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot, ECoverFacing Facing, bool bFireIdle, TOptional<bool> CrouchedOverride)
{
	const bool bStealthMode = InOperative.IsInStealthMode();
	const USkeletalMeshComponent* Source = InOperative.GetMesh();
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;
	const bool bCrouched = CrouchedOverride.IsSet() ? CrouchedOverride.GetValue()
		: bFireIdle ? InOperative.GetStance() == EOperativeStance::Crouching || InSlot.Height == ECoverHeight::LowCover
		: CoverRules::DefaultStanceFor(InSlot.Height) == EOperativeStance::Crouching;
	UAnimSequenceBase* Clip = nullptr;
	float WallYaw = 0.f;
	if (Asset)
	{
		if (const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Source->GetAnimInstance()))
		{
			if (bStealthMode)
			{
				const FName StealthClipName = bFireIdle
					? StealthModeRules::GetWallLeanClipName(Facing)
					: (Facing == ECoverFacing::Left ? FName(TEXT("Stealth_LeftWall_Idle_IP")) : FName(TEXT("Stealth_RightWall_Idle_IP")));
				Clip = Anim->GetStealthNamedClip(StealthClipName);
				if (Clip)
				{
					WallYaw = StealthModeRules::GetWallLeanYawOffset(Facing);
				}
			}
			if (!Clip)
			{
				// The corner peek preview: the corner fire stance (cvr_*_fire_idle, leaning out round the edge, rifle up).
				const TArray<TObjectPtr<UAnimSequenceBase>>& Idles = bFireIdle ? (bCrouched ? Anim->CoverCrouchFireIdle : Anim->CoverStandFireIdle)
					: (bCrouched ? Anim->CoverCrouchIdle : Anim->CoverStandIdle);
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
		}
	}

	if (bShown && !bMantlePose && Operative.Get() == &InOperative && AreSlotsEquivalent(Slot, InSlot)
		&& ShownFacing == Facing && bCornerPeekPose == bFireIdle
		&& bShownStealthMode == bStealthMode && bShownCrouched == bCrouched
		&& ShownClip == Clip)
	{
		return;
	}

	Operative = const_cast<AOperativeCharacter*>(&InOperative);
	Slot = InSlot;
	bShown = true;
	bCornerPeekPose = bFireIdle;
	bMantlePose = false;
	bShownStealthMode = bStealthMode;
	bShownCrouched = bCrouched;
	ShownFacing = Facing;
	// Back to the wall (actor yaw = wall normal); the side he would face only picks the _L/_R idle clip (CoverFacingRules).
	const float HalfHeight = InOperative.GetSimpleCollisionHalfHeight();
	SetActorLocationAndRotation(InSlot.WorldLocation + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, CoverFacingRules::FacingYaw(InSlot, Facing), 0.f));
	ApplyGhostBody(InOperative, Asset, Clip, WallYaw, CoverRules::DefaultStanceFor(InSlot.Height) == EOperativeStance::Crouching);
}

void ACoverGhostActor::ShowMantleFor(const AOperativeCharacter& InOperative, const FVector& LandingFeet, float Yaw, bool bCrouched, UAnimSequenceBase* ClimbClip,
	float ClipSeconds, const FVector& PoseLocation, FName AnchorBone, const FVector& AnchorTarget)
{
	const USkeletalMeshComponent* Source = InOperative.GetMesh();
	USkeletalMesh* Asset = Source ? Cast<USkeletalMesh>(Source->GetSkinnedAsset()) : nullptr;
	const UOperativeAnimInstance* Anim = Source ? Cast<UOperativeAnimInstance>(Source->GetAnimInstance()) : nullptr;
	// B17-2: the climb clip's frame (hands on the lip) when given; else the pose he ends the climb in, his locomotion idles (empty on the
	// operatives' anim instances - their ABP plays its own: the T-pose the designer saw).
	UAnimSequenceBase* Clip = ClimbClip ? ClimbClip : Anim ? (bCrouched ? Anim->Animations.CrouchIdle.Get() : Anim->Animations.Idle.Get()) : nullptr;
	const FVector Where = ClimbClip ? PoseLocation : LandingFeet + FVector(0.f, 0.f, InOperative.GetSimpleCollisionHalfHeight());
	if (bShown && bMantlePose && Operative.Get() == &InOperative && bShownCrouched == bCrouched && ShownClip == Clip
		&& FMath::IsNearlyEqual(ShownClipSeconds, ClipSeconds, 0.01f) && FVector::DistSquared(ShownPoseLocation, Where) <= 1.f
		&& FMath::IsNearlyEqual(static_cast<float>(GetActorRotation().Yaw), Yaw, 1.f))
	{
		return;
	}
	Operative = const_cast<AOperativeCharacter*>(&InOperative);
	Slot = FCoverSlot();
	Slot.WorldLocation = LandingFeet;
	bShown = true;
	bCornerPeekPose = false;
	bMantlePose = true;
	bShownStealthMode = InOperative.IsInStealthMode();
	bShownCrouched = bCrouched;
	ShownClipSeconds = ClipSeconds;
	ShownPoseLocation = Where;
	// The landing: his standing capsule's half height over it (the mesh's relative transform below is the same pair as his own); a climb
	// frame: where its hand sits on the lip.
	SetActorLocationAndRotation(Where, FRotator(0.f, Yaw, 0.f));
	ApplyGhostBody(InOperative, Asset, Clip, 0.f, bCrouched);
	if (ClimbClip && Body->GetAnimInstance())
	{
		// Frozen on that frame; the pose evaluated now (a ghost nobody renders yet - or a headless smoke - would keep the reference pose).
		// Held on the frame by a zero play rate, not Stop(): a stopped single-node body left the window silhouette undrawn (designer
		// 2026-11-22, WindowStairHoverSmoke shots); the pose is still evaluated now for the anchor below.
		Body->SetPosition(ClipSeconds, false);
		Body->SetPlayRate(0.f);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		Body->MarkRenderStateDirty();
		// The contact hand exactly on the lip: the posed hand measured (the clip's root lock decides how the frame sits on the mesh origin).
		if (!AnchorBone.IsNone() && Body->GetBoneIndex(AnchorBone) != INDEX_NONE)
		{
			AddActorWorldOffset(AnchorTarget - Body->GetSocketLocation(AnchorBone));
		}
	}
}

void ACoverGhostActor::ApplyGhostBody(const AOperativeCharacter& InOperative, USkeletalMesh* Asset, UAnimSequenceBase* Clip, float WallYaw, bool bCrouchedPlaceholder)
{
	const USkeletalMeshComponent* Source = InOperative.GetMesh();
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
	if (Asset && Source)
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
		ShownClip = Clip;
		if (Clip)
		{
			if (!FMath::IsNearlyZero(WallYaw))
			{
				FTransform Adjusted = Source->GetRelativeTransform();
				Adjusted.SetRotation((FQuat(FVector::UpVector, FMath::DegreesToRadians(WallYaw)) * Adjusted.GetRotation()).GetNormalized());
				Body->SetRelativeTransform(Adjusted);
			}
			Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Body->SetAnimation(Clip);
			Body->SetPlayRate(1.f); // a held climb frame (ShowMantleFor) set it to 0
			Body->Play(true);
		}
	}
	else
	{
		ShownClip = nullptr;
		Body->SetVisibility(false);
		Placeholder->SetVisibility(true);
		Placeholder->SetRelativeScale3D(FVector(0.7f, 0.7f, bCrouchedPlaceholder ? 1.2f : 1.8f));
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
	bMantlePose = false;
	bShownStealthMode = false;
	bShownCrouched = false;
	ShownClip = nullptr;
	SetActorHiddenInGame(true);
	if (Body)
	{
		Body->Stop();
	}
}
