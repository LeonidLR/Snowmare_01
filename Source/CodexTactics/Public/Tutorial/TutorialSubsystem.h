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
	bool HandleLethalHit(UHealthComponent* Health, const FString& Source);
	void WriteTelemetry(const FString& Event, FName StepId, FName BeatId, float ActiveSeconds) const;
	TArray<FSpokenLine> ResolveLines(const FTutorialStep& Step) const;
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
	float SquadBindTimer = 0.f;
	TArray<TWeakObjectPtr<AOperativeCharacter>> BoundMembers;
	TArray<TWeakObjectPtr<ATutorialMarkerActor>> Markers;
	FString SessionId;
	/** Progress loaded before the steps (a save applied before the level's steps file was read): applied by LoadSteps. */
	TOptional<FTutorialProgress> DeferredProgress;
};
