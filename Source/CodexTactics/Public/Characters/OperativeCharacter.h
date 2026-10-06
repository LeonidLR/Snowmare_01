#pragma once

#include "CoreMinimal.h"
#include "Combat/AIGrenadeRules.h"
#include "Combat/SquadFireRules.h"
#include "GameFramework/Character.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/PersonalItemRules.h"
#include "Characters/ProgressionRules.h"
#include "Characters/SquadAutonomyRules.h"
#include "Characters/FirePostureRules.h"
#include "Combat/TargetedShotRules.h"
#include "Interactables/DeployableRules.h"
#include "Tactics/CoverTypes.h"
#include "OperativeCharacter.generated.h"

class UMaterialInterface;

/** Materials of one squad member's outfit, by skeletal mesh material slot name. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeOutfit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outfit")
	TMap<FName, TObjectPtr<UMaterialInterface>> SlotMaterials;
};

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
	/** Blind fire at a last known position silhouette (Sprint 08-F): aimed at AimPoint, hit chance x0.2. */
	bool bBlind = false;
	FVector AimPoint = FVector::ZeroVector;
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

	/** Box selection mark (Godot player.gd set_group_selected): a ring under the feet, gold for the leader. */
	void SetGroupSelected(bool bSelected, bool bInMultiSelection);

	/** Gold ring under the active leader, cyan under every selected operative (Sprint 06-A). */
	void UpdateSelectionRing();
	bool IsSelectionRingShown() const;

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

	/**
	 * Vaults a low obstacle ahead (barricade / "Vault" object, 25..105 cm, a landing spot behind it) — Godot
	 * try_vault_obstacle: not while sprinting at speed, followers only when blocked. Returns true when a vault starts.
	 */
	bool TryVault(const FVector& Direction, bool bForceWhenBlocked);

	bool IsVaulting() const { return bVaulting; }

	/** Ground speed of the current vault (the animation reads it). */
	float GetVaultSpeed() const;

	/** Length of the vault in progress, s (a step off an obstacle top is short: 0.5 s). */
	float GetVaultDuration() const { return VaultDuration; }

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

	/** Exploring a camera zone alone (Godot is_in_camera_zone; set by ACameraZoneVolume). */
	UPROPERTY(VisibleInstanceOnly, Transient, BlueprintReadOnly, Category = "CodexTactics|Operative")
	bool bInCameraZone = false;

	// --- See-through silhouette (Godot player.gd enable_silhouette / _check_silhouette_occlusion, silhouette.gdshader) ---

	/** Show the coloured outline through walls when the camera cannot see this operative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Silhouette")
	bool bEnableSilhouette = true;

	/** Only while hidden (true, Godot default); false shows it all the time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Silhouette")
	bool bSilhouetteOcclusionOnly = true;

	/** /Game/VFX/Materials/M_Silhouette (Scripts/Editor/create_silhouette_material.py); parameter "Color". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Silhouette")
	TObjectPtr<UMaterialInterface> SilhouetteMaterial;

	/** Godot get_silhouette_color: leader cyan, engineer amber, medic-sapper green, recruit olive (alpha 0.55). */
	FLinearColor GetSilhouetteColor() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Silhouette")
	bool IsSilhouetteVisible() const { return bSilhouetteVisible; }

	// --- Progression (Godot player.gd level / current_exp / unspent_stat_points) ---

	/** Level 1..10 (Godot MAX_LEVEL). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Progression", meta = (ClampMin = "1", ClampMax = "10"))
	int32 Level = 1;

	/** EXP towards the next level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Progression", meta = (ClampMin = "0"))
	int32 CurrentExp = 0;

	/** Free stat points (+3 per level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Progression", meta = (ClampMin = "0"))
	int32 UnspentStatPoints = 0;

	/** Stats at BeginPlay (Godot initial_base_*): points taken back cannot go below them. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "CodexTactics|Progression")
	float InitialBaseHealth = 130.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "CodexTactics|Progression")
	float InitialBaseLuck = 25.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "CodexTactics|Progression")
	float InitialBaseAccuracy = 90.f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category = "CodexTactics|Progression")
	float InitialBaseFortitude = 15.f;

	/** EXP the next level needs (Godot get_next_level_exp). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Progression")
	int32 GetNextLevelExp() const;

	/**
	 * Godot add_exp: EXP with overflow into the next levels; each level-up gives +3 points, heals fully, floats
	 * «⭐ УРОВЕНЬ N!» and posts the radio line.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Progression")
	void AddExp(int32 Amount);

	/** Max health, luck, accuracy or fortitude. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Progression")
	float GetStatValue(EProgressStat Stat) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Progression")
	bool CanIncreaseStat(EProgressStat Stat) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Progression")
	bool CanDecreaseStat(EProgressStat Stat) const;

	/** Spends a point: +5 max health (and +5 health) or +1 of the stat. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Progression")
	bool IncreaseStat(EProgressStat Stat);

	/** Takes a point back (health clamps to the new maximum). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Progression")
	bool DecreaseStat(EProgressStat Stat);

	/** Stores the current stats as the floor for DecreaseStat (BeginPlay, after the balance config). */
	void CaptureProgressionBases();

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

	/**
	 * Godot is_expendable (recruit_susanin.gd): the death of such a member (the recruit) does not fail the mission — he
	 * leaves the squad and the HQ reports it; the recruit role is always expendable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Squad")
	bool bExpendable = false;

	bool IsExpendable() const { return bExpendable || SquadRole == EOperativeRole::Recruit; }

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

	/**
	 * Outfit per squad member, indexed by SquadIndex (wraps for recruits) so the operatives look different
	 * (the Godot role tint's job on the new art): material slot name -> material.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TArray<FOperativeOutfit> SquadOutfits;

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

	/** Stress and panic in the real-time fight (Godot PanicComponent). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Panic")
	TObjectPtr<class UPanicComponent> PanicComponent;

	/** Panicking: does not shoot, reload or obey orders (Godot panic_comp.is_panicking). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Panic")
	bool IsPanicking() const;

	/** Max speed / turn rate from the stance, sprint, wounds and carrying (SpeedOverride >= 0 replaces the speed). */
	void ApplyMovementParams(float SpeedOverride = -1.f);

	/** Speed multiplier of the current cold tier (Godot speed_multiplier 1 / 0.7 / 0.45 / 0.25). */
	void SetColdSpeedMultiplier(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetColdSpeedMultiplier() const { return ColdSpeedMultiplier; }

	// --- Combat & Weapon System (Godot player.gd parity) ---

	virtual void Tick(float DeltaTime) override;

	/** Godot _check_silhouette_occlusion (20 Hz): a ray from the camera to the body centre, a hit 35 cm short = hidden. */
	void UpdateSilhouette(float DeltaTime);
	void SetSilhouetteVisible(bool bVisible);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SilhouetteMID;

	bool bSilhouetteVisible = false;
	float SilhouetteCheckTimer = 0.f;

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

	/** Native twin of On Grenade Throw (the anim instance plays the throw clip and may set GrenadeThrowDuration from it). */
	FSimpleMulticastDelegate OnGrenadeThrowNative;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void StartReload();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	bool CanShoot() const;

	/**
	 * An attack on this operative (Godot player.gd take_damage): dodge with luck * 0.4 %, then
	 * max(1, Amount * stance defense * (1 - clamp(fortitude * 1.5 %, 0, 50 %))); bBypassAvoidance (grenades, traps
	 * on the squad) skips both and takes max(1, Amount). Floating «💨 УКЛОНЕНИЕ!», «-N» or «💥 КРИТИЧЕСКИЙ УДАР! -N».
	 * Sprint 12: in cover a hit from the wall's frontal arc is absorbed (CoverRules) and a crit on a head kept down is
	 * undone (Amount / CritMultiplierApplied — the attacker passes the multiplier it applied). Returns the health taken.
	 */
	float TakeHit(float Amount, const FString& Attacker, bool bCrit = false, bool bBypassAvoidance = false, AActor* AttackerActor = nullptr,
		float CritMultiplierApplied = 1.f);

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
	 * decide cover (0.8 crouched) and block prone shooters («🚫 Баррикада блокирует огонь»). bAllowAutoTargets = false
	 * (fire posture holds the automatic fire) keeps only the direct orders: priority target and blind fire.
	 */
	FShootCandidate FindShootTarget(float DeltaTime, bool bAllowAutoTargets = true);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	void ProcessCombatShooting(float DeltaTime);

	/**
	 * Godot _shoot_at_target: misfire, hit roll, then weapon damage * stance * Cover * crit (luck %, x2) * elevation
	 * (+15 %) * distance factor; cryo / fire weapons chill / warm the shooter.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Combat")
	bool ShootAtTarget(AActor* Target, float Cover = 1.f);

	/** Godot _set_squad_tactical_cease_fire: nobody shoots while Space is held for the turn-based switch. */
	bool bTacticalCeaseFire = false;

	// --- Tactical cover (Sprint 12, TANDEM «SPRINT 12 DIRECTIVE»; UE-only, no Godot reference — Gemini Sprint 12 spec).
	// Back to a wall (CoverSlot): high cover = standing / crouched behind a full wall, fire round an exposed corner
	// (lean) or blind; low cover = crouched behind a 60 cm barricade, stand to fire over it. CoverRules / CoverTraceRules
	// / CoverDecisionRules hold the pure rules. ---

	/** Pressed against a wall at CoverSlot (set by EnterCover, cleared by LeaveCover). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bInCover = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	ECoverHeight CurrentCoverHeight = ECoverHeight::None;

	/** The corner he works (lean / blind fire side). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	ECoverFacing CoverFacing = ECoverFacing::Right;

	/** Leaning out of the corner for an aimed shot (head exposed, no cover absorption meanwhile). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bIsCornerLeaning = false;

	/** Firing blind round the corner / over the top (head down: -40 % accuracy, no headshots on him). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bIsBlindFiring = false;

	/** Side-stepping along the wall to another slot of it. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bShimmying = false;

	/** Shimmy direction along the wall: +1 to his right (facing away from the wall), -1 to his left, 0 none. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	float ShimmyDirection = 0.f;

	/** How he fires from this cover (player: key N toggles lean / blind; Commander Mode: CoverDecisionRules). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	ECoverFireMode CoverFireMode = ECoverFireMode::CornerLean;

	/** Commander Mode decided to hold fire behind the cover (no automatic shots; direct orders still fire). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bCoverHoldFire = false;

	/** Damage taken in the last seconds (decays; CoverDecisionRules input). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	float RecentIncomingDamage = 0.f;

	/** The cover he stands at (valid while bInCover). */
	const FCoverSlot& GetCoverSlot() const { return CoverSlot; }

	/** Fired after EnterCover / LeaveCover (AnimBP, sounds, UI). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Cover", meta = (DisplayName = "On Cover Changed"))
	void ReceiveCoverChanged(bool bNowInCover, ECoverHeight Height);

	/**
	 * Order: sprint (or walk) to the slot and take cover there (EnterCover on arrival). Refused like a move order while
	 * raging / panicking. In the tactical pause the controller plans the walk instead and calls SetPendingCover.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	EOperativeOrderResult OrderTakeCover(const FCoverSlot& Slot, bool bSprint);

	/** Remembers a slot to enter once he arrives there (planned walks, grid walks). */
	void SetPendingCover(const FCoverSlot& Slot);
	void ClearPendingCover() { bHasPendingCover = false; }
	bool HasPendingCover() const { return bHasPendingCover; }

	/** Snaps him to the slot's wall: facing along the normal, the default stance of the height, Cover_Enter. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void EnterCover(const FCoverSlot& Slot);

	/** Leaves the wall (move order, death, vault, the player clicking away). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void LeaveCover(const FString& Reason);

	/** Side-step along the same wall to Target (CoverTraceRules::IsSameWall); refused when it is another wall. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	EOperativeOrderResult OrderShimmyTo(const FCoverSlot& Target);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void SetCoverFireMode(ECoverFireMode Mode);

	/** Key N: CornerLean <-> BlindFire. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	ECoverFireMode ToggleCoverFireMode();

	void SetCoverFacing(ECoverFacing Facing) { CoverFacing = Facing; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsInHighCover() const { return bInCover && CurrentCoverHeight == ECoverHeight::HighCover; }

	/** A shot is possible from this cover: low cover (over the top) or an exposed corner on the facing side. */
	bool CanFireFromCover() const;

	/** Where shots leave in cover: the muzzle moved round the facing's corner (CoverRules::CornerMuzzle). */
	FVector GetCoverFireOrigin() const;

	/**
	 * Sprint 12-E sight: an observer at ObserverLocation cannot see him — high cover, head down, observer behind the
	 * wall (CoverRules::HiddenFromObserver). Firing demasks him (UTacticalSightSubsystem::IsDemasked, checked by the caller).
	 */
	bool IsHiddenInCoverFrom(const FVector& ObserverLocation) const;

	/** Shots taken from cover by mode (smokes / stats). */
	int32 GetCoverLeanShots() const { return CoverLeanShots; }
	int32 GetCoverBlindShots() const { return CoverBlindShots; }

	// --- Fire posture (rules of engagement of the automatic fire, user request 2026-10-06; FirePostureRules) ---

	/** This operative has its own posture (the posture keys with a box-selected group); false: it follows the squad's. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Combat|Posture")
	bool bHasPostureOverride = false;

	/** The own posture while bHasPostureOverride. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Combat|Posture")
	ESquadFirePosture PostureOverride = FirePostureRules::DefaultPosture;

	/** Attacked (hit or dodged) in the current fight: a Defensive operative fights back. Cleared when the fight ends. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Combat|Posture")
	bool bProvokedThisFight = false;

	/** The posture in force (override, else the squad's; Aggressive without a squad). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat|Posture")
	ESquadFirePosture GetFirePosture() const;

	/** The posture lets this operative open fire on its own now (provocation included). */
	bool MayAutoFireNow() const;

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

	/** Godot main.gd plain click on an enemy in a wave: current_leader.manual_priority_target = enemy (no checks, no turn). */
	void AssignPriorityTarget(AActor* Enemy) { ManualPriorityTarget = Enemy; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	AActor* GetManualPriorityTarget() const { return ManualPriorityTarget.Get(); }

	// --- Commander Mode (Sprint 07; USquadAutonomySubsystem drives these) ---

	/** The target the autonomy picked (ROE policy): fired at after the manual priority target, before the closest enemy. */
	void SetAutonomyTarget(AActor* Enemy) { AutonomyTarget = Enemy; }
	AActor* GetAutonomyTarget() const { return AutonomyTarget.Get(); }

	/**
	 * Blind fire (Sprint 08-F): the operative fires at the silhouette (last known position) of an enemy he cannot see,
	 * -80 % accuracy, until the silhouette goes (the enemy is seen again: it becomes the priority target) or it dies.
	 */
	void SetBlindFireTarget(class AEnemyGhostActor* Ghost);
	class AEnemyGhostActor* GetBlindFireTarget() const;

	/** The enemy the operative shot at last (real-time fire). */
	AActor* GetCurrentCombatTarget() const { return CurrentCombatTarget.Get(); }

	/** A live enemy in range and in the line of fire now (the fire's own check, Godot _find_shoot_target ray). */
	bool CanHitEnemy(AActor* Enemy) const;

	/** A move by the autonomy: walks like a player order but keeps the tactical anchor. */
	EOperativeOrderResult AutonomousMoveTo(const FVector& Destination);

	/**
	 * Field aid (Sprint 07-D): spends one of this operative's medkits on Patient (the medkit's heal, Godot
	 * heal_with_item amounts). False without a medkit, when Patient is dead / at full health or out of reach (2.5 m).
	 */
	bool HealAlly(AOperativeCharacter& Patient);

	/** Field aid reach, cm. */
	static constexpr float AidReachCm = 250.f;

	/** The point the last player move order pinned this operative to (TANDEM 7-B); the autonomy stays on its leash. */
	FTacticalAnchor TacticalAnchor;

	/** Tactical pause: remembers a targeted shot (one per kind) executed when the pause is released. */
	void PlanTargetedShot(AActor* Target);

	/** Executes the planned shots in Godot order: barrel, mine, crate, trapped object, then the priority enemy. */
	void ExecutePlannedTargetedShots();

	void ClearPlannedTargetedShots() { PlannedShots.Reset(); }

	int32 GetPlannedTargetedShotCount() const { return PlannedShots.Num(); }

	/** Line-of-fire origin: the feet plus the Godot muzzle height of the stance (1.4 / 0.85 / 0.25 m). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	FVector GetMuzzleLocation() const;

	/** Where tracers leave: the weapon mesh's "Muzzle" socket, else MuzzleOffset on it, else GetMuzzleLocation. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	FVector GetWeaponMuzzleLocation() const;

	/** Muzzle in the weapon mesh's own space when it has no "Muzzle" socket (m16_01: the barrel runs along +Z, 98 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Combat")
	FVector MuzzleOffset = FVector(0.f, 0.f, 98.f);

	/**
	 * Turn the body so the barrel (not the chest) points at the target while aiming: the aim pose holds the rifle at an
	 * angle to the actor's forward, measured from the weapon mesh (barrel = MuzzleOffset direction).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CodexTactics|Combat")
	bool bAlignBarrelWithTarget = true;

	/** True while the operative keeps facing its combat target (moving sideways / backwards instead of turning). */
	bool IsFacingCombatTarget() const { return bFacingCombatTarget; }

	/**
	 * Yaw a standing-still operative turns to every frame (Godot _align_rotation_with_leader: parked followers face where
	 * the leader faces); cleared by any move.
	 */
	void SetIdleFacingYaw(float Yaw) { IdleFacingYaw = Yaw; bHasIdleFacing = true; }
	void ClearIdleFacing() { bHasIdleFacing = false; }

	/** Current barrel-to-body yaw correction, degrees. */
	float GetBarrelYawOffset() const { return BarrelYawOffset; }

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
	/** Shows the selection ring when selected and not the leader, or the leader of a multi-selection. */

	/** Vault in progress (Godot is_vaulting): kinematic arc, collision off, the walk resumes after it. */
	void UpdateVault(float DeltaTime);
	/** Tries a vault while walking a path (leader always, followers when blocked). */
	void UpdateVaultTrigger(float DeltaTime);
	bool bVaulting = false;
	float VaultTimer = 0.f;
	float VaultDuration = 1.f;
	float VaultHeight = 100.f;
	float VaultCooldown = 0.f;
	float BlockedTimer = 0.f;
	FVector VaultStart = FVector::ZeroVector;
	FVector VaultLanding = FVector::ZeroVector;
	FVector VaultResumeTarget = FVector::ZeroVector;
	bool bVaultResume = false;
	/** Seconds standing on a barricade / barrel top (VaultNavigation::IsStandingOnObstacle). */
	float ObstacleTopTime = 0.f;
	/** Jumps down off an obstacle top after 0.3 s there; true while starting that jump. */
	bool UpdateObstacleStepOff(float DeltaTime);

	bool bGroupSelected = false;
	bool bInMultiSelection = false;

	UPROPERTY(Transient)
	TObjectPtr<class ARadiusRingActor> SelectionRing;

	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth, float Delta);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);
	/** Pushes max speed and turn rate for the current state into CharacterMovement. */
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

	/** Godot combat_facing_direction: real-time fight, not sprinting, a live target -> face it, no turn to the movement. */
	void UpdateCombatFacing(float DeltaTime);
	bool bFacingCombatTarget = false;
	float BarrelYawOffset = 0.f;
	/** Planar velocity smoothed for the movement facing (FacingRules). */
	FVector SmoothedVelocity = FVector::ZeroVector;
	float IdleFacingYaw = 0.f;
	bool bHasIdleFacing = false;
	TWeakObjectPtr<AActor> PendingFlankTarget;
	float TargetSwitchTimer = 0.f;

	mutable TWeakObjectPtr<AActor> ManualPriorityTarget;
	mutable TWeakObjectPtr<AActor> AutonomyTarget;
	TWeakObjectPtr<class AEnemyGhostActor> BlindFireGhost;
	TWeakObjectPtr<AActor> BlindFireSource;
	/** Set around ShootAtTarget for a blind shot. */
	bool bBlindShot = false;
	FVector BlindAimPoint = FVector::ZeroVector;
	/** Set around ShootAtTarget for a cover blind shot (head down, -40 %). */
	bool bCoverBlindShot = false;
	/** Cover state (Sprint 12). */
	FCoverSlot CoverSlot;
	FCoverSlot PendingCoverSlot;
	bool bHasPendingCover = false;
	/** Set while OrderTakeCover / OrderShimmyTo issue their move: the move does not leave the cover. */
	bool bCoverMoveOrder = false;
	float LeanTimer = 0.f;
	float BlindFireTimer = 0.f;
	int32 CoverLeanShots = 0;
	int32 CoverBlindShots = 0;
	/** Facing lock at the wall, lean / blind timers, shimmy state, the pending slot on arrival. */
	void UpdateCover(float DeltaTime);
	/** Called before a shot from cover: lean out or keep the head down by CoverFireMode. */
	void BeginCoverShot();
	/** Range and line of fire to a silhouette's aim point (barricades by SquadFireRules::JudgeLine). */
	bool EvaluateBlindLine(const class AEnemyGhostActor& Ghost, FShootCandidate& Out) const;
	/** Set while AutonomousMoveTo runs: the move does not re-pin the anchor. */
	bool bAutonomousOrder = false;
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
	/** The last RequestMove destination (the walk resumes there after jumping off an obstacle top). */
	FVector LastMoveDestination = FVector::ZeroVector;

	/** A move waiting for the stand-up clip (OrderMoveTo from prone / during a stance clip). */
	FTimerHandle PendingMoveTimer;
	bool bPendingMoveReplay = false;

	/** Seconds until a stance clip that starts now / is playing lets the operative walk (0 without a clip). */
	float GetStanceChangeDelay(EOperativeStance From, EOperativeStance To) const;
};
