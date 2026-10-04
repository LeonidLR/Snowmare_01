#pragma once

#include "CoreMinimal.h"
#include "Characters/EnemyAIRules.h"
#include "GameFramework/Character.h"
#include "Data/CombatTypes.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
struct FOverheadLabel;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedDynamic, AEnemyCharacter*, Enemy);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedNative, AEnemyCharacter*);

/**
 * Base enemy character in CodexTactics.
 * Tagged "Enemy" for combat queries and targeting.
 * Configurable via EEnemyArchetype (Hound, Spitter, Brute, Frostbitten).
 * Godot reference: Scenes/movements/enemy_base.gd.
 */
UCLASS()
class CODEXTACTICS_API AEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Configures the enemy according to the archetype stats and visuals. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void InitializeArchetype(EEnemyArchetype InArchetype);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	EEnemyArchetype GetArchetype() const { return Archetype; }

	/** Name used in radio lines (Godot enemy_name). */
	const FString& GetEnemyDisplayName() const { return EnemyDisplayName; }

	/**
	 * Godot apply_balance_config (enemy_base.gd + enemy_frost_hound / spitter / brute.gd): crit chance / multiplier and,
	 * for hounds, spitters and brutes, health / speed / damage / range / cooldown from the imported game_balance_config.
	 * Called at the end of ApplyArchetypeDefaults with the game mode's GameBalanceConfig.
	 */
	void ApplyBalance(const class UGodotBalanceAsset& Config);

	/**
	 * Level wave modifiers (Godot main.gd _spawn_custom_json_wave): max health = CustomHealth * HpMult when the spawn
	 * has custom_stats.health, else max health * HpMult (full health); attack damage * DamageMult; speed * SpeedMult.
	 */
	void ApplyWaveModifiers(float HpMult, float DamageMult, float SpeedMult, float CustomHealth = 0.f);

	/** The wave modifiers plus the spawn's custom_stats (health, damage, speed, attack range / cooldown). */
	void ApplySpawnEntry(const struct FWaveModifiers& Mods, const struct FEnemySpawnEntry& Entry);

	/** Avoids burning barrels (Godot fears_fire; turn-based enemies keep out of the 5 x 5 fire-fear area). */
	bool DoesFearFire() const { return bFearsFire; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	/** The pack coordinator may send it at Candidate: no recent dead end there and (fire fearers) not inside a fire zone. */
	bool IsTargetUsableForTactics(const AActor* Candidate) const;

	/** EXP every squad member gets for this kill. */
	int32 GetKillExpReward() const { return KillExpReward; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsDying() const { return bIsDying; }

	/**
	 * Godot enemy_base.gd _update_overhead_ui: armor tier marker, name, statuses, HP (hound / cutter 1.15 m, brute 2.4 m,
	 * others 1.8 m up). The status emoji the HUD font lacks are written as words.
	 */
	bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackDamage() const { return AttackDamage; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackRange() const { return AttackRange; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackCooldown() const { return AttackCooldown; }

	/** Attacks target directly (applies damage spec to target's health component). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void AttackTarget(AActor* Target);

	/** Finds the closest living operative from USquadSubsystem. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	AActor* FindClosestSquadMember() const;

	/**
	 * Godot _find_closest_squad_member: operatives, turrets and the generator weighted by EnemyAIRules::SelectTarget
	 * (small enemies go for the generator / turrets first, large ones for the squad).
	 */
	AActor* FindTarget() const;

	/** Cutter pounce (Godot enemy_cutter.gd): windup, ballistic flight, impact, recovery. */
	enum class ECutterJumpPhase : uint8 { None, Windup, Airborne, Impact };

	/** Godot start_jump_attack: the cutter leaps at Target (3.5-9 m, jump config from DA_EnemyAnim_cutter). */
	bool StartJumpAttack(AActor* Target);
	bool IsJumpAttacking() const { return JumpPhase != ECutterJumpPhase::None; }
	/** Seconds until the next pounce. */
	float GetJumpCooldown() const { return JumpCooldownTimer; }

	/** Blueprint hooks for animations / effects (the enemy's AnimBP can also read UEnemyAnimInstance). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnAttackStarted(AActor* Target);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnHitReaction(float Damage);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnDeath();

	/** Blueprint hooks for the jump animation (the cutter Blueprint plays its clips). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackStarted();
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackImpact();

	/** Godot is_fleeing_fire: running from a burning barrel / warm zone. */
	bool IsFleeingFire() const { return bFleeingFire; }

	/**
	 * No usable target (all in fire / heat zones or out of reach): the enemy goes for the nearest operative and ignores
	 * its fear of fire until a usable target appears (user decision 2026-10-02; it used to wait at the zone's edge).
	 */
	bool IsBravingFire() const { return bBravingFire; }

	/** Current victim (operative, turret or generator). */
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** Shared enemy tuning (Godot enemy_* / spitter_preferred_range keys). */
	FEnemyAIConfig AIConfig;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Enemy")
	FOnEnemyDiedDynamic OnEnemyDied;

	FOnEnemyDiedNative OnEnemyDiedNative;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Enemy")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	EEnemyArchetype Archetype = EEnemyArchetype::FrostHound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	FString EnemyDisplayName = TEXT("Ледяная гончая");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackDamage = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackRange = 180.0f; // 1.8m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackCooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritChance = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritMultiplier = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	bool bFearsFire = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	bool bIsDying = false;

	/** EXP every squad member gets for this kill (Godot exp_reward_<type>; ProgressionRules::KillReward). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	int32 KillExpReward = 9;

	float AttackTimer = 0.0f;

	/** Seconds the attack clip still plays: the enemy stands and faces AttackLockTarget (Godot is_attacking). */
	float AttackLockTimer = 0.0f;
	TWeakObjectPtr<AActor> AttackLockTarget;

	/** Starts the attack clip and the stand-still lock; the cooldown lasts at least the clip (Godot _play_attack_animation). */
	void StartAttackAnimation(AActor* Target);

	// --- Cutter jump (Godot enemy_cutter.gd; numbers from /Game/Data/Enemies/DA_EnemyAnim_cutter) ---
	bool bJumpAttackEnabled = false;
	float JumpAttackSpeed = 1.85f;
	float JumpMinDistance = 350.f;
	float JumpMaxDistance = 900.f;
	float JumpCooldown = 6.f;
	float JumpAttackDamage = 28.f;
	float JumpDamageRadius = 220.f;
	ECutterJumpPhase JumpPhase = ECutterJumpPhase::None;
	float JumpPhaseTimer = 0.f;
	float JumpCooldownTimer = 0.f;
	float JumpFlightDuration = 0.7f;
	bool bJumpDamageDealt = false;
	bool bAirborneDeath = false;
	TWeakObjectPtr<AActor> JumpTarget;
	void TickJumpAttack(float DeltaTime);
	void ApplyJumpImpactDamage();
	virtual void Landed(const FHitResult& Hit) override;

	/** Walk speed before frost / fear factors (archetype, balance, wave modifiers). */
	float BaseWalkSpeed = 300.f;
	bool bFleeingFire = false;
	/** Set by FindTarget (see IsBravingFire). */
	mutable bool bBravingFire = false;
	/** Godot last_attacker_source (a turret hit pulls large enemies to turrets). */
	FString LastAttackerSource;
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Targets it could not get closer to (no path): skipped until the time (world seconds). */
	TMap<TWeakObjectPtr<AActor>, double> UnreachableUntil;
	/** Progress towards the current target (stuck detection). */
	TWeakObjectPtr<AActor> ProgressTarget;
	FVector ProgressLocation = FVector::ZeroVector;
	float ProgressTimer = 0.f;
	/** Running a morale fall-back (the floating text once per fall-back). */
	bool bFallingBack = false;

	/** Nearest burning barrel / active heat source within the fear radius (Godot _find_nearest_active_fire_source). */
	bool FindNearestFire(FVector& OutFire) const;
	/** Nearest burning barrel / active heat zone whose fear radius (OutRadius) reaches within Extra of the feet. */
	bool FindNearestFireZone(float Extra, FVector& OutFire, float& OutRadius) const;
	/** Location inside a burning barrel's / active heat zone's fear radius (the heat of Candidate itself excluded). */
	bool IsInFearZone(const FVector& Location, const AActor* Candidate) const;
	/** Barricade / turret in the way within 2.2 m (Godot _find_blocking_barricade). */
	AActor* FindBlockingObstacle(const AActor* Target) const;
	/** Godot _attack_barricade (brutes hit twice as hard) and melee on turrets / the generator. */
	void AttackObject(AActor* Object);
	/** Godot enemy_frost_spitter.gd _process_enemy_behavior. */
	void TickSpitter(float DeltaTime);

	/**
	 * The behaviour part of Tick (targets, attacks, movement); Tick then turns the body (UpdateMovementFacing).
	 * Archetypes with their own brain (AMarksmanEnemyCharacter) override it.
	 */
	virtual void TickBehavior(float DeltaTime);

	/** Turn towards Yaw this frame (attack lock, aiming, pounce windup): the movement facing then stays out of it. */
	void FaceYaw(float Yaw, float DeltaTime, float InterpSpeed);

	/** Godot enemy_base.gd: face the (smoothed) velocity above 0.2 m/s with lerp_angle(turn_speed), else keep. */
	void UpdateMovementFacing(float DeltaTime);

	/** Godot enemy_base.gd turn_speed (lerp factor, 1/s). */
	float TurnSpeed = 8.f;
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bFacedThisTick = false;
	/** Godot position of a target: operatives at their centre, objects at their base. */
	static FVector GodotPosition(const AActor* Actor);

	UFUNCTION()
	void HandleDamaged(const FDamageSpec& Spec, float FinalDamage);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);

	void ApplyArchetypeDefaults();
};
