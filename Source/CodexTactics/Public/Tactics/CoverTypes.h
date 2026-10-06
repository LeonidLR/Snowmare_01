#pragma once

#include "CoreMinimal.h"
#include "CoverTypes.generated.h"

/**
 * Tactical cover data (Sprint 12, TANDEM «SPRINT 12 DIRECTIVE: Full Tactical Cover System»; UE-only, no Godot
 * reference — Gemini Sprint 12 spec). High cover = walls / buildings an operative hugs back-to-wall; low cover = the
 * 60 cm barricades and other knee-high obstacles (Sprint 08 rules stay).
 */

/** Cover height class of a wall surface. */
UENUM(BlueprintType)
enum class ECoverHeight : uint8
{
	/** Not a cover (too low, or nothing there). */
	None,
	/**
	 * Below CoverTraceRules LowCoverMaxCm (130 cm): crouch behind it, stand to fire over it. A 130-180 cm wall is
	 * classed Low too (decision 2026-10-06: it hides a crouched man fully but not a standing one, so it plays as low
	 * cover — crouch idle, rise to fire over it).
	 */
	LowCover,
	/** HighCoverMinCm (180 cm) or taller: full-height wall, fire only round an exposed corner or blind. */
	HighCover
};

/** Which corner of the cover the operative works (lean / blind fire side). */
UENUM(BlueprintType)
enum class ECoverFacing : uint8
{
	Left,
	Right
};

/** How the operative fires from cover. */
UENUM(BlueprintType)
enum class ECoverFireMode : uint8
{
	/** Not in cover, or a low cover fired over normally. */
	Normal,
	/** Lean out of the corner (or rise over the low cover), aimed fire, head exposed for the moment. */
	CornerLean,
	/** Fire round the corner / over the top without exposing the head: -40 % accuracy, no headshots on him. */
	BlindFire
};

/** One wall-hugging position found by CoverTraceRules::FindCoverSlotAt. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FCoverSlot
{
	GENERATED_BODY()

	/** Where the operative's feet stand: on the navmesh, SlotOffsetCm (45 cm) off the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	FVector WorldLocation = FVector::ZeroVector;

	/** The point on the wall surface the slot was found at (chest height). */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	FVector WallPoint = FVector::ZeroVector;

	/** Outward normal of the wall (planar, unit): the operative faces along it, his back to the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	FVector WallNormal = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	ECoverHeight Height = ECoverHeight::None;

	/** Wall top above the slot's ground, cm (what classified the height). */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	float WallTopCm = 0.f;

	/** A free corner to the operative's left (facing away from the wall) within the probe range. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	bool bLeftEdgeExposed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	bool bRightEdgeExposed = false;

	/** Distance along the wall to the exposed corners, cm (0 when not exposed). */
	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	float LeftEdgeDistanceCm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cover")
	float RightEdgeDistanceCm = 0.f;

	/** The wall (barricade, wall mesh, building) when it is an actor. */
	TWeakObjectPtr<AActor> WallActor;

	bool IsValid() const { return Height != ECoverHeight::None; }

	/** Right-hand tangent of the wall for a man facing along the normal (Cross(Up, Normal)). */
	FVector RightTangent() const { return FVector::CrossProduct(FVector::UpVector, WallNormal).GetSafeNormal(); }

	bool HasExposedEdge() const { return bLeftEdgeExposed || bRightEdgeExposed; }
	bool IsEdgeExposed(ECoverFacing Facing) const { return Facing == ECoverFacing::Left ? bLeftEdgeExposed : bRightEdgeExposed; }
};
