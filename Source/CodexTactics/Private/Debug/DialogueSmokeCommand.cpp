// Dev-only console command for a headless dialogue check (frontend NEW GAME -> the campaign level, any start map):
//   Scripts/smoke.ps1 -Command CodexTactics.DialogueSmoke
// 1. the frontend's NEW GAME opens the level, whose start plays the intro briefing in the bottom window (a headless
// check without the frontend gets the radio line instead); 2. Space advances a line, world clicks are blocked,
// Esc skips; 2b. no cold accumulates while the window is open, it resumes once closed (user request 2026-10-06);
// 3. the preparation dialogue plays in the message feed line by line with the Godot delays.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/MissionSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "Data/NarrativeManifest.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UI/DialogueSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Debug/FrontendSmokeUtils.h"

namespace DialogueSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		int32 Stage = 0;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Failures = 0;
		TWeakObjectPtr<UWorld> StartWorld;
	};

	void Check(FState& State, bool bOk, const TCHAR* What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("DialogueSmoke"));
		return false;
	}

	bool FeedHas(const UGameMessageSubsystem* Messages, const TCHAR* Prefix)
	{
		for (const FGameMessage& Message : Messages->GetHistory())
		{
			if (Message.Text.ToString().StartsWith(Prefix))
			{
				return true;
			}
		}
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	bool Step(FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = FrontendSmokeUtils::FindGameWorld();
		if (State.Time > 70.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return Finish(State, false);
		}
		if (!World)
		{
			return true;
		}
		if (State.Stage == -1) // NEW GAME from the frontend (works from any map)
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			State.StartWorld = World;
			UCodexFrontendSubsystem::Get(World)->StartNewGame();
			Next(State);
			State.Stage = 0;
			return true;
		}
		if (World == State.StartWorld.Get())
		{
			return true; // the level is still opening
		}
		UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>();
		UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		switch (State.Stage)
		{
		case 0:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Mission->IsMissionWorld() && Mission->GetStartMode() == EMissionStartMode::Game && !UGameplayStatics::IsGamePaused(World),
				TEXT("NEW GAME: the level started in Game mode (no in-level menu)"));
			Check(State, Dialogue->IsDialogueOpen() && Dialogue->GetCurrentSequence()->Lines.Num() == FNarrativeManifest::Get().Sequences[TEXT("DA_DialogueIntro")].Num(), TEXT("intro briefing opened (line count from the narrative manifest)"));
			Check(State, Dialogue->GetCurrentSequence()->Lines[0].SpeakerName == TEXT("Medic-Sapper"), TEXT("first speaker is the medic-sapper"));
			PC->SpacePressed();
			PC->SpaceReleased();
			Check(State, Dialogue->GetLineIndex() == 1, TEXT("Space advances a line"));
			Check(State, Flow->GetCombatMode() != ECodexCombatMode::TacticalPause, TEXT("Space did not toggle the pause"));
			// User request 2026-10-06: no cold while the dialogue is open.
			if (AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader())
			{
				Leader->ColdLevel = 30.f;
			}
			Next(State);
			return true;
		case 1: // 2 s with the briefing open: the cold holds.
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
			Check(State, Dialogue->IsDialogueOpen() && Leader && FMath::IsNearlyEqual(Leader->ColdLevel, 30.f, 0.01f), *FString::Printf(
				TEXT("no cold accumulates while the dialogue is open (cold %.2f after 2 s)"), Leader ? Leader->ColdLevel : -1.f));
			Dialogue->SkipDialogue(); // Esc
			Check(State, !Dialogue->IsDialogueOpen(), TEXT("skip closes the window"));
			Next(State);
			return true;
		}
		case 2: // The window closed: the cold runs again (chills or warms, but it moves).
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
			Check(State, Leader && !FMath::IsNearlyEqual(Leader->ColdLevel, 30.f, 0.01f), *FString::Printf(
				TEXT("the cold resumes after the dialogue (cold %.2f)"), Leader ? Leader->ColdLevel : -1.f));
			Flow->TriggerCombatZone();
			Flow->FinishCutscene(); // -> Preparation: the prep dialogue plays in the feed
			Next(State);
			return true;
		}
		case 3: // First prep line at once, the second after its 3.5 s delay.
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, FeedHas(Messages, TEXT("Alert! Perimeter under threat!")) && !FeedHas(Messages, TEXT("Generator is humming")), TEXT("first prep line posted, second waits"));
			Next(State);
			return true;
		case 4:
			if (State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, FeedHas(Messages, TEXT("Generator is humming")), TEXT("second prep line after the delay"));
			return Finish(State, true);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->Stage = -1;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(*State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.DialogueSmoke"),
		TEXT("Dev check: frontend NEW GAME -> intro briefing window, Space / skip, preparation lines in the feed; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
