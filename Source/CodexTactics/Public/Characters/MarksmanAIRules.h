#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "MarksmanAIRules.generated.h"

/**
 * Marksman enemy tuning (UE-only archetype, no Godot reference: Gemini's spec in docs/port/TANDEM.md, request 3,
 * 2026-10-01). Distances in cm, times in s. Edited on AMarksmanEnemyCharacter (class defaults / Blueprint).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FMarksmanConfig
{
	GENERATED_BODY()

	/** Operatives closer than this make him retreat at a sprint (kiting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float RetreatDistance = 1200.f;
	/** Preferred engagement band: closer -> steps back, farther (or no line of fire) -> closes in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float PreferredMinRange = 2000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float PreferredMaxRange = 3500.f;
	/** Notices an operative in line of sight this close while patrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float DetectionRange = 4500.f;
	/** Telegraphed aim before every shot; the aim breaks when the line of fire is lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float AimDuration = 2.f;
	/** Pause after a shot before the next aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float ShotCooldown = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float ShotDamage = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float CritChance = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float CritMultiplier = 2.f;
	/** Base hit chance at a standing target in the open, standing shooter, within PreferredMaxRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float BaseAccuracy = 0.6f;
	/** Shooter stance multipliers (prone +35 %, crouch +15 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float ProneAccuracyBonus = 1.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float CrouchAccuracyBonus = 1.15f;
	/** Target in hard cover this long -> the marksman flanks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float FlankAfterCampSeconds = 4.f;
	/** Flank point angle from the target's facing (clamped to 45-90 deg) and distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float FlankAngleDegrees = 70.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float FlankDistance = 2200.f;
	/** Capsule half heights per stance (prone = 1/3 of standing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float StandHalfHeight = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float CrouchHalfHeight = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float ProneHalfHeight = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float WalkSpeed = 320.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float SprintSpeed = 520.f;
	/** Seconds he stays down after an ambush hit before repositioning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float AmbushProneSeconds = 1.5f;
	/** Other marksmen this close are alerted by an ambush hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marksman")
	float AlertRadius = 1500.f;
};

/** What the marksman is doing (debug / anim / UI). */
UENUM(BlueprintType)
enum class EMarksmanAIState : uint8
{
	Patrol,
	Engage,
	Aim,
	Retreat,
	Flank,
	Ambushed
};

/** Movement decision for the current distance / line of fire. */
enum class EMarksmanMove : uint8
{
	Hold,
	Approach,
	BackOff,
	Retreat
};

/** Pure decision rules of the Marksman (tested in CodexTactics.Marksman.*). */
namespace MarksmanAIRules
{
	/** Closer than Threshold (cm): break off at a sprint. */
	CODEXTACTICS_API bool ShouldRetreat(float DistanceCm, float ThresholdCm);

	/** The target has hidden in hard cover for FlankAfterSeconds or longer. */
	CODEXTACTICS_API bool ShouldFlank(bool bTargetInCover, float CampSeconds, float FlankAfterSeconds = 4.f);

	/** Moving -> stand (full speed); behind a low obstacle -> crouch; elevated / open ground -> prone. */
	CODEXTACTICS_API EOperativeStance EvaluateBestStance(bool bLowCover, bool bElevated, bool bMoving);

	/** Retreat below RetreatDistance, back off below the band, close in above it or without a line of fire. */
	CODEXTACTICS_API EMarksmanMove ChooseMove(const FMarksmanConfig& Config, float DistanceCm, bool bHasLineOfFire);

	/**
	 * Flank point DistanceCm from TargetPos, DesiredAngle (clamped 45-90) off the target's facing, on the side nearer
	 * to the marksman (Pos). TargetFacing is a planar direction.
	 */
	CODEXTACTICS_API FVector ComputeFlankDestination(const FVector& Pos, const FVector& TargetPos, const FVector& TargetFacing,
		float DesiredAngleDegrees, float DistanceCm);

	/**
	 * Hit chance: BaseAcc x shooter stance bonus x target stance (crouch 0.8, prone 0.6) x CoverMult, full within
	 * PreferredMaxRange and falling linearly to half at twice that; clamped to [0.05, 0.95].
	 */
	CODEXTACTICS_API float ComputeSniperHitChance(const FMarksmanConfig& Config, EOperativeStance ShooterStance,
		EOperativeStance TargetStance, float CoverMult, float DistanceCm);

	/** Capsule half height of a stance. */
	CODEXTACTICS_API float GetHalfHeight(const FMarksmanConfig& Config, EOperativeStance Stance);
}
