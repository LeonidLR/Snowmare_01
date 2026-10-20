#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"
#include "Tactics/CoverWrapRules.h"

/**
 * Wall-edge cover switch (game designer 2026-10-20; UE-only, no Godot reference): an operative in cover at a wall EDGE (an exposed
 * corner of his slot within EdgeReachCm) double-clicks ANOTHER cover within MaxRangeCm: he dashes there (out of cover, a normal
 * sprint) and arrives through the M4 Cover Pack's run-in transition (std_jog_fwd[_L|_R]_to_cvr_{std|crch}_idle_{L|R}) instead of
 * the stand-still enter clip. A single click / a slow second click stays the ordinary cover order; a double-click while not at an
 * edge (or out of range) is the ordinary running cover order as before.
 *
 * The pack's transitions (measured 2026-10-20, Scripts/Editor/measure_cover_switch_clips.py, root bone, no root motion):
 *  - std_jog_fwd_to_cvr_*_idle_{L|R}: the jog comes STRAIGHT at the wall (starts 4.6 / 5.5 m out along the wall normal);
 *  - std_jog_fwd_L_to_cvr_*: the jog runs along the wall line from the pack's L side = the operative's own RIGHT as he ends with
 *    his back to the wall (+FCoverSlot::RightTangent; starts 4.2-5.2 m away), turns and settles;
 *  - std_jog_fwd_R_to_cvr_*: the same from his own LEFT;
 *  - the suffix _L / _R is the cover idle he ends in = the side he faces along the wall (CoverFacingRules::ClipIndex: Right -> _L).
 * Only the last metres are played: the montage starts where the clip's body is nearest to the runner (ChooseClipStart), so the
 * clip's jog blends with the real run and the turn into the wall is the clip's.
 * Pure rules, tested in CodexTactics.Tactics.CoverSwitch.*.
 */
struct CODEXTACTICS_API FCoverSwitchConfig
{
	/** Off: a double-click on another cover is always the ordinary (running) cover order. */
	bool bEnabled = true;
	/** The target slot lies at most this far from his slot (2D), cm. */
	float MaxRangeCm = 800.f;
	/** ... and at least this far (closer: the ordinary cover order), cm. */
	float MinRangeCm = 120.f;
	/** He stands at the wall edge when an exposed edge of his slot lies within this (FCoverFacingConfig::CornerReachCm), cm. */
	float EdgeReachCm = 100.f;
};

/** Why a double-click on another cover is (not) a switch. */
enum class ECoverSwitchVerdict : uint8
{
	Switch,
	Disabled,
	NotInCover,
	/** Side-stepping along his wall: not standing at the edge yet. */
	Shimmying,
	/** In cover but no exposed edge of his slot within EdgeReachCm. */
	NotAtEdge,
	/** The click is on his own wall (a shimmy / corner peek, never a switch). */
	SameWall,
	TooClose,
	OutOfRange,
	/** No valid target slot. */
	NoTarget
};

/** Which way the run-in transition comes in, in the pack's naming (_L = from the operative's own right as he ends at the wall). */
enum class ECoverSwitchApproach : uint8
{
	/** std_jog_fwd_to_cvr_*: straight at the wall. */
	Front,
	/** std_jog_fwd_L_to_cvr_*: along the wall from his own right (+RightTangent). */
	FromPackL,
	/** std_jog_fwd_R_to_cvr_*: along the wall from his own left. */
	FromPackR
};

/**
 * The run-in clip's body (its root bone) over time, as a world-space XY offset from where it ends (= the cover slot), sampled
 * (Times ascending, same length as Offsets).
 */
struct CODEXTACTICS_API FCoverSwitchTrack
{
	TArray<float> Times;
	TArray<FVector2D> Offsets;

	bool IsValid() const { return Times.Num() >= 2 && Times.Num() == Offsets.Num(); }
	float Length() const { return Times.Num() > 0 ? Times.Last() : 0.f; }
};

/**
 * Corner-to-corner switch (game designer 2026-10-20, second pass): the corner of the other cover he dashes to (ChooseSwitchCorner). The
 * slot stands SlotPoint (OffsetCm off the face, the stand-off of Facing short of the corner); Facing = the side of that corner along the
 * face, i.e. he arrives facing back along the new wall towards the open side he came over, ready to peek round that corner.
 */
struct CODEXTACTICS_API FCoverSwitchCorner
{
	int32 Edge = INDEX_NONE;
	int32 Vertex = INDEX_NONE;
	FVector2D Corner = FVector2D::ZeroVector;
	FVector2D SlotPoint = FVector2D::ZeroVector;
	FVector2D Normal = FVector2D::ZeroVector;
	ECoverFacing Facing = ECoverFacing::Right;
	float DistanceCm = 0.f;

	bool IsValid() const { return Edge != INDEX_NONE; }
};

/** Where to start which run-in clip (ChooseClipStart). Approach INDEX_NONE = no usable clip. */
struct CODEXTACTICS_API FCoverSwitchClipStart
{
	int32 Approach = INDEX_NONE;
	float StartSeconds = 0.f;
	/** The clip body's offset from the slot at StartSeconds (world XY), cm. */
	FVector2D ClipOffset = FVector2D::ZeroVector;
	/** Distance between the runner and the clip body at StartSeconds, cm (eased out by the run-in glide). */
	float MismatchCm = 0.f;
};

