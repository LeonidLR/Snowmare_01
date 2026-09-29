#pragma once

#include "CoreMinimal.h"
#include "Combat/AIGrenadeRules.h"
#include "Combat/SquadFireRules.h"
#include "GameFramework/Character.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/PersonalItemRules.h"
#include "Combat/TargetedShotRules.h"
#include "Interactables/DeployableRules.h"
#include "OperativeCharacter.generated.h"

class UStaticMeshComponent;
class UWeaponDataAsset;

/** Clip / reserve of one weapon of the arsenal (Godot ammo_inventory entry). */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FWeaponAmmoState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	int32 Clip = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	int32 Reserve = 0;
};
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
	Unreachable,
	/** Raging: player orders are ignored (Godot set_target «В ЯРОСТИ! НЕ ПОДЧИНЯЕТСЯ!»). */
	Refused
};

/** A target the operative can shoot now (Godot _find_shoot_target result). */
struct FShootCandidate
{
	AActor* Enemy = nullptr;
	float Distance = 0.f;
	/** 0.8 crouched behind a barricade. */
	float Cover = 1.f;
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

	/** Godot is_behind_barricade: a standing barricade within 2.2 m (centre 1 m above the feet to the barricade's base). */
	bool IsBehindBarricade() const;

	/** Godot is_in_barricade_cover: crouched behind a barricade (action bar shield). */
	bool IsInBarricadeCover() const { return Stance == EOperativeStance::Crouching && IsBehindBarricade(); }

	/** Godot set_facing_point (Shift + click): stops and turns towards Point. */
	void SetFacingPoint(const FVector& Point);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool CanSprint() const;

	/** Godot is_wounded: health below MovementConfig.WoundedHealthThreshold (or bWounded forced). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsWounded() const;

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

	/**
	 * Holds its position as a guard (Godot is_guarding): out of the formation, keeps facing, orders still move it.
	 * Toggled by USquadSubsystem::ToggleGuard (T / action bar «ОБОР»).
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Squad")
	bool bGuarding = false;

	/**
	 * Squad member (Godot group "squad"). A recruit spawned with false (Godot recruit_susanin.gd is_recruited) stays out
	 * of the squad subsystem — no selection, no formation, no targeting by enemies, no shooting — until
	 * URecruitSubsystem recruits it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative")
	bool bRecruited = true;

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

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Inventory")
	int32 GetItemCount(EPersonalItem Item) const;

	/**
	 * Godot use_personal_item / heal_with_item: spends one item for its health / warmth. False without the item or at
	 * full health with no cold.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	bool UsePersonalItem(EPersonalItem Item);

	/** Ammo for weapons outside the arsenal, by Godot weapon id (shotgun, flamethrower, cryo_emitter, plasma_carbine). */
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

