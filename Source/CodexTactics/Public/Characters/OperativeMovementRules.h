#pragma once

#include "CoreMinimal.h"
#include "OperativeMovementRules.generated.h"

/** Body stance of an operative. Godot reference: player.gd `enum Stance`. */
UENUM(BlueprintType)
enum class EOperativeStance : uint8
{
	Standing,
	Crouching,
	Prone
};

/**
 * Movement tuning of an operative, in UE units (cm, cm/s, deg/s).
 * Defaults are the values the Godot build actually runs with: resources/balance.tres
 * (anim_walk_speed 2.2, anim_run_speed 7.25, anim_crouch_speed 1.25) and
 * resources/game_balance_config.gd (character_acceleration 14, character_deceleration 18,
 * wounded_speed_multiplier 0.65), player.gd (prone 0.28, carrying 0.30, max_cold_to_sprint 60,
 * stance turn speeds 14 / 7.5 / 2.5 rad/s).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeMovementConfig
{
	GENERATED_BODY()

	/** Walking speed while standing, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float WalkSpeed = 220.f;

	/** Sprint speed while standing, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float RunSpeed = 725.f;

	/** Walking speed while crouching, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float CrouchSpeed = 125.f;

	/** Crawling speed as a fraction of WalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "1"))
	float ProneSpeedMultiplier = 0.28f;

	/** Speed multiplier while badly wounded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "1"))
	float WoundedSpeedMultiplier = 0.65f;

	/** Speed multiplier while carrying or pushing an object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "1"))
	float CarryingSpeedMultiplier = 0.30f;

	/** Cold level (percent) from which sprinting is impossible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "100"))
	float MaxColdToSprint = 60.f;

	/** Acceleration, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0"))
	float Acceleration = 1400.f;

	/** Braking deceleration, cm/s^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0"))
	float Deceleration = 1800.f;

	/** Distance before a path goal where braking starts, cm (Godot: 1.4 m slow-down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0"))
	float PathBrakingDistance = 140.f;

	/** Arrival radius for move orders, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Acceleration", meta = (ClampMin = "0"))
	float AcceptanceRadius = 25.f;

	/** Yaw turn rate while standing, deg/s (14 rad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turning", meta = (ClampMin = "0"))
	float TurnRateStanding = 802.f;

	/** Yaw turn rate while crouching, deg/s (7.5 rad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turning", meta = (ClampMin = "0"))
	float TurnRateCrouching = 430.f;

	/** Yaw turn rate while prone, deg/s (2.5 rad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Turning", meta = (ClampMin = "0"))
	float TurnRateProne = 143.f;
};

/** Pure movement formulas for operatives. Godot reference: player.gd _process_leader_movement(), can_sprint(). */
namespace OperativeMovementRules
{
	/** Speed multiplier of a stance relative to WalkSpeed. */
	CODEXTACTICS_API float GetStanceSpeedMultiplier(const FOperativeMovementConfig& Config, EOperativeStance Stance);

	/** Sprinting needs cold below MaxColdToSprint, no heavy wound, and not lying prone. */
	CODEXTACTICS_API bool CanSprint(const FOperativeMovementConfig& Config, EOperativeStance Stance, float ColdPercent, bool bWounded);

	/**
	 * Maximum ground speed, cm/s. bSprinting only applies while standing
	 * (a sprint order stands a crouching operative up first).
	 */
	CODEXTACTICS_API float ComputeMaxSpeed(const FOperativeMovementConfig& Config, EOperativeStance Stance, bool bSprinting, bool bWounded, bool bCarrying);

	/** Yaw turn rate for a stance, deg/s. */
	CODEXTACTICS_API float GetTurnRate(const FOperativeMovementConfig& Config, EOperativeStance Stance);
}
