#pragma once

#include "CoreMinimal.h"
#include "Core/PlayerCommands.h"
#include "GameFlow/GameFlowTypes.h"
#include "Survival/ColdRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"
#include "TutorialSubsystem.generated.h"

class AOperativeCharacter;
class ATutorialMarkerActor;
class ATutorialTriggerVolume;
class FJsonObject;
class UHealthComponent;

/** The hint card as the overlay shows it. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialCardView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Title;

	/** Body text with {key:<CommandId>} tokens resolved to key caps. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Body;

	/** "[B]  [Alt+V]". */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Keys;

	/** "2 / 5" among the cards of its beat (empty for a single card). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Progress;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bSkippable = true;

	/** The confirm moment (the step was just completed; the card hides after a short flash). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bDone = false;

	/** The operative the card is about (step "focus": "coldest_operative"), null when none. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<AOperativeCharacter> FocusOperative = nullptr;

	/** His display name (also substituted for the {focus} token of the text). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText FocusName;
};

/** One objective tracker line. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialObjectiveView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bDone = false;
};

/** The radio / walk-and-talk subtitle strip. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialSubtitleView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	/** Dialogue speaker name ("Commander", "Engineer", "Medic-Sapper", "HQ"). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Speaker;

	/** Role tag of the dialogue speaker card (DialogueRules::GetSpeakerStyle). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Role;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Line;

	/** Linear colour of the speaker's name (dialogue card colour). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FLinearColor SpeakerColor = FLinearColor::White;

	/** Seconds left on this line. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	float SecondsLeft = 0.f;

	/** 0-based index of the shown line and the step's line count ("2 / 5"). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	int32 LineIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	int32 LineCount = 0;
};

/** The "Retry from checkpoint" prompt after a lesson down. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialRetryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bHasCheckpoint = false;
};

/**
 * Demo 01 tutorial framework (producer plan C8, UX brief 03 §2): loads Content/Data/Tutorial/<file>.json (the level JSON
 * "demo" object names it), runs the per-step state machine (FTutorialStateMachine) on player commands
 * (UCodexEventBus::OnPlayerCommand, the PlayerCommands::Report choke point), gameplay events, tutorial volumes and time;
 * feeds the non-blocking overlay (UTutorialOverlayWidget: hint card, objective tracker, radio / walk-and-talk strip that
 * never pauses the game) and the world markers (ATutorialMarkerActor); writes Saved/Tutorial/tutorial.jsonl (testers'
 * telemetry); saves the completed steps in the save's "tutorial" section; per-beat checkpoint autosave (slot
 * "checkpoint") and, in lesson beats, a lethal hit downs the operative instead of killing him ("Retry from checkpoint").
 * Tutorial ON / OFF: SetTutorialEnabled / console Codex.Tutorial.Enabled (a player setting kept in
 * Saved/Tutorial/profile.json). Texts: FNarrativeManifest::GetTutorialText ("[EN missing: <id>]" until the Tutorial tab
 * exists, TANDEM request #17). UE-only.
 */
