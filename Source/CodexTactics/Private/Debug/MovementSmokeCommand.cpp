// Dev-only console command for headless movement checks on L_MovementTest:
//   UnrealEditor-Cmd CodexTactics.uproject /Game/Maps/L_MovementTest -game -nullrhi -ExecCmds="CodexTactics.MovementSmoke"
// Orders the leader to sprint through the narrow corridor, logs the squad once per second,
// then logs PASS/FAIL (leader reached the goal, followers stay close behind) and exits.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"

namespace MovementSmoke
{
	/** Corridor exit in L_MovementTest design coordinates (SmokeUtils::LevelPoint maps it onto the current layout). */
	const FVector DesignGoal(2600.f, 0.f, 100.f);
	constexpr float GoalTolerance = 60.f;
	constexpr float FollowerTolerance = 700.f;
	constexpr int32 DurationSeconds = 14;
	/** Runtime NavMesh tiles are built asynchronously; give them time before the order. */
	constexpr float NavWarmupSeconds = 3.f;

	void LogSquad(UWorld* World, int32 Second)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke t=%2ds %-12s pos=(%6.0f,%6.0f) speed=%4.0f slot=%d sprint=%d"),
				Second, *Member->DisplayName.ToString(), Member->GetActorLocation().X, Member->GetActorLocation().Y,
				Member->GetVelocity().Size2D(), Squad->GetFormationSlot(Member), Member->IsSprinting() ? 1 : 0);
		}
	}

	void LogNavigationState(UWorld* World, const FVector& LeaderLocation)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const ANavigationData* NavData = NavSys ? NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke nav: system=%s navdata=%s invokers=%d building=%d"),
			NavSys ? TEXT("yes") : TEXT("NO"), *GetNameSafe(NavData),
			NavSys ? NavSys->GetInvokerLocations().Num() : -1,
			NavSys ? (NavSys->IsNavigationBuildInProgress() ? 1 : 0) : -1);
		if (NavSys)
		{
			FNavLocation Projected;
			const bool bLeaderOnNav = NavSys->ProjectPointToNavigation(LeaderLocation, Projected, FVector(100.f, 100.f, 300.f));
			const bool bGoalOnNav = NavSys->ProjectPointToNavigation(SmokeUtils::LevelPoint(World, DesignGoal), Projected, FVector(100.f, 100.f, 300.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke nav: leader on navmesh=%d goal on navmesh=%d"), bLeaderOnNav ? 1 : 0, bGoalOnNav ? 1 : 0);
		}
	}

	void Finish(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		bool bPass = Leader && FVector::Dist2D(Leader->GetActorLocation(), SmokeUtils::LevelPoint(World, DesignGoal)) <= GoalTolerance;
		if (Leader)
		{
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Leader && FVector::Dist2D(Member->GetActorLocation(), Leader->GetActorLocation()) > FollowerTolerance)
				{
					bPass = false;
				}
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MovementSmoke"));
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: no squad leader in this world"));
			FPlatformMisc::RequestExit(false, TEXT("MovementSmoke"));
			return;
		}

		SmokeUtils::PlaceSquadAtTestStart(World);
		LogNavigationState(World, Leader->GetActorLocation());
		const EOperativeOrderResult Order = Leader->OrderMoveTo(SmokeUtils::LevelPoint(World, DesignGoal), /*bSprint*/ true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: order result %s"), *UEnum::GetValueAsString(Order));

		TSharedRef<int32> Second = MakeShared<int32>(0);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Second]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				LogSquad(W, ++(*Second));
				if (*Second >= DurationSeconds)
				{
					Finish(W);
				}
			}
		}), 1.f, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.MovementSmoke"),
		TEXT("Dev check: leader sprints through the L_MovementTest corridor; logs squad positions and PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
