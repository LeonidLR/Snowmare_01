#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules for look hold outside stealth (059, solo walk / jog / crouch / prone strafe).
 * Designer request 2026-11-15: docs/design/LOOK_HOLD_SOLO.md.
 * Shared by solo look hold, formation leader look hold, and stealth manual look anchor.
 */
namespace LookHoldRules
{
	/** The 8-direction sector (0..7 = F, FR, R, BR, B, BL, L, FL) nearest to a body-relative movement direction, deg. */
	CODEXTACTICS_API int32 GetDirectionSector(float RelativeMoveDeg);

	/**
	 * Sector for a gait start clip.
	 * When a look point holds the body, the body stays facing the look point and the legs strafe (LeadSector).
	 * Without a look point, the body turns into the travel within ~0.2 s, so the start is forward (0).
	 */
	CODEXTACTICS_API int32 GetGaitStartSector(int32 LeadSector, bool bBodyHeldByLook);

	/** Returns true when the legs' direction is more than ToleranceDeg off the center of StartSector. */
	CODEXTACTICS_API bool IsOffGaitStartSector(int32 StartSector, float LegDirectionDeg, float ToleranceDeg = 30.f);

	/** Look point is valid if outside the deadzone radius (50 cm) in the XY plane. */
	CODEXTACTICS_API bool IsValidLookPoint(const FVector& LookPoint, const FVector& OperativeLocation, float MinDistanceCm = 50.f);

	/** Sprint clears the look point (no strafe clips for sprint). */
	inline bool ShouldClearOnSprint(bool bSprinting) { return bSprinting; }

	/** Entering cover clears the look point (wall attachment owns facing). */
	inline bool ShouldClearOnCover(bool bInCover) { return bInCover; }

	/** Turn-based combat clears the look point (the tactical grid owns facing). */
	inline bool ShouldClearOnTurnBased() { return true; }

	/** Incapacitated states clear the look point: dead, knocked down, panicking, ladder, vault, carry. */
	CODEXTACTICS_API bool ShouldClearOnIncapacitated(bool bDead, bool bKnockedDown, bool bPanicking, bool bClimbingLadder, bool bVaulting, bool bCarrying);

	/** Real-time combat / fire orders do NOT clear the look point (only temporarily turn to the target). */
	inline bool ShouldClearOnFireOrder() { return false; }

	/**
	 * Look-hold eligibility:
	 * In stealth mode: any stealthed operative is eligible.
	 * Outside stealth: solo mode applies to any selected operative; formation applies to the leader only.
	 */
	CODEXTACTICS_API bool IsLookHoldEligible(bool bInStealthMode, bool bSoloMode, bool bIsLeader, bool bGuarding);

	/** Standing upper body twist limit (deg). */
	inline constexpr float StandingMaxTwistDeg = 45.f;

	/** Prone upper body twist limit (deg): crawl turns the whole body. */
	inline constexpr float ProneMaxTwistDeg = 0.f;

	/** Clamps residual twist angle to +/- LimitDeg. */
	CODEXTACTICS_API float ClampResidualTwist(float TwistDeg, float LimitDeg = 45.f);
}
