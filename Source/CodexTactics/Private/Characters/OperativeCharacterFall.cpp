// AOperativeCharacter: falls and drops (game designer items 6 / 10 / 11a, 2026-11-03; docs/systems/stances-and-movement.md
// "Falls and drops"). The drop / fall itself is ULedgeFallComponent; this cluster is the operative's side of it. UE-only (no Godot reference).

#include "Characters/OperativeCharacter.h"

#include "AIController.h"
#include "Characters/OperativeMovementRules.h"
#include "Components/CapsuleComponent.h"
#include "Falls/FallRules.h"
#include "Falls/LedgeFallComponent.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/NavMeshPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"

namespace OperativeFallInternal
{
	static TAutoConsoleVariable<int32> CVarFallLinkCommit(
		TEXT("Codex.Fall.LinkCommit"),
		1,
		TEXT("The soft-lock fix (user bug 2026-11-03): 1 (default) a man at / over a plain nav link that drops a level (a hole link) keeps his path - the ")
		TEXT("formation does not re-path him off the rim; 0 off (the old behaviour: the DropSoftlockSmoke rescue stage uses it to make the soft-lock)."),
		ECVF_Default);
}

bool AOperativeCharacter::IsDroppingOffLedge() const
{
	return LedgeFall && LedgeFall->IsDropping();
}

float AOperativeCharacter::GetSprintSpeedCap() const
{
	return OperativeMovementRules::ComputeMaxSpeed(MovementConfig, EOperativeStance::Standing, CanSprint(), IsWounded(), bCarrying) * ColdSpeedMultiplier
		* (Stance == EOperativeStance::Standing ? GetWoundTierSpeedScale() : 1.f);
}

bool AOperativeCharacter::IsCommittedToNavLinkDrop() const
{
	if (OperativeFallInternal::CVarFallLinkCommit.GetValueOnGameThread() <= 0)
	{
		return false;
	}
	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!Following || Following->GetStatus() != EPathFollowingStatus::Moving || !Following->GetPath().IsValid() || !Following->GetPath()->IsValid())
	{
		return false;
	}
	const FNavMeshPath* NavPath = Following->GetPath()->CastPath<FNavMeshPath>();
	const ARecastNavMesh* Recast = NavPath ? Cast<ARecastNavMesh>(NavPath->GetNavigationDataUsed()) : nullptr;
	if (!Recast)
	{
		return false;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector Goal = NavPath->GetDestinationLocation();
	const FallRules::FFallConfig& Config = FallRules::GetConfig();
	for (const NavNodeRef Ref : NavPath->PathCorridor)
	{
		FVector A;
		FVector B;
		// A link poly of the corridor (a ground poly has no end points); a smart link (a ladder, a generated drop) has its own component.
		if (Recast->GetLinkEndPoints(Ref, A, B) && !Recast->GetNavLinkUserId(Ref).IsValid() && FallRules::IsCommittedToDrop(Feet, Goal, A, B, Config))
		{
			return true;
		}
	}
	return false;
}
