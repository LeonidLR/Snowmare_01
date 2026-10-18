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
 * At an exposed edge on the facing side he takes the corner pose: with a known / active threat (visible, heard, ordered
 * target, shot taken within FireReadyHoldSeconds) the FIRE-READY pose (cvr_*_fire_idle_L/R, entered through
 * cvr_*_idle_to_fire, left through cvr_*_fire_to_idle); with no threat the look-around pose (cvr_*_look_at_idle_L/R).
 * Clip side (measured 2026-10-06 from the pack's poses, user decision): the M4 pack names its sides as seen FACING the
 * wall (its enter clip starts facing it), so pack _L = the operative's own RIGHT hand as he stands back to the wall
 * (+RightTangent, ECoverFacing::Right; screen-left when he faces the default camera) and _R = his own left. Clip arrays
 * stay [_L, _R]: index 0 = ECoverFacing::Right, 1 = ECoverFacing::Left (ClipIndex). An edge a little further is walked
 * to automatically; he stops short of it by the stand-off of the clip side that plays there (the _L peek steps ~69 cm
 * out, the _R one ~40 cm, standing). Pure rules, tested in CodexTactics.Tactics.Cover.ShimmyFacesThreat /
 * ThreatSideFlipHysteresis / CornerPoseAndSnap / ThreatPriority / CornerStandOffPerClipSide / CornerShotOnlyBehindWall.
 */
struct CODEXTACTICS_API FCoverFacingConfig
{
	/** The threat must lie this far along the wall on the other side before he turns round, cm. */
	float ThreatSideHysteresisCm = 75.f;
	/** Within this distance of the exposed edge on the facing side he stands at the corner (corner pose), cm. */
	float CornerReachCm = 100.f;
	/** An exposed edge on the facing side up to this far away is walked to automatically on entry / turn, cm. */
	float AutoCornerSnapCm = 200.f;
	/**
	 * Corner stand-off: he stops this far short of the measured edge (the probes step 40 cm, the real edge lies within the
	 * last step), per pack clip side and stance = the peek clip's body (pelvis) step-out minus a 12 cm margin, so head and
	 * muzzle clear the edge even when the real edge lies right at the measured distance. Measured 2026-10-06: standing _L
	 * +69 cm (his right corner), _R 40 cm (his left); crouched _L 77 cm, _R 61 cm. cm.
	 */
	float StandStandOffPackLCm = 57.f;
	float StandStandOffPackRCm = 28.f;
	float CrouchStandOffPackLCm = 65.f;
	float CrouchStandOffPackRCm = 49.f;
	/** At the corner but further from the edge than the stand-off by more than this: step closer (on entry / turn), cm. */
	float CornerSnapToleranceCm = 15.f;
	/**
	 * Corner shot only at targets behind the wall: a target is "around the corner" when it lies beyond the exposed edge
	 * on its side and no further than this in front of the wall face, cm. Anything else on the open side gets a normal shot.
	 */
	float CornerShotFrontBandCm = 120.f;
	/** A shot from cover waits until the body faces along the wall within this, degrees. */
	float FacingToleranceDeg = 20.f;
	/** Threat re-evaluation interval, s. */
	float ThreatUpdateSeconds = 0.2f;
	/** Visible enemies beyond this are ignored as a threat, cm. */
	float MaxThreatDistanceCm = 5000.f;
	/** The fire-ready corner pose is held this long after the last threat sighting / shot, then it relaxes to the look-around pose, s. */
	float FireReadyHoldSeconds = 4.f;
};

/**
 * Manual corner peek by click (user request 2026-10-13, Demo 01 rev 5c): in high cover, a click on the exposed END of his own
 * wall (the last EdgeHitCm of its front face, its end face or its top there) sends the operative to that corner into the
 * corner-aim pose (leaning out, rifle up), held until another order. Tested in CodexTactics.Tactics.Cover.CornerPeekEdgeHit.
 */
struct CODEXTACTICS_API FCoverPeekConfig
{
	/** The click counts as the edge from this far in from the edge along the wall, cm. */
	float EdgeHitCm = 75.f;
	/** ... up to this far past it (the end face, the cursor slipping off the corner), cm. */
	float PastEdgeCm = 60.f;
	/** At most this far in front of the wall face (a click on the face itself is ~0), cm. */
	float MaxFrontCm = 60.f;
	/** At most this far behind the face (the wall's top / end face: its thickness), cm. */
	float MaxBehindCm = 250.f;
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

	/**
	 * A shimmy in ShimmyDirection (+1 right, -1 left) is forward (towards the facing side) — else backwards (still facing
	 * the threat). User rule 2026-10-06: with NO threat known (bThreatKnown false) he always moves face-forward in the
	 * direction of the move (moving left = walk_fwd_loop_L, right = walk_fwd_loop_R).
	 */
	CODEXTACTICS_API bool IsShimmyForward(ECoverFacing Facing, float ShimmyDirection, bool bThreatKnown = true);

	/**
	 * Facing once a shimmy in ShimmyDirection is ordered: a known threat keeps the side facing it; with no threat known
	 * the facing (idle clip side after the move) follows the movement direction.
	 */
	CODEXTACTICS_API ECoverFacing FacingForShimmy(ECoverFacing Current, float ShimmyDirection, bool bThreatKnown);

	/** Distance to the exposed edge on Side, cm; negative when that side has no exposed edge in probe range. */
	CODEXTACTICS_API float EdgeDistance(const FCoverSlot& Slot, ECoverFacing Side);

	/** At the corner of the facing side: that edge is exposed and within CornerReachCm. */
	CODEXTACTICS_API bool IsAtCorner(const FCoverSlot& Slot, ECoverFacing Facing, const FCoverFacingConfig& Config = FCoverFacingConfig());

	/**
	 * Stand-off from the exposed edge on the facing side, cm: the stand-off of the pack clip side that plays there
	 * (ClipIndex: facing Right -> _L, Left -> _R) for the stance.
	 */
	CODEXTACTICS_API float CornerStandOff(ECoverFacing Facing, bool bCrouched, const FCoverFacingConfig& Config = FCoverFacingConfig());

	/**
	 * The exposed edge on the facing side lies within AutoCornerSnapCm and he stands further from it than the clip side's
	 * stand-off (CornerStandOff) by more than CornerSnapToleranceCm: walk OutShiftCm along the wall towards it
	 * (edge distance - stand-off). Never walks away from the edge.
	 */
	CODEXTACTICS_API bool ShouldSnapToCorner(const FCoverSlot& Slot, ECoverFacing Facing, float& OutShiftCm,
		const FCoverFacingConfig& Config = FCoverFacingConfig(), bool bCrouched = false);

	/** Depth of Target in front of the wall face (along the wall normal from Slot.WallPoint), cm; negative = behind the wall. */
	CODEXTACTICS_API float DepthInFrontOfWall(const FCoverSlot& Slot, const FVector& Target);

	/**
	 * User decision 2026-10-06: the cover shot (corner lean / blind / over the top) is only for a target BEHIND the cover
	 * (past the wall face plane) or, at a high wall, around the corner: beyond the exposed edge on its side and no more than
	 * CornerShotFrontBandCm in front of the wall face (the lean-out line reaches it). A target out on the open side (in
	 * front of the wall, the operative's side) gets a normal shot: he steps off the wall, fires, and comes back.
	 */
	CODEXTACTICS_API bool ShouldCornerShot(const FCoverSlot& Slot, const FVector& Target, const FCoverFacingConfig& Config = FCoverFacingConfig());

	/**
	 * The fire-ready corner pose (cvr_*_fire_idle) is wanted: at the exposed edge on the facing side, not shimmying, and a
	 * threat was seen / ordered / shot at no longer than HoldSeconds ago. Else the corner pose is the look-around idle.
	 */
	CODEXTACTICS_API bool IsFireReady(bool bAtCorner, bool bShimmying, bool bHasThreat, float SecondsSinceThreat,
		float HoldSeconds = FCoverFacingConfig().FireReadyHoldSeconds);

	/**
	 * Clip index of a shimmy loop. The walk clips' _L / _R is the MOVEMENT direction along the wall in the pack's naming
	 * (walk_fwd_loop_L = face-forward towards his own right, walk_bwd_loop_L = backing towards his own right while facing
	 * his left): forward = towards the facing side = its index, backward = away from it = the other index (threat on his
	 * right: shimmy right = fwd_loop_L, shimmy left = bwd_loop_R).
	 */
	CODEXTACTICS_API int32 ShimmyClipIndex(ECoverFacing Facing, bool bForward);

	/**
	 * Where the fire stance (fire-ready pose, sustained aim) can be taken: at a high wall only at the exposed edge on the
	 * facing side (a lean round the corner); at a low cover anywhere along it (over the top; user request 2026-10-07).
	 */
	CODEXTACTICS_API bool IsFiringSpot(ECoverHeight Height, bool bAtCorner);

	/**
	 * The facing while engaged (user PIE video 2026-10-07: a hound pack swarming in front of his corner flipped the threat
	 * side shot by shot, so the fire stance jumped between fire_idle_L (+73 cm) and fire_idle_R (-40 cm) on every shot).
	 * Engaged (leaned out / fired within the hold), he keeps a facing whose edge is exposed and never turns to a closed
	 * side; with both edges exposed the flip waits until he is no longer engaged.
	 */
	CODEXTACTICS_API ECoverFacing KeepEngagedFacing(const FCoverSlot& Slot, ECoverFacing Current, ECoverFacing Wanted, bool bEngaged);

	/** Target lies beyond the exposed edge on the facing side (along the wall from the slot), at any depth. */
	CODEXTACTICS_API bool IsBeyondFacingEdge(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Target);

	/**
	 * The corner fire stance's aim direction (cvr_*_fire_idle, measured on the clips' barrel 2026-10-07): along the wall
	 * towards the facing side, turned OutwardDeg past it towards the space behind the wall (round the corner). 2D, unit.
	 */
	CODEXTACTICS_API FVector CornerAimDirection(const FCoverSlot& Slot, ECoverFacing Facing, float OutwardDeg);

	/**
	 * Corner hold at an exposed edge (user decision 2026-10-07): the target is fired at from the corner stance when it is
	 * behind the wall or beyond the facing edge AND within ReachDeg of the stance's aim direction seen from FireOrigin
	 * (ReachDeg = the upper-body twist limit + the aim cone). Anything else - the open side in front of the wall, a flank
	 * rush - is no corner shot: he steps off the wall and turns to it.
	 */
	CODEXTACTICS_API bool IsCornerHoldTarget(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& FireOrigin, const FVector& Target,
		float OutwardDeg, float ReachDeg);

	/**
	 * The corner hold is wanted (he steps out into the corner fire stance on his own and holds it): at the exposed edge of
	 * a high wall (bAtHoldSpot) AND in a fight or with a fresh known threat. Exploring with nothing known he hugs the wall
	 * with the look-around corner pose instead (user bug 2026-10-11: at the first wall of the level, no enemy anywhere, he
	 * stood stepped out past the edge aiming round it - "not hugging the wall, looking somewhere else"; the 2026-10-06
	 * clip rule: fire-ready pose only with a known threat, look-around without one).
	 */
	CODEXTACTICS_API bool WantsCornerHold(bool bAtHoldSpot, bool bInFight, bool bThreatFresh);

	/** How an enemy bears on the return from open shots to the corner hold (ClassifyOpenShotThreat). */
	enum class EOpenShotThreat : uint8
	{
		/** Farther than the rush distance (the older sight / out-of-sight rules decide). */
		None,
		/** Within the rush distance and engaged from out here (hittable), not covered by the corner, or on the move: he stays out. */
		Pins,
		/** Within the rush distance, waiting round the corner: covered by the corner stance, no shot from out here, standing still. */
		WaitsRoundCorner,
		/** The same right at his side (within CornerWaitReleaseCm): only the corner stance can fight it. */
		WaitsAtHisSide
	};

	/** A rusher faster than this (2D, cm/s) counts as on the move (ClassifyOpenShotThreat). */
	inline constexpr float RusherOnTheMoveCmPerSec = 100.f;

	/** An enemy waiting round the corner within this distance (2D, cm) is "at his side" (ClassifyOpenShotThreat). */
	inline constexpr float CornerWaitReleaseCm = 300.f;

	/** Classifies one enemy for the open-shot return (bHittableFromOpen: a line of fire from where he stands now). */
	CODEXTACTICS_API EOpenShotThreat ClassifyOpenShotThreat(float DistanceCm, float BreakCm, bool bCoveredByCorner, bool bHittableFromOpen,
		bool bOnTheMove);

	/**
	 * Off the wall for open shots from a corner hold: do the enemies within the rush distance keep him in the open stance?
	 * Yes when any of them pins him (one he engages from out here, one the corner does not cover, one on the move), or
	 * when the rest wait round the corner farther off (the 2026-10-07 behaviour: one step off the wall per engagement).
	 * No when none pins him and one waits right at his side round the corner - bug hunt 2026-10-18: a frostbitten
	 * stopped 1.7 m off behind the corner (with two spitters 6.8 m off behind the wall) kept him out for 40 s without a
	 * single shot; back at the corner he fights it from the lean-out.
	 */
	CODEXTACTICS_API bool OpenShotThreatsKeepHimOut(bool bAnyPins, bool bAnyWaitsRoundCorner, bool bAnyWaitsAtHisSide);

	/**
	 * Cover entry with a big turn: the actor turns its back to the wall at once and the mesh holds the old yaw while the
	 * enter clip (whose first frame faces the wall) blends in. Fraction of that turn still held on the mesh: 1 - the clip's
	 * weight while it plays (EnterWeight >= 0; 0 once it is fully in); 1 while the clip has not started yet (not seen,
	 * within AwaitSeconds of the entry); -1 when it is gone without having brought the body round (cut short by a shimmy,
	 * ended, never came): the caller eases out what is left over a fixed time. Never holds for good (user bug 2026-10-11:
	 * a cut clip left the mesh turned ~180 deg).
	 */
	CODEXTACTICS_API float EntryHeldYawFraction(float EnterWeight, bool bClipSeen, float SecondsSinceEntry, float AwaitSeconds = 0.25f);

	/**
	 * The point lies within the wall's span along it (between its exposed edges, MarginCm past an edge still counts; a
	 * closed side is unbounded). Past an exposed edge there is no wall behind the face plane: a bone there - the corner
	 * fire stance steps 40-73 cm past the edge and aims round it - is not "in the wall" (user bug 2026-10-11: the clearance
	 * push shoved the whole body ~30 cm off the wall at a corner).
	 */
	CODEXTACTICS_API bool IsWithinWallSpan(const FCoverSlot& Slot, const FVector& Point, float MarginCm = 0.f);

	/**
	 * Manual corner peek (rev 5c): the exposed edge of Current's high cover that a click at Click picks, if any. Edges come from
	 * Current (within its probe range) and, for a long wall, from Probe (a slot of the same wall traced near the click; may be
	 * null). OutEdgeAlongCm = the edge's position along the wall from Current.WorldLocation (+ = along RightTangent). The click
	 * must lie within FCoverPeekConfig::EdgeHitCm inside the edge .. PastEdgeCm past it, and no further than MaxFrontCm in front
	 * of / MaxBehindCm behind the wall face; the nearest qualifying edge wins. Rev 5e (2026-10-14): low cover peeks too (crouched
	 * round its end); a probe slot of another cover height is ignored.
	 */
	CODEXTACTICS_API bool FindPeekEdge(const FCoverSlot& Current, const FVector& Click, const FCoverSlot* Probe, ECoverFacing& OutSide,
		float& OutEdgeAlongCm, const FCoverPeekConfig& Config = FCoverPeekConfig());
	/** Where he stands for the peek, along the wall from the same origin as EdgeAlongCm: StandOffCm short of the edge, on its inner side. */
	CODEXTACTICS_API float CornerPeekStandAlong(ECoverFacing Side, float EdgeAlongCm, float StandOffCm);

	/**
	 * Rev 5d (user bug 2026-10-14): a click on the peek edge of HIS OWN wall wins over the "a click near an operative picks him"
	 * proximity rule (ACodexTacticsPlayerController::FindClickedMember, SelectRadius 120 cm): the edge he already stands at lies
	 * well inside that radius, so the near-edge click used to select him (nothing happened) while the hover showed the peek ghost.
	 * Only a click on a squad member's BODY (the hit actor is the member) still selects.
	 */
	CODEXTACTICS_API bool CornerPeekClickWins(bool bPeekEdgeOfOwnWall, bool bHitSquadBody);

	/** What OrderCornerPeek does (rev 5d). */
	enum class ECornerPeekOrder : uint8
	{
		/** He already stands at the corner: the corner aim starts where he is (no shimmy, no AP in turn-based). */
		PoseInPlace,
		/** Real time / tactical pause: shimmy the rest of the way to the stand point, then the corner aim. */
		ShimmyThenPose,
		/** Turn-based and not at that corner: refused ("move to the corner first"). */
		Refused,
	};

	/**
	 * Rev 5d: the corner peek order from where the body is. StandAlongCm / BodyAlongCm: the stand point / the body along the wall
	 * (same origin); bAtThatCorner: his cover slot already counts as at that corner (IsAtCorner on that side, the pose condition).
	 * In place = within CornerSnapToleranceCm + 10 of the stand point and not shimmying, or - turn-based, where grid cells do not
	 * line up with stand-offs - anywhere at that corner. Otherwise real time / pause shimmy there; turn-based refuses.
	 */
	CODEXTACTICS_API ECornerPeekOrder DecideCornerPeekOrder(float StandAlongCm, float BodyAlongCm, bool bShimmying, bool bAtThatCorner,
		bool bTurnBased, const FCoverFacingConfig& Config = FCoverFacingConfig());
	/** Clip array index of a facing side: 0 = Right (pack *_L = his own right, back to the wall), 1 = Left (pack *_R). */
	CODEXTACTICS_API int32 ClipIndex(ECoverFacing Facing);
}
