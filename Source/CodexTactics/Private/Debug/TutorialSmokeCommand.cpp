// Dev-only headless check of the Demo 01 tutorial framework on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.TutorialSmoke
// Inline steps (lesson beat L0 + fight beat FIGHT); a runtime ATutorialTriggerVolume 6 m ahead of the leader and an
// ATutorialMarkerActor for its step:
//  1. before any steps: the marker is hidden, entering the volume does nothing (and does not use it up);
//  2. steps loaded: beat L0 starts, the checkpoint slot is written, the walk-and-talk line plays in the subtitle strip
//     ("[EN missing: ...]" placeholder) while the leader keeps walking (no pause, time dilation 1, no dialogue window);
//  3. the leader walks into the volume: the hint card with [B] shows, the marker appears;
//  4. B pressed through the real input path (PlayerController::InputKey -> Enhanced Input -> ToggleSoloMode ->
//     PlayerCommands::Report): solo on, the card completes, the next card ([B] regroup) follows, the marker hides;
//     B again: regroup completes it;
//  5. lesson beat: a lethal blow leaves the engineer alive at 1 HP (down), the retry prompt shows;
//     "Retry from checkpoint" loads the checkpoint: health restored, progress back to the beat start, prompt gone;
//  6. the fight beat: the lethal-hit guard no longer saves anyone; tutorial off hides everything;
//  7. tutorial.jsonl got "shown" / "completed" lines.
// Saves go to Saved/SmokeSaves/Tutorial (exclusive in Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "Tutorial/TutorialMarkerActor.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tutorial/TutorialTriggerVolume.h"
#include "UI/CodexTacticsHUD.h"
#include "UI/DialogueSubsystem.h"
#include "UI/TutorialOverlayWidget.h"
#include "Components/BoxComponent.h"
#include "GameFramework/WorldSettings.h"

