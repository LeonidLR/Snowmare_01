#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FormationGhostActor.generated.h"

class AOperativeCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;

/** Where a placement ghost's pose comes from (smokes / logs). */
UENUM()
enum class EFormationGhostPose : uint8
{
	/** Not shown, or no pose could be set (the reference pose - the bug_05 T-pose). */
	None,
	/** The operative's idle clip (or the ready clip ReadyPoseClip), held at one frame (single node, paused). */
	IdleClip,
	/** The operative's own current pose, followed (leader pose) - the fallback when the idle clip is missing or left the reference pose. */
	OperativePose
};

/**
 * The custom formation's placement ghost (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md "Custom formation"): a holographic
 * copy of one operative's mesh standing at his placed spot, facing his look, in a natural standing pose. Godot reference: none (UE-only).
 * Bug_05 (the game designer's PIE 2026-11-01): the ghosts stood in the T-pose and pale - now the pose is the operative's idle clip held at one
 * frame and evaluated at once (single node, paused; the placement pause stops the world clock, so nothing waits for a tick), checked against the
 * reference pose (the fallback follows the operative's own pose); the look is the dedicated hologram M_FormationGhost (unlit, translucent,
 * exposure-invariant, a bright fresnel rim and rising scanlines, skeletal-mesh usage; Scripts/Editor/create_formation_ghost_material.py) on every
 * slot, plus the operative's see-through silhouette as the overlay (seen behind a wall). The tutorial markers' hologram orange; the selected ghost
 * is brighter yellow, a refused spot flashes red. No collision (clicks pass through to the ground). The look arrow is drawn by the HUD.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AFormationGhostActor : public AActor
{
	GENERATED_BODY()

public:
	AFormationGhostActor();

	/** Copies Operative's mesh and a natural pose (his idle, held); stands at Feet (the ground point) looking Yaw. */
	void ShowFor(const AOperativeCharacter& Operative, const FVector& Feet, float Yaw);
	/** Moves the shown ghost (feet on the ground, facing Yaw). */
	void SetPlacement(const FVector& Feet, float Yaw);
	/** The selected operative's ghost glows brighter. */
	void SetSelected(bool bSelected);
	/** A refused placement: the ghost flashes red for Seconds of real time (TickFlash). */
	void FlashRefused(float Seconds = 0.6f);
	/** Real-time flash countdown (the placement pause stops the world clock). */
	void TickFlash(float RealDeltaSeconds);
	void Hide();

	bool IsShown() const { return bShown; }
	USkeletalMeshComponent* GetBody() const { return Body; }
	/** Where the ghost's feet stand. */
	FVector GetFeet() const { return Feet; }
	float GetYaw() const { return GhostYaw; }
	/** Where the shown pose comes from (None = the reference pose). */
	EFormationGhostPose GetPoseSource() const { return PoseSource; }
	/** The largest bone rotation of the shown pose away from the mesh's reference pose, deg (0 = the T-pose). Measured by ShowFor. */
	float GetPoseOffRefDeg() const { return PoseOffRefDeg; }
	/** The hologram material on the body (smokes: the dedicated one, not the pale silhouette). */
	const UMaterialInterface* GetHologramMaterial() const;

	/** Hologram tint (the tutorial markers' MI_HoloMarker_Orange orange), normal / selected / refused. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor HoloColor = FLinearColor(1.f, 0.38f, 0.04f, 1.f);
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor SelectedColor = FLinearColor(1.f, 0.85f, 0.3f, 1.f);
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor RefusedColor = FLinearColor(1.f, 0.08f, 0.05f, 1.f);
	/** Emissive strength of the hologram, normal / selected (M_FormationGhost "EmissiveIntensity"). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	float Intensity = 1.6f;
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	float SelectedIntensity = 2.4f;
	/**
	 * The standing ready clip when the operative's ABP has no baseline Idle (ABP_Operative plays its idle from its graph): the Rifle_2 idle loop
	 * (its skeleton is made compatible with the operatives' by Scripts/Editor/setup_operative_rifle2_animation.py).
	 */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	TSoftObjectPtr<class UAnimSequenceBase> ReadyPoseClip = TSoftObjectPtr<class UAnimSequenceBase>(
		FSoftObjectPath(TEXT("/Game/RifleAnims/Rifle_2/Idle/M_Neutral_Stand_Idle_Loop_Rifle.M_Neutral_Stand_Idle_Loop_Rifle")));
	/** The idle clip is held at this fraction of its length (a settled ready stance, not the clip's blend-in start). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation", meta = (ClampMin = "0", ClampMax = "1"))
	float IdlePoseFraction = 0.35f;

private:
	void ApplyColor();
	/** Sets the pose (the idle clip held, else the operative's own) and measures it against the reference pose. */
	void ApplyPose(const AOperativeCharacter& Operative, USkeletalMeshComponent& Source);
	/** The largest bone rotation of Pose away from the reference pose of Body's mesh, deg. */
	float OffRefDeg(TConstArrayView<FTransform> Pose) const;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Formation")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Formation")
	TObjectPtr<USkeletalMeshComponent> Body;
	/** The dedicated hologram (/Game/UI/Materials/M_FormationGhost); null when the asset is missing (then the silhouette carries the look). */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> HologramMaterial;
	/** Fallback see-through silhouette (/Game/VFX/Materials/M_Silhouette) when the operative has no SilhouetteMaterial. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Overlay;

	FVector Feet = FVector::ZeroVector;
	float GhostYaw = 0.f;
	float HalfHeight = 90.f;
	bool bShown = false;
	bool bSelected = false;
	float FlashRemaining = 0.f;
	EFormationGhostPose PoseSource = EFormationGhostPose::None;
	float PoseOffRefDeg = 0.f;
	FString PoseClipName;
};
