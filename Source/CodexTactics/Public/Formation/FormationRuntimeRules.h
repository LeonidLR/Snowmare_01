#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Formation look sectors - the runtime glue (Godot reference: none (UE-only, stage 6 of docs/design/FORMATION_SECTORS.md, contract
 * docs/design/FORMATION_CONTRACT.md 7; reference implementation Tools/TrainingGround/src/movement/sim.ts simulate / stepTowards /
 * wallAhead)). Pure: how USquadSubsystem drives the pure formation rules (FormationShapeRules / FormationSectorRules /
 * FormationTerrainRules / FormationWallRules) per tick - which role a follower slot plays, the squad's mode, the formation with the
 * wall layout only on arrival, the terrain probe rays and the walls built from their hits, the follower speed with the slot's
 * velocity lead (FOLLOW_GAIN), when a member walking into the wall layout turns to his layout yaw, the wall room from the probes along
 * a wall and the minimal-travel role assignment of a wall order. Tests: CodexTactics.Formation.Runtime.*.
 *
 * Behind the console variable Codex.Formation.Sectors (default 1; 0 = the old triangle formation, facing and speeds, every old path
 * untouched).
 */
namespace FormationRuntime
{
	/** Contract 7 / sim WALL_TURN_DIST: a member walking to his wall-layout slot turns to the layout yaw this close to it, cm. */
	inline constexpr float WallTurnDistCm = 100.f;
	/** Contract 7 / sim FOLLOW_GAIN: a 1 m gap to the (moving) slot adds 2 m/s on top of the slot's own velocity, 1/s. */
	inline constexpr float FollowGain = 2.f;
	/** Contract 7 / sim STOP_RADIUS: a member stops only within this distance of a slot that stands still, cm. */
	inline constexpr float StopRadiusCm = 25.f;
	/** A slot slower than this stands still, cm/s (sim: 1 cm/s per tick -> 1). */
	inline constexpr float SlotStillSpeed = 1.f;
	/** Moving more than this far off his facing is walking backwards (contract 7 / sim angleDiff > 120), deg. */
	inline constexpr float BackwardAngleDeg = 120.f;
	/** Sim wallAhead: the wall layout's wall is the one straight ahead of the leader within this distance, cm. */
	inline constexpr float WallAheadCm = 300.f;
	/** Runtime probe height above the feet: an obstacle lower than this is looked over, not a wall for the formation, cm. */
	inline constexpr float ProbeHeightCm = 120.f;
	/** A probe hit becomes a thin wall segment this long each side of the hit point, cm. */
	inline constexpr float ProbeSegmentHalfCm = 10.f;
	/** Wall room probes: one sample every this many cm along the wall. */
	inline constexpr float RoomStepCm = 50.f;
	/** Wall room probes: at most this far each side, cm (Layout needs 2 x wall.spacing for the 4th man). */
	inline constexpr float RoomMaxCm = 400.f;
	/** A follower's formation move aims this far ahead of his slot along the slot's velocity, s (he never "arrives" at a moving slot). */
	inline constexpr float LeadSeconds = 0.5f;
	/** The automatic wall layout is computed again once the leader stands this far from where it was computed, cm. */
	inline constexpr float WallLayoutRecomputeCm = 50.f;

	/** Console Codex.Formation.Sectors != 0 (default 1). */
	CODEXTACTICS_API bool IsEnabled();

	/**
	 * The role of the follower in formation slot SlotIndex (SquadFormation slots: 0 left, 1 right, 2 the second row) in a squad of
	 * MemberCount (leader included): 2 -> slot 0 is the pair's partner Z; 3 -> slot 0 L, slot 1 P; 4 -> slot 0 L, slot 1 P, slot 2 Z
	 * (design: today's slot 2 = 2nd row LEFT becomes the rear man straight behind). Unset beyond the four roles (5+ members: the old
	 * slot math for the extra ones).
	 */
	CODEXTACTICS_API TOptional<EFormationRole> RoleForFollowerSlot(int32 SlotIndex, int32 MemberCount);

	/** The squad's mode: the wall layout -> wall; the leader moving -> run when sprinting, else walk (patrol); standing -> halt. */
	CODEXTACTICS_API EFormationMode ResolveMode(bool bLeaderMoving, bool bLeaderSprinting, bool bWallLayout);

	/**
	 * The formation for the terrain class (FormationTerrainRules::FormationFor) with contract 7's "the wall layout only on ARRIVAL": a
	 * wall straight ahead (at_wall) while the leader still walks is a corner to turn - Previous (the formation he walked in) is kept.
	 */
	CODEXTACTICS_API EFormationKind ChooseFormation(EFormationTerrainClass Class, int32 MemberCount, bool bLeaderStopped, EFormationKind Previous);

