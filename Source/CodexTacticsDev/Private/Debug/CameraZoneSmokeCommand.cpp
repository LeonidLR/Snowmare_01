// Dev-only console command for headless camera zone checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CameraZoneSmoke
// The leader walks into the bunker camera zone, then back out. Checks that the view switches to the zone
// camera while the leader is inside, followers hold outside, and the view returns to the camera pawn after.
// Waits on conditions, not on fixed seconds (2026-11-02): since the formation runtime (b75e3cef, user decision 2026-10-25: the squad
// walks at the patrol pace, ~80 % of the walk) the leader reached the zone after the old fixed 12 s check.

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
	/** The leader must be inside the zone by then (the walk there takes ~11 s at the patrol pace). */
	constexpr int32 EnterCapSeconds = 45;
	/** Seconds inside the zone before the enter check (the followers have stopped at the perimeter). */
	constexpr int32 InsideSettleSeconds = 2;
	/** The leader must be out of the zone and the view back by then, counted from the order out. */
	constexpr int32 ExitCapSeconds = 40;

	struct FState
	{
		int32 Second = 0;
		int32 InsideSeconds = 0;
		int32 ExitSecond = 0;
		bool bEnterChecked = false;
		bool bEnterOk = false;
		bool bDone = false;
		FVector Start = FVector::ZeroVector;
	};

	ACameraZoneVolume* FindZone(UWorld* World)
	{
		TActorIterator<ACameraZoneVolume> It(World);
		return It ? *It : nullptr;
	}

	void Finish(bool bEnterOk, bool bExitOk)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke enter=%d exit=%d"), bEnterOk ? 1 : 0, bExitOk ? 1 : 0);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bEnterOk && bExitOk ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CameraZoneSmoke"));
	}

	void Step(UWorld* World, FState& State)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		ACameraZoneVolume* Zone = FindZone(World);
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		if (State.bDone)
		{
			return;
		}
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

		if (!State.bEnterChecked)
		{
			State.InsideSeconds = Leader->bInCameraZone ? State.InsideSeconds + 1 : 0;
			if (State.InsideSeconds < InsideSettleSeconds && State.Second < EnterCapSeconds)
			{
				return;
			}
			State.bEnterChecked = true;
			State.bEnterOk = bViewOnZone && Squad->AreFollowersHolding() && FollowersInside == 0 && Leader->bInCameraZone;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke enter check at t=%ds: %s"), State.Second, State.bEnterOk ? TEXT("ok") : TEXT("FAIL"));
			// Back out to where he started (outside the zone).
			Leader->OrderMoveTo(State.Start, false);
			State.ExitSecond = State.Second;
			return;
		}
		const bool bExitOk = PC && PC->GetViewTarget() == PC->GetPawn() && !Squad->AreFollowersHolding() && !Leader->bInCameraZone;
		if ((bExitOk && !Leader->IsMoving()) || State.Second - State.ExitSecond >= ExitCapSeconds)
		{
			State.bDone = true;
			Finish(State.bEnterOk, bExitOk);
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
		TSharedRef<FState> State = MakeShared<FState>();
		State->Start = Leader->GetActorLocation();
		Leader->OrderMoveTo(Zone->GetActorLocation(), false);
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
