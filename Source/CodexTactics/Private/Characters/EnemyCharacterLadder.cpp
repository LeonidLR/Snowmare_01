// AEnemyCharacter: ladders (docs/design/LADDERS.md 7.5, user decision 2026-10-22: the non-mutant humans climb - the zombified PMC,
// the Military Zombie, the Marksman, the future PMC mercenaries; mutants never). The climb itself is ULadderClimbComponent (the
// operatives' state machine and clips); this cluster is the enemy's side: the class flag, the nav filter, the suspended AI, the
// resumed move, the save point. UE-only system (no Godot reference).

#include "Characters/EnemyCharacter.h"

#include "AIController.h"
#include "Characters/EnemyAIController.h"
#include "CodexTactics.h"
#include "Ladders/LadderClimbComponent.h"

void AEnemyCharacter::InitLadderClimb()
{
	if (!LadderClimb)
	{
		return;
	}
	bLadderClipsMissing = false;
	if (bCanClimbLadders && !LadderClimb->EnsureClips())
	{
		bLadderClipsMissing = true;
		UE_LOG(LogCodexTactics, Warning, TEXT("[Ladder] %s (%s) may climb but its ladder clips do not load: it paths around ladders"), *GetName(),
			*GetClass()->GetName());
	}
	LadderClimb->SetComponentTickEnabled(CanClimbLadders());
	if (AEnemyAIController* EnemyAIC = Cast<AEnemyAIController>(GetController()))
	{
		EnemyAIC->RefreshNavigationFilter();
	}
}

bool AEnemyCharacter::CanClimbLadders() const
{
	return bCanClimbLadders && LadderClimb && !bLadderClipsMissing;
}

bool AEnemyCharacter::IsClimbingLadder() const
{
	return LadderClimb && LadderClimb->IsOnLadder();
}

bool AEnemyCharacter::IsLadderBusy() const
{
	return LadderClimb && LadderClimb->IsBusy();
}

void AEnemyCharacter::ResumeMoveAfterLadder(const FVector& Destination)
{
	// Every move flag the AI keeps is reset (the climb stopped the path): the throttled AI asks again this tick; a patrol / search
	// walk is re-issued by its own code. Until then it walks on to where the path led.
	ResetAIMoveThrottle();
	bPatrolMoveIssued = false;
	bEscortMoving = false;
	bSearchMoving = false;
	bScentMoveIssued = false;
	if (AAIController* AIC = Cast<AAIController>(GetController()); AIC && !IsTurnBasedHeld())
	{
		AIC->MoveToLocation(Destination, 50.f, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true);
	}
}

FVector AEnemyCharacter::GetSaveLocation() const
{
	return LadderClimb && LadderClimb->IsOnLadder() ? LadderClimb->GetSaveLocation() : GetActorLocation();
}
