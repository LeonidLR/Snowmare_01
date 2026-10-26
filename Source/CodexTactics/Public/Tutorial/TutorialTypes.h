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
 * between squad mates since the level started; rev 5d), "squad_prone" (living operatives lying prone) and "enemies_unseen" (living
 * enemies no operative sees now - UTacticalSightSubsystem::IsVisibleToSquad; 0 while the squad's sight is off; both 2026-10-19 b).
 * All clauses of a step must hold (AND).
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
	/**
	 * "cold_bump": every squad member's cold + Amount, raised to at least MinLevel (UColdSurvivalComponent::AddColdShock).
	 * "set_posture" (2026-10-19): the whole squad's fire posture becomes Posture (USquadSubsystem::SetSquadPosture, overrides cleared).
	 * "tactical_pause" (2026-10-21: the fight's free tactical pause) is RETIRED since the user decision 2026-10-26 ("the tactical pause only
	 * by Space"): it loads with a warning and is dropped (TutorialRules::IsRetiredAction).
	 */
	FName Type;
	/** "posture" of a set_posture action: "passive" | "defensive" | "aggressive" (FirePostureRules::ParseName). */
	FString Posture;
	float Amount = 0.f;
	/** "min_level": cold_bump never leaves anyone below this (0 = no floor). */
	float MinLevel = 0.f;
};

/**
 * An enemy filter on the event of a trigger / completion / cancel_on (2026-10-21, the A4 ambush split into two parts, each bound to ITS
 * zombie): JSON keys "enemy" (an id of the file's "enemies" bindings), "within_m" and "off_route_m" next to the "event" they filter.
 * The event counts only when its subject is an enemy of that binding (FTutorialStateMachine::EnemyFilterProvider; the runtime resolves
 * the subject actor and the binding's selectors) and, when set, a living operative stood within WithinM metres of it and it stood at
 * least OffRouteM metres from its own patrol route (lured off its beat into the open) at that moment.
 */
struct CODEXTACTICS_API FTutorialEnemyFilter
{
	/** The "enemies" binding id; none = no enemy filter (the other two keys need it). */
	FName Enemy;
	/** "within_m" > 0: the nearest living operative stood at most this many metres from the enemy (a close-range shot). */
	float WithinMetres = 0.f;
	/** "off_route_m" > 0: the enemy stood at least this many metres (2D) from its patrol route (an enemy without a route passes). */
	float OffRouteMetres = 0.f;

	bool IsSet() const { return !Enemy.IsNone(); }
};

/**
 * One "enemies" binding of a steps file (2026-10-21): the level enemy a lesson is about, found WITHOUT changing the map. An enemy belongs to
 * the binding when any selector matches: "route" = its patrol route actor (AEnemyCharacter::AssignedPatrolRoute) by actor label (editor
 * builds), object name or actor tag; "actor" = the enemy itself by the same three. JSON:
 *   "enemies": [ { "id": "A4_PERIMETER", "route": ["PatrolRoute_A4_Hounds", "PatrolRouteActor_1"] } ]
 */
/**
 * "dormant" of an enemy binding (2026-10-23, game designer: the A4 zombie #1 spotted the squad while it ran to the overlook cover): the
 * bound enemies do not exist for the game from the level start - hidden, no collision, no AI, no senses, parked out of the world - until
 * the "until" clauses hold (e.g. the whole squad in cover at the overlook); then they are woken at "spawn": "hidden_on_route" = the point
 * of their own patrol route hidden from every living operative (sight traces from each one's eye for his stance) and farthest along the
 * route from them (none hidden: the farthest), patrolling on from there; "in_place" = where they were placed. Woken: the gameplay events
 * "EnemyWoke.<binding>" and "EnemyWoke" (subject = the enemy). Tutorial off / SKIP TUTORIAL: woken in place at once.
 * The runtime keeps the state and the spawn point in the save ("dormant_enemies"). JSON:
 *   "dormant": { "until": [ { "stat": "squad_not_in_cover_at.VOL_A4_OVERLOOK", "below": 1 } ], "spawn": "hidden_on_route" }
 */
