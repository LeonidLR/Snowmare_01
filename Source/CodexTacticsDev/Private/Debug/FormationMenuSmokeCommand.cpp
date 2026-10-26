// Dev-only console command: the formation radial menu on B (stage 7, user decision 2026-10-26: G is the grenade, the menu moved to
// B; docs/design/FORMATION_CONTRACT.md 9) on L_MovementTest, through the real input path (PlayerController::InputKey -> Enhanced
// Input IA_SoloMode Started / Completed -> SoloModePressed / SoloModeReleased; Esc / RMB through their bindings):
//   Scripts/smoke.ps1 -Command CodexTactics.FormationMenuSmoke
//   1. hold B: the menu opens after FormationMenuHoldSeconds (not before), Auto highlighted, the 9 icons loaded;
//   2. the virtual cursor on 12 o'clock highlights Line, nothing chosen yet; the release picks it: the squad's choice and the
//      requested formation are Line, the HUD line "Formation: Line ...";
//   3. a tap of B still toggles solo (no menu); 4. a hold in solo opens nothing, its release toggles back to the squad;
//   5. Esc cancels (choice kept, no pause menu, the release toggles nothing); 6. RMB cancels the same way;
//   7. the release in the centre = Auto (the choice cleared: the terrain decides again);
//   8. in the tactical pause of a fight the menu works (Column picked);
//   9. a one-member squad: a hold never opens it, the release is a plain tap (solo).
// "shot" (rendered, not -nullrhi): saves Saved/Screenshots/UI/formation_menu.png with the menu open (current Line, Echelon right
// highlighted). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationMenuRules.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueSubsystem.h"
#include "UI/FormationMenuWidget.h"
#include "UnrealClient.h"

