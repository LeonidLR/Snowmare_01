#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

class UWorld;

/**
 * Wall tracing for the cover system (Sprint 12-B; UE-only, no Godot reference — Gemini Sprint 12 spec). The pure
 * decisions (height class from the wall top / the chest and head traces, corner exposure from the edge probes, the
 * slot offset, same-wall test) are tested in CodexTactics.Tactics.Cover.*; FindCoverSlotAt runs the traces in the
 * world and projects the slot onto the navmesh.
 */
struct CODEXTACTICS_API FCoverTraceConfig
{
	/** Knee, chest and head heights of the horizontal traces above the ground, cm (the knee trace finds 60 cm covers). */
	float KneeHeightCm = 45.f;
	float ChestHeightCm = 90.f;
	float HeadHeightCm = 170.f;
	/** Wall tops below this are low cover, cm. */
	float LowCoverMaxCm = 130.f;
	/** Wall tops from this up are high cover, cm (130-180 counts as low — see ECoverHeight). */
	float HighCoverMinCm = 180.f;
	/** Anything lower than this is no cover at all, cm. */
	float MinCoverHeightCm = 40.f;
	/** The operative's feet stand this far off the wall, cm (directive: 40-45). */
	float SlotOffsetCm = 45.f;
	/** Corner probes step along the wall this far, cm, up to MaxEdgeProbes steps. */
	float EdgeProbeStepCm = 40.f;
	int32 MaxEdgeProbes = 5;
	/** Bisection steps refining the edge inside the bracketing 40 cm step (4: within 2.5 cm; user decision 2026-10-07). */
	int32 EdgeRefineIterations = 4;
	/** A click further than this from a wall surface finds no cover, cm. */
	float MaxWallDistanceCm = 160.f;
	/** Walls steeper than this (|normal.Z| below it) count; ramps / floors do not. */
	float MaxWallNormalZ = 0.3f;
	/** Navmesh projection extent for the slot. */
	FVector NavProjectExtent = FVector(60.f, 60.f, 150.f);
	/** Two slots are on the same wall when their normals agree this much and their planes lie this close, cm. */
	float SameWallNormalDot = 0.9f;
	float SameWallPlaneDistanceCm = 40.f;
};

namespace CoverTraceRules
{
	/** Height class of a wall WallTopCm high above the ground. */
	CODEXTACTICS_API ECoverHeight ClassifyHeight(float WallTopCm, const FCoverTraceConfig& Config = FCoverTraceConfig());

	/** Height class from the horizontal traces alone (no wall top measured): chest + head = high, knee or chest only = low. */
	CODEXTACTICS_API ECoverHeight ClassifyFromTraces(bool bChestHit, bool bHeadHit, bool bKneeHit = true);

	/** Feet position of the slot: WallPoint pushed OffsetCm along the (planar) normal, at the ground height given. */
	CODEXTACTICS_API FVector ComputeSlotLocation(const FVector& WallPoint, const FVector& WallNormal, float OffsetCm, float GroundZ);

	/**
	 * Corner exposure from the probes along one side (ProbeHits[i] = the i-th probe, (i + 1) * StepCm from the slot,
	 * still hit the wall). Exposed when a probe misses; OutEdgeDistanceCm = the distance of the first miss (0 when not).
	 */
	CODEXTACTICS_API bool EdgeExposedFromProbes(const TArray<bool>& ProbeHits, float StepCm, float& OutEdgeDistanceCm);

	/**
	 * Refines a coarse edge: the wall is there at HitCm (the last probe on it), not at MissCm (the first probe off it).
	 * Bisects Iterations times with WallAt(cm) and returns the middle of the final bracket: the edge distance within
	 * (MissCm - HitCm) / 2^(Iterations + 1). Tested in CodexTactics.Tactics.Cover.EdgeProbePrecision.
	 */
	CODEXTACTICS_API float RefineEdgeDistance(float HitCm, float MissCm, TFunctionRef<bool(float)> WallAt, int32 Iterations);

	/** The two slots lie on one wall (parallel, same plane) — a click there is a shimmy, not a new cover. */
	CODEXTACTICS_API bool IsSameWall(const FCoverSlot& A, const FCoverSlot& B, const FCoverTraceConfig& Config = FCoverTraceConfig());

	/** Signed distance of Point along A's wall tangent (positive = to the operative's right), cm. */
	CODEXTACTICS_API float AlongWallDistance(const FCoverSlot& A, const FVector& Point);

	/** The corner the operative should work: the exposed edge towards the threat; the only exposed one; else Right. */
	CODEXTACTICS_API ECoverFacing ChooseFacing(const FCoverSlot& Slot, const FVector* ThreatLocation);

	/** The slot a wall point gets for a man facing along the normal (the wall behind him); yaw of that facing. */
	CODEXTACTICS_API float FacingYaw(const FCoverSlot& Slot);

	/**
	 * Finds the cover slot at a clicked wall point: knee- and chest-high traces along TraceDirection (planar; the camera's
	 * view direction or towards the wall) from 80 cm short of the point find the wall and its normal; the head trace and a
	 * downward trace on the wall top classify it; probes left / right find the corners; the slot is projected onto the
	 * navmesh. Pawns and silhouettes never count as walls. False when nothing cover-like is there.
	 */
	CODEXTACTICS_API bool FindCoverSlotAt(UWorld* World, const FVector& CursorHitPoint, const FVector& TraceDirection, FCoverSlot& OutSlot,
		const FCoverTraceConfig& Config = FCoverTraceConfig(), const TArray<const AActor*>& IgnoredActors = TArray<const AActor*>());

	/**
	 * A shimmy target on the same wall as Current: the click projected onto the wall line, traced again (the wall may
	 * end before it). False when the click leaves the wall.
	 */
	CODEXTACTICS_API bool FindShimmySlot(UWorld* World, const FCoverSlot& Current, const FVector& ClickPoint, FCoverSlot& OutSlot,
		const FCoverTraceConfig& Config = FCoverTraceConfig(), const TArray<const AActor*>& IgnoredActors = TArray<const AActor*>());
}