struct CODEXTACTICS_API FTutorialDormant
{
	bool bEnabled = false;
	TArray<FTutorialCondition> Until;
	/** "hidden_on_route" (default) | "in_place". */
	FName Spawn;

	bool IsSet() const { return bEnabled; }
	bool SpawnsHidden() const { return Spawn != FName(TEXT("in_place")); }
};

struct CODEXTACTICS_API FTutorialEnemyBinding
{
	FName Id;
	TArray<FString> Routes;
	TArray<FString> Actors;
	/** 2026-10-23: dormant until a condition holds (FTutorialDormant). */
	FTutorialDormant Dormant;
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
	/** "enemy" / "within_m" / "off_route_m" (2026-10-21): only events of that enemy count as hits ("event" / "failure_retry" triggers). */
	FTutorialEnemyFilter EnemyFilter;
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
	/**
	 * "role_spots": true (game designer 2026-10-19, the A3 cover lesson: "each role has its own spot"): an event counts only when its
	 * subject stands at HIS role's spot of this step - a tutorial marker with this step's id and his role (ATutorialMarkerActor::RoleId,
	 * FTutorialStateMachine::RoleSpotProvider). Also counted while he already stands there (in cover) when the step runs
	 * (HandleRoleSpotPresence). A level without such markers: any place counts (no soft-lock on an older map).
	 */
	bool bRoleSpots = false;
	/** "enemy" / "within_m" / "off_route_m" (2026-10-21): an event counts only when its subject is that enemy (FTutorialEnemyFilter). */
	FTutorialEnemyFilter EnemyFilter;
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
	/** "enemy" / "within_m" / "off_route_m" next to "event" (2026-10-21): only that enemy's event cancels. */
	FTutorialEnemyFilter EnemyFilter;
	/**
	 * "condition" (2026-10-22, game designer: "both A4 zombies are dead, yet the wire card still asks for the wire"): world-state clauses
	 * (the step "condition" syntax, AND) polled while the step runs - when all hold it is cancelled; already true when it becomes due:
	 * cancelled at once (e.g. {"stat": "all_dead.A4_PERIMETER", "at_least": 1} = the zombie the lesson is about is dead, killed any way).
	 */
	TArray<FTutorialCondition> Conditions;

	bool IsSet() const { return Steps.Num() > 0 || !Event.IsEmpty() || Volumes.Num() > 0 || Conditions.Num() > 0; }
};

/**
 * "confine" (rev 5i, game designer 2026-10-15: "the player just runs everyone into the hut and skips the tactical entry"): while the
 * confinement holds, the squad is kept in an area - every move order (player clicks, cover orders, walk-ups, autonomy) whose
 * destination or path end lies outside the allowed area is refused with a short feed line, and formation moves are pulled back
 * into the holding volume. Starts when the step is triggered (a volume step: the moment the squad arrives), ends when the step named
 * by "until" (default: the step itself) finishes, when a later beat starts, or when the tutorial is switched off / skipped / a fight
 * starts (UTutorialSubsystem). One confinement at a time (a new one replaces the old). JSON:
 *   "confine": { "volume": "VOL_A1_HUT_APPROACH", "allow": ["VOL_A1_DOOR_L", ...], "until": "A1_hut_enter", "stop": true,
 *                "span": true, "stance": "crouch", "exit": { "stance": "prone", "through": "CrawlGapPassed" } }
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
	/** "exit.stance": an order OUTSIDE the allowed area is allowed for an operative in this stance - each at his own pace, any order. */
	FName ExitStance;
	/**
	 * "exit.through": this gameplay event names an operative who got through (e.g. "CrawlGapPassed") - he moves on freely, any stance.
	 * (2026-10-20, game designer: the former "one_at_a_time" queue is gone - several may crawl under the fence at once.)
	 */
	FString ThroughEvent;
	/**
	 * "free": "focus" (2026-10-20, the A4 ambush by ONE operative): the confining step's focus operative is not held at all - he moves
	 * anywhere, the others stay in the area ("Hold here - <Name> handles it.").
	 */
	bool bFreeFocus = false;
	/**
	 * "free": "first" (2026-10-23, game designer: "any ONE operative, in Solo, the other two stay in cover"): the first operative ordered
	 * outside the area becomes the free one (FTutorialConfineView::FreeOperative, saved); after that the others are held like "focus".
	 */
	bool bFreeFirst = false;

	bool IsSet() const { return !Volume.IsNone(); }
	bool HasExit() const { return !ExitStance.IsNone() || !ThroughEvent.IsEmpty(); }
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
	/** Outside while the lesson belongs to another operative ("free": "focus"; "Hold here - <Name> handles it."). */
	HeldBack,
	/**
	 * 2026-10-23 ("lock_in_place", FTutorialPlaceLock): the ordered operative was locked at the spot his part of the lesson put him on
	 * ("Hold this position - stack on the door.") - any order that would move him away is refused, inside the area or not.
	 */
	LockedInPlace
};

