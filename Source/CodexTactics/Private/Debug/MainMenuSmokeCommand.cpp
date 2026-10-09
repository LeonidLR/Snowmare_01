// Dev-only console command: the frontend main menu screens and the mission start modes (2026-10-08 frontend).
//   Scripts/smoke.ps1 -Command CodexTactics.MainMenuSmoke -Map /Game/Maps/L_MainMenu   (any start map works)
// 1. title -> main menu: six entries, CONTINUE off without saves (reason on the description line);
// 2. OPTIONS / CREDITS / LOAD GAME (empty slot list) open on the Menu layer and BACK returns; back on the main menu
//    returns to the title; QUIT asks first (NO keeps the menu, YES quits — quitting is disabled for the check);
// 3. NEW GAME: the level starts in Game mode; StartMission(Combat) (dev / bot) completes the quest chain, heals / warms
//    the squad and starts the cutscene; Ctrl + X (quick restart) repeats combat mode; a normal restart starts Game mode.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Debug/FrontendSmokeUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Quests/QuestSubsystem.h"
#include "UI/DialogueSubsystem.h"
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexFrontendPlayerController.h"
#include "UI/Frontend/CodexInfoScreens.h"
#include "UI/Frontend/CodexMainMenuScreen.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexSaveSlotsScreen.h"
#include "UI/Frontend/CodexTitleScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

