#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PmcSquadSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Training/TrainingSectorSubsystem.h"

namespace SectorBoundarySmoke
{
	constexpr float Interval = 0.1f;

	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		SmokeUtils::FPreloadGate Preload;
		float Time = 0.f;
		float MoveAt = 0.f;
		FVector Goal = FVector::ZeroVector;
		bool bOrdered = false;
		bool bSawPmc = false;
	};

	bool Finish(bool bPass, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bPass ? TEXT("ok  ") : TEXT("FAIL"), What);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SectorBoundarySmoke"));
		return false;
	}

	bool FindEdgeGoal(AOperativeCharacter& Leader, const FBox2D& Box, FVector& Outside, FVector& OnNav)
	{
		UWorld* World = Leader.GetWorld();
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		const FVector Feet = Leader.GetActorLocation() - FVector(0.f, 0.f, Leader.GetSimpleCollisionHalfHeight());
		double Best = TNumericLimits<double>::Max();
		for (const double Along : { 0.2, 0.4, 0.6, 0.8 })
		{
			const FVector2D Interior = FMath::Lerp(Box.Min, Box.Max, Along);
			const FVector Candidates[] = {
				FVector(Box.Min.X - 500.0, Interior.Y, Feet.Z), FVector(Box.Max.X + 500.0, Interior.Y, Feet.Z),
				FVector(Interior.X, Box.Min.Y - 500.0, Feet.Z), FVector(Interior.X, Box.Max.Y + 500.0, Feet.Z)
			};
			for (const FVector& Candidate : Candidates)
			{
				const FVector Goal = TrainingSectorRules::Clamp(Box, Candidate, Leader.GetSimpleCollisionRadius() + 45.0);
				FNavLocation Projected;
				if (!Nav->ProjectPointToNavigation(Goal, Projected, FVector(180.0, 180.0, 200.0))
					|| !TrainingSectorRules::Contains(Box, Projected.Location))
				{
					continue;
				}
				const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, Feet, Projected.Location, &Leader);
				if (Path && Path->IsValid() && !Path->IsPartial() && Path->GetPathLength() < Best)
				{
					Best = Path->GetPathLength();
					Outside = Candidate;
					OnNav = Projected.Location;
				}
			}
		}
		return Best < TNumericLimits<double>::Max();
	}

	bool Step(TSharedRef<FState> State)
	{
		UWorld* World = State->World.Get();
		if (!World)
		{
			return false;
		}
		switch (State->Preload.Poll(World))
		{
		case SmokeUtils::EPreloadGate::Waiting: return true;
		case SmokeUtils::EPreloadGate::Failed: return Finish(false, TEXT("gameplay preload failed"));
		default: break;
		}
		State->Time += Interval;
		const UTrainingSectorSubsystem* Training = UTrainingSectorSubsystem::Get(World);
		const FBox2D* Box = Training ? Training->GetArea() : nullptr;
		const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
		if (!Box || !Pmc)
		{
			return Finish(false, TEXT("load X6 sector data with -Sector=1"));
		}
		if (!Leader || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
		{
			return State->Time < 30.f || Finish(false, TEXT("the sector navigation or squad never became ready"));
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Member || !TrainingSectorRules::Contains(*Box, Member->GetActorLocation()))
			{
				return Finish(false, TEXT("player operative outside the sector"));
			}
			Member->bTacticalCeaseFire = true;
		}
		const TArray<int32> TeamIds = Pmc->GetTeamIds();
		if (TeamIds.Num() > 1)
		{
			return Finish(false, TEXT("more than one PMC squad"));
		}
		for (int32 TeamId : TeamIds)
		{
			for (AOperativeCharacter* Member : Pmc->GetTeamMembers(TeamId))
			{
				if (!Member || !TrainingSectorRules::Contains(*Box, Member->GetActorLocation()))
				{
					return Finish(false, TEXT("PMC fighter outside the sector"));
				}
				Member->bTacticalCeaseFire = true;
				State->bSawPmc = true;
			}
		}
		if (!State->bOrdered)
		{
			FVector Outside;
			if (!FindEdgeGoal(*Leader, *Box, Outside, State->Goal)
				|| Leader->OrderMoveTo(Outside, false) != EOperativeOrderResult::Accepted
				|| !TrainingSectorRules::Contains(*Box, Leader->GetLastMoveDestination()))
			{
				return Finish(false, TEXT("outside move was not accepted with an in-sector destination"));
			}
			State->bOrdered = true;
			State->MoveAt = State->Time;
			return true;
		}
		if (State->bSawPmc && State->Time - State->MoveAt > 5.f && FVector::Dist2D(Leader->GetActorLocation(), State->Goal) <= 220.0)
		{
			return Finish(true, TEXT("the outside move stopped at the sector edge; the player and one PMC squad stayed inside"));
		}
		return State->Time < 105.f || Finish(false, TEXT("the move did not arrive at the edge with one PMC squad inside"));
	}

	void Run(const TArray<FString>&, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const TSharedRef<FState> State = MakeShared<FState>();
		State->World = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) { return Step(State); }), Interval);
	}

	FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.SectorBoundarySmoke"),
		TEXT("X6 sector: order outside, watch the team stop inside the boundary, verify the one PMC squad stays inside."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif
