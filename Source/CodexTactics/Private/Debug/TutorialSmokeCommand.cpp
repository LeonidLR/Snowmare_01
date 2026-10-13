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
//  7. tutorial.jsonl got "shown" / "completed" lines;
//  8. the STAGED camera lesson (beat CAM, user feedback 2026-10-12) through real InputKey: W held ~1.7 s completes the pan
//     stage (hold_s 1.0) and nothing else; Q + E (count 2), a ~1 s RMB drag (hold_s 0.5) and two wheel notches complete
//     the next stages in order, and WASD / Q / the wheel never complete a stage that is not theirs;
//     the camera chain also has the MMB drag-pan stage (user addition 2026-10-13): a ~1 s middle-button drag completes it;
//  9. the REAL demo_01_steps.json, A0 -> A1 (user flow 2026-10-13, rev 5): the level opens with the HQ radio only - a ground
//     click during it is refused (Order.Move locked, TUT_A0_WAIT_HQ once) and the camera cards wait; W and an MMB drag during
//     the radio go into the command history, so after the radio the pan and drag stages are PRECREDITED (never shown, two
//     "already done" feed ticks) and Q/E is the first card; sprint is locked (a double click walks, TUT_A0_NOSPRINT once), the
//     inventory drawer stays closed (TUT_A0_NOINVENTORY once) and U is refused; beacon 1 (beat A1) unlocks NOTHING; the crack:
//     select (key 2) -> the one-at-a-time bridge card completed by three BridgeCrossed events (the bridge actor itself is
//     BridgeFenceSmoke's job) -> regroup with B -> box -> the squad banter; 20 s after the crossing the gust (+20 cold, >= 42 %);
//     the heat-pack card UNLOCKS the inventory / items (the drawer opens now), the coldest uses his own starter pack, the mate
//     card; THEN the sprint card unlocks sprint (a double click sprints), THEN WT_A1_RESUPPLY + the hut objective; the hut loot
//     cards and the closing line; beacon 3 plays the formation lines with no guard card; the save's tutorial section carries the
//     command history; O toggles the player's enemy vision cones (View.VisionCones).
// Saves go to Saved/SmokeSaves/Tutorial (exclusive in Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Survival/TacticalHeatSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Data/TacticalHeatTuning.h"
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
#include "UI/InventoryDrawerWidget.h"
#include "Combat/VisionConeComponent.h"
#include "Dom/JsonObject.h"
#include "UI/DialogueSubsystem.h"
#include "UI/TutorialOverlayWidget.h"
#include "Components/BoxComponent.h"
#include "GameFramework/WorldSettings.h"

