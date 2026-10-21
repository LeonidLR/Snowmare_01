#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Tutorial/TutorialTypes.h"

class FJsonObject;

/** One line of Saved/Tutorial/tutorial.jsonl (testers' telemetry). */
struct CODEXTACTICS_API FTutorialTelemetryEntry
{
	/** "session_start", "beat_started", "shown", "completed", "completed_silently", "skipped", "timed_out", "lesson_downed", "retry", "enabled", "disabled". */
	FString Event;
	FName Step;
	FName Beat;
	/** Seconds the step was on screen (0 when not shown). */
	float ActiveSeconds = 0.f;
	/** Seconds since the world started. */
	float GameSeconds = 0.f;
	FString Map;
	FString Session;
	/** ISO 8601 UTC. */
	FString Timestamp;
};

/**
 * Pure rules of the Demo 01 tutorial framework (UX brief 03 §2, producer plan C8): the steps JSON contract, matching,
 * line timing, the lesson "downed instead of killed" rule, save and telemetry formats. UE-only (Godot had no tutorial).
 */
namespace TutorialRules
{
	/** "hint_card" | "objective" | "radio" | "walk_talk" | "feed". */
	CODEXTACTICS_API bool ParseKind(const FString& Text, ETutorialStepKind& OutKind);
	/** "manual" | "beat_start" | "volume" | "event" | "after_step" | "first_use" | "failure_retry" | "condition". */
	CODEXTACTICS_API bool ParseTriggerType(const FString& Text, ETutorialTriggerType& OutType);
	CODEXTACTICS_API const TCHAR* StateName(ETutorialStepState State);
	/** "completed" / "skipped" / "timed_out" -> the state; false for anything else. */
	CODEXTACTICS_API bool ParseFinishedState(const FString& Text, ETutorialStepState& OutState);

	/**
	 * Parses a steps JSON (the schema: docs/systems/tutorial-engine.md). Errors fail (invalid JSON, a step / beat without id, a
	 * duplicate id, an unknown kind / trigger type / stat / action / focus / subject / confine stance, a trigger without its
	 * required field, a step id reference - after_step, requires, distinct_from, cancel_on, confine until - naming no step; 2026-10-21: an
	 * "enemies" binding without id / selectors or duplicated, an "enemy" filter naming no binding);
	 * soft problems go to OutWarnings (an unknown key at any level, a beat not declared in "beats" is added, "blocking": true is
	 * forced off, a filter without the completion it filters, a hint card without completion gets timeout_s 12, ...).
	 */
	CODEXTACTICS_API bool ParseSteps(const FString& Json, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError);

	/**
	 * Cross-checks parsed steps against the game (warnings only): every command pattern ("complete.commands", "trigger.command",
	 * beat / step "lock" / "unlock") matches a registered player command (Core/PlayerCommands.h), a dotted key cap names a
	 * registered command, "subject": "focus" has a step "focus", and no after_step chain leads back to its own step.
	 */
	CODEXTACTICS_API void ValidateSteps(const FTutorialStepsData& Data, TArray<FString>& OutWarnings);

	/**
	 * Loads Content/Data/Tutorial/<FileName> (a bare name or a path relative to that folder): ParseSteps, then ValidateSteps
	 * (its findings are appended to OutWarnings).
	 */
	CODEXTACTICS_API bool LoadStepsFile(const FString& FileName, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError);
	CODEXTACTICS_API FString GetTutorialDirectory();

	CODEXTACTICS_API bool IsFinished(ETutorialStepState State);
	CODEXTACTICS_API bool IsRunning(ETutorialStepState State);

	/** Name pattern match for events (same syntax as PlayerCommands::Matches: exact, "*", "Prefix.*"). */
	CODEXTACTICS_API bool MatchesName(const FString& Pattern, FName Name);

	/** True when the step's completion counts this command (any pattern of "complete.commands"). */
	CODEXTACTICS_API bool CountsCommand(const FTutorialStep& Step, FName CommandId);

	/**
	 * True when a step's completion is satisfied: Progress reached Needed (at least 1) AND the accumulated hold time reached
	 * HoldSeconds ("complete.hold_s"; 0 = no hold needed).
	 */
	CODEXTACTICS_API bool IsProgressMet(int32 Progress, int32 Needed, float HeldSeconds, float HoldSeconds);

	/** Seconds a subtitle line stays: Override when > 0, else clamp(1.2 + 0.055 s per character, 2.5, 9). */
	CODEXTACTICS_API float LineDurationSeconds(const FString& Text, float Override = 0.f);

