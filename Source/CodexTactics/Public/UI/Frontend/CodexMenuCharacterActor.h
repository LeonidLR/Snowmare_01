#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CodexMenuCharacterActor.generated.h"

class UAnimationAsset;
class USkeletalMeshComponent;

/**
 * A character standing in the menu scene, idling: a skeletal mesh that loops IdleAnimation (or runs the mesh
 * component's own Anim Blueprint when IdleAnimation is empty). Purely visual — no squad / AI / survival logic.
 * The artist sets the mesh, materials, animation and placement in L_MainMenu (or subclasses it as a Blueprint).
 * No Godot reference (2026-10-08 user decision: professional frontend).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACodexMenuCharacterActor : public AActor
{
	GENERATED_BODY()

public:
	ACodexMenuCharacterActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Frontend")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** Looping idle (sequence / blend space); empty = keep the mesh component's animation mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Frontend")
	TObjectPtr<UAnimationAsset> IdleAnimation;

protected:
	virtual void BeginPlay() override;
};
