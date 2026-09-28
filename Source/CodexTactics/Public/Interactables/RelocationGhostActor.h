#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RelocationGhostActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Placement preview of an object being moved: a copy of its mesh that follows the cursor, cyan where the object
 * may go and red outside the allowed radius. No collision.
 * Godot reference: main.gd `_create_relocate_ghost_preview`, `_set_relocate_ghost_material_valid`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ARelocationGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ARelocationGhostActor();

	/** Copies the source mesh (asset + transform relative to the source actor). */
	void CopyFrom(const UStaticMeshComponent* SourceMesh, const AActor* SourceActor);

	void SetValid(bool bValid);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Relocation")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Godot colours: valid (0.2, 0.9, 1.0), invalid (1.0, 0.25, 0.25). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Relocation")
	FLinearColor ValidColor = FLinearColor::FromSRGBColor(FColor(51, 230, 255));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Relocation")
	FLinearColor InvalidColor = FLinearColor::FromSRGBColor(FColor(255, 64, 64));

	/** Material tinted through its "Color" vector parameter (a translucent hologram material can replace it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Relocation")
	TObjectPtr<UMaterialInterface> GhostBaseMaterial;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
};
