#include "AI/PatrolRouteActor.h"

#include "AI/PatrolRouteRules.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"

APatrolRouteActor::APatrolRouteActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	RouteSpline->SetupAttachment(SceneRoot);
	// A small triangle to start from (the designer drags / adds points in the level).
	RouteSpline->ClearSplinePoints(false);
	RouteSpline->AddSplinePoint(FVector(0.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
	RouteSpline->AddSplinePoint(FVector(600.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
	RouteSpline->AddSplinePoint(FVector(300.f, 500.f, 0.f), ESplineCoordinateSpace::Local, false);
	RouteSpline->SetClosedLoop(true, false);
	RouteSpline->UpdateSpline();
#if WITH_EDITORONLY_DATA
	RouteSpline->bShouldVisualizeScale = false;
	RouteSpline->EditorUnselectedSplineSegmentColor = FLinearColor(1.f, 0.55f, 0.1f);
	RouteSpline->EditorSelectedSplineSegmentColor = FLinearColor(1.f, 0.85f, 0.2f);
#endif
	// Visible in PIE too with the «Splines» show flag (patrol debugging).
	RouteSpline->SetDrawDebug(true);
}

void APatrolRouteActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!RouteSpline)
	{
		return;
	}
	// The patroller walks straight lines between the points: draw them that way, closed while it loops.
	for (int32 Index = 0; Index < RouteSpline->GetNumberOfSplinePoints(); ++Index)
	{
		RouteSpline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}
	RouteSpline->SetClosedLoop(bIsLoop, false);
	RouteSpline->UpdateSpline();
}

int32 APatrolRouteActor::GetNumberOfWaypoints() const
{
	return RouteSpline ? RouteSpline->GetNumberOfSplinePoints() : 0;
}

FVector APatrolRouteActor::GetWaypointWorldLocation(int32 Index) const
{
	if (!RouteSpline || Index < 0 || Index >= RouteSpline->GetNumberOfSplinePoints())
	{
		return GetActorLocation();
	}
	return RouteSpline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World);
}

int32 APatrolRouteActor::GetNextWaypointIndex(int32 CurrentIndex, bool& bInOutForward) const
{
	return PatrolRouteRules::GetNextWaypointIndex(GetNumberOfWaypoints(), CurrentIndex, bIsLoop, bPingPong, bInOutForward);
}

float APatrolRouteActor::GetWaitTimeAtWaypoint(int32 Index) const
{
	return PatrolRouteRules::GetWaitTime(PerPointWaitTime, Index, DefaultWaitTimeSeconds);
}
