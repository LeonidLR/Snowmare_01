#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

class FJsonObject;

/**
 * Give way (the game designer's bug report 2026-11-02 item 7, "formation stuck at a ramp foot": the idle squadmates bunched in a stair /
 * ramp mouth block it; L_Gen_BgenTest_X6 LevelGenRampSmoke legs=solo 4 / 17 - the ordered man pushed at 5-15 cm/s behind them for 10 s).
 * An idle operative (no own move order, not in cover, no combat action, standing) whose capsule a squadmate's path ahead crosses steps
 * aside - perpendicular to that path, onto the navmesh, off the whole path corridor - and stays idle there. Unlike the Solo holder yield
 * (UpdateHolderYield: the selected man only, once per order) it works for every mover with an own order, in squad and Solo mode, and is
 * asked again (retry_s) while he still stands in the way. Ramps, stairs, ladder feet and doorways alike: the rule only reads the path.
 *
 * Content/Data/AI/squad_regroup.json "give_way" (snake_case keys, cm / s; a missing key keeps the default), read once on the first
 * SquadGiveWayTuning::Get(). Runtime: USquadSubsystem::UpdateGiveWay (SquadSubsystemGiveWay.cpp), CVar Codex.Squad.GiveWay (1; 0 = off).
 * Pure. Tests: CodexTactics.Squad.GiveWay.*. Godot reference: none (UE-only).
 */
struct CODEXTACTICS_API FSquadGiveWayTuning
{
	/** The rule runs at all (enabled, 0 / 1). */
	bool bEnabled = true;
	/** Only this much of the mover's path ahead of him counts (a man far down his way steps aside later, when it is near), cm (look_ahead_cm). */
	float LookAheadCm = 400.f;
	/** The path crosses a capsule when it passes closer than the two capsule radii + this, cm (clearance_cm). */
	float ClearanceCm = 20.f;
	/** He steps to a spot this far off the path (then 1.5x, 2x), never closer to it than the crossing distance, cm (step_aside_cm). */
	float StepAsideCm = 120.f;
	/** A path point counts for him only within this height of his feet (a flight above / below is not his way), cm (same_level_cm). */
	float SameLevelCm = 150.f;
	/** A spot must project onto the navmesh within this (2D), cm (nav_snap_cm). */
	float NavSnapCm = 30.f;
	/** ... and no more than this above / below his feet - a flat step, not a stair's first flight (also the walk check's lift), cm (max_rise_cm). */
	float MaxRiseCm = 60.f;
	/** Asked again after this long while he still stands in the way (a refused / blocked step, a mover's new path), s (retry_s). */
	float RetrySeconds = 1.5f;
};

namespace SquadGiveWayRules
{
	/** What decides whether he may be asked to give way. */
	struct FIdleState
	{
		bool bWalker = true;
		bool bOwnMoveOrder = false;
		bool bInCover = false;
		bool bCombatAction = false;
		bool bMoving = false;
		bool bHeld = false;
		/** Squad mode: he stands on the spot his last order sent him to (a group order's place - GroupCoverSmoke); Solo / a hold: never set. */
		bool bAtOwnPost = false;
	};

	/** Idle: a walker with no own order, not in cover, no combat action, standing, not held by a traffic / ramp-file hold, not at his post. */
	CODEXTACTICS_API bool IsIdle(const FIdleState& State);

	/** Where a mover's path crosses an idle man's capsule. */
	struct FCrossing
	{
		bool bCrosses = false;
		/** The crossing segment (Path[Segment] -> Path[Segment + 1]). */
		int32 Segment = INDEX_NONE;
		/** Along the path from the mover to the point nearest the idle man, cm. */
		float AlongCm = 0.f;
		/** That point on the path (2D) and the segment's unit direction. */
		FVector2D OnPath = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D::ZeroVector;
		/** +1 he stands left of the path (Direction rotated +90 deg), -1 right. */
		int32 Side = 1;
	};

	/**
	 * Path[0] = the mover now, then his path's corners (navmesh Z). The first path point within LookAheadCm along that passes closer than
	 * CrossCm (2D) to IdleFeet and lies within SameLevelCm of his feet height; never behind the mover (Path[0]).
	 */
	CODEXTACTICS_API FCrossing FindCrossing(TArrayView<const FVector> Path, const FVector& IdleFeet, float CrossCm, const FSquadGiveWayTuning& Tuning);

	/** 2D distance from Point to the path within MaxAlongCm of its start (MaxAlongCm <= 0: the whole path). */
	CODEXTACTICS_API float DistanceToPath(TArrayView<const FVector> Path, const FVector2D& Point, float MaxAlongCm);

	/**
	 * Spots to step to, best first: perpendicular to the crossing segment at StepAsideCm x 1 / 1.5 / 2 - his own side first (crossing the
	 * path walks into the mover), each distance straight out then 45 deg back against the mover's way - only those at least CrossCm off the
	 * path within the crossing's AlongCm + LookAheadCm (a corner's next leg - the flight up - is no spot). The caller keeps the first on the
	 * navmesh, on his level, off a ramp and free.
	 */
	CODEXTACTICS_API TArray<FVector2D> AsideSpots(TArrayView<const FVector> Path, const FCrossing& Crossing, float CrossCm, const FSquadGiveWayTuning& Tuning);
}

namespace SquadGiveWayTuning
{
	/** Content/Data/AI/squad_regroup.json (its "give_way" object). */
	CODEXTACTICS_API FString GetDefaultPath();
	/** The tuning in force (the file on the first call, else the defaults). */
	CODEXTACTICS_API const FSquadGiveWayTuning& Get();
	/** The "give_way" object's parameters over Base (clamped to sane ranges). */
	CODEXTACTICS_API FSquadGiveWayTuning FromJson(const FJsonObject& GiveWay, const FSquadGiveWayTuning& Base = FSquadGiveWayTuning());
	/** Re-reads the file into Get(); false (the defaults kept) when it is missing, unreadable or has no "give_way" object. */
	CODEXTACTICS_API bool Reload();
}