	/** "[EN missing: <TextId>]" (the dialogue placeholder style). */
	CODEXTACTICS_API FString MakeMissingText(FName TextId);

	/**
	 * Lesson beats (Demo 01 user decision 2026-10-11): while the tutorial is on and the current beat is a lesson, a lethal
	 * hit downs the operative instead of killing him (any operative, the commander too); outside lessons normal rules
	 * (permanent loss, commander-only defeat).
	 */
	CODEXTACTICS_API bool ShouldDownInsteadOfKill(bool bTutorialEnabled, bool bLessonBeatActive);

	/** Health a downed (not killed) operative keeps. */
	constexpr float LessonHealthFloor = 1.f;

	/**
	 * The progress as the save's "tutorial" object: {"beat", "enabled", "finished": {id: state}, "pending": [ids], "locks": [entries],
	 * "history": {command: {count, held_s}}, "runtime": {id: {progress, held_s, subjects, contributors}}, "confines": [{step, released,
	 * holder, passed}]} (UTutorialSubsystem adds "volumes_fired", "lock_hints", "skipped").
	 */
	CODEXTACTICS_API TSharedRef<FJsonObject> SaveProgress(const FTutorialProgress& Progress, bool bEnabled);
	/** Reads the save's "tutorial" object; false when it is not one. */
	CODEXTACTICS_API bool LoadProgress(const FJsonObject& Object, FTutorialProgress& OutProgress);

	/** One JSON object line (no newline) for tutorial.jsonl. */
	CODEXTACTICS_API FString BuildTelemetryLine(const FTutorialTelemetryEntry& Entry);

	/** Key caps of a step joined for the card: "[B]  [Alt+V]". */
	CODEXTACTICS_API FString FormatKeys(const TArray<FString>& Keys);

	// --- Conditions, command locks, actions (2026-10-12, Demo 01 A0-A1 redesign) ---------------------------------------

	/**
	 * Condition stats the runtime provides (UTutorialSubsystem::GetConditionStat): "squad_cold_max", "squad_cold_min", "leader_cold",
	 * "squad_heat_packs", "squad_min_heat_packs", "heat_packs_given" (rev 5d), "squad_solo" (rev 5i: 1 in Solo), "squad_injury_max",
	 * "squad_prone", "enemies_unseen" (2026-10-19), "traps_spotted.<tag>" (rev 5h), "squad_not_low_in.<volume>" (rev 5i).
	 */
	CODEXTACTICS_API bool IsKnownStat(FName Stat);

	/** Action types: "cold_bump", "set_posture". */
	CODEXTACTICS_API bool IsKnownAction(FName Type);

	/** Focus kinds: "coldest_operative", "wounded_operative". */
	CODEXTACTICS_API bool IsKnownFocus(FName Focus);

	/** All clauses hold (AND; an empty list holds). Stat returns the current value of a stat. */
	CODEXTACTICS_API bool IsConditionMet(const TArray<FTutorialCondition>& Conditions, TFunctionRef<float(FName)> Stat);

	/**
	 * Applies a step's / beat's lock changes to the entries in force: Unlock first, then Lock. Returns true when the set
	 * changed. Unlock removes every lock pattern it covers (PatternCovers; "*" removes all); when a wider lock still covers
	 * it, an exception entry "!<pattern>" frees just that part (Demo 01 progressive unlock, user request 2026-10-13: beat A0
	 * locks "Trap.*", the mine card frees "Trap.DisarmMine"). Lock adds a pattern and takes back the exceptions it covers.
	 * Exceptions no lock covers any more are dropped. Entries are saved as they are (the save's "locks").
	 */
	CODEXTACTICS_API bool ApplyLockChanges(TArray<FString>& InOutLocks, const TArray<FString>& Lock, const TArray<FString>& Unlock);

	/** True when a lock pattern matches the command (PlayerCommands::Matches) and no "!" exception entry frees it. */
	CODEXTACTICS_API bool IsCommandLocked(const TArray<FString>& Locks, FName CommandId);

	/**
	 * The command a handler carries out under the locks: CommandId itself when it is not locked; a locked command with a
	 * milder fallback degrades to it (a locked "Order.Sprint" walks: "Order.Move"); NAME_None = refused (no fallback).
	 */
	CODEXTACTICS_API FName ResolveLockedCommand(const TArray<FString>& Locks, FName CommandId);