namespace MainMenuSmoke
{
	constexpr float StepSeconds = 0.25f;

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
		FPlatformMisc::RequestExit(false, TEXT("MainMenuSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool TopIs(UCodexUISubsystem* UI, FGameplayTag Layer, FGameplayTag Screen)
	{
		const UCodexActivatableScreen* Top = UI->GetTopScreen(Layer);
		return Top && Top->GetScreenTag() == Screen;
	}

	/** Runs on the core ticker, so it survives the level loads. */
	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		if (State.Time > 120.f)
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
		const FGameplayTag Menu = CodexUITags::Layer_Menu;
		switch (State.Stage)
		{
		case 0:
			State.LastWorld = World;
			State.SaveDir = FPaths::ProjectSavedDir() / TEXT("SmokeSaves") / TEXT("MainMenu");
			IFileManager::Get().DeleteDirectory(*State.SaveDir, false, true);
			Frontend->SaveDirectoryOverride = State.SaveDir; // empty: no saves
			Frontend->bQuitDisabled = true;
			Next(State, FrontendSmokeUtils::EnsureFrontend(World) ? 1 : 100);
			return true;
		case 100: // waiting for the frontend map
			if (bNewWorld && UCodexFrontendSubsystem::IsFrontendWorld(World))
			{
				State.LastWorld = World;
				Next(State, 1);
			}
			return true;
		case 1: // Title -> main menu.
		{
			UCodexTitleScreen* Title = Cast<UCodexTitleScreen>(UI->GetTopScreen(Menu));
			if (!Title && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Title != nullptr, TEXT("title screen"));
			if (!Title)
			{
				return Finish(State, false);
			}
			Title->ContinueFromTitle();
			Next(State, 2);
			return true;
		}
		case 2: // The main menu and its sub-screens.
		{
			UCodexMainMenuScreen* MainMenu = Cast<UCodexMainMenuScreen>(UI->GetTopScreen(Menu));
			if (!MainMenu && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, MainMenu != nullptr, TEXT("main menu"));
			if (!MainMenu)
			{
				return Finish(State, false);
			}
			int32 Entries = 0;
			for (const ECodexMainMenuEntry Entry : { ECodexMainMenuEntry::Continue, ECodexMainMenuEntry::NewGame, ECodexMainMenuEntry::LoadGame,
				ECodexMainMenuEntry::Options, ECodexMainMenuEntry::Credits, ECodexMainMenuEntry::Quit })
			{
				Entries += MainMenu->FindEntryButton(CodexFrontendRules::GetEntryId(Entry)) ? 1 : 0;
			}
			Check(State, Entries == 6, FString::Printf(TEXT("six menu entries (%d)"), Entries));
			MainMenu->SelectMenuEntry(ECodexMainMenuEntry::Continue);
			Check(State, !MainMenu->IsEntryEnabled(ECodexMainMenuEntry::Continue) && MainMenu->GetDescriptionLine().ToString() == TEXT("No saved game yet."),
				FString::Printf(TEXT("CONTINUE off without saves (\"%s\")"), *MainMenu->GetDescriptionLine().ToString()));

			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::Options);
			Check(State, TopIs(UI, Menu, CodexUITags::Screen_Options), TEXT("OPTIONS opens the options placeholder"));
			if (UCodexActivatableScreen* Options = UI->GetTopScreen(Menu))
			{
				Options->ActivateEntry(TEXT("Back"));
			}
			Check(State, UI->GetTopScreen(Menu) == MainMenu, TEXT("BACK returns to the main menu"));

			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::Credits);
			UCodexCreditsScreen* Credits = Cast<UCodexCreditsScreen>(UI->GetTopScreen(Menu));
			Check(State, Credits && !Credits->GetCreditsText().IsEmpty(), TEXT("CREDITS lists the credits"));
			if (Credits)
			{
				Credits->RequestBack();
			}

			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::LoadGame);
			UCodexSaveSlotsScreen* Slots = Cast<UCodexSaveSlotsScreen>(UI->GetTopScreen(Menu));
			Check(State, Slots && Slots->GetMode() == ECodexSaveScreenMode::Load && Slots->GetSlotCount() == 0, TEXT("LOAD GAME: empty slot list"));
			if (Slots)
			{
				Slots->RequestBack();
			}
			Check(State, UI->GetTopScreen(Menu) == MainMenu, TEXT("back from LOAD GAME"));

			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::Quit);
			UCodexConfirmDialog* Confirm = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal));
			Check(State, Confirm && Confirm->GetConfirmType() == ECodexConfirmType::YesNo, TEXT("QUIT asks first"));
			if (Confirm)
			{
				Confirm->Cancel();
			}
			Check(State, !UI->GetTopScreen(CodexUITags::Layer_Modal) && UI->GetTopScreen(Menu) == MainMenu && Frontend->QuitRequests == 0, TEXT("NO keeps the menu"));
			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::Quit);
			if (UCodexConfirmDialog* Again = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal)))
			{
				Again->Confirm();
			}
			Check(State, Frontend->QuitRequests == 1, TEXT("YES quits the game"));

			MainMenu->RequestBack();
			Check(State, TopIs(UI, Menu, CodexUITags::Screen_Title), TEXT("back on the main menu: title screen again"));
			if (UCodexTitleScreen* Title = Cast<UCodexTitleScreen>(UI->GetTopScreen(Menu)))
			{
				Title->ContinueFromTitle();
			}
			Next(State, 3);
			return true;
		}
		case 3:
		{
			UCodexMainMenuScreen* MainMenu = Cast<UCodexMainMenuScreen>(UI->GetTopScreen(Menu));
			Check(State, MainMenu != nullptr, TEXT("title -> main menu again"));
			if (!MainMenu)
			{
				return Finish(State, false);
			}
			MainMenu->ActivateMenuEntry(ECodexMainMenuEntry::NewGame);
			Next(State, 4);
			return true;
		}
		case 4: // NEW GAME -> Game mode; then the dev / bot combat mode.
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			if (!bNewWorld || !Mission || !Mission->IsMissionWorld() || State.StageTime < 1.5f)
			{
				return true;
			}
			State.LastWorld = World;
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Mission->GetStartMode() == EMissionStartMode::Game && !UGameplayStatics::IsGamePaused(World), TEXT("NEW GAME: Game mode, world runs"));
			if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
			{
				Dialogue->SkipDialogue(); // the intro briefing of a frontend start
			}
			for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
			{
				Member->ColdLevel = 50.f;
				Member->HealthComponent->ApplyDirectHealthLoss(30.f, TEXT("Smoke"));
			}
			Mission->StartMission(EMissionStartMode::Combat);
			Check(State, World->GetSubsystem<UQuestSubsystem>()->IsGatePowered() && World->GetSubsystem<UQuestSubsystem>()->IsGeneratorRunning(),
				TEXT("combat mode: quest chain completed"));
			Check(State, Flow->GetPhase() == ECodexGamePhase::Cutscene, TEXT("combat mode: pre-combat cutscene"));
			bool bFresh = true;
			for (const AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
			{
				bFresh &= Member->ColdLevel == 0.f && Member->HealthComponent->GetCurrentHealth() >= Member->HealthComponent->GetMaxHealth();
			}
			Check(State, bFresh, TEXT("combat mode: squad healed and warmed"));
			Mission->RestartMission(/*bQuick*/ true);
			Next(State, 5);
			return true;
		}
		case 5: // After Ctrl + X: straight into combat mode.
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			if (!bNewWorld || !Mission || State.StageTime < 1.5f)
			{
				return true;
			}
			State.LastWorld = World;
			Check(State, Mission->GetStartMode() == EMissionStartMode::Combat, TEXT("quick restart repeats combat mode"));
			Check(State, World->GetSubsystem<UGameFlowSubsystem>()->GetPhase() == ECodexGamePhase::Cutscene, TEXT("quick restart: cutscene again"));
			Mission->RestartMission(/*bQuick*/ false);
			Next(State, 6);
			return true;
		}
		case 6: // After "Restart": Game mode at once (no in-level menu).
		{
			UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
			if (!bNewWorld || !Mission || State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Mission->GetStartMode() == EMissionStartMode::Game && !UGameplayStatics::IsGamePaused(World), TEXT("normal restart: Game mode"));
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
		TEXT("CodexTactics.MainMenuSmoke"),
		TEXT("Dev check: frontend main menu screens (continue / options / credits / load / quit / back) and the mission start modes; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
