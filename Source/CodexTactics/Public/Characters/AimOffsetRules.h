#pragma once

#include "CoreMinimal.h"

/**
 * Aim pitch for the ABP's Aim Offset (user request 2026-10-07; UE-only, no Godot reference). A hound running at the
 * operative is lower than his eyes, yet the clips aim level: the 1D aim offset /Game/RifleAnims/Animations/AimOffsets/
 * AO_Idle/AO1D_Rifle_Idle (axis "Pitch" -90..90, -90 = FwdDown, 0 = FwdCenter, +90 = FwdUp, additive mesh space on
 * AS_Rifle_Idle) bends the spine / arms by AimPitch. UOperativeAnimInstance feeds it; pure rules, tested in
 * CodexTactics.Anim.AimOffset.*.
 */
struct CODEXTACTICS_API FAimOffsetState
{
	bool bEnabled = true;
	/** A ranged weapon in hand (a melee weapon has no aim). */
	bool bRangedWeapon = true;
	bool bWeaponVisible = true;
	bool bReloading = false;
	/** A grenade throw / hit reaction window has the arms. */
	bool bUpperBodyAction = false;
	bool bVaulting = false;
	bool bSprinting = false;
	/** Prone: the crawl / prone aim clips do not fit the standing-idle offset. */
	bool bProne = false;
	bool bDead = false;
};

namespace AimOffsetRules
{
	/**
	 * Pitch in degrees (+ up, - down) from From (muzzle / eye height) to To (the target's aim point) relative to the
	 * horizontal plane, clamped to +-MaxAbsDegrees (the AO's range). The horizontal distance is floored (50 cm) so a
	 * target at his feet does not flip the pitch.
	 */
	CODEXTACTICS_API float PitchToTarget(const FVector& From, const FVector& To, float MaxAbsDegrees);

	/** The aim offset is wanted (alpha target 1) - else 0. */
	CODEXTACTICS_API bool WantsAimOffset(const FAimOffsetState& State);

	/** Constant-rate alpha step (1 / BlendSeconds per second). */
	CODEXTACTICS_API float StepAlpha(float Current, bool bWanted, float DeltaSeconds, float BlendSeconds);

	/**
	 * Signed horizontal angle in degrees from the pose's aim direction BaseDirection (at From) to the target To, + = to
	 * his right (UE yaw, clockwise seen from above; the 2D aim offset AO_Rifle_Aim's "Yaw" axis has RightCenter at +90,
	 * LeftCenter at -90, checked on its samples 2026-10-07), clamped to +-MaxAbsDegrees. 0 for a degenerate direction.
	 */
	CODEXTACTICS_API float YawToTarget(const FVector& From, const FVector& BaseDirection, const FVector& To, float MaxAbsDegrees);

	/**
	 * What is left of the aim error once the upper body twisted by AppliedYawDegrees (the AO's current yaw x alpha):
	 * |ErrorDegrees - AppliedYawDegrees|, degrees. A shot is allowed while this is within the aim cone.
	 */
	CODEXTACTICS_API float ResidualAimError(float ErrorDegrees, float AppliedYawDegrees);

	/**
	 * The AimYaw target from the raw (unclamped, -180..180) angle to the target (user PIE video AnimOffset_Bug_01,
	 * 2026-10-08: a target behind him sat at +-180 deg, its sign flipped frame to frame and the clamped yaw jumped between
	 * +60 and -60 - the upper body jittered). A target beyond ReachDeg is out of the twist's reach: 0 (the body turns to it
	 * for a shot, not the spine); it is reached again only below ReachDeg - HysteresisDeg (bInOutOfReach keeps the state).
	 * Within reach: the raw angle clamped to +-ClampDeg.
	 */
	CODEXTACTICS_API float YawTargetWithinReach(float RawDegrees, float ClampDeg, float ReachDeg, float HysteresisDeg, bool& bInOutOfReach);
}
