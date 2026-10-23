#include "Characters/EnemyAIController.h"
#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/VaultNavigation.h"
#include "Ladders/LadderNavigation.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/PathFollowingComponent.h"

namespace EnemyRepath
{
	static TAutoConsoleVariable<int32> CVarRepath(TEXT("Codex.AI.Repath"), 1,
		TEXT("Enemy move re-path throttle: 1 = re-issue a move only on a changed goal / a stopped move / the interval, 0 = every tick (old)"));
	static TAutoConsoleVariable<float> CVarRepathInterval(TEXT("Codex.AI.RepathInterval"), 0.3f,
		TEXT("Shortest time between two re-issues of an unchanged or slowly moving enemy move goal, s"));
	static TAutoConsoleVariable<float> CVarRepathMinGoalDelta(TEXT("Codex.AI.RepathMinGoalDeltaCm"), 75.f,
		TEXT("An enemy move goal that moved further than this (cm) counts as a changed goal"));

	int64 TotalRequests = 0;
	int64 TotalPathfinds = 0;
	int64 TotalAsks = 0;
}

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
	RefreshNavigationFilter();
	ResetRepathState();
}

void AEnemyAIController::RefreshNavigationFilter()
{
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetPawn());
	DefaultNavigationFilterClass = Enemy && Enemy->CanClimbLadders() ? UNavFilter_NoVaultLadder::StaticClass() : UNavFilter_NoVault::StaticClass();
}

FPathFollowingRequestResult AEnemyAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	if (const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetPawn()); Enemy && Enemy->IsTurnBasedHeld())
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[TurnBased] %s: AI move refused (held by the grid)"), *Enemy->GetName());
		FPathFollowingRequestResult Refused;
		Refused.Code = EPathFollowingRequestResult::Failed;
		LastMoveToRequestId = 0;
		return Refused;
	}
	++MoveRequestCount;
	++EnemyRepath::TotalRequests;
	const FPathFollowingRequestResult Result = Super::MoveTo(MoveRequest, OutPath);
	LastMoveToRequestId = Result.MoveId.GetID();
	if (Result.Code != EPathFollowingRequestResult::AlreadyAtGoal)
	{
		++MovePathfindCount; // past the already-at-goal test: a (synchronous) path query was attempted
		++EnemyRepath::TotalPathfinds;
	}
	return Result;
}

void AEnemyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	LastFinishedRequestId = RequestID.GetID();
	bLastFinishSuccess = Result.IsSuccess();
	bLastFinishInterrupted = Result.IsInterrupted();
	Super::OnMoveCompleted(RequestID, Result);
}

FRepathSettings AEnemyAIController::GetRepathSettings()
{
	FRepathSettings Settings;
	Settings.bEnabled = EnemyRepath::CVarRepath.GetValueOnGameThread() != 0;
	Settings.IntervalSeconds = FMath::Max(0.f, EnemyRepath::CVarRepathInterval.GetValueOnGameThread());
	Settings.MinGoalDeltaCm = FMath::Max(0.f, EnemyRepath::CVarRepathMinGoalDelta.GetValueOnGameThread());
	return Settings;
}

ERepathFollowState AEnemyAIController::GetRepathFollowState() const
{
	const UPathFollowingComponent* Follow = GetPathFollowingComponent();
	const bool bIdle = !Follow || Follow->GetStatus() == EPathFollowingStatus::Idle;
	const bool bCurrentIsOurs = Follow && LastRepathRequestId != 0 && Follow->GetCurrentRequestId().GetID() == LastRepathRequestId;
	const bool bFinishedOurs = LastRepathRequestId != 0 && LastFinishedRequestId == LastRepathRequestId;
	return RepathRules::ClassifyFollowState(bHasRepathGoal, bIdle, bCurrentIsOurs, bLastRepathFailed, bFinishedOurs, bLastFinishSuccess,
		bLastFinishInterrupted);
}

bool AEnemyAIController::ShouldIssue(const FRepathGoal& Wanted, const FAIMoveRequest& Request)
{
	++MoveAskCount;
	++EnemyRepath::TotalAsks;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// The same reach test MoveTo runs first (cheap: no path query).
	const UPathFollowingComponent* Follow = GetPathFollowingComponent();
	const bool bAtGoal = Follow && Request.IsValid() && Follow->HasReached(Request);
	return RepathRules::ShouldRepath(bHasRepathGoal ? &LastRepathGoal : nullptr, Wanted, GetRepathFollowState(), Now - LastRepathTime, bAtGoal,
		GetRepathSettings());
}

void AEnemyAIController::RecordIssued(const FRepathGoal& Wanted, EPathFollowingRequestResult::Type Code)
{
	const UWorld* World = GetWorld();
	LastRepathGoal = Wanted;
	bHasRepathGoal = true;
	LastRepathTime = World ? World->GetTimeSeconds() : 0.0;
	LastRepathRequestId = LastMoveToRequestId;
	bLastRepathFailed = Code == EPathFollowingRequestResult::Failed;
}

bool AEnemyAIController::RequestMoveToLocation(const FVector& Goal, float AcceptanceRadius)
{
	FRepathGoal Wanted;
	Wanted.Location = Goal;
	Wanted.AcceptanceRadius = AcceptanceRadius;
	// As AAIController::MoveToLocation(Goal, AcceptanceRadius, bStopOnOverlap false) builds it (for the reach test).
	FAIMoveRequest Request(Goal);
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetReachTestIncludesAgentRadius(false);
	if (!ShouldIssue(Wanted, Request))
	{
		return false;
	}
	RecordIssued(Wanted, MoveToLocation(Goal, AcceptanceRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true));
	return true;
}

bool AEnemyAIController::RequestMoveToActor(AActor* Goal, float AcceptanceRadius)
{
	if (!Goal)
	{
		return false;
	}
	FRepathGoal Wanted;
	Wanted.GoalActorId = Goal->GetUniqueID();
	Wanted.Location = Goal->GetActorLocation();
	Wanted.AcceptanceRadius = AcceptanceRadius;
	// As AAIController::MoveToActor(Goal, AcceptanceRadius) builds it (bStopOnOverlap true), for the reach test.
	FAIMoveRequest Request(Goal);
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetReachTestIncludesAgentRadius(true);
	if (!ShouldIssue(Wanted, Request))
	{
		return false;
	}
	RecordIssued(Wanted, MoveToActor(Goal, AcceptanceRadius));
	return true;
}

void AEnemyAIController::ResetRepathState()
{
	bHasRepathGoal = false;
	LastRepathRequestId = 0;
	bLastRepathFailed = false;
}

int64 AEnemyAIController::GetTotalMoveRequestCount()
{
	return EnemyRepath::TotalRequests;
}

int64 AEnemyAIController::GetTotalPathfindCount()
{
	return EnemyRepath::TotalPathfinds;
}

int64 AEnemyAIController::GetTotalMoveAskCount()
{
	return EnemyRepath::TotalAsks;
}

void AEnemyAIController::ResetTotalMoveCounters()
{
	EnemyRepath::TotalRequests = 0;
	EnemyRepath::TotalPathfinds = 0;
	EnemyRepath::TotalAsks = 0;
}
