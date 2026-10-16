// Dev-only console command for a headless pause menu check on L_MovementTest (frontend framework, 2026-10-08):
//   Scripts/smoke.ps1 -Command CodexTactics.PauseMenuSmoke
// Slots go to Saved/SmokeSaves. Esc opens the pause menu (UCodexPauseMenuScreen on the GameMenu layer; world paused,
// LOAD GAME disabled without saves); SAVE GAME opens the slot screen with «Leonid_01» suggested; saving adds a slot;
// the same name asks for the overwrite confirmation (Esc closes only the dialog, YES overwrites); Esc goes back to the
// pause menu; LOAD GAME + a slot restores the squad and closes everything (world runs); a slot is deleted (confirm);
// Esc closes the menu (behaviour of Godot pause_menu_dialog.gd, save_load_dialog.gd, main.gd KEY_ESCAPE).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexFrontendSubsystem.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexSaveBridge.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

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
		UCodexUISubsystem* UI = UCodexUISubsystem::Get(World);
		UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(World);
		AOperativeCharacter* Commander = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Hud || !UI || !Frontend || !Commander)
		{
			Check(State, false, TEXT("HUD, UI subsystem, frontend subsystem, commander"));
			return Finish(State, false);
		}
		Frontend->SaveDirectoryOverride = FPaths::ProjectSavedDir() / TEXT("SmokeSaves");
		IFileManager::Get().DeleteDirectory(*Frontend->SaveDirectoryOverride, false, true);

		auto Top = [UI](FGameplayTag Layer) { return UI->GetTopScreen(Layer); };
		Check(State, Hud->HandleEscape() && UGameplayStatics::IsGamePaused(World), TEXT("Esc: pause menu, world paused"));
		UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(Top(CodexUITags::Layer_GameMenu));
		if (!Pause)
		{
			Check(State, false, TEXT("pause screen on the GameMenu layer"));
			return Finish(State, false);
		}
		Check(State, !Pause->IsLoadEnabled() && Pause->IsSaveEnabled() && Pause->GetStatusLine().ToString() == TEXT("No saved games"),
			FString::Printf(TEXT("LOAD off, SAVE on outside combat, status \"%s\""), *Pause->GetStatusLine().ToString()));

		Pause->ActivateEntry(TEXT("Save"));
		UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(Top(CodexUITags::Layer_GameMenu));
		if (!Slots)
		{
			Check(State, false, TEXT("SAVE GAME opens the slot screen"));
			return Finish(State, false);
		}
		Check(State, Slots->GetMode() == ECodexSaveScreenMode::Save && UGameplayStatics::IsGamePaused(World), TEXT("slot screen in save mode, still paused"));
		Check(State, Slots->GetSlotName() == TEXT("Leonid_01") && Slots->GetSaveButtonLabel().ToString() == TEXT("SAVE"),
			FString::Printf(TEXT("suggested %s / %s"), *Slots->GetSlotName(), *Slots->GetSaveButtonLabel().ToString()));
		Commander->MedkitsCount = 2;
		Slots->PressSave();
		Check(State, Slots->GetSlotCount() == 1 && Slots->GetStatusLine().ToString().Contains(TEXT("Game saved to slot")), Slots->GetStatusLine().ToString());
		Slots->SetSlotName(TEXT("Leonid_01"));
		Check(State, Slots->GetSaveButtonLabel().ToString() == TEXT("OVERWRITE"), TEXT("existing name: OVERWRITE"));
		Slots->PressSave();
		Check(State, Cast<UCodexConfirmDialog>(Top(CodexUITags::Layer_Modal)) != nullptr, TEXT("overwrite asks for confirmation"));
		Hud->HandleEscape();
		Check(State, !Top(CodexUITags::Layer_Modal) && Top(CodexUITags::Layer_GameMenu) == Slots, TEXT("Esc closes the confirmation only"));
		Slots->PressSave();
		if (UCodexConfirmDialog* Confirm = Cast<UCodexConfirmDialog>(Top(CodexUITags::Layer_Modal)))
		{
			Confirm->Confirm();
		}
		Check(State, Slots->GetStatusLine().ToString().Contains(TEXT("overwritten")) && Slots->GetSlotCount() == 1, Slots->GetStatusLine().ToString());
		Hud->HandleEscape();
		Pause = Cast<UCodexPauseMenuScreen>(Top(CodexUITags::Layer_GameMenu));
		Check(State, Pause && Pause->IsLoadEnabled() && Pause->GetStatusLine().ToString().StartsWith(TEXT("Latest save: Leonid_01")),
			FString::Printf(TEXT("Esc: back to the pause menu, LOAD on (%s)"), Pause ? *Pause->GetStatusLine().ToString() : TEXT("-")));
		if (!Pause)
		{
			return Finish(State, false);
		}

		Commander->MedkitsCount = 7;
		Pause->ActivateEntry(TEXT("Load"));
		Slots = Cast<UCodexSaveSlotsScreen>(Top(CodexUITags::Layer_GameMenu));
		Check(State, Slots && Slots->GetMode() == ECodexSaveScreenMode::Load, TEXT("LOAD GAME opens the slot screen in load mode"));
		if (Slots)
		{
			Slots->ChooseSlot(TEXT("Leonid_01"));
		}
		Check(State, !Hud->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(World) && Commander->MedkitsCount == 2,
			FString::Printf(TEXT("load: menus closed, world runs, medkits back (%d)"), Commander->MedkitsCount));

		Hud->HandleEscape();
		Pause = Cast<UCodexPauseMenuScreen>(Top(CodexUITags::Layer_GameMenu));
		if (Pause)
		{
			Pause->ActivateEntry(TEXT("Save"));
		}
		Slots = Cast<UCodexSaveSlotsScreen>(Top(CodexUITags::Layer_GameMenu));
		if (Slots)
		{
			Slots->ChooseSlot(TEXT("Leonid_01")); // save mode: picks the slot
			Slots->PressDelete();
			if (UCodexConfirmDialog* Confirm = Cast<UCodexConfirmDialog>(Top(CodexUITags::Layer_Modal)))
			{
				Confirm->Confirm();
			}
		}
		Check(State, Slots && Slots->GetSlotCount() == 0 && !CodexSaveBridge::SlotExists(World, TEXT("Leonid_01")), TEXT("slot deleted after the confirmation"));
		Hud->HandleEscape(); // slot screen -> pause menu
		Hud->HandleEscape(); // pause menu closes
		Check(State, !Hud->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(World), TEXT("Esc closes the pause menu, world runs"));
		IFileManager::Get().DeleteDirectory(*Frontend->SaveDirectoryOverride, false, true);
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
		TEXT("Dev check: Esc pause menu (frontend framework), save slot screen (suggested name, overwrite confirmation), load, delete; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
