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
 * come from the runtime (UTutorialSubsystem::GetConditionStat): "squad_cold_max", "squad_cold_min", "leader_cold", "squad_injury_max"
 * (the share of health the most wounded living member has lost, %: 30 = he is below 70 %; rev 2026-10-19),
 * "squad_heat_packs", "squad_min_heat_packs" (the fewest any living operative carries), "heat_packs_given" (heat packs handed
 * between squad mates since the level started; rev 5d). All clauses of a step must hold (AND).
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
	/**
	 * "in_volume" (rev 5c, 2026-10-13): an event counts only when its subject (the operative) stands inside a tutorial volume
	 * with this id (ATutorialTriggerVolume::GetEffectiveVolumeId) at that moment - "cover entered at the left side of the door".
	 * Events without a subject, or a subject outside every such volume, never count then.
	 */
	FName InVolume;
	/**
	 * "distinct_from" (rev 5c): an operative who already counted for any of these steps never counts here (events and volume
	 * entries; needs a subject) - three DIFFERENT operatives for "left of the door", "right of the door" and "the one who goes
	 * in". Who counted is runtime state (not saved: after a load mid-lesson any operative may count again).
	 */
	TArray<FName> DistinctFrom;
	/**
	 * "subject": "focus" (rev 2026-10-19, the medkit lesson): an event counts only when its subject is the step's focus operative
	 * (FTutorialStateMachine::FocusSubjectProvider - "the medkit heals THAT operative"). No focus chosen yet: any subject counts.
	 */
	bool bFocusSubject = false;
	/** > 0: an active step that is not done after this many seconds times out (hides; counts as finished). */
	float TimeoutSeconds = 0.f;
	/**
	 * "hold_s" > 0: besides Count uses, the matching continuous command (key pan, edge pan, drags) must have been active this
	 * many seconds in total while the step is shown (hold ticks, PlayerCommands::ReportHold). 0 = count only.
	 */
	float HoldSeconds = 0.f;
	/**
	 * "condition" inside "complete" (rev 5d, 2026-10-14): world-state clauses (same syntax as the step "condition") polled while
	 * the step runs - it completes as soon as all hold ("every operative carries a heat pack"). Already true when the step becomes
	 * due: completed at once without showing (precredited, unless "precredit": false - then it finishes on the first poll).
	 */
	TArray<FTutorialCondition> Conditions;

	bool HasCondition() const { return Commands.Num() > 0 || !Event.IsEmpty() || !Volume.IsNone() || !Conditions.IsEmpty(); }
};

/**
 * "cancel_on" (rev 5e, game designer 2026-10-14: the bridge lines kept playing long after the crossing): the step is dropped as
 * soon as the place / moment it talks about is over - one of "step" (string or array: that step FINISHED - completed, skipped or
 * timed out), "event" (a gameplay event pattern happened) or "volume" (a squad member entered that tutorial volume). Waiting /
 * queued: skipped silently (never played later); shown: a radio / walk-and-talk lets the line on screen run out and drops the rest,
 * any other kind hides. Already due when the step would queue (the step named here finished before): skipped at once. The
 * cancelled step finishes as Skipped, so its after_step followers still fire.
 */
struct CODEXTACTICS_API FTutorialCancelOn
{
	TArray<FName> Steps;
	FString Event;
	TArray<FName> Volumes;

	bool IsSet() const { return Steps.Num() > 0 || !Event.IsEmpty() || Volumes.Num() > 0; }
};

/**
 * "confine" (rev 5i, game designer 2026-10-15: "the player just runs everyone into the hut and skips the tactical entry"): while the
 * confinement holds, the squad is kept in an area - every move order (player clicks, cover orders, walk-ups, autonomy) whose
 * destination or path end lies outside the allowed area is refused with a short feed line, and formation moves are pulled back
 * into the holding volume. Starts when the step is triggered (a volume step: the moment the squad arrives), ends when the step named
 * by "until" (default: the step itself) finishes, when a later beat starts, or when the tutorial is switched off / skipped / a fight
 * starts (UTutorialSubsystem). One confinement at a time (a new one replaces the old). JSON:
 *   "confine": { "volume": "VOL_A1_HUT_APPROACH", "allow": ["VOL_A1_DOOR_L", ...], "until": "A1_hut_enter", "stop": true,
 *                "span": true, "stance": "crouch", "exit": { "stance": "prone", "one_at_a_time": "CrawlGapPassed" } }
 */
