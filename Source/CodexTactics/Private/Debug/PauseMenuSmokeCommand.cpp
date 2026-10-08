// Dev-only console command for a headless pause menu / save-load dialog check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.PauseMenuSmoke
// Slots go to Saved/SmokeSaves. Esc opens the pause menu (world paused, "Load" disabled without saves);
// "Save" opens the dialog with «Leonid_01» suggested; saving adds a card; the same name asks for the overwrite
// confirmation (Esc closes it, «Да» overwrites); Esc goes back to the pause menu; "Load" + a slot restores the
// squad and closes everything (world runs); a slot is deleted; Esc toggles the menu off
// (Godot pause_menu_dialog.gd, save_load_dialog.gd, main.gd KEY_ESCAPE / save / load handlers).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/PauseMenuWidget.h"
#include "UI/SaveLoadDialogWidget.h"

namespace PauseMenuSmoke
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
		FPlatformMisc::RequestExit(false, TEXT("PauseMenuSmoke"));
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
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UPauseMenuWidget* Pause = Hud ? Hud->GetPauseMenu() : nullptr;
		USaveLoadDialogWidget* Dialog = Hud ? Hud->GetSaveLoadDialog() : nullptr;
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		AOperativeCharacter* Commander = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Pause || !Dialog || !Saves || !Commander)
		{
			Check(State, false, TEXT("pause menu, dialog, saves, commander"));
			return Finish(State, false);
		}
		Saves->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves");
		IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);

		Check(State, Hud->HandleEscape() && Pause->IsOpen() && UGameplayStatics::IsGamePaused(World), TEXT("Esc: pause menu open, world paused"));
		Check(State, !Pause->IsLoadEnabled() && Pause->GetStatusText().ToString() == TEXT("No saved data"), Pause->GetStatusText().ToString());

		Pause->OpenSave();
		Check(State, Dialog->IsOpen() && !Pause->IsOpen() && UGameplayStatics::IsGamePaused(World), TEXT("\"Save\" opens the dialog, still paused"));
		Check(State, Dialog->GetTitleText().ToString().Contains(TEXT("SAVE AND OVERWRITE")) && Dialog->GetSlotNameText() == TEXT("Leonid_01")
			&& Dialog->GetSaveButtonText().ToString().Contains(TEXT("Save")), FString::Printf(TEXT("title / suggested %s / button"), *Dialog->GetSlotNameText()));
		Commander->MedkitsCount = 2;
		Dialog->PressSave();
		Check(State, Dialog->GetCardCount() == 1 && Dialog->GetStatusText().ToString().Contains(TEXT("Game saved to slot")), Dialog->GetStatusText().ToString());
		Dialog->SetSlotNameText(TEXT("Leonid_01"));
		Check(State, Dialog->GetSaveButtonText().ToString().Contains(TEXT("Overwrite")), TEXT("existing name: \"Overwrite\""));
		Dialog->PressSave();
		Check(State, Dialog->IsConfirmOpen(), TEXT("overwrite asks for confirmation"));
		Hud->HandleEscape();
		Check(State, !Dialog->IsConfirmOpen() && Dialog->IsOpen(), TEXT("Esc closes the confirmation only"));
		Dialog->PressSave();
		Dialog->ConfirmOverwrite();
		Check(State, Dialog->GetStatusText().ToString().Contains(TEXT("overwritten")) && Dialog->GetCardCount() == 1, Dialog->GetStatusText().ToString());
		Hud->HandleEscape();
		Check(State, !Dialog->IsOpen() && Pause->IsOpen() && Pause->IsLoadEnabled(), TEXT("Esc: back to the pause menu, \"Load\" enabled"));
		Check(State, Pause->GetStatusText().ToString().StartsWith(TEXT("Slot: Leonid_01")), Pause->GetStatusText().ToString());

		Commander->MedkitsCount = 7;
		Pause->OpenLoad();
		Check(State, Dialog->GetTitleText().ToString().Contains(TEXT("LOAD GAME")), Dialog->GetTitleText().ToString());
		Dialog->LoadSlot(TEXT("Leonid_01"));
		Check(State, !Dialog->IsOpen() && !Pause->IsOpen() && !UGameplayStatics::IsGamePaused(World) && Commander->MedkitsCount == 2,
			TEXT("load: windows closed, world runs, medkits back"));

		Hud->HandleEscape();
		Pause->OpenSave();
		Dialog->DeleteSlot(TEXT("Leonid_01"));
		Check(State, Dialog->GetCardCount() == 0 && !Saves->HasSave(TEXT("Leonid_01")), TEXT("slot deleted"));
		Hud->HandleEscape(); // dialog -> pause menu
		Hud->HandleEscape(); // pause menu closes
		Check(State, !Pause->IsOpen() && !UGameplayStatics::IsGamePaused(World), TEXT("Esc closes the pause menu, world runs"));
		IFileManager::Get().DeleteDirectory(*Saves->SaveDirectoryOverride, false, true);
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
		TEXT("CodexTactics.PauseMenuSmoke"),
		TEXT("Dev check: Esc pause menu, save dialog (suggested name, overwrite confirmation), load, delete; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
