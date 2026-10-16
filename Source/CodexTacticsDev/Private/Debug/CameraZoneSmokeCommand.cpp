// Dev-only console command for headless camera zone checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CameraZoneSmoke
// The leader walks into the bunker camera zone, then back out. Checks that the view switches to the zone
// camera while the leader is inside, followers hold outside, and the view returns to the camera pawn after.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace CameraZoneSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr int32 EnterCheckSecond = 12;
	constexpr int32 ExitCheckSecond = 24;

	struct FState
	{
		int32 Second = 0;
		bool bEnterOk = false;
	};

	ACameraZoneVolume* FindZone(UWorld* World)
	{
		TActorIterator<ACameraZoneVolume> It(World);
		return It ? *It : nullptr;
	}

	void Step(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		ACameraZoneVolume* Zone = FindZone(World);
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		++State.Second;

		const bool bViewOnZone = PC && Zone && PC->GetViewTarget() == Zone->TargetCamera;
		int32 FollowersInside = 0;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member != Leader && Zone && Zone->ContainsLocation(Member->GetActorLocation()))
			{
				++FollowersInside;
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke t=%2ds leader=(%6.0f,%6.0f) zoneActive=%d viewOnZoneCamera=%d followersHolding=%d followersInside=%d"),
			State.Second, Leader->GetActorLocation().X, Leader->GetActorLocation().Y, Zone && Zone->IsZoneActive() ? 1 : 0,
			bViewOnZone ? 1 : 0, Squad->AreFollowersHolding() ? 1 : 0, FollowersInside);

		if (State.Second == EnterCheckSecond)
		{
			State.bEnterOk = bViewOnZone && Squad->AreFollowersHolding() && FollowersInside == 0 && Leader->bInCameraZone;
			Leader->OrderMoveTo(FVector::ZeroVector, false);
		}
		if (State.Second == ExitCheckSecond)
		{
			const bool bExitOk = PC && PC->GetViewTarget() == PC->GetPawn() && !Squad->AreFollowersHolding() && !Leader->bInCameraZone;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke enter=%d exit=%d"), State.bEnterOk ? 1 : 0, bExitOk ? 1 : 0);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bEnterOk && bExitOk ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("CameraZoneSmoke"));
		}
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		ACameraZoneVolume* Zone = FindZone(World);
		if (!Leader || !Zone || !Zone->TargetCamera)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: leader, camera zone or zone camera missing"));
			FPlatformMisc::RequestExit(false, TEXT("CameraZoneSmoke"));
			return;
		}
		Leader->OrderMoveTo(Zone->GetActorLocation(), false);

		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, State]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Step(W, *State);
			}
		}), 1.f, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		// Test layout start; the squad settles into formation during the NavMesh warm-up.
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
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
		TEXT("CodexTactics.CameraZoneSmoke"),
		TEXT("Dev check: leader enters and leaves the L_MovementTest bunker camera zone; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
