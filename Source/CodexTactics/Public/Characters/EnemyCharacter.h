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

	/** Avoids burning barrels (Godot fears_fire; turn-based enemies keep out of the 5 x 5 fire-fear area). */
	bool DoesFearFire() const { return bFearsFire; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

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

	/** Godot is_fleeing_fire: running from a burning barrel / warm zone. */
	bool IsFleeingFire() const { return bFleeingFire; }

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

	float AttackTimer = 0.0f;

	/** Walk speed before frost / fear factors (archetype, balance, wave modifiers). */
	float BaseWalkSpeed = 300.f;
	bool bFleeingFire = false;
	/** Godot last_attacker_source (a turret hit pulls large enemies to turrets). */
	FString LastAttackerSource;
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Nearest burning barrel / active heat source within the fear radius (Godot _find_nearest_active_fire_source). */
	bool FindNearestFire(FVector& OutFire) const;
	/** Barricade / turret in the way within 2.2 m (Godot _find_blocking_barricade). */
	AActor* FindBlockingObstacle(const AActor* Target) const;
	/** Godot _attack_barricade (brutes hit twice as hard) and melee on turrets / the generator. */
	void AttackObject(AActor* Object);
	/** Godot enemy_frost_spitter.gd _process_enemy_behavior. */
	void TickSpitter(float DeltaTime);
	/** Godot position of a target: operatives at their centre, objects at their base. */
	static FVector GodotPosition(const AActor* Actor);

	UFUNCTION()
	void HandleDamaged(const FDamageSpec& Spec, float FinalDamage);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);

	void ApplyArchetypeDefaults();
};
