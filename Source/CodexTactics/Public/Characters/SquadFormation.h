#pragma once

#include "CoreMinimal.h"
#include "SquadFormation.generated.h"

/**
 * Squad formation tuning, in UE units (cm, s). Defaults mirror Godot player.gd:
 * FORMATION_BACK_OFFSET 2.8, FORMATION_SIDE_OFFSET 2.6, column mode 1.8 + 1.5 per slot (hold 0.6 s),
 * formation_rotation_smoothing 3.5, smart slot swap (2.25 m^2 hysteresis, 0.35 s cooldown),
 * wander 0.6 / 0.5 m at 1.2 Hz, speed jitter 0.12, STOP_RADIUS 0.25, SLOW_RADIUS 2.5,
 * catch-up x1.25 beyond 5 m, regroup cap x1.15 within 10 m.
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSquadFormationConfig
{
	GENERATED_BODY()

	/** Sideways distance of a slot from the leader, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (ClampMin = "0"))
	float SideOffset = 260.f;

	/** Distance behind the leader per formation row, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (ClampMin = "0"))
	float BackOffset = 280.f;

	/** Column mode: distance behind the leader before the first follower, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column", meta = (ClampMin = "0"))
	float ColumnBaseBack = 180.f;

	/** Column mode: spacing between followers, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column", meta = (ClampMin = "0"))
	float ColumnSpacing = 150.f;

	/** Column mode stays on this long after the last blocked check, s (prevents flicker). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column", meta = (ClampMin = "0"))
	float ColumnHoldTime = 0.6f;

	/** How fast the formation heading follows the leader heading, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (ClampMin = "0"))
	float RotationSmoothing = 3.5f;

	/** Swapping two followers' slots must save more than this in summed squared distance, cm^2. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (ClampMin = "0"))
	float SlotSwapHysteresis = 22500.f;

	/** Minimum time between slot swaps, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slots", meta = (ClampMin = "0"))
	float SlotSwapCooldown = 0.35f;

	/** Sideways wander amplitude around the slot while the leader moves, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0"))
	float WanderSideAmplitude = 60.f;

	/** Forward/back wander amplitude around the slot while the leader moves, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0"))
	float WanderBackAmplitude = 50.f;

	/** Wander frequency multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander", meta = (ClampMin = "0"))
	float WanderFrequency = 1.2f;

	/** Relative follower speed variation while the leader moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0", ClampMax = "1"))
	float SpeedJitter = 0.12f;

	/** Follower cruise speed relative to its own max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float FollowerSpeedFactor = 0.95f;

	/** Beyond this distance to the slot the follower runs to catch up, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float CatchUpDistance = 500.f;

	/** Catch-up speed relative to the follower's max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float CatchUpMultiplier = 1.25f;

	/** Within this distance the regroup speed cap applies, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float RegroupLimitDistance = 1000.f;

	/** Regroup speed cap relative to the follower's max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float RegroupSpeedMultiplier = 1.15f;

	/** A follower closer than this to its slot stands still, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float StopRadius = 25.f;

	/** Inside this distance to the slot the follower slows down proportionally, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float SlowRadius = 250.f;

	/** Minimum follower speed while still approaching the slot, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0"))
	float MinApproachSpeed = 30.f;

	/** How often followers get a new path to their slot, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed", meta = (ClampMin = "0.02"))
	float RepathInterval = 0.2f;
};

/**
 * Pure formation math for the triangle squad formation (UE axes: X forward, Y right, Z up).
 * Godot reference: player.gd get_static_formation_slot_pos(), get_formation_target_pos(),
 * _update_formation_orientation(), _update_smart_formation_slots(), _process_follower_movement().
 */
namespace SquadFormation
{
	/** Flattens a heading to the ground plane and returns unit forward and right vectors. */
	CODEXTACTICS_API void GetForwardRight(const FVector& Heading, FVector& OutForward, FVector& OutRight);

	/**
	 * Slot position behind the leader. Even slots go left, odd slots right, two slots per row.
	 * In column mode all followers line up straight behind the leader.
	 */
	CODEXTACTICS_API FVector ComputeSlotPosition(const FSquadFormationConfig& Config, const FVector& LeaderLocation,
		const FVector& FormationForward, int32 SlotIndex, bool bColumnMode);

	/** True when swapping the followers of slot 0 and slot 1 saves more than the hysteresis. */
	CODEXTACTICS_API bool ShouldSwapSlots(const FSquadFormationConfig& Config, const FVector& LeaderLocation,
		const FVector& FormationForward, const FVector& Slot0FollowerLocation, const FVector& Slot1FollowerLocation);

	/** Rotates the formation heading toward the leader heading with exponential-like smoothing. */
	CODEXTACTICS_API FVector SmoothHeading(const FSquadFormationConfig& Config, const FVector& CurrentHeading,
		const FVector& TargetHeading, float DeltaSeconds);

	/** Organic wander offset around a slot while the leader is moving. */
	CODEXTACTICS_API FVector ComputeWanderOffset(const FSquadFormationConfig& Config, const FVector& FormationForward,
		int32 SlotIndex, float TimeSeconds);

	/**
	 * Follower speed toward its slot, cm/s. FollowerMaxSpeed is the follower's own max speed
	 * (stance, sprint, wounds applied). Returns 0 inside StopRadius.
	 */
	CODEXTACTICS_API float ComputeFollowerSpeed(const FSquadFormationConfig& Config, float FollowerMaxSpeed,
		float DistanceToSlot, int32 SlotIndex, float TimeSeconds, bool bLeaderMoving);
}
