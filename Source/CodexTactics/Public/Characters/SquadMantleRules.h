#pragma once

#include "CoreMinimal.h"

/**
 * R17-1 (designer 2026-11-17, TASK_BOARD): the squad climbs onto / over a mantle obstacle the way it takes a ladder - one climbs, the others
 * wait at the base and cover; he is up, the next one climbs; the first man up covers from the top (docs/design/MANTLING.md 6.4,
 * docs/design/FORMATION_CONTRACT.md 22). The queue itself is the ladder's (TTraversalQueue); these are the pure geometry / look rules of
 * USquadSubsystem's squad climb (SquadSubsystemMantleQueue.cpp). UE-only (Godot had no mantling). Tests: CodexTactics.Formation.MantleQueue.*.
 */
namespace SquadMantleRules
{
	/**
	 * The climbers' lanes along the face, as offsets from the first climber's lane (+ = along the face's tangent): 0, +Spacing, -Spacing,
	 * +2 Spacing, -2 Spacing, ... kept inside [MinOffsetCm, MaxOffsetCm] (the room the face has on either side of the first lane, its ends
	 * kept clear). At most Count lanes; fewer when the face is too short (the rest have no lane: they hold at the base).
	 */
	CODEXTACTICS_API TArray<float> LaneOffsets(int32 Count, float SpacingCm, float MinOffsetCm, float MaxOffsetCm);

	/**
	 * The climb order: the leader first, then the others by their formation slot (lower first); members without a slot (INDEX_NONE) after the
	 * slotted ones in the order given. Returns indices into Slots (LeaderIndex first).
	 */
	CODEXTACTICS_API TArray<int32> ClimbOrder(int32 LeaderIndex, const TArray<int32>& Slots);

	/** A known threat lies on the open side of the base (within 90 deg of the outward look): the waiters can see towards it. */
	CODEXTACTICS_API bool IsThreatOnOpenSide(float OutwardYaw, float ThreatYaw);

	/**
	 * A waiter's look (world yaw) by his place in the line: away from the obstacle (OutwardYaw), the threat side first - place 0 looks at a
	 * known threat on the open side, else straight out; place 1 +FanDeg, place 2 -FanDeg, place 3 +2 FanDeg, ... from that look, never more
	 * than MaxOffDeg off the outward look (nobody covers into the obstacle).
	 */
	CODEXTACTICS_API float WaitLookYaw(float OutwardYaw, int32 Place, float FanDeg, float MaxOffDeg, const TOptional<float>& ThreatYaw);

	/**
	 * The look of a man who is up (on the top / over the fence) by the order he came up: the first one covers the climb's direction (the far
	 * side nobody at the base can see) - or a known threat; the next ones +90 / -90 / back over the base, then the diagonals.
	 */
	CODEXTACTICS_API float UpLookYaw(float ClimbYaw, int32 UpIndex, const TOptional<float>& ThreatYaw);
}
