#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableRules.h"
#include "VehicleRules.generated.h"

/**
 * Pure rules of the squad vehicle (APC BTR-82, phases P0 + P1, docs/vehicles/APC_01_TECHNICAL.md / APC_02_DESIGN.md):
 * seats, board / disembark legality, the Y key, exit point choice, the kinematic drive model (turning radius, speed,
 * auto reverse, pure pursuit) and the visual helpers (wheel spin, steering). UE-only (Godot had no vehicles).
 */

/** A seat inside the vehicle (user decision 2026-10-12: Engineer drives, Commander mans the turret, anyone if absent). */
UENUM(BlueprintType)
enum class EVehicleSeat : uint8
{
	None,
	Driver,
	Gunner,
	Passenger
};

/** Why a board / disembark / drive order was refused (None = allowed). */
UENUM(BlueprintType)
enum class EVehicleRefusal : uint8
{
	None,
	Dead,
	KnockedDown,
	AlreadyMounted,
	NotMounted,
	TooFar,
	VehicleMoving,
	NoSeat,
	TurnBased,
	TacticalPause,
	NoDriver,
	NoExit
};

/** What the board key (Y / Alt + Y) does for the operatives it addresses. */
UENUM(BlueprintType)
enum class EVehicleKeyAction : uint8
{
	None,
	Board,
	Disembark
};

/** Engine state for the noise radius. */
UENUM(BlueprintType)
enum class EVehicleEngineState : uint8
{
	Off,
	Idle,
	Cruise,
	Rush
};

/** Kinematic drive tuning (cm, s). Tuned on the vehicle Blueprint (Unreal is master). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleDriveConfig
{
	GENERATED_BODY()

	/** Click: cruise speed (design 6 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "0"))
	float CruiseSpeedCmS = 600.f;

	/** Double click: full speed (design 10 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "0"))
	float RushSpeedCmS = 1000.f;

	/** Automatic reverse (design 2.5 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "0"))
	float ReverseSpeedCmS = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "1"))
	float AccelerationCmS2 = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "1"))
	float BrakingCmS2 = 600.f;

	/** Gameplay minimum turning radius (design 7 m): the yaw rate never exceeds speed / radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "50"))
	float MinTurnRadiusCm = 700.f;

	/** Pure pursuit look-ahead along the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "50"))
	float LookaheadCm = 400.f;

	/** Arrived within this distance of the goal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "10"))
	float ArriveToleranceCm = 150.f;

	/** A goal behind the vehicle and closer than this is reached in reverse (design: within 15 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "0"))
	float ReverseMaxDistanceCm = 1500.f;

	/** "Behind": the angle between the heading and the goal is at least this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "90", ClampMax = "180"))
	float ReverseMinAngleDeg = 120.f;

	/** Beyond this heading error the vehicle slows down to MinTurnSpeedCmS to turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "0", ClampMax = "180"))
	float TurnSlowdownAngleDeg = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive", meta = (ClampMin = "10"))
	float MinTurnSpeedCmS = 250.f;

	/** Wheel radius for the spin of the vendor ABP (BTR-82 wheels ~1.2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive|Visual", meta = (ClampMin = "1"))
	float WheelRadiusCm = 60.f;

	/** Front axle to rear axle (steering angle shown on the front wheels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive|Visual", meta = (ClampMin = "1"))
	float WheelbaseCm = 460.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drive|Visual", meta = (ClampMin = "0", ClampMax = "60"))
	float MaxSteerAngleDeg = 30.f;
};

/** Crew tuning: seats, boarding and getting out. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleCrewConfig
{
	GENERATED_BODY()

	/** Troop seats besides the driver and the gunner (design: 5, so 7 seats in all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "0", ClampMax = "10"))
	int32 PassengerSeats = 5;

	/** Y boards the nearest vehicle within this distance (design 30 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "0"))
	float BoardRangeCm = 3000.f;

	/** Climbing in at the door (task: ~2 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "0"))
	float BoardSeconds = 2.f;

	/** Climbing out (design 1 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "0"))
	float DisembarkSeconds = 1.f;

	/** The operative is at the door within this distance (2D). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "10"))
	float EntryReachCm = 220.f;

	/** Exit points stand this far off the hull side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "40"))
	float ExitOffsetCm = 110.f;

	/** Speed below which the vehicle counts as stopped (boarding / getting out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew", meta = (ClampMin = "0"))
	float StoppedSpeedCmS = 40.f;
};

/** Engine tuning: noise and the warm cabin. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FVehicleEngineConfig
{
	GENERATED_BODY()

	/** Hearing radius of the idling engine (design 20 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine", meta = (ClampMin = "0"))
	float IdleNoiseRadiusCm = 2000.f;

	/** At cruise (design 40 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine", meta = (ClampMin = "0"))
	float CruiseNoiseRadiusCm = 4000.f;

	/** At full speed (design 55 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine", meta = (ClampMin = "0"))
	float RushNoiseRadiusCm = 5500.f;

	/** How often the running engine is "heard" (a noise event per interval). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine", meta = (ClampMin = "0.1"))
	float NoiseIntervalSeconds = 1.f;

	/** Warm zone around the hull centre while the engine runs (the crew inside sits in it; design: 3 m engine deck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine", meta = (ClampMin = "0"))
	float CabinHeatRadiusCm = 350.f;
};

/** What being mounted means for an operative (the tech doc's short list of IsMounted() effects). */
struct CODEXTACTICS_API FMountedOperativeEffects
{
	bool bHidden = true;
	bool bCollision = false;
	bool bTargetableByEnemies = false;
	bool bTakesDamage = false;
	bool bAutoFire = false;
	bool bFollowsFormation = false;
	bool bAcceptsMoveOrders = false;
	/** Cold accumulates (false while the engine heats the cabin). */
	bool bColdDrain = false;
};

