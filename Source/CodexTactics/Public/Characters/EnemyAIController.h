#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "EnemyAIController.generated.h"

/**
 * AI controller for enemy characters with Detour Crowd avoidance.
 * Prevents swarming enemies from clipping through each other.
 */
UCLASS()
class CODEXTACTICS_API AEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