	/** "Tutorial.Locked.<CommandId>": the specific event a locked command raises next to "Tutorial.Locked". */
	CODEXTACTICS_API FName MakeLockedEventName(FName CommandId);

	/**
	 * True when every command id Inner can match is also matched by Outer ("Trap.*" covers "Trap.DisarmMine" and
	 * "Trap.Disarm*"; "*" covers all; an exact id covers only itself). Case-insensitive.
	 */
	CODEXTACTICS_API bool PatternCovers(const FString& Outer, const FString& Inner);

	/** "Stance" for "Stance.Squad.Prone": the part before the first '.' (the whole id without one); the lock hint is per family. */
	CODEXTACTICS_API FString GetCommandFamily(FName CommandId);

	// --- Precredit (user decision 2026-10-13: a lesson already done before its card appears counts as done) ---------------

	/**
	 * A step that may be precredited: a once-only hint card with "precredit" on whose completion is by commands only (no event /
	 * volume). Remedial / repeatable cards ("once": false) never are.
	 */
	CODEXTACTICS_API bool CanPrecredit(const FTutorialStep& Step);

	/** Uses the history already holds for the step's commands (partial credit seeds the card's progress when it fires). */
	CODEXTACTICS_API int32 CountHistoryUses(const FTutorialStep& Step, const TMap<FName, FTutorialCommandUse>& History);

	/**
	 * The player's command history already satisfies the step's completion: the uses of every command its "complete.commands"
	 * patterns match sum to at least Count, and their hold time to at least "hold_s". False when CanPrecredit is false.
	 */
	CODEXTACTICS_API bool IsPrecreditSatisfied(const FTutorialStep& Step, const TMap<FName, FTutorialCommandUse>& History);

	// --- Confine (rev 5i, game designer 2026-10-15: hold the squad in an area until the lesson is done) ---------------------

	/**
	 * Rev 5i: "squad_not_low_in.<VolumeId>" -> VolumeId (else NAME_None): the condition stat counting the living squad members that are
	 * NOT crouched / prone inside that tutorial volume ({"stat": "squad_not_low_in.VOL_A3_RIDGE", "below": 1} = everyone is down there).
	 */
	CODEXTACTICS_API FName ParseNotLowInStat(FName Stat);

	/**
	 * 2026-10-20: "squad_not_across.<VolumeId>" -> VolumeId (else NAME_None): the condition stat counting the living squad members NOT
	 * yet across the bridge nearest to that volume (ABridgeCrossingVolume, BridgeRules::IsAcrossFrom: still on the volume's side or on
	 * the span); {"stat": "squad_not_across.VOL_A1_BEACON2", "below": 1} = the whole squad is on the far side, however it got there.
	 * A level without a bridge: every living member counts as not across.
	 */
	CODEXTACTICS_API FName ParseNotAcrossStat(FName Stat);

	// --- The current navigation target (2026-10-20) ---------------------------------------------------------------------

	/**
	 * The HUD edge arrow towards an off-screen target: Projected = the target's screen point (widget units) in a ViewSize view, bProjected
	 * = it lies in front of the camera (behind: the projection is mirrored, the arrow points the other way). True when the target is
	 * off-screen (outside the view inset by Margin): OutTip = the arrow tip on the inset rectangle, OutDirection = unit vector from the
	 * view centre. False when on-screen (no arrow) or the view is too small.
	 */
	CODEXTACTICS_API bool ComputeEdgePointer(const FVector2D& ViewSize, const FVector2D& Projected, bool bProjected, float Margin, FVector2D& OutTip,
		FVector2D& OutDirection);

	/**
	 * The perspective projection the edge arrow uses (the camera's view, X forward / Y right / Z up, HorizontalFovDegrees across
	 * ViewSize.X - the same with or without a real viewport, so headless checks see what the player sees): OutScreen = World's point in
	 * the view (widget units). A point behind the camera (or on its plane) gets a point far off-screen in its sideways direction, so
	 * ComputeEdgePointer (bProjected = true) points the arrow towards the side it lies on. Returns true when World is in front.
	 */
	CODEXTACTICS_API bool ProjectToView(const FVector& CameraLocation, const FRotator& CameraRotation, float HorizontalFovDegrees, const FVector2D& ViewSize,
		const FVector& World, FVector2D& OutScreen);

