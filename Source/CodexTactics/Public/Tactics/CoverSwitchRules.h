#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

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
	CODEXTACTICS_API FCoverSwitchClipStart ChooseClipStart(const TArray<FCoverSwitchTrack>& TracksByApproach, const FVector2D& RunnerOffset,
		float MinClipLeftSeconds, const FVector2D& RunnerDirection = FVector2D::ZeroVector, float MinHeadingDot = 0.5f);
}
