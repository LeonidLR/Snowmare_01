#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/VehicleCombatRules.h"
#include "Vehicles/VehicleHullRules.h"
#include "Vehicles/VehicleRules.h"
#include "VehiclePawn.generated.h"

class AOperativeCharacter;
class UAnimInstance;
class UBoxComponent;
class UHeatSourceComponent;
class UParticleSystem;
class UParticleSystemComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UVehicleNavObstacleComponent;
class UVehicleTurretComponent;
class AEnemyCharacter;

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
	NoPath,
	/** The hull is disabled (P4): no engine until the Engineer repairs it. */
	Disabled
};

/**
 * The squad's armoured personnel carrier (APC BTR-82, phases P0 + P1; docs/vehicles/APC_01_TECHNICAL.md approach B):
 * a kinematic pawn that drives along a path on the "Vehicle" nav agent's navmesh (pure pursuit, gameplay minimum turning
 * radius, automatic reverse for short goals behind, ground conform from 4 traces), blocks pawns / gives cover with its
 * hull box, carves the infantry navmesh while parked (UVehicleNavObstacleComponent), heats the cabin while the engine runs
 * and is heard by patrols. Crew members are mounted by UVehicleSubsystem (hidden, attached, co-located).
 * The visual Blueprint (BP_APC_BTR82) only sets the mesh / ABP; C++ feeds the vendor ABP's variables (WheelSpeed,
 * WheelsAngle, DoorAngle, ...) by name and raises On Visual Drive / On Doors Changed for polish.
 * P4 (2026-10-12): the hull has health and an armour profile per damage source (VehicleHullRules); the crew inside is never
 * hit (the hull takes it). At 0 HP it is DISABLED, never destroyed: engine off, no driving, the turret silent, the damaged
 * model + wreck smoke, the crew ejected (UVehicleSubsystem); the Engineer repairs it (Y) to RepairFraction. UE-only.
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

	/** The turret (P2): autocannon + coaxial MG, manned by the gunner seat. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UVehicleTurretComponent> Turret;

	/** The disabled hull's model (P4; hidden while it runs; vendor SM_East_APC_BTR82_Damaged when empty). No collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UStaticMeshComponent> DamagedMesh;

	/** Smoke over the disabled hull (P4; vendor Cascade PS_Wreckage_01 when empty; a Niagara one is a Gemini request). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vehicle")
	TObjectPtr<UParticleSystemComponent> DamagedSmoke;

	// --- Tunables (the Blueprint is the master copy) -------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleDriveConfig DriveConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleCrewConfig CrewConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleEngineConfig EngineConfig;

	/** Running over small enemies / ramming the Brute (weapons_tuning.json "vehicle_weapons.apc_ram" overrides at BeginPlay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleCrushConfig CrushConfig;

	/** The fade to black when someone boards / gets out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleFadeConfig FadeConfig;

	/** Hull health, armour per source, repair (P4; weapons_tuning.json "vehicle_weapons.apc_hull" overrides at BeginPlay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle")
	FVehicleHullConfig HullConfig;

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

	/** The disabled model used when DamagedMesh has none (loaded the first time the hull is disabled). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	TSoftObjectPtr<UStaticMesh> DefaultDamagedMesh;

	/** The wreck smoke used when DamagedSmoke has no template. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	TSoftObjectPtr<UParticleSystem> DefaultWreckSmoke;

	/** Vendor ABP variables driven by name (float / double; missing ones are skipped). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimWheelSpeedVariable = TEXT("WheelSpeed");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimSteerVariable = TEXT("WheelsAngle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimDoorVariable = TEXT("DoorAngle");

	/**
	 * The vendor ABP's wheel clock (seconds): it turns the wheels by WheelSpeed x WheelSpeedOffset. C++ pins it to
	 * AnimWheelClockHold every tick and writes WheelSpeed = wheel angle / clock (VehicleRules::GetAnimWheelSpeedForAngle),
	 * so the wheels show exactly the angle rolled on the ground. None (or a missing variable): WheelSpeed = spin deg/s.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	FName AnimWheelOffsetVariable = TEXT("WheelSpeedOffset");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual", meta = (ClampMin = "0.1"))
	float AnimWheelClockHold = 1.f;

	/** Rolling direction of the vendor wheel bones for a positive (forward) spin (VehicleCombatSmoke checks it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Visual")
	float AnimWheelSpinSign = 1.f;

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

	/** Signed ground speed actually travelled last tick (cm/s along the heading; 0 when the hull did not move). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	float GetGroundSpeed() const { return GroundSpeed; }

	/** Wheel spin applied last tick (deg/s, signed; 0 while the hull stands) and the wheel angle shown (smokes). */
	float GetWheelSpinRate() const { return LastWheelSpin; }
	float GetWheelAngle() const { return WheelAngleDeg; }

	/** Reads a float variable of the mesh's anim instance (smokes / diagnostics); false when missing. */
	bool GetAnimFloat(FName Variable, float& OutValue) const;
	/** Writes a float variable of the mesh's anim instance (smokes probe the vendor rig). */
	void SetAnimFloatForProbe(FName Variable, float Value) const { SetAnimFloat(Variable, Value); }

	/** Small enemies run over / Brutes knocked down by this vehicle so far (smokes). */
	int32 GetCrushCount() const { return CrushCount; }
	int32 GetRamKnockdownCount() const { return RamKnockdownCount; }
	/** Seconds the hull was held by a body it may not run over (operative / NPC / standing Brute). */
	float GetBlockedSeconds() const { return BlockedSeconds; }
	/** The last body that stopped the hull (smokes). */
	AActor* GetLastBlocker() const { return LastBlocker.Get(); }

	/** Stopped (speed below CrewConfig.StoppedSpeedCmS and no goal). */
	bool IsStopped() const;

	const TArray<FVector>& GetPathPoints() const { return PathPoints; }
	bool WasLastPathPartial() const { return bPathPartial; }

	/** The "Vehicle" agent's navmesh, or nullptr. */
	class ANavigationData* GetVehicleNavData() const;

	// --- Hull (P4) ---------------------------------------------------------------------------------------------------

	/**
	 * Damage to the hull: Amount through the armour factor of Source (VehicleHullRules::GetHullDamage; a Mine = the flat
	 * mine damage). Damage number, VehicleDamaged event (Kind = the source), feed line at the low mark; at 0 HP the hull is
	 * disabled (OnDisabled: the subsystem ejects the crew). Nothing once disabled. Returns the health lost.
	 */
	float ApplyHullDamage(float Amount, EVehicleDamageSource Source, const FString& AttackerName, AActor* Attacker = nullptr);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	float GetHullHealth() const { return HullState.Health; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	float GetHullMaxHealth() const { return FMath::Max(1.f, HullConfig.MaxHealth); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vehicle")
	bool IsDisabled() const { return HullState.bDisabled; }

	const FVehicleHullState& GetHullState() const { return HullState; }

	/** Health lost to Source so far (smokes / telemetry). */
	float GetDamageTaken(EVehicleDamageSource Source) const;

	/** The Engineer's repair is done: running again at RepairFraction (VehicleRepaired is raised by the subsystem). */
	void RepairHull();

	/** Saved hull (load): health / disabled, the visuals and engine follow. */
	void RestoreHull(float Health, bool bDisabled);

	/** The point of the hull footprint's side nearest From, at ground level (enemies close in on it). */
	FVector GetClosestHullPoint(const FVector& From) const;

	/** Where an attacker stands to hit the hull: the nearest hull point pushed StandOffCm out of the side, ground level. */
	FVector GetHullAttackPoint(const FVector& From, float StandOffCm) const;

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

	/** 2D distance from World to the hull box side (0 inside / on it). */
	float GetHullDistance2D(const FVector& World) const;

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

	/** Each cannon shell / MG round (sound and extra FX polish; C++ already plays the vendor muzzle flash + tracer). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Turret Fired"))
	void ReceiveTurretFired(EVehicleTurretWeapon Weapon, FVector Muzzle);

	/** A small enemy died under the wheels (blood / snow FX, crunch sound polish). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Ran Over"))
	void ReceiveRanOver(AActor* Victim, FVector Location);

	/** The hull took damage (P4; sparks / clang polish; Damage after the armour). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Hull Damaged"))
	void ReceiveHullDamaged(float Damage, EVehicleDamageSource Source);

	/** Disabled at 0 HP / repaired (P4; C++ already swaps to DamagedMesh and runs DamagedSmoke). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Vehicle", meta = (DisplayName = "On Disabled Changed"))
	void ReceiveDisabledChanged(bool bDisabled);

	/** Fired when the drive ends at its goal (UVehicleSubsystem raises VehicleArrived). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVehicleArrived, AVehiclePawn*);
	FOnVehicleArrived OnArrived;

	/** Fired when a drive gives up (blocked for too long / no progress). */
	FOnVehicleArrived OnDriveBlocked;

	/** The hull was disabled (0 HP, P4): the subsystem ejects the crew. */
	FOnVehicleArrived OnDisabled;

private:
	void UpdateDrive(float DeltaSeconds);
	void ConformToGround(float DeltaSeconds, bool bSnap);
	/**
	 * The bodies in the hull's path this tick (VehicleCombatRules::EvaluateContact): small enemies are crushed (fast) or
	 * shoved aside (slow), a Brute is knocked down (fast) or stops the hull, operatives / NPCs stop it. Returns true when
	 * the hull must brake (OutBlocker = who).
	 */
	bool HandleContactsAhead(float DeltaSeconds, AActor*& OutBlocker);
	void CrushEnemy(AEnemyCharacter* Enemy);
	void RamEnemy(AEnemyCharacter* Enemy);
	void ApplyVisuals(float YawRateDeg, float DeltaSeconds);
	/** Writes the wheel angle to the vendor ABP (its clock pinned): also while the vehicle logic is held. */
	void PushWheelPose(float DeltaSeconds);
	void SetAnimFloat(FName Variable, float Value) const;
	void UpdateEngineNoise(float DeltaSeconds);
	void SetParked(bool bParked);
	void ApplyNavObstacleBox();
	void ReseatCrew();
	void FinishDrive(bool bArrived);
	/** Damaged model + wreck smoke on, the running model off (and back). */
	void ApplyDisabledVisuals(bool bDisabled);

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
	float GroundSpeed = 0.f;
	float LastWheelSpin = 0.f;
	float WheelAngleDeg = 0.f;
	/** Seconds since the drive started from a standstill (the acceleration ramp). */
	float LaunchSeconds = 0.f;
	FVector LastTickLocation = FVector::ZeroVector;
	bool bHasLastTickLocation = false;
	int32 CrushCount = 0;
	int32 RamKnockdownCount = 0;
	TWeakObjectPtr<AActor> LastBlocker;
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
	/** Hull health / disabled (P4); set from HullConfig at BeginPlay unless a save restored it first. */
	FVehicleHullState HullState;
	bool bHullInitialised = false;
	float DamageBySource[8] = {};
};
