#include "Characters/OperativeAIController.h"
#include "Characters/OperativeCharacter.h"
#include "Navigation/CrowdFollowingComponent.h"

AOperativeAIController::AOperativeAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSetControlRotationFromPawnOrientation = true;
}

void AOperativeAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(2.f);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
	}
}

void AOperativeAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	// A newer order replaced this one; the new order owns the operative's movement state.
	if (Result.HasFlag(FPathFollowingResultFlags::NewRequest))
	{
		return;
	}
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetPawn()))
	{
		Operative->HandleMoveFinished();
	}
}
