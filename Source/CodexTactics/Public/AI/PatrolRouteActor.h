#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PatrolRouteActor.generated.h"

class USceneComponent;
class USplineComponent;

/**
 * Designer-placed patrol route (Sprint 11-A; no Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md
 * «SPRINT 11 DIRECTIVE»). Every spline point is a waypoint; a patroller (AEnemyCharacter::AssignedPatrolRoute) walks
 * straight from point to point over the navmesh, pauses at each and turns towards the next segment. The spline is drawn
 * linear (that is how it is walked) and closed while bIsLoop. Index math: PatrolRouteRules (CodexTactics.AI.PatrolRoute.*).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API APatrolRouteActor : public AActor
{
	GENERATED_BODY()

public:
	APatrolRouteActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** 0 -> 1 -> 2 -> 0 ... (takes precedence over bPingPong). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	bool bIsLoop = true;

	/** Without bIsLoop: 0 -> 1 -> 2 -> 1 -> 0 ...; neither flag: the patroller stops at the last point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	bool bPingPong = false;

	/** Pause at a waypoint, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0"))
	float DefaultWaitTimeSeconds = 3.f;

	/** Per-waypoint pauses, s; a missing entry or a value <= 0 uses DefaultWaitTimeSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	TArray<float> PerPointWaitTime;

	/** Number of waypoints (spline points). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	int32 GetNumberOfWaypoints() const;

	/** World location of waypoint Index (the actor location for an invalid index). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	FVector GetWaypointWorldLocation(int32 Index) const;

	/**
	 * The waypoint after CurrentIndex (see bIsLoop / bPingPong); bInOutForward is the ping-pong direction. INDEX_NONE:
	 * the route ends here (neither loop nor ping-pong) or it has no points.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Patrol")
	int32 GetNextWaypointIndex(int32 CurrentIndex, UPARAM(ref) bool& bInOutForward) const;

	/** Pause at waypoint Index, s. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	float GetWaitTimeAtWaypoint(int32 Index) const;

	USplineComponent* GetRouteSpline() const { return RouteSpline; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Patrol")
	TObjectPtr<USceneComponent> SceneRoot;

	/** The route; add / move points in the level viewport (Alt-drag a point to add one). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Patrol")
	TObjectPtr<USplineComponent> RouteSpline;
};
