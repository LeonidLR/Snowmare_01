#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

class FJsonObject;

enum class EStealthWallLeanPhase : uint8
{
	Start,
	Hold,
	Stop
};

/** Stealth mode tuning (designer task 003, UE-only; no Godot reference). Data: Data/AI/stealth_mode.json. */
struct CODEXTACTICS_API FStealthModeConfig
{
	bool bEnabled = true;
	float DetectionRangeMultiplier = 0.4f;
	float DetectionBuildupMultiplier = 0.5f;
	float FootstepNoiseMultiplier = 0.5f;
	bool bBreakOnFire = true;
	float VisualOpacity = 0.4f;
	float VisualGray = 0.45f;
	// Stealth v2 (008): fast crouched gait, fast perception multipliers, and threat look-lock tuning.
	float FastSpeedCm = 300.f;
	float FastDetectionRangeMultiplier = 0.6f;
	float FastDetectionBuildupMultiplier = 0.75f;
	float LookLockKeepSeconds = 2.0f;
	float LookLockFovDeg = 100.0f;
	float LookLockSwitchRatio = 0.7f;
};

namespace StealthModeRules
{
	struct CODEXTACTICS_API FStealthLookLockCandidate
	{
		uint32 TargetId = 0;
		FVector Location = FVector::ZeroVector;
		bool bVisible = false;
	};

	struct CODEXTACTICS_API FStealthLookLockState
	{
		uint32 TargetId = 0;
		FVector LastKnownLocation = FVector::ZeroVector;
		float TimeSinceSeenSeconds = 0.f;

		bool IsActive() const { return TargetId != 0; }
		void Reset() { *this = FStealthLookLockState(); }
	};