/** The confinement in force now (FTutorialStateMachine::GetActiveConfine; the runtime adds the geometry). */
struct CODEXTACTICS_API FTutorialConfineView
{
	FName Owner;
	FTutorialConfine Spec;
	/** The holding volume + the allowed volumes open now. */
	TArray<FName> Areas;
	/** Operatives who got through (the "exit.through" event named them). */
	TArray<FName> Passed;
	/** "free": "focus" - the operative who is not held (the owner step's focus; none = not chosen yet / not free). */
	FName FreeOperative;
};

/** A confinement as the save holds it ("confines": [{"step", "released", "passed"}]; an older save's "holder" is ignored). */
struct CODEXTACTICS_API FTutorialConfineProgress
{
	FName Owner;
	bool bReleased = false;
	TArray<FName> Passed;
	/** 2026-10-23 ("free": "first"): the operative who took the lesson ("free"). */
	FName Free;
};

/**
 * "lock_in_place" (2026-10-23, game designer play-test: "I place one at the hut wall - he's fixed; the second - fixed; otherwise a stray
 * click moves the one I already put there back the other way"): when the step COMPLETES, every operative who counted for it (its
 * contributors: the subject of the completing event / volume entry) is locked where he stands - move / cover / shimmy / wrap / regroup
 * orders for him are refused with one feed line (TextId, else "Hold this position."), formation moves leave him in place. Selecting him
 * still works. Released when the "until" step finishes (default: the "until" step of the confinement in force when he was locked; no
 * confinement: when a later beat starts), when a later beat starts, and on tutorial OFF / SKIP TUTORIAL / a fight (UnlockAll). JSON:
 *   "lock_in_place": true    |    "lock_in_place": { "until": "A1_hut_enter", "text_id": "TUT_A1_HUT_HOLD" }
 */
struct CODEXTACTICS_API FTutorialPlaceLock
{
	bool bEnabled = false;
	/** The step whose finish releases the lock; none = the confinement's "until" at lock time, else the next beat. */
	FName Until;
	/** The feed line a refused order shows (narrative manifest / draft texts); none = the generic "Hold this position.". */
	FName TextId;

	bool IsSet() const { return bEnabled; }
};

/** One operative held in place by a "lock_in_place" step (FTutorialStateMachine::GetPlaceLocks; saved as "place_locks"). */
struct CODEXTACTICS_API FTutorialPlaceLockState
{
	/** The operative's id (his object name, as the event subjects / contributors). */
	FName Operative;
	/** The step that locked him. */
	FName Step;
	/** The step whose finish releases him; none = the next beat. */
	FName Until;

