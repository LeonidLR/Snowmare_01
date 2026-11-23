#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the level generator's mission goals (designer 2026-11-17, TASK_BOARD F-4 option «а»): the runtime file
 * Content/Data/LevelGen/objectives/<Map>.objectives.json ("codex.mission_goals/1", written by the Training Ground «Принять» /
 * «Собрать в Unreal» from the layout's `objectives` block - Tools/TrainingGround/src/levelgen/missionGoals.ts), when an operative has
 * reached a goal and which goals may count now (free order / sequence), and the zone-clear rule (designer 2026-11-20: a goal does not
 * count while a resisting hostile stands in its zone). No world: UMissionGoalsSubsystem feeds it the squad's and the hostiles' feet.
 * UE-only (no Godot reference: the Godot archive had no generated maps).
 */

/** What a goal stands on (the generator's candidate kinds, objectives.ts goalCandidates). */
enum class EMissionGoalKind : uint8
{
	Room,
	Yard,
	Street,
	Other
};

/** "free": any order; "sequence": only the next unreached goal counts. */
enum class EMissionGoalOrder : uint8
{
	Free,
	Sequence
};

/** One goal in world space. */
struct CODEXTACTICS_API FMissionGoal
{
	FString Id;
	EMissionGoalKind Kind = EMissionGoalKind::Other;
	/** The walk layer ("ground", "roof:<B>", "floor:<B>:<n>", "cut"). */
	FString Layer;
	/** World cm: the goal point on the floor top of its layer. */
	FVector Location = FVector::ZeroVector;
	/** 2D reach radius, cm. */
	float RadiusCm = 300.f;
	FString Building;
	FString Zone;
	/** Room goals: the room's floor polygon (world XY, >= 3 corners); then the goal counts anywhere inside it instead of the radius. */
	TArray<FVector2D> Area;
	/** "grant_keys": door keys the squad gets when the goal is reached (a quest item; openable doors R2, UDoorSubsystem::AddKey). */
	TArray<FString> GrantKeys;
};

/** The parsed file. */
struct CODEXTACTICS_API FMissionGoalsFile
{
	FString Map;
	FString Layout;
	EMissionGoalOrder Order = EMissionGoalOrder::Free;
	float DefaultRadiusCm = 300.f;
	/** The feet of an operative must be within this of the goal's floor, cm (same floor). */
	float FloorToleranceCm = 150.f;
	/** World-space label height (above the goal's floor) and font size, cm. */
	float LabelHeightCm = 550.f;
	float LabelWorldSizeCm = 80.f;
	TArray<FMissionGoal> Goals;
};

/**
 * One hostile actor as the zone-clear rule sees it (designer 2026-11-20: "a zone cannot be held while enemy soldiers in it still
 * resist"). Filled by UMissionGoalsSubsystem from the actors carrying the "Enemy" tag (monsters and hostile PMC fighters).
 */
struct CODEXTACTICS_API FMissionGoalHostile
{
	/** World cm: his feet (actor location minus the collision half height). */
	FVector Feet = FVector::ZeroVector;
	/** Health above zero (or no health component). */
	bool bAlive = true;
	/** Dying / killed in action: out of the fight even before his health reads zero or his tag goes. */
	bool bDying = false;
	// No "hidden" flag on purpose: UTacticalSightSubsystem hides the enemies the squad does not see (fog of war) - an unseen enemy in
	// the zone still resists.
};

/** A goal newly reached by an UpdateReached call. */
struct CODEXTACTICS_API FMissionGoalHit
{
	int32 GoalIndex = INDEX_NONE;
	/** Index into the feet array passed to UpdateReached (the operative that reached it). */
	int32 OperativeIndex = INDEX_NONE;
};

namespace MissionGoalsRules
{
	/** The schema id of the runtime file. */
	extern CODEXTACTICS_API const TCHAR* const Schema;

	/** Default reach radius (2D), cm - the designer's F-4 brief. */
	constexpr float DefaultRadiusCm = 300.f;
	/** Default same-floor tolerance between the operative's feet and the goal's floor, cm. */
	constexpr float DefaultFloorToleranceCm = 150.f;

	/** "room" / "yard" / "street" -> the kind; anything else Other. */
	CODEXTACTICS_API EMissionGoalKind ParseKind(const FString& Kind);
	CODEXTACTICS_API const TCHAR* KindToString(EMissionGoalKind Kind);

	/**
	 * Parses the runtime file. False (OutErrors says why) on bad JSON, a wrong / newer schema, or no valid goal; a single bad goal is
	 * skipped with an error line and the rest kept. A goal without radius_cm takes the file's default_radius_cm (else DefaultRadiusCm).
	 */
	CODEXTACTICS_API bool Parse(const FString& Json, FMissionGoalsFile& Out, TArray<FString>& OutErrors);

	/** Point-in-polygon (even-odd) on the ground plane. */
	CODEXTACTICS_API bool IsInsidePolygon(const FVector2D& Point, TConstArrayView<FVector2D> Polygon);

	/**
	 * An operative whose feet stand at FeetLocation is at the goal: same floor (|feet Z - goal Z| <= FloorToleranceCm) and - a room goal
	 * with an Area - inside the room polygon, else within RadiusCm in 2D.
	 */
	CODEXTACTICS_API bool IsAtGoal(const FMissionGoal& Goal, const FVector& FeetLocation, float FloorToleranceCm);

	/**
	 * The hostile still RESISTS: alive and not dying / killed in action (seen or not). A critically wounded fighter still fires prone
	 * (CriticalWoundRules) and a knocked-down one gets up again - both resist.
	 */
	CODEXTACTICS_API bool IsResisting(const FMissionGoalHostile& Hostile);

	/** The feet of the hostiles that resist (IsResisting), in order. */
	CODEXTACTICS_API TArray<FVector> ResistingFeet(TConstArrayView<FMissionGoalHostile> Hostiles);

	/**
	 * The goal's zone is CONTESTED: a resisting hostile stands in it - the same zone test as the squad's (IsAtGoal: same floor within
	 * FloorToleranceCm, inside the room polygon of a room goal, else within RadiusCm in 2D).
	 */
	CODEXTACTICS_API bool IsGoalContested(const FMissionGoal& Goal, TConstArrayView<FVector> ResistingHostileFeet, float FloorToleranceCm);

	/**
	 * The goal's zone outline in world space for the HUD / minimap (designer 2026-11-20: a goal is shown as a ZONE, not a point): a room
	 * goal with an Area = its room polygon at the goal's floor height, else a circle of RadiusCm (CircleSegments clamped to 8..128).
	 */
	CODEXTACTICS_API TArray<FVector> BuildZoneOutline(const FMissionGoal& Goal, int32 CircleSegments = 48);

	/** The goal may be reached now: unreached and (free order, or the first unreached one in a sequence). */
	CODEXTACTICS_API bool CanCount(EMissionGoalOrder Order, TConstArrayView<bool> Reached, int32 GoalIndex);

	/**
	 * Marks the goals reached by the operatives' feet (Reached is resized to the goal count). In a sequence a goal reached makes the next
	 * one countable in the same call (two goals stood on at once both count, in order). Returns the newly reached goals in order.
	 * Zone clear (designer 2026-11-20): a goal whose zone a resisting hostile stands in (ResistingHostileFeet, IsGoalContested) does NOT
	 * count, however many operatives stand in it; it counts on the first call after the zone is cleared with an operative still in it.
	 * OutBlocked (optional) gets the countable goals an operative stands in that stay uncounted because their zone is contested.
	 */
	CODEXTACTICS_API TArray<FMissionGoalHit> UpdateReached(const FMissionGoalsFile& File, TArray<bool>& Reached, TConstArrayView<FVector> OperativeFeet,
		TConstArrayView<FVector> ResistingHostileFeet = {}, TArray<int32>* OutBlocked = nullptr);

	CODEXTACTICS_API int32 CountReached(TConstArrayView<bool> Reached);

	/** Every goal reached (false for a file without goals: no goals = no goal victory). */
	CODEXTACTICS_API bool AreAllReached(TConstArrayView<bool> Reached);

	/** The HUD line under the objective banner: "GOALS: 1/3"; "GOALS: 1/3 - CLEAR THE ZONE" while a goal is blocked by its contested zone. */
	CODEXTACTICS_API FString FormatHudLine(int32 ReachedCount, int32 Total, bool bZoneNotClear = false);

	/** <Content>/Data/LevelGen/objectives/<MapName>.objectives.json (absolute). */
	CODEXTACTICS_API FString GetDefaultPathForMap(const FString& MapName);
}