namespace CoverSwitchRules
{
	/** Number of run-in clips per stance: 3 approaches x 2 end sides (ClipArrayIndex). */
	inline constexpr int32 ClipCount = 6;

	/**
	 * The exposed edge of Current within ReachCm (EdgeDistance >= 0 and <= ReachCm). With both edges that close, the one whose
	 * direction along the wall points more towards Toward. False when he stands at no edge.
	 */
	CODEXTACTICS_API bool FindSwitchEdge(const FCoverSlot& Current, const FVector& Toward, float ReachCm, ECoverFacing& OutEdge);

	/**
	 * Is a double-click on Target a switch? In cover (not shimmying) at an edge (FindSwitchEdge), Target on another wall
	 * (bSameWall false) and MinRangeCm..MaxRangeCm from his slot (2D). The first failing rule in the enum's order is returned.
	 */
	CODEXTACTICS_API ECoverSwitchVerdict Evaluate(bool bInCover, bool bShimmying, const FCoverSlot& Current, const FCoverSlot& Target, bool bSameWall,
		const FCoverSwitchConfig& Config = FCoverSwitchConfig());

	/** Log / smoke name of a verdict. */
	CODEXTACTICS_API const TCHAR* VerdictName(ECoverSwitchVerdict Verdict);

	/**
	 * The confirm click on the cover ghost: a switch only for a double-click with a Switch verdict and the command not locked;
	 * everything else is the ordinary cover order (unchanged input rules).
	 */
	CODEXTACTICS_API bool IsSwitchConfirm(bool bDoubleClick, ECoverSwitchVerdict Verdict, bool bLocked = false);

	/**
	 * Geometric approach class of a run from From into Target (2D): straight at the wall when the component along the wall
	 * normal (out in front of it) is at least the along-wall component, else from the side the run comes from (+RightTangent =
	 * his own right = FromPackL). Used for the preview / log and when the clips' tracks are not available.
	 */
	CODEXTACTICS_API ECoverSwitchApproach ClassifyApproach(const FCoverSlot& Target, const FVector& From);

	/** Index into the 6-clip arrays: Approach * 2 + CoverFacingRules::ClipIndex(ArrivalFacing). */
	CODEXTACTICS_API int32 ClipArrayIndex(ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing);

	/** The pack clip's asset name, e.g. "anim_M4_std_jog_fwd_L_to_cvr_std_idle_L" (logs, smokes, the default clip paths). */
	CODEXTACTICS_API FString ClipName(ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, bool bCrouch);

	/** Track offset at Time (linear between the samples, clamped to the ends). */
	CODEXTACTICS_API FVector2D OffsetAt(const FCoverSwitchTrack& Track, float Time);

	/**
	 * Which clip and where to start it: over every valid track (index = approach) and every sample that leaves at least
	 * MinClipLeftSeconds of the clip and whose body moves the way he runs (its direction there within acos(MinHeadingDot) of
	 * RunnerDirection; a zero RunnerDirection = any), the one whose body lies nearest to RunnerOffset (the runner's offset from the
	 * slot, world XY). The heading rule keeps a clip that comes in from the other side out even when its body happens to be close
	 * (a runner already level with the slot). Approach INDEX_NONE when no sample qualifies.
	 */
	/**
	 * Corner-to-corner switch (game designer 2026-10-20: "from the corner I stand at, dash to the NEAREST corner of the other cover,
	 * arriving in the mirrored pose"): over the faces of Target (a counter-clockwise footprint) on the protected side - their outward normal
	 * within acos(MinNormalDot) of ProtectedNormal (his own wall's normal: the side the threat is not on) - and both their convex ends, the
	 * corner slot nearest to From (2D). No face on that side: the clicked face ClickedEdge only. A tie keeps the clicked face. Each slot
	 * stands OffsetCm off the face and StandOffCm(facing) short of its corner (never past the face's middle). False when none.
	 */
	CODEXTACTICS_API bool ChooseSwitchCorner(const FCoverFootprint& Target, const FVector2D& From, const FVector2D& ProtectedNormal, int32 ClickedEdge,
		float OffsetCm, TFunctionRef<float(ECoverFacing)> StandOffCm, FCoverSwitchCorner& OutCorner, float MinNormalDot = 0.5f);

	/** The other side (the mirrored pose: his left shoulder to the old wall -> his right shoulder to the new one). */
	inline ECoverFacing MirrorFacing(ECoverFacing Facing) { return Facing == ECoverFacing::Left ? ECoverFacing::Right : ECoverFacing::Left; }

	/**
	 * The side he faces on arriving at a switch slot: the nearest exposed edge of Slot within ReachCm (the corner he was sent to); none ->
	 * Fallback.
	 */
	CODEXTACTICS_API ECoverFacing ArrivalFacing(const FCoverSlot& Slot, float ReachCm, ECoverFacing Fallback);

	CODEXTACTICS_API FCoverSwitchClipStart ChooseClipStart(const TArray<FCoverSwitchTrack>& TracksByApproach, const FVector2D& RunnerOffset,
		float MinClipLeftSeconds, const FVector2D& RunnerDirection = FVector2D::ZeroVector, float MinHeadingDot = 0.5f);
}
