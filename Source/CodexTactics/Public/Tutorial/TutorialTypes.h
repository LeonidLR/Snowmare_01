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
	WalkTalk
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
	FailureRetry
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

struct CODEXTACTICS_API FTutorialTrigger
{
	ETutorialTriggerType Type = ETutorialTriggerType::Manual;
	FName Volume;
	/** Event name pattern (exact, "Prefix.*" or "*"). */
	FString Event;
	FName Step;
	/** Command id pattern for first_use. */
	FString Command;
	int32 Count = 1;
	float DelaySeconds = 0.f;
};

struct CODEXTACTICS_API FTutorialCompletion
{
	/** Command id patterns; any of them counts towards Count. */
	TArray<FString> Commands;
	/** Event name pattern; each occurrence counts towards Count. */
	FString Event;
	int32 Count = 1;
	/** Entering this volume completes the step. */
	FName Volume;
	/** > 0: an active step that is not done after this many seconds times out (hides; counts as finished). */
	float TimeoutSeconds = 0.f;

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
};

struct CODEXTACTICS_API FTutorialBeat
{
	FName Id;
	/** Lesson beat: a lethal hit only downs the operative (TutorialRules::ShouldDownInsteadOfKill); retry from the checkpoint. */
	bool bLesson = false;
	/** Autosave to the "checkpoint" slot when the beat starts. */
	bool bCheckpoint = true;
	FName TitleId;
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

/** Saved tutorial progress (the "tutorial" section of a save). */
struct CODEXTACTICS_API FTutorialProgress
{
	/** Finished steps: Completed / Skipped / TimedOut. */
	TMap<FName, ETutorialStepState> Finished;
	/** Steps that were triggered but not finished (re-queued on load). */
	TArray<FName> Pending;
	FName CurrentBeat;

	bool IsEmpty() const { return Finished.IsEmpty() && Pending.IsEmpty() && CurrentBeat.IsNone(); }
};