namespace TutorialSmoke
{
	const TCHAR* Steps = TEXT(R"JSON({
		"id": "tutorial_smoke", "start_beat": "L0",
		"beats": [ { "id": "L0", "lesson": true, "checkpoint": true }, { "id": "FIGHT", "lesson": false, "checkpoint": false },
			{ "id": "CAM", "lesson": false, "checkpoint": false } ],
		"steps": [
			{ "id": "walk", "beat": "L0", "kind": "walk_talk", "trigger": { "type": "beat_start" },
			  "lines": [ { "speaker": "Engineer", "text_id": "WT_SMOKE_1", "duration_s": 4 }, { "speaker": "Commander", "text_id": "WT_SMOKE_2", "duration_s": 4 } ] },
			{ "id": "solo_card", "beat": "L0", "kind": "hint_card", "text_id": "TUT_SMOKE_SOLO", "keys": ["Squad.Solo"],
			  "trigger": { "type": "volume", "volume": "VOL_SMOKE_IN" }, "complete": { "commands": ["Squad.Solo"] } },
			{ "id": "regroup_card", "beat": "L0", "kind": "hint_card", "text_id": "TUT_SMOKE_REGROUP", "keys": ["Squad.Regroup"],
			  "trigger": { "type": "after_step", "step": "solo_card" }, "complete": { "commands": ["Squad.Regroup"] } },
			{ "id": "cam_pan", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_PAN", "keys": ["Camera.Pan.Keys"],
			  "trigger": { "type": "manual" }, "complete": { "commands": ["Camera.Pan.Keys", "Camera.Pan.Edge"], "hold_s": 1.0 } },
			{ "id": "cam_drag", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_DRAG", "keys": ["Camera.Pan.Drag"],
			  "trigger": { "type": "after_step", "step": "cam_pan" }, "complete": { "commands": ["Camera.Pan.Drag"], "hold_s": 0.5 } },
			{ "id": "cam_yaw", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_YAW", "keys": ["Camera.Rotate.Keys"], "precredit": false,
			  "trigger": { "type": "after_step", "step": "cam_drag" }, "complete": { "commands": ["Camera.Rotate.Keys"], "count": 2 } },
			{ "id": "cam_look", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_LOOK", "keys": ["Camera.Rotate.Mouse"], "precredit": false,
			  "trigger": { "type": "after_step", "step": "cam_yaw" }, "complete": { "commands": ["Camera.Rotate.Mouse"], "hold_s": 0.5 } },
			{ "id": "cam_zoom", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_ZOOM", "keys": ["Camera.Zoom"], "precredit": false,
			  "trigger": { "type": "after_step", "step": "cam_look" }, "complete": { "commands": ["Camera.Zoom"], "count": 2 } },
			{ "id": "cam_move", "beat": "CAM", "kind": "hint_card", "text_id": "TUT_SMOKE_CAM_MOVE", "keys": ["Order.Move"],
			  "trigger": { "type": "after_step", "step": "cam_zoom" }, "complete": { "commands": ["Order.Move"] } }
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
		/** A key tapped in the previous stage: released at the start of the next tick. */
		FKey PendingRelease;
		/** Demo-file part: the hut crate, the cold of each operative when solo started, NOSPRINT feed lines before. */
		TWeakObjectPtr<ALootCrateActor> Crate;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> ColdAtSolo;
		int32 NoSprintLinesBefore = 0;
		/** The leader's heat packs just before he empties the hut crate. */
		int32 HeatPacksBeforeLoot = 0;
		TWeakObjectPtr<AOperativeCharacter> Focus;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	int32 CountMessages(UWorld* World, const FString& Part)
	{
		int32 Count = 0;
		if (const UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
		{
			for (const FGameMessage& Message : Messages->GetHistory())
			{
				Count += Message.Text.ToString().Contains(Part) ? 1 : 0;
			}
		}
		return Count;
	}

	/** A double click on the ground 5 m ahead of the leader through the controller's real click path. */
	void DoubleClickAhead(UWorld* World, ACodexTacticsPlayerController* PC, AOperativeCharacter* Leader)
	{
		const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
		const FVector Spot = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Forward * 500.f);
		const FHitResult Hit(nullptr, nullptr, Spot - FVector(0.f, 0.f, 90.f), FVector::UpVector);
		PC->HandleWorldHit(Hit);
		PC->HandleWorldHit(Hit);
	}

	/** Spawns a fired-on-demand tutorial volume under the leader and enters it. */
	bool EnterVolume(UWorld* World, AOperativeCharacter* Leader, FName VolumeId)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATutorialTriggerVolume* Volume = World->SpawnActor<ATutorialTriggerVolume>(Leader->GetActorLocation() + FVector(0.f, 0.f, 3000.f),
			FRotator::ZeroRotator, Params);
		Volume->VolumeId = VolumeId;
		return Volume->TryActivate(Leader);
	}

	AOperativeCharacter* Coldest(USquadSubsystem* Squad)
	{
		AOperativeCharacter* Best = nullptr;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (!Best || Member->ColdLevel > Best->ColdLevel)
			{
				Best = Member;
			}
		}
		return Best;
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

	/** Presses Key now; it is released on the next tick (a tap through the real input path). */
	void Tap(APlayerController* PC, FState& State, const FKey& Key)
	{
		PressKey(PC, Key, IE_Pressed);
		State.PendingRelease = Key;
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
		if (!Leader || !Tutorial || !Saves || !PC || !Overlay || State.Time > 360.f)
		{
			Check(State, false, FString::Printf(TEXT("world ready / timeout (stage %d, overlay %d)"), State.Stage, Overlay ? 1 : 0));
			return Finish(State, false);
		}
		Overlay->Refresh();
		if (State.PendingRelease.IsValid())
		{
			PressKey(PC, State.PendingRelease, IE_Released);
			State.PendingRelease = FKey();
		}
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
			Next(State);
			return true;
		}
		// --- The staged camera lesson (user feedback 2026-10-12), driven through the real input path --------------------
		case 9:
		{
			Tutorial->SkipStep(TEXT("solo_card")); // free the card slot (its follow-up regroup card is skipped too)
			Tutorial->SkipStep(TEXT("regroup_card"));
			Check(State, Tutorial->TriggerStep(TEXT("cam_pan")), TEXT("camera lesson started"));
			Next(State);
			return true;
		}
		case 10:
		{
			if (Tutorial->GetStepState(TEXT("cam_pan")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_pan")) == ETutorialStepState::Active && Overlay->IsCardShown()
				&& Overlay->GetShownCardKeys().ToString() == TEXT("[W/A/S/D]"),
				FString::Printf(TEXT("stage 1: pan card shown with keys '%s' (pan %s, solo %s, active card '%s', paused %d, dilation %.2f)"),
					*Overlay->GetShownCardKeys().ToString(), TutorialRules::StateName(Tutorial->GetStepState(TEXT("cam_pan"))),
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("solo_card"))), *Tutorial->GetMachine().GetActiveCard().ToString(),
					World->IsPaused() ? 1 : 0, World->GetWorldSettings()->GetEffectiveTimeDilation()));
			Check(State, Tutorial->GetStepState(TEXT("cam_yaw")) == ETutorialStepState::Idle && Tutorial->GetStepState(TEXT("cam_zoom")) == ETutorialStepState::Idle,
				TEXT("the later stages have not started"));
			PressKey(PC, EKeys::W, IE_Pressed); // held: the camera pans, hold ticks accumulate
			Next(State);
			return true;
		}
		case 11:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_pan")) == ETutorialStepState::Active && Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_pan")) < 1.f
				&& Tutorial->GetStepProgress(TEXT("cam_pan")) == 1, FString::Printf(TEXT("0.5 s of W: one use, %.2f s held, card still up"),
				Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_pan"))));
			Next(State);
			return true;
		}
		case 12:
		{
			if (State.StageTime < 1.2f)
			{
				return true;
			}
			PressKey(PC, EKeys::W, IE_Released);
			Check(State, Tutorial->GetStepState(TEXT("cam_pan")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("W held ~1.7 s completes the pan stage (%.2f s held)"), Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_pan"))));
			Next(State);
			return true;
		}
		case 13:
		{
			if (Tutorial->GetStepState(TEXT("cam_drag")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_drag")) == ETutorialStepState::Active && Overlay->GetShownCardKeys().ToString() == TEXT("[MMB drag]"),
				FString::Printf(TEXT("stage 1b: MMB drag card shown after the breather (keys '%s')"), *Overlay->GetShownCardKeys().ToString()));
			Tap(PC, State, EKeys::W); // WASD must not finish the drag stage
			PressKey(PC, EKeys::MiddleMouseButton, IE_Pressed); // held ~1 s: Camera.Pan.Drag hold ticks
			State.Stage = 48;
			State.StageTime = 0.f;
			return true;
		}
		case 48:
		{
			if (State.StageTime < 1.0f)
			{
				return true;
			}
			PressKey(PC, EKeys::MiddleMouseButton, IE_Released);
			Check(State, Tutorial->GetStepState(TEXT("cam_drag")) == ETutorialStepState::Completed && Tutorial->GetStepProgress(TEXT("cam_drag")) == 1,
				FString::Printf(TEXT("a ~1 s MMB drag completes the drag-pan stage (%.2f s held); W did not"), Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_drag"))));
			State.Stage = 49;
			State.StageTime = 0.f;
			return true;
		}
		case 49:
		{
			if (Tutorial->GetStepState(TEXT("cam_yaw")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_yaw")) == ETutorialStepState::Active, TEXT("stage 2: Q/E card shown after the breather"));
			Tap(PC, State, EKeys::W); // WASD alone must not finish the rotate stage
			State.Stage = 14;
			State.StageTime = 0.f;
			return true;
		}
		case 14:
			Check(State, Tutorial->GetStepState(TEXT("cam_yaw")) == ETutorialStepState::Active && Tutorial->GetStepProgress(TEXT("cam_yaw")) == 0,
				TEXT("WASD does not count for the Q/E stage"));
			Tap(PC, State, EKeys::Q);
			Next(State);
			return true;
		case 15:
			Check(State, Tutorial->GetStepState(TEXT("cam_yaw")) == ETutorialStepState::Active && Tutorial->GetStepProgress(TEXT("cam_yaw")) == 1,
				TEXT("one Q press: 1 of 2"));
			Tap(PC, State, EKeys::E);
			Next(State);
			return true;
		case 16:
			Check(State, Tutorial->GetStepState(TEXT("cam_yaw")) == ETutorialStepState::Completed, TEXT("Q then E completes the Q/E stage"));
			Next(State);
			return true;
		case 17:
		{
			if (Tutorial->GetStepState(TEXT("cam_look")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_look")) == ETutorialStepState::Active, TEXT("stage 3: mouse free-look card shown"));
			Tap(PC, State, EKeys::Q); // keys / wheel must not finish the mouse stage
			Next(State);
			return true;
		}
		case 18:
			Tap(PC, State, EKeys::MouseScrollUp);
			Next(State);
			return true;
		case 19:
			Check(State, Tutorial->GetStepState(TEXT("cam_look")) == ETutorialStepState::Active && Tutorial->GetStepProgress(TEXT("cam_look")) == 0,
				TEXT("Q and the wheel do not count for the mouse stage"));
			PressKey(PC, EKeys::RightMouseButton, IE_Pressed);
			Next(State);
			return true;
		case 20:
		{
			if (State.StageTime < 0.25f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_look")) == ETutorialStepState::Active && Tutorial->GetStepProgress(TEXT("cam_look")) == 1,
				FString::Printf(TEXT("RMB drag started (%.2f s held): not enough yet"), Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_look"))));
			Next(State);
			return true;
		}
		case 21:
		{
			if (State.StageTime < 0.7f)
			{
				return true;
			}
			PressKey(PC, EKeys::RightMouseButton, IE_Released);
			Check(State, Tutorial->GetStepState(TEXT("cam_look")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("a ~1 s RMB drag completes the mouse stage (%.2f s held)"), Tutorial->GetMachine().GetHeldSeconds(TEXT("cam_look"))));
			Next(State);
			return true;
		}
		case 22:
		{
			if (Tutorial->GetStepState(TEXT("cam_zoom")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_zoom")) == ETutorialStepState::Active, TEXT("stage 4: zoom card shown"));
			Tap(PC, State, EKeys::E); // rotating does not zoom
			Next(State);
			return true;
		}
		case 23:
			Check(State, Tutorial->GetStepProgress(TEXT("cam_zoom")) == 0, TEXT("Q/E do not count for the zoom stage"));
			Tap(PC, State, EKeys::MouseScrollUp);
			Next(State);
			return true;
		case 24:
			Check(State, Tutorial->GetStepState(TEXT("cam_zoom")) == ETutorialStepState::Active && Tutorial->GetStepProgress(TEXT("cam_zoom")) == 1,
				TEXT("one wheel notch: 1 of 2"));
			Tap(PC, State, EKeys::MouseScrollDown);
			Next(State);
			return true;
		case 25:
			Check(State, Tutorial->GetStepState(TEXT("cam_zoom")) == ETutorialStepState::Completed, TEXT("two wheel notches complete the zoom stage"));
			Next(State);
			return true;
		case 26:
		{
			if (Tutorial->GetStepState(TEXT("cam_move")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("cam_move")) == ETutorialStepState::Active, TEXT("then the move card follows"));
			Next(State);
			return true;
		}
		// --- The real demo file, A0 -> A1 (user flow 2026-10-13, rev 5): HQ radio only + move lock, precredit, sprint / inventory locks, bridge, heat pack unlocks, hut ---
		case 27:
		{
			ACodexTacticsPlayerController* CodexPC = Cast<ACodexTacticsPlayerController>(PC);
			if (!CodexPC)
			{
				Check(State, false, TEXT("CodexTactics player controller"));
				return Finish(State, false);
			}
			Check(State, Tutorial->LoadStepsFile(TEXT("demo_01_steps.json")), TEXT("demo_01_steps.json loaded"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A0")) && Tutorial->IsCommandLocked(PlayerCommands::Sprint)
				&& Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen) && Tutorial->IsCommandLocked(PlayerCommands::UseHeatPack),
				TEXT("A0: sprint, the inventory and the items locked from the start"));
			Tutorial->SetTutorialEnabled(false);
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::Sprint) && !Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen), TEXT("tutorial OFF: no locks"));
			Tutorial->SetTutorialEnabled(true);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
				Member->HeatPacksCount = 1; // the demo loadout (level JSON heat_packs_count 1: one starter pack per operative)
			}
			State.NoSprintLinesBefore = CountMessages(World, TEXT("No running yet"));
			Next(State);
			return true;
		}
		case 28:
		{
			if (Tutorial->GetStepState(TEXT("A0_radio_hq")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Active && Overlay->IsSubtitleShown(),
				TEXT("A0 opens with the HQ radio in the subtitle strip"));
			Check(State, !Overlay->IsCardShown() && Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Idle, TEXT("no lesson card while HQ talks"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Move), TEXT("moves locked while HQ talks"));
			// A click on the ground during the radio: nothing moves, one feed line.
			ACodexTacticsPlayerController* CodexPC = Cast<ACodexTacticsPlayerController>(PC);
			const FVector Forward = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Spot = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Forward * 500.f);
			CodexPC->HandleWorldHit(FHitResult(nullptr, nullptr, Spot - FVector(0.f, 0.f, 90.f), FVector::UpVector));
			State.LeaderStart = Leader->GetActorLocation();
			// The player pans with W, then MMB-drags, while HQ talks: those camera stages must be precredited later (one at a time:
			// the key pan is suppressed while a drag pan runs).
			PressKey(PC, EKeys::W, IE_Pressed);
			State.Stage = 50;
			State.StageTime = 0.f;
			return true;
		}
		case 50:
		{
			if (State.StageTime < 1.4f)
			{
				return true;
			}
			PressKey(PC, EKeys::W, IE_Released);
			PressKey(PC, EKeys::MiddleMouseButton, IE_Pressed);
			State.Stage = 29;
			State.StageTime = 0.f;
			return true;
		}
		case 29:
		{
			if (State.StageTime < 0.9f)
			{
				return true;
			}
			PressKey(PC, EKeys::MiddleMouseButton, IE_Released);
			Check(State, !Leader->IsMoving() && FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) < 30.f && Tutorial->GetLockedRefusalCount() == 1,
				FString::Printf(TEXT("the click during the radio was refused (moved %.0f cm, %d refusals)"), FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart),
					Tutorial->GetLockedRefusalCount()));
			Check(State, Tutorial->GetStepState(TEXT("A0_wait_hq")) == ETutorialStepState::Completed && CountMessages(World, TEXT("HQ is talking")) == 1,
				TEXT("TUT_A0_WAIT_HQ in the feed once"));
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraPanKeys).HeldSeconds >= 1.f
				&& Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraPanDrag).HeldSeconds >= 0.5f,
				FString::Printf(TEXT("command history recorded the pan (%.2f s) and the MMB drag (%.2f s) during the radio"),
					Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraPanKeys).HeldSeconds, Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraPanDrag).HeldSeconds));
			Check(State, Tutorial->SkipStep(TEXT("A0_radio_hq")), TEXT("radio skipped (ends the intro)"));
			Next(State);
			return true;
		}
		case 30:
		{
			if (Tutorial->GetStepState(TEXT("A0_cam_rotate_keys")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::Move), TEXT("after the radio: moves unlocked"));
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A0_cam_pan_drag")) == ETutorialStepState::Completed
				&& Tutorial->GetPrecreditedCount() == 2, FString::Printf(TEXT("pan and MMB drag precredited from the history (%d precredited)"), Tutorial->GetPrecreditedCount()));
			Check(State, CountMessages(World, TEXT("already done")) == 2, TEXT("two unobtrusive 'already done' ticks in the feed"));
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_rotate_keys")) == ETutorialStepState::Active, TEXT("Q/E is the first card shown"));
			for (const TCHAR* Id : { TEXT("A0_cam_rotate_keys"), TEXT("A0_cam_rotate_mouse"), TEXT("A0_cam_zoom") })
			{
				Tutorial->SkipStep(Id);
			}
			// Locked sprint: a double click walks; locked inventory: I does nothing but a line; locked heat pack: U does nothing.
			DoubleClickAhead(World, Cast<ACodexTacticsPlayerController>(PC), Leader);
			Next(State);
			return true;
		}
		case 31:
		{
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			Check(State, Leader->IsMoving() && !Leader->IsSprinting(), FString::Printf(TEXT("locked double click: the leader walks (moving %d, sprinting %d)"),
				Leader->IsMoving() ? 1 : 0, Leader->IsSprinting() ? 1 : 0));
			Check(State, Tutorial->GetStepState(TEXT("A0_nosprint")) == ETutorialStepState::Completed
				&& CountMessages(World, TEXT("No running yet")) == State.NoSprintLinesBefore + 1, TEXT("TUT_A0_NOSPRINT in the feed once"));
			Leader->StopOperative();
			const int32 PacksBefore = Leader->HeatPacksCount;
			const int32 RefusalsBefore = Tutorial->GetLockedRefusalCount();
			Hud->ToggleInventoryDrawer();
			Check(State, !Hud->GetInventoryDrawer() || !Hud->GetInventoryDrawer()->IsOpen(), TEXT("locked inventory: the drawer stays closed"));
			Check(State, Tutorial->GetStepState(TEXT("A0_noinventory")) == ETutorialStepState::Completed && CountMessages(World, TEXT("Packs stay closed")) == 1,
				TEXT("TUT_A0_NOINVENTORY in the feed once"));
			Leader->ColdLevel = 50.f;
			Cast<ACodexTacticsPlayerController>(PC)->UseSquadItem(EPersonalItem::HeatPack);
			Check(State, Leader->HeatPacksCount == PacksBefore && Tutorial->GetLockedRefusalCount() == RefusalsBefore + 2,
				FString::Printf(TEXT("locked heat pack: U refused, the pack stays (%d -> %d, refusals +%d)"), PacksBefore, Leader->HeatPacksCount,
					Tutorial->GetLockedRefusalCount() - RefusalsBefore));
			Leader->ColdLevel = 0.f;
			// Beacon 1: A1 starts; sprint stays locked (rev 5).
			Check(State, Tutorial->StartBeat(TEXT("A1")), TEXT("A1 started"));
			Tutorial->SkipStep(TEXT("A0_move"));
			Tutorial->SkipStep(TEXT("A1_wt_start"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint) && Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen),
				TEXT("beacon 1 (beat A1): sprint and inventory STILL locked"));
			Check(State, EnterVolume(World, Leader, TEXT("VOL_A1_BEACON2")), TEXT("the crack volume fires"));
			Next(State);
			return true;
		}
		case 32:
		{
			if (Tutorial->GetStepState(TEXT("A1_select")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_select")) == ETutorialStepState::Active, TEXT("the crack: the select card"));
			Tutorial->SkipStep(TEXT("A1_wt_bridge"));
			Tap(PC, State, EKeys::Two); // select operative 2 through the real input path
			Next(State);
			return true;
		}
		case 33:
		{
			if (Tutorial->GetStepState(TEXT("A1_solo")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_select")) == ETutorialStepState::Completed, TEXT("key 2 selected a member and completed the card"));
			Check(State, Tutorial->GetStepState(TEXT("A1_solo")) == ETutorialStepState::Active, TEXT("the one-at-a-time bridge card"));
			Squad->SetLeaderByIndex(0);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 5.f; // no cold zone here: nobody reaches Chills in 20 s at x1
			}
			// The crossing itself is BridgeFenceSmoke's job: here the bridge's events drive the lesson.
			Tutorial->NotifyEvent(TEXT("BridgeRefused"), Squad->GetMembers()[1]->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge_refused")) == ETutorialStepState::Completed && CountMessages(World, TEXT("Only one on the bridge")) == 1,
				TEXT("the bridge creak feed line once"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Tutorial->NotifyEvent(TEXT("BridgeCrossed"), Member->GetFName());
				State.ColdAtSolo.Add(Member, Member->ColdLevel);
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_solo")) == ETutorialStepState::Completed, TEXT("three crossings complete the bridge card"));
			Next(State);
			return true;
		}
		case 34:
		{
			if (Tutorial->GetStepState(TEXT("A1_regroup")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Active, TEXT("regroup card after the bridge"));
			Squad->ToggleSoloMode(); // the squad stands in solo mode after the crossing
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 35:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (Tutorial->GetStepState(TEXT("A1_box")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Completed, TEXT("B: regrouped, the card done"));
			Check(State, Tutorial->GetStepState(TEXT("A1_box")) == ETutorialStepState::Active, TEXT("then the box select card"));
			Tutorial->SkipStep(TEXT("A1_box"));
			Next(State);
			return true;
		}
		case 36:
		{
			const ETutorialStepState Banter = Tutorial->GetStepState(TEXT("A1_wt_banter"));
			if (Banter != ETutorialStepState::Active && Banter != ETutorialStepState::Completed && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Banter == ETutorialStepState::Active || Banter == ETutorialStepState::Completed, TEXT("the squad banter plays after the bridge"));
			Tutorial->SkipStep(TEXT("A1_wt_banter"));
			Next(State);
			return true;
		}
		case 37:
		{
			if (Tutorial->GetStepState(TEXT("A1_gust")) == ETutorialStepState::Waiting && State.StageTime < 25.f)
			{
				return true;
			}
			const ETutorialStepState Gust = Tutorial->GetStepState(TEXT("A1_gust"));
			Check(State, Gust == ETutorialStepState::Active || Gust == ETutorialStepState::Completed,
				FString::Printf(TEXT("~20 s after the crossing with nobody at Chills: the gust plays (%s)"), TutorialRules::StateName(Gust)));
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, float>& Entry : State.ColdAtSolo)
			{
				if (const AOperativeCharacter* Member = Entry.Key.Get())
				{
					Check(State, Member->ColdLevel >= Entry.Value + 20.f - 0.01f && Member->ColdLevel >= 42.f - 0.01f,
						FString::Printf(TEXT("gust: %s %.0f%% -> %.0f%% (+20, at least 42)"), *Member->DisplayName.ToString(), Entry.Value, Member->ColdLevel));
				}
			}
			Next(State);
			return true;
		}
		case 38:
		{
			if (Tutorial->GetStepState(TEXT("A1_heatpack")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_heatpack")) == ETutorialStepState::Active, TEXT("heat pack card at Chills"));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen) && !Tutorial->IsCommandLocked(PlayerCommands::UseHeatPack),
				TEXT("the heat pack card UNLOCKS the inventory and the items"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("sprint still locked until the lesson is done"));
			const FTutorialCardView View = Tutorial->GetCardView();
			AOperativeCharacter* Focus = Tutorial->GetFocusOperative(TEXT("A1_heatpack"));
			const AOperativeCharacter* Now = Coldest(Squad);
			Check(State, Focus && View.FocusOperative == Focus && Now && Focus->ColdLevel >= Now->ColdLevel - 1.f && View.Body.ToString().Contains(Focus->DisplayName.ToString()),
				FString::Printf(TEXT("the card targets the coldest: %s %.0f%%, body '%s'"), Focus ? *Focus->DisplayName.ToString() : TEXT("-"), Focus ? Focus->ColdLevel : -1.f,
					*View.Body.ToString()));
			Hud->ToggleInventoryDrawer();
			Check(State, Hud->GetInventoryDrawer() && Hud->GetInventoryDrawer()->IsOpen(), TEXT("the inventory drawer opens now (unlocked)"));
			Hud->ToggleInventoryDrawer();
			UTacticalHeatSubsystem* Heat = World->GetSubsystem<UTacticalHeatSubsystem>();
			Check(State, Focus && Heat && Heat->UseHeatPack(Focus, Focus), TEXT("his own starter heat pack used on the coldest"));
			Check(State, Tutorial->GetStepState(TEXT("A1_heatpack")) == ETutorialStepState::Completed, TEXT("using a heat pack completes the card"));
			Next(State);
			return true;
		}
		case 39:
		{
			if (Tutorial->GetStepState(TEXT("A1_heatpack_mate")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_heatpack_mate")) == ETutorialStepState::Active, TEXT("another operative still >= 40 %: the mate card"));
			AOperativeCharacter* Focus = Tutorial->GetFocusOperative(TEXT("A1_heatpack_mate"));
			UTacticalHeatSubsystem* Heat = World->GetSubsystem<UTacticalHeatSubsystem>();
			Check(State, Focus && Heat && Heat->UseHeatPack(Focus, Focus), TEXT("the next coldest cracks his own starter pack"));
			Check(State, Tutorial->GetStepState(TEXT("A1_heatpack_mate")) == ETutorialStepState::Completed, TEXT("the mate card completes"));
			Next(State);
			return true;
		}
		case 40:
		{
			if (Tutorial->GetStepState(TEXT("A1_sprint")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_sprint")) == ETutorialStepState::Active, TEXT("'running warms you up': the sprint card after the lesson"));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("sprint UNLOCKED by the sprint card"));
			Check(State, Tutorial->GetStepState(TEXT("A1_obj_hut")) == ETutorialStepState::Idle && Tutorial->GetStepState(TEXT("A1_wt_resupply")) == ETutorialStepState::Idle,
				TEXT("no resupply line / hut objective before the sprint card is done"));
			Tutorial->SkipStep(TEXT("A1_wt_warm"));
			DoubleClickAhead(World, Cast<ACodexTacticsPlayerController>(PC), Leader);
			Next(State);
			return true;
		}
		case 41:
		{
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			Check(State, Leader->IsSprinting(), TEXT("a double click now sprints"));
			Check(State, Tutorial->GetStepState(TEXT("A1_sprint")) == ETutorialStepState::Completed, TEXT("Order.Sprint completes the sprint card"));
			Leader->StopOperative();
			Next(State);
			return true;
		}
		case 42:
		{
			const ETutorialStepState Resupply = Tutorial->GetStepState(TEXT("A1_wt_resupply"));
			if (Resupply != ETutorialStepState::Active && Resupply != ETutorialStepState::Completed && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Resupply == ETutorialStepState::Active || Resupply == ETutorialStepState::Completed, TEXT("WT_A1_RESUPPLY plays after the sprint card"));
			Check(State, Tutorial->GetStepState(TEXT("A1_obj_hut")) == ETutorialStepState::Active, TEXT("the hut objective (OBJ_A1_HUT) shows with it"));
			// The fishermen's hut: the crate with 3 heat packs, the leader walks in.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Side = Leader->GetActorRightVector().GetSafeNormal2D();
			ALootCrateActor* Crate = World->SpawnActor<ALootCrateActor>(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(),
				Leader->GetActorLocation() + Side * 250.f), FRotator::ZeroRotator, Params);
			FLootContents Contents;
			Contents.Medkits = Contents.CannedFood = Contents.Bread = Contents.Chocolate = Contents.Matches = 0;
			Contents.RifleAmmo = Contents.PistolAmmo = Contents.ShotgunAmmo = Contents.FlameFuel = Contents.CryoAmmo = Contents.PlasmaAmmo = 0;
			Contents.Firewood = 0;
			Contents.HeatPacks = 3;
			Crate->Contents = Contents;
			State.Crate = Crate;
			State.HeatPacksBeforeLoot = Leader->HeatPacksCount;
			Check(State, EnterVolume(World, Leader, TEXT("VOL_A1_HUT")), TEXT("the hut volume fires"));
			Next(State);
			return true;
		}
		case 43:
		{
			if (Tutorial->GetStepState(TEXT("A1_loot_open")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_open")) == ETutorialStepState::Active, TEXT("hut: 'open the crate' card"));
			World->GetSubsystem<UInteractionSubsystem>()->OpenLootDialog(State.Crate.Get());
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_open")) == ETutorialStepState::Completed, TEXT("opening the crate (Loot.Open) completes it"));
			Next(State);
			return true;
		}
		case 44:
		{
			if (Tutorial->GetStepState(TEXT("A1_loot_take")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Active, TEXT("'take the heat packs' card"));
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
			Interactions->LootItem(ELootItem::HeatPack);
			Interactions->CloseLootDialog();
			const int32 Expected = FMath::Min(TacticalHeatTuning::Get().HeatPackMaxStack, State.HeatPacksBeforeLoot + 3);
			Check(State, Leader->HeatPacksCount == Expected, FString::Printf(TEXT("the leader took what fits of the 3 heat packs (%d -> %d, stack max %d)"),
				State.HeatPacksBeforeLoot, Leader->HeatPacksCount, TacticalHeatTuning::Get().HeatPackMaxStack));
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Completed
				&& Tutorial->GetStepState(TEXT("A1_obj_hut")) == ETutorialStepState::Completed, TEXT("ItemLooted.HeatPack completes the card and the hut objective"));
			Tutorial->SkipStep(TEXT("A1_wt_hut"));
			Next(State);
			return true;
		}
		case 45:
		{
			const ETutorialStepState Stocked = Tutorial->GetStepState(TEXT("A1_wt_stocked"));
			if (Stocked != ETutorialStepState::Active && Stocked != ETutorialStepState::Completed && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Stocked == ETutorialStepState::Active || Stocked == ETutorialStepState::Completed, TEXT("the closing line plays after the loot"));
			Check(State, EnterVolume(World, Leader, TEXT("VOL_A1_BEACON3")), TEXT("beacon 3 volume fires"));
			Tutorial->SkipStep(TEXT("A1_wt_stocked"));
			Next(State);
			return true;
		}
		case 46:
		{
			const ETutorialStepState Formation = Tutorial->GetStepState(TEXT("A1_wt_regroup"));
			if (Formation != ETutorialStepState::Active && Formation != ETutorialStepState::Completed && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Formation == ETutorialStepState::Active || Formation == ETutorialStepState::Completed, TEXT("beacon 3: the formation lines"));
			Check(State, Tutorial->GetMachine().GetActiveCard().IsNone(), TEXT("no guard card at beacon 3 any more (guard moves to the horde beat)"));
			// Save round trip of the command history: the "tutorial" section carries it.
			const TSharedPtr<FJsonObject> Saved = Tutorial->CaptureSaveState();
			const TSharedPtr<FJsonObject>* History = nullptr;
			Check(State, Saved.IsValid() && Saved->TryGetObjectField(TEXT("history"), History) && (*History)->HasField(PlayerCommands::CameraPanKeys.ToString()),
				TEXT("the save's tutorial section carries the command history"));
			// The vision-cone toggle (user decision 2026-10-13): O through the real input path flips the player setting and reports View.VisionCones.
			const bool bConesBefore = UVisionConeComponent::IsPlayerEnemyConesOn();
			Tap(PC, State, EKeys::O);
			State.Focus = nullptr;
			State.NoSprintLinesBefore = bConesBefore ? 1 : 0;
			Next(State);
			return true;
		}
		case 47:
		{
			const bool bConesBefore = State.NoSprintLinesBefore == 1;
			Check(State, UVisionConeComponent::IsPlayerEnemyConesOn() != bConesBefore, TEXT("O toggles the player's enemy vision cones"));
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::ToggleVisionCones).Count == 1, TEXT("View.VisionCones reported once"));
			Check(State, CountMessages(World, TEXT("Vision cones")) >= 1, TEXT("a feed line says ON / OFF"));
			UVisionConeComponent::SetPlayerEnemyCones(bConesBefore);
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
