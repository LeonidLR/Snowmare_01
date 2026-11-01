#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FormationGhostActor.generated.h"

class AOperativeCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;

/**
 * The custom formation's placement ghost (game designer decisions 2026-11-01; docs/design/FORMATION_SECTORS.md "Custom formation"): a holographic
 * copy of one operative's mesh standing at his placed spot, facing his look, in his plain idle. Godot reference: none (UE-only). The same hologram
 * technique as the cover ghost (ACoverGhostActor: the operative's see-through silhouette material on every slot + as the overlay; M_HoloMarker has
 * no skeletal-mesh usage, so the silhouette material carries the hologram), tinted the tutorial markers' hologram orange; the selected ghost is
 * brighter, a refused spot flashes red. No collision (clicks pass through to the ground). The look arrow is drawn by the HUD (ACodexTacticsHUD).
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AFormationGhostActor : public AActor
{
	GENERATED_BODY()

public:
	AFormationGhostActor();

	/** Copies Operative's mesh and idle; stands at Feet (the ground point) looking Yaw. */
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

	/** Hologram tint (the tutorial markers' MI_HoloMarker_Orange orange), normal / selected / refused. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor HoloColor = FLinearColor(1.f, 0.45f, 0.08f, 1.f);
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor SelectedColor = FLinearColor(1.f, 0.85f, 0.35f, 1.f);
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Formation")
	FLinearColor RefusedColor = FLinearColor(1.f, 0.1f, 0.08f, 1.f);

private:
	void ApplyColor();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Formation")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Formation")
	TObjectPtr<USkeletalMeshComponent> Body;
	/** Fallback hologram (/Game/VFX/Materials/M_Silhouette) when the operative has no SilhouetteMaterial. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	FVector Feet = FVector::ZeroVector;
	float GhostYaw = 0.f;
	float HalfHeight = 90.f;
	bool bShown = false;
	bool bSelected = false;
	float FlashRemaining = 0.f;
};
