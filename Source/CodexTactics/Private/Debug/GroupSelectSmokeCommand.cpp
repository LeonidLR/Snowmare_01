// Dev-only console command for a headless check of box selection and group orders on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.GroupSelectSmoke
// Godot main.gd _perform_box_selection / _set_selected_squad / _get_group_target_positions, player.gd set_group_selected:
// rings under the selected and always under the leader (Sprint 06-A), the group walks to its formation around the click,
// picking a leader by number drops the group. In the fight's tactical pause a screen box selects all three and a ground
// click plans a move for each; all of them walk once the pause ends.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "EngineUtils.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/SquadFormation.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
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
		TArray<FVector> PauseStarts;
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
			if (const UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Check(State, Feedback->GetMovePingCount() == 3, FString::Printf(TEXT("a move ping at each operative's target (Sprint 06-B): %d"),
					Feedback->GetMovePingCount()));
			}
			const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
			Check(State, Messages && !Messages->GetHistory().IsEmpty()
				&& Messages->GetHistory().Last().Text.ToString().Contains(TEXT("Group (3 operatives)")), TEXT("group order line"));
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
			// Sprint 06-A: the active leader always keeps his gold ring.
			Check(State, !Squad->HasMultiSelection() && !Members[0]->IsSelectionRingShown() && !Members[1]->IsSelectionRingShown()
				&& Members[2]->IsSelectionRingShown(), TEXT("a new leader drops the group's rings, keeps his own"));
			FVector2D Min;
			FVector2D Max;
			Check(State, !PC->GetSelectionBox(Min, Max), TEXT("no box without a drag"));
			// The fight: a real-time wave (its enemies parked far away), then the tactical pause.
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
				TEXT("tactical pause"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// A box over the whole screen selects all three; a click on the ground plans a move for each of them.
			int32 SizeX = 0;
			int32 SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			// Headless there is no viewport to project into: then the box result is set directly.
			const int32 Boxed = SizeX > 0 ? PC->SelectInBox(FVector2D::ZeroVector, FVector2D(SizeX, SizeY)) : 0;
			if (SizeX <= 0)
			{
				Squad->SetSelectedGroup({ Members[0], Members[1], Members[2] });
			}
			Check(State, (SizeX <= 0 || Boxed == 3) && Squad->HasMultiSelection(), FString::Printf(TEXT("pause: all three selected (box %d, viewport %dx%d)"), Boxed, SizeX, SizeY));
			FVector Average = FVector::ZeroVector;
			for (const AOperativeCharacter* Member : Members)
			{
				Average += Member->GetActorLocation() / Members.Num();
				State.PauseStarts.Add(Member->GetActorLocation());
			}
			FHitResult Hit;
			// The click lands right next to the engineer: with a group selected that is still the group's move order.
			Hit.ImpactPoint = Hit.Location = Members[1]->GetActorLocation() + Members[1]->GetActorForwardVector() * 80.f;
			PC->HandleWorldHit(Hit);
			Check(State, Squad->GetPlannedOrderCount() == 3 && Squad->HasMultiSelection(),
				FString::Printf(TEXT("pause: the click planned %d moves, group kept %d"), Squad->GetPlannedOrderCount(), Squad->HasMultiSelection() ? 1 : 0));
			// A second click 5 m ahead replaces the plans: all three must walk once the pause ends.
			FHitResult Far;
			Far.ImpactPoint = Far.Location = Average + Squad->GetLeader()->GetActorForwardVector() * 500.f;
			PC->HandleWorldHit(Far);
			Flow->ToggleTacticalPause();
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if (State.StageTime < 4.f)
			{
				return true;
			}
			for (int32 Index = 0; Index < Members.Num() && Index < State.PauseStarts.Num(); ++Index)
			{
				const float Moved = FVector::Dist2D(Members[Index]->GetActorLocation(), State.PauseStarts[Index]);
				Check(State, Moved > 150.f, FString::Printf(TEXT("after the pause %s moved %.0f cm"), *Members[Index]->DisplayName.ToString(), Moved));
			}
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
