#include "Characters/EnemyAIController.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Interactables/VaultNavigation.h"
#include "Navigation/CrowdFollowingComponent.h"

AEnemyAIController::AEnemyAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSetControlRotationFromPawnOrientation = true;
	// Enemies never vault (Godot): the vault areas over barricades / "Vault" objects stay closed to them.
	DefaultNavigationFilterClass = UNavFilter_NoVault::StaticClass();
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(2.0f);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Medium);
	}
}

FPathFollowingRequestResult AEnemyAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetPawn()); Enemy && Enemy->IsTurnBasedHeld())
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[TurnBased] %s: AI move refused (held by the grid)"), *Enemy->GetName());
		FPathFollowingRequestResult Refused;
		Refused.Code = EPathFollowingRequestResult::Failed;
		return Refused;
	}
	return Super::MoveTo(MoveRequest, OutPath);
}