namespace TutorialSmoke
{
	const TCHAR* Steps = TEXT(R"JSON({
		"id": "tutorial_smoke", "start_beat": "L0",
		"beats": [ { "id": "L0", "lesson": true, "checkpoint": true }, { "id": "FIGHT", "lesson": false, "checkpoint": false } ],
		"steps": [
			{ "id": "walk", "beat": "L0", "kind": "walk_talk", "trigger": { "type": "beat_start" },
			  "lines": [ { "speaker": "Engineer", "text_id": "WT_SMOKE_1", "duration_s": 4 }, { "speaker": "Commander", "text_id": "WT_SMOKE_2", "duration_s": 4 } ] },
			{ "id": "solo_card", "beat": "L0", "kind": "hint_card", "text_id": "TUT_SMOKE_SOLO", "keys": ["Squad.Solo"],
			  "trigger": { "type": "volume", "volume": "VOL_SMOKE_IN" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "regroup_card", "beat": "L0", "kind": "hint_card", "text_id": "TUT_SMOKE_REGROUP", "keys": ["Squad.Regroup"],
			  "trigger": { "type": "after_step", "step": "solo_card" }, "complete": { "commands": ["Squad.Regroup"] } }
		] })JSON");

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<ATutorialTriggerVolume> Volume;
		TWeakObjectPtr<ATutorialMarkerActor> Marker;
		FVector LeaderStart = FVector::ZeroVector;
		bool bSubtitleWhileMoving = false;
		bool bNeverPaused = true;
		float CheckpointHealth = 0.f;
		int64 TelemetrySizeAtStart = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FString SaveDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("SmokeSaves/Tutorial");
	}

	bool Finish(FState& State, bool bComplete)
	{
		IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TutorialSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	void PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	AOperativeCharacter* FindRole(USquadSubsystem* Squad, EOperativeRole Role)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == Role)
			{
				return Member;
			}
		}
		return nullptr;
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
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		APlayerController* PC = World->GetFirstPlayerController();
		ACodexTacticsHUD* Hud = PC ? PC->GetHUD<ACodexTacticsHUD>() : nullptr;
		UTutorialOverlayWidget* Overlay = Hud ? Hud->GetTutorialOverlay() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || !Tutorial || !Saves || !PC || !Overlay || State.Time > 120.f)
		{
			Check(State, false, FString::Printf(TEXT("world ready / timeout (stage %d, overlay %d)"), State.Stage, Overlay ? 1 : 0));
			return Finish(State, false);
		}
		Overlay->Refresh();
		switch (State.Stage)
		{
		case 0:
		{
			Saves->SaveDirectoryOverride = SaveDir();
			IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
			Saves->bAutosaveEnabled = true;
			State.TelemetrySizeAtStart = FMath::Max<int64>(0, IFileManager::Get().FileSize(*Tutorial->GetTelemetryPath()));
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Spot = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Forward * 700.f);
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ATutorialTriggerVolume* Volume = World->SpawnActor<ATutorialTriggerVolume>(Spot, FRotator::ZeroRotator, Params);
			Volume->Box->SetBoxExtent(FVector(160.f, 160.f, 200.f));
			Volume->VolumeId = TEXT("VOL_SMOKE_IN");
			ATutorialMarkerActor* Marker = World->SpawnActor<ATutorialMarkerActor>(Spot - FVector(0.f, 0.f, 90.f), FRotator::ZeroRotator, Params);
			Marker->StepId = TEXT("solo_card");
			State.Volume = Volume;
			State.Marker = Marker;
			Check(State, !Tutorial->HasSteps() && !Marker->IsMarkerShown(), TEXT("before any steps: no tutorial, marker hidden"));
			Check(State, !Volume->TryActivate(Leader) && !Volume->HasFired(), TEXT("before any steps: the volume does not fire and is not used up"));
			FTutorialStepsData Data;
			TArray<FString> Warnings;
			FString Error;
			Check(State, TutorialRules::ParseSteps(Steps, Data, Warnings, Error), TEXT("inline steps parse ") + Error);
			Tutorial->LoadSteps(Data);
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("L0")) && Tutorial->IsLessonBeat(), TEXT("lesson beat L0 started"));
			Check(State, Saves->HasSave(UTutorialSubsystem::CheckpointSlot), TEXT("checkpoint slot written at the beat start"));
			const FString Line = Overlay->GetShownSubtitle().ToString();
			Check(State, Overlay->IsSubtitleShown() && Line == TEXT("[EN missing: WT_SMOKE_1]"),
				FString::Printf(TEXT("walk-and-talk line in the subtitle strip: '%s' (%s)"), *Line, *Overlay->GetShownSpeaker().ToString()));
			Check(State, !Overlay->IsCardShown() && !State.Marker->IsMarkerShown(), TEXT("no card / marker yet"));
			if (AOperativeCharacter* Engineer = FindRole(Squad, EOperativeRole::Engineer))
			{
				State.CheckpointHealth = Engineer->HealthComponent->GetCurrentHealth();
			}
			State.LeaderStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(State.Volume->GetActorLocation(), false);
			Next(State);
			return true;
		}
		case 2:
		{
			// Walking into the volume while the squad talks: nothing pauses, nothing blocks.
			const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			State.bNeverPaused &= !World->IsPaused() && FMath::IsNearlyEqual(World->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f)
				&& !(Dialogue && Dialogue->IsDialogueOpen());
			if (Overlay->IsSubtitleShown() && Leader->GetVelocity().Size2D() > 50.f)
			{
				State.bSubtitleWhileMoving = true;
			}
			if (Tutorial->GetStepState(TEXT("solo_card")) != ETutorialStepState::Active && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, State.bSubtitleWhileMoving && State.bNeverPaused, TEXT("the walk-and-talk line played while the leader walked (no pause, no dialogue window)"));
			Check(State, State.Volume->HasFired(), TEXT("the leader entered the volume (fired once)"));
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Overlay->IsCardShown() && Overlay->GetShownCardKeys().ToString() == TEXT("[B]"),
				FString::Printf(TEXT("hint card shown with keys '%s', body '%s'"), *Overlay->GetShownCardKeys().ToString(), *Overlay->GetShownCardBody().ToString()));
			Check(State, State.Marker->IsMarkerShown(), TEXT("marker shown while its step is active"));
			Check(State, !Squad->IsSoloMode(), TEXT("not solo yet"));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 4:
			PressKey(PC, EKeys::B, IE_Released);
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode(), TEXT("B through the input path: solo on"));
			Check(State, Tutorial->GetStepState(TEXT("solo_card")) == ETutorialStepState::Completed, TEXT("the solo card completed by the Squad.Solo command"));
			Check(State, !State.Marker->IsMarkerShown(), TEXT("marker hidden once its step is done"));
			Next(State);
			return true;
		case 5:
			if (Tutorial->GetStepState(TEXT("regroup_card")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("regroup_card")) == ETutorialStepState::Active && State.StageTime >= 0.8f,
				FString::Printf(TEXT("next card after the breather (%.1f s)"), State.StageTime));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		case 6:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("regroup_card")) == ETutorialStepState::Completed,
				TEXT("B again: regroup, the regroup card completed"));
			// Lesson beat: a lethal blow downs instead of killing.
			AOperativeCharacter* Engineer = FindRole(Squad, EOperativeRole::Engineer);
			if (!Engineer)
			{
				Check(State, false, TEXT("engineer"));
				return Finish(State, false);
			}
			Engineer->HealthComponent->ApplyDirectHealthLoss(10000.f, TEXT("TutorialSmoke"));
			Check(State, Engineer->HealthComponent->IsAlive() && FMath::IsNearlyEqual(Engineer->HealthComponent->GetCurrentHealth(), 1.f)
				&& Tutorial->GetLessonDownCount() == 1, FString::Printf(TEXT("lesson: lethal blow -> engineer alive at %.0f HP (downed)"),
				Engineer->HealthComponent->GetCurrentHealth()));
			Check(State, Squad->GetMembers().Contains(Engineer), TEXT("lesson: still in the squad"));
			Next(State);
			return true;
		}
		case 7:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Overlay->IsRetryShown(), TEXT("retry prompt shown"));
			Check(State, Tutorial->RetryFromCheckpoint(), TEXT("retry from checkpoint loads the slot"));
			Next(State);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			AOperativeCharacter* Engineer = FindRole(Squad, EOperativeRole::Engineer);
			Check(State, Engineer && FMath::IsNearlyEqual(Engineer->HealthComponent->GetCurrentHealth(), State.CheckpointHealth, 1.f),
				FString::Printf(TEXT("checkpoint restored the engineer's health (%.0f / %.0f)"), Engineer ? Engineer->HealthComponent->GetCurrentHealth() : -1.f, State.CheckpointHealth));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("L0")) && Tutorial->GetStepState(TEXT("solo_card")) == ETutorialStepState::Idle
				&& Tutorial->GetStepState(TEXT("regroup_card")) == ETutorialStepState::Idle, TEXT("tutorial progress back to the beat start"));
			Check(State, !Overlay->IsRetryShown(), TEXT("retry prompt gone"));
			// The fight beat: normal rules.
			Check(State, Tutorial->StartBeat(TEXT("FIGHT")) && !Tutorial->IsLessonBeat(), TEXT("fight beat started (not a lesson)"));
			Check(State, Engineer && !Engineer->HealthComponent->LethalDamageGuard.Execute(Engineer->HealthComponent, TEXT("TutorialSmoke")),
				TEXT("fight beat: the lethal-hit guard lets the blow kill"));
			// Tutorial off: nothing shows.
			Tutorial->TriggerStep(TEXT("solo_card"));
			Tutorial->SetTutorialEnabled(false);
			Overlay->Refresh();
			Check(State, !Overlay->IsCardShown() && !Overlay->IsSubtitleShown() && !State.Marker->IsMarkerShown(), TEXT("tutorial OFF hides the card, strip and marker"));
			Tutorial->SetTutorialEnabled(true);
			Overlay->Refresh();
			Check(State, Overlay->IsCardShown() && State.Marker->IsMarkerShown(), TEXT("tutorial ON shows them again"));
			FString Log;
			FFileHelper::LoadFileToString(Log, *Tutorial->GetTelemetryPath());
			const FString Tail = Log.Mid(static_cast<int32>(FMath::Min<int64>(State.TelemetrySizeAtStart, Log.Len())));
			Check(State, Tail.Contains(TEXT("\"event\":\"shown\",\"step\":\"solo_card\"")) && Tail.Contains(TEXT("\"event\":\"completed\",\"step\":\"solo_card\""))
				&& Tail.Contains(TEXT("\"event\":\"lesson_downed\"")) && Tail.Contains(TEXT("\"event\":\"retry\"")), TEXT("tutorial.jsonl has shown / completed / lesson_downed / retry"));
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
		FTimerHandle PlaceHandle;
		TWeakObjectPtr<UWorld> PlaceWorld(World);
		World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
		{
			SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
		}), 0.5f, false);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TutorialSmoke"),
		TEXT("Dev check: tutorial volume -> card -> B via input -> next card, walk-and-talk without pause, marker, lesson down + checkpoint retry, ON/OFF, telemetry; PASS / FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