namespace VehicleRules
{
	/** Driver + gunner + passengers. */
	CODEXTACTICS_API int32 GetCapacity(const FVehicleCrewConfig& Config);

	/**
	 * Seats for the whole crew aboard, in boarding order: the Engineer drives (else the first non-Commander, else the
	 * Commander), the Commander mans the gun (else the next one aboard), everyone else rides; beyond the capacity: None.
	 * A crew of one always drives.
	 */
	CODEXTACTICS_API TArray<EVehicleSeat> AssignSeats(const TArray<EOperativeRole>& Crew, const FVehicleCrewConfig& Config);

	/** Inputs of a board order for one operative. */
	struct FBoardCheck
	{
		bool bAlive = true;
		bool bKnockedDown = false;
		bool bMounted = false;
		float DistanceCm = 0.f;
		float VehicleSpeedCmS = 0.f;
		int32 FreeSeats = 1;
		bool bTurnBased = false;
		bool bTacticalPause = false;
	};

	CODEXTACTICS_API EVehicleRefusal CanBoard(const FBoardCheck& Check, const FVehicleCrewConfig& Config);

	/** Getting out: mounted, not in the grid fight / the pause (P1), a free exit. A moving vehicle stops first. */
	CODEXTACTICS_API EVehicleRefusal CanDisembark(bool bMounted, bool bTurnBased, bool bTacticalPause, bool bHasFreeExit);

	/** Driving: a driver aboard, real time only (P1: refused in the tactical pause and turn-based with a feed line). */
	CODEXTACTICS_API EVehicleRefusal CanDrive(bool bHasDriver, bool bTurnBased, bool bTacticalPause);

	/**
	 * The Y key: the addressed operatives (Y: the selection, Alt + Y: the whole squad) all aboard = get out; otherwise
	 * the ones outside board. Nobody addressed = nothing.
	 */
	CODEXTACTICS_API EVehicleKeyAction ResolveKeyAction(int32 AddressedCount, int32 MountedCount);

	/**
	 * The exit points to use, best first (indices into Candidates, only free ones): with known threats the side facing
	 * away from the nearest threat (dot of the exit direction and the threat direction, lowest first); without, the ones
	 * nearest ViewPoint (the camera side). Ties keep the candidate order.
	 */
	CODEXTACTICS_API TArray<int32> RankExitPoints(const TArray<FVector>& Candidates, const TArray<bool>& bFree, const FVector& VehicleCentre,
		const TArray<FVector>& Threats, const FVector& ViewPoint);

