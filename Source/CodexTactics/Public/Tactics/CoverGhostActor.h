#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tactics/CoverTypes.h"
#include "CoverGhostActor.generated.h"

class AOperativeCharacter;
class UAnimSequenceBase;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Holographic preview of an operative at a cover slot (Sprint 12-C; UE-only, no Godot reference — Gemini Sprint 12
 * spec): a copy of the operative's skeletal mesh pressed back-to-wall at the slot, facing along the wall towards the
 * side he would face there (CoverFacingRules), playing the cover idle clip of the slot's height (or the plain idle while
 * no cover clips are set). Look (user report 2026-10-06): exactly the operatives' see-through silhouette technique
 * (AOperativeCharacter::SetSilhouetteVisible) — an OverlayMaterial MID of the operative's SilhouetteMaterial
 * (/Game/VFX/Materials/M_Silhouette: translucent, unlit, depth test off; no custom depth / stencil) with his silhouette
 * colour; the same MID replaces the base materials so no opaque body shows. Shown by the first click on a wall,
 * confirmed by the second, hidden by any other click, RMB or Esc. Without a skeletal mesh the placeholder cylinder
 * stands in.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ACoverGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ACoverGhostActor();

	/** Shows the ghost of Operative at Slot, facing away from the wall, in the cover idle of the slot's height. */
	void ShowFor(const AOperativeCharacter& Operative, const FCoverSlot& Slot);

	/**
	 * The ghost at Slot in the cover idle of Facing's side - the arrival pose of an order that decides the side itself (the corner switch's
	 * mirrored pose, the end of a wrap round the object; 2026-10-20).
	 */
	void ShowForFacing(const AOperativeCharacter& Operative, const FCoverSlot& Slot, ECoverFacing Facing);

	/** The side the shown pose faces (smokes). */
	ECoverFacing GetShownFacing() const { return ShownFacing; }

	/**
	 * The manual corner peek preview (rev 5c, hovering the edge of his own wall): the ghost at the corner stand point Slot in
	 * the corner-aim pose of Side (cvr_*_fire_idle: leaning out, rifle up) - distinct from the cover-entry ghost. The pose's stance is his
	 * current one (low cover: crouched); CrouchedOverride sets it instead (the squad's peeker who is not in cover yet: the stance the cover
	 * entry will give him, 2026-10-26).
	 */
	void ShowCornerPeekFor(const AOperativeCharacter& Operative, const FCoverSlot& Slot, ECoverFacing Side, TOptional<bool> CrouchedOverride = TOptional<bool>());

	/** The ghost shows a corner peek pose (ShowCornerPeekFor) rather than a cover entry. */
	bool IsCornerPeekPose() const { return bShown && bCornerPeekPose; }

	/**
	 * The mantle preview (batch 2, docs/port/MANTLING_BATCH2_REPORT.md: hovering a climbable obstacle the selected operative can climb): the
	 * same hologram at the climb's landing (feet), facing Yaw (the climb's direction), in the pose he ends in there - his crouch idle on a top,
	 * his idle past a fence. Slot.WorldLocation = the landing.
	 * B17-2 (designer option A, 2026-11-17): with ClimbClip the ghost instead stands at PoseLocation (the actor; AOperativeCharacter::
	 * PlanMantleGhostPose) frozen in that clip's frame at ClipSeconds - hands on the lip, a leg up - not in the end pose on top; once posed it
	 * is shifted so AnchorBone (the contact hand) sits exactly on AnchorTarget (the pose is measured, whatever the clip's root lock does).
	 */
	void ShowMantleFor(const AOperativeCharacter& Operative, const FVector& LandingFeet, float Yaw, bool bCrouched, UAnimSequenceBase* ClimbClip = nullptr,
		float ClipSeconds = 0.f, const FVector& PoseLocation = FVector::ZeroVector, FName AnchorBone = NAME_None, const FVector& AnchorTarget = FVector::ZeroVector);

	/** The ghost shows a mantle landing (ShowMantleFor). */
	bool IsMantlePose() const { return bShown && bMantlePose; }

	void Hide();

	bool IsShown() const { return bShown; }
	const FCoverSlot& GetSlot() const { return Slot; }
	AOperativeCharacter* GetOperative() const { return Operative.Get(); }

	/** Fallback see-through material with a "Color" vector parameter when the operative sets none (M_Silhouette). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	/** The ghost's body (smokes: material / overlay checks). */
	USkeletalMeshComponent* GetBody() const { return Body; }

	/** The silhouette MID applied to the body (overlay and base slots). */
	UMaterialInstanceDynamic* GetGhostMaterialInstance() const { return Material; }

	/** The clip last selected by ShowPose (cover idle / corner lean; smokes). */
	const UAnimSequenceBase* GetShownClip() const { return ShownClip.Get(); }

private:
	UMaterialInterface* ResolveBaseMaterial(const AOperativeCharacter& InOperative) const;
	/** ShowFor / ShowCornerPeekFor: the ghost at Slot facing Facing, in the cover idle or (bFireIdle) the corner fire stance. */
	void ShowPose(const AOperativeCharacter& InOperative, const FCoverSlot& InSlot, ECoverFacing Facing, bool bFireIdle,
		TOptional<bool> CrouchedOverride = TOptional<bool>());
	/** The hologram's body: the operative's mesh with the silhouette MID playing Clip (WallYaw turns it), else the placeholder cylinder. */
	void ApplyGhostBody(const AOperativeCharacter& InOperative, class USkeletalMesh* Asset, UAnimSequenceBase* Clip, float WallYaw, bool bCrouchedPlaceholder);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<UStaticMeshComponent> Placeholder;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> ShownClip;

	TWeakObjectPtr<AOperativeCharacter> Operative;
	FCoverSlot Slot;
	bool bShown = false;
	bool bCornerPeekPose = false;
	bool bMantlePose = false;
	bool bShownStealthMode = false;
	bool bShownCrouched = false;
	/** The frozen climb frame shown by ShowMantleFor (B17-2), s. */
	float ShownClipSeconds = 0.f;
	/** Where ShowMantleFor was asked to put the ghost (before the anchor shift). */
	FVector ShownPoseLocation = FVector::ZeroVector;
	ECoverFacing ShownFacing = ECoverFacing::Right;
};
