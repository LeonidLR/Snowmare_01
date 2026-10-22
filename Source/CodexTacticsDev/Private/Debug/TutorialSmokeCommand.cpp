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
//     standing refused ("Go prone first"), prone the first goes, (2026-10-20) a second prone goes at the same time (no queue), any order
//     through ("1 / 3"), three through -> free; B (regroup) refused "Not yet - each operative crosses ...", each into his own cover (3 distinct),
//     then the peek card frees Regroup; (rev 2026-10-19) a mine blast beside the engineer (everyone carries a medkit) -> the MEDKIT card
//     focused on him (Item.Use / Inventory.Open free), H on a mate does not count, he presses H -> healed, the card completes; O is refused in A1 and toggles the enemy vision cones after the A4 cone card; C crouches after the stance
//     lesson; Space stays refused in A4; tutorial OFF unlocks every command; the story dialogue window takes Enter the same way
//     (tap = next line, hold = skip); (rev 5i) SKIP TUTORIAL from A0: the button shows in A0, the pause menu entry asks and No
//     keeps it, the button asks with the world paused and Yes skips - no cards / strip / objectives, C and the inventory (locked at
//     A0) work, every command free, one feed line; the save says skipped, RESET starts it again, loading the save skips it again;
//     2026-10-21 (the A4 ambush in two parts, each bound to ITS zombie by the demo file's "enemies"): the smoke's two routes carry the
//     lake's route labels as tags; the far zombie's TripwireTriggered.Enemy does not complete part 1, the perimeter zombie's does; the
//     whistle lures the far zombie (part 2), the close-shot card's target follows him, the perimeter zombie "killed" beside the squad does
//     not complete it, the far one killed off his route within 10 m does (kill EVENTS here; TutorialAmbushLakeSmoke shoots for real);
// 10. (2026-10-20, the current navigation target) stages 140-153 on a runtime course (the lake's volumes + a bridge + the bridge marker):
//     the squad crosses the "crevasse" WITHOUT the bridge beacon (teleported across beside the bridge) -> the bridge objective is passed
//     (never shown, its marker never appears), the target jumps to the hut approach; the whole hut lesson (Solo B, select 2, both door
//     sides, the third goes in, loot, take, share, regroup B) -> no bridge marker / target ever, and right after the lesson the target
//     is beacon 3 (the guide beacon stands there); at every check exactly one target, at most one edge arrow, the arrow (headless: the
//     1920 x 1080 camera view) points at the target when it is off-screen (the camera looks away), its distance label in m, the minimap
//     highlights exactly one objective icon; beacon 3 reached -> the camp.
// Saves go to Saved/SmokeSaves/Tutorial (exclusive in Scripts/test_map.json).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class APatrolRouteActor;

