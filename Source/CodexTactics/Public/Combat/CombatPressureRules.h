#pragma once

#include "CoreMinimal.h"
#include "AI/AmbushWatchRules.h"
#include "Data/CombatTypes.h"

/**
 * The combat lab's endless enemy pressure (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md; UE-only, no Godot reference):
 * every IntervalSeconds a spawn event brings a group of GroupMin..GroupMax enemies (at most MaxAlive alive; an event waits while the cap is
 * full) - a PATROL walking a loop or AMBUSHERS hidden near the squad's way. Pure rules (schedule, group, kind, type, spawn spot validity /
 * score, recycling) tested in CodexTactics.Combat.Pressure.*; the level JSON block "pressure" (LevelJsonRules::ParsePressureSettings) fills
 * FCombatPressureConfig; UCombatPressureSubsystem applies them in the world.
 */

/** The two kinds of spawn. */
enum class EPressureSpawnKind : uint8
{
	/** Walks a patrol loop (APatrolRouteActor) until it perceives the squad. */
	Patrol,
	/** Waits concealed near the squad's way and strikes when its side is unwatched (AmbushWatchRules). */
	Ambush
};

/** One entry of the type weights ("pressure"."types"). */
struct CODEXTACTICS_API FPressureTypeWeight
{
	EEnemyArchetype Type = EEnemyArchetype::Frostbitten;
	float Weight = 1.f;
};

/** The level JSON block "pressure" (defaults = the designer's 2026-11-01 numbers). */
struct CODEXTACTICS_API FCombatPressureConfig
{
	/** The block is present and not "enabled": false. */
	bool bEnabled = false;
	/** Seconds between spawn events ("interval_sec"). */
	float IntervalSeconds = 120.f;
	/** Seconds from the level start to the first event ("first_event_sec"). */
	float FirstEventSeconds = 15.f;
	/** Group size ("group_min" / "group_max"). */
	int32 GroupMin = 1;
	int32 GroupMax = 3;
	/** At most this many pressure enemies alive ("max_alive"). */
	int32 MaxAlive = 4;
	/** Share of patrol events 0..1 ("patrol_share"); the rest are ambushes. */
	float PatrolShare = 0.6f;
	/** Random seed (reproducible spawns, "seed"). */
	int32 Seed = 1;
	/** Type weights ("types"); empty = the five allowed types equally. */
	TArray<FPressureTypeWeight> TypeWeights;
	/** No spawn point / patrol waypoint closer than this to any wall or obstacle box ("wall_clearance_cm"). */
	float WallClearanceCm = 250.f;
	/** Patrols spawn this far from every operative ("min_squad_distance_cm" / "max_squad_distance_cm" from the leader). */
	float MinSquadDistanceCm = 2500.f;
	float MaxSquadDistanceCm = 5000.f;
	/** "patrol": {"points", "radius_cm", "wait_sec"}: the patrol loop round its spawn point. */
	int32 PatrolPoints = 4;
	float PatrolRadiusCm = 1200.f;
	float PatrolWaitSeconds = 2.f;
	/** "ambush": {"min_squad_distance_cm", "max_squad_distance_cm"}: the hiding spot's distance from every operative / the leader. */
	float AmbushMinDistanceCm = 1400.f;
	float AmbushMaxDistanceCm = 3000.f;
	/** "ambush": {"watch_half_angle_deg", "watch_range_cm", "strike_range_cm"}. */
	FAmbushWatchParams Watch;
	/** "recycle": {"distance_cm", "after_sec"}: an unengaged pressure enemy this far from the squad, unseen, for this long is removed. */
	float RecycleDistanceCm = 6000.f;
	float RecycleAfterSeconds = 60.f;
};

/** A wall / obstacle as a 2D oriented box (the level's blocking static meshes). */
struct CODEXTACTICS_API FPressureBox
{
	FVector2D Center = FVector2D::ZeroVector;
	/** Unit axis of the box's local X (its local Y is perpendicular, rotated +90 deg). */
	FVector2D AxisX = FVector2D(1.0, 0.0);
	/** Half sizes along the local X / Y, cm. */
	FVector2D HalfSize = FVector2D::ZeroVector;
};

/** Why a spawn point is refused. */
enum class ESpawnPointVerdict : uint8
{
	Ok,
	/** Closer than the clearance to a wall / obstacle box (or inside one). */
	NearWall,
	/** Closer than the minimum distance to an operative. */
	NearSquad,
	/** Outside the level's bounds. */
	OutOfBounds
};

/** When the spawn timer asks for an event. */
enum class EPressureEventDecision : uint8
{
	/** Not due yet. */
	NotDue,
	/** Due and there is room: spawn now. */
	Spawn,
	/** Due but MaxAlive enemies are alive: the event waits for room. */
	DelayedCapFull
};

