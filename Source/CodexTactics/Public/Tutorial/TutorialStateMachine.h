#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialTypes.h"

/** A change the tutorial runtime reports to its owner (UI, log, checkpoint). */
struct CODEXTACTICS_API FTutorialTransition
{
	enum class EType : uint8 { StepState, BeatStarted };

	EType Type = EType::StepState;
	FName StepId;
	FName BeatId;
	ETutorialStepState From = ETutorialStepState::Idle;
	ETutorialStepState To = ETutorialStepState::Idle;
	/** Finished while queued: never shown. */
	bool bSilent = false;
	/** Seconds the step was shown before this change. */
	float ActiveSeconds = 0.f;
};

/**
 * The per-step state machine of the tutorial (pure C++, no world: unit-tested by CodexTactics.Tutorial.*).
 * Idle -> (trigger) -> Waiting (delay_s) -> Queued -> Active -> Completed | Skipped | TimedOut.
 * Display slots: one hint card at a time with a breather between cards, up to MaxObjectives objectives, one radio /
 * walk-and-talk step at a time. A queued step counts commands / events too and finishes silently when done before it
 * was shown (UX brief rule 6). Once-steps never re-trigger; others may after they finished. UTutorialSubsystem feeds it
 * commands, events, volumes and time, and drains ConsumeTransitions().
 */
class CODEXTACTICS_API FTutorialStateMachine
{
public:
	/** Seconds between two hint cards (UX brief 03 §2.3: confirm, then a 1.5 s breather). */
	float CardBreatherSeconds = 1.5f;
	int32 MaxObjectives = 3;
	/** At most one failure_retry (remedial) step per this many seconds. */
	float RemedialCooldownSeconds = 60.f;

	/** Replaces the steps and resets all progress. */
	void Load(const FTutorialStepsData& InData);
	bool IsLoaded() const { return Data.Steps.Num() > 0; }
	const FTutorialStepsData& GetData() const { return Data; }

	// --- Inputs -----------------------------------------------------------------------------------------------------
	void HandleCommand(FName CommandId);
	/**
	 * A hold tick of a continuous command (the action is still going on): adds DeltaSeconds to every ACTIVE step whose
	 * completion counts the command and has "hold_s"; it completes once uses (Count) and hold time are both reached. Never
	 * counts as a use and never fires first_use.
	 */
	void HandleCommandHold(FName CommandId, float DeltaSeconds);
	/** A gameplay event; SubjectId (the operative's id) feeds "distinct_operatives" completions. */
	void HandleEvent(FName EventName, FName SubjectId = NAME_None);
	/** A squad member entered a tutorial volume: VolumeId for "volume" triggers / completions, StepId / BeatId start them directly. */
	void HandleVolume(FName VolumeId, FName StepId, FName BeatId);
	/** Starts a beat (only forward unless bAllowBackwards). Returns true when the current beat changed. */
	bool StartBeat(FName BeatId, bool bAllowBackwards = false);
	/** Fires a step now (manual triggers, replay). False when it may not trigger (running, or once and finished). */
	bool TriggerStep(FName StepId);
	/** Skips a running skippable step. */
	bool SkipStep(FName StepId);
	/** Finishes a running step as completed (radio / walk-and-talk lines done, scripted completion). */
	bool CompleteStep(FName StepId);
	/** Forgets a step's progress and fires it again. */
	bool ReplayStep(FName StepId);
	/** Clears all progress (steps stay loaded). */
	void ResetProgress();
	void Tick(float DeltaSeconds);

	// --- Queries ----------------------------------------------------------------------------------------------------
	ETutorialStepState GetState(FName StepId) const;
	int32 GetProgress(FName StepId) const;
	/** Hold seconds collected so far ("complete.hold_s" steps). */
	float GetHeldSeconds(FName StepId) const;
	/** Seconds the step has been in its current state. */
	float GetStateSeconds(FName StepId) const;
	FName GetActiveCard() const;
	TArray<FName> GetActiveObjectives() const;
	FName GetActiveSpeech() const;
	FName GetCurrentBeat() const;
	const FTutorialBeat* GetCurrentBeatData() const;
	bool IsLessonBeat() const;
	/** "2 / 5": the card's place among the hint cards of its beat (1-based) and their count. */
	void GetCardProgress(FName StepId, int32& OutIndex, int32& OutCount) const;

	// --- Save -------------------------------------------------------------------------------------------------------
	FTutorialProgress ExportProgress() const;
	/** Restores finished steps and the beat; pending steps are queued again (no transitions for the finished ones). */
	void ImportProgress(const FTutorialProgress& Progress);

	TArray<FTutorialTransition> ConsumeTransitions();

private:
	struct FRuntime
	{
		ETutorialStepState State = ETutorialStepState::Idle;
		int32 Progress = 0;
		/** Accumulated hold time of the counted commands while Active ("complete.hold_s"). */
		float HeldSeconds = 0.f;
		float StateSeconds = 0.f;
		float DelayLeft = 0.f;
		int32 TriggerHits = 0;
		bool bEverFinished = false;
		/** Subjects already counted by a distinct_operatives completion. */
		TSet<FName> CountedSubjects;
	};

	int32 IndexOf(FName StepId) const;
	bool CanTrigger(int32 Index) const;
	void Fire(int32 Index);
	void Enqueue(int32 Index);
	void Activate(int32 Index);
	void Finish(int32 Index, ETutorialStepState FinalState);
	void SetState(int32 Index, ETutorialStepState NewState, bool bSilent = false);
	void AddProgress(int32 Index, int32 Amount, int32 Needed);
	void PumpQueues();
	void FireBeatStartSteps(FName BeatId);

	FTutorialStepsData Data;
	TArray<FRuntime> Runtime;
	TArray<int32> Queue;
	int32 CurrentBeatIndex = INDEX_NONE;
	float CardBreatherLeft = 0.f;
	float RemedialCooldownLeft = 0.f;
	TSet<FName> UsedCommands;
	TArray<FTutorialTransition> Transitions;
};