#include "Characters/FirePostureRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Core/PlayerCommands.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
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
#include "Misc/App.h"
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
#include "Components/SplineComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Data/DialogueSequenceAsset.h"
#include "UObject/Package.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Camera/TacticalCameraPawn.h"
#include "AI/PatrolRouteActor.h"
#include "Level/BridgeCrossingVolume.h"
#include "UI/MinimapSubsystem.h"

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
		/** 2026-10-19 b: the patrol enemy lured in the A4 lure lesson (stages 130-136). */
		TWeakObjectPtr<AEnemyCharacter> LureEnemy;
		/** 2026-10-19 b: the symmetric-sight card (stages 137-139): a 1.2 m wall and an enemy behind it. */
		TWeakObjectPtr<AActor> SightWall;
		TWeakObjectPtr<AEnemyCharacter> SightEnemy;
		/** The leader's heat packs just before he empties the hut crate. */
		int32 HeatPacksBeforeLoot = 0;
		TWeakObjectPtr<AOperativeCharacter> Focus;
		/** The engine frame of the last step (one step per frame). */
		uint64 LastFrame = 0;
		/** Rev 2026-10-19 medkit lesson: the leader before it (given back afterwards). */
		TWeakObjectPtr<AOperativeCharacter> LeaderBeforeMedkit;
		/** Rev 5c hut entry: the small VOL_A1_HUT round the third operative. */
		TWeakObjectPtr<ATutorialTriggerVolume> HutVolume;
		/** Rev 5i: the hut (a VOL_A1_HUT 10 m ahead, outside the approach area) and the confinement refusals before a check. */
		FVector HutPoint = FVector::ZeroVector;
		int32 ConfinedBefore = 0;
		/** Bug 2026-10-20 (stage 107): per follower - stood up, sprinted, best speed / the leader's. */
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> SprintProbe;
		/** ... and where the squad stood before that sprint (put back after it: the later stages build round the squad). */
		TMap<TWeakObjectPtr<AOperativeCharacter>, FTransform> SprintHome;
		TMap<TWeakObjectPtr<AOperativeCharacter>, float> SprintCold;
		int32 HoldLinesBefore = 0;
		/** 2026-10-20 guide course (stages 140-153): its origin / axes, the bridge marker, checks made. */
		FVector CourseOrigin = FVector::ZeroVector;
		FVector CourseForward = FVector::ForwardVector;
		FVector CourseRight = FVector::RightVector;
		TWeakObjectPtr<ATutorialMarkerActor> BridgeMarker;
		bool bBridgeMarkerEverShown = false;
		int32 GuideArrowChecks = 0;
		/** 2026-10-20 ambush by one operative: the patrol route of the wire spot, a held mate. */
		TWeakObjectPtr<APatrolRouteActor> AmbushRoute;
		/** 2026-10-21: part 2's route (the A4_LURE binding) and part 1's zombie (on AmbushRoute, the A4_PERIMETER binding). */
		TWeakObjectPtr<APatrolRouteActor> LureRoute;
		TWeakObjectPtr<AEnemyCharacter> PerimeterEnemy;
		bool bSawWallCards = false;
		TWeakObjectPtr<AOperativeCharacter> AmbushMate;
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


	/** 2026-10-20 guide course: a lake volume at Location (box HalfSizeCm), Leader-activated and fire-once like the map's beacons. */
	ATutorialTriggerVolume* SpawnCourseVolume(UWorld* World, const FVector& Location, FName VolumeId, FName BeatId, float HalfSizeCm)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		ATutorialTriggerVolume* Volume = World->SpawnActorDeferred<ATutorialTriggerVolume>(ATutorialTriggerVolume::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Volume)
		{
			return nullptr;
		}
		Volume->VolumeId = VolumeId;
		Volume->BeatId = BeatId;
		Volume->ActivatedBy = ETutorialVolumeFilter::Leader;
		Volume->bFireOnce = true;
		Volume->Box->SetBoxExtent(FVector(HalfSizeCm, HalfSizeCm, 150.f));
		Volume->FinishSpawning(Transform);
		return Volume;
	}

	/** Teleports the squad side by side (1.2 m apart along Right) to Point and stops everyone. */
	void TeleportSquadTo(USquadSubsystem* Squad, const FVector& Point, const FVector& Right)
	{
		for (int32 Index = 0; Index < Squad->GetMembers().Num(); ++Index)
		{
			AOperativeCharacter* Member = Squad->GetMembers()[Index];
			Member->StopOperative();
			Member->SetActorLocation(Point + Right * ((Index - 1) * 120.f), false, nullptr, ETeleportType::TeleportPhysics);
			Member->ColdLevel = 5.f; // no cold on this course: the heat pack lesson stays out of the way
		}
	}

	/** The volume with this id that the leader stands in fires (the overlap may have fired on the teleport already). */
	void FireCourseVolume(UWorld* World, FName VolumeId)
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			if (It->GetEffectiveVolumeId() == VolumeId)
			{
				It->CheckOverlapping();
			}
		}
	}

	/**
	 * 2026-10-20: exactly one current target (valid, the expected one when given), never the bridge, at most one edge arrow; the
	 * bridge marker never shown; the minimap highlights exactly one objective icon (once it is set up).
	 */
	void CheckOneGuide(FState& State, UWorld* World, UTutorialSubsystem* Tutorial, UTutorialOverlayWidget* Overlay, const TCHAR* Where, FName Expect,
		bool bBridgeAhead = false)
	{
		Overlay->Refresh();
		const FTutorialGuideView& Guide = Tutorial->GetGuideView();
		const FString Described = UTutorialSubsystem::DescribeGuide(Guide.Guide);
		Check(State, Guide.bValid && (Expect.IsNone() || Guide.Guide.Id == Expect),
			FString::Printf(TEXT("guide %s: exactly one current target %s (expected %s)"), Where, *Described, Expect.IsNone() ? TEXT("any") : *Expect.ToString()));
		// Before the crossing the bridge approach IS the next place; once across it must never be the target again.
		Check(State, (bBridgeAhead || (Guide.Guide.Id != FName(TEXT("A1_bridge")) && Guide.Guide.Id != FName(TEXT("VOL_A1_BEACON2")))) && Overlay->GetEdgePointerCount() <= 1,
			FString::Printf(TEXT("guide %s: %s, %d edge arrow(s)"), Where, bBridgeAhead ? TEXT("the bridge lies ahead") : TEXT("never the bridge"), Overlay->GetEdgePointerCount()));
		State.bBridgeMarkerEverShown |= State.BridgeMarker.IsValid() && State.BridgeMarker->IsMarkerShown();
		Check(State, !State.bBridgeMarkerEverShown, FString::Printf(TEXT("guide %s: the bridge marker never appeared"), Where));
		const UMinimapSubsystem* Minimap = World->GetSubsystem<UMinimapSubsystem>();
		if (Minimap && Minimap->IsReady())
		{
			TArray<FMinimapIcon> Icons;
			Minimap->BuildIcons(Icons);
			int32 Highlighted = 0;
			for (const FMinimapIcon& Icon : Icons)
			{
				Highlighted += Icon.Kind == EMinimapIconKind::Objective && Icon.bHighlight && FVector::Dist2D(Icon.World, Guide.Location) < 1.f ? 1 : 0;
			}
			Check(State, Highlighted == 1, FString::Printf(TEXT("guide %s: the minimap highlights the target (%d icon(s))"), Where, Highlighted));
		}
	}

	/** 2026-10-20: the edge arrow agrees with the camera view - off-screen: one arrow along the target's direction from the centre. */
	void CheckGuideArrow(FState& State, APlayerController* PC, UTutorialSubsystem* Tutorial, UTutorialOverlayWidget* Overlay, const TCHAR* Where)
	{
		Overlay->Refresh();
		const FTutorialGuideView& Guide = Tutorial->GetGuideView();
		const APlayerCameraManager* Camera = PC->PlayerCameraManager;
		if (!Guide.bValid || !Camera)
		{
			Check(State, false, FString::Printf(TEXT("arrow %s: a target and a camera"), Where));
			return;
		}
		FVector2D Screen;
		const FVector2D View = Overlay->GetPointerViewSize();
		TutorialRules::ProjectToView(Camera->GetCameraLocation(), Camera->GetCameraRotation(), Camera->GetFOVAngle(), View, Guide.Location, Screen);
		FVector2D ExpectTip;
		FVector2D ExpectDirection;
		const bool bOffScreen = TutorialRules::ComputeEdgePointer(View, Screen, true, 48.f, ExpectTip, ExpectDirection);
		FVector2D Tip;
		FVector2D Direction;
		FText Distance;
		FVector2D ViewSize;
		const bool bArrow = Overlay->GetGuidePointer(Tip, Direction, Distance, ViewSize);
		// Independent of the projection: the target's side in the camera's own axes (right = +X on screen, up = -Y).
		const FVector Local = Camera->GetCameraRotation().UnrotateVector(Guide.Location - Camera->GetCameraLocation());
		const FVector2D Side = FVector2D(Local.Y, -Local.Z).GetSafeNormal();
		if (bOffScreen)
		{
			++State.GuideArrowChecks;
			Check(State, bArrow && FVector2D::DotProduct(Direction, ExpectDirection) > 0.99f && Distance.ToString().EndsWith(TEXT(" m"))
				&& (Local.X > 0.f || FVector2D::DotProduct(Direction, Side) > 0.f),
				FString::Printf(TEXT("arrow %s: off-screen -> one arrow towards the target (dir %.2f, %.2f; expected %.2f, %.2f; '%s')"), Where,
					Direction.X, Direction.Y, ExpectDirection.X, ExpectDirection.Y, *Distance.ToString()));
		}
		else
		{
			Check(State, !bArrow, FString::Printf(TEXT("arrow %s: the target is on screen - no arrow"), Where));
		}
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
		// One step per engine frame on the GAME clock (rev 2026-10-19: under a loaded parallel batch the ticker's wall delta ran the stage
		// timers ahead of the game - a 0.1 s wait passed within the frame of the key press, a 15 s walk timed out after 8 s of game time).
		if (GFrameCounter == State.LastFrame)
		{
			return true;
		}
		State.LastFrame = GFrameCounter;
		const float GameDelta = static_cast<float>(FApp::GetDeltaTime());
		State.Time += GameDelta;
		State.StageTime += GameDelta;
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
			// 2026-10-20 pacing: the breather is 0.8 s (was 1.5 s) - the next card within ~1 s, but not in the same frame.
			Check(State, Tutorial->GetStepState(TEXT("regroup_card")) == ETutorialStepState::Active && State.StageTime >= 0.1f && State.StageTime <= 1.2f,
				FString::Printf(TEXT("next card after the 0.8 s breather (%.1f s after the stage)"), State.StageTime));
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
				Member->HealthComponent->Heal(10000.f); // rev 2026-10-19: nobody wounded - the medkit lesson comes later in this smoke
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
			// Rev 2026-10-19: the take plays the search clip; the heat packs move at its grab (the leader must stand at the crate).
			if (ALootCrateActor* Crate = State.Crate.Get())
			{
				Leader->StopOperative();
				const FVector Spot = Crate->GetApproachPoint(Leader->GetActorLocation());
				Leader->TeleportTo(FVector(Spot.X, Spot.Y, Leader->GetActorLocation().Z), Leader->GetActorRotation(), false, true);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke the looter stands %.0f cm from the crate"), Crate->GetDistanceTo(Leader->GetActorLocation()));
			}
			UInteractionSubsystem* Interactions = World->GetSubsystem<UInteractionSubsystem>();
			Interactions->LootItem(ELootItem::HeatPack);
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
			Interactions->CloseLootDialog();
			const int32 Expected = FMath::Min(TacticalHeatTuning::Get().HeatPackMaxStack, State.HeatPacksBeforeLoot + 3);
			Check(State, Leader->HeatPacksCount == Expected, FString::Printf(TEXT("the leader took what fits of the 3 heat packs (%d -> %d, stack max %d)"),
				State.HeatPacksBeforeLoot, Leader->HeatPacksCount, TacticalHeatTuning::Get().HeatPackMaxStack));
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Completed
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
			// 2026-10-22: the wire lines are beat-less (any PMC wire met first, in any beat): the beat stays A2, only the wire disarm is freed.
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::DisarmTripwire) && Tutorial->GetCurrentBeat() == FName(TEXT("A2"))
				&& Tutorial->GetStepState(TEXT("A3_wire_spotted")) == ETutorialStepState::Completed && CountMessages(World, TEXT("Click the wire and choose Disarm")) == 1,
				FString::Printf(TEXT("a wire spotted: disarm unlocked at once (beat %s, not moved), the 'how to disarm' feed line posted"), *Tutorial->GetCurrentBeat().ToString()));
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
			// Bug 2026-10-20: after a confinement released, the followers' crouch is the confinement's copy (not a stance key): the leader
			// runs (a sprint order stands him up) and they stand up and run with him. Warm (cold blocks a sprint for anybody).
			State.SprintProbe.Reset();
			State.SprintHome.Reset();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.SprintHome.Add(Member, Member->GetActorTransform());
				State.SprintCold.Add(Member, Member->ColdLevel);
				Member->ColdLevel = 0.f;
				if (Member != Leader)
				{
					State.SprintProbe.Add(Member, FVector::ZeroVector);
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s after the confinement: %s, stance from the squad %d"), *Member->DisplayName.ToString(),
						*UEnum::GetValueAsString(Member->GetStance()), Member->IsStanceFromSquad() ? 1 : 0);
				}
			}
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() - Leader->GetActorForwardVector().GetSafeNormal2D() * 2000.f), true);
			State.Stage = 107;
			State.StageTime = 0.f;
			return true;
		}
		case 107:
		{
			// Per follower: X = stood up, Y = sprinted, Z = best speed / the leader's while he sprints.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->ColdLevel = 0.f;
			}
			const float LeaderSpeed = static_cast<float>(Leader->GetVelocity().Size2D());
			for (TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Probe : State.SprintProbe)
			{
				if (const AOperativeCharacter* Member = Probe.Key.Get())
				{
					Probe.Value.X = FMath::Max(Probe.Value.X, Member->GetStance() == EOperativeStance::Standing ? 1.f : 0.f);
					Probe.Value.Y = FMath::Max(Probe.Value.Y, Member->IsSprinting() ? 1.f : 0.f);
					if (Leader->IsSprinting() && LeaderSpeed > 300.f)
					{
						Probe.Value.Z = FMath::Max(Probe.Value.Z, static_cast<double>(Member->GetVelocity().Size2D() / LeaderSpeed));
					}
				}
			}
			if (Leader->IsSprinting() && State.StageTime < 4.f)
			{
				return true;
			}
			int32 Running = 0;
			FString Who;
			for (const TPair<TWeakObjectPtr<AOperativeCharacter>, FVector>& Probe : State.SprintProbe)
			{
				const bool bRan = Probe.Value.X > 0.5 && Probe.Value.Y > 0.5 && Probe.Value.Z >= 0.9;
				Running += bRan ? 1 : 0;
				Who += FString::Printf(TEXT(" %s: up %d sprint %d best %.0f %%;"), Probe.Key.IsValid() ? *Probe.Key->DisplayName.ToString() : TEXT("-"),
					Probe.Value.X > 0.5 ? 1 : 0, Probe.Value.Y > 0.5 ? 1 : 0, Probe.Value.Z * 100.0);
			}
			Check(State, State.SprintProbe.Num() > 0 && Running == State.SprintProbe.Num(),
				FString::Printf(TEXT("bug 2026-10-20: after the crouch confinement released the leader runs - the followers stand up and run with him (%d / %d:%s %.2f s)"),
					Running, State.SprintProbe.Num(), *Who, State.StageTime));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
				if (const FTransform* Home = State.SprintHome.Find(Member))
				{
					Member->TeleportTo(Home->GetLocation(), Home->Rotator(), false, true);
				}
				if (const float* Cold = State.SprintCold.Find(Member))
				{
					Member->ColdLevel = *Cold; // the later lessons run as cold as before
				}
			}
			Tutorial->SkipStep(TEXT("A3_stance"));
			Tutorial->SkipStep(TEXT("A3_vault"));
			State.Stage = 110;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 5i item 4 (game designer 2026-10-15): the low fence - stop, Solo, then each operative prone under the gap on his own order
		// (2026-10-20: no "one at a time" queue - several at once, any order); then each into his own cover; Regroup refused meanwhile. A real 8 x 8 m VOL_A3_FENCE round the squad, "past the fence"
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
				&& Tutorial->GetStepState(TEXT("A3_fence_crawl")) == ETutorialStepState::Active, TEXT("rev 5i: B - Solo on, then the 'under the fence' card"));
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
				TEXT("rev 5i: prone, the first one may crawl past the fence"));
			// 2026-10-20 (game designer): no queue - the second one prone goes at the same time, on his own order.
			Check(State, Second->OrderMoveTo(State.HutPoint + FVector(0.f, 150.f, 0.f), false) == EOperativeOrderResult::Accepted
				&& CountMessages(World, TEXT("wait until he is through")) == 0,
				TEXT("2026-10-20: a second one prone may crawl at the same time (no 'one at a time' queue)"));
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, Second); // the second one is through first: any order
			Check(State, Tutorial->GetStepProgress(TEXT("A3_fence_crawl")) == 1 && Tutorial->GetCardView().Title.ToString().Contains(TEXT("1 / 3")),
				FString::Printf(TEXT("the first through (the second ordered): '1 / 3' ('%s')"), *Tutorial->GetCardView().Title.ToString()));
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, First);
			Check(State, Third->OrderMoveTo(State.HutPoint, false) == EOperativeOrderResult::Refused, TEXT("rev 5i: the third standing: refused"));
			Third->DropProne();
			GameplayEvents::Raise(World, GameplayEvents::CrawlGapPassed, Third);
			Check(State, Tutorial->GetStepState(TEXT("A3_fence_crawl")) == ETutorialStepState::Completed && !Tutorial->IsConfining(),
				TEXT("rev 5i: three through: the crossing done, the squad free"));
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			State.NotYetBefore = CountMessages(World, TEXT("Not yet - each operative crosses"));
			PressKey(PC, EKeys::B, IE_Pressed);
			Next(State);
			return true;
		}
		case 114:
		{
			PressKey(PC, EKeys::B, IE_Released);
			Check(State, Squad->IsSoloMode() && Tutorial->GetLockedRefusalCount() == State.RefusalsBefore + 1
				&& CountMessages(World, TEXT("Not yet - each operative crosses")) == State.NotYetBefore + 1,
				TEXT("rev 5i: B (regroup) refused after the crossing - 'Not yet - each operative crosses ...', still in Solo"));
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
			// 2026-10-19 (game designer): role-specific - a ring per role (the lake map's section 5j; here spawned 8 m apart): an operative
			// counts only at HIS ring, any order; his ring hides once he counted.
			TMap<AOperativeCharacter*, ATutorialMarkerActor*> Rings;
			AOperativeCharacter* M0 = Squad->GetMembers()[0];
			AOperativeCharacter* M1 = Squad->GetMembers()[1];
			AOperativeCharacter* M2 = Squad->GetMembers()[2];
			const FVector RingBase = M0->GetActorLocation() - FVector(0.f, 0.f, M0->GetSimpleCollisionHalfHeight());
			// Everyone goes back where he stood afterwards (the rings lie 15 m off: no proximity spot of the smoke's mines from there).
			TMap<AOperativeCharacter*, FVector> StoodAt;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				StoodAt.Add(Member, Member->GetActorLocation());
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Member = Squad->GetMembers()[Index];
				const FTransform Where(RingBase + FVector(800.f * Index, 1500.f, 0.f));
				ATutorialMarkerActor* Ring = World->SpawnActorDeferred<ATutorialMarkerActor>(ATutorialMarkerActor::StaticClass(), Where, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Ring->StepId = TEXT("A3_cover");
				Ring->RoleId = UTutorialSubsystem::GetRoleId(*Member);
				Ring->Label = FText::FromName(Ring->RoleId);
				Ring->RingRadiusCm = 110.f;
				Ring->FinishSpawning(Where);
				Rings.Add(Member, Ring);
			}
			auto PutAt = [](AOperativeCharacter* Member, const ATutorialMarkerActor* Ring)
			{
				Member->StopOperative();
				Member->TeleportTo(Ring->GetActorLocation() + FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight() + 5.f), Member->GetActorRotation(), false, true);
			};
			Check(State, Rings[M0]->IsMarkerShown() && Rings[M1]->IsMarkerShown() && Rings[M2]->IsMarkerShown(), TEXT("2026-10-19: the three role rings are shown"));
			PutAt(M0, Rings[M1]);
			RaiseCoverEntered(World, M0);
			Check(State, Tutorial->GetStepProgress(TEXT("A3_cover")) == 0, TEXT("2026-10-19: cover at another role's ring does not count"));
			PutAt(M0, Rings[M0]);
			RaiseCoverEntered(World, M0);
			RaiseCoverEntered(World, M0);
			Check(State, Tutorial->GetStepProgress(TEXT("A3_cover")) == 1, TEXT("rev 5i: one operative counts once (at his own ring)"));
			Check(State, !Rings[M0]->IsMarkerShown() && Rings[M1]->IsMarkerShown(), TEXT("2026-10-19: his ring hides, the others stay"));
			PutAt(M2, Rings[M2]);
			RaiseCoverEntered(World, M2);
			PutAt(M1, Rings[M1]);
			RaiseCoverEntered(World, M1);
			Check(State, Tutorial->GetStepState(TEXT("A3_cover")) == ETutorialStepState::Completed,
				TEXT("rev 5i: three different operatives in cover, each at his own ring (any order): done"));
			for (TPair<AOperativeCharacter*, ATutorialMarkerActor*>& Pair : Rings)
			{
				Pair.Value->Destroy();
				Pair.Key->TeleportTo(StoodAt[Pair.Key], Pair.Key->GetActorRotation(), false, true);
			}
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
			State.Stage = 82;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 2026-10-19 (game designer): the medkit lesson the first time an operative is badly hurt - a mine blast beside the engineer.
		case 82:
		{
			AOperativeCharacter* Engineer = nullptr;
			bool bEveryoneHasMedkit = true;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->Heal(10000.f);
				bEveryoneHasMedkit &= Member->MedkitsCount >= 1;
				if (Member->SquadRole == EOperativeRole::Engineer)
				{
					Engineer = Member;
				}
			}
			Check(State, bEveryoneHasMedkit, TEXT("medkit lesson: every operative carries a medkit at that moment"));
			State.LeaderBeforeMedkit = Leader;
			Check(State, Engineer && Tutorial->GetStepState(TEXT("X_medkit")) == ETutorialStepState::Idle, TEXT("medkit lesson: no card while everyone is healthy"));
			if (!Engineer)
			{
				return Finish(State, false);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Feet = Engineer->GetActorLocation() - FVector(0.f, 0.f, Engineer->GetSimpleCollisionHalfHeight());
			if (AProximityMineActor* Mine = World->SpawnActor<AProximityMineActor>(Feet + Engineer->GetActorRightVector() * 200.f, FRotator::ZeroRotator, Params))
			{
				Mine->Detonate();
			}
			// Only HE is the wounded one (a mate standing near him caught the blast too in some runs - the squad's spots vary).
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Engineer)
				{
					Member->HealthComponent->Heal(10000.f);
				}
			}
			const float Share = Engineer->HealthComponent->GetCurrentHealth() / FMath::Max(1.f, Engineer->HealthComponent->GetMaxHealth());
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke medkit lesson: the blast left the engineer at %.0f%%"), Share * 100.f);
			if (Share >= 0.7f)
			{
				Engineer->HealthComponent->ApplyDirectHealthLoss(Engineer->HealthComponent->GetCurrentHealth() - 0.5f * Engineer->HealthComponent->GetMaxHealth(),
					TEXT("Smoke"));
			}
			State.Focus = Engineer;
			State.Stage = 83;
			State.StageTime = 0.f;
			return true;
		}
		case 83:
		{
			// Other lesson cards of the beat may hold the card slot: skipped here so the medkit card shows (it queues behind them in game).
			const FName Card = Tutorial->GetMachine().GetActiveCard();
			if (!Card.IsNone() && Card != FName(TEXT("X_medkit")))
			{
				Tutorial->SkipStep(Card);
			}
			if (Tutorial->GetStepState(TEXT("X_medkit")) != ETutorialStepState::Active && State.StageTime < 12.f)
			{
				return true;
			}
			AOperativeCharacter* Engineer = State.Focus.Get();
			Check(State, Tutorial->GetStepState(TEXT("X_medkit")) == ETutorialStepState::Active && Engineer && Tutorial->GetFocusOperative(TEXT("X_medkit")) == Engineer,
				FString::Printf(TEXT("medkit lesson: the card shows, focused on the wounded engineer (%.1f s)"), State.StageTime));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::UseItem) && !Tutorial->IsCommandLocked(PlayerCommands::InventoryOpen),
				TEXT("medkit lesson: Item.Use / Inventory.Open are not locked"));
			// A medkit on someone else does not complete it.
			AOperativeCharacter* Other = nullptr;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Other = Member != Engineer ? Member : Other;
			}
			if (Other)
			{
				Other->HealthComponent->ApplyDirectHealthLoss(20.f, TEXT("Smoke"));
				Squad->SetLeader(Other);
				Tap(PC, State, EKeys::H);
			}
			State.Stage = 84;
			State.StageTime = 0.f;
			return true;
		}
		case 84:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("X_medkit")) == ETutorialStepState::Active, TEXT("medkit lesson: a medkit on a mate does not complete it"));
			AOperativeCharacter* Engineer = State.Focus.Get();
			State.HeatPacksBeforeLoot = Engineer ? Engineer->MedkitsCount : 0; // reused: his medkits before H
			State.CheckpointHealth = Engineer ? Engineer->HealthComponent->GetCurrentHealth() : 0.f;
			if (Engineer)
			{
				Squad->SetLeader(Engineer); // the card's input: select him, press H
				Tap(PC, State, EKeys::H);
			}
			State.Stage = 85;
			State.StageTime = 0.f;
			return true;
		}
		case 85:
		{
			if (Tutorial->GetStepState(TEXT("X_medkit")) == ETutorialStepState::Active && State.StageTime < 2.f)
			{
				return true;
			}
			const AOperativeCharacter* Engineer = State.Focus.Get();
			Check(State, Engineer && Engineer->MedkitsCount == State.HeatPacksBeforeLoot - 1 && Engineer->HealthComponent->GetCurrentHealth() > State.CheckpointHealth,
				TEXT("medkit lesson: H used his medkit (healed, one medkit fewer)"));
			Check(State, Tutorial->GetStepState(TEXT("X_medkit")) == ETutorialStepState::Completed, TEXT("medkit lesson: the medkit on HIM completes the card"));
			State.Stage = 86;
			State.StageTime = 0.f;
			return true;
		}
		case 86:
		{
			// The blast may have floored someone: up again, the earlier leader back, then the A4 part.
			bool bAnyDown = false;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bAnyDown |= Member->IsKnockedDown();
			}
			if (bAnyDown && State.StageTime < 10.f)
			{
				return true;
			}
			if (AOperativeCharacter* Before = State.LeaderBeforeMedkit.Get())
			{
				Squad->SetLeader(Before);
			}
			State.Stage = 120;
			State.StageTime = 0.f;
			return true;
		}
		// 2026-10-19 (game designer): the POSTURE lesson right after the trap disarm lesson (A3_wire finished; the wire card of the A2
		// spot above was skipped long before the cover field, so the A3 pair is started here by hand - the order of the real file is
		// CodexTactics.Tutorial.AmbushLesson.*). The squad entered A3 Defensive (the approach lines' set_posture action).
		case 120:
		{
			Check(State, Squad->GetSquadPosture() == ESquadFirePosture::Defensive, FString::Printf(TEXT("the squad entered the complex DEFENSIVE (set_posture action; %s)"),
				*FirePostureRules::GetLabel(Squad->GetSquadPosture())));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::PosturePassive), TEXT("the posture keys are locked before their lesson"));
			State.RefusalsBefore = Tutorial->GetLockedRefusalCount();
			Tap(PC, State, EKeys::Comma);
			Next(State);
			return true;
		}
		case 121:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Tutorial->GetLockedRefusalCount() == State.RefusalsBefore + 1 && Squad->GetEffectivePosture(Leader) == ESquadFirePosture::Defensive,
				TEXT("',' before the lesson: refused, the posture unchanged"));
			Check(State, Tutorial->TriggerStep(TEXT("A3_posture_radio")) && Tutorial->TriggerStep(TEXT("A3_posture")), TEXT("the posture lesson starts (radio + card)"));
			Next(State);
			return true;
		}
		case 122:
		{
			if (Tutorial->GetStepState(TEXT("A3_posture")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_posture")) == ETutorialStepState::Active, FString::Printf(TEXT("the POSTURE card is up (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A3_posture")))));
			Check(State, TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A3_posture_radio"))) || Tutorial->GetStepState(TEXT("A3_posture_radio")) == ETutorialStepState::Completed,
				TEXT("the Commander's 'nobody shoots first' radio plays with it"));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::PosturePassive) && !Tutorial->IsCommandLocked(PlayerCommands::PostureAggressive),
				TEXT("the card unlocks , . /"));
			Tap(PC, State, EKeys::Period); // 2026-10-22: the real key of the lesson - the selected operative (the leader) goes DEFENSIVE
			Next(State);
			return true;
		}
		case 123:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A3_posture")) == ETutorialStepState::Completed && Squad->GetEffectivePosture(Leader) == ESquadFirePosture::Defensive,
				TEXT("'.' (Posture.Defensive): the leader is DEFENSIVE, the card completes"));
			Squad->SetSquadPosture(ESquadFirePosture::Defensive); // the rest of the lesson runs with the whole squad Defensive (Alt + .)
			State.Stage = 104;
			State.StageTime = 0.f;
			return true;
		}
		case 104:
		{
			// 2026-10-20: the overlook (the ambush holds the pair there) and a patrol route 12 m ahead (the ambusher's wire spot).
			SpawnVolumeAt(World, Leader->GetActorLocation(), TEXT("VOL_A4_OVERLOOK"), 600.f);
			{
				const FVector RouteSpot = Leader->GetActorLocation() + Leader->GetActorForwardVector().GetSafeNormal2D() * 1200.f;
				FActorSpawnParameters RouteParams;
				RouteParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				State.AmbushRoute = World->SpawnActor<APatrolRouteActor>(APatrolRouteActor::StaticClass(), FTransform(RouteSpot), RouteParams);
				// 2026-10-21: the demo file binds part 1 to the zombie on the route labelled PatrolRoute_A4_Hounds on the lake (a selector also
				// matches a tag) and part 2 to PatrolRoute_A4_Frostbitten - the smoke's routes carry those tags.
				if (APatrolRouteActor* Route = State.AmbushRoute.Get())
				{
					Route->Tags.Add(FName(TEXT("PatrolRoute_A4_Hounds")));
				}
				const FVector LureRouteSpot = Leader->GetActorLocation() + Leader->GetActorForwardVector().GetSafeNormal2D() * 800.f;
				State.LureRoute = World->SpawnActor<APatrolRouteActor>(APatrolRouteActor::StaticClass(), FTransform(LureRouteSpot), RouteParams);
				if (APatrolRouteActor* Route = State.LureRoute.Get())
				{
					Route->Tags.Add(FName(TEXT("PatrolRoute_A4_Frostbitten")));
				}
			}
			Check(State, Tutorial->TriggerStep(TEXT("A4_cones")) && Tutorial->GetCurrentBeat() == FName(TEXT("A4")), TEXT("A4 started with the cone card"));
			Check(State, Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Skipped || Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Waiting,
				TEXT("the posture lesson was done in A3: its overlook twin is cancelled (or about to be)"));
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
			State.Stage = 130;
			State.StageTime = 0.f;
			return true;
		}
		// 2026-10-19 b (game designer play-test): THE AMBUSH in two parts - part 1 the wire rigged (the card) and a patrol walking into it
		// (TripwireTriggered.Enemy, raised here: the wire itself is PMCTripwire / Deployable smokes' job), part 2 the other enemy LURED:
		// the lure card unlocks Stealth.*, a REAL whistle (key 5) brings a guard 8 m away (placed in the map, senses off) to the noise
		// (StealthLure.Heard completes it), then the reminder card asks for AGGRESSIVE (/).
		case 130:
		{
			if (Tutorial->GetStepState(TEXT("A4_tripwire")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Active && !Tutorial->IsCommandLocked(PlayerCommands::DeployTripwire),
				FString::Printf(TEXT("part 1: the tripwire card (unlocks the wire; %s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_tripwire")))));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::StealthWhistle) && Tutorial->IsCommandLocked(PlayerCommands::StealthThrow),
				TEXT("the lure is still locked"));
			// 2026-10-21: part 1's zombie walks the AmbushRoute (senses off, parked: only its events matter here).
			if (!State.PerimeterEnemy.IsValid())
			{
				const FVector RouteStart = State.AmbushRoute.IsValid() ? State.AmbushRoute->GetActorLocation() : Leader->GetActorLocation();
				AEnemyCharacter* Zombie = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, RouteStart + FVector(0.f, 0.f, 100.f),
					FRotator::ZeroRotator);
				if (Zombie)
				{
					Zombie->bOverridePerception = true;
					Zombie->PerceptionOverride.SightRangeCm = 0.f;
					Zombie->PerceptionOverride.ProximityCm = 0.f;
					Zombie->PerceptionOverride.HearWalkCm = Zombie->PerceptionOverride.HearRunCm = 0.f;
					Zombie->PerceptionOverride.HearCrouchWalkCm = Zombie->PerceptionOverride.HearCrawlCm = 0.f;
					Zombie->PerceptionOverride.HearGunshotCm = Zombie->PerceptionOverride.HearExplosionCm = 0.f;
					Zombie->PerceptionOverride.SmellRadiusCm = 0.f;
					Zombie->AssignedPatrolRoute = State.AmbushRoute.Get();
					Zombie->CustomTimeDilation = 0.f;
					Zombie->SetActorTickEnabled(false);
				}
				State.PerimeterEnemy = Zombie;
			}
			// 2026-10-20 (game designer): the ambush is done by ONE operative - the Medic-Sapper; Solo with her selected, the others held.
			{
				AOperativeCharacter* Ambusher = Tutorial->GetFocusOperative(TEXT("A4_tripwire"));
				AOperativeCharacter* Mate = nullptr;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Mate = Mate ? Mate : (Member != Ambusher ? Member : nullptr);
				}
				Check(State, Ambusher && Ambusher->SquadRole == EOperativeRole::MedicSapper && Squad->IsSoloMode() && Squad->GetLeader() == Ambusher
					&& Tutorial->IsConfining() && Tutorial->GetCardView().Title.ToString().Contains(Ambusher->DisplayName.ToString().ToUpper()),
					FString::Printf(TEXT("ambush: the Medic-Sapper alone (%s), Solo on with her selected, the squad held, the card names her ('%s')"),
						Ambusher ? *Ambusher->DisplayName.ToString() : TEXT("-"), *Tutorial->GetCardView().Title.ToString()));
				Tutorial->RefreshGuideNow();
				const FTutorialGuideView& Guide = Tutorial->GetGuideView();
				const APatrolRouteActor* Route = State.AmbushRoute.Get();
				// 2026-10-21: the spot is on the PERIMETER zombie's route only ("@patrol_route.A4_PERIMETER"), nearest to her.
				FVector Nearest = FVector::ZeroVector;
				if (Route && Ambusher)
				{
					TArray<FVector> Points;
					for (int32 Index = 0; Index < Route->GetNumberOfWaypoints(); ++Index)
					{
						Points.Add(Route->GetWaypointWorldLocation(Index));
					}
					Nearest = TutorialRules::NearestPointOnRoute(Points, Route->bIsLoop, Ambusher->GetActorLocation());
				}
				Check(State, Guide.bValid && Guide.Guide.Kind == ETutorialGuideKind::Spot && Guide.Guide.Id == FName(TEXT("@patrol_route.A4_PERIMETER")) && Route
					&& FVector::Dist2D(Guide.Location, Nearest) < 50.f,
					FString::Printf(TEXT("ambush: the current target is the wire spot on the perimeter zombie's route nearest to her (%s, %.0f cm from it)"),
						*UTutorialSubsystem::DescribeGuide(Guide.Guide), FVector::Dist2D(Guide.Location, Nearest)));
				const FVector Far = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorForwardVector().GetSafeNormal2D() * 1000.f);
				const int32 LinesBefore = CountMessages(World, TEXT("handles it"));
				Check(State, Mate && Mate->OrderMoveTo(Far, false) == EOperativeOrderResult::Refused && CountMessages(World, TEXT("handles it")) == LinesBefore + 1,
					FString::Printf(TEXT("ambush: %s's order off the overlook is refused ('Hold here - <Name> handles it.')"), Mate ? *Mate->DisplayName.ToString() : TEXT("-")));
				Check(State, Ambusher && UTutorialSubsystem::CheckConfinedOrder(World, Ambusher, Far) && Ambusher->OrderMoveTo(Far, false) != EOperativeOrderResult::Refused,
					TEXT("ambush: the Medic-Sapper may go anywhere"));
				if (Ambusher)
				{
					Ambusher->StopOperative();
				}
				State.AmbushMate = Mate;
			}
			Tutorial->HandlePlayerCommand(PlayerCommands::DeployTripwire, FPlayerCommandContext()); // rigged (the anchors: Deployable smokes)
			// The other patrol member: a guard 8 m ahead, placed in the map (on its post), deaf and blind (only the lure moves it).
			const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			const FVector Post = SmokeUtils::ClearPoint(World, Feet, Feet + Ahead * 800.f);
			AEnemyCharacter* Guard = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten, Post + FVector(0.f, 0.f, 100.f), Ahead.Rotation());
			if (Guard)
			{
				Guard->bOverridePerception = true;
				Guard->PerceptionOverride.SightRangeCm = 0.f;
				Guard->PerceptionOverride.ProximityCm = 0.f;
				Guard->PerceptionOverride.HearWalkCm = Guard->PerceptionOverride.HearRunCm = 0.f;
				Guard->PerceptionOverride.HearCrouchWalkCm = Guard->PerceptionOverride.HearCrawlCm = 0.f;
				Guard->PerceptionOverride.HearGunshotCm = Guard->PerceptionOverride.HearExplosionCm = 0.f;
				Guard->PerceptionOverride.SmellRadiusCm = 0.f;
				Guard->GetHealthComponent()->SetMaxHealth(100000.f);
				World->GetSubsystem<ULevelEncounterSubsystem>()->MarkLevelEnemy(Guard);
				// 2026-10-21: part 2's zombie (A4_LURE): its route is the tagged LureRoute, moved onto its post (a two-point route 1 m long: it
				// stays there, on patrol).
				if (APatrolRouteActor* Route = State.LureRoute.Get(); Route && Route->GetRouteSpline())
				{
					Route->SetActorLocation(Post);
					Route->GetRouteSpline()->ClearSplinePoints(false);
					Route->GetRouteSpline()->AddSplinePoint(Post, ESplineCoordinateSpace::World, false);
					Route->GetRouteSpline()->AddSplinePoint(Post + Ahead * 50.f, ESplineCoordinateSpace::World, true);
					Guard->AssignedPatrolRoute = Route;
				}
			}
			State.LureEnemy = Guard;
			Next(State);
			return true;
		}
		case 131:
		{
			if (Tutorial->GetStepState(TEXT("A4_trap_wait")) != ETutorialStepState::Active && State.StageTime < 3.f)
			{
				return true;
			}
			AEnemyCharacter* Guard = State.LureEnemy.Get();
			Check(State, Tutorial->GetStepState(TEXT("A4_tripwire")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active,
				TEXT("rigged: the card completes, the objective 'wait until the patrol walks into the wire' shows"));
			Check(State, Guard && Guard->IsOnPatrol() && !Guard->IsHuntingSquad(), TEXT("the second enemy stands on its route, unaware"));
			// 2026-10-21: each part reacts only to ITS zombie - the far one (part 2's) pulling a pin does not complete part 1.
			Check(State, Guard && Tutorial->IsEnemyBound(*Guard, TEXT("A4_LURE")) && State.PerimeterEnemy.IsValid()
				&& Tutorial->IsEnemyBound(*State.PerimeterEnemy, TEXT("A4_PERIMETER")) && !Tutorial->IsEnemyBound(*Guard, TEXT("A4_PERIMETER")),
				TEXT("bindings: the guard is A4_LURE (its route's tag), the perimeter zombie A4_PERIMETER"));
			if (Guard)
			{
				Tutorial->NotifyEvent(TEXT("TripwireTriggered.Enemy"), Guard->GetFName());
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_wt_trap")) == ETutorialStepState::Idle,
				TEXT("part 1: the FAR zombie tripping a wire does not complete it (no 'that's one' line)"));
			if (AEnemyCharacter* Zombie = State.PerimeterEnemy.Get())
			{
				Tutorial->NotifyEvent(TEXT("TripwireTriggered.Enemy"), Zombie->GetFName()); // the perimeter zombie walked into it
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_trap_wait")) == ETutorialStepState::Completed, TEXT("part 1: the perimeter zombie pulled the pin -> the objective is done"));
			Next(State);
			return true;
		}
		case 132:
		{
			// 2026-10-21: part 2 starts with the approach along the near container (cover, then round its corner in cover) - the moves
			// themselves are the cover smokes' job: her cover event / the wall-follow command here.
			if (Tutorial->GetStepState(TEXT("A4_hug_wall")) == ETutorialStepState::Active)
			{
				const AOperativeCharacter* Ambusher = Tutorial->GetFocusOperative(TEXT("A4_hug_wall"));
				State.bSawWallCards = true;
				Tutorial->NotifyEvent(GameplayEvents::CoverEntered, Ambusher ? Ambusher->GetFName() : NAME_None);
			}
			if (Tutorial->GetStepState(TEXT("A4_round_corner")) == ETutorialStepState::Active)
			{
				Tutorial->HandlePlayerCommand(PlayerCommands::CoverShimmy, FPlayerCommandContext());
			}
			if (Tutorial->GetStepState(TEXT("A4_lure")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, State.bSawWallCards && Tutorial->GetStepState(TEXT("A4_round_corner")) == ETutorialStepState::Completed,
				TEXT("part 2: the wall card (her cover) and the round-the-corner card (wall-follow) come before the lure"));
			Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active, FString::Printf(TEXT("part 2: the LURE card (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_lure")))));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::StealthWhistle) && !Tutorial->IsCommandLocked(PlayerCommands::StealthThrow),
				TEXT("the lure card unlocks whistle / throw"));
			Check(State, TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A4_wt_trap"))) || TutorialRules::IsFinished(Tutorial->GetStepState(TEXT("A4_wt_trap"))),
				TEXT("the 'that's one' line came with the trap kill"));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->StopOperative();
			}
			Tap(PC, State, EKeys::Five); // WHISTLE (the real key) - the guard 8 m away hears it
			Next(State);
			return true;
		}
		case 133:
		{
			const AEnemyCharacter* Guard = State.LureEnemy.Get();
			if (Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Active && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Guard && Guard->IsInvestigatingLure(), TEXT("the whistle: the guard leaves its post to investigate"));
			Check(State, Tutorial->GetStepState(TEXT("A4_lure")) == ETutorialStepState::Completed, FString::Printf(TEXT("StealthLure.Heard completes the lure card (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_lure")))));
			// 2026-10-20: parts 1 + 2 done by her alone - the hold is released, the pair may move again (fire support).
			if (AOperativeCharacter* Mate = State.AmbushMate.Get())
			{
				const FVector Far = SmokeUtils::ClearPoint(World, Mate->GetActorLocation(), Mate->GetActorLocation() + Mate->GetActorForwardVector().GetSafeNormal2D() * 1000.f);
				Check(State, !Tutorial->IsConfining() && UTutorialSubsystem::CheckConfinedOrder(World, Mate, Far),
					TEXT("ambush: the lure done - the hold is released, the others may move"));
			}
			Next(State);
			return true;
		}
		case 134:
		{
			if (Tutorial->GetStepState(TEXT("A4_close_shot")) != ETutorialStepState::Active && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Active, FString::Printf(TEXT("he is coming: the CLOSE SHOT card (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot")))));
			Tutorial->RefreshGuideNow();
			Check(State, Tutorial->GetGuideView().bValid && Tutorial->GetGuideView().Guide.Id == FName(TEXT("@enemy.A4_LURE")) && State.LureEnemy.IsValid()
				&& FVector::Dist2D(Tutorial->GetGuideView().Location, State.LureEnemy->GetActorLocation()) < 50.f,
				FString::Printf(TEXT("the current target follows him (%s)"), *UTutorialSubsystem::DescribeGuide(Tutorial->GetGuideView().Guide)));
			// 2026-10-21: the PERIMETER zombie "shot" right beside an operative, far off its route: not part 2's zombie (the kill event only:
			// a real shot would start the fight - the lake smoke TutorialAmbushLakeSmoke shoots for real).
			if (AEnemyCharacter* Zombie = State.PerimeterEnemy.Get())
			{
				const FVector Beside = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() - Leader->GetActorForwardVector().GetSafeNormal2D() * 400.f);
				Zombie->SetActorLocation(FVector(Beside.X, Beside.Y, Zombie->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
				Tutorial->NotifyEvent(TEXT("EnemyKilled.Squad"), Zombie->GetFName());
				Tutorial->NotifyEvent(TEXT("EnemyKilled"), Zombie->GetFName());
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Active && Tutorial->GetStepState(TEXT("A4_close_shot_far")) == ETutorialStepState::Idle,
				TEXT("part 2: the PERIMETER zombie killed up close does not complete the close shot (nor the 'not up close' line)"));
			Next(State);
			return true;
		}
		case 135:
		{
			// The far zombie walks to the whistle: killed once he is off his route (>= 4 m) and within 10 m of an operative.
			AEnemyCharacter* Guard = State.LureEnemy.Get();
			const APatrolRouteActor* Route = State.LureRoute.Get();
			float OffRoute = 0.f;
			float Nearest = TNumericLimits<float>::Max();
			if (Guard && Route)
			{
				TArray<FVector> Points;
				for (int32 Index = 0; Index < Route->GetNumberOfWaypoints(); ++Index)
				{
					Points.Add(Route->GetWaypointWorldLocation(Index));
				}
				OffRoute = TutorialRules::DistanceToRoute2D(Points, Route->bIsLoop, Guard->GetActorLocation());
				for (const AOperativeCharacter* Member : Squad->GetMembers())
				{
					Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist(Member->GetActorLocation(), Guard->GetActorLocation())));
				}
			}
			if ((OffRoute < 450.f || Nearest > 950.f) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Guard && OffRoute >= 400.f && Nearest <= 1000.f, FString::Printf(TEXT("the lured zombie came off his route (%.1f m) to %.1f m from the squad"),
				OffRoute / 100.f, Nearest / 100.f));
			if (Guard)
			{
				Tutorial->NotifyEvent(TEXT("EnemyKilled.Squad"), Guard->GetFName()); // the close shot (the kill event; the fight is the lake smoke's)
				Tutorial->NotifyEvent(TEXT("EnemyKilled"), Guard->GetFName());
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot")) == ETutorialStepState::Completed, FString::Printf(TEXT("part 2: the close shot in the open completes it (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot")))));
			// The fight itself would unlock everything: both zombies are taken away.
			if (Guard)
			{
				Guard->Destroy();
			}
			if (AEnemyCharacter* Zombie = State.PerimeterEnemy.Get())
			{
				Zombie->Destroy();
			}
			Next(State);
			return true;
		}
		case 136:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_close_shot_far")) == ETutorialStepState::Skipped,
				FString::Printf(TEXT("the 'not up close' line never shows after a close shot (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_close_shot_far")))));
			Check(State, World->GetSubsystem<UGameFlowSubsystem>()->GetPhase() == ECodexGamePhase::Exploration, TEXT("no fight started in the smoke (Phase 2)"));
			// The squad holds fire again: an Aggressive squad would open fire on the next enemy it sees (the level is an ambush level
			// since the guard was placed - "auto" + placed enemies) and the fight would unlock everything.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			// 2026-10-20: the ambush left the Medic-Sapper selected (Solo): the sight check below is laid out round the first operative again.
			Squad->SetLeaderByIndex(0);
			Leader = Squad->GetLeader();
			// 2026-10-19 b, symmetric sight: a 1.2 m wall 1 m ahead of the leader, a frozen enemy (senses off) 6 m beyond it.
			const FVector Ahead = Leader->GetActorForwardVector().GetSafeNormal2D();
			{
				// The mates stand where the ambush left them (Solo): put them behind the leader, out of the enemy's line.
				int32 Behind = 0;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					if (Member != Leader)
					{
						Member->StopOperative();
						const FVector Spot = SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() - Ahead * (150.f * ++Behind));
						Member->SetActorLocation(FVector(Spot.X, Spot.Y, Leader->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
					}
				}
			}
			const float GroundZ = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight();
			const FVector WallSpot = Leader->GetActorLocation() + Ahead * 100.f;
			if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
			{
				const FTransform WallTransform(Ahead.Rotation(), FVector(WallSpot.X, WallSpot.Y, GroundZ + 60.f), FVector(0.3f, 5.f, 1.2f));
				if (AStaticMeshActor* Wall = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WallTransform, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
				{
					Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
					Wall->GetStaticMeshComponent()->SetStaticMesh(Cube);
					Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
					Wall->FinishSpawning(WallTransform);
					State.SightWall = Wall;
				}
			}
			const FVector EnemySpot = Leader->GetActorLocation() + Ahead * 700.f;
			AEnemyCharacter* Hidden = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Frostbitten,
				FVector(EnemySpot.X, EnemySpot.Y, GroundZ + 100.f), (-Ahead).Rotation());
			if (Hidden)
			{
				Hidden->bOverridePerception = true;
				Hidden->PerceptionOverride.SightRangeCm = 0.f;
				Hidden->PerceptionOverride.ProximityCm = 0.f;
				Hidden->PerceptionOverride.HearWalkCm = Hidden->PerceptionOverride.HearRunCm = 0.f;
				Hidden->PerceptionOverride.HearCrouchWalkCm = Hidden->PerceptionOverride.HearCrawlCm = 0.f;
				Hidden->PerceptionOverride.SmellRadiusCm = 0.f;
				Hidden->CustomTimeDilation = 0.f;
				Hidden->SetActorTickEnabled(false);
			}
			State.SightEnemy = Hidden;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Leader)
				{
					Member->SetStance(EOperativeStance::Prone); // the mates (behind the leader) lie low too
				}
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_hidden")) == ETutorialStepState::Idle, TEXT("no 'out of sight' card while nobody hides from an enemy"));
			Tap(PC, State, EKeys::V); // the leader goes prone behind the wall
			State.Stage = 137;
			State.StageTime = 0.f;
			return true;
		}
		case 137:
		{
			if (Tutorial->GetStepState(TEXT("A4_hidden")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			const AEnemyCharacter* Hidden = State.SightEnemy.Get();
			Check(State, Leader->GetStance() == EOperativeStance::Prone && Hidden && Sight && !Sight->IsVisibleToSquad(Hidden) && Hidden->IsHidden(),
				TEXT("prone behind the 1.2 m wall: the enemy beyond it is hidden from the squad (symmetric sight)"));
			Check(State, Tutorial->GetConditionStat(TEXT("enemies_unseen")) >= 1.f && Tutorial->GetConditionStat(TEXT("squad_prone")) >= 1.f,
				TEXT("stats: an enemy unseen, someone prone"));
			Check(State, Tutorial->GetStepState(TEXT("A4_hidden")) == ETutorialStepState::Active, FString::Printf(TEXT("the 'OUT OF SIGHT' card (%s)"),
				TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_hidden")))));
			Tap(PC, State, EKeys::Z); // rise to look
			Next(State);
			return true;
		}
		case 138:
		{
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
			const AEnemyCharacter* Hidden = State.SightEnemy.Get();
			Check(State, Tutorial->GetStepState(TEXT("A4_hidden")) == ETutorialStepState::Completed, TEXT("Z (stand up) completes the card"));
			Check(State, Hidden && Sight && Sight->IsVisibleToSquad(Hidden) && !Hidden->IsHidden(), TEXT("standing: the enemy is seen over the wall"));
			if (AActor* Wall = State.SightWall.Get())
			{
				Wall->Destroy();
			}
			if (AEnemyCharacter* Gone = State.SightEnemy.Get())
			{
				Gone->Destroy();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->SetStance(EOperativeStance::Standing);
			}
			Next(State);
			return true;
		}
		case 139:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			// After the stance lesson (A3; A4 repeats it as a safety) C is accepted.
			Leader->SetStance(EOperativeStance::Standing);
			Tap(PC, State, EKeys::C);
			State.Stage = 65;
			State.StageTime = 0.f;
			return true;
		}
		case 65:
		{
			if (State.StageTime < 0.2f || (!TutorialRules::IsFinished(Tutorial->GetStepState(TEXT("A4_posture"))) && State.StageTime < 5.f))
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A4_posture")) == ETutorialStepState::Skipped, FString::Printf(
				TEXT("the overlook posture twin never shows - A3 taught it (%s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A4_posture")))));
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
				Check(State, Tutorial->IsConfining() && Tutorial->JudgeMove(*Leader, Far) == ETutorialConfineVerdict::Outside,
					FString::Printf(TEXT("rev 5i: an inline confinement holds the squad (%s)"), *HoldError));
				Tutorial->SetTutorialEnabled(false);
				Tutorial->SetTutorialEnabled(true);
				Check(State, !Tutorial->IsConfining() && Tutorial->JudgeMove(*Leader, Far) == ETutorialConfineVerdict::Allowed,
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
			State.Stage = 140;
			State.StageTime = 0.f;
			return true;
		}
		// 2026-10-20 (game designer play-test): the current navigation target; the bridge skipped never calls the squad back.
		case 140:
		{
			for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
			{
				It->Destroy(); // the earlier stages' stand-ins: the course below has its own
			}
			for (TActorIterator<ATutorialMarkerActor> It(World); It; ++It)
			{
				It->Destroy();
			}
			if (Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			Squad->SetLeaderByIndex(0);
			Leader = Squad->GetLeader();
			State.CourseOrigin = Leader->GetActorLocation();
			State.CourseForward = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.CourseRight = FVector::CrossProduct(FVector::UpVector, State.CourseForward).GetSafeNormal();
			const FVector O = State.CourseOrigin;
			const FVector F = State.CourseForward;
			const FVector R = State.CourseRight;
			SpawnCourseVolume(World, O + F * 800.f, TEXT("VOL_A0_BEACON1"), TEXT("A1"), 300.f);
			SpawnCourseVolume(World, O + F * 2000.f, TEXT("VOL_A1_BEACON2"), NAME_None, 250.f);
			SpawnCourseVolume(World, O + F * 4000.f + R * 1500.f, TEXT("VOL_A1_HUT_APPROACH"), NAME_None, 400.f);
			SpawnCourseVolume(World, O + F * 6000.f, TEXT("VOL_A1_BEACON3"), NAME_None, 300.f);
			SpawnCourseVolume(World, O + F * 8000.f, TEXT("VOL_A2_CAMP"), TEXT("A2"), 300.f);
			// The bridge over the "crevasse" (local X along the course) and its marker at the near end, as on the lake.
			const FTransform BridgeTransform(F.Rotation(), O + F * 2600.f);
			ABridgeCrossingVolume* Bridge = World->SpawnActorDeferred<ABridgeCrossingVolume>(ABridgeCrossingVolume::StaticClass(), BridgeTransform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Bridge)
			{
				Bridge->Span->SetBoxExtent(FVector(300.f, 100.f, 125.f));
				Bridge->FinishSpawning(BridgeTransform);
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			ATutorialMarkerActor* Marker = World->SpawnActor<ATutorialMarkerActor>(ATutorialMarkerActor::StaticClass(), FTransform(O + F * 2250.f), Params);
			if (Marker)
			{
				Marker->StepId = TEXT("A1_bridge");
				Marker->FinishSpawning(FTransform(O + F * 2250.f));
				State.BridgeMarker = Marker;
			}
			State.bBridgeMarkerEverShown = false;
			Check(State, Bridge && Marker && Tutorial->LoadStepsFile(TEXT("demo_01_steps.json")) && Tutorial->GetCurrentBeat() == FName(TEXT("A0")),
				TEXT("guide course: the lake's volumes, a bridge, the bridge marker; the demo tutorial from A0"));
			Next(State);
			return true;
		}
		case 141:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("A0"), TEXT("VOL_A0_BEACON1"));
			TeleportSquadTo(Squad, State.CourseOrigin + State.CourseForward * 800.f, State.CourseRight);
			FireCourseVolume(World, TEXT("VOL_A0_BEACON1"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A1")), TEXT("guide course: beacon 1 -> A1"));
			Next(State);
			return true;
		}
		case 142:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("A1 start"), TEXT("VOL_A1_BEACON2"), /*bBridgeAhead*/ true);
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge")) == ETutorialStepState::Idle && Tutorial->GetConditionStat(TEXT("squad_not_across.VOL_A1_BEACON2")) == 3.f,
				TEXT("guide course: before the crossing - the bridge objective not started, nobody across"));
			// Across the crevasse 20 m beside the bridge: the bridge beacon never fires.
			TeleportSquadTo(Squad, State.CourseOrigin + State.CourseForward * 3300.f + State.CourseRight * 2000.f, State.CourseRight);
			Next(State);
			return true;
		}
		case 143:
		{
			if (!TutorialRules::IsFinished(Tutorial->GetStepState(TEXT("A1_bridge"))) && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Tutorial->GetConditionStat(TEXT("squad_not_across.VOL_A1_BEACON2")) == 0.f, TEXT("guide course: all three across (stat squad_not_across = 0)"));
			Check(State, Tutorial->GetStepState(TEXT("A1_bridge")) == ETutorialStepState::Skipped && Tutorial->GetStepState(TEXT("A1_wt_bridge")) == ETutorialStepState::Skipped,
				FString::Printf(TEXT("guide course: across without the bridge - the bridge objective and lines passed (%s, %s)"),
					TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_bridge"))), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_wt_bridge")))));
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("across"), TEXT("VOL_A1_HUT_APPROACH"));
			Tutorial->SkipStep(TEXT("A1_gust"));
			Tutorial->SkipStep(TEXT("A1_wt_banter"));
			TeleportSquadTo(Squad, State.CourseOrigin + State.CourseForward * 4000.f + State.CourseRight * 1500.f, State.CourseRight);
			FireCourseVolume(World, TEXT("VOL_A1_HUT_APPROACH"));
			Next(State);
			return true;
		}
		case 144:
		{
			if (Tutorial->GetStepState(TEXT("A1_solo")) != ETutorialStepState::Active && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_solo")) == ETutorialStepState::Active, TEXT("guide course: the hut approach - the Solo card"));
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("Solo card"), NAME_None);
			Tap(PC, State, EKeys::B);
			Next(State);
			return true;
		}
		case 145:
		{
			if (Tutorial->GetStepState(TEXT("A1_select")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A1_select")) == ETutorialStepState::Active, TEXT("guide course: B - Solo, the select card"));
			Tap(PC, State, EKeys::Two);
			SpawnVolumeAround(World, Squad->GetMembers()[1], TEXT("VOL_A1_DOOR_L"));
			Next(State);
			return true;
		}
		case 146:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_stack_l")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			AOperativeCharacter* Second = Squad->GetMembers()[1];
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("left of the door"), TEXT("VOL_A1_DOOR_L"));
			RaiseCoverEntered(World, Second);
			SpawnVolumeAround(World, Squad->GetMembers()[0], TEXT("VOL_A1_DOOR_R"));
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_l")) == ETutorialStepState::Completed, TEXT("guide course: left of the door done"));
			Next(State);
			return true;
		}
		case 147:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_stack_r")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			RaiseCoverEntered(World, Squad->GetMembers()[0]);
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_stack_r")) == ETutorialStepState::Completed, TEXT("guide course: right of the door done"));
			Next(State);
			return true;
		}
		case 148:
		{
			if (Tutorial->GetStepState(TEXT("A1_hut_enter")) != ETutorialStepState::Active && State.StageTime < 4.f)
			{
				return true;
			}
			if (ATutorialTriggerVolume* Hut = SpawnVolumeAround(World, Squad->GetMembers()[2], TEXT("VOL_A1_HUT"), 150.f))
			{
				Hut->CheckOverlapping();
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_hut_enter")) == ETutorialStepState::Completed && !Tutorial->IsConfining(),
				TEXT("guide course: the third operative inside - the squad free"));
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("inside the hut"), NAME_None);
			Next(State);
			return true;
		}
		case 149:
		{
			if (Tutorial->GetStepState(TEXT("A1_loot_open")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Tutorial->HandlePlayerCommand(TEXT("Loot.Open"), FPlayerCommandContext());
			Next(State);
			return true;
		}
		case 150:
		{
			if (Tutorial->GetStepState(TEXT("A1_loot_take")) != ETutorialStepState::Active && State.StageTime < 6.f)
			{
				return true;
			}
			Tutorial->NotifyEvent(TEXT("ItemLooted.HeatPack"), Squad->GetMembers()[0]->GetFName());
			Check(State, Tutorial->GetStepState(TEXT("A1_loot_take")) == ETutorialStepState::Completed, TEXT("guide course: the heat packs taken"));
			// The looter hands one to each mate (Shift + drag: into the kit).
			AOperativeCharacter* Looter = Squad->GetMembers()[0];
			Looter->HeatPacksCount = 3;
			USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Transfer && Member != Looter)
				{
					Member->HeatPacksCount = 0;
					FTransferRequest Give = FTransferRequest::MakeGive(Looter, Member, ETransferItem::HeatPack, 1);
					Give.bHandOverOnly = true;
					Transfer->Execute(Give);
				}
			}
			Next(State);
			return true;
		}
		case 151:
		{
			if (Tutorial->GetStepState(TEXT("A1_regroup")) != ETutorialStepState::Active && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Tutorial->GetStepState(TEXT("A1_share_card")) == ETutorialStepState::Completed && Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Active,
				TEXT("guide course: shared - the regroup card"));
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("regroup card"), NAME_None);
			Tap(PC, State, EKeys::B);
			Next(State);
			return true;
		}
		case 152:
		{
			if (Tutorial->GetStepState(TEXT("A1_box")) != ETutorialStepState::Active && !TutorialRules::IsFinished(Tutorial->GetStepState(TEXT("A1_box"))) && State.StageTime < 4.f)
			{
				return true;
			}
			Check(State, !Squad->IsSoloMode() && Tutorial->GetStepState(TEXT("A1_regroup")) == ETutorialStepState::Completed, TEXT("guide course: B - Solo off, regrouped"));
			Tutorial->SkipStep(TEXT("A1_box"));
			// Right after the hut lesson: the next place is shown at once - beacon 3, with the guide beacon standing there.
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("after the hut lesson"), TEXT("VOL_A1_BEACON3"));
			const ATutorialMarkerActor* Beacon = Tutorial->GetGuideBeacon();
			const FVector Beacon3 = State.CourseOrigin + State.CourseForward * 6000.f;
			Check(State, Beacon && Beacon->IsMarkerShown() && FVector::Dist2D(Beacon->GetActorLocation(), Beacon3) < 50.f,
				FString::Printf(TEXT("guide course: the guide beacon stands at beacon 3 (%.0f cm off)"), Beacon ? FVector::Dist2D(Beacon->GetActorLocation(), Beacon3) : -1.f));
			CheckGuideArrow(State, PC, Tutorial, Overlay, TEXT("after the hut lesson"));
			// The camera looks the other way: the target is off-screen, the arrow must point at it.
			if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
			{
				Camera->LookAtPoint(State.CourseOrigin - State.CourseForward * 9000.f + State.CourseRight * 3000.f);
			}
			Next(State);
			return true;
		}
		case 153:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			CheckGuideArrow(State, PC, Tutorial, Overlay, TEXT("camera turned away"));
			// 2026-10-20 pacing instrumentation (the scripted pacing table is CodexTactics.Tutorial.Flow.Pacing): the guide course run.
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke pacing (guide course): %.1f s without any instruction, longest wait after a card %.2f s"),
				Tutorial->GetNoInstructionSeconds(), Tutorial->GetMaxNextInstructionSeconds());
			Check(State, Tutorial->GetNoInstructionSeconds() <= 1.f, FString::Printf(TEXT("pacing: never without an instruction on screen since the course loaded (%.1f s)"),
				Tutorial->GetNoInstructionSeconds()));
			Check(State, State.GuideArrowChecks >= 1, FString::Printf(TEXT("guide course: the edge arrow was checked off-screen (%d)"), State.GuideArrowChecks));
			TeleportSquadTo(Squad, State.CourseOrigin + State.CourseForward * 6000.f, State.CourseRight);
			FireCourseVolume(World, TEXT("VOL_A1_BEACON3"));
			Tutorial->SkipSpeech();
			CheckOneGuide(State, World, Tutorial, Overlay, TEXT("beacon 3 reached"), TEXT("VOL_A2_CAMP"));
			if (ATacticalCameraPawn* Camera = PC->GetPawn<ATacticalCameraPawn>())
			{
				Camera->LookAtPoint(Squad->GetLeader()->GetActorLocation());
			}
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
