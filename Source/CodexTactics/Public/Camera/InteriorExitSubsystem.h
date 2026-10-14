#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Camera/InteriorExitRules.h"
#include "InteriorExitSubsystem.generated.h"

class ACameraZoneVolume;
class AInteriorExitArrowActor;

/** One doorway of a building (a camera zone). */
struct FInteriorExit
{
	/** On the zone's edge (automatic) or the marker's location, at floor height. */
	FVector DoorPoint = FVector::ZeroVector;
	/** Out of the building (2D). */
	FVector Outward = FVector::ForwardVector;
	/** Where the walk out ends (on the navmesh). */
	FVector ExitTarget = FVector::ZeroVector;
	float WidthCm = 0.f;
	bool bFromMarker = false;
};

/**
 * Leaving a building with a fixed interior camera (rev 5g item 5, user request 2026-10-14; UE-only). With all three inside, the
 * interior camera made it hard to click outside. While a camera zone shows its fixed camera, an EXIT arrow
 * (AInteriorExitArrowActor) stands at each doorway (placed ABuildingExitMarker actors of that zone, else the doorways found where
 * the infantry navmesh crosses the zone's box: InteriorExitRules); a click sends the squad - in Solo the selected operative(s) -
 * ~2 m out of that door. The M key (ToggleInteriorView) switches the interior camera <-> the normal top-down view while the
 * leader is inside (ACameraZoneVolume::TogglePlayerOutsideView; reset when he leaves).
 */
UCLASS()
class CODEXTACTICS_API UInteriorExitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;

	/** The doorways of Zone: its placed markers, else the automatic search (at the height of FloorHint). */
	TArray<FInteriorExit> FindExits(ACameraZoneVolume* Zone, const FVector& FloorHint) const;

	/** Sends the squad (Solo: the selected operatives) out through Arrow's door; returns how many were ordered. */
	int32 OrderExit(AInteriorExitArrowActor* Arrow);

	/** M: the camera zone the leader is in switches its view; false when there is none. OutOutside = the new state. */
	bool ToggleInteriorView(bool& bOutOutside);

	/** The zone the leader is in right now (its camera switch on), or null. */
	ACameraZoneVolume* GetLeaderZone() const;

	/** The zone whose interior camera is the view right now, or null. */
	ACameraZoneVolume* GetShownZone() const { return ShownZone.Get(); }

	/** The EXIT arrows on screen now. */
	TArray<AInteriorExitArrowActor*> GetArrows() const;

	/** The arrow under the cursor (hover look); null clears it. */
	void SetHoveredArrow(AInteriorExitArrowActor* Arrow);

	InteriorExitRules::FExitScanConfig ScanConfig;

private:
	void ShowArrows(ACameraZoneVolume* Zone);
	void ClearArrows();

	TWeakObjectPtr<ACameraZoneVolume> ShownZone;
	TArray<TWeakObjectPtr<AInteriorExitArrowActor>> Arrows;
};