	/** Toggle a mixed selection on; toggle an entirely stealthed selection off. An empty selection never enables it. */
	CODEXTACTICS_API bool EnableForSelection(int32 SelectedCount, int32 StealthedCount);
	/** Formation blocks stealth: allowed only in SOLO mode or while followers hold position (`bSoloOrHolding`), when guarding outside the formation, or when no active formation runs. */
	CODEXTACTICS_API bool IsBlockedByFormation(bool bSoloOrHolding, bool bFormationActive, bool bGuarding);
	/** Localized tooltip/refusal shown when formation blocks stealth entry. */
	CODEXTACTICS_API FText GetFormationBlockedTooltip();
	/** Sprint always breaks stealth; a shot breaks it only when stealth_break_on_fire is enabled. Delegates to 5-arg ShouldBreak(..., false, Config). */
	CODEXTACTICS_API bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, const FStealthModeConfig& Config);
	/** Fast stealth movement does not break stealth; normal sprint (outside fast stealth) breaks it; firing breaks it when bBreakOnFire is true. */
	CODEXTACTICS_API bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, bool bFast, const FStealthModeConfig& Config);
	/** Pure helper for Stealth v2 §3: selects Lean_Start / Lean / Lean_Stop clip name by wall facing and lean phase. Returns NAME_None on invalid facing. */
	CODEXTACTICS_API FName GetWallLeanClipName(ECoverFacing Facing, EStealthWallLeanPhase Phase = EStealthWallLeanPhase::Hold);
	/** Returns the yaw offset (-90 for Left, 90 for Right, 0 otherwise) matching the wall clip facing. */
	CODEXTACTICS_API float GetWallLeanYawOffset(ECoverFacing Facing);
	/** Multipliers relative to the existing stance/weather/perception rules; disabled/inactive = identity. */
	CODEXTACTICS_API float DetectionRangeScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionRangeScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionBuildupScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float DetectionBuildupScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	CODEXTACTICS_API float FootstepNoiseScale(bool bActive, const FStealthModeConfig& Config);
	CODEXTACTICS_API float FootstepNoiseScale(bool bActive, bool bFast, const FStealthModeConfig& Config);
	/** Body turn rate floor while stealthed without a look point / in fast stealth, deg/s (~18 rad/s: the ~0.2 s turn of a fresh order). */
	inline constexpr float MinMoveTurnRateDeg = 1031.3f;
	/** FacingRules::SmoothVelocity rate of the body's movement facing: fast stealth follows the travel twice as tightly. */
	inline float FacingVelocitySmoothRate(bool bStealthFast) { return bStealthFast ? 20.f : 10.f; }
	/** The 8-direction sector (0..7 = F, FR, R, BR, B, BL, L, FL) nearest to a body-relative movement direction, deg. */
	CODEXTACTICS_API int32 GetDirectionSector(float RelativeMoveDeg);
	/**
	 * Designer bug 2026-11-14 (034): the sector a stealth gait START clip plays. Without a look point holding the body (manual anchor /
	 * auto lock), the body turns to the travel within ~0.2 s (UpdateCombatFacing), so the start is the forward one - a start picked
	 * from the first moving frame's relative direction (before that turn; a re-order while moving, a stop-and-go) was a sideways /
	 * backwards clip that kept the body for its whole ~1.5 s while he already ran forward. With a look point the body stays: the
	 * travel's own sector (a strafe start) is right.
	 */
	CODEXTACTICS_API int32 GetGaitStartSector(int32 LeadSector, bool bBodyHeldByLook);
	/** The legs' smoothed body-relative direction (deg) is more than ToleranceDeg off a gait start's sector centre. */
	CODEXTACTICS_API bool IsOffGaitStartSector(int32 StartSector, float LegDirectionDeg, float ToleranceDeg = 30.f);
	/**
	 * A playing gait start no longer matches the legs (a re-order to another side, a look holding the body off the travel): it fades
	 * out instead of playing to its end. bMatchedBefore = the legs were inside its sector once; until then (a fresh order turning
	 * the body into the forward start) it is kept for TurnGraceSeconds of real time.
	 */
	CODEXTACTICS_API bool ShouldEndGaitStart(bool bOffSector, bool bMatchedBefore, float ElapsedSeconds, float TurnGraceSeconds = 0.3f);
	/**
	 * Designer follow-up 2026-11-14 (034): the stealth gait still counts as moving - above StartSpeed, or a re-order's velocity dip
	 * (he keeps a move order and the ABP's smoothed speed is still above FullSpeed), so a re-order never plays a stop + start.
	 */
	CODEXTACTICS_API bool IsGaitMoving(float Speed, float SmoothedSpeed, bool bWasMoving, bool bHasMoveOrder, float StartSpeed, float FullSpeed);
	/**
	 * A gait start clip (an acceleration from standstill) begins only from standstill / low speed: the smoothed speed below Share of
	 * the gait's nominal speed. Faster (a re-order while running) the cycle only changes direction - no start, no reset.
	 */
	CODEXTACTICS_API bool ShouldBeginGaitStart(float SmoothedSpeed, float GaitNominalSpeed, float Share = 0.5f);
	/**
	 * Play rate of a gait start clip: it encodes its own acceleration from standstill, so it plays at the commanded (target) speed over
	 * its gait's nominal speed - not the capsule's momentary speed, which near 0 slowed the start into a ~1 s warm-up.
	 */
	CODEXTACTICS_API float GaitStartPlayRate(float CommandedSpeed, float StartNominalSpeed, float MaxPlayRate);
	/** Fast stealth's jog share floor by speed: 0 below 0.35x FastSpeedCm, 1 from 0.6x (Opus H1: no slow-motion jog strides while slow). */
	CODEXTACTICS_API float FastGaitFloor(float Speed, float FastSpeedCm);
	/** True when CandidateLocation lies outside MinDistanceCm and within +-FovDeg of HeadingYawDeg in the XY plane. */
	CODEXTACTICS_API bool IsInLookLockFov(const FVector& ObserverLocation, float HeadingYawDeg, const FVector& CandidateLocation,
		float FovDeg, float MinDistanceCm = 50.f);
	/** Updates automatic stealth threat look-lock: keeps seen threat, remembers lost threat for LookLockKeepSeconds, switches only when closer than LookLockSwitchRatio. */
	CODEXTACTICS_API void UpdateLookLock(FStealthLookLockState& State, const FVector& ObserverLocation, float HeadingYawDeg,
		TArrayView<const FStealthLookLockCandidate> Candidates, float DeltaSeconds, const FStealthModeConfig& Config,
		float MinDistanceCm = 50.f);
	/** Optional JSON keys over Base; malformed values keep Base; multipliers/visuals clamp to [0, 1], speeds to [0, 2000], and look-lock tuning to non-negative bounds. */
	CODEXTACTICS_API FStealthModeConfig FromJson(const FJsonObject& Json, const FStealthModeConfig& Base = FStealthModeConfig());
	/** Loads the project data once. A missing/invalid file keeps the documented defaults. */
	CODEXTACTICS_API const FStealthModeConfig& GetConfig();
	CODEXTACTICS_API FString GetDefaultPath();
}
