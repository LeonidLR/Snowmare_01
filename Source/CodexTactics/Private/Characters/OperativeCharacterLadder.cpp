// AOperativeCharacter: ladders (docs/design/LADDERS.md, user decisions 2026-10-22). The climb itself is ULadderClimbComponent;
// this cluster is the operative's side of it (gates, the resumed path, the save point). UE-only system (no Godot reference).

#include "Characters/OperativeCharacter.h"

#include "Ladders/LadderClimbComponent.h"

bool AOperativeCharacter::IsClimbingLadder() const
{
	return LadderClimb && LadderClimb->IsOnLadder();
}

void AOperativeCharacter::ResumeMoveAfterLadder(const FVector& Destination)
{
	ApplyMovementParams();
	RequestMove(Destination);
}

FVector AOperativeCharacter::GetSaveLocation() const
{
	return LadderClimb && LadderClimb->IsOnLadder() ? LadderClimb->GetSaveLocation() : GetActorLocation();
}
