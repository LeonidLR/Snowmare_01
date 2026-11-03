// AOperativeCharacter: ladders (docs/design/LADDERS.md, user decisions 2026-10-22). The climb itself is ULadderClimbComponent;
// this cluster is the operative's side of it (gates, the resumed path, the save point). UE-only system (no Godot reference).

#include "Characters/OperativeCharacter.h"

#include "Falls/LedgeFallComponent.h"
#include "Ladders/LadderClimbComponent.h"

bool AOperativeCharacter::IsClimbingLadder() const
{
	return LadderClimb && LadderClimb->IsOnLadder();
}

void AOperativeCharacter::ResumeMoveAfterLadder(const FVector& Destination, bool bOwnOrder)
{
	ApplyMovementParams();
	RequestMove(Destination);
	// The climb stopped his path, not his order: an own order stays his own (the formation does not pull him off it, LADDERS.md 7.1).
	bOwnMoveOrder = bOwnOrder && bHasMoveOrder;
}

FVector AOperativeCharacter::GetFloorReferenceLocation() const
{
	return LadderClimb ? LadderClimb->GetFloorReferenceLocation() : GetActorLocation();
}

FVector AOperativeCharacter::GetSaveLocation() const
{
	if (LadderClimb && LadderClimb->IsOnLadder())
	{
		return LadderClimb->GetSaveLocation();
	}
	return LedgeFall && LedgeFall->IsBusy() ? LedgeFall->GetSaveLocation() : GetActorLocation(); // mid-drop: the landing (2026-11-03)
}
