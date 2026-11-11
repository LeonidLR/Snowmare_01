#pragma once

#include "CoreMinimal.h"
#include "Combat/AIGrenadeRules.h"
#include "Combat/SquadFireRules.h"
#include "GameFramework/Character.h"
#include "Characters/CorpseBody.h"
#include "Characters/CriticalWoundRules.h"
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
#include "Combat/LauncherRules.h"
#include "Combat/LauncherTacticsRules.h"
#include "Combat/SniperRules.h"
#include "Combat/PeekSightRules.h"
#include "Combat/SuppressionRules.h"
#include "OperativeCharacter.generated.h"

class UMaterialInterface;
enum class EOperativeHitKind : uint8; // Characters/OperativeHitRules.h

/** One area-fire round (AOperativeCharacter::FireAreaRound): quick fire or the SUPPRESS order (2026-10-26). */
struct CODEXTACTICS_API FOperativeAreaRound
{
	/** Spray cone half-angle, degrees (x the suppression spread when he is suppressed). */
	float SpreadDeg = 1.5f;
	/** An enemy the round crosses is hit by the weapon's chance x this. */
	float HitMultiplier = 0.5f;
	/** Shoot timer after the round, s. */
	float NextRoundDelay = 0.65f;
	/** Fired on full-auto (the round's suppression class, the full-auto counter). */
	bool bFullAuto = false;
	/** Log prefix ("QuickFire" / "Suppress"). */
	const TCHAR* LogTag = TEXT("QuickFire");
};

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
	/** A return round at an under-fire muzzle flash (bug #43, UnderFireRules; blind, AimPoint = the flash). */
	bool bReturnFire = false;
	/**
	 * Aimed at the part of a peeking man that shows (fix/corner-peek-standoff 2026-11-09, SquadFireRules::ChooseShotAimPoint): his body is
	 * behind his corner, the line to his leaned-out head and shoulder (AOperativeCharacter::GetPeekExposedPoint) is clear; AimPoint = that point.
	 * A normal aimed shot (not blind); the lean multiplier (x0.5) is the target's GetIncomingHitChanceMultiplier.
	 */
	bool bExposedAim = false;
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
class CODEXTACTICS_API AOperativeCharacter : public ACharacter, public ICorpseBody
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

	/**
	 * Formation move issued by the squad: path to Destination at a fixed speed, keeps stance and sprint state. PathBrakingCm >= 0: the path's
	 * braking distance for this move (the movement config's otherwise; item 2 2026-11-03: a wall order's places, SquadWallTuning).
	 */
	EOperativeOrderResult FollowTo(const FVector& Destination, float Speed, float PathBrakingCm = -1.f);

	/**
	 * Item 2 (2026-11-03): one tick of the squad's steered final approach into a wall order's place - a path move in force is handed over (no
	 * stop), then he walks along Direction at Speed (movement input, his walk animation). Call every tick while it lasts.
	 */
	void SteerFormationMove(const FVector2D& Direction, float Speed);

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
	/**
	 * The autonomy crouched him on a halt in a fight (the hold crouch, decision 6 of 2026-11-01 night): a squad stance that his next move
	 * (a formation follow or his own order) stands him up from at once (SquadAutonomyRules::ShouldRiseFromHoldCrouch).
	 */
	void MarkHoldCrouch() { SquadStanceSerial = StanceSerial; HoldCrouchSerial = StanceSerial; }
	bool IsHoldCrouch() const { return HoldCrouchSerial == StanceSerial && Stance == EOperativeStance::Crouching; }
	/**
	 * The player's stance key / squad stance order (even one that changes nothing): his stance is his own - no longer the squad's, and the
	 * player's (IsStanceFromPlayer) until anything changes it again.
	 */
	void ClearStanceFromSquad() { SquadStanceSerial = MAX_uint32; PlayerStanceSerial = StanceSerial; }
	/**
	 * His current stance is the player's stance order (ClearStanceFromSquad after V / C / Z, Alt + V / C / Z, the action bar cycle). Solo's guard
	 * crouch on a leader switch leaves it alone (bug 2026-11-08: the previously ordered operative got up from prone into a crouch when the
	 * player selected the next one; SquadAutonomyRules::KeepsOwnStanceInSolo). Any later stance change ends it.
	 */
	bool IsStanceFromPlayer() const { return PlayerStanceSerial == StanceSerial; }

	/**
	 * Seconds he must stand still while a stance change plays (OperativeMovementRules::ComputeMoveHold): the settle time armed by
	 * SetStance plus the stance clip already playing. Moves issued meanwhile wait (OrderMoveTo queues, FollowTo refuses).
	 */
	float GetMoveHoldSeconds() const;

	/** Stops any movement immediately. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Operative")
	void StopOperative();

	/** Stops, drops the sprint and turns to face Location (public since 2026-11-04: the order catalog's Hold faces a remembered threat). */
	void StopAndFace(const FVector& Location);

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

	/** Movement-updated hook: re-seats the selection ring after the CMC moved the capsule (the actor tick runs before the CMC tick). */
	UFUNCTION()
	void HandleCharacterMovementUpdatedForRing(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);

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

	/**
	 * The critical wound (the game designer 2026-11-10, CriticalWoundRules; critical_wound.json "critical_from" 0.2): HP under that share - he
	 * goes down by himself and only crawls (the injured crawl), no stand / crouch / sprint / ladder; he still fires prone. Healed back to the
	 * threshold he may stand again. The squad always; a hostile PMC fighter only with "apply_to_hostiles".
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsCriticallyWounded() const;

	/**
	 * The wound tier (the game designer 2026-11-10, feat/injured-tiers-fullbody; CriticalWoundRules::ChooseWoundTier): Light = IsWounded down to
	 * critical_wound.json "heavy_from" (35 %), Heavy = under it (the slow injured walk, no run, standing speed x "heavy_speed_multiplier" 0.45),
	 * Critical = IsCriticallyWounded. Heavy / critical only where the critical rule applies (the squad; hostiles with "apply_to_hostiles").
	 */
	CriticalWoundRules::EWoundTier GetWoundTier() const;

	/** The heavy tier's extra speed factor while standing (CriticalWoundRules::TierSpeedScale; 1 otherwise). */
	float GetWoundTierSpeedScale() const;

	/**
	 * The wounded full-body layer holds this shot while the torso rises from the injured clip to the weapon pose (InjuredLocomotionRules,
	 * "raise_to_fire_s" in weapon_anim_sets.json "injured"): asks the animation for the raise (every fire attempt with a target keeps it up)
	 * and returns true while the shot must wait. False when the layer does not hold the torso (healthy, cover, crouched, no animation tick).
	 */
	bool HoldShotForInjuredRaise();
	/** Fire attempts the raise held (smokes) and the world time of the last one (-1 = none). */
	int32 InjuredRaiseHolds = 0;
	double LastInjuredRaiseHoldTime = -1.0;

	/** The critical wound holds him down right now (UpdateCriticalWound's state: set when he went down, cleared when healed). */
	bool IsHeldDownByCriticalWound() const { return bCriticalWoundDown; }

	/** Posts "<name> is critically wounded - heal him" (a refused stand / crouch / run order) when he is critical; false otherwise. */
	bool NotifyCriticalWoundRefusal() const;

	/** Times he went down by the critical wound (smokes). */
	int32 GetCriticalWoundDowns() const { return CriticalWoundDowns; }

	/** Max ground speed for the current stance/sprint/wound/carry state, cm/s. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	float GetMaxSpeed() const;

	/**
	 * His allowed walk now, cm/s: GetMaxSpeed without the sprint (stance, wound tier, cold, carrying) - what the formation's wound pace
	 * (SquadWoundPaceRules) compares.
	 */
	float GetAllowedWalkSpeed() const;

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

	/**
	 * Godot get_silhouette_color: leader cyan, engineer amber, medic-sapper green, recruit olive (alpha 0.55) - the player's squad. A hostile
	 * operative (a PMC fighter, !IsPlayerTeam()) is HostileSilhouetteColor (the game designer's decision 2026-11-08).
	 */
	FLinearColor GetSilhouetteColor() const;

	/** The see-through silhouette of a hostile operative (TeamId other than the player's): red (the game designer 2026-11-08). */
	static inline const FLinearColor HostileSilhouetteColor = FLinearColor(1.f, 0.04f, 0.04f, 0.6f);

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
	 * Team (user request 2026-11-03, docs/systems/pmc-squad-ai.md): 0 = the player's squad; 1.. = a hostile PMC squad - the same body
	 * and skills on the other side. A hostile operative never joins USquadSubsystem (no selection / HUD / formation / saves / campaign
	 * carry); he registers in UPmcSquadSubsystem (its team sight, targets and commander) and carries the "Enemy" tag for the player's
	 * men. Set before BeginPlay (ACodexTacticsGameMode::SpawnOperative / UPmcSquadSubsystem::SpawnFighter).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Operative")
	int32 TeamId = 0;

	/** The player's side (TeamId 0). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Operative")
	bool IsPlayerTeam() const { return TeamId == 0; }

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

	/**
	 * A skeletal weapon model (UWeaponDataAsset::HandSkeletalMesh: the D-1 shotgun, 2026-11-08), a child of WeaponMesh so every
	 * show / hide of the weapon carries it; WeaponMesh holds no static model meanwhile and takes the model's attach transform.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Operative")
	TObjectPtr<USkeletalMeshComponent> WeaponSkeletalMesh;

	/** The hand model's extra static parts (UWeaponDataAsset::HandMeshParts: the pistol's magazine), children of WeaponMesh at identity. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WeaponMeshParts;

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

	/** The climb ended at the other end: the path goes on to Destination (like the vault's resume; a drop's landing too). */
	void ResumeMoveAfterLadder(const FVector& Destination, bool bOwnOrder = false);

	// --- Falls and drops (docs/systems/stances-and-movement.md "Falls and drops"; cluster OperativeCharacterFall.cpp) ---

	/** Running drops through the generated drop links, knockback falls over an edge, the landing's fall damage (2026-11-03). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Falls")
	TObjectPtr<class ULedgeFallComponent> LedgeFall;

	/** Running off an edge, in the air or landing (no fire, cover, facing or stance change meanwhile). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Falls")
	bool IsDroppingOffLedge() const;

	/** His sprint speed now (standing; wounds, the cold, a load): the fastest run-up he can take off an edge, cm/s. */
	float GetSprintSpeedCap() const;

	/**
	 * His running path takes a plain nav link that drops a level (the level generator's hole links - not a ladder / generated drop link, those
	 * have their components) and he is at it or already over it (FallRules::IsCommittedToDrop): the formation leaves his path alone until he
	 * has dropped (the soft-lock of 2026-11-03: re-pathed from the rim's navmesh-free band, the followers stopped ON the rim of a roof hole).
	 */
	bool IsCommittedToNavLinkDrop() const;

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

	/**
	 * Cone range = his SIGHT range (user request 2026-11-04: "the cone must show how far he sees, not the weapon"): UTacticalSightSubsystem::
	 * GetSightRangeCm - the clear-day 120 m x his height bonus x the weather / time of day, what CanOperativeSee uses. Off (or no sight
	 * subsystem): the older rules below (bVisionConeRangeFromWeapon / VisionConeRangeCm). A peeking man's cone stays the peek cone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Vision Cone")
	bool bVisionConeShowsSightRange = true;

	/** Without bVisionConeShowsSightRange: cone range = the current weapon's real-time range x the stance multiplier (what it can hit); off: VisionConeRangeCm. */
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
	/**
	 * Bug 2026-11-08 (the game designer's screenshot: a killed PMC fighter lying on the ground kept the orange see-through silhouette):
	 * takes every overlay material (the silhouette, a hit flash / target highlight, a defend / suppress marker) off all mesh components of
	 * him and his attached actors - shown or hidden. BecomeCorpse calls it, and the corpse's tick keeps it so (nothing puts one back).
	 * Returns the number of overlays removed.
	 */
	int32 ClearBodyOverlays(const TCHAR* Why);

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
	 * The designer's roster (2026-11-08, WeaponRoster): removes every out-of-play weapon from the arsenal, the ammo inventory, the loose
	 * ammo and the bonus items; one in hands is replaced by the M16 (else the first weapon). InitArsenal, a save load and the campaign
	 * carry call it.
	 */
	void DropOutOfPlayWeapons();

	/** The primary weapon: the first non-grenade weapon of the arsenal (the loadout's first; the Engineer's D-1). Empty: none. */
	FString GetPrimaryWeaponId() const;

	/** Takes the primary weapon in hands; false when there is none / it cannot be taken. */
	bool TakePrimaryWeapon();

	/** After a save / the campaign carry: the saved weapon if he owns it, else his primary, else his first (WeaponLoadoutRules). */
	void EquipRestoredWeapon(const FString& SavedWeaponId);

	/**
	 * The sidearm switch (SidearmRules, 2026-11-08): the primary ran dry with an enemy within the threat range -> the pistol now (the reload is
	 * dropped, the draw delays the first shot). True when he switched.
	 */
	bool TryDrawSidearm();
	/** The sidearm drawn by the rule goes back to the primary (which then reloads) after the holster time without a target / when dry. */
	void UpdateSidearmReturn(float DeltaTime);
	/** He holds the sidearm the rule drew (not a weapon the player chose). */
	bool IsSidearmDrawnByRule() const { return bSidearmDrawnByRule; }
	/** Sidearm draws by the rule since spawn (for the smokes). */
	int32 GetSidearmDraws() const { return SidearmDraws; }

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
	 * A body killed off the ground falls first (fix/corpse-falls-to-ground, game designer PIE 2026-11-08: a PMC fighter killed on a ladder hung
	 * in the air in his death pose): BecomeCorpse leaves him to gravity when FallRules::ShouldCorpseFall (on a ladder - its own drop -, mid-vault,
	 * the floor > 30 cm below his feet) and stops him once he lands (UpdateCorpseFall); the death pose waits for the landing
	 * (UOperativeAnimInstance). True while he falls. (ICorpseBody: UCorpseSubsystem never freezes a hostile PMC body in the air.)
	 */
	virtual bool IsCorpseFalling() const override { return bCorpseFalling; }

	/**
	 * ICorpseBody (game designer 2026-11-09: "every enemy body stays, ALL enemies, one cap"): a HOSTILE PMC fighter's body goes under the
	 * level's corpse cap with the monsters (UCorpseSubsystem, registered in HandleDied); the player's own squad's KIA never do (they stay for
	 * good, they hold the remains). The death clip / held fall holds its last frame (UOperativeAnimInstance::IsDeathPoseSettled).
	 */
	virtual bool IsDeathPoseSettled() const override;
	/** ICorpseBody: still climbing / dropped by a ladder. */
	virtual bool IsCorpseOnLadder() const override;
	/**
	 * ICorpseBody: a settled hostile PMC body costs nothing more - his AI controller destroyed, no actor / movement / component tick (anims
	 * paused on the death pose, the weapon still in his hands), no collision; still rendered where he fell. Never for the player's squad.
	 */
	virtual void FreezeCorpse() override;
	/** FreezeCorpse ran. */
	virtual bool IsCorpseFrozen() const override { return bCorpseFrozen; }
	/** ICorpseBody: the skeletal body (the C++ placeholder cylinder without one). */
	virtual UPrimitiveComponent* GetCorpseVisual() const override;
	/**
	 * ICorpseBody: a hostile PMC fighter leaves no remains crate (HandleDied: only the player's own KIA do), so his body never holds
	 * unsearched loot today; were he given one, CorpseRules keeps the body while it is in view.
	 */
	virtual bool HoldsUnsearchedLoot() const override { return false; }
	/** The rule evaluated now (the anim asks it in the death event, before or after BecomeCorpse ran). */
	bool ShouldCorpseFallNow() const;
	/** The last corpse fall's height, cm (smokes). */
	float GetLastCorpseFallCm() const { return LastCorpseFallCm; }

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
	void ClearPendingCover() { bHasPendingCover = false; bRunCornerPeekPending = false; }
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

	/**
	 * Run-and-peek (#55, user request 2026-11-07; RunCornerPeekRules): from the open - or from cover at another wall - run to CornerSlot (a
	 * cover slot short of a wall's end, on the face that faces him) and, entering that cover, go straight into the manual corner peek round
	 * the end at EdgeOnFace (OrderCornerPeek with the edge found again from the entered slot: CoverFacingRules::FindPeekEdge) - no separate
	 * "hug the wall first, then click the corner" step. The walk is his ordinary cover order (OrderTakeCover: any later order replaces it, as
	 * any order does); its entry is a corner-peek arrival (MarkCornerPeekArrival: no wall-facing enter clip when he comes in along the wall).
	 * Knocked down: buffered like any order. Returns OrderTakeCover's result.
	 */
	EOperativeOrderResult OrderRunCornerPeek(const FCoverSlot& CornerSlot, const FVector& EdgeOnFace, bool bSprint = false);

	/**
	 * Arms the run-and-peek on the cover order he already walks (a group order gave him the corner slot): his entry into that slot turns into
	 * the corner peek round the end at EdgeOnFace. False (nothing armed) without a pending cover order.
	 */
	bool ArmRunCornerPeek(const FVector& EdgeOnFace);

	/**
	 * OrderTakeCover(CornerSlot) whose entry is a run-and-peek arrival (armed BEFORE the walk starts, so also when he already stands within
	 * the slot's arrival range and the entry happens inside this call): the entry goes straight into the corner peek round EdgeOnFace.
	 * OrderRunCornerPeek and the squad's carried run-and-peek corner man (USquadSubsystem::OrderFormationCornerPeek bRunPeek) use it.
	 */
	EOperativeOrderResult OrderTakeCoverForPeek(const FCoverSlot& CornerSlot, const FVector& EdgeOnFace, bool bSprint = false);

	/** A run-and-peek is armed: his walk to the corner is running (OrderRunCornerPeek / ArmRunCornerPeek; smokes). */
	bool IsRunCornerPeekPending() const { return bRunCornerPeekPending; }

	/** Run-and-peek arrivals whose corner peek order was accepted (smokes / stats). */
	int32 GetRunCornerPeekCount() const { return RunCornerPeekCount; }

	/**
	 * Run-and-peek arrivals that went straight into the peek stance (IsCoverEntryPeekStance: no wall hug, no turn out; the game designer's
	 * video 2026-11-08) - smokes / stats.
	 */
	int32 GetPeekStanceEntryCount() const { return PeekStanceEntryCount; }

	/** The last peek stance arrival: his facing as he arrived minus the peek facing (the corner stance's aim), deg (smokes). */
	float GetLastPeekStanceArrivalOffDeg() const { return LastPeekStanceArrivalOffDeg; }

	/**
	 * The run-and-peek arrival goes straight into the peek stance (Codex.Cover.RunPeekStance, default 1; 0 = the 2026-11-07 entry: the enter
	 * clip's wall hug, then the step-out round the corner - A/B).
	 */
	static bool IsRunPeekStanceEnabled();

	/**
	 * The run-and-peek arrives already turned (Codex.Cover.RunPeekYawLock, default 1; the game designer's video Bug_2 2026-11-08 - "another
	 * small extra turn" on the peek entry): the run's last metres turn his DRAWN body to the fire idle's drawn facing at a planned even rate,
	 * started sooner (RunCornerPeekRules::PreTurnStartCm / PreTurnStepYaw / BodyTargetActorYaw), and the anim holds the drawn body's yaw
	 * through the blend exactly (UOperativeAnimInstance::BeginPeekStanceYawLock). 0 = the first fix's arrival (the actor turned to the corner
	 * aim in the last 1.5 m, the mesh's held yaw released by the blend weight - A/B).
	 */
	static bool IsRunPeekYawLockEnabled();

	/**
	 * The last peek stance arrival with the yaw lock: the fire idle's drawn body yaw minus the run's last drawn body yaw, deg (the turn the
	 * lock still had to ease out; ~0 when the pre-turn did its job) - smokes.
	 */
	float GetLastPeekStanceBodyGapDeg() const { return LastPeekStanceBodyGapDeg; }

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

	/**
	 * How far his peek view reaches, cm (#32, 2026-11-07): his effective sight range (UTacticalSightSubsystem::GetPeekSightRangeCm - the same
	 * clear-day range x height level x weather / time of day as his own eyes) x PeekSightConfig.SightRangeFraction; the fixed
	 * PeekSightConfig.RangeCm without a sight subsystem or with bRangeFromSight off.
	 */
	float GetPeekSightRangeCm() const;

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

	/**
	 * The squad corner peek's arrival along the wall (IsCoverEntryEasedTurn, 2026-11-01): the body's turn from his walk eases out over this
	 * long - about the corner step-out clip (cvr_std_idle_*_to_fire, ~1 s), so the walk's yaw hands over to the step-out's own turn round
	 * the corner in one movement (0.3 s let the body swing out to the wall normal first and back round the corner: a 50 deg wobble), s.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0.05"))
	float CornerPeekEntryYawBlendSeconds = 0.9f;

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

	/**
	 * This cover entry turns the body round without the pack's enter clip (CoverFacingRules::DecideCoverEntryTurn -> EasedTurn while an
	 * enter clip is assigned): the old yaw is eased out on the mesh and the AnimInstance goes straight to the cover loops / the corner
	 * step-out (the squad corner peek bug 2026-11-01).
	 */
	bool IsCoverEntryEasedTurn() const { return bCoverEntryEasedTurn; }

	/**
	 * This cover entry is a run-and-peek arrival at its corner that goes straight into the corner peek stance (CoverFacingRules::
	 * DecideCoverEntryTurn -> PeekStance; the game designer's video 2026-11-08 "08.11.26_InstantLookUp_Bug_1": he ran up, hugged the wall,
	 * then turned and peeked - "like a stage play"): the AnimInstance plays no enter clip, no back-to-the-wall idle and no idle -> fire
	 * step-out - the run blends into the fire idle round the corner; the corner aim is on in the entry frame; his leftover turn from the run to
	 * the peek facing eases out on the mesh. He IS in cover at that corner (leaving the peek returns him to the normal corner pose).
	 */
	bool IsCoverEntryPeekStance() const { return bCoverEntryPeekStance; }

	/**
	 * The squad's corner peek (USquadSubsystem::OrderFormationCornerPeek): the pending cover order is the peeker's walk to his corner along the
	 * wall - its entry turns straight into the peek (no wall-facing enter clip when he arrives along the wall). Cleared by the next cover order
	 * (OrderTakeCover / SetPendingCover), consumed by the entry. Call right after the accepted OrderTakeCover.
	 */
	void MarkCornerPeekArrival() { bCornerPeekArrivalPending = bHasPendingCover; }

	/** The pending cover order is a corner peek arrival (MarkCornerPeekArrival; smokes). */
	bool IsCornerPeekArrivalPending() const { return bCornerPeekArrivalPending; }

	/** Cover entries that turned in without the enter clip because they were a corner peek arrival along the wall (smokes / stats). */
	int32 GetCornerPeekArrivalEntryCount() const { return CornerPeekArrivalEntryCount; }

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

	/**
	 * Angle between where the pose aims now (after the twist) and the target, degrees. Prone (no twist) the larger of that and the
	 * body's own error (bug #33, FireArcRules::ShotResidualDeg).
	 */
	float GetShotAimResidualDeg(const FVector& TargetLocation) const;
	/** The barrel's direction in the hand model's own space (MuzzleOffset; a launcher: its weapon's muzzle_offset, the tube's bore). */
	FVector GetBarrelAxisOnModel() const;

	/** One frame of the aim turn to AimAt (FireArcRules::StepAimYaw: eased / AimTurnRateDegPerSec, prone at the prone turn rate). */
	void TurnAimTowards(const FVector& AimAt, float DeltaTime);

	/**
	 * Under fire (bug #43, UnderFireRules): a hostile Shooter fired a round at him or at a mate near him from FlashPoint (his muzzle).
	 * The flash is his threat for FUnderFireConfig::MemorySeconds: he turns to it and, with no line to the shooter's body, returns
	 * fire at it.
	 */
	void NoteUnderFire(AOperativeCharacter* Shooter, const FVector& FlashPoint);

	/** The live under-fire threat: the shooter and his flash point; false when none / forgotten. */
	bool GetUnderFireThreat(AOperativeCharacter*& OutShooter, FVector& OutFlashPoint) const;

	/** Turned to an under-fire flash point this frame (no combat target in his sights). */
	bool IsFacingUnderFire() const { return bFacingUnderFire; }

	/** Rounds he returned at an under-fire flash point (smokes / bot report). */
	int32 ReturnFireShots = 0;

	/**
	 * The manoeuvre under fire (#43 rest, 2026-11-07, UnderFireRules): under fire, not in cover, no order of his own - the nearest cover
	 * slot shielding him from the flash, else a short step off the line for a clear line to the flash (a rear man of a column, a mate in
	 * the way), else prone. The squad's men and the hostile PMC fighters alike. Throttled (DecisionSeconds); called from Tick.
	 */
	void UpdateUnderFireManoeuvre(float DeltaTime);

	/**
	 * He holds the spot his manoeuvre took him to (UnderFireRules::IsHoldingManoeuvreSpot): USquadSubsystem::IsOnOwnOrder counts it, so the
	 * formation does not pull him back into the line of fire until the threat is over or the leader gets a new order.
	 */
	bool IsHoldingUnderFireSpot() const;

	/** His latest move order is his own manoeuvre under fire (not a player's / commander's order): the formation does not walk after it. */
	bool IsOnUnderFireManoeuvreMove() const { return bUnderFireManoeuvred && bUnderFireIssuedMove && GetMoveOrderSerial() == UnderFireIssuedSerial; }

	/** What the manoeuvre under fire did so far (smokes / bot report). */
	int32 UnderFireCoverMoves = 0;
	int32 UnderFireSidesteps = 0;
	int32 UnderFireProneDrops = 0;

	/**
	 * The prone crawl under fire (the game designer's CrawlAndShoot_Bug_01, 2026-11-09, ProneCrawlRules): prone, out of cover, moving on an
	 * order (his own, or a formation move he really crawls on). He crawls first: no round / grenade / new reload until he stops; his body faces
	 * the danger (the target, the flash, the threat he fought) while the directional crawl clips move him the ordered way.
	 */
	bool IsOrderedCrawl() const;
	/** The fire hold is on now (IsOrderedCrawl, Codex.Combat.ProneCrawlHoldFire, not raging). */
	bool IsCrawlFireHeld() const;
	/** Frames the crawl held his fire with something to fire at (smokes). */
	int32 CrawlFireHolds = 0;
	/** What his crawling body faced this frame (ProneCrawlRules::ECrawlFacing as uint8; 0 = the movement / not crawling - smokes, logs). */
	uint8 GetCrawlFacingKind() const { return CrawlFacingKind; }

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
	 * Leaving the cover from the corner fire pose (fix/corner-exit-jerk): the fire idle's body stands ~55 cm off the capsule (leaned out round the
	 * corner) and the actor's turn swung it round the capsule 17 cm a frame; for CornerLeaveTurnSeconds the turn keeps that body under this
	 * speed (cm/s) while it stands more than 20 cm off the capsule.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "10"))
	float CornerLeaveBodySpeed = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cover", meta = (ClampMin = "0"))
	float CornerLeaveTurnSeconds = 1.2f;

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
	void AssignPriorityTarget(AActor* Enemy) { ManualPriorityTarget = Enemy; CancelSuppressFire(TEXT("a new attack order")); }

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

	/**
	 * Dev diagnostics (fix/corner-peek-standoff 2026-11-09, smokes / Codex.Debug.FireTrace): the real-time fire's shot-line verdict at Target
	 * as one line - the reason (EvaluateShotLine), the traced line's origin and end point and what stopped it.
	 */
	FString DescribeShotLine(AActor* Target) const;

	/**
	 * Honest sight (fix/pmc-fire-through-wall 2026-11-08): his side sees Target now - the squad's sight for the player's men
	 * (UTacticalSightSubsystem::IsVisibleToSquad), his team's for a hostile fighter (UPmcSquadSubsystem::IsVisibleToTeam). True without them.
	 */
	bool IsSeenByOwnSide(const AActor* Target) const;

	/**
	 * Fire at a point is worth a round (fix/pmc-fire-through-wall 2026-11-08, one rule for the squad and the PMC): the line from his muzzle at
	 * his stance now (prone 25 cm; in cover the cover's fire origin) to Point, the world only (bodies never stop it), passes
	 * SuppressionRules::IsFireLineUseful - clear, or blocked only near the point (the target's own cover). OutBlockedAtCm: where it is blocked
	 * (-1 clear). Console Codex.Combat.HonestFire 0 = always true (the old rule, A/B).
	 */
	bool HasUsefulFireLine(const FVector& Point, float* OutBlockedAtCm = nullptr) const;
	/** The same from a given muzzle (the commander asks before he picks a suppress order). */
	bool HasUsefulFireLineFrom(const FVector& Muzzle, const FVector& Point, float* OutBlockedAtCm = nullptr) const;
	/** Codex.Combat.HonestFire (default 1): honest sight for the PMC teams, the fire-line rule for fire at a point. */
	static bool IsHonestFireOn();
	/** Rounds at a point not fired because no line of fire reached it (smokes). */
	int32 GetNoLineOfFireSkips() const { return NoLineOfFireSkips; }

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

	/** Drops the planned targeted shots, the planned quick fire and the planned SUPPRESS order (pause plans dropped / restarted). */
	void ClearPlannedTargetedShots() { PlannedShots.Reset(); bQuickFirePlanned = false; bSuppressPlanned = false; }

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

	// --- Suppressive fire (game designer decisions 2026-10-26; SuppressionRules, OperativeCharacterSuppression.cpp; UE-only) ---

	/**
	 * SUPPRESS order: sustained bursts at Target (a live enemy; his cover takes the rounds when he hides) or at Point (a spot), full-auto
	 * with an assault rifle (UWeaponDataAsset::FullAutoInterval; any other ammo weapon at its own cadence), spending ammo fast, reloading
	 * as needed, until magazine + reserve run low, the target dies or the order is cancelled (a move / cover order, the 7 key again,
	 * turn-based combat). Every round suppresses what it passes / strikes (USuppressionSubsystem). An explicit order: any fire posture.
	 * False when he cannot take it (as quick fire: dead, knocked down, aboard, raging, panicking, the knife, a frozen weapon, hands busy)
	 * or the ammo is already low, or Codex.Combat.Suppression is 0.
	 */
	bool OrderSuppress(AActor* Target, const FVector& Point);
	/** Tactical pause: remembers the order; ExecutePlannedSuppress (pause release) gives it. */
	bool PlanSuppress(AActor* Target, const FVector& Point);
	bool ExecutePlannedSuppress();
	/** Ends a running SUPPRESS order (Why: the log line); a planned one goes with ClearPlannedTargetedShots. */
	void CancelSuppressFire(const TCHAR* Why = TEXT("cancelled"));
	/** Drops a planned SUPPRESS order (the 7 key ceasing it in the tactical pause). */
	void ClearPlannedSuppress() { bSuppressPlanned = false; PlannedSuppressTarget.Reset(); }
	bool HasSuppressOrder() const { return bSuppressActive; }
	bool HasPlannedSuppress() const { return bSuppressPlanned; }
	AActor* GetSuppressTarget() const { return SuppressTarget.Get(); }
	FVector GetSuppressPoint() const { return SuppressPoint; }
	AActor* GetPlannedSuppressTarget() const { return PlannedSuppressTarget.Get(); }
	FVector GetPlannedSuppressPoint() const { return PlannedSuppressPoint; }
	/** Rounds fired by SUPPRESS orders since spawn / on full-auto (smokes). */
	int32 GetSuppressRoundsFired() const { return SuppressRoundsFired; }
	int32 GetFullAutoRoundsFired() const { return FullAutoRoundsFired; }
	/** Why the last SUPPRESS order ended. */
	ESuppressOrderStop GetLastSuppressStop() const { return LastSuppressStop; }

	/** His suppression tier now (USuppressionSubsystem; None in turn-based combat / with the CVar off). */
	ESuppressionTier GetSuppressionTier() const;
	/** Shots fired blind because he was suppressed / frames his fire was held because he was pinned / shots that left while pinned (0). */
	int32 GetSuppressedBlindShots() const { return SuppressedBlindShots; }
	int32 GetPinnedFireHolds() const { return PinnedFireHolds; }
	int32 GetShotsWhilePinned() const { return ShotsWhilePinned; }
	/** Hit chance of his last aimed shot (ShootAtTarget, after cold / blind / suppression; smokes). */
	float GetLastShotHitChance() const { return LastShotHitChance; }
	int32 GetLastShotPelletHits() const { return LastShotPelletHits; }
	/**
	 * Every ranged shooter's hit chance at him is multiplied by this (user decision 2026-11-07): 0.5 while he leans out of cover
	 * and Shooter is on the wall's far side, else 1 (CoverRules::LeanIncomingHitMultiplier). Operatives / PMC, the marksman and
	 * the acid spitter read it; melee and blasts do not.
	 */
	float GetIncomingHitChanceMultiplier(const AActor* Shooter) const;
	/**
	 * Bug #39: the hit chance multiplier of his weapon at PlanarDistanceCm from him (WeaponRangeRules: x1 up to the effective range - stretched
	 * by his stance's and, against Target below him, the elevation's range bonus - falling to the max reach, 0 beyond it).
	 */
	float GetRangeHitMultiplierAt(const AActor* Target, float PlanarDistanceCm) const;

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

	/**
	 * He has a facing order of his own (SetFacingPoint), a quick fire or a SUPPRESS order: a look decided elsewhere (the coverage
	 * allocator's fixed look).
	 */
	bool HasOwnFacing() const { return bExplicitFacing || bQuickFireActive || bSuppressActive; }

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

	/**
	 * Rounds fired since the level began - counted also when no round leaves the clip (the combat lab's infinite-ammo cheat, the rage's
	 * free rounds): the bot run report's "would-be spent" ammo (docs/design/COMBAT_LAB.md section 7).
	 */
	int32 GetRoundsSpent() const { return RoundsSpent; }

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
	/** FreezeCorpse ran (a hostile PMC body under the corpse cap, 2026-11-09). */
	bool bCorpseFrozen = false;
	/** IsCorpseFalling: since BecomeCorpse, until he lands. */
	bool bCorpseFalling = false;
	float CorpseFallSeconds = 0.f;
	float CorpseFallStartFeetZ = 0.f;
	float LastCorpseFallCm = 0.f;
	/** Leaves the falling corpse to gravity until it lands, then stops it (Tick). */
	void UpdateCorpseFall(float DeltaTime);
	/** The floor straight below his feet (this body and every pawn ignored): found, and the gap feet -> floor, cm. */
	bool TraceCorpseFloor(float& OutGapCm, float& OutFloorZ) const;
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
	/** GetRoundsSpent. */
	int32 RoundsSpent = 0;
	/** The sidearm switch (TryDrawSidearm / UpdateSidearmReturn). */
	bool bSidearmDrawnByRule = false;
	float SidearmNoTargetSeconds = 0.f;
	int32 SidearmDraws = 0;
	/**
	 * Range (weapon x stance x elevation), dead zone and line of fire to Enemy (Godot _find_shoot_target ray: walls block,
	 * barricades by SquadFireRules::JudgeLine, another enemy in the way becomes the target unless bKeepTarget).
	 */
	bool EvaluateShotLine(AActor* Enemy, bool bKeepTarget, FShootCandidate& Out, bool& bOutBarricadeBlocked,
		const TCHAR** OutReason = nullptr, FString* OutDetail = nullptr) const;
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
	/** GetShotAimResidualDeg before the prone body arc (the pose / barrel residual). */
	float GetPoseAimResidualDeg(const FVector& TargetLocation) const;
	/** Under fire (NoteUnderFire): the shooter, his flash point and when the last round came in. */
	TWeakObjectPtr<AOperativeCharacter> UnderFireShooter;
	FVector UnderFirePoint = FVector::ZeroVector;
	double UnderFireTime = -1.0e9;
	bool bFacingUnderFire = false;
	/** ProneCrawlRules::ECrawlFacing of this frame's crawl facing (UpdateCombatFacing). */
	uint8 CrawlFacingKind = 0;
	// The manoeuvre under fire (#43, UpdateUnderFireManoeuvre).
	/** The first round of the current spell under fire (-1e9: none running). */
	double UnderFireSpellStart = -1.0e9;
	float UnderFireDecisionTimer = 0.f;
	double UnderFireNextCoverSearch = -1.0e9;
	double UnderFireNextSidestep = -1.0e9;
	/** He manoeuvred in this spell (the formation leaves him on the spot: IsHoldingUnderFireSpot). */
	bool bUnderFireManoeuvred = false;
	/** He went prone under fire (crouches again when it is over). */
	bool bUnderFireWentProne = false;
	/** The leader's move order serial when he manoeuvred (a new order releases the spot). */
	uint32 UnderFireLeaderSerial = 0;
	TWeakObjectPtr<const AOperativeCharacter> UnderFireLeader;
	/** His own move order serial right after his manoeuvre's walk (a cover sprint / a step) - IsOnUnderFireManoeuvreMove. */
	uint32 UnderFireIssuedSerial = 0;
	bool bUnderFireIssuedMove = false;
	/** One manoeuvre decision (gate, cover search, sidestep search, prone). */
	void DecideUnderFireManoeuvre(AOperativeCharacter& Shooter, const FVector& Flash);
	/** The nearest cover slot within the radius whose wall shields him from Flash (UnderFireRules / PmcCommanderRules::CoverQualityAgainst). */
	bool FindUnderFireCover(const FVector& Flash, struct FCoverSlot& OutSlot) const;
	/** From Point (feet), his muzzle height: a clear line to Flash (allies ignored, Shooter counts as clear) and no mate in it. */
	bool HasUnderFireLine(const FVector& Feet, const FVector& Flash, const AOperativeCharacter* Shooter, bool* bOutMateInLine = nullptr) const;
	/** His team's live members (the squad / his PMC team), himself excluded. */
	TArray<AOperativeCharacter*> GetUnderFireMates() const;
	/** TurnAimTowards steps once per frame (the combat facing and the fire loop both turn him; prone the rate must hold). */
	uint64 AimTurnFrame = MAX_uint64;
	/** A return round at the flash point (FindShootTarget, UnderFireRules::ShouldReturnFire). */
	bool EvaluateReturnFire(FShootCandidate& Out) const;
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
	/** Set around ShootAtTarget for a shot at a peeking man's exposed head and shoulder (FShootCandidate::bExposedAim): the round's end point. */
	bool bExposedAimShot = false;
	FVector ExposedAimPoint = FVector::ZeroVector;
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
	/** The current cover entry turns in without the enter clip (IsCoverEntryEasedTurn); the pending order is a corner peek arrival. */
	bool bCoverEntryEasedTurn = false;
	bool bCornerPeekArrivalPending = false;
	int32 CornerPeekArrivalEntryCount = 0;
	/** Run-and-peek (#55): armed by OrderRunCornerPeek / ArmRunCornerPeek, consumed by the entry into that slot (StartRunCornerPeekOnEntry). */
	bool bRunCornerPeekPending = false;
	FCoverSlot RunCornerPeekSlot;
	FVector RunCornerPeekEdge = FVector::ZeroVector;
	int32 RunCornerPeekCount = 0;
	/** The armed corner slot's peek facing (the corner stance's aim yaw from that slot, deg): the run's last metres turn to it (#55 fix). */
	float RunCornerPeekFacingYaw = 0.f;
	/** OrderTakeCoverForPeek: OrderTakeCover arms the run-and-peek on the walk it starts (before the walk can end in an entry). */
	bool bRunCornerPeekRequest = false;
	FVector RunCornerPeekRequestEdge = FVector::ZeroVector;
	/** The current cover entry goes straight into the peek stance (IsCoverEntryPeekStance). */
	bool bCoverEntryPeekStance = false;
	/** The peek stance's guard on its held turn (UpdateCoverEntryBlend): the pose's pelvis at the entry, checked once, flips (smokes / logs). */
	FQuat PeekStancePelvisAtEntry = FQuat::Identity;
	bool bPeekStanceTurnChecked = true;
	int32 PeekStanceTurnFlips = 0;
	int32 PeekStanceEntryCount = 0;
	float LastPeekStanceArrivalOffDeg = -1.f;
	/** Arrives already turned (IsRunPeekYawLockEnabled): the armed corner's side and the cover facing yaw he will get there, deg. */
	ECoverFacing RunCornerPeekSide = ECoverFacing::Right;
	float RunCornerPeekCoverYaw = 0.f;
	/** The pre-turn has started for this arming (it then runs on to the entry, whatever the speed does to its start distance). */
	bool bRunCornerPeekPreTurnOn = false;
	/** The fire idle's drawn body yaw (component space, UOperativeAnimInstance::GetPeekStanceBodyYawCs), sampled once per arming. */
	bool bRunCornerPeekFinalBodyValid = false;
	float RunCornerPeekFinalBodyCs = 0.f;
	/** The run pose's drawn body yaw (component space) through a short low-pass (the strafe the turn causes shifts it). */
	bool bRunCornerPeekRunBodySeeded = false;
	float RunCornerPeekRunBodyCs = 0.f;
	/** The way the pre-turn turns (+1 / -1; 0 = not yet): it never steps back (RunCornerPeekRules::OneWayStepYaw). */
	int32 RunCornerPeekPreTurnSign = 0;
	/** The yaw lock's entry: the drawn body's gap the lock eased out (smokes); the mesh's held yaw for the entry frame only. */
	float LastPeekStanceBodyGapDeg = 0.f;
	uint64 PeekStanceBridgeFrame = 0;
	float PeekStanceBridgeYaw = 0.f;
	/** EnterCover's end: an armed run-and-peek arriving at its corner turns into the corner peek (always disarms). */
	void StartRunCornerPeekOnEntry(const FCoverSlot& Slot);
	/** The armed run-and-peek arrives with this cover entry: still armed, the entered slot on the ordered corner's wall, near the ordered slot. */
	bool IsRunCornerPeekArrivalAt(const FCoverSlot& Slot) const;
	/** The armed run-and-peek's end seen from Slot: which corner (OutSide) and where along the wall (OutEdgeAlongCm, from Slot). */
	void ResolveRunCornerPeekEdge(const FCoverSlot& Slot, ECoverFacing& OutSide, float& OutEdgeAlongCm) const;
	/**
	 * The run-and-peek's last metres turn the runner (RunCornerPeekRules::ShouldPreTurn; UpdateCombatFacing): from PreTurnStartCm before the
	 * entry with the yaw lock (latched for the arming), the first fix's 1.5 m from the slot without it.
	 */
	bool WantsRunCornerPeekPreTurn();
	/** One frame of that pre-turn: the actor yaw that draws his body at the fire idle's drawn facing by the entry (or the corner aim, A/B). */
	float StepRunCornerPeekPreTurn(float DeltaTime);
	/** How far short of its slot the run enters the cover: the glide of his momentum (TryEnterCoverFromRun), cm. */
	float GetRunInGlideCm(float SpeedCmS) const;
	/**
	 * EnterCover of a peek stance arrival with the yaw lock: the drawn body's facing at the entry (OldActorYaw = his yaw before the turn to
	 * the wall) handed to the anim's lock, eased to the fire idle's own (the entry frame's pose held on the mesh if it is already drawn).
	 * False when the lock cannot run (no anim / clip / bones): the first fix's held yaw then.
	 */
	bool BeginPeekStanceYawLock(float OldActorYaw, ECoverFacing Side, bool bCrouch);
	/**
	 * The pre-turn's start (WantsRunCornerPeekPreTurn): the anim's approach lock from the drawn body now to the fire idle's drawn facing, over
	 * the pre-turn's own plan (DistanceToSlotCm at SpeedCmS, entering the glide short of the slot) - main's verification 2026-11-08, the
	 * shotgun Engineer's jog swayed the drawn body back and forth under the turn.
	 */
	void BeginPeekApproachYawLock(float DistanceToSlotCm, float SpeedCmS);
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
	/** The running yaw blend's length (CoverEntryYawBlendSeconds, or CornerPeekEntryYawBlendSeconds for a corner peek arrival), s. */
	float CoverEntryYawBlendActive = 0.3f;
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
	/**
	 * fix/corner-exit-jerk (RecoilProbe 2026-11-08): the extra lean's world direction / amount while it is on - it eases out on the side it
	 * leaned to, also after a shimmy turned the facing (the other side's amount is 0 cm: the 11 cm jumped back in a frame) or the cover was left.
	 */
	FVector CornerLeanExtraVector = FVector::ZeroVector;
	/** Leaving the cover from the corner fire pose: until then the actor's turn keeps the leaned-out body moving <= CornerLeaveBodySpeed. */
	double CornerLeaveTurnUntil = -1.0;
	/** Clamps this frame's turn of the actor (from YawBefore) while the drawn body still stands far off the capsule after the corner pose. */
	void LimitCornerLeaveTurn(float YawBefore, float DeltaTime);
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
	/** One quick fire area round towards Aim (no aimed target): FireAreaRound with the quick fire spread / hit factor / cadence. */
	void FireQuickFireRound(const FVector& Aim);
	/**
	 * One area-fire round towards Aim (quick fire, the SUPPRESS order): ammo, misfire, noise, the spread trace (x the suppression
	 * spread when he is suppressed), what it crosses (an enemy by its chance x Spec.HitMultiplier, a barrel / mine / trapped object
	 * goes off) and the suppression it causes.
	 */
	void FireAreaRound(const FVector& Aim, const FOperativeAreaRound& Spec);
	/** The SUPPRESS order (OperativeCharacterSuppression.cpp), called from RunCombatShooting; returns its reason. */
	const TCHAR* RunSuppressFire(float DeltaTime, AActor*& OutTarget);
	/** The SUPPRESS order can be taken now (refusal floating text when bExplain). */
	bool CanTakeSuppress(bool bExplain);
	/** His round's suppression class: full-auto, the sniper rifle, else semi-auto. */
	ESuppressionWeapon GetOwnRoundClass(bool bFullAuto) const;
	/** Reports one of his rounds to USuppressionSubsystem (the weapon's suppression_weight). */
	void ReportOwnRound(const FVector& Start, const FVector& End, const AActor* Target, bool bHit, bool bStruckCover, bool bFullAuto) const;
	bool bSuppressActive = false;
	bool bSuppressPlanned = false;
	/** The order is at an enemy (not a spot): it ends when he dies (the weak pointer alone cannot tell a dead target from none). */
	bool bSuppressAtTarget = false;
	FVector SuppressPoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> SuppressTarget;
	FVector PlannedSuppressPoint = FVector::ZeroVector;
	TWeakObjectPtr<AActor> PlannedSuppressTarget;
	/** Rounds of the current full-auto burst (SuppressionRules::NextOrderRoundDelay). */
	int32 SuppressBurstCount = 0;
	/** World time of the order / its last round (the order's timeout counts from it). */
	double SuppressLastProgressTime = 0.0;
	int32 SuppressRoundsFired = 0;
	/** Fire at a point skipped for no line of fire (HasUsefulFireLine). */
	int32 NoLineOfFireSkips = 0;
	int32 FullAutoRoundsFired = 0;
	ESuppressOrderStop LastSuppressStop = ESuppressOrderStop::None;
	int32 SuppressedBlindShots = 0;
	int32 PinnedFireHolds = 0;
	int32 ShotsWhilePinned = 0;
	float LastShotHitChance = 0.f;
	/** Pellets of the last aimed shot that hit (1 / 0 for a single-bullet weapon). */
	int32 LastShotPelletHits = 0;
	/**
	 * ShootAtTarget's hit: damage (crit, elevation, stance, cover, distance table, the weapon's damage falloff), x PelletShare (the share
	 * of a shotgun's pellets that hit, WeaponBallisticsRules), the weapon's penetration class, telemetry, the weapon's self cold / warmth.
	 */
	void ApplyRangedHit(AActor* Target, float Cover, int32 DistCells, bool bRagingShot, float PelletShare = 1.f);
	/** Full-auto recoil bloom of the SUPPRESS order, degrees, and the time of its last round (WeaponBallisticsRules::SettleRecoil). */
	float RecoilBloomDeg = 0.f;
	double RecoilLastRoundTime = -1.0;
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
	/** StanceSerial when the player's stance order set the stance (IsStanceFromPlayer). */
	uint32 PlayerStanceSerial = MAX_uint32;
	/** StanceSerial when the autonomy's hold crouch set the stance (IsHoldCrouch). */
	uint32 HoldCrouchSerial = MAX_uint32;
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
	/** The critical wound holds him prone (CriticalWoundRules::Step; OperativeCharacterStance.cpp). */
	bool bCriticalWoundDown = false;
	int32 CriticalWoundDowns = 0;
	/** Per tick: down when he became critical (as soon as he is free), the lock lifted when healed. */
	void UpdateCriticalWound();
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

