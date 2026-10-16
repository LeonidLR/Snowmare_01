// Dev-only console command for a headless narrative element / dialogue trigger check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.NarrativeSmoke
// Godot narrative_element.gd / dialogue_trigger.gd: the level has the note, signpost and poster; from afar the note
// shows only its marker, within 2 m its text; its menu reads the text into the feed (level texts without an English manifest entry show "[EN missing: ...]", never Cyrillic);
// the wave-rest trigger plays its dialogue once when an operative walks in.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Data/NarrativeManifest.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/NarrativeElementActor.h"
#include "Quests/DialogueTriggerVolume.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"
#include "UI/OverheadLabel.h"

namespace NarrativeSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
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
		FPlatformMisc::RequestExit(false, TEXT("NarrativeSmoke"));
		return false;
	}

	bool HasMessage(UWorld* World, const FString& Speaker, const FString& Part)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (Message.Speaker.ToString().Contains(Speaker) && Message.Text.ToString().Contains(Part))
			{
				return true;
			}
		}
		return false;
	}

	/** True when any message of the feed contains a Cyrillic letter (nothing Russian may reach the screen). */
	bool FeedHasCyrillic(UWorld* World)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (FNarrativeManifest::ContainsCyrillic(Message.Speaker.ToString()) || FNarrativeManifest::ContainsCyrillic(Message.Text.ToString()))
			{
				return true;
			}
		}
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
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		if (!Leader || State.Stage++ > 0)
		{
			return Finish(State, false);
		}
		TArray<ANarrativeElementActor*> Elements;
		ANarrativeElementActor* Note = nullptr;
		for (TActorIterator<ANarrativeElementActor> It(World); It; ++It)
		{
			Elements.Add(*It);
			Note = It->NarrativeType == ENarrativeType::Note ? *It : Note;
		}
		ADialogueTriggerVolume* Trigger = nullptr;
		for (TActorIterator<ADialogueTriggerVolume> It(World); It; ++It)
		{
			Trigger = *It;
		}
		Check(State, Elements.Num() == 3 && Note && Trigger, FString::Printf(TEXT("level: %d narrative elements, note, dialogue trigger"), Elements.Num()));
		if (!Note || !Trigger)
		{
			return Finish(State, false);
		}

		FOverheadLabel Far;
		Note->GetOverheadLabel(Far);
		Check(State, !Note->IsReadableNow() && Far.bHasMarker && Far.Text.TrimStartAndEnd().IsEmpty(), TEXT("from afar: marker only"));
		Leader->TeleportTo(Note->GetActorLocation() + FVector(120.f, 0.f, 60.f), Leader->GetActorRotation(), false, true);
		FOverheadLabel Near;
		Note->GetOverheadLabel(Near);
		Check(State, Note->IsReadableNow() && !FNarrativeManifest::ContainsCyrillic(Near.Text) && !Near.Text.TrimStartAndEnd().IsEmpty(),
			TEXT("within 2 m: the text shows"));

		UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
		Interactions->OpenMenuNow(Note);
		const bool bMenu = Interactions->IsActionMenuOpen() && !FNarrativeManifest::ContainsCyrillic(Interactions->GetActionMenu().Title.ToString())
			&& !FNarrativeManifest::ContainsCyrillic(Interactions->GetActionMenu().Description.ToString())
			&& Interactions->GetActionMenu().ConfirmText.ToString() == TEXT("Read Aloud");
		Check(State, bMenu, TEXT("menu: English only, \"Read Aloud\""));
		Interactions->ConfirmActionMenu();
		Check(State, Note->bHasBeenRead && !FeedHasCyrillic(World), TEXT("read aloud into the feed"));

		Check(State, Trigger->TryTrigger(Leader) && HasMessage(World, TEXT("Medic-Sapper"), TEXT("First wave repelled")), TEXT("trigger plays the wave-rest dialogue"));
		Check(State, !Trigger->TryTrigger(Leader), TEXT("only once"));
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
		TEXT("CodexTactics.NarrativeSmoke"),
		TEXT("Dev check: narrative elements (marker, readable text, read aloud) and the dialogue trigger; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
