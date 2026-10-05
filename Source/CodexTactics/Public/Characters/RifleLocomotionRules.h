#pragma once

#include "CoreMinimal.h"

/**
 * Rifle_2 locomotion (user's Content/RifleAnims/Rifle_2 pack; ABP_Operative_Rifle2, UE-only, no Godot reference): the
 * pure choices behind the Idle / Turn-in-place / Start / Walk / Stop state machine — which of the 8 directional
 * start / stop clips, which planted foot, which turn-in-place clip (45 / 90 / 135 / 180, left / right) and when.
 * Tested in CodexTactics.Characters.RifleLocomotion.*; UOperativeAnimInstance runs them every frame.
 */

/** The machine's states (the AnimGraph state machine follows them through bLoco* flags). */
enum class ERifleLocoState : uint8
{
	Idle,
	IdleBreak,
	Turn,
	Start,
	Walk,
	Stop
};

namespace RifleLocomotionRules
{
	/** 8 sectors of 45°, centred on: 0 F, 1 FR, 2 R, 3 BR, 4 B, 5 BL, 6 L, 7 FL (the blend space order). */
	inline constexpr int32 SectorCount = 8;
	/** Stand turns: 45 / 90 / 135 / 180°. */
	inline constexpr int32 TurnBucketCount = 4;
	/** Idle with the root this far off the facing starts a turn-in-place, degrees. */
	inline constexpr float TurnTriggerDeg = 40.f;
	/** Speed above which a move intent starts walking / below which a stop is complete, cm/s. */
	inline constexpr float StartSpeed = 10.f;
	/** A start clip hands over to the walk loop this long before it ends, s. */
	inline constexpr float StartBlendOut = 0.2f;

	/** Sector of a movement direction relative to the facing (degrees, any range). */
	CODEXTACTICS_API int32 DirectionSector(float DirectionDeg);

	/** Pack suffix of a sector: F, FR, RR, BR, B, BL, LL, FL (left / right walk the LL / RR clips). */
	CODEXTACTICS_API const TCHAR* SectorName(int32 Sector);

	/** Foot planted first on a start: right-hand sectors lead with the right foot, the rest with the left (0 left, 1 right). */
	CODEXTACTICS_API int32 StartFoot(int32 Sector);

	/** Foot planted on a stop from the walk phase: half cycles alternate left / right (0 left, 1 right). */
	CODEXTACTICS_API int32 StopFoot(float WalkSeconds, float CycleSeconds);

	/** Index into the 16-entry start / stop clip arrays: sector * 2 + foot. */
	CODEXTACTICS_API int32 ClipIndex(int32 Sector, int32 Foot);

	/** Signed shortest angle from -> to (degrees, -180..180]. */
	CODEXTACTICS_API float DeltaYaw(float FromDeg, float ToDeg);

	/** A turn-in-place starts: idle, not moving, the root offset at least TurnTriggerDeg. */
	CODEXTACTICS_API bool ShouldTurnInPlace(float RootYawOffsetDeg, float Speed, bool bMoveIntent);

	/**
	 * The turn clip for an offset (offset = mesh yaw - actor yaw: negative means the actor turned right, so the body
	 * turns right): bucket 0..3 = 45 / 90 / 135 / 180 nearest to |offset|; a flip of exactly 180 (or more) turns right.
	 * OutRight: true for the R clips.
	 */
	CODEXTACTICS_API int32 TurnBucket(float RootYawOffsetDeg, bool& OutRight);

	/** Degrees a bucket turns. */
	CODEXTACTICS_API float BucketDegrees(int32 Bucket);

	/** Start when there is a move intent and the body moves; stop when the intent is gone (the body may still slide). */
	CODEXTACTICS_API bool ShouldStart(float Speed, bool bMoveIntent);
	CODEXTACTICS_API bool ShouldStop(bool bMoveIntent);
}