	/**
	 * The runtime spots a "guide": "@<name>" may name: "@patrol_route" (2026-10-20: the level's routes), and since 2026-10-21 the
	 * enemy-bound spots "@patrol_route.<enemy>" (that enemy binding's route), "@lure_spot.<enemy>" (where to stand to lure it:
	 * LureSpotDistanceCm short of its route, towards the focus operative) and "@enemy.<enemy>" (the enemy itself, followed while it moves).
	 */
	CODEXTACTICS_API bool IsKnownGuideSpot(FName Guide);

	/**
	 * Splits a guide spot: "@lure_spot.A4_LURE" -> OutKind "lure_spot", OutEnemy "A4_LURE"; "@patrol_route" -> "patrol_route", none.
	 * False when Guide is not a "@" spot.
	 */
	CODEXTACTICS_API bool ParseGuideSpot(FName Guide, FString& OutKind, FName& OutEnemy);

	/** "@lure_spot.<enemy>": the spot stands this far from the enemy's route (inside the 12 m whistle and the 11.25 m crouched throw). */
	constexpr float LureSpotDistanceCm = 1000.f;

	/**
	 * An "enemies" selector (2026-10-21) matches an actor by its object name, its actor label (editor builds; empty elsewhere) or one of its
	 * actor tags - case-insensitive, whole words.
	 */
	CODEXTACTICS_API bool MatchesActorSelector(const FString& Selector, FName ObjectName, const FString& Label, const TArray<FName>& Tags);

	/** 2D distance from Location to the polyline Points (closed when bLoop); a single point = the distance to it; no points = MAX_flt. */
	CODEXTACTICS_API float DistanceToRoute2D(const TArray<FVector>& Points, bool bLoop, const FVector& Location);

	/** The polyline point nearest (2D) to From (closed when bLoop); From itself for no points. */
	CODEXTACTICS_API FVector NearestPointOnRoute(const TArray<FVector>& Points, bool bLoop, const FVector& From);

	/**
	 * "@lure_spot": the point DistanceCm from RoutePoint towards From (2D, RoutePoint's height); From itself when it is already that
	 * close or closer.
	 */
	CODEXTACTICS_API FVector LureSpotTowards(const FVector& RoutePoint, const FVector& From, float DistanceCm);

	/**
	 * Does an event with these facts pass the enemy filter? bSubjectMatches = the subject is an enemy of the binding; NearestOperativeCm =
	 * the nearest living operative's distance (< 0 = none); OffRouteCm = its 2D distance from its route (< 0 = no route: passes).
	 */
	CODEXTACTICS_API bool PassesEnemyFilter(const FTutorialEnemyFilter& Filter, bool bSubjectMatches, float NearestOperativeCm, float OffRouteCm);

	/**
	 * "focus": "role:<EOperativeRole>" (2026-10-20, the A4 ambusher) -> the role name ("MedicSapper"), else NAME_None. Chosen when the
	 * step is triggered: the living operative of that role, else the leader.
	 */
	CODEXTACTICS_API FName ParseRoleFocus(FName Focus);

	/** The arrow's distance label: "42 m" (rounded metres, never negative). */
	CODEXTACTICS_API FString FormatGuideDistance(float DistanceCm);

	/** "crouch" / "crouched" / "crouching" -> crouch, "prone", "stand" / "standing" -> stand; anything else NAME_None. */
	CODEXTACTICS_API FName NormalizeStance(const FString& Text);

	/** Have ("stand" | "crouch" | "prone") is low enough for Need (none / stand = any; crouch = crouched or prone; prone = prone). */
	CODEXTACTICS_API bool StanceSatisfies(FName Have, FName Need);

	/**
	 * May an operative (OperativeId, in OperativeStance) be sent to a destination under the confinement? bDestinationInside = the
	 * destination (and the path end) lies in the allowed area (the runtime tests the geometry). Order of the rules: the confinement's
	 * "stance" (every order) -> inside = allowed -> no "exit" = Outside -> got through already = allowed -> the exit stance (each
	 * operative on his own: no queue since 2026-10-20). "free": "focus": the free operative first = allowed; the others outside = HeldBack.
	 */
	CODEXTACTICS_API ETutorialConfineVerdict JudgeConfinedMove(const FTutorialConfineView& Confine, FName OperativeId, FName OperativeStance,
		bool bDestinationInside);

	/** "allowed" / "outside" / "stance" / "exit_stance" / "not_your_turn" (logs, telemetry). */
	CODEXTACTICS_API const TCHAR* ConfineVerdictName(ETutorialConfineVerdict Verdict);
}