public:
	// --- The RPG-7 (2026-11-09; the designer's numbers the same day; LauncherRules, OperativeCharacterLauncher.cpp) ---

	/** The weapon in hands is a launcher (handling "launcher" with a projectile block). */
	bool IsLauncherEquipped() const;

	/** The last launcher shot's refusal (None = it went). */
	ELauncherRefusal GetLastLauncherRefusal() const { return LastLauncherRefusal; }
	int32 GetLauncherShots() const { return LauncherShots; }
	int32 GetLauncherRefusals() const { return LauncherRefusals; }

	/**
	 * The loaded rocket on the tube (the weapon's "visual.loaded_round_mesh", 2026-11-09): shown while a round is in the tube, hidden from the shot,
	 * back from the reload clip's insertion moment (WeaponAnimLayerRules::LauncherRoundVisible). False without a launcher / its round model.
	 */
	bool IsLauncherRoundVisible() const;

	/**
	 * The share of the whole reload (0..1) at which the round shows on the model again: set by the anim instance from the reload clip it plays
	 * (UOperativeAnimInstance, the launcher set's reload_insert_s; the clip's rate fitted to the reload time); 0.5 without a launcher clip.
	 */
	float LauncherRoundInsertFraction = 0.5f;

	/** The effect hooks the last launcher shot spawned (muzzle flash + back-blast; 0 while the data hooks are empty; smokes). */
	int32 GetLauncherFxSpawned() const { return LauncherFxSpawned; }

	/** The reload's progress, 0..1 (0 when not reloading): the elapsed share of the weapon's reload time. */
	float GetReloadProgress01() const;

	/** The tube's reload waits for the shot's follow-through (the game designer 2026-11-09: the Reload clip at rate 1 after the Shot clip). */
	bool IsLauncherReloadPending() const { return bLauncherReloadPending; }
	/** The launcher's Shot clip still plays (or the rocket left this instant). */
	bool IsLauncherShotFollowThrough() const;

