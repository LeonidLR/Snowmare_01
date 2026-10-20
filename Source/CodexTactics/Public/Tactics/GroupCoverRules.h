#pragma once

#include "CoreMinimal.h"

/**
 * Group cover order (game designer 2026-10-20; UE-only, no Godot reference): a cover click given to a GROUP (a box selection of
 * two or more, or group mode = the formation follows the leader, Solo off) sends every member to the same cover object. The
 * direct-order operative (the leader, gold ring - the one the cover ghost was shown for) takes the clicked slot; the others get
 * the nearest free slots along the SAME wall on the SAME side, shoulder to shoulder (SlotSpacingCm); who does not fit (a short
 * barricade) goes PRONE directly behind the cover, facing it. Reverses the 2026-10-11 decision "no automatic cover stacking for
 * followers" for GROUP orders only (Solo / a single selection: only that operative, as before).
 *
 * Pure rules (tested in CodexTactics.Tactics.GroupCover.*): where the probes go along the wall, which probed slots are usable,
 * which are chosen, the cover's extent, the prone spots behind it and the minimal-travel assignment. The world part (the wall
 * probes = CoverTraceRules::FindShimmySlot, the navmesh, the orders) is ACodexTacticsPlayerController::OrderGroupCover
 * (CodexTacticsPlayerControllerCover.cpp) and USquadSubsystem (the prone arrivals / hold).
 */
struct CODEXTACTICS_API FGroupCoverConfig
{
	/**
	 * Slots along the wall are this far apart, cm: shoulder to shoulder - in cover an operative faces ALONG the wall with his
	 * rifle, so a closer neighbour would stand in his muzzle.
	 */
	float SlotSpacingCm = 100.f;
	/** A companion slot needs at least this much wall beyond it on its outer side, cm (else his body sticks out past the end). */
	float MinEdgeClearanceCm = 30.f;
	/** A probed slot may lie this far from its planned place along the wall (navmesh projection), cm; further = no slot there. */
	float AlongToleranceCm = 35.f;
	/** Probes per side beyond the members still to place (occupied slots are skipped, not counted). */
	int32 ExtraProbesPerSide = 2;
	/** A slot / prone spot is taken when another body (not of this order) stands this close, cm. */
	float OccupiedRadiusCm = 60.f;
	/**
	 * Prone spots: the body's centre this far off the wall face, cm. Behind the one at the wall (his slot is 45 cm off it) with
	 * room for the lying body; a 60 cm barricade still hides a prone body there from an enemy more than ~7 m away (SightRules:
	 * eye 160 cm, prone profile 25 cm).
	 */
	float ProneDepthCm = 170.f;
	/** Prone spots lie side by side this far apart, cm. */
	float ProneSpacingCm = 75.f;
	/** Prone spots keep this far inside the cover's ends when the cover is wide enough, cm. */
	float ProneEdgeMarginCm = 15.f;
	/** The cover's extent beyond the clicked slot on a side whose corner probes found no end, cm (CoverTraceRules probes 5 x 40). */
	float UnknownReachCm = 200.f;
	/** The prone order is "arrived" within this distance of its spot, cm. */
	float ProneArriveCm = 150.f;
	/** Stopped further than this from the spot (but within ProneRetryCm): one more walk onto it, cm. */
	float ProneSettleCm = 30.f;
	float ProneRetryCm = 400.f;
	/** Turning to face the cover before lying down ends within this angle or after this time. */
	float ProneFaceToleranceDeg = 25.f;
	float ProneFaceMaxSeconds = 1.2f;
};

/** Result of one probe along the wall. */
enum class EGroupCoverProbe : uint8
{
	/** A slot on the same wall, same side, free. */
	Free,
	/** A slot there, but another body (not of this order) stands in it: skipped, the scan goes on past it. */
	Occupied,
	/** No slot there (the wall ends, another object, too close to the end, off the navmesh): the scan on this side stops. */
	NoWall
};

/** Where one member of a group cover order goes. */
enum class EGroupCoverSpotKind : uint8
{
	/** Into a cover slot along the wall. */
	Slot,
	/** Prone behind the cover. */
	Prone
};

struct FGroupCoverAssignment
{
	EGroupCoverSpotKind Kind = EGroupCoverSpotKind::Slot;
	/** Index into the slot list / the prone spot list given to PlanAssignments; INDEX_NONE = nowhere (more members than places). */
	int32 Index = INDEX_NONE;
};

