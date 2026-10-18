// UE-only (no Godot reference): the operatives' Detour crowd path following without the false "moved too far" finish.
// Bug hunt 2026-10-18 (HordeIntroSmoke "the planned move runs", ~1 run in 5).

#pragma once

#include "CoreMinimal.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "OperativeCrowdFollowingComponent.generated.h"

/**
 * UCrowdFollowingComponent finishes a move as Success when, on the last path segment, the crowd agent's move direction
 * points away from the goal (its overshoot test "moved too far"). For a short straight order that test is armed from
 * the first update: a stale direction from the previous walk, or an avoidance velocity pushing the operative away from a
 * hound standing on his path, ended a fresh 5 m order at once with 0 cm moved - the player's order silently dropped in a
 * melee scrum. Here the overshoot test only runs near the path's end (OperativeMovementRules::AllowsCrowdOvershootFinish,
 * where an overshoot can happen); farther away the move goes on (blocked / repath handling stays the engine's).
 */
UCLASS()
class CODEXTACTICS_API UOperativeCrowdFollowingComponent : public UCrowdFollowingComponent
{
	GENERATED_BODY()

public:
	UOperativeCrowdFollowingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ApplyCrowdAgentVelocity(const FVector& NewVelocity, const FVector& DestPathCorner, bool bTraversingLink, bool bIsNearEndOfPath) override;

protected:
	virtual void SetMoveSegment(int32 SegmentStartIndex) override;

private:
	/** Disarms the overshoot test while the agent is not near the path's end (AllowsCrowdOvershootFinish). */
	void LimitOvershootTest();
};
