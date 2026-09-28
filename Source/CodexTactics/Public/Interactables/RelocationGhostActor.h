#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RelocationGhostActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Placement preview: a copy of an object's mesh that follows the cursor, tinted by whether the spot is allowed
 * (moving an object: cyan / red; setting up a deployable: green / red). No collision.
 * Godot reference: main.gd `_create_relocate_ghost_preview`, `_set_relocate_ghost_material_valid`,
 * `_create_ghost_preview`, `_set_ghost_material_valid`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ARelocationGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ARelocationGhostActor();

	/** Copies a placed object's mesh (asset + transform relative to the object). */
	void CopyFrom(const UStaticMeshComponent* SourceMesh, const AActor* SourceActor);

	/** Copies a class-default mesh (relative transform of the template component). */
	void CopyFromTemplate(const UStaticMeshComponent* TemplateMesh);

	void SetColors(const FLinearColor& Valid, const FLinearColor& Invalid);

	void SetValid(bool bValid);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Relocation")
	TObjectPtr<USceneComponent> Root;

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
	void ApplyMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	bool bLastValid = true;
};
