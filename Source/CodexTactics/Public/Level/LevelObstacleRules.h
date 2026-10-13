#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

/**
 * Pure rules of the Demo 01 level obstacles (user flow 2026-10-13, docs/demo/DEMO01_BEAT_SHEET.md rev 5): the one-at-a-time
 * shaky bridge (ABridgeCrossingVolume, beat A1 "select + solo") and the low fence a body has to crawl under
 * (ACrawlGapActor, beat A3 "stances"). UE-only (Godot had neither). Tested by CodexTactics.Level.*.
 */
namespace BridgeRules
{
	/** What happens to an operative who steps onto the span. */
	enum class EBridgeEntry : uint8
	{
		/** Free span: he is on the bridge now. */
		Admit,
		/** Full: he stops at the edge ("The bridge won't hold two!"), waits until it is free. */
		Refuse,
		/** He is already one of the occupants. */
		AlreadyInside
	};

	/** Capacity <= 0 = unlimited. */
	CODEXTACTICS_API EBridgeEntry Evaluate(int32 Occupants, int32 Capacity, bool bAlreadyOccupant);

	/** Position along the span (the volume's local X, cm) of a world point. */
	CODEXTACTICS_API float SpanCoordinate(const FTransform& BridgeToWorld, const FVector& WorldPoint);

	/** He left on the other side from where he stepped on (both coordinates along the span, cm): a crossing, not a step back. */
	CODEXTACTICS_API bool DidCross(float EntryCoordinate, float ExitCoordinate);

	/** The refusal line / creak repeats at most once per CooldownSeconds per operative (Last < 0 = never said). */
	CODEXTACTICS_API bool ShouldRepeatRefusal(float LastRefusalTime, float Now, float CooldownSeconds);

	/** Default seconds between two refusal lines for the same operative. */
	inline constexpr float DefaultRefusalCooldownSeconds = 4.f;
}

namespace CrawlGapRules
{
	/** Height (cm) of the bar's underside: a prone capsule (2 x 40 cm radius = 80 cm) passes, crouched (117 cm) and standing do not. */
	inline constexpr float DefaultClearanceCm = 90.f;
	/** Top of the blocking bar (cm): the visual fence the user places may be taller. */
	inline constexpr float DefaultBarTopCm = 200.f;
	/** How far (cm) on each side of the fence the "go prone" approach zone reaches. */
	inline constexpr float DefaultApproachCm = 150.f;
	/** Speed (cm/s) below which an operative counts as standing (no refusal). */
	inline constexpr float ApproachSpeedThreshold = 15.f;

	/** Only a prone body goes under the fence (manual prone: user default 2026-10-13). */
	CODEXTACTICS_API bool CanPassUnder(EOperativeStance Stance);

	/** The body (capsule height, cm) fits under a bar with this clearance. */
	CODEXTACTICS_API bool FitsUnder(float BodyHeightCm, float ClearanceCm);

	/**
	 * He is walking towards the fence: his planar velocity points at the gap (positive component along the direction to the
	 * fence line) and he is faster than ApproachSpeedThreshold.
	 */
	CODEXTACTICS_API bool IsApproaching(const FVector& ToFence2D, const FVector& Velocity2D);

	/** Which side of the fence (-1 / +1) a local X coordinate lies on (0 = on the fence line). */
	CODEXTACTICS_API int32 Side(float LocalX);

	/** He came out on the other side: a crawl-through (both sides non-zero and different). */
	CODEXTACTICS_API bool DidPass(int32 EntrySide, int32 ExitSide);
}
