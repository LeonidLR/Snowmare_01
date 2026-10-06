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
 * spec): a copy of the operative's skeletal mesh in the see-through hologram material (M_GhostHologram, the
 * relocation ghost's; cyan) pressed back-to-wall at the slot, playing the cover idle clip of the slot's height (or the
 * plain idle while no cover clips are set). Shown by the first click on a wall, confirmed by the second, hidden by any
 * other click, RMB or Esc. Without a skeletal mesh the placeholder cylinder stands in.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ACoverGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ACoverGhostActor();

	/** Shows the ghost of Operative at Slot, facing away from the wall, in the cover idle of the slot's height. */
	void ShowFor(const AOperativeCharacter& Operative, const FCoverSlot& Slot);

	void Hide();

	bool IsShown() const { return bShown; }
	const FCoverSlot& GetSlot() const { return Slot; }
	AOperativeCharacter* GetOperative() const { return Operative.Get(); }

	/** Godot ghost colour family: cyan preview (0.2, 0.9, 1.0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover")
	FLinearColor GhostColor = FLinearColor::FromSRGBColor(FColor(51, 230, 255));

	/** Hologram material with a "Color" vector parameter (M_GhostHologram). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Cover")
	TObjectPtr<UMaterialInterface> GhostMaterial;

private:
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Cover")
	TObjectPtr<UStaticMeshComponent> Placeholder;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	TWeakObjectPtr<AOperativeCharacter> Operative;
	FCoverSlot Slot;
	bool bShown = false;
};
