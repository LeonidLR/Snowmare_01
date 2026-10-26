#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Vehicles/VehicleCombatRules.h"
#include "Vehicles/VehicleHullRules.h"
#include "VehicleTurretComponent.generated.h"

class AActor;
class AEnemyCharacter;
class AOperativeCharacter;
class AVehiclePawn;
class UParticleSystem;
class UParticleSystemComponent;

/**
 * The APC's turret (phase P2, docs/vehicles/APC_02_DESIGN.md §1.4; UE-only): manned by the gunner seat (Commander, else
 * anyone aboard; nothing without a gunner). In real time the gunner picks targets by his fire posture
 * (VehicleCombatRules::ChooseTarget: Passive holds, Defensive answers within 20 m once provoked, Aggressive engages
 * anything visible) or the Ctrl+click priority target, slews the turret / gun (vendor ABP TurretHorizontalAngle /
 * GunElevation) and fires the 30 mm autocannon (HE, splash, limited shells per map) or the coaxial MG (unlimited,
 * overheats). User decision 2026-10-26 (reverses the 2026-10-12 "no suppression stagger"): the MG and the cannon SUPPRESS - every
 * round is reported to USuppressionSubsystem with machine-gun / cannon weighting (docs/design/SUPPRESSION.md; still no stagger
 * status). Line of sight from the muzzle socket gun_jntSocket; nothing within the dead zone round the hull. Shots: tracers + muzzle flash (vendor Cascade PS_MuzzleFire_01), damage numbers through
 * UHealthComponent, kill credit = the gunner (AttackerSource = his name), noise for enemies.
 * Events VehicleTurretFired (Kind = "Cannon" / "MachineGun") and VehicleTurretKill.
 * P4: silent while the hull is disabled; a Ctrl+click on a barrel / mine / trapped object = one cannon shell at it once
 * aimed (range, dead zone, line of fire), detonating it like an operative's targeted shot (shared
 * AOperativeCharacter::DetonateShotTarget); costs a shell. Auto-fire stays enemy-only.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UVehicleTurretComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVehicleTurretComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Gun numbers (the Blueprint is the master copy; weapons_tuning.json "vehicle_weapons.apc_turret" overrides at BeginPlay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Turret")
	FVehicleTurretConfig Config;

	/** Muzzle socket on the vehicle mesh (vendor: gun_jntSocket). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Turret")
	FName MuzzleSocket = TEXT("gun_jntSocket");

	/** Vendor ABP variables (degrees): turret yaw relative to the hull and gun elevation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Turret")
	FName AnimTurretYawVariable = TEXT("TurretHorizontalAngle");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Turret")
	FName AnimGunPitchVariable = TEXT("GunElevation");

	/** Sign of the ABP turret angle relative to UE yaw (vendor rig: measured by VehicleSmoke). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Turret")
	float AnimTurretYawSign = 1.f;

	/** Sign of the ABP gun elevation relative to "up" (vendor rig: measured by VehicleSmoke). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Turret")
	float AnimGunPitchSign = 1.f;

	/** Muzzle flash (vendor Cascade emitter; a Niagara replacement is a Gemini request). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Vehicle|Turret")
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	// --- Orders / queries ---------------------------------------------------------------------------------------------

	/** Ctrl+click: the gunner's priority target (obeyed in every posture while it can be engaged). */
	void SetPriorityTarget(AActor* Target);
	AActor* GetPriorityTarget() const { return PriorityTarget.Get(); }

	/**
	 * Ctrl+click on an object (P4): checks it now (VehicleHullRules::CheckObjectShot) and, when it may be shelled, makes it
	 * the gun's next shot (the enemy targets wait; dropped after ObjectTargetTimeoutSeconds or once it cannot be shelled).
	 */
	EVehicleObjectShot SetObjectTarget(AActor* Object);
	AActor* GetObjectTarget() const { return ObjectTarget.Get(); }
	/** The check of a cannon shot at Object from where the turret is now. */
	EVehicleObjectShot CheckObjectShot(const AActor* Object) const;
	/** Shells fired at objects so far (smokes). */
	int32 GetObjectShots() const { return ObjectShots; }

	/** An object order gives up after this long without a shot (turret slewing, line lost). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vehicle|Turret", meta = (ClampMin = "0.5"))
	float ObjectTargetTimeoutSeconds = 6.f;

	/** The gunner (nullptr = the turret is unmanned). */
	AOperativeCharacter* GetGunner() const;

	int32 GetShellsLeft() const { return ShellsLeft; }
	int32 GetShellsMax() const { return ShellsMax; }
	/** Restores the shells (save / load). */
	void SetShellsLeft(int32 Shells);
	float GetMgHeat() const { return MgHeat; }
	bool IsMgOverheated() const { return bMgOverheated; }
	AActor* GetCurrentTarget() const;
	EVehicleTurretWeapon GetCurrentWeapon() const { return CurrentWeapon; }
	/** Turret yaw relative to the hull and gun pitch (degrees, UE sense: yaw left-handed like actors, pitch up > 0). */
	float GetTurretYaw() const { return TurretYaw; }
	float GetGunPitch() const { return GunPitch; }
	int32 GetShotsFired(EVehicleTurretWeapon Weapon) const { return Weapon == EVehicleTurretWeapon::Cannon ? CannonShots : MgRounds; }
	int32 GetKills() const { return Kills; }
	/** Seconds the gun was blocked by the dead zone with an otherwise engageable enemy (smokes). */
	int32 GetDeadZoneRefusals() const { return DeadZoneRefusals; }

	/** The muzzle in world space (the mesh socket, else above the hull centre). */
	FVector GetMuzzleLocation() const;

	/** HUD line (VehicleCombatRules::GetHudLine). */
	FString GetHudLine() const;

	/** Turret yaw / gun pitch set at once (smokes: probing the vendor rig). */
	void SetAimImmediate(float YawDeg, float PitchDeg);

