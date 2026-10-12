#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavRelevantComponent.h"
#include "VehicleNavigation.generated.h"

/**
 * Vehicle navigation (APC P0, docs/vehicles/APC_01_TECHNICAL.md §3). Config/DefaultEngine.ini declares two nav agents:
 * index 0 "Default" (the infantry, the engine's former implicit agent re-declared exactly) and index 1 "Vehicle" (radius
 * 200, height 300, bCanFly=True so no ground pawn's NavAgentProps ever picks it; the vehicle queries it by name).
 * Keep that order: the areas below address the agents by index. UE-only.
 */
namespace VehicleNavAgents
{
	/** SupportedAgents index of the infantry agent. */
	inline constexpr int32 InfantryIndex = 0;
	/** SupportedAgents index of the vehicle agent. */
	inline constexpr int32 VehicleIndex = 1;
	/** Name of the vehicle agent in SupportedAgents. */
	inline const FName VehicleAgentName(TEXT("Vehicle"));
	/** Actor tag of a NavMeshBoundsVolume that also builds the vehicle navmesh (see UVehicleSubsystem). */
	inline const FName VehicleNavTag(TEXT("VehicleNav"));
}

/**
 * "No vehicles here": unwalkable for the Vehicle agent only (thin ice, ditches, narrow alleys, gates). Place a
 * NavModifierVolume with this Area Class; the infantry navmesh ignores it. The vehicle's centre is kept out of the
 * volume, so make it ~2 m larger than the spot it guards (half the hull width).
 */
UCLASS(Config = Engine)
class CODEXTACTICS_API UNavArea_NoVehicle : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_NoVehicle(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/**
 * The parked vehicle's footprint on the infantry navmesh (unwalkable, Default agent only): operatives and enemies path
 * round the hull while the vehicle's own navmesh stays whole under it.
 */
UCLASS(Config = Engine)
class CODEXTACTICS_API UNavArea_VehicleHull : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_VehicleHull(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/**
 * One rotated box nav modifier (UNavArea_VehicleHull) over the hull, inflated by Margin, on the infantry navmesh. The
 * vehicle switches it off while it drives (no tile rebuild per frame) and back on when it stops (one rebuild).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UVehicleNavObstacleComponent : public UNavRelevantComponent
{
	GENERATED_BODY()

public:
	UVehicleNavObstacleComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Box in the owner's space (centre, half extent; yaw only). */
	void SetLocalBox(const FVector& Centre, const FVector& HalfExtent);

	/** On (parked) / off (driving); refreshes the navigation octree. */
	void SetObstacleActive(bool bActive);

	bool IsObstacleActive() const { return bObstacleActive; }

	/** World corners (4, at the box centre height) of the inflated footprint (smokes / debug). */
	TArray<FVector> GetWorldFootprint() const;

	/** Extra width round the hull (> the infantry agent radius, so capsules never brush the hull while pathing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	float MarginCm = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	TSubclassOf<UNavArea> AreaClass;

	virtual void CalcAndCacheBounds() const override;
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

private:
	FTransform GetBoxTransform() const;

	FVector LocalCentre = FVector::ZeroVector;
	FVector LocalHalfExtent = FVector(400.f, 160.f, 140.f);
	bool bObstacleActive = true;
};
