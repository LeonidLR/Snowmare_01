// Dev-only console command for a headless Space-hold check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.HoldSphereSmoke
// Godot main.gd Space hold + tactical_hold_sphere.gd: while Space is held the squad holds fire and the dome / ring
// grow (eased) towards 15 m; releasing early hides them and lifts the cease fire.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HoldSphereActor.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace HoldSphereSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float FirstRadius = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("HoldSphereSmoke"));
		return false;
	}

	bool AllCeaseFire(USquadSubsystem* Squad, bool bExpected)
	{
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->bTacticalCeaseFire != bExpected)
			{
				return false;
			}
		}
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!PC || State.Time > 20.f)
		{
			Check(State, false, TEXT("controller / timeout"));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
			PC->SpacePressed();
			Next();
			return true;
		case 1:
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			State.FirstRadius = PC->GetHoldSphere() ? PC->GetHoldSphere()->GetCurrentRadius() : 0.f;
			Check(State, PC->GetHoldSphere() && PC->GetHoldSphere()->IsShown() && State.FirstRadius > 0.f && State.FirstRadius < 1500.f,
				FString::Printf(TEXT("held 0.3 s: dome shown, radius %.0f cm"), State.FirstRadius));
			Check(State, AllCeaseFire(Squad, true), TEXT("squad holds fire"));
			Check(State, PC->IsSpaceHeld() && PC->GetSpaceHeldTime() > 0.2f, TEXT("charge time for the HUD bar"));
			Next();
			return true;
		case 2:
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			Check(State, PC->GetHoldSphere()->GetCurrentRadius() > State.FirstRadius, TEXT("dome keeps growing"));
			PC->SpaceReleased();
			Check(State, !PC->GetHoldSphere()->IsShown() && AllCeaseFire(Squad, false), TEXT("release: dome hidden, fire allowed"));
			Check(State, FMath::IsNearlyEqual(AHoldSphereActor::EasedRadius(0.5f, 1500.f), 1125.f), TEXT("eased radius: half the hold = 11.25 m"));
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
		TEXT("CodexTactics.HoldSphereSmoke"),
		TEXT("Dev check: Space-hold dome growth, cease fire, release; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
