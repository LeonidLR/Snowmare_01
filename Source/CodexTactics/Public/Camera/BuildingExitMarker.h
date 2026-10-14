#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingExitMarker.generated.h"

class ACameraZoneVolume;
class UArrowComponent;

/**
 * A hand-placed doorway of a building with a fixed interior camera (rev 5g item 5, user request 2026-10-14; UE-only). Place it in
 * the doorway with its arrow (X) pointing OUT of the building. While the zone's interior camera is on, an EXIT arrow is shown here;
 * a click sends the squad (Solo: the selected operative) ExitDistanceCm out through it. Any marker of a zone replaces the
 * automatic doorway search (UInteriorExitSubsystem) for that zone. A marker belongs to Zone, or - left empty - to the camera
 * zone whose box (grown by 2 m) contains it. Invisible in the game.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ABuildingExitMarker : public AActor
{
	GENERATED_BODY()

public:
	ABuildingExitMarker();

	/** The camera zone (building) this door belongs to; empty = the zone that contains the marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	TObjectPtr<ACameraZoneVolume> Zone;

	/** How far out of the door the exit walk ends, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera", meta = (ClampMin = "50"))
	float ExitDistanceCm = 200.f;

	/** The outward direction (the arrow, flattened). */
	FVector GetOutwardDirection() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UArrowComponent> Arrow;
};
