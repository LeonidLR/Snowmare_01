#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tactics/DoorwayBreachRules.h"
#include "DoorwayBreachGhostActor.generated.h"

class AOperativeCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Holographic preview actor displaying the 3-man (or 4-man) doorway breach triad.
 * Step 3 of Tactical Doorway Breach (docs/design/DOORWAY_BREACH.md).
 * Displays translucent silhouettes at Left Jamb, Right Jamb, and Center Point.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ADoorwayBreachGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ADoorwayBreachGhostActor();

	/**
	 * Displays breach ghost triad for the given operatives.
	 * Assigns operatives via minimal travel distance automatically.
	 */
	void ShowBreachPreview(
		const FDoorwayBreachTriad& InTriad,
		const TArray<AOperativeCharacter*>& InOperatives,
		bool bValid = true);

	/**
	 * Displays breach ghost triad with explicit slot assignments.
	 */
	void ShowBreachPreviewForAssignments(
		const FDoorwayBreachTriad& InTriad,
		const TArray<FDoorwaySlotAssignment>& InAssignments,
		const TArray<AOperativeCharacter*>& InOperatives,
		bool bValid = true);

	/** Hides all ghost silhouettes. */
	void HideBreachPreview();

	/** True if ghost preview is currently visible. */
	bool IsPreviewShown() const { return bShown; }

	/** Current doorway breach triad being previewed. */
	const FDoorwayBreachTriad& GetCurrentTriad() const { return Triad; }

	/** Number of ghost operatives currently rendered in preview. */
	int32 GetVisibleGhostCount() const;

	/** Fallback translucent material (M_Silhouette). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|DoorwayBreach")
	TObjectPtr<UMaterialInterface> GhostMaterial;

private:
	struct FSlotVisual
	{
		TObjectPtr<USkeletalMeshComponent> Body;
		TObjectPtr<UStaticMeshComponent> Placeholder;
		TObjectPtr<UMaterialInstanceDynamic> Material;
		bool bActive = false;
	};

	void SetupSlotVisual(FSlotVisual& Visual, const FName& BodyName, const FName& PlaceholderName);
	void UpdateSlotVisual(
		FSlotVisual& Visual,
		const FDoorwayBreachSlot& Slot,
		const AOperativeCharacter* Operative,
		const FLinearColor& Color);
	void HideSlotVisual(FSlotVisual& Visual);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|DoorwayBreach")
	TObjectPtr<USceneComponent> Root;

	FSlotVisual LeftVisual;
	FSlotVisual RightVisual;
	FSlotVisual CenterVisual;
	FSlotVisual RearVisual;

	FDoorwayBreachTriad Triad;
	bool bShown = false;
};
