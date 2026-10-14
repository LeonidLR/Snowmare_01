#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteriorExitArrowActor.generated.h"

class ACameraZoneVolume;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The clickable "EXIT" arrow at a doorway, spawned by UInteriorExitSubsystem while a building's fixed interior camera is the view
 * (rev 5g item 5, user request 2026-10-14; UE-only). A flat amber arrow on the floor pointing out of the door + an "EXIT" label;
 * its click box blocks only the Visibility channel (the cursor trace), nothing physical. Hover: brighter, label "EXIT - click",
 * hand cursor (ACodexTacticsPlayerController). A click -> UInteriorExitSubsystem::OrderExit.
 * Meant to be restyled by a Blueprint subclass (UInteriorExitSubsystem::ArrowClass is not exposed yet: engine shapes for now).
 */
UCLASS()
class CODEXTACTICS_API AInteriorExitArrowActor : public AActor
{
	GENERATED_BODY()

public:
	AInteriorExitArrowActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Places the arrow at DoorPoint pointing along Outward (flattened); ExitTarget = where the walk out ends. */
	void Setup(ACameraZoneVolume* InZone, const FVector& DoorPoint, const FVector& Outward, const FVector& InExitTarget);

	void SetHovered(bool bHovered);
	bool IsHovered() const { return bIsHovered; }

	ACameraZoneVolume* GetZone() const { return Zone.Get(); }
	const FVector& GetExitTarget() const { return ExitTarget; }
	const FVector& GetOutward() const { return OutwardDirection; }

private:
	void ApplyColor();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UBoxComponent> ClickBox;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UStaticMeshComponent> Shaft;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	TWeakObjectPtr<ACameraZoneVolume> Zone;
	FVector ExitTarget = FVector::ZeroVector;
	FVector OutwardDirection = FVector::ForwardVector;
	bool bIsHovered = false;
	float PulseTime = 0.f;
};