	bool operator==(const FTutorialPlaceLockState& Other) const { return Operative == Other.Operative && Step == Other.Step && Until == Other.Until; }
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
	/** "lock_in_place" (2026-10-23): the operatives who complete this step are held where they stand (FTutorialPlaceLock). */
	FTutorialPlaceLock LockInPlace;
	/**
	 * "guide" (2026-10-20, game designer: "after the hut nothing showed where to go next"): where this step sends the squad while it
	 * is shown - a tutorial volume id (the target is that volume's box on the ground) or a step id (that step's markers). Overrides the
	 * automatic choice of the current navigation target (FTutorialStateMachine::GetGuide). None = automatic (own markers, the place
	 * the step completes in, the route).
	 */
	FName Guide;
	/** "guide": "none" - this step never names the current target (its markers still show; the next rule decides). */
	bool bNoGuide = false;
	/**
	 * "beyond" (2026-10-20, the bridge skipped): world-state clauses (the "condition" syntax) meaning the squad is already BEYOND this
	 * step's place - e.g. {"stat": "squad_not_across.VOL_A1_BEACON2", "below": 1} = all three are across the crevasse. Polled every
	 * tick while the step is not finished (also never triggered), from its beat on: when they hold, the step is passed (an objective
	 * completed, any other kind skipped, never shown again) and the route moves past the step's place, so nothing there calls the
	 * squad back.
	 */
	TArray<FTutorialCondition> Beyond;
};

/** What the current navigation target ("guide") of the tutorial is (FTutorialStateMachine::GetGuide). */
enum class ETutorialGuideKind : uint8
{
	None,
	/** The level's markers of a step (ATutorialMarkerActor::StepId == Id; the runtime picks the nearest shown one). */
	StepMarkers,
	/** A tutorial volume (the runtime shows a guide beacon on the ground at its box). */
	Volume,
	/**
	 * A spot the runtime computes ("guide": "@<name>", 2026-10-20): "@patrol_route" = the point of the level's patrol routes nearest
	 * to the step's focus operative (where the A4 ambusher rigs the wire); 2026-10-21: "@patrol_route.<enemy>", "@lure_spot.<enemy>",
	 * "@enemy.<enemy>" (an "enemies" binding; TutorialRules::IsKnownGuideSpot). The guide beacon stands there.
	 */
	Spot
};

/** The current navigation target: exactly one at a time - the HUD edge arrow and the highlighted minimap icon point at it. */
struct CODEXTACTICS_API FTutorialGuide
{
	ETutorialGuideKind Kind = ETutorialGuideKind::None;
	/** The marker step id or the volume id. */
	FName Id;
	/** The step that asked for it; none = the route chose it (the next place ahead / the last place reached). */
	FName SourceStep;

	bool IsSet() const { return Kind != ETutorialGuideKind::None && !Id.IsNone(); }
	bool operator==(const FTutorialGuide& Other) const { return Kind == Other.Kind && Id == Other.Id && SourceStep == Other.SourceStep; }
	bool operator!=(const FTutorialGuide& Other) const { return !(*this == Other); }
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
	/** "enemies" (2026-10-21): the level enemies lessons are bound to (FTutorialEnemyBinding). */
	TArray<FTutorialEnemyBinding> Enemies;

	const FTutorialStep* FindStep(FName StepId) const;
	const FTutorialEnemyBinding* FindEnemy(FName BindingId) const;
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
	/**
	 * 2026-10-20 ("route"): the furthest place on the route the squad has reached (a tutorial volume id; none = only its beat's start).
	 * The current navigation target is derived from it, so it survives a load.
	 */
	FName RouteVolume;
	/** 2026-10-23 ("place_locks"): the operatives held in place by a "lock_in_place" step (FTutorialPlaceLockState). */
	TArray<FTutorialPlaceLockState> PlaceLocks;

	bool IsEmpty() const { return Finished.IsEmpty() && Pending.IsEmpty() && CurrentBeat.IsNone() && Locks.IsEmpty() && History.IsEmpty(); }
};
