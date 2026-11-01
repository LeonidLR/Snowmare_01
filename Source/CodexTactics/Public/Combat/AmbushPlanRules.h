#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/**
 * Coordinated multi-direction ambushes (game designer answer 2, 2026-11-01, docs/design/COMBAT_LAB.md "Coordinated ambushes"; UE-only, no
 * Godot reference). The classic scheme: first the FLANKS are blocked (a party left and a party right of the squad strike first), then the
 * REAR and the FRONT attack. Ranged enemies (Spitter, Marksman) of any party open suppressive fire (USuppressionSubsystem's deliberate
 * intent) together with the flank block, while the others flank. Each party waits at its own hiding spot (AmbushSpotRules candidates in its
 * sector round the plan's anchor). Pure rules, tested in CodexTactics.Combat.AmbushPlan.*; UCombatPressureSubsystem runs the plans.
 */

/** A member's party in an ambush. */
enum class EAmbushParty : uint8
{
	/** A plain (single-spot) ambush group. */
	Single,
	FlankLeft,
	FlankRight,
	Front,
	Rear
};

/** A coordinated ambush's phase. */
enum class EAmbushPlanPhase : uint8
{
	/** The squad is beyond the trigger range of the plan's anchor: every party waits hidden. */
	Waiting,
	/** In range, but both flank parties' sides are watched: holding. */
	Holding,
	/** The flank parties (and every ranged member, suppressing) strike. */
	FlankBlock,
	/** The rear and the front parties strike too. */
	Assault
};

/** Tuning (level JSON "pressure"."ambush"."coordinated"). */
struct CODEXTACTICS_API FAmbushPlanParams
{
	/** Share of ambush events that are coordinated plans 0..1 ("share"; the rest are single-spot ambushes). */
	float Share = 0.35f;
	/** Bodies of a plan ("bodies_min" / "bodies_max"; one per party, the extras go to the front, then the rear). */
	int32 BodiesMin = 4;
	int32 BodiesMax = 6;
	/**
	 * A plan spawns only while the pressure enemies alive + its bodies stay within this ("max_alive"; the plain events keep the level's
	 * max_alive 4, so a plan of 4-6 needs its own cap).
	 */
	int32 MaxAlive = 6;
	/** Every party's spot this far from every operative / the leader, cm ("min_squad_distance_cm" / "max_squad_distance_cm"). */
	float MinSquadDistanceCm = 1000.f;
	float MaxSquadDistanceCm = 3000.f;
	/** The plan's anchor (the kill zone's middle) this far ahead of the leader along the squad's heading, cm ("anchor_ahead_cm"). */
	float AnchorAheadCm = 500.f;
	/** Preferred party spots round the anchor, cm ("flank_offset_cm", "front_offset_cm", "rear_offset_cm"). */
	float FlankOffsetCm = 1500.f;
	float FrontOffsetCm = 1600.f;
	float RearOffsetCm = 1500.f;
	/** Party spots at least this far apart, cm ("min_separation_cm"). */
	float MinSeparationCm = 600.f;
	/** The plan arms when the nearest operative is this close to the anchor, cm ("trigger_range_cm"). */
	float TriggerRangeCm = 1800.f;
	/**
	 * The rear + front strike this long after the flanks at the earliest / at the latest, s ("assault_min_sec" / "assault_max_sec"); in
	 * between as soon as every living flank member is within BlockDistanceCm of an operative ("block_distance_cm").
	 */
	float AssaultMinSeconds = 1.5f;
	float AssaultMaxSeconds = 4.f;
	float BlockDistanceCm = 900.f;
	/** At least this many ranged members (Spitter / Marksman) per plan ("min_ranged"; melee front / rear members are swapped). */
	int32 MinRanged = 1;
};

/** One step of a plan's phase machine (AmbushPlanRules::NextPhase). */
struct CODEXTACTICS_API FAmbushPlanTick
{
	EAmbushPlanPhase Phase = EAmbushPlanPhase::Waiting;
	/** Seconds since the phase began. */
	float SecondsInPhase = 0.f;
	/** Planar distance from the anchor to the nearest living operative, cm. */
	float NearestSquadCm = MAX_flt;
	/** Flank parties (with a living member) whose spot is unwatched now (AmbushWatchRules::IsWatched false). */
	int32 FlanksUnwatched = 0;
	/** Some party was engaged otherwise (shot at, spotted, a mate down). */
	bool bAnyEngaged = false;
	/** Living flank members / those within BlockDistanceCm of an operative. */
	int32 FlankMembersAlive = 0;
	int32 FlankMembersInPlace = 0;
};

namespace AmbushPlanRules
{
	/** The four parties of a plan in their assignment order (index 0..3): FlankLeft, FlankRight, Front, Rear. */
	CODEXTACTICS_API EAmbushParty PlanParty(int32 SlotIndex);

	/** "flank left" / "flank right" / "front" / "rear" / "single" (logs). */
	CODEXTACTICS_API const TCHAR* PartyName(EAmbushParty Party);
	CODEXTACTICS_API const TCHAR* PhaseName(EAmbushPlanPhase Phase);

	CODEXTACTICS_API bool IsFlank(EAmbushParty Party);

	/** The ranged enemies that suppress (Spitter, Marksman; the Cryo Drone is never a pressure enemy). */
	CODEXTACTICS_API bool IsRanged(EEnemyArchetype Type);

	/** The party of a plan's MemberIndex-th body: 0 FlankLeft, 1 FlankRight, 2 Front, 3 Rear, then Front / Rear alternately. */
	CODEXTACTICS_API EAmbushParty PartyOf(int32 MemberIndex);

	/**
	 * Bodies of a new plan from Rand01 in [0, 1): BodiesMin..BodiesMax cut to the room under Params.MaxAlive with Alive pressure enemies;
	 * 0 when there is no room for 4 (a plan needs one body per party).
	 */
	CODEXTACTICS_API int32 PlanBodies(float Rand01, const FAmbushPlanParams& Params, int32 Alive);

	/**
	 * Ensures MinRanged ranged members: melee FRONT members are swapped to Ranged first, then the rear's (the flanks stay melee - they block).
	 * Returns how many were swapped.
	 */
	CODEXTACTICS_API int32 EnsureRanged(TArray<EEnemyArchetype>& Types, EEnemyArchetype Ranged, int32 MinRanged);

	/** The plan's anchor: AnchorAheadCm ahead of Leader along HeadingYawDeg. */
	CODEXTACTICS_API FVector2D Anchor(const FVector2D& Leader, float HeadingYawDeg, const FAmbushPlanParams& Params);

	/** A party's preferred spot round Anchor (FlankRight = the heading's right, +90 deg in UE yaw). */
	CODEXTACTICS_API FVector2D SlotPoint(const FVector2D& Anchor, float HeadingYawDeg, EAmbushParty Party, const FAmbushPlanParams& Params);

	/** The sector of Point round Anchor: Front within +-45 deg of the heading, FlankRight 45..135, Rear beyond 135, FlankLeft -135..-45. */
	CODEXTACTICS_API EAmbushParty SectorOf(const FVector2D& Anchor, float HeadingYawDeg, const FVector2D& Point);

	/**
	 * Assigns a hiding spot to each party (4 entries, PlanParty order; INDEX_NONE = none): the candidate in the party's sector nearest to
	 * its slot point, at least MinSeparationCm from the spots already taken; the flanks first.
	 */
	CODEXTACTICS_API TArray<int32> AssignSpots(const FVector2D& Anchor, float HeadingYawDeg, const TArray<FVector2D>& Candidates,
		const FAmbushPlanParams& Params);

	/** Every entry of AssignSpots is set (the classic scheme needs all four parties). */
	CODEXTACTICS_API bool IsComplete(const TArray<int32>& Assigned);

	/**
	 * The phase machine. Waiting / Holding: a party engaged otherwise -> FlankBlock; the nearest operative beyond TriggerRangeCm -> Waiting;
	 * in range with a flank side unwatched -> FlankBlock, else Holding. FlankBlock -> Assault once AssaultMinSeconds passed and every living
	 * flank member is in place (or none is left), or at AssaultMaxSeconds. Assault stays.
	 */
	CODEXTACTICS_API EAmbushPlanPhase NextPhase(const FAmbushPlanTick& Tick, const FAmbushPlanParams& Params);

	/** A member strikes in Phase: flank parties and every ranged member (the suppressors) from FlankBlock, everyone in Assault. */
	CODEXTACTICS_API bool Springs(EAmbushPlanPhase Phase, EAmbushParty Party, bool bRanged);
}