	/** Rage after repeated crits from one enemy (Godot RageComponent). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TObjectPtr<class URageComponent> RageComponent;

	/** Raging now (orders refused, chaotic fire). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsRaging() const;

	/** Speed multiplier of the current cold tier (Godot speed_multiplier 1 / 0.7 / 0.45 / 0.25). */
	void SetColdSpeedMultiplier(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetColdSpeedMultiplier() const { return ColdSpeedMultiplier; }

	// --- Combat & Weapon System (Godot player.gd parity) ---

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void EquipWeapon(class UWeaponDataAsset* NewWeapon);

	// --- Arsenal (Godot player.gd available_weapons / ammo_inventory / switch_to_weapon_by_id) ---

	/**
	 * Godot _init_weapons: every operative carries the arsenal (M16, pistol, grenade, knife); full clips, reserve M16
	 * RifleReserve (60), pistol 24, others 0; the grenade clip is 1 while grenades are left. The first weapon is equipped.
	 */
	void InitArsenal(const TArray<UWeaponDataAsset*>& Weapons, int32 RifleReserve);

	/** Godot switch_to_weapon_by_id: the current clip / reserve go back to the inventory, the new ones come out. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	bool SwitchToWeaponById(const FString& WeaponId);

	/** Clip / reserve of an arsenal weapon (the equipped one reports its live values). */
	FWeaponAmmoState GetAmmoState(const FString& WeaponId) const;

	/** Reserve rounds of a weapon: arsenal weapon (live if equipped) or ExtraAmmo. */
	int32 GetReserve(const FString& WeaponId) const;

	/** Removes up to Max reserve rounds of a weapon (arsenal or ExtraAmmo); returns how many. */
	int32 TakeReserve(const FString& WeaponId, int32 Max);

	/** Adds reserve rounds of a weapon: arsenal weapon (live if equipped) or ExtraAmmo. */
	void AddAmmo(const FString& WeaponId, int32 Count);

	/** False for melee weapons (Godot uses_ammo). */
	bool UsesAmmo() const;

	/** Weapons this operative can switch to (Godot available_weapons). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat")
	TArray<TObjectPtr<UWeaponDataAsset>> AvailableWeapons;

	/** Clip / reserve of the weapons not in hands, by Godot weapon id. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Combat")
	TMap<FString, FWeaponAmmoState> AmmoInventory;

	/** Godot grenade_damage / grenade_effect_radius / grenade_throw_range exports (85, 4 m, 12 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float GrenadeDamage = 85.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float GrenadeEffectRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float GrenadeThrowRange = 1200.f;

	/**
	 * Length of the throw animation, s; the grenade leaves the hand at 70 % of it (Godot get_grenade_throw_duration,
	 * 2.0 without an animation). The Blueprint sets it to its montage length.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float GrenadeThrowDuration = 2.f;

	/** The operative throws a grenade now (AnimBP / montage hook; Godot play_grenade_throw). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Combat", meta = (DisplayName = "On Grenade Throw"))
	void ReceiveGrenadeThrow();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void StartReload();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	bool CanShoot() const;

	/**
	 * An attack on this operative (Godot player.gd take_damage): dodge with luck * 0.4 %, then
	 * max(1, Amount * stance defense * (1 - clamp(fortitude * 1.5 %, 0, 50 %))); bBypassAvoidance (grenades, traps
	 * on the squad) skips both and takes max(1, Amount). Floating «💨 УКЛОНЕНИЕ!», «-N» or «💥 КРИТИЧЕСКИЙ УДАР! -N».
	 * Returns the health taken.
	 */
	float TakeHit(float Amount, const FString& Attacker, bool bCrit = false, bool bBypassAvoidance = false, AActor* AttackerActor = nullptr);

	/** Forces the next TakeHit dodge roll (smokes): 1 dodges, 0 never. Negative = random. */
	UPROPERTY(Transient)
	float ForcedDodgeRollForTesting = -1.f;

	/**
	 * The enemy this operative would shoot now, without the target-switch memory (Godot _find_shoot_target: the manual
	 * priority target while it can be hit, else the closest enemy in range with a line of fire).
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	AActor* FindBestCombatTarget() const;

	/**
	 * Godot _find_shoot_target(delta): priority target, else the closest visible enemy; a current target is kept until a
	 * much closer one (stance ratio, or within 3.5 m) stays closer for the stance's reaction delay. Barricade rules
	 * decide cover (0.8 crouched) and block prone shooters («🚫 Баррикада блокирует огонь»).
	 */
	FShootCandidate FindShootTarget(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void ProcessCombatShooting(float DeltaTime);

	/**
	 * Godot _shoot_at_target: misfire, hit roll, then weapon damage * stance * Cover * crit (luck %, x2) * elevation
	 * (+15 %) * distance factor; cryo / fire weapons chill / warm the shooter.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	bool ShootAtTarget(AActor* Target, float Cover = 1.f);

	/** Target switching per stance (Godot stance_target_switch_delay_* / stance_switch_distance_ratio_*; set from the balance). */
	FSquadFireConfig FireConfig;

	/** Autonomous grenade throws (Godot ai_grenade_*; set from the balance). */
	FAIGrenadeConfig AIGrenadeConfig;

	/** Godot ai_grenade_cooldown: seconds until the next autonomous throw. */
	float AIGrenadeCooldown = 0.f;

	/**
	 * Godot _evaluate_ai_grenade_opportunity + execute_ai_grenade_throw: throws at the biggest safe enemy cluster in
	 * range (not while the player aims a grenade or in turn-based combat). True when a grenade left the hand.
	 */
	bool TryAIGrenadeThrow();

	/** Godot _auto_switch_on_empty: M16 with rounds, else the pistol, else a grenade at a cluster, else the knife. */
	void AutoSwitchOnEmpty();

	/** Forces the crit roll of the next hit (smokes): 1 crits, 0 never. Negative = random. */
	UPROPERTY(Transient)
	float ForcedCritRollForTesting = -1.f;

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

	/** Godot cover_chatter_timer: the «Занял укрытие» radio line at most every 5 s. */
	double LastCoverChatterTime = -100.0;

	/** Godot weapon_freeze_notify_timer: «ОРУЖИЕ ЗАМЁРЗЛО» at most every 2.5 s. */
	float WeaponFreezeNotifyTimer = 0.f;
	void NotifyWeaponFrozen();

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
	void HandleHealthChanged(float NewHealth, float MaxHealth, float Delta);

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
	/**
	 * Range (weapon x stance x elevation), dead zone and line of fire to Enemy (Godot _find_shoot_target ray: walls block,
	 * barricades by SquadFireRules::JudgeLine, another enemy in the way becomes the target unless bKeepTarget).
	 */
	bool EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked) const;
	/** Godot _notify_barricade_blocked (every 3.5 s at most). */
	void NotifyBarricadeBlocked();
	float BarricadeBlockNotifyTimer = 0.f;
	TWeakObjectPtr<AActor> CurrentCombatTarget;
	TWeakObjectPtr<AActor> PendingFlankTarget;
	float TargetSwitchTimer = 0.f;

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
