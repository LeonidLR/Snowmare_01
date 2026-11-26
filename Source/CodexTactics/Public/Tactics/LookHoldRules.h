#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules for look hold outside stealth (059, solo walk / jog / crouch / prone strafe).
 * Designer request 2026-11-15: docs/design/LOOK_HOLD_SOLO.md.
 * Shared by solo look hold, formation leader look hold, and stealth manual look anchor.
 */
namespace LookHoldRules
{
	/** Configuration loaded from Content/Data/Tactics/look_hold.json. */
	struct CODEXTACTICS_API FLookHoldConfig
	{
		float MinPlacementDistanceCm = 50.f;
		float StandingMaxTwistDeg = 45.f;
		float ProneMaxTwistDeg = 0.f;
	};

	CODEXTACTICS_API const FLookHoldConfig& GetConfig();
	CODEXTACTICS_API bool LoadConfigFromJson(const FString& JsonString, FLookHoldConfig& OutConfig);
	CODEXTACTICS_API void ReloadConfig();

	/** Default constants (fallback) */
	inline constexpr float DefaultMinPlacementDistanceCm = 50.f;
	inline constexpr float DefaultStandingMaxTwistDeg = 45.f;
	inline constexpr float DefaultProneMaxTwistDeg = 0.f;

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

	/** Look point is valid if outside the deadzone radius in the XY plane. If MinDistanceCm < 0, uses config. */
	CODEXTACTICS_API bool IsValidLookPoint(const FVector& LookPoint, const FVector& OperativeLocation, float MinDistanceCm = -1.f);

	/** Sprint clears the look point (no strafe clips for sprint). */
	inline bool ShouldClearOnSprint(bool bSprinting) { return bSprinting; }

	/** Entering cover clears the look point (wall attachment owns facing). */
	inline bool ShouldClearOnCover(bool bInCover) { return bInCover; }

	/** Turn-based combat clears the look point (the tactical grid owns facing). */
	inline bool ShouldClearOnTurnBased() { return true; }

	/** Knockdown or death clears the look point; actions like vault/ladder pause facing rather than erase. */
	CODEXTACTICS_API bool ShouldClearOnIncapacitated(bool bDead, bool bKnockedDown, bool bPanicking, bool bClimbingLadder, bool bVaulting, bool bCarrying);

	/** Real-time combat / fire orders do NOT clear the look point (only temporarily turn to the target). */
	inline bool ShouldClearOnFireOrder() { return false; }

	/**
	 * Look-hold eligibility:
	 * In stealth mode: any stealthed operative is eligible.
	 * Outside stealth: solo mode applies to any selected operative; formation applies to the leader only.
	 * (Followers in formation keep formation facing; points from solo do not sleep on followers).
	 */
	CODEXTACTICS_API bool IsLookHoldEligible(bool bInStealthMode, bool bSoloMode, bool bIsLeader);

	/** Clamps residual twist angle to +/- LimitDeg. If LimitDeg < 0, uses config StandingMaxTwistDeg. */
	CODEXTACTICS_API float ClampResidualTwist(float TwistDeg, float LimitDeg = -1.f);

	/** Locked world look direction (variant B, task 145). */
	struct CODEXTACTICS_API FLookDirection
	{
		float YawDeg = 0.f;
		float PitchDeg = 0.f;
	};

	/**
	 * When Shift+clicking flat terrain at roughly the operative's standing feet elevation, keep look pitch horizontal (0 deg)
	 * so standing operatives do not pitch downward toward nearby ground clicks; steeper elevation clicks keep true Z.
	 */
	CODEXTACTICS_API FVector AdjustClickForFlatGround(const FVector& OperativeLocation, float HalfHeightCm, const FVector& ClickPoint, float FlatBandCm = 65.f);

	/** Compute fixed world yaw + pitch from OperativeLocation to ClickPoint at the moment of the click. */
	CODEXTACTICS_API FLookDirection DirectionFromClick(const FVector& OperativeLocation, const FVector& ClickPoint);

	/** Project a synthetic look target along the locked compass direction from the operative's current position. */
	CODEXTACTICS_API FVector ProjectLookPoint(const FVector& OperativeLocation, float YawDeg, float PitchDeg = 0.f, float DistanceCm = 1000.f);
}
