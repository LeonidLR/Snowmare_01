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
	/** Dropped by its "cancel_on" (rev 5e): a running speech keeps only the line on screen, nothing is played later. */
	bool bCancelled = false;
	/**
	 * Rev 5f (monotonic route): finished because the squad arrived at a LATER place first - a volume-triggered step that never
	 * started (skipped, its unlocks apply, its after_step followers fire) or a "go to X" step whose X lies behind.
	 */
	bool bPassed = false;
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
 * after_step cards that would follow them are dropped (precredited or skipped, DropEarlierBeatCard); a shown radio /
 * walk-and-talk and the interaction objectives of earlier beats go on.
 * Rev 5f (game designer 2026-10-14: the barrel lesson never started when the squad came to the camp another way): progress is
 * MONOTONIC BY LOCATION. Every tutorial volume has a place on the route (GetVolumeRoutePoint: the file order of the steps that
 * use it, or the start of the beat a volume actor starts - RegisterVolumeBeat). Entering a volume (ApplyArrival) starts its
 * beat when that beat is later than the current one, then passes everything that lies BEHIND it on the route: once-steps
 * triggered by an earlier volume that never started are finished as Skipped without showing (bPassed: unlocks apply,
 * after_step followers fire, so nothing waits for a place the squad walked past), running steps that complete at an earlier
 * volume ("go to X") are completed (objectives) or dropped (cards). A new beat also passes the earlier beats' volume steps,
 * completes their "go to" objectives, drops their waiting walk-and-talks and every after_step follower that would start later;
 * earlier interaction objectives (event / condition, e.g. share the heat packs) keep running in parallel - they block nothing.
 * UTutorialSubsystem feeds it commands, events, volumes and time, and drains ConsumeTransitions().
 */
class CODEXTACTICS_API FTutorialStateMachine
{
public:
	/**
	 * Seconds between two hint cards (UX brief 03 §2.3: confirm, then a breather). 2026-10-20 (game designer: "I keep waiting for the next
	 * instruction"): 0.8 s = the confirm flash, so the next card follows the action within ~1 s (was 1.5 s).
	 */
	float CardBreatherSeconds = 0.8f;
	int32 MaxObjectives = 3;
	/** At most one failure_retry (remedial) step per this many seconds. */
	float RemedialCooldownSeconds = 60.f;

	/**
	 * Current value of a condition stat ("squad_cold_max", ...; TutorialRules::IsKnownStat). Set by the runtime
	 * (UTutorialSubsystem: the squad's cold / heat packs) or by tests; unset = every stat reads 0.
	 */
	TFunction<float(FName)> StatProvider;
	/**
	 * The focus operative's id of a step ("focus"; "complete.subject": "focus" counts only his events). Set by the runtime
	 * (UTutorialSubsystem) or by tests; unset / NAME_None = no focus chosen (any subject counts).
	 */
	TFunction<FName(FName StepId)> FocusSubjectProvider;
	/**
	 * "role_spots" completions (2026-10-19): the subject stands at his role's spot of the step (UTutorialSubsystem: a marker of that
	 * step with his role, within its ring). Unset = every subject is at his spot.
	 */
	TFunction<bool(FName StepId, FName SubjectId)> RoleSpotProvider;
	/**
	 * The current navigation target (2026-10-20): the level has tutorial markers for this step (ATutorialMarkerActor::StepId). Set by
	 * the runtime (UTutorialSubsystem) or by tests; unset = no step has markers.
	 */
	TFunction<bool(FName StepId)> StepMarkerProvider;
	/** The level has a tutorial volume with this id (a guide target needs one). Unset = every volume exists. */
	TFunction<bool(FName VolumeId)> VolumeProvider;
	/** The runtime can show the spot "@<name>" for this step (ETutorialGuideKind::Spot). Unset = no spot can be shown. */
	TFunction<bool(FName Spot, FName StepId)> SpotProvider;
	/**
	 * 2026-10-21 (the A4 ambush, one zombie per part): does the event subject SubjectId pass an "enemy" filter (FTutorialEnemyFilter: the
	 * subject is an enemy of that binding, a living operative within "within_m", at least "off_route_m" off its route)? Set by the runtime
	 * (UTutorialSubsystem: TutorialRules::PassesEnemyFilter on the subject actor) or by tests. Unset: a filtered event never counts.
	 */
	TFunction<bool(const FTutorialEnemyFilter& Filter, FName SubjectId)> EnemyFilterProvider;

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
	/**
	 * A gameplay event; SubjectId (the operative's id) feeds "distinct_operatives" / "distinct_from" completions; SubjectVolumes =
	 * the ids of the tutorial volumes the subject stands in now ("in_volume" completions; the runtime fills it).
	 */
	void HandleEvent(FName EventName, FName SubjectId = NAME_None, const TArray<FName>& SubjectVolumes = TArray<FName>());
	/**
	 * A squad member (SubjectId) entered a tutorial volume: VolumeId for "volume" triggers / completions, StepId / BeatId start them
	 * directly.
	 */
	void HandleVolume(FName VolumeId, FName StepId, FName BeatId, FName SubjectId = NAME_None);
	/**
	 * A squad member already STANDS in the volume when a step that completes there starts running (queued / shown): counts for the
	 * "volume" completions of the running steps only (no triggers, no beat start) - the third operative who walked into the hut
	 * before the "go in" card came still completes it. Returns true when a step counted it.
	 */
	bool HandleVolumePresence(FName VolumeId, FName SubjectId);
	/**
	 * "role_spots" (2026-10-19): SubjectId already stands at a spot (in cover - the runtime checks that) while the role-spot steps run:
	 * counts for each running "role_spots" step whose RoleSpotProvider accepts him, with that step's filters (distinct operatives,
	 * distinct_from, confinement) - never for other steps listening to the same event. True when a step counted him.
	 */
	bool HandleRoleSpotPresence(FName SubjectId);
	/** Operatives who counted for a step's completion so far (distinct_from; runtime only). */
	const TSet<FName>* GetContributors(FName StepId) const;

