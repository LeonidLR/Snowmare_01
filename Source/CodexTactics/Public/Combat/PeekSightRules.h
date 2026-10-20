#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Tactics/CoverTypes.h"
#include "PeekSightRules.generated.h"

/**
 * Peek sight (game designer 2026-10-19: "when an operative peeks out from cover he sees NOTHING - peeking must give at least a
 * LIMITED view round the corner"). UE-only, no Godot reference (Godot had no line of sight). The squad's sight
 * (UTacticalSightSubsystem, symmetric since d380d83) traced from the capsule's eyes behind the wall, ignoring that in the corner
 * aim / corner hold / manual peek his head and rifle are leaned OUT past the corner, and over a low wall when he aims over it.
 *
 * While he peeks, a second view opens from the leaned-out eye point: a cone of HalfAngleDeg round the pose's aim direction, up to
 * RangeCm, line of sight traced from that point. His own eyes (the capsule) keep seeing what they see. Enemies look at the same
 * leaned-out point (UTacticalSightSubsystem::ProfilePoint): what he sees from there can see him.
 *
 * The trap search of the peek (FTrapPeekSpotConfig, rev 5j) looks from the same eye point, in the same aim and the same 90 deg cone,
 * only shorter (12 m: a mine lies flush with the snow) and with a reach that sweeps out over 1.5 s - the mines are found one by one,
 * the nearest first, where his drawn cone covers them. BP_Operative Class Defaults -> Cover|Peek.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FPeekSightConfig
{
	GENERATED_BODY()

	/** Off: peeking shows nothing beyond what his own eyes see (the pre-2026-10-19 rule). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek")
	bool bEnabled = true;

	/** How far the peek sees an enemy, cm (planar, from the leaned-out eye). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float RangeCm = 1800.f;

	/** Half the peek cone round the pose's aim direction, degrees (45 = a 90 degree cone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0", ClampMax = "180"))
	float HalfAngleDeg = 45.f;

	/** Aiming over a low wall: the eyes rise to the wall top + this, cm (never below his stance's eyes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float OverTopClearanceCm = 15.f;

	/** ... but never above this over his feet, cm (a 1.8 m "low" wall is not seen over). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float OverTopMaxEyeCm = 170.f;

	/** Aiming over a low wall he leans this far towards it, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float OverTopLeanCm = 25.f;

	/**
	 * Round a corner the head is leaned out in FRONT of the wall's face line, away from the wall, cm (2026-10-20: measured on the clips'
	 * head bone, 32-43 cm in front of the capsule): the peek eye point is moved that far along the wall normal so it matches the visible head.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek", meta = (ClampMin = "0"))
	float CornerLeanDepthCm = 35.f;
};

/** What the peek view looks from (PeekSightRules::DecidePose). */
enum class EPeekSightPose : uint8
{
	/** Not peeking: his own eyes only. */
	None,
	/** Leaned out round the end of a wall: the corner aim / hold of high cover, the crouched corner lean of low cover. */
	CornerLean,
	/** Aiming over the top of a low wall from cover. */
	OverTop
};

/** Pure rules of the peek view; tested in CodexTactics.Combat.Sight.Peek*. AOperativeCharacter::GetPeekSight applies them. */
namespace PeekSightRules
{
	/**
	 * The pose the peek view looks from. In cover (Height != None), not prone, in the corner aim (bCornerAimActive): at a peek corner
	 * (bAtPeekCorner: the manual peek spot or the high cover corner hold spot) -> CornerLean; aiming over a LOW wall elsewhere ->
	 * OverTop; a high wall away from its corner shows nothing. Hugging the wall (no aim) -> None.
	 */
	CODEXTACTICS_API EPeekSightPose DecidePose(ECoverHeight Height, EOperativeStance Stance, bool bCornerAimActive, bool bAtPeekCorner);

	/** Eye height over a low wall: max(stance eyes, wall top + clearance), capped at OverTopMaxEyeCm (not below the stance eyes). */
	CODEXTACTICS_API float OverTopEyeHeight(const FPeekSightConfig& Config, float StanceEyeCm, float WallTopCm);

	/** The over-the-top eye point: Feet + the eye height, OverTopLeanCm towards the wall (WallNormal points away from it). */
	CODEXTACTICS_API FVector OverTopEyePoint(const FPeekSightConfig& Config, const FVector& Feet, const FVector& WallNormal, float StanceEyeCm,
		float WallTopCm);

	/**
	 * The corner-lean eye point: the lean point past the edge (CoverRules::CornerLeanPoint) moved CornerLeanDepthCm out along the wall normal
	 * (WallNormal points away from the wall), where the leaned-out head really is.
	 */
	CODEXTACTICS_API FVector CornerLeanEyePoint(const FPeekSightConfig& Config, const FVector& LeanPoint, const FVector& WallNormal);

	/** BaseDirection turned by YawDeg (the upper-body twist), planar unit. */
	CODEXTACTICS_API FVector TwistedDirection(const FVector& BaseDirection, float YawDeg);

	/** Target lies in the peek cone (enabled, planar distance <= RangeCm, within HalfAngleDeg of Direction). */
	CODEXTACTICS_API bool IsInCone(const FPeekSightConfig& Config, const FVector& Eye, const FVector& Direction, const FVector& Target);
}
