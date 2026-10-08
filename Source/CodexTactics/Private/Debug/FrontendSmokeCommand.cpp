// Dev-only console command: the whole frontend flow headless (2026-10-08 user decision: professional frontend).
//   Scripts/smoke.ps1 -Command CodexTactics.FrontendSmoke -Map /Game/Maps/L_MainMenu   (any start map works: it opens L_MainMenu)
// 1. title screen on the Menu layer, camera on the "Title" anchor; any key -> main menu (CONTINUE off without saves);
// 2. selecting LOAD GAME / CREDITS shows the description and blends the camera to that entry's anchor;
// 3. NEW GAME opens the campaign level in Game mode with the intro briefing (frontend start);
// 4. Esc -> pause menu (world paused), SAVE GAME enabled in exploration: a slot is saved; RESUME;
// 5. in a wave fight SAVE GAME is disabled with "Saving is disabled during combat";
// 6. QUIT TO MAIN MENU asks first; YES opens the menu map straight at the main menu (CONTINUE on);
// 7. CONTINUE reopens the save's level and loads it (marker restored, no intro).
// Saves go to Saved/SmokeSaves/Frontend.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/PlayerCameraManager.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Debug/FrontendSmokeUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueSubsystem.h"
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexSaveBridge.h"
#include "UI/Frontend/CodexFrontendPlayerController.h"
#include "UI/Frontend/CodexMainMenuScreen.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexMenuCameraAnchor.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexTitleScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

