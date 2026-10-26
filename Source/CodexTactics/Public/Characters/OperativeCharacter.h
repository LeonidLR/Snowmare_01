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
#include "Interactables/TrapSpotRules.h"
#include "Tactics/CoverDecisionRules.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTypes.h"
#include "Tactics/CoverWrapRules.h"
#include "Combat/KnockdownRules.h"
#include "Combat/KnockdownTypes.h"
#include "Combat/SniperRules.h"
#include "Combat/PeekSightRules.h"
#include "OperativeCharacter.generated.h"

class UMaterialInterface;
enum class EOperativeHitKind : uint8; // Characters/OperativeHitRules.h

/** Materials of one squad member's outfit, by skeletal mesh material slot name. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FOperativeOutfit
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outfit")
	TMap<FName, TObjectPtr<UMaterialInterface>> SlotMaterials;
};

class UStaticMeshComponent;
class UStaticMesh;
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
	/** Raging: player orders are ignored (Godot set_target "ENRAGED! IGNORING ORDERS!"). */
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
 * Source layout (cleanup day 2026-10-14, docs/port/REFACTOR_OPERATIVE_2026-10.md): the members are defined in one
 * translation unit per cluster under Private/Characters/ - OperativeCharacter.cpp (core, Tick), OperativeCharacterMovement,
 * Stance (+ prone dive), Vault, Overlays (selection ring, silhouette, vision cone), Inventory, Progression, Health (hits,
 * knockdown, death), Weapons, Combat (target selection, fire), Aim, TargetedShots, Cover, CoverEntry, CornerAim, Sniper.
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

	/** Goal of the last move request (a use order sees another order replace its walk-up). */
	const FVector& GetLastMoveDestination() const { return LastMoveDestination; }

	/** Counts OrderMoveTo calls (not the replay of a deferred one): a change = this operative was given an order (weather idle). */
	uint32 GetMoveOrderSerial() const { return MoveOrderSerial; }
	/** Actors holding him in place (SetTrafficHold); stale entries are dropped by IsTrafficHeld. */
	mutable TArray<TWeakObjectPtr<const AActor>> TrafficHoldSources;

	/** World time of the last attack on him (hit or dodged; -1e9 = never). */
	double GetLastAttackedTime() const { return LastAttackedTime; }

	/**
	 * Weather idle (UWarmIdleSubsystem, user request 2026-10-12): he walks to / holds a spot at this heat source's ring (a seat at a
	 * campfire / stove, a hand-warming place at a barrel). A parked follower keeps it while the leader stays in that warm zone.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Weather")
	TWeakObjectPtr<AActor> WarmSpotSource;

	/** The heat source's warm reach of WarmSpotSource (Radius + Tolerance), cm. */
	float WarmSpotReach = 0.f;

	/** Formation move issued by the squad: path to Destination at a fixed speed, keeps stance and sprint state. */
	EOperativeOrderResult FollowTo(const FVector& Destination, float Speed);

	/**
	 * Rev 5g (game designer 2026-10-14): a move order of his OWN is running (OrderMoveTo: player, interaction walk-up, relocation,
	 * autonomy, bot) - not a formation move (FollowTo). Ends on arrival / failure (HandleMoveFinished), StopOperative, a formation
	 * move or ReleaseOwnMoveOrder. USquadSubsystem leaves such a follower alone (SquadOrderRules::IsOnOwnOrder), so selecting
	 * another operative never cancels it.
	 */
	bool HasOwnMoveOrder() const { return bOwnMoveOrder && bHasMoveOrder; }

	/** Regroup / Solo off: the walk goes on, but it no longer counts as his own order (the next formation repath takes him back). */
	void ReleaseOwnMoveOrder() { bOwnMoveOrder = false; }

	/**
	 * Formation rules v2.1 bypass (USquadSubsystem, contract docs/design/FORMATION_CONTRACT.md 10.2): his running own move order is steered
	 * through Via first - the order stays his (GetLastMoveDestination, the serial and HasOwnMoveOrder are kept); reaching Via (or failing
	 * there) the walk goes on to the order's own destination by itself. False (nothing changed) without an own order running, under a
	 * tutorial confinement, or when no path to Via is found.
	 */
	bool SteerOwnMoveVia(const FVector& Via);
	/** The steered own order goes straight on to its destination (the next waypoint passed / the last one done); false without one. */
	bool ResumeOwnMove();
	/** His own order is being steered through a bypass waypoint (SteerOwnMoveVia). */
	bool IsOwnMoveSteered() const { return bSteeringOwnMove && HasOwnMoveOrder(); }

	/**
	 * The squad automation set his current stance (Regroup's copy of the leader's stance, Solo's guard crouch, a tutorial confinement's
	 * copy), not the player's stance key: the formation may stand him up to sprint with the leader (SquadOrderRules::
	 * ShouldStandToSprintWithLeader, bug 2026-10-20). Any later stance change (the player's) makes it his own again.
	 */
	void MarkStanceFromSquad() { SquadStanceSerial = StanceSerial; }
	bool IsStanceFromSquad() const { return SquadStanceSerial == StanceSerial; }
	/** The player's stance key / squad stance order (even one that changes nothing): his stance is his own. */
	void ClearStanceFromSquad() { SquadStanceSerial = MAX_uint32; }

	/**
	 * Seconds he must stand still while a stance change plays (OperativeMovementRules::ComputeMoveHold): the settle time armed by
	 * SetStance plus the stance clip already playing. Moves issued meanwhile wait (OrderMoveTo queues, FollowTo refuses).
	 */
	float GetMoveHoldSeconds() const;

	/** Stops any movement immediately. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void StopOperative();

	/**
	 * Traffic hold (Demo 01 level obstacles, 2026-10-13): a level actor (ABridgeCrossingVolume waiting for a free span,
	 * ACrawlGapActor refusing a standing body) holds this operative where he stands - the formation does not pull him
	 * (USquadSubsystem::UpdateFollower returns early) and his own moves are stopped by the actor. Several sources may
	 * hold him at once; he is free when none is left (or the source is gone).
	 */
	void SetTrafficHold(const AActor* Source, bool bHeld);
	bool IsTrafficHeld() const;

	/** Changes stance; sprinting ends unless standing. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void SetStance(EOperativeStance NewStance);

	/**
	 * Rev 5e item 6 (user request 2026-10-14): a player's prone order, also while moving (OperativeMovementRules::DecideProneEntry
	 * with MovementConfig.ProneDiveMinWalkSpeed / ProneDiveMinRunSpeed): standing and walking / sprinting fast enough he DIVES prone
	 * on the move - the walk / run-to-prone clip (UOperativeAnimInstance::GetProneDiveClip) plays with ROOT MOTION (rev 5f: the
	 * clip's root travel moves the capsule through the CharacterMovementComponent - collision, floor, no walking off a ledge; the
	 * body never leaves the capsule, so nothing snaps back), stretched by his momentum (MovementConfig.ProneDiveMaxMomentumScale)
	 * and shortened by the navmesh ahead (OperativeMovementRules::ComputeProneDiveTranslationScale). After it: with a move order
	 * whose goal is still ahead he crawls on to it, else he stays where the dive ended (ShouldCrawlOnAfterDive). A dive clip
	 * without Enable Root Motion falls back to "stop, then lie down". Slower / crouched: he stops, then lies down in place. Still:
	 * in place. In cover, carrying, vaulting, aboard or knocked down a moving body is refused as before. Turn-based stance
	 * changes stay on the grid (AP rules).
	 */
	EProneEntry DropProne();

	/** The dive going on now (None = none; the anim instance plays its clip). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	EProneDive GetProneDive() const { return ProneDive; }

	bool IsProneDiving() const { return ProneDive != EProneDive::None; }

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

	/** Localised stance name ("STANDING", "CROUCHED", "PRONE"). */
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
	 * "⭐ LEVEL N!" and posts the radio line.
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
	 * Toggled by USquadSubsystem::ToggleGuard (T / action bar "GUARD").
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

	/**
	 * Adds (or removes, negative Delta) carried items, clamped to 0..max. Sprint 15 campfire: -1 spends the materials of one
	 * fire (matches + firewood, else flamethrower fuel), +1 gives back one match + one firewood.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Inventory")
	void AddDeployable(EDeployableType Type, int32 Delta);

	/**
	 * Hands one item of Type to To (a portable stove takes its gas along; a campfire moves one match + one firewood). False when
	 * this operative has none or To has no room.
	 */
	bool MoveDeployableTo(AOperativeCharacter& To, EDeployableType Type);

	/** Next type in the cycle order turret -> barricade -> mine -> stove -> campfire (skips empty types when possible). */
	EDeployableType CycleDeployableType();

	// --- Sprint 15 «Mobile & Tactical Heat» (TANDEM #14; starting amounts: tactical_heat.json "starting_loadout") ---

	/** Chemical heat packs (one slot, stack FTacticalHeatConfig::HeatPackMaxStack 3): U / the drawer, or dragged onto a mate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 HeatPacksCount = 0;

	/** Firewood: one per campfire (with a match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 FirewoodCount = 0;

	/** Signal flares (grenade slot: Shift + G / the weapon selector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 FlaresCount = 0;

	/** Packed portable gas stoves (max FTacticalHeatConfig::StoveMaxCarried). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	int32 PortableStovesCount = 0;

	/** Gas left in the packed stove's canister, s (kept with the item; goes into the stove when it is set up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Inventory", meta = (ClampMin = "0"))
	float StoveGasSeconds = 0.f;

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

	/** Sprint 14 knockdown & recovery (fall / downed / get-up; UE-only, TANDEM request #12). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Knockdown")
	TObjectPtr<class UKnockdownComponent> KnockdownComponent;

	/** Knocked down (any phase): no orders (move orders are buffered), no fire, no aim, no reload. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Knockdown")
	bool IsKnockedDown() const;

	// --- Ladders (docs/design/LADDERS.md; cluster OperativeCharacterLadder.cpp) ---

	/** The ladder climb: a path through a ladder's nav link hands him over to it at the link (no climb input). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Ladder")
	TObjectPtr<class ULadderClimbComponent> LadderClimb;

	/** On a ladder (moving onto / off it, or falling off it): no fire, no aim, no facing, no cover, no stance change. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Ladder")
	bool IsClimbingLadder() const;

	/** The climb ended at the other end: the path goes on to Destination (like the vault's resume). */
	void ResumeMoveAfterLadder(const FVector& Destination, bool bOwnOrder = false);

	/** Where a save puts him: on a ladder, the link point of the end he is heading to (LADDERS.md 7.8). */
	FVector GetSaveLocation() const;

	/**
	 * Where he counts for the floor-dependent views (roof cutaway, minimap floor; LADDERS.md 7.3): on a ladder the spot the climb started
	 * from (the floor he left) until he is off it - no flicker mid-ladder; his location otherwise.
	 */
	FVector GetFloorReferenceLocation() const;

	/** Max speed / turn rate from the stance, sprint, wounds and carrying (SpeedOverride >= 0 replaces the speed). */
	void ApplyMovementParams(float SpeedOverride = -1.f);

	/** Speed multiplier of the current cold tier (Godot speed_multiplier 1 / 0.7 / 0.45 / 0.25). */
	void SetColdSpeedMultiplier(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetColdSpeedMultiplier() const { return ColdSpeedMultiplier; }

	// --- Vision cone (user request 2026-10-11; Godot player.gd _setup_vision_cone / _update_vision_cone) ---

	/** Ground cone of the operative's fire sector (look: its ConeMaterial / colours / segments). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Vision Cone")
	TObjectPtr<class UVisionConeComponent> VisionCone;

	/**
	 * Draw this operative's vision cone (Godot show_vision_cones, off in Godot's shipped config). Set it on BP_Operative's
	 * Class Defaults or on one placed / spawned operative; console Codex.Debug.VisionCones 0 / 1 overrides every actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone")
	bool bShowVisionCone = false;

	/** Cone range = the current weapon's real-time range x the stance multiplier (what it can hit); off: VisionConeRangeCm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone")
	bool bVisionConeRangeFromWeapon = true;

	/** Cone range without a weapon (or bVisionConeRangeFromWeapon off), cm (Godot vision_cone_range 14 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone", meta = (ClampMin = "0"))
	float VisionConeRangeCm = 1400.f;

	/**
	 * Half angle of the cone, degrees (Godot vision_cone_angle 70 = +-35). Since 2026-10-20 the squad's sight has a real field of view
	 * (UTacticalSightSubsystem::GetFovDeg: 140 standing / crouched, 110 prone) and the drawn cone uses it; this value is only the
	 * cone of the old 360 degree sight (Codex.Sight.Fov 0, or no tactical sight running).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone", meta = (ClampMin = "1", ClampMax = "180"))
	float VisionConeHalfAngleDeg = 35.f;

	/** Range the cone shows now, cm. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Vision Cone")
	float GetVisionConeRange() const;

	/** Half angle the cone shows now: half the stance's field of view while the FOV rule runs, else VisionConeHalfAngleDeg. */
	float GetVisionConeHalfAngle() const;

	/**
	 * Where his own eyes look (planar unit vector), what UTacticalSightSubsystem::CanOperativeSee and his drawn cone centre on (2026-10-20):
	 * out of cover his body's facing; in cover he looks ALONG the wall to the side he faces (CoverFacing; the clips' look-around pose) and, in
	 * the fire-ready / corner aim, over the cover the way the corner stance aims (CoverFacingRules::CornerAimDirection). The peek cone
	 * (GetPeekSight) stays separate.
	 */
	FVector GetSightFacing() const;

	/** Godot _is_any_enemy_spotted: a combat / priority target, or a living enemy inside the cone with a clear line. */
	bool IsEnemySpottedInCone() const;

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

	/**
	 * Adds one more weapon to the arsenal after InitArsenal (user request 2026-10-09: the Medic-Sapper's sniper rifle):
	 * full magazine, Reserve rounds (< 0 = the weapon's DefaultReserveAmmo); the weapon in hands stays.
	 */
	void AddArsenalWeapon(UWeaponDataAsset* Weapon, int32 Reserve = -1);

	/**
	 * Replaces the Blueprint's skeletal mesh before BeginPlay (per-role body, FSquadMemberSpawn::BodyMesh: the Female Soldier
	 * for the Medic-Sapper). The AnimBP stays; the Blueprint's index-based material overrides are dropped (they belong to the
	 * old mesh's slots).
	 */
	void ApplyBodyMeshOverride(class USkeletalMesh* Mesh);

	// --- Sniper rifle (user request 2026-10-09, SniperRules): only kneeling / prone and standing still. ---

	/** The weapon in hands is a sniper rifle (EWeaponHandling::SniperRifle). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Sniper")
	bool IsSniperWeaponEquipped() const;

	/**
	 * Gets her ready for a sniper shot: a direct order while moving stops her, standing she kneels by herself, the shot
	 * waits for the kneel clip. Returns the step (Ready = fire now). Not a sniper: always Ready.
	 */
	ESniperFireStep PrepareSniperShot(bool bDirectOrder);

	/** After a direct sniper order she holds her spot this long (the formation does not pull her away), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Sniper", meta = (ClampMin = "0"))
	float SniperOrderHoldSeconds = 3.f;

	/** A Ctrl + click object shot waiting for her to kneel is given up after this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Sniper", meta = (ClampMin = "0"))
	float SniperPendingShotSeconds = 6.f;

	/** After kneeling for a shot the shot waits at least this long (in cover the kneel is a cross-blend, not a stance clip), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Sniper", meta = (ClampMin = "0"))
	float SniperKneelSettleSeconds = 0.45f;

	/**
	 * The actor just turned at once from OldYaw (a turn-based shot turns him to the target): the body follows smoothly over
	 * CoverEntryYawBlendSeconds instead of snapping (user report 2026-10-09). Not in cover.
	 */
	void StartBodyYawBlend(float OldYaw);

	/** How far the body (mesh) still lags behind the actor's facing after an instant turn, deg (0 = on it). */
	float GetBodyYawLagDeg() const;

	/** Turn-based grid shot shown now: the fire clip of the pose (sniper shot + bolt, prone / standing rifle fire). */
	void PlayShotClip(AActor* Target, bool bHit);

	/** How far the actor turned when it entered the current cover (its back to the wall), deg (the anim blends a big turn in fast). */
	float GetCoverEntryTurnDeg() const { return CoverEntryTurnDeg; }

	/** Holding her spot for a direct sniper order (FollowTo refused). */
	bool IsHoldingForSniperShot() const;

	/** Turn-based grid shot of a sniper: the anim instance plays the shot (after the kneel clip) and the bolt. */
	void PlaySniperGridShot();

	/** Smokes / stats: kneels for a shot, stops for a shot, sniper shots fired, shots refused (wrong stance / moving). */
	int32 GetSniperKneels() const { return SniperKneels; }
	int32 GetSniperStops() const { return SniperStops; }
	int32 GetSniperShots() const { return SniperShots; }
	int32 GetSniperRefusedShots() const { return SniperRefusedShots; }
	/** An object shot waiting for the kneel (Ctrl + click). */
	AActor* GetSniperPendingObject() const { return SniperPendingObject.Get(); }

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

	/**
	 * Seconds from the throw start to the release of the grenade (the measured release frame of the throw clip, set by
	 * the anim instance with the clip); <= 0 = GrenadeThrowDuration * 70 % (GrenadeRules::ReleaseDelay). Reset by every throw.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "CodexTactics|Combat")
	float GrenadeReleaseSeconds = -1.f;

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
	 * on the squad) skips both and takes max(1, Amount). Floating "💨 DODGE!", "-N" or "💥 CRITICAL HIT! -N".
	 * Sprint 12: in cover a hit from the wall's frontal arc is absorbed (CoverRules) and a crit on a head kept down is
	 * undone (Amount / CritMultiplierApplied — the attacker passes the multiplier it applied). Returns the health taken.
	 */
	float TakeHit(float Amount, const FString& Attacker, bool bCrit = false, bool bBypassAvoidance = false, AActor* AttackerActor = nullptr,
		float CritMultiplierApplied = 1.f);

	/**
	 * A turn-based barrel / mine blast (Godot _detonate_* -> take_damage): Amount already carries the grid stance factor,
	 * so only the luck dodge (as Godot), the fortitude cut and the downed x1.33 apply (OperativeHitRules GridBlast; no
	 * cover, no stance again, no heavy-hit knockdown). Returns the health taken (0 = dodged).
	 */
	float TakeGridBlast(float Amount, const FString& Source);

	/** Forces the next TakeHit dodge roll (smokes): 1 dodges, 0 never. Negative = random. */
	UPROPERTY(Transient)
	float ForcedDodgeRollForTesting = -1.f;

	/**
	 * Where the last hit came from (the attacker's location, else a point in front of him) — the hit reaction and the
	 * death fall pick their direction from it. False before the first hit.
	 */
	bool GetLastHitSource(FVector& OutLocation) const;

	/**
	 * Killed in action (user decision 2026-10-08: only the commander's death fails the mission; the others stay down as
	 * corpses for the rest of the mission, out of the squad, the HUD, the turn order and the enemies' targets). The save
	 * system can read it from the health component too (dead = health 0).
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Squad")
	bool IsKilledInAction() const { return bKilledInAction; }

	/**
	 * Save-game load of a fallen member: lies as a corpse at once (no death cinematic, no remains, no HQ line), out of the
	 * squad and the turn order. The caller restores his health to 0 itself (UHealthComponent). For the save system.
	 */
	void RestoreKilledInAction();

	/**
	 * The enemy this operative would shoot now, without the target-switch memory (Godot _find_shoot_target: the manual
	 * priority target while it can be hit, else the closest enemy in range with a line of fire).
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	AActor* FindBestCombatTarget() const;

	/**
	 * Godot _find_shoot_target(delta): priority target, else the closest visible enemy; a current target is kept until a
	 * much closer one (stance ratio, or within 3.5 m) stays closer for the stance's reaction delay. Barricade rules
	 * decide cover (0.8 crouched) and block prone shooters ("🚫 Barricade blocks the shot"). bAllowAutoTargets = false
	 * (fire posture holds the automatic fire) keeps only the direct orders: priority target and blind fire; with it, the
	 * automatic targets (Commander Mode target, visible enemies) are only those MayAutoFireAtEnemy accepts (Defensive: the
	 * enemies that have detected the squad until it is provoked).
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

	/**
	 * Return to cover after a knockdown (user decision 2026-10-18): the decision taken at his last get-up
	 * (KnockdownRules::DecideCoverReturn; NotInCover before any) - smokes / logs.
	 */
	EKnockdownCoverReturn GetLastCoverReturn() const { return LastCoverReturn; }

	/** Fired after EnterCover / LeaveCover (AnimBP, sounds, UI). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Cover", meta = (DisplayName = "On Cover Changed"))
	void ReceiveCoverChanged(bool bNowInCover, ECoverHeight Height);

	/**
	 * Order: sprint (or walk) to the slot and take cover there (EnterCover on arrival). Refused like a move order while
	 * raging / panicking. In the tactical pause the controller plans the walk instead and calls SetPendingCover.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	EOperativeOrderResult OrderTakeCover(const FCoverSlot& Slot, bool bSprint);

	/**
	 * Group cover order (game designer 2026-10-20, GroupCoverRules): he holds exactly the slot that order gave him - no automatic
	 * walk to the threat-side corner (TrySnapToCoverCorner), so neighbours shoulder to shoulder never walk into each other. Set
	 * by the controller after OrderTakeCover / SetPendingCover; cleared by the next OrderTakeCover / SetPendingCover (any other
	 * cover order). Kept over an open shot's return and a knockdown's return to the same slot.
	 */
	void SetHoldGroupCoverSlot(bool bHold) { bHoldGroupCoverSlot = bHold; }
	/** In cover (or on his way into it) on a group cover order's slot. */
	bool IsHoldingGroupCoverSlot() const { return bHoldGroupCoverSlot && (bInCover || bHasPendingCover); }

	/** Remembers a slot to enter once he arrives there (planned walks, grid walks). */
	void SetPendingCover(const FCoverSlot& Slot);
	void ClearPendingCover() { bHasPendingCover = false; }
	bool HasPendingCover() const { return bHasPendingCover; }

	// --- Wall-edge cover switch (game designer 2026-10-20, Tactics/CoverSwitchRules; cluster file OperativeCharacterCoverEntry.cpp):
	// in cover at a wall edge, a double-click on another cover within range sends him there at a run; out of cover on the way (enemy
	// sight / fire apply as usual) and in through the M4 pack's run-in transition (std_jog_fwd[_L|_R]_to_cvr_*). ---

	/**
	 * The switch order: the ordinary running cover order to Target (on a high wall the corner slot when he would walk to it right after
	 * entering, as the group cover order does: ResolveCoverSwitchSlot) marked as a switch, so the arrival plays the run-in clip.
	 * Results as OrderTakeCover.
	 */
	EOperativeOrderResult OrderCoverSwitch(const FCoverSlot& Target);

	/**
	 * The switch to an already resolved corner slot (ResolveCoverSwitchSlot's result, e.g. the one its preview ghost shows) arriving facing
	 * ArrivalFacing - the committed target is exactly the previewed one (bug 2026-10-20: the confirm re-derived it from the clicked face).
	 */
	EOperativeOrderResult OrderCoverSwitchResolved(const FCoverSlot& Resolved, ECoverFacing ArrivalFacing);

	/** The next cover entry of another wall faces Facing (the previewed corner pose of a switch target taken by a slow confirm). */
	void SetCoverArrivalFacing(ECoverFacing Facing)
	{
		bCoverSwitchFacingPending = bHasPendingCover;
		CoverSwitchArrivalFacing = Facing;
	}

	/** The slot he is on his way to (valid while HasPendingCover; smokes). */
	const FCoverSlot& GetPendingCoverSlot() const { return PendingCoverSlot; }

	/**
	 * Marks the pending cover (SetPendingCover: a planned walk in the pause) as a switch: the arrival plays the run-in clip and he faces
	 * the corner he was sent to (the mirrored pose, CoverSwitchRules::ArrivalFacing).
	 */
	void SetPendingCoverSwitch(bool bSwitch);
	/** On his way into a cover he will enter through the switch run-in clip. */
	bool HasPendingCoverSwitch() const { return bHasPendingCover && bCoverSwitchPending; }

	/** Would a double-click on Target (another cover) be a switch now? (CoverSwitchRules::Evaluate with his slot and tunables.) */
	ECoverSwitchVerdict EvaluateCoverSwitch(const FCoverSlot& Target) const;

	/**
	 * Corner-to-corner switch (game designer 2026-10-20, second pass): the slot at the NEAREST corner of Target's object on his protected
	 * side (CoverSwitchRules::ChooseSwitchCorner on its footprint, the face whose normal agrees with his own wall's; traced there), and
	 * OutFacing = the side of that corner (he arrives facing back towards the open side - the mirrored pose). An object without a footprint:
	 * the corner of the clicked wall he would walk to right after entering (high cover), else Target, facing PredictCoverFacing.
	 */
	FCoverSlot ResolveCoverSwitchSlot(const FCoverSlot& Target, ECoverFacing* OutFacing = nullptr) const;

	/** The side he faces after a switch into Resolved (a ResolveCoverSwitchSlot result): its nearest exposed edge within the corner reach. */
	ECoverFacing GetCoverSwitchArrivalFacing(const FCoverSlot& Resolved) const;

	/** The tunables below as the rules' config. */
	FCoverSwitchConfig GetCoverSwitchConfig() const;

	/** The current cover entry came from a switch dash: the AnimInstance plays the run-in clip (approach / start below). */
	bool IsCoverSwitchEntry() const { return bInCover && bCoverSwitchEntry; }
	ECoverSwitchApproach GetCoverSwitchApproach() const { return CoverSwitchApproach; }
	/** The run-in clip is the pack's sprint one (std_sprint_fwd*_to_cvr_*), else the jog one. */
	bool IsCoverSwitchSprintClip() const { return bCoverSwitchSprintClip; }
	/** Where the run-in clip starts, s (its body nearest to where he ran in from: CoverSwitchRules::ChooseClipStart). */
	float GetCoverSwitchStartSeconds() const { return CoverSwitchStartSeconds; }
	/** Switch arrivals through the run-in clip so far (smokes). */
	int32 GetCoverSwitchCount() const { return CoverSwitchCount; }
	/** The last switch run-in's mismatch between the runner and the clip body (eased out by the glide), cm (smokes / logs). */
	float GetLastCoverSwitchMismatchCm() const { return LastCoverSwitchMismatchCm; }

	/** Off: a double-click on another cover is always the ordinary running cover order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch")
	bool bCoverSwitchEnabled = true;

	/** The other cover's slot lies at most this far from his slot (2D), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchMaxRangeCm = 800.f;

	/** ... and at least this far (closer: the ordinary cover order), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchMinRangeCm = 120.f;

	/** "At the wall edge": an exposed edge of his slot within this (the corner reach), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchEdgeReachCm = 100.f;

	/**
	 * The run-in clip may take over from this far from the target slot (the pack's clips run 4-5.5 m; only the last part plays, from
	 * where its body meets the runner): from here on he looks for the moment the clip's body is closest to him, cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "50"))
	float CoverSwitchClipLeadCm = 220.f;

	/** Within the lead: the clip takes over as soon as its body is this close to him (else when the fit stops improving), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchGoodFitCm = 40.f;

	/** ... and at the latest this far from the slot (a diagonal run-in meets the clip's line only close in), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "30"))
	float CoverSwitchLastChanceCm = 120.f;

	/** The clip's body more than this from the runner (a run in from an odd direction): the ordinary run-in / enter clip instead, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchMaxMismatchCm = 110.f;

	/**
	 * A sprinting dash (>= 500 cm/s) prefers the pack's sprint run-ins by this much of the clip fit (user 2026-10-21: "click and he quickly
	 * moves from one edge to the other"), cm. 0 = the nearest body of either set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0"))
	float CoverSwitchSprintPreferenceCm = 60.f;

	/** At least this much of the run-in clip plays (its turn into the wall), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Switch", meta = (ClampMin = "0.1"))
	float CoverSwitchMinClipSeconds = 0.6f;

	// --- Wall-following round the cover object (game designer 2026-10-20, Tactics/CoverWrapRules; cluster file
	// OperativeCharacterCoverWrap.cpp): in cover at an object, a click along the same object's outline (another face, the ground within
	// ~1 m of it) sends him there hugging it - a shimmy along each face, round each corner, in cover all the way. ---

	/** The wrap tunables below as the rules' config. */
	FCoverWrapConfig GetCoverWrapConfig() const;

	/** The wrap he would make to Click from where he is now (CoverWrapWorld::PlanWrap); NotInCover / Disabled / ... when none. */
	ECoverWrapVerdict PlanCoverWrap(const FVector& Click, FCoverWrapPlan& OutPlan) const;

	/**
	 * The wrap order: walks Plan's waypoints at the shimmy pace, in cover all the way (the cover slot switches to each face as he turns
	 * its corner, the body turning with the arc), and enters Plan.GoalSlot facing Plan.GoalFacing. A path round no corner is a plain shimmy
	 * (OrderShimmyTo). Refused like a shimmy (raging, panicking, frostbitten prone); prone he gets up first.
	 */
	EOperativeOrderResult OrderCoverWrap(const FCoverWrapPlan& Plan);

	/** The tactical pause: Plan is planned (after SetPendingCover(Plan.GoalSlot)); the release of the walk to its goal runs the wrap. */
	void SetPendingCoverWrap(const FCoverWrapPlan& Plan);
	bool HasPendingCoverWrap() const { return bCoverWrapPlanned && bHasPendingCover; }

	/** On his way round the object now (smokes / HUD). */
	bool IsCoverWrapping() const { return bCoverWrapActive; }
	/** The running / last wrap's plan. */
	const FCoverWrapPlan& GetCoverWrapPlan() const { return CoverWrapPlan; }
	/** Wraps that reached their goal / corners turned on the way (smokes). */
	int32 GetCoverWrapCount() const { return CoverWrapCount; }
	int32 GetCoverWrapCornersTurned() const { return CoverWrapCornersTurned; }

	/** Off: clicks round his cover object are the ordinary orders (a ground click leaves the cover). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Wrap")
	bool bCoverWrapEnabled = true;

	/** A ground click counts when it lies at most this far outside the cover object's outline, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Wrap", meta = (ClampMin = "0"))
	float CoverWrapMaxSurfaceDistanceCm = 100.f;

	/** Less travel than this along the outline is no wrap (the click leaves the cover as before), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Wrap", meta = (ClampMin = "0"))
	float CoverWrapMinTravelCm = 60.f;

	/** More travel than this is no wrap, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Wrap", meta = (ClampMin = "0"))
	float CoverWrapMaxTravelCm = 3000.f;

	/** A goal this close to a face's outer end becomes that corner's slot (facing it), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Wrap", meta = (ClampMin = "0"))
	float CoverWrapCornerSnapCm = 100.f;

	/** Snaps him to the slot's wall: facing along the normal, the default stance of the height, Cover_Enter. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void EnterCover(const FCoverSlot& Slot);

	/** Leaves the wall (move order, death, vault, the player clicking away). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void LeaveCover(const FString& Reason);

	/** Side-step along the same wall to Target (CoverTraceRules::IsSameWall); refused when it is another wall. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	EOperativeOrderResult OrderShimmyTo(const FCoverSlot& Target);

	/**
	 * Manual corner peek (user request 2026-10-13, Demo 01 rev 5c; command Cover.CornerPeek): in high cover - and, rev 5e (game
	 * designer 2026-10-14), crouched behind LOW cover (the crouched corner stance cvr_crch_fire_idle leans out round its end) - go to the exposed
	 * edge on Side (EdgeAlongCm = its position along the wall from the cover slot, + = along the slot's RightTangent; the
	 * controller finds it with CoverFacingRules::FindPeekEdge) and hold the corner-aim pose there - leaning out, rifle up - also
	 * with no enemy around, until another order (a move / another cover / a shimmy / the other corner; leaving the cover).
	 * Turn-based: only where he already stands at that corner (a shimmy is a grid move there; the pose itself costs no AP),
	 * else Refused. Unreachable when the wall gives no slot there. Rev 5d (CoverFacingRules::DecideCornerPeekOrder): already
	 * at / near that corner the aim starts in place (no shimmy within CornerSnapToleranceCm + 10 cm of the stand point).
	 */
	EOperativeOrderResult OrderCornerPeek(ECoverFacing Side, float EdgeAlongCm);

	/** A manual corner peek is ordered and not released yet (he may still be walking to the corner). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsManualCornerPeek() const { return bManualCornerPeek; }

	/** The ordered corner of the manual peek (valid while IsManualCornerPeek). */
	ECoverFacing GetManualCornerPeekSide() const { return ManualCornerPeekSide; }

	/** At the ordered corner now (in cover - high or low - not shimmying, the edge exposed on that side): the peek pose is held. */
	bool IsManualCornerPeekSpot() const;

	/** Manual peeks that reached their pose (the CornerPeek event; smokes / stats). */
	int32 GetManualCornerPeekCount() const { return ManualCornerPeekCount; }

	/**
	 * Demo 01 rev 5h (every level): leaning out round a corner now - the corner aim of a manual peek or of the corner hold.
	 * While it holds, hidden traps in his peek sight cone are spotted (PeekSpotConfig, UpdatePeekTrapSpotting).
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsPeekingRoundCorner() const;

	/** Rev 5h: the corner peek reveals hidden traps in its sight cone (range, cone, line of sight; TrapSpotRules). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek")
	FTrapPeekSpotConfig PeekSpotConfig;

	/** Hidden traps this operative revealed by peeking (smokes / stats). */
	int32 GetPeekSpottedCount() const { return PeekSpottedCount; }

	/**
	 * Peek sight (game designer 2026-10-19: "peeking out of cover he sees NOTHING"): while he leans out round a corner (corner aim /
	 * corner hold of high cover, the crouched corner lean of low cover) or aims over a low wall, a limited view - this cone round the
	 * pose's aim - opens from the leaned-out eye point (PeekSightRules). Used by the squad's sight (UTacticalSightSubsystem: revealing,
	 * targeting, posture fire), his drawn vision cone and - the other way round - the enemies looking at him.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover|Peek")
	FPeekSightConfig PeekSightConfig;

	/** The pose his peek view looks from now (None: not peeking - his own eyes only). */
	EPeekSightPose GetPeekSightPose() const;

	/**
	 * While peeking (GetPeekSightPose != None): OutEye = the leaned-out eye point (past the corner edge / above the low wall top),
	 * OutDirection = the pose's aim direction after the upper-body twist (planar unit). False when not peeking.
	 */
	bool GetPeekSight(FVector& OutEye, FVector& OutDirection) const;

	/** While peeking: the point an observer sees of him (the leaned-out head and shoulder, profile height). False when not peeking. */
	bool GetPeekExposedPoint(FVector& OutPoint) const;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	void SetCoverFireMode(ECoverFireMode Mode);

	/** Key N: CornerLean <-> BlindFire. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Cover")
	ECoverFireMode ToggleCoverFireMode();

	void SetCoverFacing(ECoverFacing Facing) { CoverFacing = Facing; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsInHighCover() const { return bInCover && CurrentCoverHeight == ECoverHeight::HighCover; }

	/** In cover at a wall whose ends can be peeked round: high cover, or low cover crouched (rev 5e, 2026-10-14). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsInPeekableCover() const { return bInCover && CurrentCoverHeight != ECoverHeight::None; }

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

	// --- Facing along the wall (user design rule 2026-10-06, CoverFacingRules): the back stays against the wall, the
	// body faces along it towards the last known threat; at the exposed edge on that side he takes the corner pose. ---

	/** At the exposed edge on the facing side (corner-ready pose). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bAtCoverCorner = false;

	/** A threat direction is known (else the facing defaults to the nearest exposed edge). */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	bool bHasCoverThreat = false;

	/** The last known threat (priority target / nearest visible / heard enemy) the facing follows. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CodexTactics|Cover")
	FVector CoverThreatLocation = FVector::ZeroVector;

	/** Running into cover: within this distance of the slot the body turns to face the wall (the enter clip's start pose), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverApproachFaceWallCm = 300.f;

	/** Cover entry without an enter clip weight to follow: the body's turn along the wall blends over this long, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverEntryYawBlendSeconds = 0.3f;

	/** How long the fire-ready corner pose is held after the last threat sighting / ordered target / shot, s (FCoverFacingConfig). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverFireReadyHoldSeconds = 4.f;

	/** Smokes only: no threat is ever gathered (the corner pose relaxes to the look-around idle). */
	bool bIgnoreCoverThreatForTesting = false;

	/** A threat (seen, heard, ordered, shot at) was known within CoverFireReadyHoldSeconds. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsCoverThreatActive() const;

	/** Corner pose with an active threat: the AnimBP shows cvr_*_fire_idle (else the corner pose is cvr_*_look_at_idle). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cover")
	bool IsCoverFireReady() const;

	/** Actor yaw wanted in cover: along the wall towards CoverFacing. */
	float GetCoverFacingYaw() const;

	/** The current shimmy goes towards the facing side (forward clip), else backwards. */
	bool IsShimmyForward() const;

	/** Facing he would take at Slot with what he knows now (the cover ghost preview). */
	ECoverFacing PredictCoverFacing(const FCoverSlot& Slot) const;

	/**
	 * A grid (turn-based) shot from cover: lean / blind pose by CoverFireMode and the cover fire clip (OnWeaponFiredNative),
	 * the body kept along the wall. The caller resolves the hit.
	 */
	void PlayCoverShot(AActor* Target, bool bHit);

	// --- Open shot from cover (user decision 2026-10-06, CoverFacingRules::ShouldCornerShot): the cover shot is only for
	// targets behind the wall / around the corner; a target out on the open side gets a normal shot — he steps off the
	// wall (leaves the cover), fires as usual and, unless ordered elsewhere, comes back to the same slot. ---

	/** In cover and Target needs the cover shot (behind the wall / around the corner); false out of cover or for an open-side target. */
	bool IsCornerShotTarget(const FVector& TargetLocation) const;

	/**
	 * Steps off the wall for a normal shot at an open-side target: leaves the cover and remembers the slot to come back to
	 * (bReturnToCoverAfterOpenShot). False (nothing done) while shimmying or out of cover.
	 */
	bool BeginOpenShotFromCover(const AActor* Target);

	/** Off the wall for an open shot, coming back to the slot once he stops firing (the AnimInstance skips the cover exit / enter clips). */
	bool IsCoverOpenShotActive() const { return bOpenShotReturnPending; }

	/** This cover entry is the return after an open shot (no Cover_Enter clip: he only stepped off the wall). */
	bool IsQuietCoverEntry() const { return bQuietCoverEntry; }

	/** After an open shot from cover (target in front of the wall) he returns to the slot unless ordered elsewhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover")
	bool bReturnToCoverAfterOpenShot = true;

	/**
	 * He returns to the slot after this calm: no open shot and no enemy in sight within 30 m for this long (user PIE
	 * 2026-10-07: he stepped back into cover between the shots at a stream of targets; Jev: stay out while more keep
	 * coming, back in once none is in sight), s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float OpenShotReturnDelaySeconds = 1.f;

	/** After a cover shot he counts as engaged this long: the fire stance keeps its side (CoverFacingRules::KeepEngagedFacing), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEngageHoldSeconds = 3.f;

	// --- Corner hold and the aim cone (user decisions 2026-10-07, PIE videos CoverBug_01 / CoverBug_02): at an exposed edge
	// of a high wall he steps out into the corner fire stance and HOLDS it - no ducking between shots, no side flips; he
	// goes behind the corner only to reload and steps right back out. A shot is fired only where the current pose aims:
	// within AimConeDeg of the stance's aim direction after the upper-body twist (the 2D aim offset's AimYaw, limited by
	// UOperativeAnimInstance::AimYawClampDegrees). A target beyond that - a flank rush in front of the wall - makes him
	// step off the wall into the open stance and turn to it (AimTurnRateDegPerSec), then fire. ---

	/** At an exposed edge of a high wall he holds the corner fire stance (steps out at once, fires from it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover")
	bool bCornerHoldAtEdge = true;

	/**
	 * The corner hold only in a fight or with a fresh known threat (user bug 2026-10-11): exploring with no enemy known he
	 * hugs the wall at its edge (look-around corner pose) instead of stepping out past it and aiming round it. Off = the
	 * 2026-10-07 behaviour (the hold at every exposed edge, also with nobody around).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover")
	bool bCornerHoldOnlyWhenEngaged = true;

	/**
	 * The older automatic breaks of the corner aim (CoverDecisionRules::DecideCornerAim: duck for safety, return with no
	 * target in sight). Off by user decision 2026-10-07: only the reload sends him behind the corner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover")
	bool bCornerAutoDuck = false;

	/**
	 * The corner fire stance (cvr_*_fire_idle) aims this far past the along-wall direction towards the space behind the
	 * wall (round the corner), degrees. Measured 2026-10-07 on the real barrel in CornerHoldSmoke: anim_M4_cvr_std_fire_idle_L
	 * steps 73 cm past the edge and aims 102-104 deg past the along-wall line, i.e. straight past the corner into the
	 * space behind the wall (slightly back towards the wall).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "-90", ClampMax = "150"))
	float CornerAimOutwardDeg = 100.f;

	/** A shot leaves only while the barrel is within this angle of the target (after the upper-body twist), degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "1", ClampMax = "90"))
	float AimConeDeg = 30.f;

	/** Out of cover he turns the body to the target at least this fast (a quick but visible turn), deg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "30"))
	float AimTurnRateDegPerSec = 360.f;

	/** Once off the wall for an open shot he stays in the open stance at least this long, s (no cover <-> open flip-flop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float OpenShotCommitSeconds = 3.f;

	/** At a cover (or stepped off it for open shots): a melee enemy closing within this distance takes over as the target at once (no flank-switch delay; in the open the stance delay stays), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float FlankRushBreakCm = 700.f;

	/** At an exposed edge of a high wall, facing it, not shimmying: the corner hold spot. */
	bool IsCornerHoldSpot() const;

	/**
	 * He steps out into the corner hold now: at the hold spot and (bCornerHoldOnlyWhenEngaged) in a fight - wave combat or
	 * a turn-based fight - or with a fresh known threat (CoverFacingRules::WantsCornerHold). Else he hugs the wall.
	 */
	bool WantsCornerHold() const;

	/** Where the current pose's aim starts (corner hold: the lean-out muzzle; else the muzzle height over the actor). */
	FVector GetStanceAimOrigin() const;

	/** The current pose's aim direction before the upper-body twist (2D unit): corner hold stance or the barrel's yaw. */
	FVector GetStanceAimBaseDirection() const;

	/** The upper-body twist the AnimInstance applies now (AimYaw x AimOffsetAlpha; 0 without the 2D aim offset), degrees. */
	float GetAppliedAimYaw() const;

	/** How far the upper body may twist to a target (UOperativeAnimInstance::AimYawClampDegrees; 0 without yaw), degrees. */
	float GetAimTwistLimitDeg() const;

	/** The corner hold claims targets within this angle of its aim direction (twist limit + cone - 5), degrees. */
	float GetCornerHoldReachDeg() const;

	/** The lean-out muzzle at Slot's Facing corner for a muzzle at Muzzle (CoverRules::CornerMuzzle). */
	static FVector CornerFireOriginFor(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Muzzle);

	/** Angle between where the pose aims now (after the twist) and the target, degrees. */
	float GetShotAimResidualDeg(const FVector& TargetLocation) const;

	/** Turns the body at once so the barrel points at TargetLocation (a grid shot resolves in the same frame). */
	void FaceAimAt(const FVector& TargetLocation);

	/** Open shots taken off the wall (smokes / stats). */
	int32 GetCoverOpenShots() const { return CoverOpenShots; }

	/**
	 * Corner stand-off per pack clip side and stance, cm (FCoverFacingConfig defaults, measured 2026-10-06: the peek
	 * clip's body step-out minus 12 cm). _L plays at his own right corner, _R at his left.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverStandOffStandPackLCm = 57.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverStandOffStandPackRCm = 28.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverStandOffCrouchPackLCm = 65.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverStandOffCrouchPackRCm = 49.f;

	/** The facing rules' config with this operative's tunables (stand-offs). */
	FCoverFacingConfig GetCoverFacingConfig() const;

	// --- Sustained corner aim (user request 2026-10-07, CoverDecisionRules::DecideCornerAim, Jev-calibrated): after the
	// first corner shot at a target behind the wall he stays leaned out in the fire stance (cvr_*_fire_idle) and fires
	// from it; he ducks back behind the corner only to reload, for safety (laser, grenade, flank, heavy fire, big hit,
	// badly wounded) or when no target was in sight for the grace period, and on a shimmy / move order / leaving. ---

	/** Leaned out in the corner fire stance (the AnimInstance holds cvr_*_fire_idle, shots play cvr_*_fire from it). */
	bool IsCornerAimActive() const { return bCornerAimActive; }

	/** Why the last sustained aim ended (StayAndFire = never ended yet). */
	ECornerAimDecision GetLastCornerAimBreak() const { return LastCornerAimBreak; }

	/** How many times the sustained aim ended for Reason (smokes / stats). */
	int32 GetCornerAimBreakCount(ECornerAimDecision Reason) const { return CornerAimBreakCounts[static_cast<int32>(Reason)]; }

	/** Ends the sustained aim (Reason is logged and counted); DuckForSafety also blocks a new lean-out for the re-entry delay. */
	void EndCornerAim(ECornerAimDecision Reason, const TCHAR* Why);

	/** No corner-shot target in sight this long: back to the plain cover pose, s (FCoverDecisionConfig::AimNoTargetGraceSeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CornerAimNoTargetGraceSeconds = 2.f;

	/** After ducking back for safety he does not lean out again for this long, s (FCoverDecisionConfig::AimReentryDelaySeconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CornerAimReentryDelaySeconds = 1.5f;

	// --- Moving off / running into cover (user-found bugs 2026-10-07) ---

	/** Leaves the cover because he walks off it (grid walk): the AnimInstance plays no fire -> idle exit, the walk shows at once. */
	void LeaveCoverToMove(const FString& Reason);

	/** He left the cover to walk off (a move order or a grid walk). */
	bool IsMovingOffCover() const { return !bInCover && (bCoverLeftToMove || bHasMoveOrder); }

	/** The current cover entry came from a run (the enter clip blends over the last steps). */
	bool IsCoverEnteringFromRun() const { return bInCover && bCoverEntryFromRun; }

	/**
	 * Running into cover: the cover is entered this far before the slot — where the enter clip's start pose stands (the
	 * pack's std_idle_fwd_to_cvr_std_idle begins 76 cm out from the wall facing it, crouched 98 cm) — plus speed x
	 * CoverEnterDecelSeconds / 2 (capped) that the body glides on, decelerating, while the clip blends in. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEnterClipLeadStandCm = 76.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEnterClipLeadCrouchCm = 98.f;

	/** The run's momentum glides out over this long into the enter clip, s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverEnterDecelSeconds = 0.4f;

	/**
	 * Low cover corner peek (2026-10-20): the cancelled sideways root step of the crouched fire stance is let through over this long when the
	 * peek starts (he visibly leans out past the wall's end) and cancelled again when it is over, s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CoverLeanBlendSeconds = 0.25f;

	/**
	 * Corner peek (2026-10-20): the mesh leans this much further out past the wall's end than the clip's own step-out, per pack clip side, cm.
	 * Measured on the head bone: the _L clips (his right corner) put the head 25 cm past the edge, the _R ones (his left corner) only 14 cm -
	 * his head sits right over the pelvis - so _R gets +11. A tracked, eased mesh offset (CoverEntryAppliedOffset), gone when the peek is over;
	 * the stand-off is NOT shortened instead: hugging the wall 17 cm from its end lets his eyes see round it (PeekSightSmoke).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CornerLeanExtraPackLCm = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CornerLeanExtraPackRCm = 11.f;

	/**
	 * Wall clearance (user report 2026-10-11: the body / rifle sank into the wall - during a cover entry the old run pose,
	 * held on the turned mesh, blends with the enter clip and swings a hand through the wall): while in cover the closest
	 * skin bone to the wall plane is kept this far out; the mesh is pushed out along the wall normal by what is missing,
	 * eased in / out (UpdateCoverWallClearance). 0 = off. cm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Clearance", meta = (ClampMin = "0"))
	float CoverWallClearanceCm = 6.f;

	/** Most the mesh is pushed away from the wall, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Clearance", meta = (ClampMin = "0"))
	float CoverWallClearanceMaxPushCm = 60.f;

	/** How fast the push rises, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Clearance", meta = (ClampMin = "1"))
	float CoverWallClearanceRiseCmPerSec = 300.f;

	/** How fast the push eases out when the need is gone, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Clearance", meta = (ClampMin = "1"))
	float CoverWallClearanceInterp = 8.f;

	/** Cap of the extra glide distance, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CoverEnterMaxGlideCm = 120.f;

	/** Visual mesh offset applied now by the run-in blend, actor-local (smokes), cm. */
	FVector GetCoverEntryMeshOffset() const { return CoverEntryAppliedOffset; }

	/**
	 * Rev 2026-10-19: how far the mesh stands from the Blueprint's place on the capsule now, horizontally, cm (smokes: 0 out of cover;
	 * in cover the tracked run-in / wall-push offsets).
	 */
	float GetMeshBaseOffsetCm() const;

	/**
	 * Rev 2026-10-19: drops every transient body offset (cover run-in blend, wall push, entry yaw) and puts the mesh back on its
	 * Blueprint place on the capsule (a load in place, a teleport).
	 */
	void ResetBodyOffsets();

	/**
	 * Rev 2026-10-19 (user: "the save keeps the state the character was in BEFORE the load"): a load in place applies the saved fields on
	 * this LIVE operative - first everything transient goes: the knockdown (no get-up), panic / stress, cover / pending cover / shimmy /
	 * corner aim / manual peek, the prone dive (no crawl-on), every montage, the body offsets, the move, the autonomy anchor / target.
	 * (Player loads reopen the level - USaveGameSubsystem::LoadGameWithTravel - so this is the dev / smoke path.)
	 */
	void ResetTransientStateForLoad();

	/** A friendly (squad) grenade that has landed within the danger radius also sends him behind the corner (default off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover")
	bool bCornerAimDuckForFriendlyGrenades = false;

	/** The decision config with this operative's tunables (grace, re-entry delay). */
	FCoverDecisionConfig GetCoverDecisionConfig() const;

	/** Smokes: the next corner-aim situation reports this suppression pressure (< 0: measured). */
	float ForcedCornerAimSuppressionForTesting = -1.f;

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

	/** The posture lets this operative open fire on its own now at ANY enemy (provocation included; autonomous grenades). */
	bool MayAutoFireNow() const;

	/**
	 * The posture lets him open fire on his own at THIS enemy (user decision 2026-10-19, FirePostureRules::MayAutoFireAt): Passive
	 * never, Defensive at an enemy that has detected the squad (AEnemyCharacter::IsHuntingSquad) or once provoked, Aggressive always.
	 */
	bool MayAutoFireAtEnemy(const AActor* Enemy) const;

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
	 * The detonation of a targeted shot that hit an object (shared by ShootAtObject and the APC cannon, P4): a barrel
	 * explodes, a mine goes off, a trapped crate / trapped object detonates remotely. False when nothing went off (an
	 * untrapped crate, a burning barrel, not an object kind).
	 */
	static bool DetonateShotTarget(AActor* Target, ETargetedShotKind Kind, const FText& ShooterName);

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

	/**
	 * Executes the planned shots in Godot order: barrel, mine, crate, trapped object, then the priority enemy. An object
	 * shot that cannot fire yet (reloading, misfire, frozen weapon, no line out of the cover) stays planned so the squad
	 * retries it (bug 2026-10-08: it was silently dropped). Returns true when nothing is left planned.
	 */
	bool ExecutePlannedTargetedShots();

	/** Drops the planned targeted shots and the planned quick fire (pause plans dropped / restarted). */
	void ClearPlannedTargetedShots() { PlannedShots.Reset(); bQuickFirePlanned = false; }

	int32 GetPlannedTargetedShotCount() const { return PlannedShots.Num(); }

	// --- Quick fire, Ctrl + click on a point (game designer request 2026-10-19; QuickFireRules, OperativeCharacterQuickFire.cpp; UE-only) ---

	/**
	 * Quick fire order: turn to Point and fire a short burst at it now (QuickFireRules::GetBurstRounds; the weapon's normal
	 * cadence, ammo, reload, misfire). Enemy (a visible live enemy the player clicked near) is shot with the normal hit rules
	 * while it is alive, in range and in the line of fire; otherwise every round is area fire at the point (it hits what it
	 * crosses: an enemy by the area-fire chance, a barrel / mine / trapped object goes off). An explicit order: any fire
	 * posture, also while exploring (the gunshot is heard). The sniper stops and kneels first (direct-order rule); in cover he
	 * steps off the wall for it (open shot) and walks back. False when he cannot take it: dead, knocked down, aboard, raging,
	 * panicking, a weapon without ammo (the knife) or a frozen weapon.
	 */
	bool OrderQuickFire(const FVector& Point, AActor* Enemy);

	/** Tactical pause: remembers the quick fire; ExecutePlannedQuickFire (pause release) orders it. False when he cannot fire. */
	bool PlanQuickFire(const FVector& Point, AActor* Enemy);

	/** Pause release: the planned quick fire becomes an order. True when one was planned and ordered. */
	bool ExecutePlannedQuickFire();

	/** Drops a running quick fire order (a move order, turn-based combat, the timeout). */
	void CancelQuickFire();

	bool HasQuickFireOrder() const { return bQuickFireActive; }
	bool HasPlannedQuickFire() const { return bQuickFirePlanned; }
	FVector GetQuickFirePoint() const { return QuickFirePoint; }
	AActor* GetQuickFireTarget() const { return QuickFireTarget.Get(); }
	/** Rounds this operative fired by quick fire orders since spawn (smokes). */
	int32 GetQuickFireRoundsFired() const { return QuickFireRoundsFired; }
	/** Planar angle between his aim direction and the point at his last quick fire round, degrees (smokes). */
	float GetLastQuickFireAimErrorDeg() const { return LastQuickFireAimErrorDeg; }

	/** Line-of-fire origin: the feet plus the Godot muzzle height of the stance (1.4 / 0.85 / 0.25 m). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	FVector GetMuzzleLocation() const;

	/** Where tracers leave: the weapon mesh's "Muzzle" socket, else MuzzleOffset on it, else GetMuzzleLocation. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Combat")
	FVector GetWeaponMuzzleLocation() const;

	/**
	 * The point he aims at now (the one the tracer flies to: the target's capsule centre, or the blind-fire silhouette):
	 * the target of a shot fired within AimTargetHoldSeconds, else the current combat target (manual priority, Commander
	 * Mode or the closest visible enemy). False when he aims at nothing. Feeds UOperativeAnimInstance::AimPitch.
	 */
	bool GetAimTargetPoint(FVector& OutPoint) const;

	/** A shot's target stays the aim target this long after the shot (turn-based shots, targeted shots at objects), s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Combat", meta = (ClampMin = "0"))
	float AimTargetHoldSeconds = 1.0f;

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

	/**
	 * User request 2026-10-22 (ThreatFacingRules): standing after a retreat in a real-time fight with no target in his sights, he turns
	 * to the enemy he was fighting (or the nearest live one). True while that turn drives his facing.
	 */
	bool IsTurningToThreat() const { return bTurningToThreat; }
	/** World time of his last shot (any target / point), s. */
	double GetLastShotTime() const { return LastShotTime; }
	void ClearIdleFacing() { bHasIdleFacing = false; }

	// --- Formation look sectors (Codex.Formation.Sectors, docs/design/FORMATION_SECTORS.md; driven by USquadSubsystem) -------------

	/**
	 * The yaw his formation sector asks for this frame (FormationSectorRules::LookYaw / the wall layout); unset = none. USquadSubsystem
	 * sets or clears it every tick. UpdateCombatFacing turns the body to it, walking or standing (the Rifle_2 8-direction locomotion
	 * then strafes / walks backwards), below a combat target, an explicit facing order (SetFacingPoint, quick fire), the cover and an
	 * idle facing set by an order (group cover, the warm spot) - design priority combat target > facing order > cover > formation sector
	 * > movement direction. Not prone, sprinting, carrying or on the turn-based grid.
	 */
	void SetFormationLookYaw(const TOptional<float>& Yaw) { FormationLookYaw = Yaw; }
	TOptional<float> GetFormationLookYaw() const { return FormationLookYaw; }

	/** True while the formation sector drives his facing (last UpdateCombatFacing). */
	bool IsFacingFormationSector() const { return bFacingFormationSector; }

	/**
	 * Patrol pace (FORMATION_SECTORS.md: the whole squad walks at pace.patrol x walk speed in patrol mode, so the rear man keeps up
	 * walking backwards): his walk speed (not the sprint, not a formation move's own speed) is scaled by this. 1 = none.
	 */
	void SetFormationPaceMultiplier(float Multiplier);
	float GetFormationPaceMultiplier() const { return FormationPaceMultiplier; }

	/**
	 * The formation wall layout's baseline pose (a group cover order in group mode, FORMATION_SECTORS.md "Wall"): crouched, back to the
	 * wall, facing out. His next own move order stands him up again when the crouch was the squad's (IsStanceFromSquad).
	 */
	void SetFormationWallPose(bool bInPose) { bFormationWallPose = bInPose; }
	bool IsInFormationWallPose() const { return bFormationWallPose; }

	/**
	 * Formation wall group (Codex.Formation.Sectors, rules v2.2, USquadSubsystem::ReallocateWallGroup): the side along the wall the
	 * squad's coverage allocator gives this FOLLOWER in his cover slot (unset = his own CoverFacingRules: threat side, corner hold, nearest
	 * edge). Above the threat side and the corner hold, below a manual corner peek; no automatic walk to a corner for it. Cleared when he
	 * leaves the cover.
	 */
	void SetCoverLookSide(const TOptional<ECoverFacing>& Side);
	TOptional<ECoverFacing> GetCoverLookSide() const { return CoverLookSide; }

	/**
	 * Formation wall group: out of his cover slot (quietly - no CoverLeft event) into the back-to-wall pose where he stands: crouched (the
	 * squad's crouch), facing out - his look then comes from the squad every tick (SetFormationLookYaw). False when he is not in cover or
	 * is busy (side-stepping, aiming round the corner, moving, knocked down, the entry glide not over).
	 */
	bool LeaveCoverForWallPose();

	/** He has a facing order of his own (SetFacingPoint) or a quick fire: a look decided elsewhere (the coverage allocator's fixed look). */
	bool HasOwnFacing() const { return bExplicitFacing || bQuickFireActive; }

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

	/** Godot cover_chatter_timer: the "In cover" radio line at most every 5 s. */
	double LastCoverChatterTime = -100.0;

	/** Godot weapon_freeze_notify_timer: "WEAPON FROZEN" at most every 2.5 s. */
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
	/** TakeHit / TakeGridBlast: one incoming blow resolved by OperativeHitRules for its kind. */
	float ResolveIncomingHit(EOperativeHitKind Kind, float Amount, const FString& Attacker, bool bCrit, AActor* AttackerActor,
		float CritMultiplierApplied);

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

	/** Vision cone: visibility / shape / colour every tick, the spotted check every 0.2 s. */
	void UpdateVisionCone(float DeltaTime);
	float VisionConeCheckTimer = 0.f;
	bool bVisionConeSpotted = false;

	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth, float Delta);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);
	/** A dead body: no movement, no pawn / click collision, no selection, no orders (stays where he fell). */
	void BecomeCorpse();
	/** Leaves supplies in searchable remains at his feet (Godot corpse_loot). */
	void SpawnRemainsLoot();
	bool bKilledInAction = false;
	bool bHasLastHitSource = false;
	FVector LastHitSource = FVector::ZeroVector;
	/** Knockdown phase change: interrupts (aim, reload, move, cover), frees / restores the capsule and the movement. */
	void HandleKnockdownPhase(EKnockdownPhase NewPhase, EKnockdownPhase OldPhase);
	/** After a get-up: back to the cover slot a blast took him out of, by KnockdownRules::DecideCoverReturn (2026-10-18). */
	void ReturnToCoverAfterKnockdown();
	/** The capsule's Pawn response before a knockdown (restored when he is up). */
	TEnumAsByte<ECollisionResponse> KnockdownSavedPawnResponse = ECR_Block;
	/**
	 * The one path request of every operative move. Rev 5i: under a tutorial confinement (UTutorialSubsystem) an own order whose
	 * destination or path end lies outside the allowed area is refused (with the feed line); a formation move (bFormationMove) is
	 * pulled into the holding area instead.
	 */
	EOperativeOrderResult RequestMove(const FVector& Destination, bool bFormationMove = false);
	/** The nav query filter of his moves (crawl-only areas, known traps: TripwireRules::ChooseOperativeMoveFilter); none = the default. */
	TSubclassOf<class UNavigationQueryFilter> ChooseMoveFilter() const;
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
	bool EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked,
		const TCHAR** OutReason = nullptr) const;
	/** ProcessCombatShooting's body: returns why no shot left this frame ("fired" when one did) and the target it had. */
	const TCHAR* RunCombatShooting(float DeltaTime, AActor*& OutTarget);
	/**
	 * Dev trace (Codex.Debug.FireTrace 1, 2026-10-18 corner hold bug hunt): logs the fire decision when its reason
	 * changes, and every 2 s while a live enemy is within 5 m and no shot leaves - with the nearest enemies' shot-line
	 * verdicts and the aim residual.
	 */
	void TraceFireDecision(const TCHAR* Reason, const AActor* Target);
	FString LastFireTraceReason;
	double LastFireTraceTime = -1.0e9;
	/** Godot _notify_barricade_blocked (every 3.5 s at most). */
	void NotifyBarricadeBlocked();
	float BarricadeBlockNotifyTimer = 0.f;
	TWeakObjectPtr<AActor> CurrentCombatTarget;
	/** The last shot's target / point and time (GetAimTargetPoint). */
	TWeakObjectPtr<AActor> LastShotTarget;
	FVector LastShotPoint = FVector::ZeroVector;
	bool bLastShotBlind = false;
	double LastShotTime = -1.0e9;
	void NoteShotAim(AActor* Target, const FVector& Point, bool bBlind);

	/** Godot combat_facing_direction: real-time fight, not sprinting, a live target -> face it, no turn to the movement. */
	void UpdateCombatFacing(float DeltaTime);
	bool bFacingCombatTarget = false;
	float BarrelYawOffset = 0.f;
	/** Planar velocity smoothed for the movement facing (FacingRules). */
	FVector SmoothedVelocity = FVector::ZeroVector;
	float IdleFacingYaw = 0.f;
	bool bHasIdleFacing = false;
	/** Formation look sectors (SetFormationLookYaw / SetFormationPaceMultiplier / SetFormationWallPose). */
	TOptional<float> FormationLookYaw;
	bool bFacingFormationSector = false;
	float FormationPaceMultiplier = 1.f;
	bool bFormationWallPose = false;
	/** SetCoverLookSide: the wall group's side along the wall in cover. */
	TOptional<ECoverFacing> CoverLookSide;
	/** ThreatFacingRules: the enemy he last faced in this fight and where he last saw it; a facing order of his own (SetFacingPoint). */
	TWeakObjectPtr<AActor> LastThreat;
	FVector LastThreatLocation = FVector::ZeroVector;
	bool bFoughtThisFight = false;
	bool bExplicitFacing = false;
	bool bTurningToThreat = false;
	/** The point ThreatFacingRules turns him to now; false: nothing to face. */
	bool FindThreatPoint(FVector& OutPoint) const;
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
	/** SetHoldGroupCoverSlot: a group cover order's exact slot, no automatic corner walk. */
	bool bHoldGroupCoverSlot = false;
	float LeanTimer = 0.f;
	float BlindFireTimer = 0.f;
	int32 CoverLeanShots = 0;
	int32 CoverBlindShots = 0;
	/** Facing lock at the wall, lean / blind timers, shimmy state, the pending slot on arrival. */
	void UpdateCover(float DeltaTime);
	/** Called before a shot from cover: lean out or keep the head down by CoverFireMode. */
	void BeginCoverShot();
	/** World time of the last threat sighting / shot in cover (IsCoverThreatActive). */
	double CoverThreatSeenTime = -1.0e9;

	/** Facing along the wall: threat re-evaluated every ThreatUpdateSeconds. */
	float CoverThreatTimer = 0.f;
	/** The enemy his last cover shot aimed at (the strongest threat for the facing while it lives). */
	TWeakObjectPtr<AActor> CoverShotTarget;
	/** The shimmy running was started by the corner snap (not the player). */
	bool bCoverAutoSnap = false;
	/** Check the corner snap on the next cover update (after entering a wall / turning to the other side). */
	bool bCoverSnapPending = false;
	/** Re-reads the threat, turns to its side (hysteresis), snaps to the corner after a turn; bAtCoverCorner. */
	void UpdateCoverFacing(bool bAllowSnap);
	/** Priority target, else nearest visible enemy, else nearest heard ghost (CoverFacingRules::PickThreat). */
	bool GatherCoverThreat(FVector& OutLocation) const;
	/** Walks to the exposed edge on the facing side when it lies just beyond the corner reach. */
	void TrySnapToCoverCorner();
	/** Open shot from cover: the slot to come back to, the last open shot's time, the return pending. */
	FCoverSlot OpenShotReturnSlot;
	double OpenShotLastTime = -1.0e9;
	/** When he stepped off the wall for the open shots (OpenShotCommitSeconds). */
	double OpenShotStartTime = -1.0e9;
	/** The slot he returns to was a corner hold spot facing this side (enemies its stance covers do not keep him out). */
	bool bOpenShotReturnToCornerHold = false;
	ECoverFacing OpenShotReturnFacing = ECoverFacing::Right;
	bool bOpenShotReturnPending = false;
	/** The current cover entry is the return after an open shot (IsQuietCoverEntry); the request is set around that EnterCover. */
	bool bQuietCoverEntry = false;
	bool bQuietCoverEntryRequest = false;
	/** LeaveCover raises no CoverLeft event (the open-shot step off the wall comes back on its own). */
	bool bQuietCoverLeaveRequest = false;
	/** EnterCover ran (new wall, not a quiet return): CoverEntered waits until the body has stopped at the slot. */
	bool bCoverEnteredEventPending = false;
	float CoverEnteredPendingSeconds = 0.f;
	/**
	 * Raises the pending CoverEntered once he has settled: the run-in glide / enter clip blend over, on the slot (within 25 cm),
	 * no corner walk pending or running, not moving; at the latest 3 s after the entry. Called from UpdateCover.
	 */
	void UpdateCoverEnteredEvent(float DeltaTime);
	/**
	 * Demo 01 tutorial (beat A6): CoverEntered / CoverLeft / CoverShimmy / CornerReached through GameplayEvents::RaiseWithContext
	 * (subject = this operative, target = the wall actor, kind = High / Low or the corner side, location = the slot).
	 */
	void RaiseCoverEvent(FName EventName, const FCoverSlot& Slot, FName Kind = NAME_None);
	int32 CoverOpenShots = 0;
	/** Back to the slot once the open shots are over (no move order, standing still, not far off). */
	void UpdateOpenShotReturn();
	/** Moving off / running into cover state. */
	bool bCoverLeftToMove = false;
	bool bCoverEntryFromRun = false;
	bool bCoverEntryFromRunRequest = false;
	/** Run-in blend: the clip's start-pose offset (world), the residual glide (world), time, the offset applied to the mesh. */
	FVector CoverEntryClipOffset = FVector::ZeroVector;
	FVector CoverEntryGlide = FVector::ZeroVector;
	float CoverEntryBlendTime = -1.f;
	FVector CoverEntryAppliedOffset = FVector::ZeroVector;
	/** Cover entry: the actor's instant turn along the wall, drawn back on the mesh while the enter clip blends in (deg). */
	float CoverEntryYawOffset = 0.f;
	float CoverEntryYawTime = -1.f;
	float CoverEntryAppliedYaw = 0.f;
	float CoverEntryTurnDeg = 0.f;
	/** The body-yaw blend runs outside cover (StartBodyYawBlend). */
	bool bBodyYawFree = false;
	/** Entered with a big turn: the old yaw is held on the mesh until the enter clip plays (one anim update). */
	bool bCoverEntryAwaitClip = false;
	/** The awaited enter clip has been seen playing (CoverFacingRules::EntryHeldYawFraction); seconds since the entry. */
	bool bCoverEntryClipSeen = false;
	float CoverEntryAwaitSeconds = 0.f;
	/** Enters the pending cover early when running in (the enter clip overlaps the last steps). */
	void TryEnterCoverFromRun();
	/**
	 * The switch's run-in (TryEnterCoverFromRun with a pending switch): within CoverSwitchClipLeadCm, heading for the slot, enters and
	 * picks the run-in clip + its start for the facing he ends with. False = not now (the ordinary run-in rules go on).
	 */
	bool TryEnterCoverSwitchFromRun();
	/** Wall-edge cover switch state (OrderCoverSwitch -> the pending switch -> the run-in entry). */
	bool bCoverSwitchPending = false;
	bool bCoverSwitchEntry = false;
	bool bCoverSwitchEntryRequest = false;
	ECoverSwitchApproach CoverSwitchApproach = ECoverSwitchApproach::Front;
	bool bCoverSwitchSprintClip = false;
	float CoverSwitchStartSeconds = 0.f;
	int32 CoverSwitchCount = 0;
	float LastCoverSwitchMismatchCm = 0.f;
	/** The best clip fit seen on this run-in so far (TryEnterCoverSwitchFromRun takes the clip when it stops improving), cm. */
	float CoverSwitchProbeMismatchCm = TNumericLimits<float>::Max();
	/** The side a switch arrival faces (the corner he was sent to; the mirrored pose), applied by the next EnterCover of another wall. */
	bool bCoverSwitchFacingPending = false;
	ECoverFacing CoverSwitchArrivalFacing = ECoverFacing::Right;
	/** Wrap state (OrderCoverWrap -> waypoints -> the goal's EnterCover). */
	bool bCoverWrapActive = false;
	bool bCoverWrapPlanned = false;
	FCoverWrapPlan CoverWrapPlan;
	int32 CoverWrapNext = 0;
	int32 CoverWrapSlotIndex = 0;
	int32 CoverWrapCount = 0;
	int32 CoverWrapCornersTurned = 0;
	float CoverWrapStallSeconds = 0.f;
	float CoverWrapElapsedSeconds = 0.f;
	/** How far along the plan's path he is (never back), cm; the path distance of every plan point. */
	float CoverWrapProgressCm = 0.f;
	TArray<float> CoverWrapPointDistance;
	/**
	 * The running wrap (UpdateCover): steers along the plan's path (AddMovementInput at the point a little ahead - exactly on the offset line,
	 * not the navmesh's distance off the wall), the next face at each corner, EnterCover at the goal; stuck -> in cover where he stands.
	 */
	void UpdateCoverWrap(float DeltaTime);
	/** HandleMoveFinished during a wrap (the stop of an earlier path following): true = ignored, he steers himself. */
	bool ContinueCoverWrapOnArrival();
	/** Holds the plan's face SlotIndex from now on (the turn round a corner). */
	void SetCoverWrapFace(int32 SlotIndex);
	/** Ends the running wrap (another order, leaving the cover, a stall). */
	void CancelCoverWrap(const TCHAR* Why);
	/** A stuck / blocked wrap: in cover where he stands (the face he holds, traced there), else out of cover. */
	void GiveUpCoverWrapHere(const TCHAR* Why);
	/** The planned wrap is released (OrderMoveTo to its goal): it starts. */
	EOperativeOrderResult StartPlannedCoverWrap();
	/** Moves the mesh so the body stays continuous while the enter clip blends in (V = (1 - w) C + glide). */
	void UpdateCoverEntryBlend(float DeltaTime);
	/** The mesh push away from the wall that keeps every skin bone outside it (CoverWallClearanceCm), world cm along the normal. */
	float UpdateCoverWallClearance(float DeltaTime);
	float CoverWallPush = 0.f;
	/**
	 * The wall push applied to the mesh now, actor-local (rev 2026-10-19). It was a scalar re-applied along the CURRENT facing: a turn /
	 * a teleport / a new wall normal while the push was on left up to ~1.2 m of it on the mesh for good (the body beside its capsule).
	 */
	FVector CoverWallPushAppliedLocal = FVector::ZeroVector;
	/**
	 * Low cover: share of the fire stance's sideways root step that is cancelled on the mesh (1 = over the top, where he stands; 0 = the corner
	 * peek, where the step is the lean out past the wall's end - 2026-10-20). Eased by UpdateCoverEntryBlend over CoverLeanBlendSeconds.
	 */
	float CoverLeanCancelAlpha = 1.f;
	/** The corner peek's extra lean share (0..1, CornerLeanExtraPack*Cm), eased by UpdateCoverEntryBlend. */
	float CornerLeanExtraAlpha = 0.f;
	FVector CoverWallPushDir = FVector::ForwardVector;
	/** Skin bones (no IK targets / virtual bones) of the mesh asset the list was built for. */
	TArray<int32> CoverClearanceBones;
	TWeakObjectPtr<const USkeletalMesh> CoverClearanceBonesMesh;
	/** World time of the last shot from cover (lean / blind; CoverEngageHoldSeconds). */
	double CoverLastShotTime = -1.0e9;
	/** Damage from ranged attackers in the last seconds (decays like RecentIncomingDamage; the corner-aim duck input). */
	float RecentRangedDamage = 0.f;
	/** The corner-aim trace (Codex.Debug.CornerAim, the [CornerAim] log): the pre-fix inputs next to the fixed ones. */
	struct FCornerAimTrace
	{
		int32 AllAimers = 0;
		float AllPressure = 0.f;
		float AllDamage = 0.f;
		bool bAnyOpenSideNear = false;
		float NearestOpenSideCm = 1.0e9f;
		float NearestGrenadeCm = 1.0e9f;
	};
	mutable FCornerAimTrace CornerAimTrace;
	FString CornerAimLabel;
	ECornerAimDecision LastCornerAimTraceDecision = ECornerAimDecision::StayAndFire;
	ECornerAimDecision LastCornerAimLegacyDecision = ECornerAimDecision::StayAndFire;
	FString LastCornerAimTraceReason;
	/** Sustained corner aim state (IsCornerAimActive). */
	bool bCornerAimActive = false;
	/** The cover slot a knockdown took him out of (bKnockedFromCover), for the return after a blast (2026-10-18). */
	FCoverSlot KnockdownCoverSlot;
	bool bKnockedFromCover = false;
	/** What knocked him down last (the knockdown state is reset by the recovery). */
	EKnockdownCause KnockdownCause = EKnockdownCause::None;
	EKnockdownCoverReturn LastCoverReturn = EKnockdownCoverReturn::NotInCover;
	/** Manual corner peek (OrderCornerPeek): ordered side, the CornerPeek event still due, the shimmy it orders keeps it. */
	bool bManualCornerPeek = false;
	ECoverFacing ManualCornerPeekSide = ECoverFacing::Right;
	bool bManualCornerPeekEventPending = false;
	bool bCornerPeekOrdering = false;
	int32 ManualCornerPeekCount = 0;
	double CornerAimLastTargetTime = -1.0e9;
	double CornerAimEndTime = -1.0e9;
	double CornerAimDuckUntil = -1.0e9;
	float CornerAimEvalTimer = 0.f;
	ECornerAimDecision LastCornerAimBreak = ECornerAimDecision::StayAndFire;
	int32 CornerAimBreakCounts[4] = { 0, 0, 0, 0 };
	/** Starts / refreshes the sustained aim on a corner lean shot (high cover). */
	void BeginCornerAim();
	/** Re-evaluates DecideCornerAim while leaned out (every 0.1 s). */
	void UpdateCornerAim(float DeltaTime);
	/** Rev 5h: while peeking round a corner, reveals the hidden traps in the peek sight cone (every PeekSpotConfig.IntervalSeconds). */
	void UpdatePeekTrapSpotting(float DeltaTime);
	float PeekSpotTimer = 0.f;
	/** How long the current peek has been searching (the trap search's reach sweeps out over PeekSpotConfig.SweepSeconds). */
	float PeekSpotElapsed = 0.f;
	int32 PeekSpottedCount = 0;
	/** The corner-aim inputs gathered from the world (targets, fire on him, laser, grenade, flank). */
	FCornerAimSituation GatherCornerAimSituation() const;
	/** Range and line of fire to a silhouette's aim point (barricades by SquadFireRules::JudgeLine). */
	bool EvaluateBlindLine(const class AEnemyGhostActor& Ghost, FShootCandidate& Out) const;
	/** Set while AutonomousMoveTo runs: the move does not re-pin the anchor. */
	bool bAutonomousOrder = false;
	TMap<ETargetedShotKind, TWeakObjectPtr<AActor>> PlannedShots;

	/** Quick fire (OperativeCharacterQuickFire.cpp): the running order, called from RunCombatShooting; returns its reason. */
	const TCHAR* RunQuickFire(float DeltaTime, AActor*& OutTarget);
	/** One area-fire round towards Aim (no aimed target): ammo, misfire, noise, the trace and what it crosses. */
	void FireQuickFireRound(const FVector& Aim);
	/** ShootAtTarget's hit: damage (crit, elevation, stance, cover, distance), telemetry, the weapon's self cold / warmth. */
	void ApplyRangedHit(AActor* Target, float Cover, int32 DistCells, bool bRagingShot);
	/** The order can be taken now (OrderQuickFire / PlanQuickFire); spawns the refusal floating text when bExplain. */
	bool CanTakeQuickFire(bool bExplain);
	bool bQuickFireActive = false;
	bool bQuickFirePlanned = false;
	FVector QuickFirePoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> QuickFireTarget;
	FVector PlannedQuickFirePoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PlannedQuickFireTarget;
	int32 QuickFireRoundsLeft = 0;
	/** World time of the order / of its last round (the timeout counts from it). */
	double QuickFireLastProgressTime = 0.0;
	int32 QuickFireRoundsFired = 0;
	float LastQuickFireAimErrorDeg = 0.f;

	/** Placeholder body; hidden automatically once the Blueprint assigns a skeletal mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> FacingMarker;

	/** Skeletal mesh Z offset set by the Blueprint for the standing capsule. */
	float MeshBaseZ = 0.f;
	/** Rev 2026-10-19: the Blueprint's mesh XY offset and yaw (the body's place on the capsule; EnforceMeshBaseOffset). */
	FVector2D MeshBaseXY = FVector2D::ZeroVector;
	float MeshBaseYaw = 0.f;
	bool bMeshBaseCaptured = false;
	/**
	 * Rev 2026-10-19 (user bug: the body ~1 m beside its selection ring, also while walking): the mesh's XY / yaw on the capsule may
	 * differ from the Blueprint's only by the tracked cover offsets (run-in blend, wall push, entry yaw); anything else is a leftover
	 * and is put back here every frame (logged).
	 */
	void EnforceMeshBaseOffset();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	EOperativeStance Stance = EOperativeStance::Standing;
	float ColdSpeedMultiplier = 1.f;
	bool bSprinting = false;
	bool bHasMoveOrder = false;
	/** The running move is an order of his own (OrderMoveTo), not a formation move (HasOwnMoveOrder). */
	bool bOwnMoveOrder = false;
	/** His own order's path following goes to a bypass waypoint first (SteerOwnMoveVia); its end resumes the order. */
	bool bSteeringOwnMove = false;
	/** Counts his stance changes; SquadStanceSerial = the count when the squad automation set it (IsStanceFromSquad). */
	uint32 StanceSerial = 0;
	uint32 SquadStanceSerial = MAX_uint32;
	/** The last RequestMove destination (the walk resumes there after jumping off an obstacle top). */
	FVector LastMoveDestination = FVector::ZeroVector;
	uint32 MoveOrderSerial = 0;
	double LastAttackedTime = -1.0e9;

	/** A move waiting for the stand-up clip (OrderMoveTo from prone / during a stance clip). */
	FTimerHandle PendingMoveTimer;
	bool bPendingMoveReplay = false;

	/** World time (s) until which the stance change armed by SetStance holds him still (before the anim instance starts the clip). */
	float StanceSettleUntil = 0.f;

	/** Rev 5e item 6 / rev 5f: the prone dive on the move with root motion (DropProne / UpdateProneDive). */
	EProneDive ProneDive = EProneDive::None;
	double ProneDiveStartTime = 0.0;
	float ProneDiveLength = 0.f;
	TWeakObjectPtr<UAnimSequenceBase> ProneDiveClip;
	/** Where the dive started, the move order it interrupted (goal) and the movement settings it changes for its length. */
	FVector ProneDiveStart = FVector::ZeroVector;
	FVector ProneDiveGoal = FVector::ZeroVector;
	bool bProneDiveHadOrder = false;
	float ProneDiveSavedRootMotionScale = 1.f;
	bool bProneDiveSavedWalkOffLedges = true;
	/** Ends the dive when its clip is over (or he is knocked down / in cover / stood up). */
	void UpdateProneDive(float DeltaSeconds);
	/** Ends the dive: root motion scale and ledges restored, the crawl speed; crawls on to a goal still ahead, else stays. */
	void EndProneDive();
	/** Root bone translation of the dive clip at Time (component space), cm. */
	FVector GetProneDiveRoot(float Time) const;

	/** Seconds until a stance clip that starts now / is playing lets the operative walk (0 without a clip). */
	float GetStanceChangeDelay(EOperativeStance From, EOperativeStance To) const;

	/** Sets WeaponMesh to the equipped weapon's HandMesh (or back to the Blueprint's model). */
	void ApplyWeaponVisual();
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DefaultWeaponMeshAsset;
	FTransform DefaultWeaponMeshTransform = FTransform::Identity;
	bool bDefaultWeaponMeshCaptured = false;
	/** Sniper: an object shot (Ctrl + click) waiting for the kneel, retried every frame until it fires or times out. */
	void UpdateSniperPendingShot(float DeltaTime);
	TWeakObjectPtr<AActor> SniperPendingObject;
	float SniperPendingTimer = 0.f;
	double SniperHoldUntil = -1.0;
	/** When she last knelt for a shot (the shot waits SniperKneelSettleSeconds after it). */
	double SniperKneelTime = -100.0;
	/** The last kneel / stop feed line (one per order, not per frame). */
	double SniperLastPrepLineTime = -100.0;
	int32 SniperKneels = 0;
	int32 SniperStops = 0;
	int32 SniperShots = 0;
	int32 SniperRefusedShots = 0;
	/** The sniper rule allows a shot that leaves the barrel now (refuses + counts otherwise). */
	bool SniperShotAllowed();
};
