// Dev-only headless check of saving / loading WITH the Demo 01 tutorial on the designer's review map (rev 5f, user decision
// 2026-10-14 "no autosaves - only checkpoints + F5", and the save bug he hit):
//   Scripts/smoke_parallel.ps1 -Commands TutorialSaveSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake,
//   -LevelJson=L_Demo01_A_FrozenLake.json; both from Scripts/test_map.json)
// Saves go to Saved/SmokeSaves/TutorialSave (also in the reopened world: the pending load carries the directory).
//  1. The tutorial runs from the level (A0). The leader is put into the level's VOL_A0_BEACON1 -> A1 starts and writes the A1
//     CHECKPOINT (the only save the game writes on its own); sprint is locked; a command is in the precredit history.
//  2. The crack (VOL_A1_BEACON2): the "beacon two is past the crack" talk is dropped (cancel_on); two of three cross the bridge;
//     one heat pack is handed over (the share lesson's counter). F5.
//  3. F9 (QuickLoad): the newest of the quicksave / checkpoint = the quicksave; the map REOPENS and the save is applied. In the
//     fresh world: beat A1 (not A0), the dropped talk stays dropped, the bridge still at two of three, the history, sprint still
//     locked, the hand-over counted, the used-up volumes still used up - and the A1 CHECKPOINT FILE IS UNTOUCHED (before rev 5f the
//     reopened level started its tutorial at A0 and wrote that checkpoint over the slot being loaded).
//  4. "Retry from checkpoint" (rev 2026-10-19: it REOPENS the level, as F9): back at the A1 start - the bridge not started, its volume
//     NOT used up, no hand-over; the leader walks into the crack volume again -> the bridge objective runs.
//  5. Rev 2026-10-19 (user: "the save keeps the state from BEFORE the load"): the Commander is knocked down, Retry -> he is up, idle,
//     no knockdown, no montage, the mesh on its place on the capsule (<= 5 cm), stance as saved - twice in a row; Solo ON + F5, the
//     followers walk off, F9 -> Solo and the followers holding, a leader move leaves them in place; an Aggressive squad, a placed
//     enemy (level enemy) detects it -> the fight starts (WaveCombat, Attack.* unlocked) and the squad shoots it, nobody panics / flees.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadTransferSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/PanicComponent.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/PlayerCommands.h"
#include "Core/SaveGameRules.h"
#include "Core/SaveGameSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "Tutorial/TutorialTriggerVolume.h"

