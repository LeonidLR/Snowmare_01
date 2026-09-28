#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Characters/OperativeMovementRules.h"
#include "Combat/TargetedShotRules.h"
#include "Interactables/DeployableRules.h"
#include "OperativeCharacter.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWeaponFiredDynamic, AOperativeCharacter*, Operative, AActor*, Target, bool, bHit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWeaponMisfiredDynamic, AOperativeCharacter*, Operative);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnWeaponFiredNative, AOperativeCharacter*, AActor*, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnWeaponMisfiredNative, AOperativeCharacter*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnOperativeStanceChanged, AOperativeCharacter*, Operative, EOperativeStance, OldStance, EOperativeStance, NewStance);

/**
 * Body shape of one stance. The capsule applies to every operative Blueprint; the placeholder fields only
 * drive the built-in cylinder body, which is shown while the Blueprint has no skeletal mesh.
 * Godot reference: player.gd `set_stance` (capsule height 2.0 / 1.3 / 0.7 m, placeholder mesh squash).
 */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeStanceShape
{
	GENERATED_BODY()

	/** Collision capsule half height, cm (feet stay on the ground when it changes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance", meta = (ClampMin = "10"))
	float CapsuleHalfHeight = 90.f;

	/** Placeholder body scale (engine cylinder: 100 cm diameter, 100 cm tall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance|Placeholder")
	FVector PlaceholderScale = FVector(0.7f, 0.7f, 1.8f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance|Placeholder")
	FRotator PlaceholderRotation = FRotator::ZeroRotator;

	/** Height of the placeholder body centre above the feet, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance|Placeholder")
	float PlaceholderCenterHeight = 90.f;

	/** Facing marker position: X forward, Z above the feet, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance|Placeholder")
	FVector MarkerOffset = FVector(40.f, 0.f, 140.f);
};

/** Outcome of a move order. */
UENUM(BlueprintType)
enum class EOperativeOrderResult : uint8
{
	Accepted,
	/** No AI controller possesses the operative. */
	NoController,
	/** No navigable path to the destination. */
	Unreachable
};

/**
 * A squad member. Always driven by an AOperativeAIController (NavMesh + crowd avoidance);
 * the player controller only issues orders. Leader/follower roles are managed by USquadSubsystem.
 * Godot reference: Scenes/movements/player.gd (set_target, stop_movement, set_stance, can_sprint,
 * _process_leader_movement).
 * Meant to be subclassed by a Blueprint (BP_Operative) that owns the look: skeletal mesh, AnimBP, materials,
 * capsule and stance shapes. Without a skeletal mesh a tinted placeholder cylinder shows the stance.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AOperativeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AOperativeCharacter();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Player move order: path to Destination over the NavMesh; sprint if requested and allowed. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	EOperativeOrderResult OrderMoveTo(const FVector& Destination, bool bSprint);

	/** Formation move issued by the squad: path to Destination at a fixed speed, keeps stance and sprint state. */
	EOperativeOrderResult FollowTo(const FVector& Destination, float Speed);

	/** Stops any movement immediately. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void StopOperative();

	/** Changes stance; sprinting ends unless standing. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void SetStance(EOperativeStance NewStance);

	/** Starts or stops sprinting; ignored when sprinting is not allowed. */
	void SetSprinting(bool bNewSprinting);

	/** Applies role body color and material tint to the operative mesh. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void ApplyBodyColor();

	/** Configures squad index, name, and role color, then applies visuals. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void SetSquadIdentity(int32 InSquadIndex, const FText& InName, const FLinearColor& InColor);

	/** Called by the AI controller when a move request finishes. */
	void HandleMoveFinished();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool CanSprint() const;

	/** Max ground speed for the current stance/sprint/wound/carry state, cm/s. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetMaxSpeed() const;

	/** True while the operative has a move order in progress or still visibly moves. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsMoving() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	EOperativeStance GetStance() const { return Stance; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsSprinting() const { return bSprinting; }

	/** Position in the squad roster (0 = commander); selection keys 1..N map to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	int32 SquadIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FText DisplayName;

	/** Placeholder body tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FLinearColor BodyColor = FLinearColor(0.2f, 0.5f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FOperativeMovementConfig MovementConfig;

	/** Stance shapes (capsule; placeholder body). Godot heights 2.0 / 1.3 / 0.7 m scaled to the 1.8 m capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance")
	FOperativeStanceShape StandingShape;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance")
	FOperativeStanceShape CrouchingShape;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance")
	FOperativeStanceShape ProneShape;

	/** How fast the placeholder body blends between stances (Godot tween 0.15 s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Stance", meta = (ClampMin = "0.1"))
	float StanceBlendSpeed = 15.f;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Stance")
	const FOperativeStanceShape& GetStanceShape(EOperativeStance InStance) const;

	/** True while the Blueprint has no skeletal mesh and the placeholder cylinder is shown. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool UsesPlaceholderBody() const;

	/** Fired after every stance change (AnimBP, sounds, UI). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Stance")
	FOnOperativeStanceChanged OnStanceChanged;

	/** Blueprint hook for stance changes. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Stance", meta = (DisplayName = "On Stance Changed"))
	void ReceiveStanceChanged(EOperativeStance OldStance, EOperativeStance NewStance);

	/** Localised stance name («СТОЯ», «СИДЯ», «ЛЁЖА»). */
	static FText GetStanceDisplayName(EOperativeStance InStance);

	/** Cold level in percent. Owned by the cold survival system once ported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative", meta = (ClampMin = "0", ClampMax = "100"))
	float ColdLevel = 0.f;

	/** Squad role: defusal skill and which deployables this operative collects first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative")
	EOperativeRole SquadRole = EOperativeRole::Commander;

	/** Luck, % (Godot luck: commander 25, engineer 30, medic-sapper 35); +0.5 % defusal chance per point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative", meta = (ClampMin = "0", ClampMax = "100"))
	float Luck = 25.f;

	/** Carried engineering items (Godot turrets_count / barricades_count / mines_count; start 0, from loot / dismantling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 TurretsCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 BarricadesCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 MinesCount = 0;

	/** Hand grenades, used to trap objects (Godot starting_grenades 2, max_grenades 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 GrenadesCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 MaxGrenades = 4;

	/** Deployable type the F key sets up next (Godot selected_deployable_type). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Inventory")
	EDeployableType SelectedDeployType = EDeployableType::Turret;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Inventory")
	int32 GetDeployableCount(EDeployableType Type) const;

	/** Adds (or removes, negative Delta) carried items, clamped to 0..max. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	void AddDeployable(EDeployableType Type, int32 Delta);

	/** Next type in the Godot cycle order turret -> barricade -> mine (skips empty types when possible). */
	EDeployableType CycleDeployableType();

	/** Placement / relocation radius outside the pause and preparation, cm (Godot placement_radius 15 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	float PlacementRadius = 1500.f;

	/** Cannot lift / push objects at or above this cold, % (Godot max_cold_to_lift_objects 80). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0", ClampMax = "100"))
	float MaxColdToLiftObjects = 80.f;

	/** Cannot lift / push objects below this health fraction (Godot min_health_percent_to_lift 0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0", ClampMax = "1"))
	float MinHealthFractionToLift = 0.5f;

	/** Starts / stops carrying or pushing an object (carry speed, no sprint). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void SetCarrying(bool bNewCarrying);

	/** Provisions (Godot player.gd defaults for every role: medkit 1, canned food 2, chocolate 2, bread 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 MedkitsCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 CannedFoodCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 ChocolateCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 BreadCount = 0;

	/** Ammo for weapons not ported yet, by Godot weapon id (pistol, shotgun, flamethrower, cryo_emitter, plasma_carbine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory")
	TMap<FName, int32> ExtraAmmo;

	/** Bonus weapon / clothing ids found in crates (weapon switching comes with the weapon system). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory")
	TArray<FString> BonusItems;

	/** Personal matches for lighting barrels (Godot game_balance_config *_matches_count: 3 each). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 MatchesCount = 3;

	/** Heavy wound flag. Owned by the health system once ported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative")
	bool bWounded = false;

	/** Carrying/pushing an object. Owned by the relocation system once ported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative")
	bool bCarrying = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TObjectPtr<class UHealthComponent> HealthComponent;

	/** Weapon visual on the skeletal mesh (the Blueprint sets the mesh and its offset from WeaponSocket). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	/** Skeletal mesh material slot tinted with the role colour (Godot tints only the coat). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FName RoleColorMaterialSlot = TEXT("Explorer_Coat");

	/** Vector parameter of that material receiving BodyColor (Interchange glTF materials: BaseColorFactor). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FName RoleColorParameter = TEXT("BaseColorFactor");

	/** Skeletal mesh bone or socket the weapon follows (Godot BoneAttachment3D hand_r). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Operative")
	FName WeaponSocket = TEXT("hand_r");

	/** Real-time cold (writes ColdLevel, speed tier, weapon freeze, frostbite). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TObjectPtr<class UColdSurvivalComponent> ColdSurvival;

	/** Speed multiplier of the current cold tier (Godot speed_multiplier 1 / 0.7 / 0.45 / 0.25). */
	void SetColdSpeedMultiplier(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetColdSpeedMultiplier() const { return ColdSpeedMultiplier; }

	// --- Combat & Weapon System (Godot player.gd parity) ---

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void EquipWeapon(class UWeaponDataAsset* NewWeapon);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void StartReload();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	bool CanShoot() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	AActor* FindBestCombatTarget() const;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void ProcessCombatShooting(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	bool ShootAtTarget(AActor* Target);

	// --- Ctrl + click targeted shots (Godot main.gd Ctrl branch, player.gd shoot_at_* / set_manual_priority_target) ---

	/** Marksmanship, % (Godot accuracy: commander 90, engineer 75, medic-sapper 85); drives remote mine shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0", ClampMax = "100"))
	float Accuracy = 90.f;

	/** What a Ctrl + click on this actor aims at (None: not a valid target). */
	static ETargetedShotKind ClassifyShotTarget(const AActor* Target);

	/**
	 * Fires one aimed round at a barrel (explodes), a mine (hit chance by stance / distance / cold), a supply crate or
	 * a trapped object (remote detonation). Stops the operative and consumes ammo. False when the weapon cannot fire.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	bool ShootAtObject(AActor* Target);

	/** Enemy fired at first while it is alive, in range and in the line of fire (Godot manual_priority_target). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void SetManualPriorityTarget(AActor* Enemy);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	AActor* GetManualPriorityTarget() const { return ManualPriorityTarget.Get(); }

	/** Tactical pause: remembers a targeted shot (one per kind) executed when the pause is released. */
	void PlanTargetedShot(AActor* Target);

	/** Executes the planned shots in Godot order: barrel, mine, crate, trapped object, then the priority enemy. */
	void ExecutePlannedTargetedShots();

	void ClearPlannedTargetedShots() { PlannedShots.Reset(); }

	int32 GetPlannedTargetedShotCount() const { return PlannedShots.Num(); }

	/** Tracer start: the feet plus the Godot muzzle height of the stance (1.4 / 0.85 / 0.25 m). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	FVector GetMuzzleLocation() const;

	/** Godot _can_begin_weapon_shot: alive, not reloading, no misfire delay, weapon not frozen, round in the clip. */
	bool CanBeginWeaponShot();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat")
	TObjectPtr<class UWeaponDataAsset> CurrentWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat")
	int32 CurrentClip = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat")
	int32 ReserveAmmo = 120;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Combat")
	bool bIsReloading = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Combat")
	float ReloadTimer = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Combat")
	float ShootTimer = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Combat")
	float MisfireCooldownTimer = 0.0f;

	UPROPERTY(Transient)
	bool bForceMisfireForTesting = false;

	UPROPERTY(Transient)
	bool bForceHitForTesting = false;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Combat")
	FOnWeaponFiredDynamic OnWeaponFired;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Combat")
	FOnWeaponMisfiredDynamic OnWeaponMisfired;

	FOnWeaponFiredNative OnWeaponFiredNative;
	FOnWeaponMisfiredNative OnWeaponMisfiredNative;

private:
	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);
	/** Pushes max speed and turn rate for the current state into CharacterMovement. */
	void ApplyMovementParams(float SpeedOverride = -1.f);
	EOperativeOrderResult RequestMove(const FVector& Destination);
	/** Resizes the capsule for the stance, keeping the feet in place and the skeletal mesh on the ground. */
	void ApplyStanceCapsule();
	/** Shows the placeholder only without a skeletal mesh. */
	void UpdatePlaceholderVisibility();
	/** Blends the placeholder body towards the stance shape (Alpha 1 = snap). */
	void UpdatePlaceholderPose(float Alpha);

	/** Godot _consume_ammo_after_shot: one round; reload when the clip runs dry. */
	void ConsumeAmmoAfterShot();
	/** Stops, drops the sprint and turns to face Location. */
	void StopAndFace(const FVector& Location);
	/** True when the priority target can be fired at now (range, line of fire; barricades block only prone shooters). */
	bool CanFireAtPriorityTarget(const AActor* Target, float MaxRange) const;

	mutable TWeakObjectPtr<AActor> ManualPriorityTarget;
	TMap<ETargetedShotKind, TWeakObjectPtr<AActor>> PlannedShots;

	/** Placeholder body; hidden automatically once the Blueprint assigns a skeletal mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FacingMarker;

	/** Skeletal mesh Z offset set by the Blueprint for the standing capsule. */
	float MeshBaseZ = 0.f;
	bool bMeshBaseCaptured = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	EOperativeStance Stance = EOperativeStance::Standing;
	float ColdSpeedMultiplier = 1.f;
	bool bSprinting = false;
	bool bHasMoveOrder = false;
};
