#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Data/CombatTypes.h"
#include "EnemyCharacter.generated.h"

class UHealthComponent;
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

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsDying() const { return bIsDying; }

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

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);

	void ApplyArchetypeDefaults();
};