	/** Max yaw rate (deg/s) of a vehicle at Speed with the minimum turning radius: |v| / R. */
	CODEXTACTICS_API float GetMaxYawRateDeg(float SpeedCmS, float MinTurnRadiusCm);

	/** Turns CurrentYaw towards DesiredYaw by at most the speed-limited yaw rate over DeltaSeconds (degrees, -180..180). */
	CODEXTACTICS_API float StepYaw(float CurrentYawDeg, float DesiredYawDeg, float SpeedCmS, float MinTurnRadiusCm, float DeltaSeconds);

	/** A goal behind (>= ReverseMinAngleDeg off the heading) and within ReverseMaxDistanceCm is reached in reverse. */
	CODEXTACTICS_API bool ShouldReverse(const FVector2D& Forward, const FVector2D& ToGoal, const FVehicleDriveConfig& Config);

	/** Accelerates / brakes Current towards Target (signed speeds; braking when slowing down or changing direction). */
	CODEXTACTICS_API float StepSpeed(float CurrentCmS, float TargetCmS, const FVehicleDriveConfig& Config, float DeltaSeconds);

	/**
	 * Speed to aim for (unsigned): cruise / rush / reverse, capped by the braking distance to the goal (sqrt(2 a d)) and
	 * slowed to MinTurnSpeedCmS while the heading error exceeds TurnSlowdownAngleDeg.
	 */
	CODEXTACTICS_API float GetDesiredSpeed(bool bRush, bool bReverse, float RemainingCm, float HeadingErrorDeg, const FVehicleDriveConfig& Config);

	/**
	 * Pure pursuit: the point LookaheadCm further along Path (polyline, 2D distances) from the projection of Location onto
	 * the segment InOutSegment or later (the segment index only moves forward); past a corner sharper than 25 deg only 1 m
	 * more (no cutting the inside). The last point when the look-ahead runs off the end. OutRemainingCm = the path length
	 * still ahead of the projection.
	 */
	CODEXTACTICS_API FVector GetLookaheadPoint(const TArray<FVector>& Path, const FVector& Location, int32& InOutSegment, float LookaheadCm,
		float& OutRemainingCm);

	/**
	 * Where a click on the vehicle should probe for cover (the hull side the player looks at): in hull-local 2D, the
	 * point where the ray from LocalClick backwards along -LocalViewDir leaves the box of HalfExtent (centred at the origin),
	 * and that side's outward normal. False for a zero direction.
	 */
	CODEXTACTICS_API bool GetHullSideFacing(const FVector2D& LocalClick, const FVector2D& LocalViewDir, const FVector2D& HalfExtent,
		FVector2D& OutLocalPoint, FVector2D& OutLocalNormal);

	/** Engine state from the drive (stopped = Idle, moving fast = Rush, else Cruise). */
	CODEXTACTICS_API EVehicleEngineState GetEngineState(bool bRunning, float SpeedCmS, bool bRush);

	/** Hearing radius of the engine for patrols (data-driven, 0 when off). */
	CODEXTACTICS_API float GetNoiseRadiusCm(EVehicleEngineState State, const FVehicleEngineConfig& Config);

	/** Wheel spin for the vendor ABP's WheelSpeed (deg/s, signed). */
	CODEXTACTICS_API float GetWheelSpinDegPerSec(float SpeedCmS, float WheelRadiusCm);

	/** Front wheel steering angle (Ackermann, deg) for a yaw rate at Speed, clamped to MaxSteerAngleDeg. */
	CODEXTACTICS_API float GetSteerAngleDeg(float YawRateDegPerSec, float SpeedCmS, const FVehicleDriveConfig& Config);

	/** The effects of being mounted (bEngineRunning: the cabin is heated). */
	CODEXTACTICS_API FMountedOperativeEffects GetMountedEffects(bool bEngineRunning);

	/** Short display name of a seat ("Driver", "Gunner", "Passenger"). */
	CODEXTACTICS_API FString GetSeatName(EVehicleSeat Seat);

	/** Inverse of GetSeatName (None for anything else). */
	CODEXTACTICS_API EVehicleSeat ParseSeatName(const FString& Name);
}
