#include "Characters/OperativeAIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeCrowdFollowingComponent.h"
#include "CodexTactics.h"
#include "HAL/IConsoleManager.h"
#include "Navigation/CrowdFollowingComponent.h"

AOperativeAIController::AOperativeAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOperativeCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
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
		static const IConsoleVariable* MoveTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.MoveTrace"));
		if (MoveTrace && MoveTrace->GetInt() != 0)
		{
			const FVector Velocity = Operative->GetVelocity();
			const FVector ToGoal = (Operative->GetLastMoveDestination() - Operative->GetActorLocation()).GetSafeNormal2D();
			UE_LOG(LogCodexTactics, Display, TEXT("[MoveTrace] %s: move completed: %s, %.0f cm from the goal, velocity %.0f (dot to goal %.2f)"),
				*Operative->DisplayName.ToString(), *Result.ToString(), FVector::Dist2D(Operative->GetLastMoveDestination(), Operative->GetActorLocation()),
				Velocity.Size2D(), FVector::DotProduct(Velocity.GetSafeNormal2D(), ToGoal));
		}
		Operative->HandleMoveFinished();
	}
}
