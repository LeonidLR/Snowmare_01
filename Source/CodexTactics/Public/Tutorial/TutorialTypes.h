#pragma once

#include "CoreMinimal.h"
#include "TutorialTypes.generated.h"

/** How a tutorial step is presented (steps JSON "kind"). */
UENUM(BlueprintType)
enum class ETutorialStepKind : uint8
{
	/** "hint_card": title + body + key caps, bottom-left; one at a time. */
	HintCard,
	/** "objective": a line of the objective tracker (at most 3 at once). */
	Objective,
	/** "radio": HQ / squad radio lines in the subtitle strip. */
	Radio,
	/** "walk_talk": squad banter in the subtitle strip while the squad keeps moving (never pauses). */
	WalkTalk,
	/**
	 * "feed": its line(s) go to the message feed at once (no display slot, never queued behind a card or a speech) and the
	 * step completes immediately. For short reactions ("No running yet") while a card / speech is up.
	 */
	Feed
};

/** What starts a step (steps JSON "trigger.type"). */
enum class ETutorialTriggerType : uint8
{
	/** "manual": only UTutorialSubsystem::TriggerStep / a volume naming the step. */
	Manual,
	/** "beat_start": when its beat starts. */
	BeatStart,
	/** "volume": a squad member enters the ATutorialTriggerVolume whose VolumeId (or StepId) is "trigger.volume". */
	Volume,
	/** "event": a gameplay event ("trigger.event", "*" wildcard) happened "trigger.count" times. */
	Event,
	/** "after_step": another step ("trigger.step") finished (completed, skipped or timed out). */
	AfterStep,
	/** "first_use": the first time the player issues a command matching "trigger.command". */
	FirstUse,
	/** "failure_retry": a failure event ("trigger.event") happened "trigger.count" times; remedial, rate-limited. */
	FailureRetry,
	/** "condition": polled every tick; fires as soon as the step's "condition" clauses hold (world state, e.g. squad cold). */
	Condition
};

/** Runtime state of one step. */
UENUM(BlueprintType)
enum class ETutorialStepState : uint8
{
	Idle,
	/** Triggered, waiting for "trigger.delay_s". */
	Waiting,
	/** Triggered, waiting for its display slot (one card / 3 objectives / one speech at a time). */
	Queued,
	/** Shown. */
	Active,
	Completed,
	Skipped,
	TimedOut
};

/** One line of a radio / walk-and-talk step. */
struct CODEXTACTICS_API FTutorialLine
{
	/** Speaker as in the dialogues ("Commander", "Engineer", "Medic-Sapper", "HQ"); empty = the manifest's speaker_en. */
	FString Speaker;
	FName TextId;
	/** Seconds on screen; <= 0 = from the text length (TutorialRules::LineDurationSeconds). */
	float DurationSeconds = 0.f;
};

/**
 * One clause of a step "condition" (JSON {"stat": "squad_cold_max", "at_least": 40} or {"stat": ..., "below": 40}). Stats
 * come from the runtime (UTutorialSubsystem::GetConditionStat): "squad_cold_max", "squad_cold_min", "leader_cold",
 * "squad_heat_packs". All clauses of a step must hold (AND).
 */
struct CODEXTACTICS_API FTutorialCondition
{
	FName Stat;
	/** True: Stat >= Value ("at_least"); false: Stat < Value ("below"). */
	bool bAtLeast = true;
	float Value = 0.f;
};

/** A world action a step runs when it is shown (JSON "actions": [{ "type": ... }] or a single "action": {...}). */
struct CODEXTACTICS_API FTutorialAction
{
	/** "cold_bump": every squad member's cold + Amount, raised to at least MinLevel (UColdSurvivalComponent::AddColdShock). */
	FName Type;
	float Amount = 0.f;
	/** "min_level": cold_bump never leaves anyone below this (0 = no floor). */
	float MinLevel = 0.f;
};

struct CODEXTACTICS_API FTutorialTrigger
{
	ETutorialTriggerType Type = ETutorialTriggerType::Manual;
	FName Volume;
	/** Event name pattern (exact, "Prefix.*" or "*"). */
	FString Event;
	FName Step;
	/**
	 * "after_step" with "step": [ ... ] (an array): EVERY listed step must have finished (completed, skipped or timed out);
	 * the last of them to finish fires the trigger (e.g. the walk card waits for both the HQ radio and the camera chain).
	 * Always holds Step first; a single "step" string leaves it with that one id.
	 */
	TArray<FName> AfterSteps;
	/** Command id pattern for first_use. */
	FString Command;
	int32 Count = 1;
	float DelaySeconds = 0.f;
	/**
	 * "requires": step ids that must have been triggered (left Idle) before this trigger counts at all (events / volumes /
	 * conditions before that are ignored, not stored). Manual TriggerStep / ReplayStep ignore it.
	 */
	TArray<FName> Requires;
};

