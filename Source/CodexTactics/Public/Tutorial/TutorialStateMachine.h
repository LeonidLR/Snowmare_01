#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
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
	/** Completed from the player's command history before it was shown ("precredit"): the lesson was already done. */
	bool bPrecredited = false;
	/** Seconds the step was shown before this change. */
	float ActiveSeconds = 0.f;
};

/**
 * The per-step state machine of the tutorial (pure C++, no world: unit-tested by CodexTactics.Tutorial.*).
 * Idle -> (trigger) -> Waiting (delay_s) -> Queued -> Active -> Completed | Skipped | TimedOut.
 * Display slots: one hint card at a time with a breather between cards, up to MaxObjectives objectives, one radio /
 * walk-and-talk step at a time. A queued step counts commands / events too and finishes silently when done before it
 * was shown (UX brief rule 6). Once-steps never re-trigger; others may after they finished. When a later beat starts (the
 * squad walked on, e.g. reached beacon 1 during the A0 camera lesson), the running hint cards of earlier beats and the
 * after_step cards that would follow them are dropped (precredited or skipped, DropEarlierBeatCard); radio / walk-and-talk
 * and objectives of earlier beats go on. UTutorialSubsystem feeds it
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

	/**
	 * Current value of a condition stat ("squad_cold_max", ...; TutorialRules::IsKnownStat). Set by the runtime
	 * (UTutorialSubsystem: the squad's cold / heat packs) or by tests; unset = every stat reads 0.
	 */
	TFunction<float(FName)> StatProvider;

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

	/** True when a lock pattern in force matches CommandId (beat / step "lock"; see FTutorialStep::Lock). */
	bool IsCommandLocked(FName CommandId) const;
	/**
	 * Drops every lock in force (a wave / horde fight started, the tutorial was switched off: the game must never stay
	 * crippled). Later beat / step "lock" lists still apply. False when nothing was locked.
	 */
	bool UnlockAll();
	/** The lock patterns in force, in the order they were added. */
	const TArray<FString>& GetLocks() const { return Locks; }
	/**
	 * The player's command history since the steps were loaded (every HandleCommand / HandleCommandHold, whether or not a step
	 * counted it): "precredit" completes a due command card the history already satisfies (TutorialRules::IsPrecreditSatisfied).
	 */
	const TMap<FName, FTutorialCommandUse>& GetCommandHistory() const { return History; }
	FTutorialCommandUse GetCommandUse(FName CommandId) const;

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
		/** The step's lock / unlock were applied (once per trigger). */
		bool bLocksApplied = false;
	};

	int32 IndexOf(FName StepId) const;
	bool CanTrigger(int32 Index) const;
	/** The step is a hint card of a beat before the current one (the story has moved on past it). */
	bool IsEarlierBeatCard(int32 Index) const;
	/**
	 * Drops a leftover hint card of an earlier beat without showing it any more: completed when the player's command history
	 * already satisfies it ("precredit"), else skipped. Its followers fire as usual (a chain of leftovers drops in one go).
	 */
	void DropEarlierBeatCard(int32 Index);
	/** Completes the step and marks its transition as precredited (the player had already done the lesson). */
	void FinishPrecredited(int32 Index);
	/** Every step of an after_step trigger's "step" list has finished at least once (FTutorialTrigger::AfterSteps). */
	bool AreAfterStepsFinished(int32 Index) const;
	/** Every "trigger.requires" step has been triggered at least once. */
	bool IsArmed(int32 Index) const;
	float ReadStat(FName Stat) const;
	bool AreConditionsMet(int32 Index) const;
	void ApplyStepLocks(int32 Index);
	void PollConditionTriggers();
	/** Which steps are running now (inputs only count for steps that were running when they came). */
	TArray<bool> RunningSnapshot() const;
	/** bForce: manual TriggerStep / ReplayStep (ignores "requires"). */
	void Fire(int32 Index, bool bForce = false);
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
	TArray<FString> Locks;
	TMap<FName, FTutorialCommandUse> History;
};