	/** A place on the tutorial route (rev 5f): Key orders the places (larger = later), BeatIndex = the beat the place belongs to. */
	struct FRoutePoint
	{
		int32 Key = INDEX_NONE;
		int32 BeatIndex = INDEX_NONE;
		bool IsValid() const { return Key != INDEX_NONE; }
	};
	/**
	 * Rev 5f: a volume actor in the level starts BeatId (ATutorialTriggerVolume::BeatId): its place on the route is the start of
	 * that beat. The runtime registers every volume of the world when the steps load; HandleVolume registers the entered one too.
	 * Kept across Load (world data).
	 */
	void RegisterVolumeBeat(FName VolumeId, FName BeatId);
	/**
	 * Where VolumeId lies on the route: the earliest step that uses it (a volume trigger before that step's completion; a
	 * completion of a step started at its beat start - "go to the camp" - counts as the END of that beat), or the start of the
	 * registered beat, whichever is later. Invalid for a volume no step uses and no beat starts.
	 */
	FRoutePoint GetVolumeRoutePoint(FName VolumeId) const;
	/**
	 * 2026-10-20: the place of a step on the route - the first of its "guide" volume, "complete.volume", "complete.in_volume" and
	 * trigger volume that lies on the route. Invalid for a step with no place.
	 */
	FRoutePoint GetStepPlace(FName StepId) const;
	/**
	 * 2026-10-20: how far the squad got on the route - the furthest place it reached (an arrival, or a step passed by its "beyond"),
	 * at least the start of the current beat. INDEX_NONE before any beat.
	 */
	int32 GetRouteProgressKey() const;
	/** The volume of that furthest place (none = only the beat's start). Saved ("route"). */
	FName GetRouteProgressVolume() const { return RouteProgressVolume; }
	/**
	 * THE current navigation target (2026-10-20, game designer: "after the hut lesson nothing showed where to go next"; "a marker of a
	 * place I had skipped called me back"): exactly one, derived from the data each time it is asked:
	 * 1. the shown hint card (the lesson in hand): its "guide", else its markers, else the volume it completes in;
	 * 2. else the shown objectives / talks with a "guide" or markers whose place is NOT behind the squad - the nearest place first;
	 * 3. else the route: the next place ahead (the smallest route key beyond the progress) that the level has and some unfinished step
	 *    or a later beat still uses - the next beacon / the next beat's start volume;
	 * 4. else the place reached last (the data names nothing further: hold there).
	 * "guide": "none" makes a step name nothing. None before the steps load / without any place.
	 */
	FTutorialGuide GetGuide() const;
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
	 * One command of the player's history since the steps were loaded (every HandleCommand / HandleCommandHold, whether or not a
	 * step counted it): "precredit" completes a due command card the history already satisfies (TutorialRules::IsPrecreditSatisfied).
	 */
	FTutorialCommandUse GetCommandUse(FName CommandId) const;

	// --- Confine (rev 5i, game designer 2026-10-15) --------------------------------------------------------------------
	/**
	 * A confinement is in force (a step with "confine" was triggered and its "until" step has not finished; FTutorialConfine). While
	 * it holds, the route does not move (an arrival at another volume neither starts a beat nor passes steps) and a step that
	 * completes in one of its "allow" areas counts only while it is SHOWN (the lesson's order: right of the door only after left).
	 */
	bool IsConfined() const;
	/** The confinement in force: its spec, the areas open now, who got through; false when none. */
	bool GetActiveConfine(FTutorialConfineView& Out) const;
	/** Ends the confinement in force (the tutorial was switched off / skipped, a fight started). False when none held. */
	bool ReleaseConfines();
	/** Changes whenever a confinement starts or ends (the runtime stops the squad when one starts). */
	int32 GetConfineSerial() const { return ConfineSerial; }

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
		/** Every subject whose event / volume entry counted for this step (distinct_from of other steps). */
		TSet<FName> Contributors;
		/** The step's lock / unlock were applied (once per trigger). */
		bool bLocksApplied = false;
	};

	int32 IndexOf(FName StepId) const;
	bool CanTrigger(int32 Index) const;
	/** The step is a hint card of a beat before the current one (the story has moved on past it). */
	bool IsEarlierBeatCard(int32 Index) const;
	/** The step belongs to a beat before the current one (any kind). */
	bool IsEarlierBeatStep(int32 Index) const;
	/** Rev 5f: a follower of an earlier beat that became due after the story moved on - cards as DropEarlierBeatCard, others skipped. */
	void DropEarlierBeatStep(int32 Index);
	/** Rev 5f: an Idle once-step is finished as Skipped without ever starting (bPassed); its locks apply, its followers fire. */
	void PassStep(int32 Index);
	/** A volume-triggered once-step that never started: what the route passes when the squad is already beyond its place. */
	bool IsPassableVolumeStep(int32 Index) const;
	/**
	 * Finish + marks the step's own finishing transition with Flag (bPassed / bPrecredited / bCancelled) - the one way every
	 * "finished because ..." reason reaches the runtime.
	 */
	void FinishFlagged(int32 Index, ETutorialStepState FinalState, bool FTutorialTransition::* Flag);
	/** Rev 5f: the squad arrived at VolumeId (actor beat BeatId): starts a later beat, passes what lies behind on the route. */
	void ApplyArrival(FName VolumeId, FName BeatId);
	/** Rev 5f: passes every step behind Point (see the class comment). */
	void PassRouteBefore(const FRoutePoint& Point);
	/** Rev 5f: route keys of the steps' volumes (Load). */
	void RebuildRoute();
	/** 2026-10-20: GetStepPlace by index; OutVolume = the volume of that place. */
	FRoutePoint GetStepPlaceAt(int32 Index, FName* OutVolume = nullptr) const;
	/** 2026-10-20: the squad reached Point (VolumeId may be none: a beat start): the route progress moves forward, never back. */
	void NoteRouteProgress(const FRoutePoint& Point, FName VolumeId);
	/** 2026-10-20: the step's "guide" target for GetGuide (bInHand: the shown card - its completion volume counts, nothing is "behind"). */
	bool GetStepGuide(int32 Index, bool bInHand, int32 Progress, FTutorialGuide& Out) const;
	/** 2026-10-20: some step that is not finished uses VolumeId (trigger / completion / in_volume / guide), or it starts a later beat. */
	bool IsVolumeStillUsed(FName VolumeId) const;
	bool HasVolume(FName VolumeId) const;
	/** 2026-10-20: "beyond" - every unfinished step of the current / an earlier beat whose clauses hold is passed (Tick). */
	void PollBeyond();
	/** The route point of the start of BeatIndex (before its first step). */
	FRoutePoint GetBeatStartPoint(int32 BeatIndex) const;
	/**
	 * Drops a leftover hint card of an earlier beat without showing it any more: completed when the player's command history
	 * already satisfies it ("precredit"), else skipped. Its followers fire as usual (a chain of leftovers drops in one go).
	 */
	void DropEarlierBeatCard(int32 Index);
	/** Completes the step and marks its transition as precredited (the player had already done the lesson). */
	void FinishPrecredited(int32 Index);
	/** Rev 5e "cancel_on": a step named in the step's cancel list has finished already. */
	bool IsCancelDue(int32 Index) const;
	/** Rev 5e: drops a running step as Skipped and marks its transition bCancelled (FTutorialCancelOn). */
	void CancelStep(int32 Index);
	/** Rev 5e: cancels every running step whose "cancel_on" matches (a finished step / an event / a volume entry). */
	void CancelMatching(FName FinishedStep, FName EventName, FName VolumeId, FName SubjectId = NAME_None);
	/** An event of SubjectId passes Filter (no filter = always; EnemyFilterProvider unset = never). */
	bool PassesEnemyFilter(const FTutorialEnemyFilter& Filter, FName SubjectId) const;
	/** Every step of an after_step trigger's "step" list has finished at least once (FTutorialTrigger::AfterSteps). */
	bool AreAfterStepsFinished(int32 Index) const;
	/** Every "trigger.requires" step has been triggered at least once. */
	bool IsArmed(int32 Index) const;
	/** SubjectId may count for the step's completion under "distinct_from" (it counted for none of those steps). */
	bool IsSubjectAllowed(int32 Index, FName SubjectId) const;
	/** Counts a volume entry / presence of SubjectId for every running step completing in VolumeId; true when one counted. */
	bool CountVolumeCompletions(FName VolumeId, FName SubjectId, const TArray<bool>& WasRunning);
	/**
	 * The subject filters every event-style count passes (an event, a role-spot presence): "role_spots", the confinement order,
	 * "distinct_from", "distinct_operatives"; counts it (CountContribution) when they all pass. "in_volume" and "subject": "focus"
	 * are event-only filters checked by HandleEvent before.
	 */
	bool TryCountEventSubject(int32 Index, FName SubjectId);
	/** SubjectId (if any) becomes a contributor of the step and its progress grows by one towards Needed. */
	void CountContribution(int32 Index, FName SubjectId, int32 Needed);
	float ReadStat(FName Stat) const;
	bool AreConditionsMet(int32 Index) const;
	/** The step's "complete.condition" clauses exist and all hold now (rev 5d). */
	bool IsCompletionConditionMet(int32 Index) const;
	/** Completes every queued / shown step whose "complete.condition" holds now (rev 5d; Tick). */
	void PollConditionCompletions();
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
	/**
	 * Rev 5g (game designer 2026-10-14, "waiting for instructions" on arrival): a card that is CANCELLED (cancel_on) or DROPPED
	 * because the squad moved on to a later beat arms no breather - the arrival's own card shows at once.
	 */
	bool bNoBreatherOnFinish = false;
	float RemedialCooldownLeft = 0.f;
	TSet<FName> UsedCommands;
	TArray<FTutorialTransition> Transitions;
	TArray<FString> Locks;
	TMap<FName, FTutorialCommandUse> History;
	/** Rev 5f: the place of each volume the steps use (RebuildRoute) and the beats volume actors start (RegisterVolumeBeat). */
	TMap<FName, FRoutePoint> StepRoute;
	TMap<FName, FName> VolumeBeats;
	/** 2026-10-20: the furthest route place reached (GetRouteProgressKey) and its volume (saved as "route"). */
	int32 RouteProgressKey = INDEX_NONE;
	FName RouteProgressVolume;

	/** Rev 5i: a started confinement (FTutorialConfine of the step Owner). At most one is not released. */
	struct FConfineRuntime
	{
		int32 Owner = INDEX_NONE;
		bool bReleased = false;
		TArray<FName> Passed;
	};
	TArray<FConfineRuntime> Confines;
	int32 ConfineSerial = 0;
	/** Index into Confines of the confinement in force, INDEX_NONE when none. */
	int32 FindActiveConfine() const;
	/** The step that ends the confinement of Owner ("until", else Owner itself). */
	int32 GetConfineUntilIndex(int32 Owner) const;
	/** An "allow" area is open: no step completes there, or one that does is shown / finished. */
	bool IsConfineAreaOpen(FName VolumeId) const;
	/** The step completes in an "allow" area of the confinement in force and is not shown: its completion waits (lesson order). */
	bool IsCompletionHeldByConfine(int32 Index) const;
	/** The step was triggered: its "confine" starts (replacing any other). */
	void StartConfine(int32 Index);
	/** Releases the confinement whose "until" step just finished. */
	void ReleaseConfinesUntil(int32 FinishedIndex);
	/** "exit.through": the event named SubjectId - he got through (free to move on). */
	void NoteConfineEvent(FName EventName, FName SubjectId);
};
