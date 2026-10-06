#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

/**
 * Facing along the wall in cover (user design rule 2026-10-06, replaces the Sprint 12 "back to the wall, face out"
 * facing; UE-only, no Godot reference). The operative keeps his back against the wall and faces ALONG it towards the
 * side of the last known threat: a shimmy towards that side is a forward side-step (pack clip cvr_*_walk_fwd_loop), a
 * shimmy away from it walks backwards still facing the threat (cvr_*_walk_bwd_loop). When the threat changes side he
 * turns to the other side and forward / backward swap. Threat source: the priority target, else the nearest visible
 * enemy, else the nearest heard enemy / ghost silhouette; none known -> the nearest exposed edge. The threat is
 * projected onto the wall tangent; it must lie HysteresisCm past the slot on the other side to flip (no flicker).
 * At an exposed edge on the facing side he takes the corner pose (cvr_*_look_at_idle); an edge a little further
 * is walked to automatically. Pure rules, tested in CodexTactics.Tactics.Cover.ShimmyFacesThreat / ThreatSideFlipHysteresis
 * / CornerPoseAndSnap / ThreatPriority.
 */
struct CODEXTACTICS_API FCoverFacingConfig
{
	/** The threat must lie this far along the wall on the other side before he turns round, cm. */
	float ThreatSideHysteresisCm = 75.f;
	/** Within this distance of the exposed edge on the facing side he stands at the corner (corner pose), cm. */
	float CornerReachCm = 100.f;
	/** An exposed edge on the facing side up to this far away is walked to automatically on entry / turn, cm. */
	float AutoCornerSnapCm = 200.f;
	/** He stops this far short of the measured edge (the probes step 40 cm, the real edge lies within the last step), cm. */
	float CornerStandOffCm = 60.f;
	/** A shot from cover waits until the body faces along the wall within this, degrees. */
	float FacingToleranceDeg = 20.f;
	/** Threat re-evaluation interval, s. */
	float ThreatUpdateSeconds = 0.2f;
	/** Visible enemies beyond this are ignored as a threat, cm. */
	float MaxThreatDistanceCm = 5000.f;
};

/** How a threat is known (lower = stronger). */
enum class ECoverThreatSource : uint8
{
	/** The enemy the operative was ordered to shoot / is shooting (Ctrl + click, current combat target). */
	PriorityTarget,
	/** Seen right now. */
	Visible,
	/** Heard / remembered (ghost silhouette at the last known spot). */
	Heard
};

struct CODEXTACTICS_API FCoverThreatCandidate
{
	FVector Location = FVector::ZeroVector;
	ECoverThreatSource Source = ECoverThreatSource::Visible;
	float DistanceCm = 0.f;
};

namespace CoverFacingRules
{
	/** Position of Threat along the wall from the slot, cm (positive = Right side, i.e. along FCoverSlot::RightTangent). */
	CODEXTACTICS_API float ThreatAlongWall(const FCoverSlot& Slot, const FVector& Threat);

	/**
	 * Side to face for a threat: the side of its along-wall projection; it must lie HysteresisCm past the slot on the
	 * side opposite Current to flip, otherwise Current stays (a threat straight ahead never flips him).
	 */
	CODEXTACTICS_API ECoverFacing ResolveThreatSide(const FCoverSlot& Slot, const FVector& Threat, ECoverFacing Current,
		float HysteresisCm = FCoverFacingConfig().ThreatSideHysteresisCm);

	/** No threat known: the nearest exposed edge; no exposed edge -> Current. */
	CODEXTACTICS_API ECoverFacing DefaultSide(const FCoverSlot& Slot, ECoverFacing Current);

	/**
	 * The threat that sets the facing: the priority target first, then the nearest visible enemy, then the nearest heard
	 * one (ghost). INDEX_NONE when empty.
	 */
	CODEXTACTICS_API int32 PickThreat(const TArray<FCoverThreatCandidate>& Candidates);

	/** Unit direction along the wall towards Side. */
	CODEXTACTICS_API FVector AlongWallDirection(const FCoverSlot& Slot, ECoverFacing Side);

	/** Actor yaw in cover: the wall normal (back to the wall) for either Side; Side only selects the _L/_R clip. */
	CODEXTACTICS_API float FacingYaw(const FCoverSlot& Slot, ECoverFacing Side);

	/** The actor yaw is within ToleranceDeg of FacingYaw (the wall normal). */
	CODEXTACTICS_API bool IsFacingAligned(float ActorYaw, const FCoverSlot& Slot, ECoverFacing Side, float ToleranceDeg);

	/** A shimmy in ShimmyDirection (+1 right, -1 left) is forward (towards the facing side) — else backwards. */
	CODEXTACTICS_API bool IsShimmyForward(ECoverFacing Facing, float ShimmyDirection);

	/** Distance to the exposed edge on Side, cm; negative when that side has no exposed edge in probe range. */
	CODEXTACTICS_API float EdgeDistance(const FCoverSlot& Slot, ECoverFacing Side);

	/** At the corner of the facing side: that edge is exposed and within CornerReachCm. */
	CODEXTACTICS_API bool IsAtCorner(const FCoverSlot& Slot, ECoverFacing Facing, const FCoverFacingConfig& Config = FCoverFacingConfig());

	/**
	 * The exposed edge on the facing side lies beyond the corner reach but within AutoCornerSnapCm: walk OutShiftCm
	 * along the wall towards it (edge distance - CornerStandOffCm).
	 */
	CODEXTACTICS_API bool ShouldSnapToCorner(const FCoverSlot& Slot, ECoverFacing Facing, float& OutShiftCm,
		const FCoverFacingConfig& Config = FCoverFacingConfig());

	/** Clip array index of a facing side: 0 = Left (pack *_L), 1 = Right (pack *_R). */
	CODEXTACTICS_API int32 ClipIndex(ECoverFacing Facing);
}