private:
	/** Shows / hides the round on the model (every tick; ApplyWeaponVisual creates the component). */
	void UpdateLauncherRoundVisual();
	/** The launcher's muzzle flash / back-blast effect hooks at the shot (no-op while the weapon's "visual.fx" is empty). */
	void SpawnLauncherShotFx(const FVector& Muzzle, const FVector& AimDirection);

	/** The loaded round's component on the hand model (created on demand, transient). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WeaponRoundMesh;
	int32 LauncherFxSpawned = 0;

	/** The launcher rules allow the shot (refuses with a warning + counts otherwise); true for any other weapon. */
	bool LauncherShotAllowed(AActor* Target);
	/** Launches the rocket at Target (a miss scatters the aim point). */
	bool FireLauncherAt(AActor* Target, bool bRagingShot);
	/** The fire loop: starts the pending tube reload once the shot's follow-through is over. */
	void UpdateLauncherReloadStart();
	bool bLauncherReloadPending = false;
	double LauncherLastShotTime = -100.0;

	ELauncherRefusal LastLauncherRefusal = ELauncherRefusal::None;
	int32 LauncherShots = 0;
	int32 LauncherRefusals = 0;
	double LauncherLastWarningTime = -100.0;

public:
	// --- Who fires the RPG-7 and when (the game designer's answers 2026-11-09, item 7; LauncherTacticsRules, OperativeCharacterLauncherAI.cpp) ---
	// One rule for the squad and the PMC fighters, in the shared fire loop: a cluster / a man in cover / a heavy target / a group to suppress
	// draws the launcher, a lone exposed man keeps the rifle; the player's own pick and ordered attacks always win.

	/**
	 * The player picked the weapon in hands (X / the weapon selector / the turn-based bar): the launcher rule leaves his pick alone for
	 * launcher_tactics.json "player_choice_hold_s" (a picked launcher fires at any valid target, a picked rifle is never swapped for it).
	 */
	void NotePlayerWeaponChoice();
	/** Forgets the player's pick, a running fallback / strike and the rule's draw state (a new fight, smokes). */
	void ResetLauncherChoice();
	/** The id of the launcher he owns with a rocket in the tube or in reserve ("" = none). */
	FString GetLauncherWeaponId() const;
	/** He owns a launcher with rockets left. */
	bool HasLauncherRockets() const;
	/** The launcher in hands was drawn by the rule (it goes back to the primary when the rocket targets are gone). */
	bool IsLauncherDrawnByRule() const { return bLauncherDrawnByRule; }
	/**
	 * An ordered rocket (the PMC commander's RpgStrike, catalog LauncherStrike): draw the launcher, fire one rocket at Target (or the better
	 * rocket target the rule finds near it: ordered_threshold), then back to the primary. False: no launcher / rockets, no live target.
	 */
	bool OrderLauncherStrike(AActor* Target);
	bool HasLauncherStrikeOrder() const { return LauncherStrikeTarget.IsValid(); }
	void CancelLauncherStrike(const TCHAR* Why);
	/**
	 * The rule's verdict on Target for a rocket from here now (the world view: distance, the cluster / group round it, cover, heavy, mates near
	 * the impact, rockets left) against Threshold (< 0: launcher_tactics.json "threshold"). OutView gets the view it scored.
	 */
	FLauncherChoice EvaluateLauncherTarget(AActor* Target, float Threshold = -1.f, FLauncherTargetView* OutView = nullptr) const;
	/** The best rocket target among his live targets (GatherTargetsFor; null: none usable) and its verdict. */
	AActor* FindBestLauncherTarget(float Threshold, FLauncherChoice& OutChoice) const;
	/** The last decision line of the rule (smokes / the log): "<what> <target>: <reason>, score, veto". */
	const FString& GetLastLauncherDecision() const { return LastLauncherDecision; }
	int32 GetLauncherAutoDraws() const { return LauncherAutoDraws; }
	int32 GetLauncherAutoReturns() const { return LauncherAutoReturns; }
	int32 GetLauncherFallbacks() const { return LauncherFallbacks; }
	int32 GetLauncherStrikeShots() const { return LauncherStrikeShots; }

