// Dev-only console command for a headless guard check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GuardSmoke
// The engineer is made leader and T fixes it on its spot (out of the formation); the commander leads the squad 8 m
// away: the engineer stays, the medic follows. T again returns the engineer to the formation
// (Godot main.gd toggle_soldier_guard / _on_guard_slot_clicked, player.gd set_guard_mode, assign_formation_slots).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace GuardSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		FVector GuardSpot = FVector::ZeroVector;
		FVector MedicStart = FVector::ZeroVector;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GuardSmoke"));
		return false;
	}

	AOperativeCharacter* Member(USquadSubsystem* Squad, EOperativeRole Role)
	{
		for (AOperativeCharacter* Operative : Squad->GetMembers())
		{
			if (Operative->SquadRole == Role)
			{
				return Operative;
			}
		}
		return nullptr;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 30.f)
		{
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Commander = Member(Squad, EOperativeRole::Commander);
		AOperativeCharacter* Engineer = Member(Squad, EOperativeRole::Engineer);
		AOperativeCharacter* Medic = Member(Squad, EOperativeRole::MedicSapper);
		if (!PC || !Commander || !Engineer || !Medic)
		{
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
			if (State.Time < 3.f)
			{
				return true;
			}
			Squad->SetLeader(Engineer);
			PC->GuardKey();
			Check(State, Engineer->bGuarding, TEXT("T: the engineer guards its spot"));
			Squad->SetLeader(Commander);
			Check(State, Squad->GetFormationSlot(Engineer) == INDEX_NONE && Squad->GetFormationSlot(Medic) != INDEX_NONE,
				TEXT("the guard is out of the formation, the medic in it"));
			State.GuardSpot = Engineer->GetActorLocation();
			State.MedicStart = Medic->GetActorLocation();
			Commander->OrderMoveTo(Commander->GetActorLocation() + Commander->GetActorForwardVector() * 800.f, false);
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		case 1:
			if (State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, FVector::Dist2D(Engineer->GetActorLocation(), State.GuardSpot) < 60.f,
				FString::Printf(TEXT("the guard stayed (moved %.0f cm)"), FVector::Dist2D(Engineer->GetActorLocation(), State.GuardSpot)));
			Check(State, FVector::Dist2D(Medic->GetActorLocation(), State.MedicStart) > 300.f,
				FString::Printf(TEXT("the medic followed (%.0f cm)"), FVector::Dist2D(Medic->GetActorLocation(), State.MedicStart)));
			Squad->SetLeader(Engineer);
			PC->GuardKey();
			Squad->SetLeader(Commander);
			Check(State, !Engineer->bGuarding && Squad->GetFormationSlot(Engineer) != INDEX_NONE, TEXT("T again: back in the formation"));
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
		TEXT("CodexTactics.GuardSmoke"),
		TEXT("Dev check: T fixes an operative on its spot (out of the formation), T again returns it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