namespace GroupCoverRules
{
	/** Offset along the wall (positive = the slot's RightTangent) of the K-th probe (K >= 1) on Side (< 0 left, > 0 right), cm. */
	CODEXTACTICS_API float ProbeOffsetCm(int32 Side, int32 K, const FGroupCoverConfig& Config = FGroupCoverConfig());

	/** Probes per side for Others members still to place (Others + ExtraProbesPerSide). */
	CODEXTACTICS_API int32 ProbesPerSide(int32 Others, const FGroupCoverConfig& Config = FGroupCoverConfig());

	/**
	 * Classifies one probe: no slot / another wall / the wrong side / off its planned place (|AlongErrorCm| > AlongToleranceCm) /
	 * its outer end closer than MinEdgeClearanceCm (bOuterEdgeExposed + OuterEdgeCm) -> NoWall; else Occupied when a foreign
	 * body stands there, else Free.
	 */
	CODEXTACTICS_API EGroupCoverProbe ClassifyProbe(bool bSlotFound, bool bSameWall, float AlongErrorCm, bool bOuterEdgeExposed, float OuterEdgeCm,
		bool bOccupied, const FGroupCoverConfig& Config = FGroupCoverConfig());

	/**
	 * The free slot offsets of both sides (each side's probes ordered outward, probe K at ProbeOffsetCm(Side, K)): a side's scan
	 * stops at its first NoWall (the cover ends there: nobody is sent past a gap to another stretch), Occupied ones are skipped.
	 */
	CODEXTACTICS_API TArray<float> CollectFreeOffsets(const TArray<EGroupCoverProbe>& LeftProbes, const TArray<EGroupCoverProbe>& RightProbes,
		const FGroupCoverConfig& Config = FGroupCoverConfig());

	/**
	 * The Count free offsets nearest to the clicked slot (smallest |offset|: the group stays together, shoulder to shoulder); a
	 * tie goes to PreferSign's side (> 0 right, < 0 left; the side the members come from). Returned sorted ascending.
	 */
	CODEXTACTICS_API TArray<float> ChooseNearestOffsets(const TArray<float>& FreeOffsets, int32 Count, float PreferSign);

	/**
	 * The cover's extent along the wall, relative to the clicked slot, cm (X = min <= 0, Y = max >= 0): an exposed corner on a
	 * side ends it there; else it reaches at least UnknownReachCm and half a spacing past the farthest probe that found wall on
	 * that side (FarthestLeftWallCm <= 0, FarthestRightWallCm >= 0; 0 = none).
	 */
	CODEXTACTICS_API FVector2D ComputeCoverSpan(bool bLeftExposed, float LeftEdgeCm, bool bRightExposed, float RightEdgeCm,
		float FarthestLeftWallCm, float FarthestRightWallCm, const FGroupCoverConfig& Config = FGroupCoverConfig());

	/**
	 * Count prone spots directly behind the cover (X = along the wall from the clicked slot, Y = off the wall face, cm), side by
	 * side ProneSpacingCm apart. The row is centred on the clicked slot and shifted to stay ProneEdgeMarginCm inside the span;
	 * a span narrower than the row: centred on the span. Then Spare more spots, alternately outward past the row's ends
	 * (fallbacks when a spot is off the navmesh / taken). All at ProneDepthCm.
	 */
	CODEXTACTICS_API TArray<FVector2D> ComputeProneSpots(int32 Count, const FVector2D& Span, int32 Spare = 0,
		const FGroupCoverConfig& Config = FGroupCoverConfig());

	/**
	 * Minimal total travel: Result[i] = the index of the target member i goes to (each target at most once; INDEX_NONE when there
	 * are fewer targets than members - the members whose absence costs least). Exhaustive for up to 8 members x 8 targets (a
	 * minimal sum of straight lines never crosses itself), greedy nearest pairs beyond. Ties keep the lower indices.
	 */
	CODEXTACTICS_API TArray<int32> AssignMinimalTravel(const TArray<FVector2D>& Members, const TArray<FVector2D>& Targets);

	/**
	 * The whole plan for the members besides the direct-order operative: the first min(N, SlotPoints) members' worth of slots
	 * (SlotPoints = the chosen slots) plus the first N - that many PronePoints are the targets, assigned by minimal total travel.
	 */
	CODEXTACTICS_API TArray<FGroupCoverAssignment> PlanAssignments(const TArray<FVector2D>& Members, const TArray<FVector2D>& SlotPoints,
		const TArray<FVector2D>& PronePoints);
}
