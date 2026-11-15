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

/** Visual components and dynamic material state for a breach ghost slot. */
USTRUCT()
struct FDoorwayBreachSlotVisual
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Body = nullptr;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Placeholder = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;

	UPROPERTY()
	bool bActive = false;
};

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
	using FSlotVisual = FDoorwayBreachSlotVisual;

	void SetupSlotVisual(FDoorwayBreachSlotVisual& Visual, const FName& BodyName, const FName& PlaceholderName);
	void UpdateSlotVisual(
		FDoorwayBreachSlotVisual& Visual,
		const FDoorwayBreachSlot& Slot,
		const AOperativeCharacter* Operative,
		const FLinearColor& Color);
	void HideSlotVisual(FDoorwayBreachSlotVisual& Visual);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|DoorwayBreach")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	FDoorwayBreachSlotVisual LeftVisual;

	UPROPERTY()
	FDoorwayBreachSlotVisual RightVisual;

	UPROPERTY()
	FDoorwayBreachSlotVisual CenterVisual;

	UPROPERTY()
	FDoorwayBreachSlotVisual RearVisual;

	FDoorwayBreachTriad Triad;
	bool bShown = false;
};