namespace FrontendSmoke
{
	constexpr float StepSeconds = 0.25f;
	const TCHAR* SlotName = TEXT("Frontend_01");
	constexpr int32 MarkerMedkits = 4;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<UWorld> LastWorld;
		FString SaveDir;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (!State.SaveDir.IsEmpty())
		{
			IFileManager::Get().DeleteDirectory(*State.SaveDir, false, true);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FrontendSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	bool CameraAt(ACodexFrontendPlayerController* PC, FName AnchorId)
	{
		const ACodexMenuCameraAnchor* Anchor = PC ? PC->FindAnchor(AnchorId) : nullptr;
		return Anchor && PC->PlayerCameraManager && PC->GetViewTarget() == Anchor
			&& FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Anchor->GetActorLocation()) < 5.f;
	}

	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 150.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (!World || State.StageTime < 0.5f)
		{
			return true;
		}
		const bool bNewWorld = World != State.LastWorld.Get();
		UCodexUISubsystem* UI = UCodexUISubsystem::Get(World);
		UCodexFrontendSubsystem* Frontend = UCodexFrontendSubsystem::Get(World);
		if (!UI || !Frontend)
		{
			return true;
		}
		ACodexFrontendPlayerController* MenuPC = Cast<ACodexFrontendPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		switch (State.Stage)
		{
		case 0: // Reach the frontend map; isolate the saves.
			State.LastWorld = World;
			State.SaveDir = FPaths::ProjectSavedDir() / TEXT("SmokeSaves") / TEXT("Frontend");
			IFileManager::Get().DeleteDirectory(*State.SaveDir, false, true);
			Frontend->SaveDirectoryOverride = State.SaveDir;
			Frontend->bQuitDisabled = true;
			if (FrontendSmokeUtils::EnsureFrontend(World))
			{
				Next(State);
			}
			else
			{
				State.Stage = 100; // wait for the frontend world
			}
			return true;
		case 100:
			if (bNewWorld && UCodexFrontendSubsystem::IsFrontendWorld(World))
			{
				State.LastWorld = World;
				State.Stage = 1;
				State.StageTime = 0.f;
			}
			return true;
		case 1: // Title screen + title camera.
		{
			UCodexTitleScreen* Title = Cast<UCodexTitleScreen>(UI->GetTopScreen(CodexUITags::Layer_Menu));
			if (!Title && State.StageTime < 6.f)
			{
				return true; // async class load
			}
			Check(State, Title != nullptr, TEXT("title screen on the Menu layer"));
			Check(State, MenuPC && MenuPC->GetCurrentAnchorId() == TEXT("Title") && CameraAt(MenuPC, TEXT("Title")), TEXT("camera on the Title anchor"));
			if (!Title)
			{
				return Finish(State, false);
			}
			Title->ContinueFromTitle(); // = any key
			Next(State);
			return true;
		}
		case 2: // Main menu.
		{
			UCodexMainMenuScreen* Menu = Cast<UCodexMainMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_Menu));
			if (!Menu && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Menu != nullptr && UI->GetScreenCount(CodexUITags::Layer_Menu) == 1, TEXT("any key: main menu replaces the title"));
			if (!Menu)
			{
				return Finish(State, false);
			}
			Check(State, !Menu->IsEntryEnabled(ECodexMainMenuEntry::Continue) && Menu->IsEntryEnabled(ECodexMainMenuEntry::NewGame)
				&& Menu->IsEntryEnabled(ECodexMainMenuEntry::LoadGame), TEXT("CONTINUE off without saves, NEW GAME / LOAD GAME on"));
			Menu->SelectMenuEntry(ECodexMainMenuEntry::LoadGame);
			const UCodexMenuButton* LoadButton = Menu->FindEntryButton(TEXT("LoadGame"));
			Check(State, LoadButton && Menu->GetDescriptionLine().EqualTo(LoadButton->DescriptionText) && !Menu->GetDescriptionLine().IsEmpty(),
				FString::Printf(TEXT("description line follows the selection (\"%s\")"), *Menu->GetDescriptionLine().ToString()));
			Check(State, MenuPC && MenuPC->GetCurrentAnchorId() == TEXT("LoadGame") && !CameraAt(MenuPC, TEXT("LoadGame")),
				TEXT("selection starts a blend to the LoadGame anchor"));
			Next(State);
			return true;
		}
		case 3: // After the blend time the camera sits on the anchor; select CREDITS, open / close it.
		{
			if (State.StageTime < UCodexFrontendSettings::Get().CameraBlendTime + 0.5f)
			{
				return true;
			}
			Check(State, CameraAt(MenuPC, TEXT("LoadGame")), TEXT("camera arrived at the LoadGame anchor"));
			UCodexMainMenuScreen* Menu = Cast<UCodexMainMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_Menu));
			if (!Menu)
			{
				return Finish(State, false);
			}
			Menu->SelectMenuEntry(ECodexMainMenuEntry::Credits);
			Check(State, MenuPC->GetCurrentAnchorId() == TEXT("Credits"), TEXT("CREDITS selected: Credits anchor"));
			Menu->ActivateMenuEntry(ECodexMainMenuEntry::Credits);
			Check(State, UI->GetTopScreen(CodexUITags::Layer_Menu) && UI->GetTopScreen(CodexUITags::Layer_Menu)->GetScreenTag() == CodexUITags::Screen_Credits,
				TEXT("CREDITS opens the credits screen"));
			if (UCodexActivatableScreen* Credits = UI->GetTopScreen(CodexUITags::Layer_Menu))
			{
				Credits->RequestBack();
			}
			Check(State, UI->GetTopScreen(CodexUITags::Layer_Menu) == Menu, TEXT("back: main menu again"));
			Menu->ActivateMenuEntry(ECodexMainMenuEntry::NewGame);
			Next(State);
			return true;
		}
		case 4: // The campaign level, started by NEW GAME.
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			if (!bNewWorld || !Mission || !Mission->IsMissionWorld())
			{
				return true;
			}
			if (State.StageTime < 2.f)
			{
				return true;
			}
			State.LastWorld = World;
			UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Check(State, Mission->GetStartMode() == EMissionStartMode::Game && !UGameplayStatics::IsGamePaused(World),
				TEXT("NEW GAME: the level starts in Game mode at once (no in-level menu)"));
			Check(State, Dialogue && Dialogue->IsDialogueOpen(), TEXT("NEW GAME plays the intro briefing"));
			if (Dialogue && Dialogue->IsDialogueOpen())
			{
				Dialogue->SkipDialogue();
			}
			Next(State);
			return true;
		}
		case 5: // Pause menu, save outside combat.
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
			AOperativeCharacter* Commander = World->GetSubsystem<USquadSubsystem>()->GetLeader();
			if (!Hud || !Commander)
			{
				Check(State, false, TEXT("HUD and commander in the level"));
				return Finish(State, false);
			}
			Check(State, Hud->HandleEscape() && UGameplayStatics::IsGamePaused(World), TEXT("Esc: pause menu, world paused"));
			UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
			Check(State, Pause && Pause->IsSaveEnabled(), TEXT("SAVE GAME enabled in exploration"));
			if (!Pause)
			{
				return Finish(State, false);
			}
			Pause->ActivateEntry(TEXT("Save"));
			UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
			if (Slots)
			{
				Commander->MedkitsCount = MarkerMedkits;
				Slots->SetSlotName(SlotName);
				Slots->PressSave();
				Slots->RequestBack();
			}
			Check(State, Frontend && CodexSaveBridge::SlotExists(World, SlotName), TEXT("saved from the pause menu"));
			Pause->Resume();
			Check(State, !Hud->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(World), TEXT("RESUME closes the menu, world runs"));
			Commander->MedkitsCount = 0;
			// Into a wave fight.
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			Check(State, Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("wave fight started"));
			Next(State);
			return true;
		}
		case 6: // Save disabled in combat; quit to the main menu (confirm).
		{
			APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
			ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
			if (!Hud)
			{
				return Finish(State, false);
			}
			Hud->HandleEscape();
			UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
			Check(State, Pause && !Pause->IsSaveEnabled() && Pause->GetSaveHint().ToString() == TEXT("Saving is disabled during combat"),
				FString::Printf(TEXT("combat: SAVE GAME disabled (\"%s\")"), Pause ? *Pause->GetSaveHint().ToString() : TEXT("-")));
			if (!Pause)
			{
				return Finish(State, false);
			}
			Pause->SelectEntry(TEXT("Save"));
			Check(State, Pause->GetDescriptionLine().ToString() == TEXT("Saving is disabled during combat"), TEXT("the description line shows the reason"));
			Pause->ActivateEntry(TEXT("QuitToMenu"));
			UCodexConfirmDialog* Confirm = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal));
			Check(State, Confirm != nullptr, TEXT("QUIT TO MAIN MENU asks first"));
			if (Confirm)
			{
				Confirm->Cancel();
				Check(State, !UI->GetTopScreen(CodexUITags::Layer_Modal) && UI->GetTopScreen(CodexUITags::Layer_GameMenu) == Pause, TEXT("NO keeps the pause menu"));
				Pause->ActivateEntry(TEXT("QuitToMenu"));
				if (UCodexConfirmDialog* Again = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal)))
				{
					Again->Confirm();
				}
			}
			Next(State);
			return true;
		}
		case 7: // Back on the menu map, straight at the main menu.
		{
			if (!bNewWorld || !UCodexFrontendSubsystem::IsFrontendWorld(World))
			{
				return true;
			}
			UCodexMainMenuScreen* Menu = Cast<UCodexMainMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_Menu));
			if (!Menu && State.StageTime < 8.f)
			{
				return true;
			}
			State.LastWorld = World;
			Check(State, Menu != nullptr, TEXT("QUIT TO MAIN MENU: main menu without the title screen"));
			Check(State, Menu && Menu->IsEntryEnabled(ECodexMainMenuEntry::Continue), TEXT("CONTINUE enabled with a save"));
			if (!Menu)
			{
				return Finish(State, false);
			}
			Menu->ActivateMenuEntry(ECodexMainMenuEntry::Continue);
			Next(State);
			return true;
		}
		case 8: // CONTINUE: the save's level, the save applied, no intro.
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			if (!bNewWorld || !Mission || !Mission->IsMissionWorld())
			{
				return true;
			}
			if (State.StageTime < 2.5f)
			{
				return true; // the save subsystem applies the pending slot 0.5 s after the start
			}
			const AOperativeCharacter* Commander = World->GetSubsystem<USquadSubsystem>()->GetLeader();
			const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Check(State, Commander && Commander->MedkitsCount == MarkerMedkits,
				FString::Printf(TEXT("CONTINUE loaded the latest save (medkits %d)"), Commander ? Commander->MedkitsCount : -1));
			Check(State, Dialogue && !Dialogue->IsDialogueOpen(), TEXT("no intro briefing after a load"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FrontendSmoke"),
		TEXT("Dev check: title -> main menu -> camera anchors -> NEW GAME -> pause / save -> save disabled in combat -> quit to menu -> CONTINUE; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