private:
	/**
	 * The weapon choice in the fire loop, after the target is found (RunCombatShooting): may draw the launcher (returns the fire-trace reason:
	 * no shot this tick), go back to the primary, or retarget InOutShot to the rocket target. Null = fire on as decided.
	 */
	const TCHAR* UpdateLauncherChoice(float DeltaTime, FShootCandidate& InOutShot, bool bAutoFire);
	/** Before the reload / sidearm step: a rule-drawn launcher goes back to the primary when an enemy is inside its minimum range. */
	bool UpdateLauncherReturn(float DeltaTime);
	/** Back to the primary from a rule-drawn launcher (Why to the log); the launcher keeps its tube / reserve. */
	bool ReturnFromLauncher(const TCHAR* Why);
	/** A shot candidate at Target for the rocket (its line evaluated; the target's centre when no line - the blast reaches behind cover). */
	FShootCandidate MakeLauncherCandidate(AActor* Target) const;
	void NoteLauncherDecision(const FString& Line);

	bool bLauncherDrawnByRule = false;
	/** The player's pick: the weapon id and when (-1000 = never). */
	FString PlayerWeaponPickId;
	double PlayerWeaponPickTime = -1000.0;
	/** A player-picked launcher temporarily swapped for the rifle (the target is inside its minimum range / a mate near it). */
	bool bLauncherFallbackActive = false;
	TWeakObjectPtr<AActor> LauncherRuleTarget;
	TWeakObjectPtr<AActor> LauncherStrikeTarget;
	double LauncherStrikeSince = 0.0;
	float LauncherNoTargetSeconds = 0.f;
	float LauncherDecisionTimer = 0.f;
	double LauncherReturnTime = -100.0;
	double LauncherDecisionLogTime = -100.0;
	/** The weapon the rule's draw came from (it goes back to it). */
	FString LauncherReturnWeaponId;
	FString LastLauncherDecision;
	int32 LauncherAutoDraws = 0;
	int32 LauncherAutoReturns = 0;
	int32 LauncherFallbacks = 0;
	int32 LauncherStrikeShots = 0;
};
