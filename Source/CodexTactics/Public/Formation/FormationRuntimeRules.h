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
	/**
	 * A halted leader turns his search back to 12 (his sector's centre) for this long; then his facing is his own again and the halted
	 * formation's heading follows it (as the old formation), s.
	 */
	inline constexpr float LeaderHaltLookSeconds = 1.f;
	/** A formation follower gets a new move (speed + lead destination) this often, s (the old formation: FSquadFormationConfig::RepathInterval 0.2). */
	inline constexpr float FollowRepathSeconds = 0.1f;
	/** A leader "step" longer than this in one tick is a teleport, not a walk (the v2 hysteresis gets 0), cm. */
	inline constexpr float MaxStepDistanceCm = 150.f;
	/** A leader turned further than this in one tick was set (a teleport, a load, an ordered facing), not turned by his legs, deg. */
	inline constexpr float MaxTurnStepDeg = 60.f;
	/** The automatic wall layout is computed again once the leader stands this far from where it was computed, cm. */
	inline constexpr float WallLayoutRecomputeCm = 50.f;

	/** Console Codex.Formation.Sectors != 0 (default 1). */
	CODEXTACTICS_API bool IsEnabled();

	/** Console Codex.Formation.CoverAllocator != 0 (default 1; 0 = the v2.1 LookYaw tables on the move, an A/B switch). */
	CODEXTACTICS_API bool IsCoverAllocatorEnabled();

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
	 * (v1.2 fires at_wall only within classify.arrive_dist of his destination; the stop is still waited for.) bBlizzard (v1.2): open
	 * ground in a blizzard -> the blizzard tight diamond.
	 */
	CODEXTACTICS_API EFormationKind ChooseFormation(EFormationTerrainClass Class, int32 MemberCount, bool bLeaderStopped, EFormationKind Previous,
		bool bBlizzard = false);

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

	/** The lead never reaches further than this past the slot (a fast or jumping slot must not send him through a fence / round a corner), cm. */
	inline constexpr float LeadMaxCm = 120.f;

	/** Where a formation move aims: the slot plus its velocity x Lead, at most LeadMaxCm past it (the slot itself while it stands still). */
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

	/** Sim REASSIGN_GAIN (contract 11.6): the followers swap places only when that saves at least this much total travel, cm. */
	inline constexpr float ReassignGainCm = 100.f;

	/**
	 * Places, not fixed slots (contract 10.6 / 11.6, rules v2.2): Positions[i] = follower i, CurrentPlace[i] = the place (follower slot index)
	 * he holds, Places[p] = where place p stands now. The minimal-travel assignment (AssignByTravel, <= 4 places) replaces the current one
	 * when its total straight travel is at least GainCm shorter - e.g. after a turn the man nearest the rear place becomes the rear man who
	 * walks backwards. Returns the new place per follower, unset = keep (no cheaper assignment, bad input).
	 */
	CODEXTACTICS_API TOptional<TArray<int32>> ReassignPlaces(const TArray<FVector2D>& Positions, const TArray<int32>& CurrentPlace,
		const TArray<FVector2D>& Places, float GainCm = ReassignGainCm);

	/** Contract 11.2: how far back a place stands along the heading (-dot(slot - leader, heading)), cm - Allocate's FFormationCoverMember::Back. */
	CODEXTACTICS_API float PlaceBack(const FVector2D& Slot, const FVector2D& LeaderPos, float HeadingYaw);

	/**
	 * Rules v2.2 (contract 11.2): the patrol / halt looks come from the coverage allocator (FormationCoverRules::Allocate) with 3+ members,
	 * walking or halted, in every formation but the wall layout and the hedgehog ring; the pair, a run and the wall layout keep
	 * FormationSectorRules::LookYaw.
	 */
	CODEXTACTICS_API bool UsesCoverAllocator(EFormationKind Formation, EFormationMode Mode, int32 MemberCount);

	/**
	 * Contract 11.5 integration: the wall layout again after a slide - the 4th man (with 2: the pair's partner) keeps the end he is at when
	 * it has room for him (>= 2 x Spacing; the partner 1 x), else Layout's roomier end (re-choosing it would send him through the line).
	 * Adjusts the rooms so FormationWallRules::Layout puts him on bOnRight's side; 3 members: unchanged (Layout's places are fixed).
	 */
	CODEXTACTICS_API void KeepEndAfterSlide(float& RoomLeft, float& RoomRight, bool bOnRight, int32 MemberCount, float Spacing);

	/** The rear guard eases his look towards his catch-up only this far off his slot (a settled rear guard keeps the allocator's look), cm. */
	inline constexpr float RearCatchUpCm = 100.f;
	/** ... and keeps his move this far under BackwardAngleDeg off his look (his body lags the look), deg. */
	inline constexpr float RearCatchUpMarginDeg = 15.f;

	/**
	 * Rules v2.2 runtime: the rear guard catching up to his slot (a formation switch moved it metres sideways) turns his look within his
	 * rear band - HeadingYaw + 180 +- BandHalfDeg (FormationCoverRules: fov / 2 - RearSector, so 5-7 o'clock stays in his cone) - to the
	 * yaw nearest LookYaw from which his move (MoveYaw) is not walking backwards (<= BackwardAngleDeg - RearCatchUpMarginDeg off it): sideways he then
	 * catches up at the catch-up speed instead of the backward cap (walk x move.backward_mult). A move the band cannot face (straight
	 * forward) keeps LookYaw - he walks backwards, as designed.
	 */
	CODEXTACTICS_API float RearGuardCatchUpLook(float LookYaw, float HeadingYaw, float BandHalfDeg, float MoveYaw);

	/** How long a leader's own facing is remembered after another member was made the leader (a brief switch away and back), s. */
	inline constexpr float NewLeaderRememberSeconds = 10.f;
	/** A pending order's destination closer than this gives no direction (he stands on it), cm. */
	inline constexpr float NewLeaderOrderMinCm = 50.f;

	/** What decides the look of a member who just became the leader (NewLeaderLookYaw); unset = does not apply. */
	struct FNewLeaderLookInputs
	{
		/** He walks: his travel direction. */
		TOptional<float> TravelYaw;
		/** An order of his own is pending / running: the direction to its destination. */
		TOptional<float> PendingOrderYaw;
		/** His own facing when he last stopped being the leader (within NewLeaderRememberSeconds). */
		TOptional<float> FacingAsLeaderYaw;
		/** The squad's formation heading before the switch. */
		TOptional<float> SquadHeadingYaw;
		/** His facing now (the follower look he may still hold). */
		float FacingYaw = 0.f;
	};

	/**
	 * Bug 2026-10-26 (TutorialSmoke; the coordinator: "the leader's sector is forward (12) by the contract"): a member made the leader keeps
	 * no follower look (the rear guard's 6 o'clock). His look on becoming the leader - the formation heading the squad then turns round:
	 * walking, his travel; else the order he has pending; else, standing, his own facing from when he last led (a switch away and back);
	 * else the squad's heading; only without any of them his facing now.
	 */
	CODEXTACTICS_API float NewLeaderLookYaw(const FNewLeaderLookInputs& In);

	/** A slide's room probe along the wall: far enough for the destination (AlongCm) plus the line's ends, at least RoomMaxCm, at most 30 m. */
	CODEXTACTICS_API float SlideRoomCm(float AlongCm, float Spacing);

	// --- The squad hugging a wall (game designer's play-test bug 2026-10-27, the lake yard) -------------------------------------------

	/** The wall pose's glide onto the exact wall slot (WallSlotGlide): this fast, cm/s; done within this, cm. */
	inline constexpr float WallSlotGlideSpeedCm = 150.f;
	inline constexpr float WallSlotGlideDoneCm = 1.f;

	/** One object along a formation wall trace (USquadSubsystem::TraceFormationWall): where on the ray (0..1) and what it is. */
	struct FWallTraceHit
	{
		float Time = 1.f;
		/** Its collision blocks pawns (a wall, a crate); an overlap-only box (a tutorial trigger volume) does not. */
		bool bBlocksPawn = false;
		/** It is a character / vehicle (never a wall for the formation). */
		bool bPawn = false;
	};

	/**
	 * Bug 2026-10-27: the wall the formation reads along a ray - the nearest object that blocks pawns and is no pawn (as
	 * GatherFormationWalls). An overlap-only box is walked through: from inside the lake yard's TutorialTriggerVolume a plain object trace
	 * hit it at distance 0, and the wall layout read the wall at the clicked slot. INDEX_NONE when none.
	 */
	CODEXTACTICS_API int32 NearestWallHit(const TArray<FWallTraceHit>& Hits);

	/**
	 * Bug 2026-10-27: one frame of a member settled in the wall pose easing onto his wall slot (the slot is the face offset of one operative in
	 * cover; the path follower stopped AcceptanceRadius / StopRadiusCm short of it, a hand-span off the wall): the planar move towards Slot,
	 * at most SpeedCmPerSec * DeltaTime and never past it. Unset when he is within WallSlotGlideDoneCm (on it) or farther than MaxCm (not at
	 * his slot: the walk brings him).
	 */
	CODEXTACTICS_API TOptional<FVector2D> WallSlotGlide(const FVector2D& Here, const FVector2D& Slot, float MaxCm, float DeltaTime,
		float SpeedCmPerSec = WallSlotGlideSpeedCm);

	/** A wall slide's members AHEAD of the leader keep this much farther ahead of him than the layout's spacing while he walks, cm. */
	inline constexpr float SlideAheadClearCm = 40.f;

	/**
	 * Bug 2026-10-27 (found by FormationCornerPeekSmoke once the squad hugs the wall exactly): on a wall slide the places move with the
	 * leader's progress, so a lead man parked within the stop radius short of his place stood in the leader's way on the wall line, the
	 * crowd avoidance slowed the leader to a stop - and the places stopped with him. The point the slide rule is asked with for the members
	 * ahead of the leader: his position advanced ByCm along K0 -> LeaderTarget (the slide's travel), its progress capped at LeaderTarget.
	 */
	CODEXTACTICS_API FVector2D SlideLeaderAhead(const FVector2D& LeaderPos, const FVector2D& K0, const FVector2D& LeaderTarget, float ByCm = SlideAheadClearCm);

	/**
	 * The wall slide's places with the members ahead of the leader kept clear of him: BaseTargets (FormationWallRules::Slide with the leader's
	 * position) with each member whose START place lies ahead of K0 along the travel (K0 -> LeaderTarget) taking his place from AheadTargets
	 * (the same rule asked with SlideLeaderAhead); the leader and the trailing members unchanged. All arrays in StartSlots' order.
	 */
	CODEXTACTICS_API TArray<FFormationSlot> SlideTargetsClearOfLeader(const TArray<FFormationSlot>& StartSlots, const TArray<FFormationSlot>& BaseTargets,
		const TArray<FFormationSlot>& AheadTargets, const FVector2D& LeaderTarget);

	/** Yield to the leader's own walk: a body within this of his straight way ahead is in it (two capsules + a margin), cm. */
	inline constexpr float YieldCorridorCm = 80.f;
	/** ... the follower steps this far off the way, cm. */
	inline constexpr float YieldClearCm = 120.f;
	/** ... the way ahead is looked at this far (his walk's next stretch), cm. */
	inline constexpr float YieldAheadCm = 600.f;

	/**
	 * Bug 2026-10-28 (CoverSmoke since the formation stays on in the fight): the leader's own walk (a cover / move order) along a wall ended
	 * Blocked at velocity 0 - a formation follower stood on his straight way (the nav mesh ignores bodies). When the follower (Here) or the
	 * place he walks to (Target) lies on the leader's way ahead (Leader -> Destination, at most AheadCm, within CorridorCm of it), the points
	 * he may step aside to: beside the in-the-way point, ClearCm off the way - his own side first, then the other. Empty: not in the way.
	 */
	CODEXTACTICS_API TArray<FVector2D> YieldFromLeaderWay(const FVector2D& Leader, const FVector2D& Destination, const FVector2D& Here,
		const FVector2D& Target, float CorridorCm = YieldCorridorCm, float ClearCm = YieldClearCm, float AheadCm = YieldAheadCm);
}