struct CODEXTACTICS_API FTutorialCompletion
{
	/** Command id patterns; any of them counts towards Count. */
	TArray<FString> Commands;
	/** Event name pattern; each occurrence counts towards Count. */
	FString Event;
	int32 Count = 1;
	/**
	 * "distinct_operatives": only the first matching event of each subject (operative) counts, so Count = how many different
	 * operatives must do it ("put each operative into cover"). Events without a subject never count then.
	 */
	bool bDistinctSubjects = false;
	/** Entering this volume completes the step. */
	FName Volume;
	/** > 0: an active step that is not done after this many seconds times out (hides; counts as finished). */
	float TimeoutSeconds = 0.f;
	/**
	 * "hold_s" > 0: besides Count uses, the matching continuous command (key pan, edge pan, drags) must have been active this
	 * many seconds in total while the step is shown (hold ticks, PlayerCommands::ReportHold). 0 = count only.
	 */
	float HoldSeconds = 0.f;

	bool HasCondition() const { return Commands.Num() > 0 || !Event.IsEmpty() || !Volume.IsNone(); }
};

struct CODEXTACTICS_API FTutorialStep
{
	FName Id;
	FName Beat;
	ETutorialStepKind Kind = ETutorialStepKind::HintCard;
	/** Narrative manifest id of the card / objective text (or of the single radio line). */
	FName TextId;
	/** Key caps for the card: command ids ("Squad.Solo" -> "[B]") or literal labels ("Alt+V"). */
	TArray<FString> Keys;
	FTutorialTrigger Trigger;
	FTutorialCompletion Complete;
	/** Never shown again once finished (default). False: may re-trigger (remedial hints). */
	bool bOnce = true;
	bool bSkippable = true;
	/** Always false: the tutorial never blocks the game (kept so the data contract is explicit). */
	bool bBlocking = false;
	/** Radio / walk-and-talk lines; empty = one line from TextId. */
	TArray<FTutorialLine> Lines;
	/**
	 * "condition": clauses that must hold when the step becomes due (after its delay, before it queues); otherwise it is
	 * skipped silently (its after_step followers still fire). For a "condition" trigger they are also the trigger.
	 */
	TArray<FTutorialCondition> Conditions;
	/**
	 * Command locks (JSON "lock" / "unlock": command id patterns, "*" = all): applied once, when the step is shown (or, if it
	 * never shows, when it finishes). Unlock first, then lock. A locked command is degraded by its handler (a locked sprint
	 * becomes a walk) and raises "Tutorial.Locked" + "Tutorial.Locked.<CommandId>".
	 */
	TArray<FString> Lock;
	TArray<FString> Unlock;
	/** World actions run by the runtime when the step is shown. */
	TArray<FTutorialAction> Actions;
	/** "focus": "coldest_operative" -> the card names / marks the coldest squad member ({focus} token in the text). */
	FName Focus;
	/**
	 * "precredit" (default true; user decision 2026-10-13): a hint card completed by COMMANDS whose completion the player's
	 * command history (counts / hold time since the level started, FTutorialStateMachine) already satisfies when the step
	 * becomes due is completed at once without showing the card (a brief feed tick instead). False for lessons that must be
	 * done at that moment (a location, a specific target). Event / volume completions are never precredited.
	 */
	bool bPrecredit = true;
};

struct CODEXTACTICS_API FTutorialBeat
{
	FName Id;
	/** Lesson beat: a lethal hit only downs the operative (TutorialRules::ShouldDownInsteadOfKill); retry from the checkpoint. */
	bool bLesson = false;
	/** Autosave to the "checkpoint" slot when the beat starts. */
	bool bCheckpoint = true;
	FName TitleId;
	/** Command locks applied when the beat starts ("lock" / "unlock", as on steps). */
	TArray<FString> Lock;
	TArray<FString> Unlock;
};

/** A parsed Content/Data/Tutorial/<file>.json. */
struct CODEXTACTICS_API FTutorialStepsData
{
	int32 Version = 1;
	FString Id;
	/** Beat that starts with the tutorial (else the first beat a step / volume starts). */
	FName StartBeat;
	TArray<FTutorialBeat> Beats;
	TArray<FTutorialStep> Steps;

	const FTutorialStep* FindStep(FName StepId) const;
	int32 FindStepIndex(FName StepId) const;
	int32 FindBeatIndex(FName BeatId) const;
};

/** One command's use history (precredit): uses since the level started and the accumulated hold time of a continuous command. */
struct CODEXTACTICS_API FTutorialCommandUse
{
	int32 Count = 0;
	float HeldSeconds = 0.f;
};

/** Saved tutorial progress (the "tutorial" section of a save). */
struct CODEXTACTICS_API FTutorialProgress
{
	/** Finished steps: Completed / Skipped / TimedOut. */
	TMap<FName, ETutorialStepState> Finished;
	/** Steps that were triggered but not finished (re-queued on load). */
	TArray<FName> Pending;
	FName CurrentBeat;
	/** Command lock patterns in force ("locks"). */
	TArray<FString> Locks;
	/** The save had a "locks" array (older saves: the locks are rebuilt from the beat and the finished steps). */
	bool bHasLocks = false;
	/** Player command history ("history": {id: {count, held_s}}) for precredited lessons. */
	TMap<FName, FTutorialCommandUse> History;

	bool IsEmpty() const { return Finished.IsEmpty() && Pending.IsEmpty() && CurrentBeat.IsNone() && Locks.IsEmpty() && History.IsEmpty(); }
};
