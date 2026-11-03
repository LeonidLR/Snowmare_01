// AOperativeCharacter: falls and drops (game designer items 6 / 10 / 11a, 2026-11-03; docs/systems/stances-and-movement.md
// "Falls and drops"). The drop / fall itself is ULedgeFallComponent; this cluster is the operative's side of it. UE-only (no Godot reference).

#include "Characters/OperativeCharacter.h"

#include "Characters/OperativeMovementRules.h"
#include "Falls/LedgeFallComponent.h"

bool AOperativeCharacter::IsDroppingOffLedge() const
{
	return LedgeFall && LedgeFall->IsDropping();
}

float AOperativeCharacter::GetSprintSpeedCap() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Standing, CanSprint(), IsWounded(), bCarrying) * ColdSpeedMultiplier;
}
