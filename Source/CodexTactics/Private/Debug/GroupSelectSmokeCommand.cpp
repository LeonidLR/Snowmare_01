// Dev-only console command for a headless check of box selection and group orders on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GroupSelectSmoke
// Godot main.gd _perform_box_selection / _set_selected_squad / _get_group_target_positions, player.gd set_group_selected:
// rings under the selected (the leader's only in a group), the group walks to its formation around the click,
// picking a leader by number drops the group.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadFormation.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace GroupSelectSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FVector> Targets;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("GroupSelectSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			return World ? Finish(State, false) : false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || Members.Num() < 3 || !PC)
			{
				return true;
			}
			// Preparation: operatives act on their own (no formation following).
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Squad->SetLeader(Members[0]);

			// Selecting the two followers: they get rings, the leader switches to the first of them.
			Squad->SetSelectedGroup({ Members[1], Members[2] });
			Check(State, Squad->GetLeader() == Members[1], TEXT("leader not in the box: the first selected leads"));
			Check(State, Squad->HasMultiSelection() && Members[1]->IsSelectionRingShown() && Members[2]->IsSelectionRingShown()
				&& !Members[0]->IsSelectionRingShown(), TEXT("rings under both selected (the leader's too in a group)"));

			// All three: a group order sends each to its slot around the click.
			Squad->SetSelectedGroup({ Members[0], Members[1], Members[2] });
			const FVector Destination = Members[1]->GetActorLocation() + Members[1]->GetActorForwardVector() * 600.f;
			PC->OrderGroupMove(Destination, false, false);
			const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
			Check(State, Messages && !Messages->GetHistory().IsEmpty()
				&& Messages->GetHistory().Last().Text.ToString().Contains(TEXT("Группа (3 бойцов)")), TEXT("group order line"));
			State.Targets.Reset();
			FVector Average = FVector::ZeroVector;
			for (int32 Index : { 1, 0, 2 })
			{
				Average += Members[Index]->GetActorLocation() / 3.f;
			}
			State.Targets = SquadFormation::ComputeGroupTargets(Destination, Average, FVector::ForwardVector, 3);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 7.f)
			{
				return true;
			}
			// Leader (Members[1]) first, then Members[0], Members[2] in selection order.
			const AOperativeCharacter* Order[] = { Members[1], Members[0], Members[2] };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const float Distance = FVector::Dist2D(Order[Index]->GetActorLocation(), State.Targets[Index]);
				Check(State, Distance < 150.f, FString::Printf(TEXT("%s reached its group slot (%.0f cm off)"),
					*Order[Index]->DisplayName.ToString(), Distance));
			}
			// Choosing a leader by number drops the group.
			Squad->SetLeader(Members[2]);
			Check(State, !Squad->HasMultiSelection() && !Members[0]->IsSelectionRingShown() && !Members[1]->IsSelectionRingShown()
				&& !Members[2]->IsSelectionRingShown(), TEXT("a new leader drops the group and its rings"));
			FVector2D Min;
			FVector2D Max;
			Check(State, !PC->GetSelectionBox(Min, Max), TEXT("no box without a drag"));
			return Finish(State, true);
		}
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
		TEXT("CodexTactics.GroupSelectSmoke"),
		TEXT("Dev check: box selection rings, group move formation, leader switch drops the group; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