/** Inputs of the spawn spot score. */
struct CODEXTACTICS_API FPressureSpotInput
{
	EPressureSpawnKind Kind = EPressureSpawnKind::Patrol;
	/** Distance to the squad's leader, cm. */
	float DistanceCm = 0.f;
	/** The wanted band of DistanceCm. */
	float MinDistanceCm = 0.f;
	float MaxDistanceCm = 1.f;
	/** Cosine of the angle between the squad's heading and the direction to the spot (1 = straight ahead). */
	float AheadCos = 0.f;
	/** No operative has a line of sight to the spot. */
	bool bConcealed = false;
	/** An operative looks its way (AmbushWatchRules::IsWatched). */
	bool bWatched = false;
};

namespace CombatPressureRules
{
	/** Every enemy kind the pressure may spawn (designer 2026-11-01): Hound, Frostbitten, Spitter, Cutter, Marksman - never the Brute or the Cryo Drone. */
	CODEXTACTICS_API bool IsAllowedType(EEnemyArchetype Type);

	/** The allowed types with a positive weight (an empty / all-zero list = the five allowed types at weight 1). */
	CODEXTACTICS_API TArray<FPressureTypeWeight> EffectiveWeights(const TArray<FPressureTypeWeight>& Weights);

	/** The schedule: NotDue before DueAtSeconds; then Spawn while Alive < MaxAlive, else DelayedCapFull (the event waits for room). */
	CODEXTACTICS_API EPressureEventDecision DecideEvent(float NowSeconds, float DueAtSeconds, int32 Alive, int32 MaxAlive);

	/** Group size from Rand01 in [0, 1): Min..Max (both clamped >= 1, Max >= Min), cut to the room left under MaxAlive (>= 0). */
	CODEXTACTICS_API int32 GroupSize(float Rand01, int32 Min, int32 Max, int32 Alive, int32 MaxAlive);

	/** Rand01 < PatrolShare -> Patrol, else Ambush. */
	CODEXTACTICS_API EPressureSpawnKind PickKind(float Rand01, float PatrolShare);

	/** The weighted pick of Rand01 in [0, 1) over EffectiveWeights(Weights). */
	CODEXTACTICS_API EEnemyArchetype PickType(const TArray<FPressureTypeWeight>& Weights, float Rand01);

	/** Planar distance from Point to Box, cm (0 inside). */
	CODEXTACTICS_API float DistanceToBox(const FVector2D& Point, const FPressureBox& Box);

	/** The smallest DistanceToBox over Boxes, cm (MAX_flt without boxes). */
	CODEXTACTICS_API float Clearance(const FVector2D& Point, const TArray<FPressureBox>& Boxes);

	/**
	 * Is Point a valid spawn point: inside Bounds (an invalid box = no bounds check), at least ClearanceCm from every box, at least
	 * MinSquadDistanceCm from every operative. The first failing reason in that order.
	 */
	CODEXTACTICS_API ESpawnPointVerdict CheckSpawnPoint(const FVector2D& Point, const TArray<FPressureBox>& Boxes, float ClearanceCm,
		const TArray<FVector2D>& Squad, float MinSquadDistanceCm, const FBox2D& Bounds);

	/**
	 * Score of a valid spot (higher = better). Patrols: within the distance band, ahead of the squad, out of its sight and looks preferred.
	 * Ambushers: MUST be concealed (an unconcealed spot scores -1000), ahead of the squad's way preferred strongly (it walks past them and
	 * turns its back), the middle of the band preferred; a watched spot is fine (they hold until the squad looks away).
	 */
	CODEXTACTICS_API float ScoreSpot(const FPressureSpotInput& Input);

	/** The loop of Points waypoints round Center at RadiusCm, the first at PhaseDeg, counter-clockwise (>= 2 points). */
	CODEXTACTICS_API TArray<FVector2D> PatrolLoop(const FVector2D& Center, float RadiusCm, int32 Points, float PhaseDeg);

	/** Where the Index-th member of a group stands round the group's point (0 = on it; then a ring at SpacingCm, 120 deg apart). */
	CODEXTACTICS_API FVector2D GroupOffset(int32 Index, float SpacingCm);

	/**
	 * Remove an unengaged pressure enemy (still on patrol / hidden): far from every operative (beyond Config.RecycleDistanceCm), unseen, for
	 * at least Config.RecycleAfterSeconds - it frees its place under the cap for a new event. Engaged ones are never removed.
	 */
	CODEXTACTICS_API bool ShouldRecycle(bool bEngaged, bool bVisibleToSquad, float NearestSquadCm, float FarSeconds, const FCombatPressureConfig& Config);
}
