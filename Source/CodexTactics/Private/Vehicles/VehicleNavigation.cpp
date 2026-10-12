#include "Vehicles/VehicleNavigation.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "GameFramework/Actor.h"

UNavArea_NoVehicle::UNavArea_NoVehicle(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = BIG_NUMBER;
	AreaFlags = 0; // unwalkable
	DrawColor = FColor(200, 60, 20);
	SupportedAgents = FNavAgentSelector(1u << VehicleNavAgents::VehicleIndex);
	SupportedAgents.MarkInitialized();
}

UNavArea_VehicleHull::UNavArea_VehicleHull(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DefaultCost = BIG_NUMBER;
	AreaFlags = 0; // unwalkable
	DrawColor = FColor(40, 40, 40);
	SupportedAgents = FNavAgentSelector(1u << VehicleNavAgents::InfantryIndex);
	SupportedAgents.MarkInitialized();
}

UVehicleNavObstacleComponent::UVehicleNavObstacleComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	AreaClass = UNavArea_VehicleHull::StaticClass();
	bAttachToOwnersRoot = false; // its own octree entry: the owner's root is not nav-relevant
}

void UVehicleNavObstacleComponent::SetLocalBox(const FVector& Centre, const FVector& HalfExtent)
{
	LocalCentre = Centre;
	LocalHalfExtent = HalfExtent;
	bBoundsInitialized = false;
}

void UVehicleNavObstacleComponent::SetObstacleActive(bool bActive)
{
	bObstacleActive = bActive;
	// The owner moved since the last refresh: recompute the bounds before the octree reads them.
	bBoundsInitialized = false;
	if (IsNavigationRelevant() != bActive)
	{
		SetNavigationRelevancy(bActive);
	}
	else if (bActive)
	{
		RefreshNavigationModifiers();
	}
}

FTransform UVehicleNavObstacleComponent::GetBoxTransform() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FTransform::Identity;
	}
	// Yaw only: the footprint on the navmesh stays flat on slopes.
	const FRotator Yaw(0.f, Owner->GetActorRotation().Yaw, 0.f);
	return FTransform(Yaw, Owner->GetActorLocation());
}

TArray<FVector> UVehicleNavObstacleComponent::GetWorldFootprint() const
{
	const FTransform Box = GetBoxTransform();
	const FVector Half = LocalHalfExtent + FVector(MarginCm, MarginCm, 0.f);
	TArray<FVector> Corners;
	for (const FVector2D Sign : { FVector2D(1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f), FVector2D(-1.f, 1.f) })
	{
		Corners.Add(Box.TransformPosition(LocalCentre + FVector(Half.X * Sign.X, Half.Y * Sign.Y, 0.f)));
	}
	return Corners;
}

void UVehicleNavObstacleComponent::CalcAndCacheBounds() const
{
	const FTransform Box = GetBoxTransform();
	const FVector Half = LocalHalfExtent + FVector(MarginCm, MarginCm, 0.f);
	Bounds = FBox(LocalCentre - Half, LocalCentre + Half).TransformBy(Box);
}

void UVehicleNavObstacleComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	if (!bObstacleActive || !AreaClass)
	{
		return;
	}
	const FVector Half = LocalHalfExtent + FVector(MarginCm, MarginCm, 0.f);
	Data.Modifiers.Add(FAreaNavModifier(FBox(LocalCentre - Half, LocalCentre + Half), GetBoxTransform(), AreaClass));
}
