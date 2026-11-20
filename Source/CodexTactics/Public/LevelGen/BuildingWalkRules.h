#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Pure rules of the building walk check (S2.9 / P0b, docs/design/levelgen/S2_ROOMS_PLAN.md: "squad leg through every unit_entry";
 * risk rows 1 and 4): which doors are a planned building's flat entries, when a walking operative has arrived or is stuck, the
 * squad-vs-solo time gate and the building pass-share gate. Used by the dev smoke CodexTactics.LevelGenBuildingsSmoke walk=1
 * (Source/CodexTacticsDev/Private/Debug/LevelGenBuildingsSmokeCommand.cpp), which feeds it the layout JSON and the pawns' feet.
 * UE-only (no Godot reference: the Godot archive had no generated buildings).
 */

/** One flat entry door of a planned building: the door between a common room (landing / lobby / corridor) and a flat's room. */
struct CODEXTACTICS_API FBuildingFlatEntry
{
	/** The opening id (partitions[].openings[].id). */
	FString Entry;
	/** The flat (units[].id, kind "flat"). */
	FString Unit;
	/** The linked room outside the flat (where the squad starts). */
	FString FromRoom;
	/** The linked room of the flat (where the squad goes). */
	FString ToRoom;
	/** The opening's own role ("unit_entry" when the plan is consistent). */
	FString Role;
	int32 Floor = 0;
};

/** The flat entries of one building plus the plan inconsistencies found on the way. */
struct CODEXTACTICS_API FBuildingFlatEntries
{
	TArray<FBuildingFlatEntry> Entries;
	/** Ids listed in units[].entries whose opening role is not "unit_entry". */
	TArray<FString> EntriesWithoutRole;
	/** Openings with role "unit_entry" that no flat lists as its entry. */
	TArray<FString> RoleNotAnEntry;
	/** Entry ids with no opening record, or whose links do not join exactly one flat room to one room outside the flat. */
	TArray<FString> Unresolved;
};

/** "No progress" tracker: stuck when the pawn stays within MoveCm of its last mark for Seconds. */
struct CODEXTACTICS_API FBuildingWalkProgress
{
	FVector Mark = FVector::ZeroVector;
	double MarkTime = 0.0;
	bool bStarted = false;

	void Reset(double Now, const FVector& Position);
	/** Moves the mark when the pawn left it; true when it has not for Seconds. */
	bool IsStuck(double Now, const FVector& Position, double MoveCm, double Seconds);
};

namespace BuildingWalkRules
{
	/** Arrived: feet within this of the goal in 2D (cm)... */
	constexpr double ArriveRadiusCm = 80.0;
	/** ...and on the goal's storey (|feet Z - goal Z|, cm). */
	constexpr double ArriveZToleranceCm = 70.0;
	/** Stuck: moved less than this (cm) in StuckSeconds (game seconds). */
	constexpr double StuckMoveCm = 50.0;
	constexpr double StuckSeconds = 4.0;
	/** Risk row 4: a squad leg through a flat entry takes at most this many times the solo leg. */
	constexpr double MaxSquadRatio = 2.0;
	/** S2.9 acceptance: share of passing buildings. */
	constexpr double TargetMinPass = 0.95;

	/**
	 * The flat entries of a layout building: every units[] record of kind "flat" lists its entry opening ids; the opening is looked up
	 * in partitions[].openings[] and the building's openings[]; its links give the flat room (one of the unit's rooms) and the room
	 * outside. The opening role is recorded, not trusted: the plan's roles and its units can disagree (reported).
	 */
	CODEXTACTICS_API FBuildingFlatEntries ScanFlatEntries(const FJsonObject& Building);

	CODEXTACTICS_API bool IsArrived(const FVector& Feet, const FVector& Goal, double RadiusCm = ArriveRadiusCm,
		double ZToleranceCm = ArriveZToleranceCm);

	/** Squad time within MaxRatio x the solo time (a failed / zero solo leg: false). */
	CODEXTACTICS_API bool SquadWithinRatio(double SquadSeconds, double SoloSeconds, double MaxRatio = MaxSquadRatio);

	/** PASS share gate: at least one building checked and Passed / Checked >= MinPass. */
	CODEXTACTICS_API bool PassesShare(int32 Passed, int32 Checked, double MinPass);

	/** Game-seconds limit of one walk leg of a navmesh path of PathCm (20 s + 1 s per metre: a third of the walk speed). */
	CODEXTACTICS_API double LegLimitSeconds(double PathCm);
}
