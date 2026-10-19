// AEnemyCharacter: the patrol / search snapshot for the save game.
// Split out of EnemyCharacter.cpp verbatim (cleanup 2026-10-19; map: docs/port/REFACTOR_ENEMY_2026-10.md).
// UE-only (no Godot reference). Written / read by USaveGameSubsystem.

#include "Characters/EnemyCharacter.h"
#include "AI/PatrolRouteActor.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"

FEnemyPatrolSnapshot AEnemyCharacter::CapturePatrolSnapshot() const
{
	FEnemyPatrolSnapshot Snapshot;
	Snapshot.bOnPatrol = IsOnPatrol();
	Snapshot.WaypointIndex = PatrolWaypointIndex;
	Snapshot.bForward = bPatrolForward;
	Snapshot.Phase = static_cast<int32>(PatrolPhase);
	Snapshot.WaitLeft = PatrolWaitLeft;
	Snapshot.bSearching = bSearching;
	Snapshot.SearchOrigin = SearchOrigin;
	Snapshot.SearchElapsed = SearchElapsed;
	Snapshot.Suspicion = PatrolSuspicion;
	Snapshot.bLureSearch = bSearching && ActiveSearchSpec.bLure;
	Snapshot.SearchDurationSeconds = ActiveSearchSpec.DurationSeconds;
	Snapshot.SearchSweepRadiusCm = ActiveSearchSpec.SweepRadiusCm;
	Snapshot.SearchApproachSpeedFraction = ActiveSearchSpec.ApproachSpeedFraction;
	Snapshot.bScentSearch = bSearching && ActiveSearchSpec.bScent;
	return Snapshot;
}

void AEnemyCharacter::RestorePatrolSnapshot(APatrolRouteActor* Route, AEnemyCharacter* Leader, const FEnemyPatrolSnapshot& Snapshot)
{
	if (bIsDying)
	{
		return;
	}
	if (AAIController* AIC = Cast<AAIController>(GetController()))
	{
		AIC->StopMovement();
	}
	if (!Snapshot.bOnPatrol)
	{
		AssignedPatrolRoute = Route;
		EscortLeader = Leader != this ? Leader : nullptr;
		bPatrolActive = false;
		bGuardingPost = false;
		bSearching = false;
		return;
	}
	StartPatrol(Route, Leader); // virtual: the marksman returns to his patrol state too
	PatrolWaypointIndex = FMath::Max(0, Snapshot.WaypointIndex);
	bPatrolForward = Snapshot.bForward;
	PatrolPhase = static_cast<EPatrolPhase>(FMath::Clamp(Snapshot.Phase, 0, static_cast<int32>(EPatrolPhase::Finished)));
	PatrolWaitLeft = Snapshot.WaitLeft;
	PatrolTurnTime = 0.f;
	PatrolStuckTime = 0.f;
	bPatrolMoveIssued = false;
	bEscortMoving = false;
	PatrolSuspicion = Snapshot.Suspicion;
	bSearching = Snapshot.bSearching;
	ActiveSearchSpec = FPatrolSearchSpec();
	if (Snapshot.bSearching)
	{
		ActiveSearchSpec.bLure = Snapshot.bLureSearch;
		ActiveSearchSpec.DurationSeconds = Snapshot.SearchDurationSeconds;
		ActiveSearchSpec.SweepRadiusCm = Snapshot.SearchSweepRadiusCm;
		ActiveSearchSpec.ApproachSpeedFraction = Snapshot.SearchApproachSpeedFraction;
		ActiveSearchSpec.bScent = Snapshot.bScentSearch;
	}
	SearchOrigin = Snapshot.SearchOrigin;
	SearchNoise = Snapshot.SearchOrigin;
	SearchGoal = Snapshot.SearchOrigin;
	SearchListenLeft = 0.f;
	SearchMoveFailures = 0;
	SearchElapsed = Snapshot.SearchElapsed;
	SearchLegTime = 0.f;
	SearchLookLeft = 0.f;
	SearchStuckTime = 0.f;
	bSearchReachedOrigin = false;
	bSearchMoving = false;
	GetCharacterMovement()->MaxWalkSpeed = PatrolWalkSpeed;
}
