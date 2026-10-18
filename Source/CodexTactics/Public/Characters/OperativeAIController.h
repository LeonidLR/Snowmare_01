#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "OperativeAIController.generated.h"

/**
 * Drives one operative over the NavMesh with Detour Crowd avoidance (keeps squad members apart,
 * replacing Godot's separation force and whisker steering in player.gd). Path following:
 * UOperativeCrowdFollowingComponent (no false "moved too far" finish of a fresh order, bug hunt 2026-10-18).
 */
UCLASS()
class CODEXTACTICS_API AOperativeAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	AOperativeAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
