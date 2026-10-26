// Dev-only console command for a headless check that a panned-away camera STAYS (game designer 2026-10-26):
//   Scripts/smoke.ps1 -Command CodexTactics.CameraStaySmoke
// 1. pan away by 12 m (ATacticalCameraPawn::NudgePan = a WASD / drag pan); 2. the leader is ordered 10 m away: the camera
// position does not move (1 cm) while he walks; 3. a number key re-centres the camera on the chosen operative and it follows
// his next move; 4. the leader's own number re-attaches after a second pan too.
// Code: Camera/TacticalCameraPawn.cpp (bDetached, ReattachToFollowTarget), Core/CodexTacticsPlayerControllerSelection.cpp (SelectMember).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace CameraStaySmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float PanCm = 1200.f;
	constexpr float WalkCm = 1000.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector HeldCamera = FVector::ZeroVector;
		FVector WalkStart = FVector::ZeroVector;
		FVector CameraAtOrder = FVector::ZeroVector;
		float MaxDrift = 0.f;
		float MaxWalked = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CameraStaySmoke"));
		return false;
	}

	float Dist2D(const FVector& A, const FVector& B)
	{
		return static_cast<float>(FVector::Dist2D(A, B));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 60.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		ATacticalCameraPawn* Camera = PC ? Cast<ATacticalCameraPawn>(PC->GetPawn()) : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Camera || !Leader)
		{
			return State.Time < 8.f;
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto OrderWalk = [World](AOperativeCharacter* Who)
		{
			const FVector From = Who->GetActorLocation();
			return Who->OrderMoveTo(SmokeUtils::ClearPoint(World, From, From + Who->GetActorForwardVector() * WalkCm), false);
		};
		switch (State.Stage)
		{
		case 0: // settle after the test layout
			if (State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, !Camera->IsDetachedFromFollow(), TEXT("start: attached"));
			Camera->NudgePan(Camera->GetActorRightVector() * PanCm);
			Next();
			return true;
		case 1: // the pan settles; the camera is detached
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Camera->IsDetachedFromFollow(), TEXT("a manual pan detaches the camera"));
			Check(State, Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation()) > PanCm * 0.9f,
				FString::Printf(TEXT("the view is %.0f cm off the leader"), Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation())));
			State.HeldCamera = Camera->GetActorLocation();
			State.WalkStart = Leader->GetActorLocation();
			Check(State, OrderWalk(Leader) == EOperativeOrderResult::Accepted, TEXT("the leader gets a 10 m move order"));
			Next();
			return true;
		case 2: // he walks, the camera holds
			State.MaxDrift = FMath::Max(State.MaxDrift, static_cast<float>(FVector::Dist(Camera->GetActorLocation(), State.HeldCamera)));
			State.MaxWalked = FMath::Max(State.MaxWalked, Dist2D(Leader->GetActorLocation(), State.WalkStart));
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, State.MaxWalked > 500.f, FString::Printf(TEXT("the leader walked %.0f cm"), State.MaxWalked));
			Check(State, State.MaxDrift <= 1.f, FString::Printf(TEXT("the camera stayed put while he walked (max drift %.2f cm)"), State.MaxDrift));
			Check(State, Camera->IsDetachedFromFollow(), TEXT("still detached after the walk"));
			Next();
			return true;
		case 3: // another operative's number: the view comes over him
		{
			const int32 Index = Leader->SquadIndex == 0 ? 1 : 0;
			if (!Squad->GetMembers().IsValidIndex(Index))
			{
				return Finish(State, false);
			}
			PC->SelectSquadMember(Index);
			Next();
			return true;
		}
		case 4:
		{
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, !Camera->IsDetachedFromFollow() && Camera->GetFollowTarget() == Leader, TEXT("a number key re-attaches the camera to the new leader"));
			Check(State, Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation()) < 25.f,
				FString::Printf(TEXT("the view is centred on him (%.0f cm off)"), Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation())));
			State.CameraAtOrder = Camera->GetActorLocation();
			State.WalkStart = Leader->GetActorLocation();
			Check(State, OrderWalk(Leader) == EOperativeOrderResult::Accepted, TEXT("he gets a 10 m move order"));
			Next();
			return true;
		}
		case 5: // the camera follows him
		{
			if (State.StageTime < 6.f)
			{
				return true;
			}
			const float Walked = Dist2D(Leader->GetActorLocation(), State.WalkStart);
			const float CameraMoved = Dist2D(Camera->GetActorLocation(), State.CameraAtOrder);
			Check(State, Walked > 400.f && CameraMoved > Walked * 0.7f,
				FString::Printf(TEXT("the camera follows him (walked %.0f cm, camera moved %.0f cm)"), Walked, CameraMoved));
			Camera->NudgePan(Camera->GetActorForwardVector() * -PanCm);
			Next();
			return true;
		}
		case 6: // a second pan, then the leader's OWN number (it opens his profile and re-centres)
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Camera->IsDetachedFromFollow(), TEXT("panned away again: detached"));
			PC->SelectSquadMember(Leader->SquadIndex);
			Next();
			return true;
		case 7:
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, !Camera->IsDetachedFromFollow() && Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation()) < 25.f,
				FString::Printf(TEXT("the leader's own number re-centres too (%.0f cm off)"), Dist2D(Camera->GetViewCentre(), Leader->GetActorLocation())));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CameraStaySmoke"),
		TEXT("Dev check: a panned-away camera stays put while a character walks and a number key brings it back; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
