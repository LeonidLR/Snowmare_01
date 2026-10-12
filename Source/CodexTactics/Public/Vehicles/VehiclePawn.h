#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/VehicleRules.h"
#include "VehiclePawn.generated.h"

class AOperativeCharacter;
class UAnimInstance;
class UBoxComponent;
class UHeatSourceComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UVehicleNavObstacleComponent;

/** One crew member aboard and his seat. */
struct CODEXTACTICS_API FVehicleCrewMember
{
	TWeakObjectPtr<AOperativeCharacter> Operative;
	EVehicleSeat Seat = EVehicleSeat::None;
};

/** Outcome of a drive order. */
UENUM(BlueprintType)
enum class EVehicleDriveResult : uint8
{
	Started,
	/** Started, but the goal is off the vehicle navmesh / unreachable: it drives to the nearest reachable point. */
	StartedPartial,
	NoDriver,
	/** No vehicle navmesh under the vehicle or around the goal. */
	NoPath
};

/**
 * The squad's armoured personnel carrier (APC BTR-82, phases P0 + P1; docs/vehicles/APC_01_TECHNICAL.md approach B):
 * a kinematic pawn that drives along a path on the "Vehicle" nav agent's navmesh (pure pursuit, gameplay minimum turning
 * radius, automatic reverse for short goals behind, ground conform from 4 traces), blocks pawns / gives cover with its
 * hull box, carves the infantry navmesh while parked (UVehicleNavObstacleComponent), heats the cabin while the engine runs
 * and is heard by patrols. Crew members are mounted by UVehicleSubsystem (hidden, attached, co-located).
 * The visual Blueprint (BP_APC_BTR82) only sets the mesh / ABP; C++ feeds the vendor ABP's variables (WheelSpeed,
 * WheelsAngle, DoorAngle, ...) by name and raises On Visual Drive / On Doors Changed for polish. UE-only.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AVehiclePawn : public APawn
{
	GENERATED_BODY()

public:
	AVehiclePawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Components ----------------------------------------------------------------------------------------------

	/** Ground pivot (the mesh pivot, on the ground under the hull). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<USceneComponent> Root;

	/** Hull collider (the vendor mesh has none): blocks pawns, Visibility traces (clicks, cover) and the grid bake. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UBoxComponent> Hull;

	/** The vehicle model (BP: SK_East_APC_BTR82 + ABP_East_APC_BTR82); no collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** Warm cabin while the engine runs (the crew sits in it; also ~2 m round the hull). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UHeatSourceComponent> CabinHeat;

	/** The parked footprint on the infantry navmesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UVehicleNavObstacleComponent> NavObstacle;

	// --- Tunables (the Blueprint is the master copy) -------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleDriveConfig DriveConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleCrewConfig CrewConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleEngineConfig EngineConfig;

	/** Hull box centre relative to the pivot (BTR-82: the pivot is 58 cm behind the bounds centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Hull")
	FVector HullCentre = FVector(58.f, 0.f, 150.f);

	/** Hull box half extent (BTR-82 7.8 x 3.1 m, hull ~2.9 m high incl. ground clearance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Hull")
	FVector HullHalfExtent = FVector(389.f, 153.f, 125.f);

	/** Where the crew is attached (pivot space; inside the hull). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Crew")
	FVector CrewAttachOffset = FVector(40.f, 0.f, 120.f);

	/** Doors / hatches the crew walks to (pivot space, on the ground): side doors between axles 2 and 3, rear hatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Crew")
	TArray<FVector> EntryPoints;

	/** Ground probes for pitch / roll (pivot space XY): front, back, left, right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Hull")
	TArray<FVector2D> GroundProbes;

	/** Name of the vehicle nav agent (Config/DefaultEngine.ini SupportedAgents). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Navigation")
	FName NavAgentName = TEXT("Vehicle");

	/** Mesh used when the Mesh component has none (a C++-only spawn): the vendor model, never modified. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	TSoftObjectPtr<USkeletalMesh> DefaultMesh;

	/** Anim class used with DefaultMesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	TSoftClassPtr<UAnimInstance> DefaultAnimClass;

	/** Vendor ABP variables driven by name (float / double; missing ones are skipped). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimWheelSpeedVariable = TEXT("WheelSpeed");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimSteerVariable = TEXT("WheelsAngle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimDoorVariable = TEXT("DoorAngle");

	/** Door angle when open (vendor ABP units: degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	float DoorOpenAngle = 80.f;

	// --- Driving -------------------------------------------------------------------------------------------------

	/** Drives to Goal on the vehicle navmesh (bRush: full speed). Needs a driver aboard. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Vehicle")
	EVehicleDriveResult DriveTo(const FVector& Goal, bool bRush);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Vehicle")
	void StopDriving();

	/** Puts the vehicle at Location facing Yaw at once, parked on the ground (load / smokes). */
	void RestoreTransform(const FVector& Location, float Yaw);

	/** Heading (deg) the drive model steers by. */
	float GetHeadingYaw() const { return CurrentYaw; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	bool IsDriving() const { return bHasGoal; }

	/** Signed speed (cm/s, negative = reverse). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	float GetSpeed() const { return Speed; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	bool IsReversing() const { return bHasGoal && (bReverse || bManeuverReverse); }

	/** Stopped (speed below CrewConfig.StoppedSpeedCmS and no goal). */
	bool IsStopped() const;

	const TArray<FVector>& GetPathPoints() const { return PathPoints; }
	bool WasLastPathPartial() const { return bPathPartial; }

	/** The "Vehicle" agent's navmesh, or nullptr. */
	class ANavigationData* GetVehicleNavData() const;

	// --- Crew (UVehicleSubsystem mounts / dismounts) -------------------------------------------------------------

	const TArray<FVehicleCrewMember>& GetCrew() const { return Crew; }
	int32 GetCrewCount() const { return Crew.Num(); }
	int32 GetFreeSeats() const;
	EVehicleSeat GetSeatOf(const AOperativeCharacter* Operative) const;
	AOperativeCharacter* GetOccupant(EVehicleSeat Seat) const;
	bool HasDriver() const { return GetOccupant(EVehicleSeat::Driver) != nullptr; }

	/** Adds Operative and re-seats the whole crew (VehicleRules::AssignSeats). */
	void AddCrew(AOperativeCharacter* Operative);
	void RemoveCrew(AOperativeCharacter* Operative);
	/** Saved seats (load): keeps them when they are valid, else re-seats. */
	void SetCrewSeat(AOperativeCharacter* Operative, EVehicleSeat Seat);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	bool IsEngineRunning() const { return bEngineRunning; }

	void SetEngineRunning(bool bRunning);

	/** Opens / closes the doors (vendor ABP DoorAngle + On Doors Changed). */
	void SetDoorsOpen(bool bOpen);

	// --- Geometry ------------------------------------------------------------------------------------------------

	/** Entry point Index in world space (ground level). */
	FVector GetEntryPointWorld(int32 Index) const;

	/**
	 * Exit candidates round the hull (world, ground level, CrewConfig.ExitOffsetCm off the sides / the rear): three per
	 * side and two at the rear.
	 */
	TArray<FVector> GetExitCandidatesWorld() const;

	/** Hull box centre in world space. */
	FVector GetHullCentreWorld() const;

	/** True when World2D lies inside the hull footprint grown by MarginCm. */
	bool IsInsideFootprint(const FVector& World, float MarginCm = 0.f) const;

	/**
	 * A click on the hull seen along ViewDirection: the point on the hull side the player looks at (at 120 cm above the
	 * ground, just outside the surface) and that side's normal — where the cover probe starts.
	 */
	bool GetCoverProbe(const FVector& ClickPoint, const FVector& ViewDirection, FVector& OutPoint, FVector& OutNormal) const;

	// --- Blueprint polish ------------------------------------------------------------------------------------------

	/** Every tick while it moves (and once when it stops): speed (cm/s, signed) and the front wheel angle. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Visual Drive"))
	void ReceiveVisualDrive(float SpeedCmS, float SteerDeg);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Doors Changed"))
	void ReceiveDoorsChanged(bool bOpen);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Engine Changed"))
	void ReceiveEngineChanged(bool bRunning);

	/** Fired when the drive ends at its goal (UVehicleSubsystem raises VehicleArrived). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVehicleArrived, AVehiclePawn*);
	FOnVehicleArrived OnArrived;

	/** Fired when a drive gives up (blocked for too long / no progress). */
	FOnVehicleArrived OnDriveBlocked;

private:
	void UpdateDrive(float DeltaSeconds);
	void ConformToGround(float DeltaSeconds, bool bSnap);
	bool IsPawnAhead() const;
	void ApplyVisuals(float YawRateDeg, float DeltaSeconds);
	void SetAnimFloat(FName Variable, float Value) const;
	void UpdateEngineNoise(float DeltaSeconds);
	void SetParked(bool bParked);
	void ApplyNavObstacleBox();
	void ReseatCrew();
	void FinishDrive(bool bArrived);

	TArray<FVehicleCrewMember> Crew;
	TArray<FVector> PathPoints;
	int32 PathSegment = 0;
	FVector Goal = FVector::ZeroVector;
	float Speed = 0.f;
	float CurrentYaw = 0.f;
	float SmoothedPitch = 0.f;
	float SmoothedRoll = 0.f;
	float LastRemaining = 0.f;
	float NoProgressSeconds = 0.f;
	float BlockedSeconds = 0.f;
	float NoiseTimer = 0.f;
	float LastSteer = 0.f;
	bool bHasGoal = false;
	bool bRush = false;
	bool bReverse = false;
	/** K-turn: backing up at the start of a drive that points well away (see DriveTo). */
	bool bManeuverReverse = false;
	float ManeuverLeftCm = 0.f;
	bool bPathPartial = false;
	bool bEngineRunning = false;
	bool bDoorsOpen = false;
	bool bParkedState = true;
};
