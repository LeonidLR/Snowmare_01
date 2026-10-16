// Dev-only console command for a headless check of the F1 Controls reference on L_MovementTest (2026-10-11):
//   Scripts/smoke.ps1 -Command CodexTactics.ControlsScreenSmoke
// F1 (the real key path: PlayerController::InputKey) opens the screen on the GameMenu layer and pauses the world; its sections
// and rows come from the PlayerCommands registry; F1 again or Esc closes it and resumes; the tactical pause keeps running
// underneath; in the Esc pause menu CONTROLS opens it over the menu (BACK returns to the menu, F1 toggles it) and
// TUTORIAL: ON / OFF flips the player's tutorial setting (restored at the end). UE-only (no Godot reference).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/Frontend/CodexControlsScreen.h"
#include "UI/Frontend/CodexMenuButton.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"

namespace ControlsScreenSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bTutorialWas = true;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		if (World)
		{
			UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
			if (Tutorial && UTutorialSubsystem::IsTutorialEnabled() != State.bTutorialWas)
			{
				Tutorial->SetTutorialEnabled(State.bTutorialWas); // the profile file stays as the tester had it
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("ControlsScreenSmoke"));
		return false;
	}

	void Tap(APlayerController* PC, const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	}

	UCodexControlsScreen* Controls(UCodexUISubsystem* UI)
	{
		return Cast<UCodexControlsScreen>(UI->FindScreen(CodexUITags::Screen_Controls));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		State.StageTime += StepSeconds;
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		UCodexUISubsystem* UI = UCodexUISubsystem::Get(World);
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		if (!Hud || !UI || !Flow || !Tutorial)
		{
			Check(State, false, TEXT("HUD, UI subsystem, game flow, tutorial subsystem"));
			return Finish(State, World, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		auto TimedOut = [&State](const TCHAR* What)
		{
			if (State.StageTime > 4.f)
			{
				Check(State, false, FString::Printf(TEXT("timeout: %s"), What));
				return true;
			}
			return false;
		};
		const bool bPaused = UGameplayStatics::IsGamePaused(World);

		switch (State.Stage)
		{
		case 0:
			State.bTutorialWas = UTutorialSubsystem::IsTutorialEnabled();
			Check(State, !bPaused && !Controls(UI), TEXT("start: running, no controls screen"));
			Tap(PC, EKeys::F1);
			Next();
			break;
		case 1:
			if (UCodexControlsScreen* Screen = Controls(UI))
			{
				const TArray<FString> Expected = { TEXT("Movement & camera"), TEXT("Squad"), TEXT("Stances"), TEXT("Combat"),
					TEXT("Tactical pause & turn-based"), TEXT("Items & traps"), TEXT("Vehicles"), TEXT("Saving & menus") };
				Check(State, bPaused && Screen->PausedTheWorld(), TEXT("F1 opened the screen and paused the world"));
				Check(State, UI->GetTopScreen(CodexUITags::Layer_GameMenu) == Screen, TEXT("on the GameMenu layer"));
				Check(State, Screen->GetSectionTitles() == Expected, FString::Printf(TEXT("sections: %s"), *FString::Join(Screen->GetSectionTitles(), TEXT(" | "))));
				Check(State, Screen->GetRowCount() == PlayerCommands::GetRegistry().Num(),
					FString::Printf(TEXT("%d rows = %d registry commands"), Screen->GetRowCount(), PlayerCommands::GetRegistry().Num()));
				Tap(PC, EKeys::F1);
				Next();
			}
			else if (TimedOut(TEXT("F1 did not open the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 2:
			if (!Controls(UI))
			{
				Check(State, !bPaused, TEXT("F1 again closed it and resumed the world"));
				Tap(PC, EKeys::F1);
				Next();
			}
			else if (TimedOut(TEXT("F1 did not close the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 3:
			if (Controls(UI))
			{
				Tap(PC, EKeys::Escape);
				Next();
			}
			else if (TimedOut(TEXT("second F1 did not open the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 4:
			if (!Controls(UI))
			{
				Check(State, !bPaused && !Hud->IsPauseMenuOpen(), TEXT("Esc closed it, world runs, no pause menu opened"));
				// The fight: a real-time wave (its enemies parked far away), then the tactical pause.
				SmokeUtils::StartWaveFight(Flow);
				for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
				{
					It->CustomTimeDilation = 0.f;
					It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
				Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
					TEXT("tactical pause on"));
				Tap(PC, EKeys::F1);
				Next();
			}
			else if (TimedOut(TEXT("Esc did not close the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 5:
			if (Controls(UI))
			{
				Check(State, bPaused && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("F1 works in the tactical pause (world paused on top)"));
				Tap(PC, EKeys::F1);
				Next();
			}
			else if (TimedOut(TEXT("F1 in the tactical pause did not open the screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 6:
			if (!Controls(UI))
			{
				Check(State, !bPaused && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("closed: the tactical pause is still on, the world pause is off"));
				Flow->ToggleTacticalPause();
				Check(State, Hud->HandleEscape() && UGameplayStatics::IsGamePaused(World), TEXT("Esc: pause menu, world paused"));
				Next();
			}
			else if (TimedOut(TEXT("F1 did not close the controls screen in the tactical pause")))
			{
				return Finish(State, World, false);
			}
			break;
		case 7:
		{
			UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
			if (!Pause)
			{
				if (TimedOut(TEXT("pause menu")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			Check(State, Pause->FindEntryButton(TEXT("Controls")) && Pause->FindEntryButton(TEXT("Tutorial")) && Pause->FindEntryButton(TEXT("ResetTutorial")),
				TEXT("pause menu lists CONTROLS, TUTORIAL and RESET TUTORIAL"));
			Pause->ActivateEntry(TEXT("Controls"));
			Next();
			break;
		}
		case 8:
			if (UCodexControlsScreen* Screen = Controls(UI))
			{
				Check(State, bPaused && !Screen->PausedTheWorld() && UI->GetTopScreen(CodexUITags::Layer_GameMenu) == Screen, TEXT("pause menu CONTROLS opens the screen over the menu"));
				Hud->HandleEscape(); // back
				Next();
			}
			else if (TimedOut(TEXT("pause menu CONTROLS did not open the screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 9:
			if (!Controls(UI))
			{
				UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
				Check(State, Pause && bPaused, TEXT("BACK returns to the pause menu, still paused"));
				if (Pause)
				{
					Check(State, Pause->HandleHotkey(EKeys::F1), TEXT("F1 with the menu focused opens the controls (hotkey path)"));
				}
				Next();
			}
			else if (TimedOut(TEXT("BACK did not close the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 10:
			if (UCodexControlsScreen* Screen = Controls(UI))
			{
				Check(State, Screen->HandleHotkey(EKeys::F1), TEXT("F1 with the controls focused closes them (hotkey path)"));
				Next();
			}
			else if (TimedOut(TEXT("hotkey F1 did not open the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		case 11:
			if (!Controls(UI))
			{
				UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
				if (!Pause)
				{
					Check(State, false, TEXT("pause menu back on top"));
					return Finish(State, World, false);
				}
				const bool bWas = UTutorialSubsystem::IsTutorialEnabled();
				Pause->ActivateEntry(TEXT("Tutorial"));
				Check(State, UTutorialSubsystem::IsTutorialEnabled() == !bWas, TEXT("TUTORIAL entry flips the setting"));
				const UCodexMenuButton* Toggle = Pause->FindEntryButton(TEXT("Tutorial"));
				Check(State, Toggle && Toggle->GetButtonText().ToString() == (bWas ? TEXT("TUTORIAL: OFF") : TEXT("TUTORIAL: ON")),
					FString::Printf(TEXT("label \"%s\""), Toggle ? *Toggle->GetButtonText().ToString() : TEXT("-")));
				Pause->ActivateEntry(TEXT("Tutorial"));
				Check(State, UTutorialSubsystem::IsTutorialEnabled() == bWas, TEXT("and back"));
				const UCodexMenuButton* Reset = Pause->FindEntryButton(TEXT("ResetTutorial"));
				Check(State, Reset && Reset->GetIsEnabled() == Tutorial->HasSteps(), TEXT("RESET TUTORIAL enabled only with a tutorial loaded"));
				Hud->HandleEscape(); // closes the pause menu
				Next();
			}
			else if (TimedOut(TEXT("hotkey F1 did not close the controls screen")))
			{
				return Finish(State, World, false);
			}
			break;
		default:
			if (!Hud->IsPauseMenuOpen())
			{
				Check(State, !bPaused, TEXT("menus closed, world runs"));
				return Finish(State, World, true);
			}
			if (TimedOut(TEXT("pause menu did not close")))
			{
				return Finish(State, World, false);
			}
			break;
		}
		return true;
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
		TEXT("CodexTactics.ControlsScreenSmoke"),
		TEXT("Dev check: F1 Controls reference (opens + pauses, sections from the registry, F1/Esc close), pause menu CONTROLS and TUTORIAL toggle; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
