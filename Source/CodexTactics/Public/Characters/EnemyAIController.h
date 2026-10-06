#pragma once

#include "CoreMinimal.h"
#include "DetourCrowdAIController.h"
#include "EnemyAIController.generated.h"

/**
 * AI controller for enemy characters with Detour Crowd avoidance.
 * Prevents swarming enemies from clipping through each other. While its enemy is held by the turn-based fight
 * (AEnemyCharacter::IsTurnBasedHeld) every navigation move is refused: the grid moves its units itself, and an AI move
 * issued from an event (a hit, a timer) would walk a frozen enemy off the grid (bug fix 2026-10-06).
 */
UCLASS()
class CODEXTACTICS_API AEnemyAIController : public ADetourCrowdAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Refuses the move (Failed) while the pawn is held by the turn-based fight; otherwise the normal move. */
	virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath = nullptr) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
