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
//  9. the REAL demo_01_steps.json, A0 -> A1 (rev 5b, user requests 2026-10-13): progressive unlock - inventory / stances / pause
//     locked at A0, move / camera free, sprint LOCKED (rev 5e: a double click during the radio WALKS, one "Save your strength" line);
//     the HQ radio is a talk on the move with the key
//     hint + SKIP on the strip; C and Space are refused with ONE "Not yet" feed line per family (none for a repeat); the first
//     camera card shows WHILE HQ talks (card and strip never overlap: laid-out rects, or the layout constants headless); W
//     completes the pan, Q/E pressed early PRECREDIT the Q/E stage after the MMB drag; Enter tap = the next radio line, Enter
//     held 0.6 s = the whole radio ends COMPLETED (Dialogue.Next / .Skip reported once each); the walk card (rev 5c)
//     follows the camera chain and is precredited (the leader walked during the radio; rev 5e: no run card in A0); a reset A0 + beacon 1 reached EARLY drops the camera chain and the walk card cleanly while
//     the radio plays on (SKIP ends it, A1's banter follows); the inventory drawer stays closed (TUT_A0_NOINVENTORY once), U
//     is refused; the crack:
//     rev 5c: a line + the crossing objective completed by three BridgeCrossed events, no select / solo card (the bridge actor
//     itself is BridgeFenceSmoke's job) -> the squad banter; 20 s after the crossing the gust (+20 cold, >= 42 %);
//     the heat-pack card UNLOCKS the inventory / items (the drawer opens now), the coldest uses his own starter pack, the mate
//     card; THEN (rev 5e) the sprint lesson 'double-click to run' unlocks sprint (sprint locked until then; a double click completes
//     it), THEN WT_A1_RESUPPLY + the hut objective; the hut:
//     the approach line, (rev 5i) the squad HELD in a real 8 x 8 m VOL_A1_HUT_APPROACH: Solo (B) first -> select (key 2) -> the
//     tactical entry; a click into the hut (a VOL_A1_HUT 10 m ahead) is refused with one "Hold here" line before Solo, after Solo,
//     before the "go in" card; cover in VOL_A1_DOOR_R before its card does not count; cover entered in VOL_A1_DOOR_L / _R by two
//     DIFFERENT operatives, through the event bus; with the "go in" card up the third operative is clicked into the hut, walks in,
//     completes it and the squad is free -> the loot cards and the closing line -> (rev 5d) the share card + objective: the
//     looter hands a heat pack to each mate with none (Shift + drag, into his kit), both complete -> regroup (B) -> box; (rev 5f) the squad is TELEPORTED past beacon 3 into the camp volume: A2 starts, beacon 3 is passed (its
//     lines never play), the cold / ruin lines, the push card (unlocks the push; a push outside VOL_A2_RUIN does not count), the light card,
//     the warm objective; the save's tutorial section carries the
//     command history; (rev 5i) the A3 approach: the squad is held, a standing click to the ridge is refused ("Crouch first"), C
//     crouches the leader, the click is accepted, beyond the ridge still refused, the followers copy the crouch and the card completes
//     when all are low in VOL_A3_RIDGE; (rev 5i) the fence one by one: held, nobody past before Solo, B -> "one by one 0 / 3",
//     standing refused ("Go prone first"), prone the first goes, a second refused meanwhile ("One at a time"), CrawlGapPassed frees
//     the turn ("1 / 3"), three through -> free; B (regroup) refused "Not yet - one by one.", each into his own cover (3 distinct),
//     then the peek card frees Regroup; O is refused in A1 and toggles the enemy vision cones after the A4 cone card; C crouches after the stance
//     lesson; Space stays refused in A4; tutorial OFF unlocks every command; the story dialogue window takes Enter the same way
//     (tap = next line, hold = skip); (rev 5i) SKIP TUTORIAL from A0: the button shows in A0, the pause menu entry asks and No
//     keeps it, the button asks with the world paused and Yes skips - no cards / strip / objectives, C and the inventory (locked at
//     A0) work, every command free, one feed line; the save says skipped, RESET starts it again, loading the save skips it again.
// Saves go to Saved/SmokeSaves/Tutorial (exclusive in Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
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
#include "UI/Frontend/CodexConfirmDialog.h"
#include "UI/Frontend/CodexPauseMenuScreen.h"
#include "UI/Frontend/CodexUISubsystem.h"
#include "UI/Frontend/CodexUITags.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "UObject/Package.h"

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
		/** Demo-file part: the hut crate, the cold of each operative when solo started. */
		TWeakObjectPtr<ALootCrateActor> Crate;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> ColdAtSolo;
		/** Rev 5b part: refusals / "Not yet" lines before a locked key, the radio line before Enter, the cone setting before O. */
		int32 RefusalsBefore = 0;
		int32 NotYetBefore = 0;
		int32 LineIndexBefore = 0;
		bool bConesBefore = false;
		/** The leader's heat packs just before he empties the hut crate. */
		int32 HeatPacksBeforeLoot = 0;
		TWeakObjectPtr<AOperativeCharacter> Focus;
		/** Rev 5c hut entry: the small VOL_A1_HUT round the third operative. */
		TWeakObjectPtr<ATutorialTriggerVolume> HutVolume;
		/** Rev 5i: the hut (a VOL_A1_HUT 10 m ahead, outside the approach area) and the confinement refusals before a check. */
		FVector HutPoint = FVector::ZeroVector;
		int32 ConfinedBefore = 0;
		int32 HoldLinesBefore = 0;
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

	/** Spawns a tutorial volume that starts BeatId (like VOL_A0_BEACON1) under the leader and enters it. */
	bool EnterBeatVolume(UWorld* World, AOperativeCharacter* Leader, FName VolumeId, FName BeatId)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATutorialTriggerVolume* Volume = World->SpawnActor<ATutorialTriggerVolume>(Leader->GetActorLocation() + FVector(0.f, 0.f, 3000.f),
			FRotator::ZeroRotator, Params);
		Volume->VolumeId = VolumeId;
		Volume->BeatId = BeatId;
		return Volume->TryActivate(Leader);
	}

	/** A tutorial volume (VolumeId, Any Operative, not fire-once) centred on Location: the hut / approach stand-ins. */
	ATutorialTriggerVolume* SpawnVolumeAt(UWorld* World, const FVector& Location, FName VolumeId, float HalfSizeCm)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		ATutorialTriggerVolume* Volume = World->SpawnActorDeferred<ATutorialTriggerVolume>(ATutorialTriggerVolume::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->VolumeId = VolumeId;
		Volume->ActivatedBy = ETutorialVolumeFilter::AnyOperative;
		Volume->bFireOnce = false;
		Volume->Box->SetBoxExtent(FVector(HalfSizeCm, HalfSizeCm, 150.f));
		Volume->FinishSpawning(Transform);
		return Volume;
	}

	/** A small tutorial volume (VolumeId, Any Operative, not fire-once) around one operative: the hut door / interior stand-ins. */
	ATutorialTriggerVolume* SpawnVolumeAround(UWorld* World, const AOperativeCharacter* Operative, FName VolumeId, float HalfSizeCm = 40.f)
	{
		return SpawnVolumeAt(World, Operative->GetActorLocation(), VolumeId, HalfSizeCm);
	}

	/** One click on the ground at Point through the controller's real click path. */
	void ClickAt(APlayerController* PC, const FVector& Point)
	{
		if (ACodexTacticsPlayerController* Codex = Cast<ACodexTacticsPlayerController>(PC))
		{
			Codex->HandleWorldHit(FHitResult(nullptr, nullptr, Point - FVector(0.f, 0.f, 90.f), FVector::UpVector));
		}
	}

	/** A settled cover entry of Operative as the cover code raises it (subject + slot), through the event bus. */
	void RaiseCoverEntered(UWorld* World, AOperativeCharacter* Operative)
	{
		FGameplayEventContext Context;
		Context.Subject = Operative;
		Context.Kind = FName(TEXT("High"));
		Context.Location = Operative->GetActorLocation();
		GameplayEvents::RaiseWithContext(World, GameplayEvents::CoverEntered, Context);
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
		if (!Leader || !Tutorial || !Saves || !PC || !Overlay || State.Time > 600.f)
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
			Saves->bCheckpointsEnabled = true;
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
			// Tutorial off: nothing shows. (Rev 5f: the fight beat passed the L0 volume card on the way - replay it to have one on screen.)
			Tutorial->ReplayStep(TEXT("solo_card"));
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
		// --- The real demo file (rev 5b, user requests 2026-10-13): talk on the move + camera lesson alongside, Enter / hold-Enter skip,
		// progressive unlock (C / Space refused at A0), early beacon 1, bridge, heat pack, hut ---
		case 27:
		{
			ACodexTacticsPlayerController* CodexPC = Cast<ACodexTacticsPlayerController>(PC);
			if (!CodexPC)
			{
				Check(State, false, TEXT("CodexTactics player controller"));
				return Finish(State, false);
			}
			Check(State, Tutorial->LoadStepsFile(TEXT("demo_01_steps.json")), TEXT("demo_01_steps.json loaded"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A0")) && Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen)
				&& Tutorial->IsCommandLocked(PlayerCommands::StanceCrouch) && Tutorial->IsCommandLocked(PlayerCommands::TacticalPause),
				TEXT("A0: the inventory, the stances and the pause locked from the start (progressive unlock)"));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::Move) && Tutorial->IsCommandLocked(PlayerCommands::Sprint)
				&& !Tutorial->IsCommandLocked(PlayerCommands::CameraPanKeys) && !Tutorial->IsCommandLocked(PlayerCommands::SelectMember),
				TEXT("A0: move, camera and selection free from the very start; sprint LOCKED (rev 5e, until the heat pack lesson)"));
			Tutorial->SetTutorialEnabled(false);
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::StanceCrouch) && !Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen)
				&& !Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("tutorial OFF: no locks (sprint included)"));
			Tutorial->SetTutorialEnabled(true);
			Check(State, Tutorial->LoadStepsFile(TEXT("demo_01_steps.json")) && Tutorial->IsCommandLocked(PlayerCommands::StanceCrouch),
				TEXT("reloaded after the OFF check: A0 locks back"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
				Member->HeatPacksCount = 1; // the demo loadout (level JSON heat_packs_count 1: one starter pack per operative)
				Member->SetStance(EOperativeStance::Standing);
			}
			State.NotYetBefore = CountMessages(World, TEXT("Not yet"));
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
			Check(State, Overlay->IsRadioSkipShown() && Overlay->GetShownRadioHint().ToString().Contains(TEXT("[Enter] next"))
				&& Overlay->GetShownRadioHint().ToString().Contains(TEXT("[Hold Enter] skip")),
				FString::Printf(TEXT("the strip shows SKIP and the key hint '%s'"), *Overlay->GetShownRadioHint().ToString()));
			Check(State, Tutorial->GetStepState(TEXT("A0_obj_beacon")) == ETutorialStepState::Active, TEXT("the beacon objective shows with the radio"));
			// Talk on the move (user request 2026-10-13): the squad may walk while HQ talks (rev 5e: a double click walks - sprint is
			// locked until the heat pack lesson). C and Space are not taught yet.
			State.LeaderStart = Leader->GetActorLocation();
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			DoubleClickAhead(World, Cast<ACodexTacticsPlayerController>(PC), Leader);
			Tap(PC, State, EKeys::C);
			State.Stage = 51;
			State.StageTime = 0.f;
			return true;
		}
		case 51:
			Tap(PC, State, EKeys::SpaceBar);
			State.Stage = 52;
			State.StageTime = 0.f;
			return true;
		case 52:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Leader->IsMoving() && !Leader->IsSprinting(), FString::Printf(TEXT("rev 5e: during the radio a double click WALKS (moving %d, sprinting %d)"),
				Leader->IsMoving() ? 1 : 0, Leader->IsSprinting() ? 1 : 0));
			Check(State, CountMessages(World, TEXT("Save your strength")) == 1 && Tutorial->GetStepState(TEXT("A0_nosprint")) == ETutorialStepState::Completed,
				TEXT("rev 5e: the locked sprint gets its one line (no card, no generic 'Not yet')"));
			Check(State, Leader->GetStance() == EOperativeStance::Standing, TEXT("C at A0: refused, the leader stays standing"));
			Check(State, !Flow || Flow->GetCombatMode() != ECodexCombatMode::TacticalPause, TEXT("Space at A0: refused, no tactical pause"));
			Check(State, Tutorial->GetLockedRefusalCount() >= State.RefusalsBefore + 3 && CountMessages(World, TEXT("Not yet")) == State.NotYetBefore + 2,
				FString::Printf(TEXT("two refusals, one short 'Not yet' line per family (stance, time): %d lines"), CountMessages(World, TEXT("Not yet")) - State.NotYetBefore));
			Leader->StopOperative();
			Tap(PC, State, EKeys::C);
			State.Stage = 53;
			State.StageTime = 0.f;
			return true;
		}
		case 53:
		{
			if (Tutorial->GetStepState(TEXT("A0_cam_pan")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, CountMessages(World, TEXT("Not yet")) == State.NotYetBefore + 2, TEXT("C again: no second 'Not yet' line for the same family"));
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Active,
				TEXT("the first camera card shows WHILE HQ talks"));
			Overlay->Refresh();
			Check(State, Overlay->IsCardShown() && Overlay->IsSubtitleShown(), TEXT("card and radio strip on screen together"));
			FSlateRect CardRect;
			FSlateRect StripRect;
			if (Overlay->GetCardAndStripRects(CardRect, StripRect))
			{
				Check(State, !FSlateRect::DoRectanglesIntersect(CardRect, StripRect), FString::Printf(TEXT("card (%.0f..%.0f) and strip (%.0f..%.0f) do not overlap"),
					CardRect.Left, CardRect.Right, StripRect.Left, StripRect.Right));
			}
			else
			{
				Check(State, UTutorialOverlayWidget::StripSideReserve >= UTutorialOverlayWidget::CardLeftMargin + UTutorialOverlayWidget::CardMaxWidth,
					TEXT("no layout headless: the strip keeps the card column free by construction"));
			}
			PressKey(PC, EKeys::W, IE_Pressed);
			State.Stage = 54;
			State.StageTime = 0.f;
			return true;
		}
		case 54:
		{
			if (State.StageTime < 1.3f)
			{
				return true;
			}
			PressKey(PC, EKeys::W, IE_Released);
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Completed, TEXT("W held: the pan stage done during the radio"));
			State.Stage = 55;
			State.StageTime = 0.f;
			return true;
		}
		case 55:
		{
			if (Tutorial->GetStepState(TEXT("A0_cam_pan_drag")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan_drag")) == ETutorialStepState::Active, TEXT("the MMB drag card next"));
			Tap(PC, State, EKeys::Q); // Q / E early: the Q/E card is precredited when it is due
			State.Stage = 56;
			State.StageTime = 0.f;
			return true;
		}
		case 56:
			Tap(PC, State, EKeys::E);
			State.Stage = 57;
			State.StageTime = 0.f;
			return true;
		case 57:
			PressKey(PC, EKeys::MiddleMouseButton, IE_Pressed);
			State.Stage = 58;
			State.StageTime = 0.f;
			return true;
		case 58:
		{
			if (State.StageTime < 0.8f)
			{
				return true;
			}
			PressKey(PC, EKeys::MiddleMouseButton, IE_Released);
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan_drag")) == ETutorialStepState::Completed, TEXT("a ~0.8 s MMB drag completes the drag stage"));
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_rotate_keys")) == ETutorialStepState::Completed && Tutorial->GetPrecreditedCount() == 1
				&& CountMessages(World, TEXT("already done")) == 1, FString::Printf(TEXT("Q/E precredited from the history (%d precredited)"), Tutorial->GetPrecreditedCount()));
			// Dialogue skip (user request 2026-10-13): Enter tap = the next line.
			const FTutorialSubtitleView View = Tutorial->GetSubtitleView();
			Check(State, View.bVisible && View.StepId == FName(TEXT("A0_radio_hq")) && View.LineIndex + 1 < View.LineCount,
				FString::Printf(TEXT("the radio still talks (line %d / %d)"), View.LineIndex + 1, View.LineCount));
			State.LineIndexBefore = View.LineIndex;
			Tap(PC, State, EKeys::Enter);
			State.Stage = 59;
			State.StageTime = 0.f;
			return true;
		}
		case 59:
		{
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			const FTutorialSubtitleView View = Tutorial->GetSubtitleView();
			Check(State, View.bVisible && View.LineIndex == State.LineIndexBefore + 1,
				FString::Printf(TEXT("Enter tap: the next line at once (%d -> %d)"), State.LineIndexBefore + 1, View.LineIndex + 1));
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::DialogueNext).Count == 1, TEXT("Dialogue.Next reported"));
			PressKey(PC, EKeys::Enter, IE_Pressed); // held: the whole radio is skipped after 0.6 s
			State.Stage = 60;
			State.StageTime = 0.f;
			return true;
		}
		case 60:
		{
			if (State.StageTime < 0.85f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Completed && !Overlay->IsSubtitleShown(),
				FString::Printf(TEXT("Enter held 0.6 s: the radio ends as if played out (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A0_radio_hq")))));
			PressKey(PC, EKeys::Enter, IE_Released);
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::DialogueSkip).Count == 1
				&& Tutorial->GetMachine().GetCommandUse(PlayerCommands::DialogueNext).Count == 1, TEXT("Dialogue.Skip reported once; the release adds no extra line"));
			Check(State, Tutorial->GetStepState(TEXT("A0_move")) == ETutorialStepState::Idle, TEXT("camera chain still running: no walk card yet"));
			for (const TCHAR* Id : { TEXT("A0_cam_rotate_mouse"), TEXT("A0_cam_zoom") })
			{
				Tutorial->SkipStep(Id);
			}
			State.Stage = 61;
			State.StageTime = 0.f;
			return true;
		}
		case 61:
		{
			if (Tutorial->GetStepState(TEXT("A0_move")) != ETutorialStepState::Completed && State.StageTime < 6.f)
			{
				// the queued zoom card shows after the breather: skip it once it is up
				Tutorial->SkipStep(TEXT("A0_cam_zoom"));
				return true;
			}
			// Rev 5c (user request 2026-10-13): 'click to walk' follows the camera chain at once; the leader already walked during the
			// radio (stage 28), so it is precredited. Rev 5e: no run card in A0 (sprint is taught after the heat pack lesson).
			Check(State, Tutorial->GetStepState(TEXT("A0_move")) == ETutorialStepState::Completed && Tutorial->GetPrecreditedCount() == 2
				&& CountMessages(World, TEXT("already done")) == 2 && Tutorial->GetMachine().GetCommandUse(PlayerCommands::Sprint).Count == 0,
				FString::Printf(TEXT("camera chain done: the walk card follows and is precredited (%d precredited; no sprint in the history)"),
					Tutorial->GetPrecreditedCount()));
			Check(State, Tutorial->GetStepState(TEXT("A0_obj_beacon")) == ETutorialStepState::Active, TEXT("the beacon objective still asks for beacon 1"));
			// Beacon 1 reached EARLY (user request 2026-10-13): a fresh A0, the radio and the first camera card still up.
			Tutorial->ResetTutorial();
			State.Stage = 62;
			State.StageTime = 0.f;
			return true;
		}
		case 62:
		{
			if ((Tutorial->GetStepState(TEXT("A0_cam_pan")) != ETutorialStepState::Active || Tutorial->GetStepState(TEXT("A0_radio_hq")) != ETutorialStepState::Active)
				&& State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Active,
				TEXT("reset: radio and the first camera card up again"));
			Check(State, EnterBeatVolume(World, Leader, TEXT("VOL_A0_BEACON1"), TEXT("A1")), TEXT("beacon 1 volume (beat A1) entered early"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A1")), TEXT("early beacon: A1 started"));
			Check(State, Tutorial->GetStepState(TEXT("A0_cam_pan")) == ETutorialStepState::Skipped && Tutorial->GetStepState(TEXT("A0_cam_zoom")) == ETutorialStepState::Skipped
				&& Tutorial->GetMachine().GetActiveCard() != FName(TEXT("A0_cam_pan")), TEXT("the leftover camera chain is dropped cleanly"));
			Check(State, Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A0_obj_beacon")) == ETutorialStepState::Completed,
				TEXT("the radio keeps playing; the beacon objective is done"));
			Check(State, Tutorial->SkipSpeech(), TEXT("the strip's SKIP (SkipSpeech) ends the radio"));
			Check(State, Tutorial->GetStepState(TEXT("A0_radio_hq")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A0_move")) == ETutorialStepState::Skipped,
				TEXT("radio completed; the A0 walk card is dropped, never shown"));
			State.Stage = 63;
			State.StageTime = 0.f;
			return true;
		}
		case 63:
		{
			const ETutorialStepState Start = Tutorial->GetStepState(TEXT("A1_wt_start"));
			if (Start != ETutorialStepState::Active && Start != ETutorialStepState::Completed && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Start == ETutorialStepState::Active || Start == ETutorialStepState::Completed, TEXT("A1's walk-and-talk plays after the radio"));
			Tutorial->SkipStep(TEXT("A1_wt_start"));
			Leader->StopOperative();
			const int32 PacksBefore = Leader->HeatPacksCount;
			Hud->ToggleInventoryDrawer();
			Check(State, !Hud->GetInventoryDrawer() || !Hud->GetInventoryDrawer()->IsOpen(), TEXT("locked inventory: the drawer stays closed"));
			Check(State, Tutorial->GetStepState(TEXT("A0_noinventory")) == ETutorialStepState::Completed && CountMessages(World, TEXT("Packs stay closed")) == 1,
				TEXT("TUT_A0_NOINVENTORY in the feed once (its own line, no generic one)"));
			const int32 RefusalsBefore = Tutorial->GetLockedRefusalCount();
			Leader->ColdLevel = 50.f;
			Cast<ACodexTacticsPlayerController>(PC)->UseSquadItem(EPersonalItem::HeatPack);
			Check(State, Leader->HeatPacksCount == PacksBefore && Tutorial->GetLockedRefusalCount() > RefusalsBefore,
				FString::Printf(TEXT("locked heat pack: U refused, the pack stays (%d -> %d)"), PacksBefore, Leader->HeatPacksCount));
			Leader->ColdLevel = 0.f;
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint) && Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen),
				TEXT("beacon 1 (beat A1): sprint and inventory still locked (rev 5e)"));
			Check(State, EnterVolume(World, Leader, TEXT("VOL_A1_BEACON2")), TEXT("the crack volume fires"));
			State.Stage = 32;
			State.StageTime = 0.f;
			return true;
		}
		case 32:
		{
			// Rev 5c (designer review 2026-10-13): the bridge teaches nothing - a short line + the crossing objective; the bridge
			// actor holds the followers itself (one at a time on its own). Selection + Solo are taught at the hut (stages 33-35).
			if (Tutorial->GetStepState(TEXT("A1_bridge")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge")) == ETutorialStepState::Active && Tutorial->GetMachine().GetActiveCard().IsNone(),
				TEXT("the crack: the crossing objective, no select / solo card"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::SoloOn), TEXT("the bridge unlocks nothing (solo is taught at the hut)"));
			Check(State, TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A1_wt_bridge"))),
				FString::Printf(TEXT("the bridge lines play at the approach (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_wt_bridge")))));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 5.f; // no cold zone here: nobody reaches Chills in 20 s at x1
			}
			// The crossing itself is BridgeFenceSmoke's job: here the bridge's events drive the lesson.
			Tutorial->NotifyEvent(TEXT("BridgeRefused"), Squad->GetMembers()[1]->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge_refused")) == ETutorialStepState::Completed && CountMessages(World, TEXT("One at a time on the bridge")) == 1,
				TEXT("the bridge creak feed line once"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Tutorial->NotifyEvent(TEXT("BridgeCrossed"), Member->GetFName());
				State.ColdAtSolo.Add(Member, Member->ColdLevel);
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge")) == ETutorialStepState::Completed, TEXT("three crossings complete the crossing objective"));
			// Rev 5e (game designer 2026-10-14): once across, what is left of the bridge talk is dropped (never played later).
			{
				const FTutorialSubtitleView Talk = Tutorial->GetSubtitleView();
				Check(State, Tutorial->GetStepState(TEXT("A1_wt_bridge")) == ETutorialStepState::Skipped
					&& (!Talk.bVisible || Talk.StepId != FName(TEXT("A1_wt_bridge")) || Talk.LineIndex + 1 >= Talk.LineCount),
					FString::Printf(TEXT("across: the bridge talk is cancelled (%s), at most the line on screen finishes"),
						TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_wt_bridge")))));
			}
			State.Stage = 36;
			State.StageTime = 0.f;
			return true;
		}
		case 33:
		{
			// The hut (rev 5i, game designer 2026-10-15): Solo first, then select, then the door; the squad is HELD in the approach area.
			if (Tutorial->GetStepState(TEXT("A1_solo")) != ETutorialStepState::Active && State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_solo")) == ETutorialStepState::Active,
				FString::Printf(TEXT("rev 5i: the hut approach: the Solo card on arrival (%.2f s)"), State.StageTime));
			Check(State, Tutorial->IsConfining(), TEXT("rev 5i: the squad is held in the approach area"));
			const ETutorialStepState Approach = Tutorial->GetStepState(TEXT("A1_wt_hut_approach"));
			Check(State, TutorialRules::IsRunning(Approach) || Approach == ETutorialStepState::Completed,
				FString::Printf(TEXT("the approach walk-and-talk plays (or waits for the line before it: %s)"), TutorialRules::StateName(Approach)));
			Tutorial->SkipStep(TEXT("A1_wt_hut_approach"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			State.HoldLinesBefore = CountMessages(World, TEXT("Hold here - follow the training."));
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1 && CountMessages(World, TEXT("Hold here - follow the training.")) == State.HoldLinesBefore + 1
				&& !Leader->HasOwnMoveOrder(), TEXT("rev 5i: a click into the hut before Solo: refused, one 'Hold here' line, no order"));
			Check(State, Squad->GetMembers()[2]->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Refused,
				TEXT("rev 5i: a direct order into the hut for another operative is refused too"));
			const FVector Near = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorRightVector() * 150.f);
			Check(State, Leader->OrderMoveTo(Near, false) == EOperativeOrderResult::Accepted, TEXT("rev 5i: a move inside the approach area is accepted"));
			Leader->StopOperative();
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 34:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (Tutorial->GetStepState(TEXT("A1_select")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A1_solo")) == ETutorialStepState::Completed, TEXT("B: solo on, the card done"));
			Check(State, Tutorial->GetStepState(TEXT("A1_select")) == ETutorialStepState::Active,
				TEXT("rev 5i: then the select card - shown although operatives were selected before (never precredited)"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::CoverEnter), TEXT("cover still locked before the door cards"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1 && Tutorial->IsConfining(),
				TEXT("rev 5i: after Solo, before the selection: the hut is still refused"));
			Tap(PC, State, EKeys::Two); // select operative 2 through the real input path
			Next(State);
			return true;
		}
		case 35:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_stack_l")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_select")) == ETutorialStepState::Completed, TEXT("key 2 selected a member and completed the card"));
			State.Stage = 72;
			State.StageTime = 0.f;
			return true;
		}
		case 72:
		{
			// The door (rev 5c): cover entered INSIDE VOL_A1_DOOR_L / _R by two different operatives (in_volume + distinct_from).
			// Rev 5i: in the lesson's order - the right side counts only once its card is up.
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_l")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::CoverEnter),
				TEXT("left of the door: the card shows and unlocks cover"));
			AOperativeCharacter* First = Squad->GetMembers()[0];
			AOperativeCharacter* Second = Squad->GetMembers()[1];
			RaiseCoverEntered(World, Second);
			Check(State, Tutorial->GetStepProgress(TEXT("A1_hut_stack_l")) == 0 && Tutorial->GetStepProgress(TEXT("A1_hut_stack_r")) == 0,
				TEXT("cover entered away from the door counts for neither side"));
			SpawnVolumeAround(World, Second, TEXT("VOL_A1_DOOR_R"));
			Check(State, Tutorial->GetTutorialVolumesAt(Second->GetActorLocation()).Contains(FName(TEXT("VOL_A1_DOOR_R"))), TEXT("the subject stands in VOL_A1_DOOR_R"));
			RaiseCoverEntered(World, Second);
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Queued,
				TEXT("rev 5i: cover right of the door before its card does not count (the lesson order)"));
			SpawnVolumeAround(World, Second, TEXT("VOL_A1_DOOR_L"));
			RaiseCoverEntered(World, Second);
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_l")) == ETutorialStepState::Completed, TEXT("an operative left of the door completes the left card"));
			SpawnVolumeAround(World, First, TEXT("VOL_A1_DOOR_R"));
			State.Stage = 75;
			State.StageTime = 0.f;
			return true;
		}
		case 75:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_stack_r")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Active, TEXT("then the right side card"));
			RaiseCoverEntered(World, Squad->GetMembers()[1]);
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Active, TEXT("the operative of the left side does not count on the right (distinct_from)"));
			RaiseCoverEntered(World, Squad->GetMembers()[0]);
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Completed, TEXT("another operative right of the door completes it"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			Check(State, Squad->GetMembers()[2]->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Refused
				&& Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1, TEXT("rev 5i: the hut stays closed until the 'go in' card is up"));
			Squad->GetMembers()[2]->StopOperative();
			State.Stage = 76;
			State.StageTime = 0.f;
			return true;
		}
		case 76:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_enter")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_enter")) == ETutorialStepState::Active && Tutorial->IsConfining(), TEXT("'go in': the card shows, still held"));
			Squad->SetLeaderByIndex(2);
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore && Squad->GetMembers()[2]->HasOwnMoveOrder(),
				TEXT("rev 5i: with the 'go in' card up a click into the hut is accepted (the third operative walks in)"));
			Next(State);
			return true;
		}
		case 77:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_enter")) != ETutorialStepState::Completed && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_enter")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("the third operative walked into the hut: 'go in' done (%s, %.1f s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_hut_enter"))), State.StageTime));
			Check(State, !Tutorial->IsConfining(), TEXT("rev 5i: the entry lesson done - the squad is free"));
			Squad->GetMembers()[2]->StopOperative();
			Squad->SetLeaderByIndex(0); // the loot stages count the first operative's heat packs (State.HeatPacksBeforeLoot)
			State.Stage = 43;
			State.StageTime = 0.f;
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
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("rev 5e: sprint still locked during the heat pack card"));
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
			Check(State, Tutorial->GetStepState(TEXT("A1_sprint")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::Sprint),
				TEXT("rev 5e: right after the heat pack lesson the sprint lesson ('double-click to run') shows and unlocks sprint"));
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
			Check(State, Tutorial->GetStepState(TEXT("A1_sprint")) == ETutorialStepState::Completed, TEXT("rev 5e: the double click completes the sprint lesson"));
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
			// Rev 5i: a REAL approach area round the squad (8 x 8 m: the confinement tests its box) and the hut 10 m ahead, outside it.
			{
				const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
				State.HutPoint = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Ahead * 1000.f);
				State.HutVolume = SpawnVolumeAt(World, State.HutPoint, TEXT("VOL_A1_HUT"), 150.f);
				ATutorialTriggerVolume* Approach = SpawnVolumeAt(World, Leader->GetActorLocation(), TEXT("VOL_A1_HUT_APPROACH"), 400.f);
				Check(State, FVector::Dist2D(State.HutPoint, Leader->GetActorLocation()) > 600.f,
					FString::Printf(TEXT("the hut stand-in lies outside the approach area (%.0f cm ahead)"), FVector::Dist2D(State.HutPoint, Leader->GetActorLocation())));
				Approach->ActivatedBy = ETutorialVolumeFilter::Leader;
				Approach->bFireOnce = true;
				Check(State, Approach->HasFired() || Approach->TryActivate(Leader), TEXT("the hut approach volume fires"));
			}
			State.Stage = 33;
			State.StageTime = 0.f;
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
			// Rev 2026-10-19: the take plays the search clip; the heat packs move at its grab (the leader must stand at the crate).
			if (ALootCrateActor* Crate = State.Crate.Get())
			{
				Leader->StopOperative();
				const FVector Spot = Crate->GetApproachPoint(Leader->GetActorLocation());
				Leader->TeleportTo(FVector(Spot.X, Spot.Y, Leader->GetActorLocation().Z), Leader->GetActorRotation(), false, true);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke the looter stands %.0f cm from the crate"), Crate->GetDistanceTo(Leader->GetActorLocation()));
			}
				State.HeatPacksBeforeLoot, Leader->HeatPacksCount, TacticalHeatTuning::Get().HeatPackMaxStack));
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Completed
			Check(State, State.Crate.IsValid() && State.Crate->IsSearching() && Leader->HeatPacksCount == State.HeatPacksBeforeLoot
				&& Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Active,
				TEXT("rev 2026-10-19: the take starts the search - the heat packs stay in the crate until its grab"));
			State.Stage = 29;
			State.StageTime = 0.f;
			return true;
		}
		case 29:
		{
			if (State.Crate.IsValid() && State.Crate->IsSearching() && State.StageTime < 6.f)
			{
				return true;
			}
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
				&& Tutorial->GetStepState(TEXT("A1_obj_hut")) == ETutorialStepState::Completed, TEXT("ItemLooted.HeatPack completes the card and the hut objective"));
			Tutorial->SkipStep(TEXT("A1_wt_hut"));
			State.Stage = 45;
			State.StageTime = 0.f;
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
			Tutorial->SkipStep(TEXT("A1_wt_stocked"));
			State.Stage = 73;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5d (designer request 2026-10-14): share the hut's heat packs - everyone carries one, at least one handed over.
		case 73:
		{
			const ETutorialStepState Card = Tutorial->GetStepState(TEXT("A1_share_card"));
			if (Card != ETutorialStepState::Active && Card != ETutorialStepState::Completed && State.StageTime < 8.f)
			{
				return true;
			}
			USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
			int32 MinPacks = MAX_int32;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				MinPacks = FMath::Min(MinPacks, Member->HeatPacksCount);
			}
			const bool bGivenBefore = Transfer && Transfer->GetHandedOverCount(ETransferItem::HeatPack) > 0;
			Check(State, Card == (MinPacks >= 1 && bGivenBefore ? ETutorialStepState::Completed : ETutorialStepState::Active)
				&& Tutorial->GetStepState(TEXT("A1_share")) == Card, FString::Printf(TEXT("after the loot: the share card + objective (fewest packs %d, handed over before %d)"),
					MinPacks, bGivenBefore ? 1 : 0));
			Check(State, Tutorial->GetConditionStat(TEXT("squad_min_heat_packs")) == static_cast<float>(MinPacks), TEXT("stat squad_min_heat_packs"));
			// The looter hands a pack to every mate with none (Shift + drag: into his kit, even when he is cold), else one to anybody.
			int32 Gave = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Transfer && Member != Leader && Member->HeatPacksCount == 0 && Leader->HeatPacksCount > 0)
				{
					FTransferRequest Give = FTransferRequest::MakeGive(Leader, Member, ETransferItem::HeatPack, 1);
					Give.bHandOverOnly = true;
					Gave += Transfer->Execute(Give);
				}
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Transfer && Gave == 0 && Member != Leader && Leader->HeatPacksCount > 1 && Member->HeatPacksCount < TacticalHeatTuning::Get().HeatPackMaxStack)
				{
					FTransferRequest Give = FTransferRequest::MakeGive(Leader, Member, ETransferItem::HeatPack, 1);
					Give.bHandOverOnly = true;
					Gave += Transfer->Execute(Give);
				}
			}
			Check(State, Gave > 0 || bGivenBefore, FString::Printf(TEXT("the looter handed %d heat pack(s) over (Shift + drag)"), Gave));
			Next(State);
			return true;
		}
		case 74:
		{
			if (Tutorial->GetStepState(TEXT("A1_share_card")) != ETutorialStepState::Completed && State.StageTime < 3.f)
			{
				return true;
			}
			int32 MinPacks = MAX_int32;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				MinPacks = FMath::Min(MinPacks, Member->HeatPacksCount);
			}
			Check(State, MinPacks >= 1 && Tutorial->GetConditionStat(TEXT("heat_packs_given")) >= 1.f
				&& Tutorial->GetStepState(TEXT("A1_share_card")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A1_share")) == ETutorialStepState::Completed,
				FString::Printf(TEXT("everyone carries a heat pack (fewest %d), handed over %.0f: the card and the objective done"), MinPacks,
					Tutorial->GetConditionStat(TEXT("heat_packs_given"))));
			State.Stage = 70;
			State.StageTime = 0.f;
			return true;
		}
		case 70:
		{
			if (Tutorial->GetStepState(TEXT("A1_regroup")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Active && Squad->IsSoloMode(), TEXT("regroup card after the loot (still in solo)"));
			PressKey(PC, EKeys::B, IE_Pressed);
			State.Stage = 71;
			State.StageTime = 0.f;
			return true;
		}
		case 71:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (Tutorial->GetStepState(TEXT("A1_box")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Completed, TEXT("B: regrouped, the card done"));
			Check(State, Tutorial->GetStepState(TEXT("A1_box")) == ETutorialStepState::Active, TEXT("then the box select card (moved from the bridge)"));
			Tutorial->SkipStep(TEXT("A1_box"));
			// Rev 5f (game designer 2026-10-14: the barrel lesson never started when the squad came to the camp another way): the
			// squad is TELEPORTED past beacon 3 (its volume never fires) straight into the camp volume (Beat Id A2, entered through
			// the real overlap path): A2 starts from A1, beacon 3 is passed, the ruin / barrel steps run.
			{
				const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
				const FVector Camp = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Ahead * 900.f);
				for (int32 Index = 0; Index < Squad->GetMembers().Num(); ++Index)
				{
					AOperativeCharacter* Member = Squad->GetMembers()[Index];
					Member->StopOperative();
					Member->SetActorLocation(Camp + FVector(0.f, (Index - 1) * 120.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
				}
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				ATutorialTriggerVolume* CampVolume = World->SpawnActorDeferred<ATutorialTriggerVolume>(ATutorialTriggerVolume::StaticClass(),
					FTransform(Leader->GetActorLocation()), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				CampVolume->VolumeId = TEXT("VOL_A2_CAMP");
				CampVolume->BeatId = TEXT("A2");
				CampVolume->Box->SetBoxExtent(FVector(300.f, 300.f, 150.f));
				CampVolume->FinishSpawning(FTransform(Leader->GetActorLocation()));
				CampVolume->CheckOverlapping(); // the leader stands in it (the overlap may already have fired on spawn)
				Check(State, CampVolume->HasFired() && Tutorial->GetCurrentBeat() == FName(TEXT("A2")),
					FString::Printf(TEXT("teleported past beacon 3 into the camp volume: A2 started (beat %s)"), *Tutorial->GetCurrentBeat().ToString()));
				Check(State, Tutorial->GetStepState(TEXT("A1_wt_regroup")) == ETutorialStepState::Skipped
					&& Tutorial->GetStepState(TEXT("A1_obj_camp")) == ETutorialStepState::Completed,
					TEXT("beacon 3 passed on the way (its lines never play), 'reach the camp' done"));
			}
			State.Stage = 80;
			State.StageTime = 0.f;
			return true;
		}
		case 80:
		{
			// Rev 5g (game designer 2026-10-14): the push card and the objective show ON ARRIVAL (<= ~1 s); the cold / ruin lines play
			// alongside (they used to come first: ~20 s of waiting for instructions). Nothing is skipped before the check.
			if (Tutorial->GetStepState(TEXT("A2_push")) != ETutorialStepState::Active && State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A2_push")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::Relocate),
				FString::Printf(TEXT("the barrel-into-the-ruin card shows on arrival (%.2f s) and unlocks the push (%s)"), State.StageTime,
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A2_push")))));
			Check(State, TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A2_obj_warm"))), TEXT("the 'warm up in the ruin' objective runs"));
			Check(State, TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A2_wt_cold"))),
				FString::Printf(TEXT("the cold lines run alongside the card (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A2_wt_cold")))));
			if (Tutorial->IsSpeechActive())
			{
				Tutorial->SkipSpeech(); // the smoke does not wait ~25 s for the talk
			}
			// The barrel set down outside the ruin does not count; inside VOL_A2_RUIN it does (the event's subject = the barrel).
			AOperativeCharacter* Third = Squad->GetMembers()[2];
			GameplayEvents::Raise(World, GameplayEvents::RelocationDone, Third);
			Check(State, Tutorial->GetStepState(TEXT("A2_push")) == ETutorialStepState::Active, TEXT("a push that ends outside the ruin does not count"));
			// 4 m: the follower may still walk to his formation slot after the teleport (rev 5g: the card shows at once now).
			SpawnVolumeAround(World, Third, TEXT("VOL_A2_RUIN"), 400.f);
			GameplayEvents::Raise(World, GameplayEvents::RelocationDone, Third);
			Check(State, Tutorial->GetStepState(TEXT("A2_push")) == ETutorialStepState::Completed, TEXT("the barrel pushed into the ruin completes the push card"));
			State.Stage = 81;
			State.StageTime = 0.f;
			return true;
		}
		case 81:
		{
			if (Tutorial->GetStepState(TEXT("A2_light")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A2_light")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::LightBarrel),
				TEXT("then the 'light it' card (unlocks the barrel fire)"));
			GameplayEvents::Raise(World, GameplayEvents::BarrelLit, Squad->GetMembers()[2]);
			Check(State, Tutorial->GetStepState(TEXT("A2_light")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A2_obj_warm")) == ETutorialStepState::Completed,
				TEXT("the barrel lit in the ruin: card and objective done"));
			State.Stage = 46;
			State.StageTime = 0.f;
			return true;
		}
		case 46:
		{
			Check(State, Tutorial->GetStepState(TEXT("A1_wt_regroup")) == ETutorialStepState::Skipped, TEXT("beacon 3's formation lines were passed (never played later)"));
			// Save round trip of the command history: the "tutorial" section carries it.
			const TSharedPtr<FJsonObject> Saved = Tutorial->CaptureSaveState();
			const TSharedPtr<FJsonObject>* History = nullptr;
			Check(State, Saved.IsValid() && Saved->TryGetObjectField(TEXT("history"), History) && (*History)->HasField(PlayerCommands::SelectMember.ToString()),
				TEXT("the save's tutorial section carries the command history"));
			// The vision-cone toggle (user decision 2026-10-13) is taught by its A4 card: O in A1 is refused.
			State.bConesBefore = UVisionConeComponent::IsPlayerEnemyConesOn();
			Tap(PC, State, EKeys::O);
			State.Focus = nullptr;
			Next(State);
			return true;
		}
		case 47:
		{
			Check(State, UVisionConeComponent::IsPlayerEnemyConesOn() == State.bConesBefore
				&& Tutorial->GetMachine().GetCommandUse(PlayerCommands::ToggleVisionCones).Count == 0, TEXT("O in A2: refused (taught by the A4 card)"));
			// Rev 5e (user bug 2026-10-14, "the tripwire cannot be disarmed"): the disarm used to unlock only when the A3 wire card SHOWED
			// (queued behind the field's cards for minutes). A wire spotted now: the disarm is free at once and a feed line says how.
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::DisarmTripwire), TEXT("A2: the tripwire disarm is not taught yet"));
			Tutorial->NotifyEvent(TEXT("TripwireSpotted"), Leader->GetFName());
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::DisarmTripwire) && !Tutorial->IsCommandLocked(PlayerCommands::DisarmMine)
				&& Tutorial->GetStepState(TEXT("A3_wire_spotted")) == ETutorialStepState::Completed && CountMessages(World, TEXT("Click the wire and choose Disarm")) == 1,
				FString::Printf(TEXT("a wire spotted: disarm unlocked at once (beat %s), the 'how to disarm' feed line posted"), *Tutorial->GetCurrentBeat().ToString()));
			State.Stage = 100;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5i item 3 (game designer 2026-10-15): the complex comes into view - the squad is stopped and moves up to the low ridge
		// only crouched. A real 8 x 8 m VOL_A3_APPROACH round the squad, the ridge front (VOL_A3_RIDGE, 8 x 8 m) 12 m ahead.
		case 100:
		{
			Tutorial->SkipStep(TEXT("A3_wire")); // the wire card of the spot above would hold the card slot
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->SetStance(EOperativeStance::Standing);
			}
			const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.HutPoint = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Ahead * 1200.f);
			Check(State, FVector::Dist2D(State.HutPoint, Leader->GetActorLocation()) > 900.f,
				FString::Printf(TEXT("the ridge stand-in lies ahead (%.0f cm)"), FVector::Dist2D(State.HutPoint, Leader->GetActorLocation())));
			SpawnVolumeAt(World, State.HutPoint, TEXT("VOL_A3_RIDGE"), 400.f);
			ATutorialTriggerVolume* Approach = SpawnVolumeAt(World, Leader->GetActorLocation(), TEXT("VOL_A3_APPROACH"), 400.f);
			Approach->ActivatedBy = ETutorialVolumeFilter::Leader;
			Approach->bFireOnce = true;
			Check(State, Approach->HasFired() || Approach->TryActivate(Leader), TEXT("the A3 approach volume fires"));
			Next(State);
			return true;
		}
		case 101:
		{
			if (Tutorial->GetStepState(TEXT("A3_low")) != ETutorialStepState::Active && State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_low")) == ETutorialStepState::Active && Tutorial->IsConfining() && Tutorial->GetConfineStance() == FName(TEXT("crouch")),
				FString::Printf(TEXT("rev 5i: the approach: the 'stay low' card, the squad held crouched (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A3_low")))));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::StanceCrouch) && !Tutorial->IsCommandLocked(PlayerCommands::SquadStanceCrouch),
				TEXT("rev 5i: the card unlocks crouch / squad crouch"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			State.HoldLinesBefore = CountMessages(World, TEXT("Crouch first - stay low."));
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1 && CountMessages(World, TEXT("Crouch first - stay low.")) == State.HoldLinesBefore + 1
				&& !Leader->HasOwnMoveOrder(), TEXT("rev 5i: standing, a click towards the ridge is refused ('Crouch first - stay low.')"));
			Tap(PC, State, EKeys::C); // the leader crouches; the followers copy it when they follow
			Next(State);
			return true;
		}
		case 102:
		{
			if (State.StageTime < 0.4f)
			{
				return true;
			}
			Check(State, Leader->GetStance() == EOperativeStance::Crouching, TEXT("C: the leader crouches"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			Check(State, Leader->OrderMoveTo(State.HutPoint + Leader->GetActorForwardVector().GetSafeNormal2D() * 1500.f, false)
				== EOperativeOrderResult::Refused, TEXT("rev 5i: crouched, beyond the ridge is still refused"));
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1 && Leader->HasOwnMoveOrder(),
				TEXT("rev 5i: crouched, the click to the ridge is accepted"));
			Next(State);
			return true;
		}
		case 103:
		{
			if (Tutorial->GetStepState(TEXT("A3_low")) != ETutorialStepState::Completed && State.StageTime < 30.f)
			{
				return true;
			}
			int32 Low = 0;
			FString Where;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				Low += Member->GetStance() != EOperativeStance::Standing ? 1 : 0;
				const FVector Off = Member->GetActorLocation() - State.HutPoint;
				Where += FString::Printf(TEXT(" %s (%.0f, %.0f, %.0f)"), *Member->DisplayName.ToString(), Off.X, Off.Y, Off.Z);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: squad round the ridge stand-in:%s"), *Where);
			Check(State, Tutorial->GetStepState(TEXT("A3_low")) == ETutorialStepState::Completed && Low == Squad->GetMembers().Num(),
				FString::Printf(TEXT("rev 5i: the whole squad reached the ridge crouched (the followers copied the leader: %d / %d low, %.1f s, not low in the volume %.0f)"),
					Low, Squad->GetMembers().Num(), State.StageTime, Tutorial->GetConditionStat(TEXT("squad_not_low_in.VOL_A3_RIDGE"))));
			Check(State, !Tutorial->IsConfining(), TEXT("rev 5i: done - the squad is free"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
			}
			Tutorial->SkipStep(TEXT("A3_stance"));
			Tutorial->SkipStep(TEXT("A3_vault"));
			State.Stage = 110;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5i item 4 (game designer 2026-10-15): the low fence ONE BY ONE - stop, Solo, then each operative prone under the gap, one at
		// a time; then each into his own cover; Regroup refused meanwhile. A real 8 x 8 m VOL_A3_FENCE round the squad, "past the fence"
		// 10 m ahead; the crawl-throughs are the fence actor's CrawlGapPassed events (raised here - the gap itself is CrawlGap / BridgeFence smokes' job).
		case 110:
		{
			if (Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.HutPoint = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Ahead * 1000.f);
			ATutorialTriggerVolume* Fence = SpawnVolumeAt(World, Leader->GetActorLocation(), TEXT("VOL_A3_FENCE"), 400.f);
			Fence->bFireOnce = true;
			Check(State, Fence->HasFired() || Fence->TryActivate(Leader), TEXT("the fence volume fires"));
			Next(State);
			return true;
		}
		case 111:
		{
			if (Tutorial->GetStepState(TEXT("A3_fence")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_fence")) == ETutorialStepState::Active && Tutorial->IsConfining()
				&& Tutorial->IsCommandLocked(PlayerCommands::Regroup), TEXT("rev 5i: the fence: the stop / Solo card, the squad held, Regroup locked"));
			State.ConfinedBefore = Tutorial->GetConfinedRefusalCount();
			ClickAt(PC, State.HutPoint);
			Check(State, Tutorial->GetConfinedRefusalCount() == State.ConfinedBefore + 1, TEXT("rev 5i: before Solo nobody may go past the fence"));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 112:
		{
			PressKey(PC, EKeys::B, IE_Released);
			if (Tutorial->GetStepState(TEXT("A3_fence_crawl")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A3_fence")) == ETutorialStepState::Completed
				&& Tutorial->GetStepState(TEXT("A3_fence_crawl")) == ETutorialStepState::Active, TEXT("rev 5i: B - Solo on, then the 'one by one' card"));
			Check(State, Tutorial->GetCardView().Title.ToString().Contains(TEXT("0 / 3")),
				FString::Printf(TEXT("rev 5i: the card counts '0 / 3 through' ('%s')"), *Tutorial->GetCardView().Title.ToString()));
			AOperativeCharacter* First = Squad->GetMembers()[0];
			AOperativeCharacter* Second = Squad->GetMembers()[1];
			State.HoldLinesBefore = CountMessages(World, TEXT("Go prone first - then crawl under."));
			Check(State, First->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Refused
				&& CountMessages(World, TEXT("Go prone first - then crawl under.")) == State.HoldLinesBefore + 1,
				TEXT("rev 5i: standing, past the fence is refused ('Go prone first - then crawl under.')"));
			First->DropProne();
			Second->DropProne();
			Next(State);
			return true;
		}
		case 113:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			AOperativeCharacter* First = Squad->GetMembers()[0];
			AOperativeCharacter* Second = Squad->GetMembers()[1];
			AOperativeCharacter* Third = Squad->GetMembers()[2];
			Check(State, First->GetStance() == EOperativeStance::Prone && First->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Accepted,
				TEXT("rev 5i: prone, the first one may crawl past the fence (his turn)"));
			State.HoldLinesBefore = CountMessages(World, TEXT("One at a time - wait until he is through."));
			Check(State, Second->OrderMoveTo(State.HutPoint + FVector(0.f, 150.f, 0.f), false) == EOperativeOrderResult::Refused
				&& CountMessages(World, TEXT("One at a time - wait until he is through.")) == State.HoldLinesBefore + 1,
				TEXT("rev 5i: a second one prone is refused while the first crosses ('One at a time')"));
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, First);
			Check(State, Tutorial->GetStepProgress(TEXT("A3_fence_crawl")) == 1 && Tutorial->GetCardView().Title.ToString().Contains(TEXT("1 / 3")),
				FString::Printf(TEXT("rev 5i: the first through: '1 / 3' ('%s')"), *Tutorial->GetCardView().Title.ToString()));
			Check(State, Second->OrderMoveTo(State.HutPoint + FVector(0.f, 150.f, 0.f), false) == EOperativeOrderResult::Accepted,
				TEXT("rev 5i: then the second one may go"));
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, Second);
			Check(State, Third->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Refused, TEXT("rev 5i: the third standing: refused"));
			Third->DropProne();
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, Third);
			Check(State, Tutorial->GetStepState(TEXT("A3_fence_crawl")) == ETutorialStepState::Completed && !Tutorial->IsConfining(),
				TEXT("rev 5i: three through: the crossing done, the squad free"));
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			State.NotYetBefore = CountMessages(World, TEXT("Not yet - one by one."));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 114:
		{
			PressKey(PC, EKeys::B, IE_Released);
			Check(State, Squad->IsSoloMode() && Tutorial->GetLockedRefusalCount() == State.RefusalsBefore + 1
				&& CountMessages(World, TEXT("Not yet - one by one.")) == State.NotYetBefore + 1,
				TEXT("rev 5i: B (regroup) refused after the crossing - 'Not yet - one by one.', still in Solo"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
			}
			Next(State);
			return true;
		}
		case 115:
		{
			if (Tutorial->GetStepState(TEXT("A3_cover")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_cover")) == ETutorialStepState::Active && Tutorial->GetCardView().Title.ToString().Contains(TEXT("0 / 3")),
				FString::Printf(TEXT("rev 5i: then 'take cover one by one' ('%s')"), *Tutorial->GetCardView().Title.ToString()));
			RaiseCoverEntered(World, Squad->GetMembers()[0]);
			RaiseCoverEntered(World, Squad->GetMembers()[0]);
			Check(State, Tutorial->GetStepProgress(TEXT("A3_cover")) == 1, TEXT("rev 5i: one operative counts once"));
			RaiseCoverEntered(World, Squad->GetMembers()[1]);
			RaiseCoverEntered(World, Squad->GetMembers()[2]);
			Check(State, Tutorial->GetStepState(TEXT("A3_cover")) == ETutorialStepState::Completed, TEXT("rev 5i: three different operatives in cover: done"));
			Next(State);
			return true;
		}
		case 116:
		{
			if (Tutorial->GetStepState(TEXT("A3_peek")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_peek")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::Regroup),
				TEXT("rev 5i: then the peek lesson, which frees Regroup"));
			if (Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
			}
			State.Stage = 104;
			State.StageTime = 0.f;
			return true;
		}
		case 104:
		{
			Check(State, Tutorial->TriggerStep(TEXT("A4_cones")) && Tutorial->GetCurrentBeat() == FName(TEXT("A4")), TEXT("A4 started with the cone card"));
			State.Stage = 69;
			State.StageTime = 0.f;
			return true;
		}
		case 69:
		{
			if (Tutorial->GetStepState(TEXT("A4_cones")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_cones")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::ToggleVisionCones),
				TEXT("the A4 cone card (shown 2 s after the beat start) unlocks O"));
			Tap(PC, State, EKeys::O);
			State.Stage = 64;
			State.StageTime = 0.f;
			return true;
		}
		case 64:
		{
			Check(State, UVisionConeComponent::IsPlayerEnemyConesOn() != State.bConesBefore, TEXT("O toggles the player's enemy vision cones"));
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::ToggleVisionCones).Count == 1, TEXT("View.VisionCones reported once"));
			Check(State, CountMessages(World, TEXT("Vision cones")) >= 1, TEXT("a feed line says ON / OFF"));
			UVisionConeComponent::SetPlayerEnemyCones(State.bConesBefore);
			// After the stance lesson (A3; A4 repeats it as a safety) C is accepted.
			Leader->SetStance(EOperativeStance::Standing);
			Tap(PC, State, EKeys::C);
			State.Stage = 65;
			State.StageTime = 0.f;
			return true;
		}
		case 65:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Leader->GetStance() == EOperativeStance::Crouching, TEXT("C after the stance lesson: the leader crouches"));
			Leader->SetStance(EOperativeStance::Standing);
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			Tap(PC, State, EKeys::SpaceBar);
			State.Stage = 66;
			State.StageTime = 0.f;
			return true;
		}
		case 66:
		{
			Check(State, Tutorial->GetLockedRefusalCount() == State.RefusalsBefore + 1, TEXT("Space in A4: still refused (the pause is taught in the horde beat)"));
			// Tutorial OFF (pause menu) unlocks everything, and switching it back on does not lock again.
			Tutorial->SetTutorialEnabled(false);
			Tutorial->SetTutorialEnabled(true);
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::TacticalPause) && !Tutorial->IsCommandLocked(PlayerCommands::GrenadeAim)
				&& !Tutorial->IsCommandLocked(PlayerCommands::VehicleBoard), TEXT("tutorial OFF: every command unlocked (stays so when it is back on)"));
			// Rev 5i: tutorial OFF also releases a confinement in force (an inline step holding the squad round the leader).
			{
				FTutorialStepsData HoldData;
				TArray<FString> HoldWarnings;
				FString HoldError;
				TutorialRules::ParseSteps(TEXT(R"JSON({ "id": "hold", "start_beat": "H", "beats": [ { "id": "H", "checkpoint": false } ], "steps": [ { "id": "hold", "beat": "H", "kind": "hint_card", "trigger": { "type": "manual" }, "confine": { "volume": "VOL_SMOKE_HOLD" }, "complete": { "commands": ["Squad.Solo"] } } ] })JSON"),
					HoldData, HoldWarnings, HoldError);
				SpawnVolumeAt(World, Leader->GetActorLocation(), TEXT("VOL_SMOKE_HOLD"), 300.f);
				Tutorial->LoadSteps(HoldData);
				Tutorial->TriggerStep(TEXT("hold"));
				const FVector Far = Leader->GetActorLocation() + FVector(5000.f, 0.f, 0.f);
				Check(State, Tutorial->IsConfining() && Tutorial->JudgeMove(*Leader, Far, false) == ETutorialConfineVerdict::Outside,
					FString::Printf(TEXT("rev 5i: an inline confinement holds the squad (%s)"), *HoldError));
				Tutorial->SetTutorialEnabled(false);
				Tutorial->SetTutorialEnabled(true);
				Check(State, !Tutorial->IsConfining() && Tutorial->JudgeMove(*Leader, Far, false) == ETutorialConfineVerdict::Allowed,
					TEXT("rev 5i: tutorial OFF releases the confinement (and it stays released when the tutorial is back on)"));
			}
			// The story dialogue window takes the same key: Enter tap = next line, Enter held = skip.
			UDialogueSequenceAsset* Sequence = NewObject<UDialogueSequenceAsset>(GetTransientPackage());
			Sequence->Title = TEXT("Smoke");
			for (const TCHAR* Text : { TEXT("Line one."), TEXT("Line two."), TEXT("Line three.") })
			{
				FDialogueLine Line;
				Line.Text = Text;
				Sequence->Lines.Add(Line);
			}
			UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Dialogue->StartDialogue(Sequence);
			Check(State, Dialogue->IsDialogueOpen() && Dialogue->GetLineIndex() == 0, TEXT("story dialogue open"));
			Tap(PC, State, EKeys::Enter);
			State.Stage = 67;
			State.StageTime = 0.f;
			return true;
		}
		case 67:
		{
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			const UDialogueSubsystem* Dialogue = World->GetSubsystem<UDialogueSubsystem>();
			Check(State, Dialogue->IsDialogueOpen() && Dialogue->GetLineIndex() == 1, FString::Printf(TEXT("story dialogue: Enter tap -> line %d"), Dialogue->GetLineIndex() + 1));
			PressKey(PC, EKeys::Enter, IE_Pressed);
			State.Stage = 68;
			State.StageTime = 0.f;
			return true;
		}
		case 68:
		{
			if (State.StageTime < 0.85f)
			{
				return true;
			}
			Check(State, !World->GetSubsystem<UDialogueSubsystem>()->IsDialogueOpen(), TEXT("story dialogue: Enter held 0.6 s skips the rest"));
			PressKey(PC, EKeys::Enter, IE_Released);
			State.Stage = 90;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5i (game designer 2026-10-15): SKIP TUTORIAL at the very start - the button during A0 and the pause menu entry, both confirmed.
		case 90:
		{
			Check(State, Tutorial->LoadStepsFile(TEXT("demo_01_steps.json")) && Tutorial->GetCurrentBeat() == FName(TEXT("A0")), TEXT("skip: the demo tutorial again from A0"));
			Overlay->Refresh();
			Check(State, Overlay->IsSkipTutorialShown() && Tutorial->IsSkipOfferShown(), TEXT("rev 5i: A0 shows the SKIP TUTORIAL button"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen) && Tutorial->IsCommandLocked(PlayerCommands::StanceCrouch),
				TEXT("A0 locks are in force before the skip"));
			UCodexUISubsystem* UI = UCodexUISubsystem::Get(World);
			ACodexTacticsHUD* GameHud = Cast<ACodexTacticsHUD>(PC->GetHUD());
			if (!UI || !GameHud)
			{
				Check(State, false, TEXT("UI subsystem + HUD for the skip dialog"));
				return Finish(State, false);
			}
			// The pause menu entry: confirm dialog, No keeps the tutorial.
			Check(State, GameHud->HandleEscape() && UGameplayStatics::IsGamePaused(World), TEXT("Esc: pause menu"));
			UCodexPauseMenuScreen* Pause = Cast<UCodexPauseMenuScreen>(UI->GetTopScreen(CodexUITags::Layer_GameMenu));
			Check(State, Pause != nullptr, TEXT("pause menu on the GameMenu layer"));
			if (Pause)
			{
				Pause->ActivateEntry(TEXT("SkipTutorial"));
				UCodexConfirmDialog* Ask = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal));
				Check(State, Ask != nullptr, TEXT("rev 5i: pause menu SKIP TUTORIAL asks for confirmation"));
				if (Ask)
				{
					Ask->Cancel();
				}
				Check(State, !Tutorial->IsTutorialSkipped() && Tutorial->IsTutorialActive(), TEXT("rev 5i: No keeps the tutorial"));
				Pause->Resume();
			}
			// The on-screen button: the world pauses while it asks; Yes skips.
			Overlay->RequestSkipTutorial();
			UCodexConfirmDialog* Confirm = Cast<UCodexConfirmDialog>(UI->GetTopScreen(CodexUITags::Layer_Modal));
			Check(State, Confirm != nullptr && UGameplayStatics::IsGamePaused(World), TEXT("rev 5i: the SKIP TUTORIAL button asks (Yes / No) with the world paused"));
			if (Confirm)
			{
				Confirm->Confirm();
			}
			Check(State, Tutorial->IsTutorialSkipped() && !Tutorial->IsTutorialActive() && !UGameplayStatics::IsGamePaused(World),
				TEXT("rev 5i: Yes skips the tutorial, the world runs again"));
			Leader->SetStance(EOperativeStance::Standing);
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			Tap(PC, State, EKeys::C);
			Next(State);
			return true;
		}
		case 91:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Overlay->Refresh();
			Check(State, !Overlay->IsCardShown() && !Overlay->IsSubtitleShown() && Overlay->GetShownObjectiveCount() == 0 && !Overlay->IsSkipTutorialShown(),
				TEXT("rev 5i: skipped - no card, no radio strip, no objectives, no SKIP button"));
			Check(State, Leader->GetStance() == EOperativeStance::Crouching && Tutorial->GetLockedRefusalCount() == State.RefusalsBefore,
				TEXT("rev 5i: skipped - C (locked at A0) is accepted: the leader crouches"));
			Leader->SetStance(EOperativeStance::Standing);
			bool bAllFree = true;
			for (const FPlayerCommandInfo& Info : PlayerCommands::GetRegistry())
			{
				bAllFree &= !Tutorial->IsCommandLocked(Info.Id);
			}
			Check(State, bAllFree && UTutorialSubsystem::FilterCommand(World, PlayerCommands::Sprint) == PlayerCommands::Sprint,
				TEXT("rev 5i: skipped - every command is accepted (sprint sprints)"));
			Hud->ToggleInventoryDrawer();
			Check(State, Hud->GetInventoryDrawer() && Hud->GetInventoryDrawer()->IsOpen(), TEXT("rev 5i: skipped - the inventory opens (locked at A0)"));
			Hud->ToggleInventoryDrawer();
			Check(State, CountMessages(World, TEXT("Tutorial skipped")) == 1, TEXT("rev 5i: one 'Tutorial skipped' feed line"));
			// Save round trip: a load keeps it skipped (RESET TUTORIAL in between starts it again, the load skips it again).
			const TSharedPtr<FJsonObject> Saved = Tutorial->CaptureSaveState();
			bool bSavedSkipped = false;
			Check(State, Saved.IsValid() && Saved->TryGetBoolField(TEXT("skipped"), bSavedSkipped) && bSavedSkipped, TEXT("rev 5i: the save's tutorial section says skipped"));
			Check(State, Saves->SaveGame(TEXT("skiptest"), TEXT("Skip test")), TEXT("saved the skipped run"));
			Tutorial->ResetTutorial();
			Check(State, !Tutorial->IsTutorialSkipped() && Tutorial->IsTutorialActive(), TEXT("rev 5i: RESET TUTORIAL starts it again"));
			Check(State, Saves->LoadGame(TEXT("skiptest")), TEXT("loaded the skipped run in place"));
			Next(State);
			return true;
		}
		case 92:
		{
			if (!Tutorial->IsTutorialSkipped() && State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Tutorial->IsTutorialSkipped() && !Tutorial->IsTutorialActive() && !Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen),
				TEXT("rev 5i: after the load the tutorial stays skipped"));
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