struct CODEXTACTICS_API FTutorialConfine
{
	/** The holding area (a tutorial volume id): always allowed. */
	FName Volume;
	/**
	 * More areas the squad may be sent to. An area that some step COMPLETES in ("complete.volume" / "complete.in_volume") opens only
	 * when that step is shown or has finished - the lesson's order ("left of the door, then right, then the third goes in"); an area
	 * no step completes in is always open.
	 */
	TArray<FName> Allow;
	/** The confinement ends when this step finishes (completed, skipped or timed out); none = the confining step itself. */
	FName Until;
	/** "stop": the whole squad stops where it is when the confinement starts (a walk in progress is otherwise only stopped when it heads outside). */
	bool bStop = false;
	/** "span": the allowed area is ONE box - the bounding box of the holding volume and every allowed volume (a corridor between them). */
	bool bSpan = false;
	/** "stance": "crouch" | "prone" - every move order needs the ordered operative in that stance (crouch: crouched or prone). */
	FName Stance;
	/** "exit.stance": an order OUTSIDE the allowed area is allowed for an operative in this stance (with "exit.one_at_a_time"). */
	FName ExitStance;
	/**
	 * "exit.one_at_a_time": only one operative at a time may be sent outside (his turn starts with the first such order and ends when
	 * this gameplay event names him - e.g. "CrawlGapPassed"; an operative who got through may move on freely).
	 */
	FString ExitEvent;

	bool IsSet() const { return !Volume.IsNone(); }
	bool HasExit() const { return !ExitStance.IsNone() || !ExitEvent.IsEmpty(); }
};

/** Why a move order is refused by a confinement (TutorialRules::JudgeConfinedMove). */
enum class ETutorialConfineVerdict : uint8
{
	Allowed,
	/** The destination lies outside the allowed area ("Hold here - follow the training."). */
	Outside,
	/** The ordered operative is not in the stance the confinement asks for ("Crouch first - stay low."). */
	Stance,
	/** Outside, and not in the exit stance ("Go prone first, then crawl under."). */
	ExitStance,
	/** Outside, and another operative's turn ("One at a time - wait until he is through."). */
	NotYourTurn
};

/** The confinement in force now (FTutorialStateMachine::GetActiveConfine; the runtime adds the geometry). */
struct CODEXTACTICS_API FTutorialConfineView
{
	FName Owner;
	FTutorialConfine Spec;
	/** The holding volume + the allowed volumes open now. */
	TArray<FName> Areas;
	/** The operative whose exit turn it is ("one_at_a_time"), none = free. */
	FName ExitHolder;
	/** Operatives who got through (the exit event named them). */
	TArray<FName> Passed;
};

/** A confinement as the save holds it ("confines": [{"step", "released", "holder", "passed"}]). */
struct CODEXTACTICS_API FTutorialConfineProgress
{
	FName Owner;
	bool bReleased = false;
	FName ExitHolder;
	TArray<FName> Passed;
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
	/**
	 * "focus": "coldest_operative" -> the card names / marks the coldest squad member ({focus} token in the text); "wounded_operative"
	 * (rev 2026-10-19) -> the living member with the lowest health share, chosen when the step is triggered.
	 */
	FName Focus;
	/**
	 * "precredit" (default true; user decision 2026-10-13): a hint card completed by COMMANDS whose completion the player's
	 * command history (counts / hold time since the level started, FTutorialStateMachine) already satisfies when the step
	 * becomes due is completed at once without showing the card (a brief feed tick instead). False for lessons that must be
	 * done at that moment (a location, a specific target). Event / volume completions are never precredited.
	 */
	bool bPrecredit = true;
	/** "cancel_on" (rev 5e): when the step stops being relevant (FTutorialCancelOn). */
	FTutorialCancelOn CancelOn;
	/** "confine" (rev 5i): keeps the squad in an area while the lesson runs (FTutorialConfine). */
	FTutorialConfine Confine;
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

/**
 * Rev 5f (save round trip): how far one step got - a running step's counted uses / hold time / counted operatives, and who counted
 * for it ("distinct_from" of other steps; kept for finished steps too).
 */
struct CODEXTACTICS_API FTutorialStepProgress
{
	int32 Progress = 0;
	float HeldSeconds = 0.f;
	TArray<FName> Subjects;
	TArray<FName> Contributors;
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
	/**
	 * Rev 5f ("runtime": {id: {progress, held_s, subjects, contributors}}): before rev 5f a load restarted every running step from 0
	 * (e.g. two of three across the bridge -> the objective waited for crossings that never came) and forgot who did which part.
	 */
	TMap<FName, FTutorialStepProgress> StepProgress;
	/** Rev 5i: the confinements started so far (FTutorialConfineProgress; a released one stays released after a load). */
	TArray<FTutorialConfineProgress> Confines;

	bool IsEmpty() const { return Finished.IsEmpty() && Pending.IsEmpty() && CurrentBeat.IsNone() && Locks.IsEmpty() && History.IsEmpty(); }
};
