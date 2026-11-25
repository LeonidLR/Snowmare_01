#include "Characters/OperativeAIController.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeCrowdFollowingComponent.h"
#include "CodexTactics.h"
#include "AI/Navigation/NavQueryFilter.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/MantleNavigation.h"
#include "Interactables/VaultNavigation.h"
#include "NavigationData.h"
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

void AOperativeAIController::FindPathForMoveRequest(const FAIMoveRequest& MoveRequest, FPathFindingQuery& Query, FNavPathSharedPtr& OutPath) const
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(GetPawn());
	const ANavigationData* NavData = Query.NavData.Get();
	if (NavData && Operative && Operative->IsPlayerTeam() && Operative->HasPendingCover()
		&& Operative->GetPendingCoverSlot().Height == ECoverHeight::LowCover)
	{
		const FSharedConstNavQueryFilter Original = Query.QueryFilter ? Query.QueryFilter : NavData->GetDefaultQueryFilter();
		FSharedNavQueryFilter Filter = Original->GetCopy();
		for (const UClass* Area : { UNavArea_Mantle::StaticClass(), UNavArea_Vault::StaticClass(), UNavArea_WindowVault::StaticClass() })
		{
			const int32 Id = NavData->GetAreaID(Area);
			if (Id != INDEX_NONE)
			{
				Filter->SetExcludedArea(static_cast<uint8>(Id));
			}
		}
		Query.QueryFilter = Filter;
		Query.SetAllowPartialPaths(false);
	}
	Super::FindPathForMoveRequest(MoveRequest, Query, OutPath);
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
		// Give way v2 (X6 LevelGenRampSmoke): the crowd's block detection (no 10 cm in 5 s, a mate in a stair mouth) ended the player's order
		// silently ('order none') - it is asked again instead (SquadGiveWayRules::ShouldRetryBlockedMove).
		if (Result.Code == EPathFollowingResult::Blocked && Operative->RetryBlockedOwnMove())
		{
			return;
		}
		Operative->HandleMoveFinished();
	}
}