private:
	AVehiclePawn* GetVehicle() const;
	void InitShells();
	void GatherCandidates(TArray<VehicleCombatRules::FTurretCandidate>& OutCandidates, TArray<AEnemyCharacter*>& OutEnemies) const;
	bool HasLineOfFire(const FVector& From, const AActor* Target) const;
	void Retarget();
	void UpdateAim(float DeltaTime);
	void FireCannon(AEnemyCharacter* Target, AOperativeCharacter* Gunner);
	void FireMgRound(AEnemyCharacter* Target, AOperativeCharacter* Gunner);
	void FireCannonAtObject(AActor* Object, AOperativeCharacter* Gunner);
	bool HasLineOfFireToObject(const FVector& From, const AActor* Object) const;
	/** Turret held: no gunner, turn-based, the world AI pause or a disabled hull. */
	bool IsHeld() const;
	void ApplyHit(AEnemyCharacter* Enemy, float Amount, EDamageType Type, float ArmourPenetration, AOperativeCharacter* Gunner,
		EVehicleTurretWeapon Weapon);
	void PlayMuzzleFlash(bool bCannon);
	void RaiseFired(AActor* Target, EVehicleTurretWeapon Weapon);
	void PushAnim() const;

	TWeakObjectPtr<AActor> PriorityTarget;
	TWeakObjectPtr<AEnemyCharacter> CurrentTarget;
	TWeakObjectPtr<AActor> ObjectTarget;
	float ObjectTargetAge = 0.f;
	int32 ObjectShots = 0;
	EVehicleTurretWeapon CurrentWeapon = EVehicleTurretWeapon::None;
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystemComponent> FlashComponent;
	float FlashLeft = 0.f;
	float RetargetTimer = 0.f;
	float CannonTimer = 0.f;
	int32 BurstLeft = 0;
	float MgRoundAccumulator = 0.f;
	float MgFiredEventTimer = 0.f;
	float NoiseTimer = 0.f;
	float IdleSeconds = 0.f;
	float MgHeat = 0.f;
	bool bMgOverheated = false;
	bool bShellsInitialised = false;
	int32 ShellsLeft = 0;
	int32 ShellsMax = 0;
	float TurretYaw = 0.f;
	float GunPitch = 0.f;
	float DesiredYaw = 0.f;
	float DesiredPitch = 0.f;
	int32 CannonShots = 0;
	int32 MgRounds = 0;
	int32 Kills = 0;
	int32 DeadZoneRefusals = 0;
	bool bMgFiringNow = false;
};
