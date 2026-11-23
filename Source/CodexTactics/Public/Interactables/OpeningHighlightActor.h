// Designer 2026-11-23: highlight the hovered opening itself. Godot reference: none (UE-only).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OpeningHighlightActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/** A collision-free glow sized to a window opening or door leaf. */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API AOpeningHighlightActor : public AActor
{
	GENERATED_BODY()

public:
	AOpeningHighlightActor();

	/** Place the box at WorldBox's centre, with its side lengths rotated by Rotation. */
	void ShowBox(const FBox& WorldBox, const FQuat& Rotation, const FLinearColor& Color);
	void Hide();
	bool IsShown() const;

	/** Read the visible material's colour for the opening hover smoke. */
	bool GetShownColor(FLinearColor& OutColor) const;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Glow;
};