	/**
	 * Probe debounce: a world probe whose raw class (FormationTerrainRules::ClassifyRaw) differs from the class in force feeds the
	 * classifier only when the probe before it said the same - one probe alone (a ray grazing a wall's end cap, which a trace reads as a
	 * wall across the heading) does not start the hysteresis. The first probe (nothing in force) and a probe agreeing with the class in
	 * force are always taken.
	 */
	CODEXTACTICS_API bool AcceptProbe(EFormationTerrainClass Raw, const TOptional<EFormationTerrainClass>& PreviousProbeRaw,
		const TOptional<EFormationTerrainClass>& InForce);

	/** One terrain probe ray from the leader: its UE yaw and length, cm. */
	struct FProbeRay
	{
		float Yaw = 0.f;
		float Length = 0.f;
	};

	/**
	 * The rays the world probe traces so that FormationTerrainRules::Classify / PassageWidth see the same walls as with the layout's
	 * segments: the forward fan 0 / -20 / +20 deg (the 0 ray as long as max(ClassifyAtWallDist, WallAheadCm) - it also finds the wall
	 * layout's wall, the others ClassifyAtWallDist) and per side the 60 / 90 / 120 deg rays, each ClassifyProbeSide / sin(a) long (they
	 * reach ClassifyProbeSide across the heading). Order: forward 0, -20, +20, then right 60 / 90 / 120, left 60 / 90 / 120.
	 */
	CODEXTACTICS_API TArray<FProbeRay> ProbeRays(float HeadingYaw, const FFormationConfig& Config = FFormationConfig());

	/**
	 * A probe hit as a thin wall segment for the pure rules: through Point, along the surface (perpendicular to the hit's horizontal
	 * Normal), HalfLength each side, thickness 0. A ray from the probe origin to Point hits it at Point.
	 */
	CODEXTACTICS_API FFormationWall WallFromHit(const FVector2D& Point, const FVector2D& Normal, float HalfLength = ProbeSegmentHalfCm);

	/**
	 * A straight wall for FormationWallRules::Layout from the world probes: the face line through FacePoint with the outward normal
	 * NormalYaw, RoomLeft / RoomRight along it (left / right of a man FACING the wall, the Layout convention), thickness 0.
	 */
	CODEXTACTICS_API FFormationWall WallAlong(const FVector2D& FacePoint, float NormalYaw, float RoomLeft, float RoomRight);

	/**
	 * Free wall length from the wall room probes: Samples[i] = the wall still continues RoomStep * (i + 1) along it; the room ends at
	 * the first gap (the samples after it do not count), cm.
	 */
	CODEXTACTICS_API float MeasureRoom(const TArray<bool>& Samples, float StepCm = RoomStepCm);

	/** One follower's move this repath (contract 7). */
	struct FFollowerMove
	{
		/** The velocity he should move with, cm/s (planar). */
		FVector2D Velocity = FVector2D::ZeroVector;
		/** Its size after the caps, cm/s (0 = stand). */
		float Speed = 0.f;
		/** Within StopRadiusCm of a slot that stands still: he stands. */
		bool bStop = false;
		/** The move is more than BackwardAngleDeg off his facing (the backward cap applied when walking). */
		bool bBackward = false;
	};

	/**
	 * Contract 7 / sim stepTowards (no detour): v = v_slot (capped at the top speed) * Jitter + dir * min(top, d * FollowGain), top =
	 * TopSpeed * move.catchup_mult; capped by top and - walking (not bRun), the move more than BackwardAngleDeg off FacingYaw - by
	 * WalkSpeed * move.backward_mult. bStop within StopRadiusCm while the slot stands still. TopSpeed = his own max speed (walk, or the
	 * sprint when running), WalkSpeed = his walk speed.
	 */
	CODEXTACTICS_API FFollowerMove FollowerMove(const FVector2D& MemberPos, float FacingYaw, const FVector2D& SlotPos, const FVector2D& SlotVelocity,
		float Jitter, float TopSpeed, float WalkSpeed, bool bRun, const FFormationConfig& Config = FFormationConfig());

	/** Where a formation move aims: the slot plus its velocity x LeadSeconds (the slot itself while it stands still). */
	CODEXTACTICS_API FVector2D LeadDestination(const FVector2D& SlotPos, const FVector2D& SlotVelocity, float Lead = LeadSeconds);

	/**
	 * Contract 7: walking into the wall layout a member faces his travel direction and turns to his layout yaw only within
	 * WallTurnDistCm of his slot (or standing).
	 */
	CODEXTACTICS_API bool UseWallLayoutYaw(float DistanceToSlotCm, bool bMoving);

	/**
	 * Minimal-travel assignment of From[i] (members) to To[j] (places), From.Num() <= To.Num() <= 4 (brute force): Result[i] = the
	 * index into To of member i; equal sums keep the earlier permutation (member order). Empty when From is longer than To or To has
	 * more than 4 places.
	 */
	CODEXTACTICS_API TArray<int32> AssignByTravel(const TArray<FVector2D>& From, const TArray<FVector2D>& To);

	/** The leader's heading from his planar velocity: its yaw above MinSpeed (cm/s), else unset (keep the old heading). */
	CODEXTACTICS_API TOptional<float> HeadingFromVelocity(const FVector2D& Velocity, float MinSpeed = 30.f);
}
