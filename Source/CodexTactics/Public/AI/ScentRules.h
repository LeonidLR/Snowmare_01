#pragma once

#include "CoreMinimal.h"
#include "AI/PerceptionRules.h"
#include "ScentRules.generated.h"

/**
 * Frost hound scent (game designer 2026-10-18, user decisions 1 / 2 / 4; UE-only, no Godot reference; design
 * docs/design/STEALTH_LURE.md "Scent"). Walls block scent: every world-blocking surface between the hound's nose and the
 * operative multiplies the smell radius by SmellOcclusionPerWall (0.3: 6 m -> 1.8 m -> 0.5 m), the hearing model's
 * occlusion (PerceptionRules::OccludedRadius); a few offset traces are taken and the clearest one counts, so a doorway / gap
 * lets the scent through. Smell ALERTS, it does not detect: the hound stops and sniffs (SniffSeconds), then follows the scent
 * slowly to where the operative is; full detection only within SmellDetectCm in open air (or by its sight). Losing the scent
 * sends it to investigate the last scent point (a patrol search), then back to its route.
 * Pure rules, tested in CodexTactics.AI.Scent.*; numbers per archetype in Content/Data/AI/enemy_perception.json (FROST_HOUND).
 */

/** Where a hound is in its scent episode (exploration / stealth only; the fight never uses it). */
UENUM(BlueprintType)
enum class EHoundScentPhase : uint8
{
	/** No scent: its normal patrol / guard / search behaviour. */
	None,
	/** Stopped, nose up: it caught a scent (the HUD's scent marker, the feed line). */
	Sniffing,
	/** Walks slowly towards the scent point (updated while it still smells the operative). */
	Following
};

/** What one scent step asks the hound to do. */
enum class EHoundScentAction : uint8
{
	None,
	/** Caught a scent: stop and sniff (marker, feed line). */
	StartSniff,
	/** Sniffed long enough and still smells it: follow the scent. */
	StartFollow,
	/** Close in open air: the operative is detected (the patrol breaks: EPatrolAlertCause::Smell). */
	Detect,
	/** The scent is gone: investigate the last scent point (a patrol search), then back to the route. */
	LoseScent
};

/** Scent episode of one hound (owned by AEnemyCharacter). */
struct CODEXTACTICS_API FHoundScentState
{
	EHoundScentPhase Phase = EHoundScentPhase::None;
	/** Seconds in the current phase. */
	float PhaseSeconds = 0.f;
	/** Seconds since it last smelled the operative. */
	float SinceScentSeconds = 0.f;
	/** Where it last smelled the operative (the follow goal / the point a lost scent is investigated at). */
	FVector ScentPoint = FVector::ZeroVector;
};

/** What the hound's nose reports this perception step. */
struct CODEXTACTICS_API FHoundScentSense
{
	/** The scent reaches it (ScentRules::Smells: the radius cut by the walls in between). */
	bool bSmells = false;
	/** Close in open air (ScentRules::IsCloseDetection): detection. */
	bool bClose = false;
	/** Where the smelled operative is. */
	FVector Point = FVector::ZeroVector;
	/** Already on the trail (investigating a lost scent): a new scent is followed without a second sniff. */
	bool bSkipSniff = false;
};

namespace ScentRules
{
	/** Walls the scent passes, given the walls counted on each trace (the clearest trace counts; none = 0). */
	CODEXTACTICS_API int32 CombineTraceWalls(const TArray<int32>& WallsPerTrace);

	/** How far the scent reaches through Walls walls: SmellRadius x SmellOcclusionPerWall^Walls, cm (0 for a non-smeller). */
	CODEXTACTICS_API float ScentReachCm(const FEnemyPerceptionParams& Params, int32 Walls);

	/** The hound smells an operative DistanceCm away with Walls walls in between. */
	CODEXTACTICS_API bool Smells(const FEnemyPerceptionParams& Params, float DistanceCm, int32 Walls);

	/** Close enough in open air for the nose alone to detect him (no wall, within SmellDetectCm, and it can smell at all). */
	CODEXTACTICS_API bool IsCloseDetection(const FEnemyPerceptionParams& Params, float DistanceCm, int32 Walls);

	/**
	 * One perception step (DeltaSeconds) of the scent state machine: None -> Sniffing (caught a scent) -> Following (sniffed
	 * SniffSeconds and the scent is still there) -> Detect (close in open air, from any phase) or LoseScent (no scent for
	 * longer than ScentLostSeconds at the end of the sniff / while following, or following longer than ScentFollowMaxSeconds).
	 * State is updated in place (back to None on Detect / LoseScent, ScentPoint kept).
	 */
	CODEXTACTICS_API EHoundScentAction Step(const FEnemyPerceptionParams& Params, FHoundScentState& State, const FHoundScentSense& Sense, float DeltaSeconds);

	/** Cautious follow pace: PatrolWalkSpeed x Fraction, never above its normal speed, never below 50 cm/s. */
	CODEXTACTICS_API float GetFollowSpeed(float PatrolWalkSpeed, float NormalSpeed, float Fraction);

	/** The feed line when a hound catches a scent ("The hound caught a scent."). */
	CODEXTACTICS_API FText GetCaughtScentLine();
}
