// Dev-only console command for a headless item hand-over check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TransferSmoke
// «ПЕРЕД» opens the dialog (leader title, lines; the inventory drawer closes it); the medkit line starts the hand-over
// mode (ring shown); a click on the commander himself is refused, a click on the engineer hands it over; an M16 pack
// goes the same way; RMB cancels the mode (Godot transfer_dialog.gd, main.gd _start_transfer_mode,
// _handle_transfer_click, _transfer_item_to_target).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/InventoryDrawerWidget.h"
#include "UI/TransferDialogWidget.h"

namespace TransferSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TransferSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		AOperativeCharacter* Engineer = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Engineer = Member->SquadRole == EOperativeRole::Engineer ? Member : Engineer;
		}
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UTransferDialogWidget* Dialog = Hud ? Hud->GetTransferDialog() : nullptr;
		USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
		if (!Leader || !Engineer || !Dialog || !Transfer)
		{
			Check(State, false, TEXT("squad, dialog, transfer subsystem"));
			return Finish(State, false);
		}

		Hud->ToggleTransferDialog();
		Check(State, Dialog->IsOpen(), TEXT("«ПЕРЕД» opens the dialog"));
		Check(State, Dialog->GetTitleText().ToString().Contains(TEXT("ПЕРЕДАЧА: КОМАНДИР")), Dialog->GetTitleText().ToString());
		Check(State, Dialog->GetItemText(ETransferItem::RifleAmmo).ToString().Contains(TEXT("M16 [30 шт.] (Запас: 60)")),
			Dialog->GetItemText(ETransferItem::RifleAmmo).ToString());
		Check(State, !Dialog->IsItemVisible(ETransferItem::PlasmaAmmo), TEXT("plasma line hidden without plasma"));
		Hud->ToggleInventoryDrawer();
		Check(State, !Dialog->IsOpen() && Hud->GetInventoryDrawer()->IsOpen(), TEXT("the drawer closes the dialog"));
		Hud->ToggleInventoryDrawer();

		Hud->ToggleTransferDialog();
		Leader->MedkitsCount = FMath::Max(1, Leader->MedkitsCount);
		const int32 LeaderMedkits = Leader->MedkitsCount;
		const int32 EngineerMedkits = Engineer->MedkitsCount;
		Dialog->Choose(ETransferItem::Medkit);
		Check(State, !Dialog->IsOpen() && Transfer->IsTransferring() && Transfer->GetCursor() && !Transfer->GetCursor()->IsHidden(),
			TEXT("medkit chosen: hand-over mode with the ring"));
		Check(State, !Transfer->HandleClick(Leader->GetActorLocation(), Leader) && Transfer->IsTransferring(), TEXT("a click on himself is refused"));
		Check(State, Transfer->HandleClick(Engineer->GetActorLocation(), Engineer) && !Transfer->IsTransferring()
			&& Leader->MedkitsCount == LeaderMedkits - 1 && Engineer->MedkitsCount == EngineerMedkits + 1, TEXT("a click on the engineer hands it over"));

		Transfer->StartTransferMode(ETransferItem::RifleAmmo);
		const int32 EngineerReserve = Engineer->ReserveAmmo;
		Check(State, Transfer->HandleClick(Engineer->GetActorLocation() + FVector(100.f, 0.f, 0.f), nullptr)
			&& Leader->ReserveAmmo == 30 && Engineer->ReserveAmmo == EngineerReserve + 30, TEXT("click near the engineer (2.2 m): M16 pack of 30"));

		Transfer->StartTransferMode(ETransferItem::Medkit);
		Transfer->CancelTransferMode(); // what RMB / Esc call
		Check(State, !Transfer->IsTransferring() && Transfer->GetCursor()->IsHidden(), TEXT("cancel: mode off, ring hidden"));
		return Finish(State, true);
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
		TEXT("CodexTactics.TransferSmoke"),
		TEXT("Dev check: hand-over dialog, mode, clicks (self refused, mate, near mate), Esc cancel; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