namespace TutorialSaveSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bApplied = false;
		FDelegateHandle AppliedHandle;
		int32 ZoomUsesAtSave = 0;
		/** Rev 2026-10-19: loads applied so far, the slot of the last one. */
		int32 AppliedCount = 0;
		FString LastAppliedSlot;
		int32 AppliedBefore = 0;
		TArray<FVector> FollowerSpots;
		TWeakObjectPtr<AEnemyCharacter> Enemy;
		float EnemyHealthAtContact = 0.f;
		int32 RetriesChecked = 0;
	};

	void Check(FState& State, bool bOk, const FString& What);

	/** Rev 2026-10-19: everything the user saw survive a load - knockdown, montages, the mesh off its capsule, a stance - is gone. */
	void CheckCleanAfterLoad(FState& State, USquadSubsystem* Squad, const TCHAR* When)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			// Everyone: no knockdown / montage / cover / body offset carried over. The leader (the one knocked down): also standing still as
			// saved; followers may walk to their formation spots and keep a saved crouch (Solo).
			const UAnimInstance* Anim = Member->GetMesh() ? Member->GetMesh()->GetAnimInstance() : nullptr;
			const bool bLeader = Member == Squad->GetLeader();
			const bool bClean = !Member->IsKnockedDown() && !(Anim && Anim->IsAnyMontagePlaying()) && Member->GetMeshBaseOffsetCm() <= 5.f
				&& !Member->bInCover && (!bLeader || (Member->GetStance() == EOperativeStance::Standing && Member->GetVelocity().Size2D() < 5.f));
			Check(State, bClean, FString::Printf(TEXT("%s: %s up and idle as saved (knocked %d, montage %d, mesh %.1f cm off, cover %d, stance %d, speed %.0f)"),
				When, *Member->DisplayName.ToString(), Member->IsKnockedDown() ? 1 : 0, Anim && Anim->IsAnyMontagePlaying() ? 1 : 0,
				Member->GetMeshBaseOffsetCm(), Member->bInCover ? 1 : 0, static_cast<int32>(Member->GetStance()), Member->GetVelocity().Size2D()));
		}
	}

	AOperativeCharacter* FindCommander(USquadSubsystem* Squad)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member->SquadRole == EOperativeRole::Commander)
			{
				return Member;
			}
		}
		return Squad->GetLeader();
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	FString SaveDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("SmokeSaves/TutorialSave");
	}

	bool Finish(FState& State, bool bComplete)
	{
		if (State.AppliedHandle.IsValid())
		{
			USaveGameSubsystem::OnSaveApplied().Remove(State.AppliedHandle);
		}
		IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TutorialSaveSmoke"));
		return false;
	}

	UWorld* FindGameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World() && Context.World()->HasBegunPlay())
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	ATutorialTriggerVolume* FindVolume(UWorld* World, FName VolumeId)
	{
		for (TActorIterator<ATutorialTriggerVolume> It(World); It; ++It)
		{
			if (It->GetEffectiveVolumeId() == VolumeId)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Puts the squad into the level's volume (the leader in its middle, through the real overlap path) and makes sure it fired. */
	bool EnterLevelVolume(UWorld* World, FName VolumeId)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ATutorialTriggerVolume* Volume = FindVolume(World, VolumeId);
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Volume || !Leader)
		{
			return false;
		}
		const FVector Center = Volume->GetActorLocation();
		for (int32 Index = 0; Index < Squad->GetMembers().Num(); ++Index)
		{
			AOperativeCharacter* Member = Squad->GetMembers()[Index];
			Member->StopOperative();
			const FVector Spot(Center.X + (Member == Leader ? 0.f : -250.f - 90.f * Index), Center.Y, Leader->GetActorLocation().Z + 20.f);
			Member->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		}
		Volume->TryActivate(Leader); // no-op when the overlap already fired it
		return Volume->HasFired();
	}

	TSharedPtr<FJsonObject> ReadSlot(const FString& Slot)
	{
		FString Json;
		TSharedPtr<FJsonObject> Data;
		if (FFileHelper::LoadFileToString(Json, *(SaveDir() / (Slot + TEXT(".json")))))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Data);
		}
		return Data;
	}

	FString SavedBeat(const FString& Slot)
	{
		const TSharedPtr<FJsonObject> Data = ReadSlot(Slot);
		const TSharedPtr<FJsonObject>* Tutorial = nullptr;
		FString Beat;
		if (Data.IsValid() && Data->TryGetObjectField(TEXT("tutorial"), Tutorial))
		{
			(*Tutorial)->TryGetStringField(TEXT("beat"), Beat);
		}
		return Beat;
	}

	bool Step(TSharedRef<FState> StatePtr)
	{
		FState& State = *StatePtr;
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = FindGameWorld();
		if (State.Time > 400.f)
		{
			Check(State, false, FString::Printf(TEXT("timed out in stage %d"), State.Stage));
			return Finish(State, false);
		}
		if (!World || State.Time < 4.f)
		{
			return true;
		}
		USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>();
		UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>();
		USquadTransferSubsystem* Transfer = World->GetSubsystem<USquadTransferSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Saves || !Tutorial || !Transfer || !Squad || !Squad->GetLeader())
		{
			return true; // the reopened world is still starting
		}
		switch (State.Stage)
		{
		case 0:
		{
			Saves->SaveDirectoryOverride = SaveDir();
			IFileManager::Get().DeleteDirectory(*SaveDir(), false, true);
			Saves->bCheckpointsEnabled = true;
			Check(State, Tutorial->HasSteps() && Tutorial->IsTutorialActive() && Tutorial->GetCurrentBeat() == FName(TEXT("A0")),
				FString::Printf(TEXT("the review map runs the Demo 01 tutorial from the level (beat %s)"), *Tutorial->GetCurrentBeat().ToString()));
			Tutorial->HandlePlayerCommand(PlayerCommands::CameraZoom, FPlayerCommandContext()); // precredit history
			const bool bBeacon1 = EnterLevelVolume(World, TEXT("VOL_A0_BEACON1"));
			Check(State, bBeacon1 && Tutorial->GetCurrentBeat() == FName(TEXT("A1")),
				FString::Printf(TEXT("the leader in the level's VOL_A0_BEACON1: beat %s"), *Tutorial->GetCurrentBeat().ToString()));
			Check(State, Saves->HasSave(SaveGameRules::GetCheckpointSlotName()) && SavedBeat(SaveGameRules::GetCheckpointSlotName()) == TEXT("A1"),
				FString::Printf(TEXT("A1 checkpoint written at the beat start (beat in the file: %s)"), *SavedBeat(SaveGameRules::GetCheckpointSlotName())));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("sprint locked in A1 (until the heat pack lesson)"));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Check(State, EnterLevelVolume(World, TEXT("VOL_A1_BEACON2")), TEXT("the crack volume fires"));
			Check(State, Tutorial->GetStepState(TEXT("A1_wt_start")) == ETutorialStepState::Skipped, TEXT("'beacon two is past the crack' dropped at the crack (cancel_on)"));
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Tutorial->NotifyEvent(TEXT("BridgeCrossed"), Members[0]->GetFName());
			Tutorial->NotifyEvent(TEXT("BridgeCrossed"), Members[1]->GetFName());
			Check(State, Tutorial->GetStepProgress(TEXT("A1_bridge")) == 2, TEXT("two of three across the bridge"));
			TMap<ETransferItem, int32> HandedOver;
			HandedOver.Add(ETransferItem::HeatPack, 1);
			Transfer->RestoreHandedOverCounts(HandedOver); // one heat pack handed between mates (the share lesson's counter)
			State.ZoomUsesAtSave = Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraZoom).Count;
			FPlatformProcess::Sleep(1.1f); // the save timestamps have whole seconds: the quicksave is newer than the checkpoint
			Check(State, Saves->QuickSave(), TEXT("F5"));
			// Mutate, then F9.
			Transfer->RestoreHandedOverCounts(TMap<ETransferItem, int32>());
			State.AppliedHandle = USaveGameSubsystem::OnSaveApplied().AddLambda([StatePtr](UWorld*, const FString& Slot)
			{
				StatePtr->bApplied = Slot == SaveGameRules::GetQuicksaveSlotName();
				++StatePtr->AppliedCount;
				StatePtr->LastAppliedSlot = Slot;
			});
			Check(State, Saves->QuickLoad(), TEXT("F9: the newest quicksave / checkpoint is loaded with the map reopened"));
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			// The reopened world: wait for the save to be applied and the tutorial to pick it up.
			if ((!State.bApplied || !Tutorial->HasSteps() || State.StageTime < 2.f) && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, State.bApplied, TEXT("the quicksave was applied to the reopened map"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A1")), FString::Printf(TEXT("fresh world: beat %s (A1, not the level start)"), *Tutorial->GetCurrentBeat().ToString()));
			Check(State, Tutorial->GetStepState(TEXT("A1_wt_start")) == ETutorialStepState::Skipped, TEXT("fresh world: the dropped talk stays dropped"));
			Check(State, Tutorial->GetStepProgress(TEXT("A1_bridge")) == 2, FString::Printf(TEXT("fresh world: still two of three across the bridge (%d)"),
				Tutorial->GetStepProgress(TEXT("A1_bridge"))));
			Check(State, Tutorial->GetMachine().GetCommandUse(PlayerCommands::CameraZoom).Count == State.ZoomUsesAtSave && State.ZoomUsesAtSave > 0,
				TEXT("fresh world: the precredit history"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("fresh world: sprint still locked"));
			Check(State, FMath::IsNearlyEqual(Tutorial->GetConditionStat(TEXT("heat_packs_given")), 1.f), TEXT("fresh world: the heat pack hand-over counted"));
			const ATutorialTriggerVolume* Beacon2 = FindVolume(World, TEXT("VOL_A1_BEACON2"));
			const ATutorialTriggerVolume* Approach = FindVolume(World, TEXT("VOL_A1_HUT_APPROACH"));
			Check(State, Beacon2 && Beacon2->HasFired() && Approach && !Approach->HasFired(), TEXT("fresh world: the crack volume used up, the hut approach not"));
			Check(State, SavedBeat(SaveGameRules::GetCheckpointSlotName()) == TEXT("A1"), FString::Printf(
				TEXT("the A1 checkpoint was NOT overwritten by the reopened level's start (beat in the file: %s)"), *SavedBeat(SaveGameRules::GetCheckpointSlotName())));
			Saves->bCheckpointsEnabled = true;
			State.AppliedBefore = State.AppliedCount;
			Check(State, Tutorial->RetryFromCheckpoint(), TEXT("Retry from checkpoint (rev 2026-10-19: reopens the level)"));
			State.Stage = 3;
			State.StageTime = 0.f;
			return true;
		}
		case 3:
		{
			if ((State.AppliedCount == State.AppliedBefore || State.StageTime < 1.5f) && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, State.AppliedCount > State.AppliedBefore && State.LastAppliedSlot == SaveGameRules::GetCheckpointSlotName(),
				TEXT("retry: the checkpoint applied to the reopened level"));
			const ATutorialTriggerVolume* Beacon2 = FindVolume(World, TEXT("VOL_A1_BEACON2"));
			Check(State, Tutorial->GetCurrentBeat() == FName(TEXT("A1")) && Tutorial->GetStepState(TEXT("A1_bridge")) == ETutorialStepState::Idle,
				FString::Printf(TEXT("retry: back at the A1 start (bridge %s)"), TutorialRules::StateName(Tutorial->GetStepState(TEXT("A1_bridge")))));
			Check(State, Beacon2 && !Beacon2->HasFired(), TEXT("retry: the crack volume is NOT used up (it was entered after the checkpoint)"));
			Check(State, FMath::IsNearlyEqual(Tutorial->GetConditionStat(TEXT("heat_packs_given")), 0.f), TEXT("retry: no hand-over yet at the checkpoint"));
			Check(State, Tutorial->IsCommandLocked(PlayerCommands::Sprint), TEXT("retry: sprint locked"));
			Check(State, EnterLevelVolume(World, TEXT("VOL_A1_BEACON2")) && TutorialRules::IsRunning(Tutorial->GetStepState(TEXT("A1_bridge"))),
				TEXT("retry: walking into the crack again starts the bridge objective"));
			CheckCleanAfterLoad(State, Squad, TEXT("retry"));
			State.Stage = 4;
			State.StageTime = 0.f;
			return true;
		}
		// Rev 2026-10-19: the user's sequence - knocked down, Retry from checkpoint -> NOT getting up from that knockdown (twice).
		case 4:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			AOperativeCharacter* Commander = FindCommander(Squad);
			const bool bDown = Commander && Commander->KnockdownComponent
				&& Commander->KnockdownComponent->ForceKnockDown(EKnockdownDirection::Back, EKnockdownCause::HeavyMelee);
			Check(State, bDown && Commander->IsKnockedDown(), FString::Printf(TEXT("knockdown %d: the Commander on his back"), State.RetriesChecked + 1));
			State.AppliedBefore = State.AppliedCount;
			Check(State, Tutorial->RetryFromCheckpoint(), TEXT("Retry from checkpoint while he is down"));
			State.Stage = 5;
			State.StageTime = 0.f;
			return true;
		}
		case 5:
		{
			if ((State.AppliedCount == State.AppliedBefore || State.StageTime < 1.5f) && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, State.AppliedCount > State.AppliedBefore, TEXT("the checkpoint applied (level reopened)"));
			CheckCleanAfterLoad(State, Squad, *FString::Printf(TEXT("retry after knockdown %d"), ++State.RetriesChecked));
			State.Stage = State.RetriesChecked < 2 ? 4 : 6;
			State.StageTime = 0.f;
			return true;
		}
		// Solo ON saved -> after F9 Solo really holds the followers.
		case 6:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Squad->SetLeader(FindCommander(Squad));
			Squad->EnterSoloMode();
			FPlatformProcess::Sleep(1.1f); // newer than the checkpoint (whole-second timestamps)
			Check(State, Squad->IsSoloMode() && Saves->QuickSave(), TEXT("Solo ON, F5"));
			Squad->ExitSoloMode(); // the pre-load world regroups: the load must put Solo back
			State.AppliedBefore = State.AppliedCount;
			Check(State, Saves->QuickLoad(), TEXT("F9 with Solo saved ON"));
			State.Stage = 7;
			State.StageTime = 0.f;
			return true;
		}
		case 7:
		{
			if ((State.AppliedCount == State.AppliedBefore || State.StageTime < 1.5f) && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Squad->AreFollowersHolding(), FString::Printf(TEXT("after F9: Solo %d, followers holding %d (HUD = movement)"),
				Squad->IsSoloMode() ? 1 : 0, Squad->AreFollowersHolding() ? 1 : 0));
			CheckCleanAfterLoad(State, Squad, TEXT("F9 Solo"));
			State.FollowerSpots.Reset();
			AOperativeCharacter* Leader = Squad->GetLeader();
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				State.FollowerSpots.Add(Member->GetActorLocation());
			}
			if (Leader)
			{
				Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 500.f, false);
			}
			State.Stage = 8;
			State.StageTime = 0.f;
			return true;
		}
		case 8:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			const AOperativeCharacter* Leader = Squad->GetLeader();
			float LeaderMoved = 0.f;
			float FollowersMoved = 0.f;
			for (int32 Index = 0; Index < Members.Num() && State.FollowerSpots.IsValidIndex(Index); ++Index)
			{
				const float Moved = FVector::Dist2D(Members[Index]->GetActorLocation(), State.FollowerSpots[Index]);
				(Members[Index] == Leader ? LeaderMoved : FollowersMoved) = FMath::Max(Members[Index] == Leader ? LeaderMoved : FollowersMoved, Moved);
			}
			Check(State, LeaderMoved > 150.f && FollowersMoved < 60.f, FString::Printf(TEXT("Solo after the load: the leader walked %.0f cm, the others stayed (max %.0f cm)"),
				LeaderMoved, FollowersMoved));
			// The fight after a load: an Aggressive squad, a placed (level) enemy spots it.
			Squad->ExitSoloMode();
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>();
			const FVector Ahead = Leader ? Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1100.f : FVector::ZeroVector;
			AEnemyCharacter* Enemy = Waves && Leader ? Waves->SpawnEnemy(EEnemyArchetype::Frostbitten, Ahead + FVector(0.f, 0.f, 50.f),
				(-Leader->GetActorForwardVector()).Rotation()) : nullptr;
			if (!Enemy || !Encounter)
			{
				Check(State, false, TEXT("an enemy for the fight after the load"));
				return Finish(State, false);
			}
			Encounter->MarkLevelEnemy(Enemy);
			State.Enemy = Enemy;
			State.EnemyHealthAtContact = Enemy->GetHealthComponent()->GetCurrentHealth();
			ULevelEncounterSubsystem::NotifyHostileContactIn(World, EAmbushTrigger::PatrolDetection, Enemy);
			State.Stage = 9;
			State.StageTime = 0.f;
			return true;
		}
		case 9:
		{
			const AEnemyCharacter* Enemy = State.Enemy.Get();
			const bool bHit = !Enemy || Enemy->GetHealthComponent()->GetCurrentHealth() < State.EnemyHealthAtContact;
			if (!bHit && State.StageTime < 12.f)
			{
				return true;
			}
			const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Check(State, Flow && Flow->GetPhase() == ECodexGamePhase::WaveCombat, TEXT("after the loads: detection by a level enemy starts the fight"));
			Check(State, !Tutorial->IsCommandLocked(PlayerCommands::TargetedShot), TEXT("the fight released the tutorial locks (Attack.*)"));
			Check(State, bHit, FString::Printf(TEXT("the Aggressive squad shoots the enemy (%.1f s)"), State.StageTime));
			bool bPanic = false;
			for (const AOperativeCharacter* Member : Squad->GetMembers())
			{
				bPanic |= Member->PanicComponent && Member->PanicComponent->IsPanicking();
			}
			Check(State, !bPanic, TEXT("nobody panics / flees"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		// Checkpoints are written in this headless run, also in the world the F9 reopens (the CVar outlives the travel).
		if (IConsoleVariable* Headless = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Save.HeadlessCheckpoints")))
		{
			Headless->Set(1, ECVF_SetByCode);
		}
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float)
		{
			return Step(State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TutorialSaveSmoke"),
		TEXT("Dev check (review map + demo tutorial): checkpoint at a beat, F5, F9 reopening the map, Retry from checkpoint - the tutorial state round trips; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