namespace FormationMenuSmoke
{
	/** Virtual cursor offsets from the menu centre (widget units, y down): 12 / 3 / 6 o'clock, the centre. */
	const FVector2D AtLine(0.0, -150.0);
	const FVector2D AtEchelonRight(150.0, 0.0);
	const FVector2D AtColumn(0.0, 150.0);

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bShotTaken = false;
		bool bMenuSeen = false;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Removed;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SendKey(APlayerController* PC, const FKey& Which, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Which, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	const TCHAR* ChoiceName(const TOptional<EFormationMenuItem>& Choice)
	{
		return Choice.IsSet() ? FormationMenuRules::ToString(Choice.GetValue()) : TEXT("auto");
	}

	const TCHAR* EffectName(EFormationMenuEffect Effect)
	{
		switch (Effect)
		{
		case EFormationMenuEffect::Opened: return TEXT("opened");
		case EFormationMenuEffect::Picked: return TEXT("picked");
		case EFormationMenuEffect::Auto: return TEXT("auto");
		case EFormationMenuEffect::Cancelled: return TEXT("cancelled");
		case EFormationMenuEffect::Disabled: return TEXT("disabled");
		default: return TEXT("none");
		}
	}

	bool Finish(FState& State, UWorld* World, bool bComplete)
	{
		if (USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr)
		{
			for (const TWeakObjectPtr<AOperativeCharacter>& Member : State.Removed)
			{
				Squad->RegisterOperative(Member.Get());
			}
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FormationMenuSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time < 2.5f)
		{
			return true;
		}
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		const ACodexTacticsHUD* Hud = PC ? Cast<ACodexTacticsHUD>(PC->GetHUD()) : nullptr;
		if (!PC || !Squad || !Flow || !Squad->GetLeader())
		{
			Check(State, false, TEXT("player controller, squad with a leader, game flow"));
			return Finish(State, World, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; State.bMenuSeen = false; };
		auto TimedOut = [&State](float Limit, const TCHAR* What)
		{
			if (State.StageTime > Limit)
			{
				Check(State, false, FString::Printf(TEXT("timeout: %s"), What));
				return true;
			}
			return false;
		};
		UFormationMenuWidget* Widget = PC->GetFormationMenuWidget();
		State.bMenuSeen |= PC->IsFormationMenuOpen();
		const float Hold = PC->FormationMenuHoldSeconds;

		switch (State.Stage)
		{
		case 0:
			if (Squad->GetMembers().Num() < 2)
			{
				if (TimedOut(5.f, TEXT("a squad of 2+")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			if (Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			Squad->SetFormationMenuChoice(TOptional<EFormationMenuItem>());
			PC->SetFormationMenuCursorOffset(FVector2D::ZeroVector);
			Check(State, !PC->IsFormationMenuOpen() && PC->CanOpenFormationMenu(), FString::Printf(TEXT("start: %d members, squad mode, menu closed and available"),
				Squad->GetMembers().Num()));
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		// 1. Hold B: opens at the hold time, Auto highlighted.
		case 1:
			if (!PC->IsFormationMenuOpen())
			{
				if (TimedOut(2.f, TEXT("hold B did not open the menu")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			Check(State, State.StageTime >= Hold - 0.02f && State.StageTime <= Hold + 0.2f,
				FString::Printf(TEXT("hold B: the menu opened after %.2f s (hold time %.2f s)"), State.StageTime, Hold));
			Check(State, Widget && Widget->IsMenuShown() && !Widget->GetHighlighted().IsSet(), TEXT("the widget shows, Auto (the centre) highlighted"));
			Check(State, Widget && Widget->GetLoadedIconCount() == FormationMenuRules::ItemCount + 1,
				FString::Printf(TEXT("icons loaded: %d of 9"), Widget ? Widget->GetLoadedIconCount() : 0));
			Check(State, !Squad->IsSoloMode(), TEXT("the hold did not toggle solo"));
			PC->SetFormationMenuCursorOffset(AtLine);
			Next();
			break;
		// 2. The cursor on 12: Line highlighted, nothing chosen until the release.
		case 2:
			if (State.StageTime < 0.1f)
			{
				break;
			}
			Check(State, Widget && Widget->GetHighlighted() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line), TEXT("cursor at 12: Line highlighted"));
			Check(State, !Squad->GetFormationMenuChoice().IsSet(), TEXT("nothing chosen while the menu is open"));
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 3:
		{
			if (State.StageTime < 0.05f)
			{
				break;
			}
			const FString HudText = Squad->GetFormationHudText();
			if (!(Squad->GetFormationRequested() == EFormationKind::Line && HudText.StartsWith(TEXT("Line"))))
			{
				if (TimedOut(3.f, *FString::Printf(TEXT("the requested formation Line + HUD (HUD \"%s\")"), *HudText)))
				{
					return Finish(State, World, false);
				}
				break;
			}
			Check(State, !PC->IsFormationMenuOpen() && PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Picked, TEXT("release: picked, the menu closed"));
			Check(State, Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line), TEXT("the squad's choice is Line"));
			Check(State, !Squad->IsSoloMode(), TEXT("the pick did not toggle solo"));
			Check(State, true, FString::Printf(TEXT("requested Line, HUD \"Formation: %s\""), *HudText));
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		}
		// 3. A tap still toggles solo.
		case 4:
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 5:
			if (State.StageTime < 0.5f)
			{
				break;
			}
			Check(State, Squad->IsSoloMode() && !State.bMenuSeen, TEXT("tap B: solo on, no menu"));
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		// 4. A hold in solo: no menu, the release toggles back (a hold where the menu is unavailable is a tap).
		case 6:
			if (State.StageTime < Hold + 0.35f)
			{
				break;
			}
			Check(State, !State.bMenuSeen && !PC->CanOpenFormationMenu(), TEXT("hold B in solo: no menu"));
			Check(State, Squad->IsSoloMode(), TEXT("still solo while held"));
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 7:
			if (State.StageTime < 0.3f)
			{
				break;
			}
			Check(State, !Squad->IsSoloMode() && !State.bMenuSeen, TEXT("the release of the long hold in solo: back to the squad"));
			PC->SetFormationMenuCursorOffset(AtEchelonRight);
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		// 5. Esc cancels: the choice stays Line, no pause menu, the release does nothing.
		case 8:
			if (!PC->IsFormationMenuOpen() || State.StageTime < Hold + 0.15f)
			{
				if (TimedOut(2.f, TEXT("hold B (Esc case) did not open the menu")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			if (State.bShot && !State.bShotTaken)
			{
				const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/UI/formation_menu.png"));
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				State.bShotTaken = true;
				State.StageTime = Hold + 0.15f;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke shot: %s"), *File);
				break;
			}
			if (State.bShot && State.StageTime < Hold + 1.2f)
			{
				break; // the screenshot is written on a later frame
			}
			Check(State, Widget && Widget->GetHighlighted() == TOptional<EFormationMenuItem>(EFormationMenuItem::EchelonRight)
				&& Widget->GetCurrentChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line), TEXT("open: Echelon right highlighted, Line marked current"));
			SendKey(PC, EKeys::Escape, IE_Pressed);
			SendKey(PC, EKeys::Escape, IE_Released);
			Next();
			break;
		case 9:
			if (State.StageTime < 0.1f)
			{
				break;
			}
			Check(State, !PC->IsFormationMenuOpen() && PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Cancelled && Widget && !Widget->IsMenuShown(),
				FString::Printf(TEXT("Esc: cancelled (%s), hidden"), EffectName(PC->GetLastFormationMenuEffect())));
			Check(State, Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line), TEXT("Esc: the choice stays Line"));
			Check(State, !Hud || !Hud->IsPauseMenuOpen(), TEXT("Esc closed the menu only (no pause menu)"));
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 10:
			if (State.StageTime < 0.3f)
			{
				break;
			}
			Check(State, !Squad->IsSoloMode() && Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line),
				TEXT("the release after Esc: no toggle, no pick"));
			PC->SetFormationMenuCursorOffset(AtColumn);
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		// 6. RMB cancels the same way.
		case 11:
			if (!PC->IsFormationMenuOpen())
			{
				if (TimedOut(2.f, TEXT("hold B (RMB case) did not open the menu")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			SendKey(PC, EKeys::RightMouseButton, IE_Pressed);
			Next();
			break;
		case 12:
			SendKey(PC, EKeys::RightMouseButton, IE_Released);
			if (State.StageTime < 0.1f)
			{
				break;
			}
			Check(State, !PC->IsFormationMenuOpen() && PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Cancelled, TEXT("RMB: cancelled"));
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 13:
			if (State.StageTime < 0.3f)
			{
				break;
			}
			Check(State, !Squad->IsSoloMode() && Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Line),
				TEXT("RMB: the choice stays Line, the release toggles nothing"));
			PC->SetFormationMenuCursorOffset(FVector2D(6.0, -4.0)); // inside the dead zone
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		// 7. The centre = Auto.
		case 14:
			if (!PC->IsFormationMenuOpen())
			{
				if (TimedOut(2.f, TEXT("hold B (Auto case) did not open the menu")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 15:
			if (State.StageTime < 0.3f)
			{
				break;
			}
			Check(State, PC->GetLastFormationMenuEffect() == EFormationMenuEffect::Auto && !Squad->GetFormationMenuChoice().IsSet(),
				FString::Printf(TEXT("release in the centre: Auto (choice %s, HUD \"%s\")"), ChoiceName(Squad->GetFormationMenuChoice()), *Squad->GetFormationHudText()));
			// 8. The tactical pause of a fight (enemies parked far away).
			SmokeUtils::StartWaveFight(Flow);
			Next();
			break;
		case 16:
		{
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			if (UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>(); Dialogue && Dialogue->IsDialogueOpen())
			{
				Dialogue->SkipDialogue();
				break;
			}
			if (Flow->GetPhase() != ECodexGamePhase::WaveCombat)
			{
				if (TimedOut(5.f, TEXT("the wave fight")))
				{
					return Finish(State, World, false);
				}
				break;
			}
			if (Flow->GetCombatMode() != ECodexCombatMode::TacticalPause)
			{
				Flow->ToggleTacticalPause();
			}
			Check(State, Flow->GetCombatMode() == ECodexCombatMode::TacticalPause, TEXT("tactical pause on"));
			PC->SetFormationMenuCursorOffset(AtColumn);
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		}
		case 17:
			if (!PC->IsFormationMenuOpen())
			{
				if (TimedOut(2.f, *FString::Printf(TEXT("hold B in the tactical pause did not open the menu (available %d)"), PC->CanOpenFormationMenu() ? 1 : 0)))
				{
					return Finish(State, World, false);
				}
				break;
			}
			Check(State, State.StageTime <= Hold + 0.2f, FString::Printf(TEXT("tactical pause: opened after %.2f s (real time)"), State.StageTime));
			Next();
			break;
		case 18:
			if (State.StageTime < 0.1f)
			{
				break;
			}
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 19:
			if (State.StageTime < 0.2f)
			{
				break;
			}
			Check(State, Squad->GetFormationMenuChoice() == TOptional<EFormationMenuItem>(EFormationMenuItem::Column) && !Squad->IsSoloMode(),
				FString::Printf(TEXT("tactical pause: Column picked (choice %s)"), ChoiceName(Squad->GetFormationMenuChoice())));
			Flow->ToggleTacticalPause();
			// 9. A one-member squad: the others leave it for this check.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Squad->GetLeader())
				{
					State.Removed.Add(Member);
					Squad->UnregisterOperative(Member);
				}
			}
			Check(State, Squad->GetMembers().Num() == 1 && !PC->CanOpenFormationMenu(), TEXT("one member: the menu is unavailable"));
			SendKey(PC, EKeys::B, IE_Pressed);
			Next();
			break;
		case 20:
			if (State.StageTime < Hold + 0.35f)
			{
				break;
			}
			Check(State, !State.bMenuSeen, TEXT("one member: a hold opens nothing"));
			SendKey(PC, EKeys::B, IE_Released);
			Next();
			break;
		case 21:
			if (State.StageTime < 0.3f)
			{
				break;
			}
			Check(State, Squad->IsSoloMode() && !State.bMenuSeen, TEXT("one member: the release is a tap (solo on)"));
			return Finish(State, World, true);
		default:
			return Finish(State, World, false);
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
		State->bShot = Args.Contains(TEXT("shot"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}));
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationMenuSmoke"),
		TEXT("Dev check: the formation radial menu on B (hold opens, cursor highlights, release picks / Auto, tap still toggles solo, Esc / RMB cancel, ")
		TEXT("tactical pause, one member = no menu); 'shot' saves Saved/Screenshots/UI/formation_menu.png (rendered); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