UCLASS()
class CODEXTACTICS_API UTutorialSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Checkpoint save slot of the beats. */
	static const TCHAR* CheckpointSlot;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	// --- Setup ------------------------------------------------------------------------------------------------------

	/** Loads a steps file from Content/Data/Tutorial (the level's demo flags do this on their own) and starts it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool LoadStepsFile(const FString& FileName);

	/** Loads already-parsed steps (smokes, tests) and starts them. */
	void LoadSteps(const FTutorialStepsData& Data);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool HasSteps() const { return Machine.IsLoaded(); }

	/** Steps loaded and the player's tutorial setting on. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsTutorialActive() const;

	/** The player's tutorial setting (console Codex.Tutorial.Enabled; saved to Saved/Tutorial/profile.json). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	static bool IsTutorialEnabled();

	/** Tutorial ON / OFF (the pause-menu entry later); OFF hides everything and stops counting. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void SetTutorialEnabled(bool bEnabled);

	// --- Inputs (also from Blueprints: level scripts may raise their own events) --------------------------------------

	/** A player command (normally through UCodexEventBus::OnPlayerCommand). */
	void HandlePlayerCommand(FName CommandId, const FPlayerCommandContext& Context);

	/** A gameplay event (normally through UCodexEventBus::OnGameplayEvent); SubjectId = the operative's id (distinct counts). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void NotifyEvent(FName EventName, FName SubjectId = NAME_None);

	/** Ids of the tutorial volumes (ATutorialTriggerVolume::GetEffectiveVolumeId) whose box holds Location ("in_volume" completions). */
	TArray<FName> GetTutorialVolumesAt(const FVector& Location) const;

	/** A squad member entered a tutorial volume; false when it did not count (tutorial off, required beat not reached). */
	bool NotifyVolumeEntered(ATutorialTriggerVolume* Volume, AOperativeCharacter* Operative);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool StartBeat(FName BeatId);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool TriggerStep(FName StepId);

	/** Skips the visible hint card ("Skip" on the card). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipActiveCard();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipStep(FName StepId);

	// --- Dialogue skip (user request 2026-10-13: radio / walk-and-talk, every level) -----------------------------------

	/** A radio / walk-and-talk line is on screen now (the Enter key and the strip's SKIP act on it). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsSpeechActive() const;

	/**
	 * Enter (tap): the next line at once; after the last one the step ends exactly as if it had played out. False when no
	 * speech is shown. The lines are already in the log (all of them are logged when the speech starts).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool AdvanceSpeechLine();

	/**
	 * Enter (hold) / the strip's SKIP: the whole conversation ends now, exactly as if it had played out (COMPLETED, not
	 * skipped: its after_step followers fire and its lock / unlock lists apply, e.g. the walk card after the HQ radio).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipSpeech();

	/** Replays a finished step (pause menu "replay" later). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool ReplayStep(FName StepId);

	/** Forgets every step's progress and starts the tutorial again from its start beat. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void ResetTutorial();

	/** "Retry from checkpoint": loads the "checkpoint" slot in place. False without one. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool RetryFromCheckpoint();

	/** "Carry on" on the retry prompt: hides it (the downed operative keeps 1 HP). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void DismissRetryPrompt() { bRetryPrompt = false; }

	/** Saves the "checkpoint" slot now (beat start does it on its own). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SaveCheckpoint();

	// --- Queries ----------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	ETutorialStepState GetStepState(FName StepId) const { return Machine.GetState(StepId); }
	/** How far a running step got (commands / events counted towards its completion count). */
	int32 GetStepProgress(FName StepId) const { return Machine.GetProgress(StepId); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FName GetCurrentBeat() const { return Machine.GetCurrentBeat(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsLessonBeat() const { return Machine.IsLessonBeat(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialCardView GetCardView() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	TArray<FTutorialObjectiveView> GetObjectiveViews() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialSubtitleView GetSubtitleView() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialRetryView GetRetryView() const;

	/** The English text of a tutorial text id (narrative manifest), "[EN missing: <id>]" until it exists. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	static FText GetTutorialText(FName TextId);

	/** Markers shown now (the overlay draws their screen-edge pointers). */
	TArray<ATutorialMarkerActor*> GetVisibleMarkers() const;

	const FTutorialStateMachine& GetMachine() const { return Machine; }

	/** Operatives downed by the lesson rule since the last checkpoint load (smokes). */
	int32 GetLessonDownCount() const { return LessonDownCount; }

	/** Log file of this run (smokes). */
	FString GetTelemetryPath() const;

	// --- Command locks, conditions, focus (2026-10-12, Demo 01 A0-A1 redesign) ------------------------------------------

	/** True when the tutorial is active and a beat / step lock covers CommandId (e.g. "Order.Sprint" until warmed up in A1). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsCommandLocked(FName CommandId) const;

	/**
	 * THE lock choke point for order handlers: true when CommandId is locked now; the caller then refuses / degrades the order
	 * and this raises "Tutorial.Locked" + "Tutorial.Locked.<CommandId>" (a "feed" / card step can react once). When no step
	 * reacts to that event, ONE short feed line per command family ("Stance", "Time", ...) says it is not taught yet (Demo 01
	 * progressive unlock, user request 2026-10-13). False without a tutorial (tutorial OFF = no locks).
	 */
	static bool CheckCommandLocked(const UObject* WorldContext, FName CommandId);

	/**
	 * The command an order handler should carry out (TutorialRules::ResolveLockedCommand): CommandId when free, its fallback
	 * when locked ("Order.Sprint" -> "Order.Move"), NAME_None when locked without one. A locked command raises the
	 * "Tutorial.Locked" events (CheckCommandLocked).
	 */
	static FName FilterCommand(const UObject* WorldContext, FName CommandId);

	/** Locked commands refused since the steps were loaded (smokes). */
	int32 GetLockedRefusalCount() const { return LockedRefusals; }

	/** "Not taught yet" feed lines posted (one per command family, smokes). */
	int32 GetLockHintCount() const { return LockHintFamilies.Num(); }

	/** Hint cards completed from the command history without being shown ("precredit", smokes). */
	int32 GetPrecreditedCount() const { return PrecreditedCount; }

	/**
	 * A step condition stat now: "squad_cold_max" / "squad_cold_min" (cold % of the living squad members), "leader_cold",
	 * "squad_heat_packs" (heat packs carried by the whole squad), "squad_min_heat_packs" (the fewest any living member carries),
	 * "heat_packs_given" (heat packs handed between squad mates since the level started, USquadTransferSubsystem; rev 5d).
	 */
	float GetConditionStat(FName Stat) const;

	/** The operative a shown step focuses on ("focus"), null when none. */
	AOperativeCharacter* GetFocusOperative(FName StepId) const;

	// --- Markers / save ---------------------------------------------------------------------------------------------

	void RegisterMarker(ATutorialMarkerActor* Marker);
	void UnregisterMarker(ATutorialMarkerActor* Marker);

	/** The save's "tutorial" object (null when there is nothing to save). */
	TSharedPtr<FJsonObject> CaptureSaveState() const;
	void RestoreSaveState(const FJsonObject& Object);

private:
	struct FSpokenLine
	{
		FString Speaker;
		FString Text;
		float Duration = 3.f;
	};

	void InitFromLevel();
	void StartTutorial();
	void BindWorldEvents();
	void BindSquadMembers();
	void ProcessTransitions();
	void RefreshMarkers();
	void StartSpeech(FName StepId);
	void TickSpeech(float DeltaSeconds);
	/** The speech step's lines are over (played out, advanced past the last one or skipped): it completes unless it waits for more. */
	void EndSpeech();
	bool HandleLethalHit(UHealthComponent* Health, const FString& Source);
	void WriteTelemetry(const FString& Event, FName StepId, FName BeatId, float ActiveSeconds) const;
	TArray<FSpokenLine> ResolveLines(const FTutorialStep& Step) const;
	/** A step was shown: focus, world actions, feed lines. */
	void HandleStepShown(const FTutorialStep& Step);
	void RunActions(const FTutorialStep& Step);
	/** The step completes in a volume: the squad members standing in it already count (FTutorialStateMachine::HandleVolumePresence). */
	void CountVolumePresence(const FTutorialStep& Step);
	/** {focus} -> the focused operative's name. */
	FString ResolveFocusToken(const FString& Text, FName StepId) const;
	AOperativeCharacter* FindColdestMember() const;
	FTutorialCardView BuildCardView(FName StepId, bool bDone) const;
	FText ObjectiveText(FName StepId) const;

	UFUNCTION()
	void HandleBusCommand(FName CommandId, const FPlayerCommandContext& Context);
	UFUNCTION()
	void HandleBusEvent(FName EventName, AActor* Subject);
	UFUNCTION()
	void HandleMineSpotted(AActor* Mine, AOperativeCharacter* Spotter);
	UFUNCTION()
	void HandleItemUsed(AOperativeCharacter* Soldier, const FString& ItemId);
	UFUNCTION()
	void HandleSoldierDowned(AOperativeCharacter* Soldier);
	UFUNCTION()
	void HandleDialogueFinished();
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);
	UFUNCTION()
	void HandleColdTierChanged(AOperativeCharacter* Operative, EColdTier Tier);

	FTutorialStateMachine Machine;
	FString StepsFile;
	bool bInitDone = false;
	bool bBusBound = false;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;

	/** Speech playback of the active radio / walk-and-talk step. */
	FName SpeechStep;
	TArray<FSpokenLine> SpeechLines;
	int32 SpeechIndex = 0;
	float SpeechLineLeft = 0.f;

	/** The confirm flash of the card / objectives that just completed. */
	FName FlashCard;
	float FlashCardLeft = 0.f;
	TMap<FName, float> FlashObjectives;

	bool bRetryPrompt = false;
	int32 LessonDownCount = 0;
	int32 LockedRefusals = 0;
	/** Command families ("Stance", "Time", ...) that already got their "not taught yet" feed line. */
	TSet<FString> LockHintFamilies;
	/** Posts the "not taught yet" line for CommandId's family once, unless a step of the file reacts to its locked event. */
	void PostLockHint(FName CommandId);
	/** Drops every lock (a fight started / the tutorial was switched off) and logs why. */
	void UnlockAllCommands(const TCHAR* Reason);
	int32 PrecreditedCount = 0;
	/** Operative each shown "focus" step is about (resolved when it shows). */
	TMap<FName, TWeakObjectPtr<AOperativeCharacter>> FocusOperatives;
	float SquadBindTimer = 0.f;
	TArray<TWeakObjectPtr<AOperativeCharacter>> BoundMembers;
	TArray<TWeakObjectPtr<ATutorialMarkerActor>> Markers;
	FString SessionId;
	/** Progress loaded before the steps (a save applied before the level's steps file was read): applied by LoadSteps. */
	TOptional<FTutorialProgress> DeferredProgress;
};
