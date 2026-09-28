#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/CombatTypes.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthChangedSignature, float, NewHealth, float, MaxHealth, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamagedSignature, const FDamageSpec&, DamageSpec, float, FinalDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiedSignature, AActor*, Victim, const FString&, AttackerSource);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDiedNativeSignature, AActor*, const FString&);

/**
 * Health, armor, elemental defense and status effect management component.
 * Godot reference: enemy_base.gd take_damage / _process_status_effects and operative vital signs.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Applies damage according to Godot formula:
	 * effective_armor = (bArmorShred ? base_armor * 0.3 : base_armor) * (1 - armor_pen)
	 * dmg_after_armor = amount * (1 - effective_armor)
	 * final_damage = max(1.0, dmg_after_armor * element_mult * DefenseMultiplier)
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Health")
	float TakeDamage(const FDamageSpec& Spec);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Health")
	void Heal(float Amount);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Health")
	bool IsAlive() const { return CurrentHealth > 0.0f; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Health")
	float GetHealthFraction() const { return MaxHealth > 0.0f ? CurrentHealth / MaxHealth : 0.0f; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Health")
	void SetMaxHealth(float NewMax, bool bResetCurrent = true);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Health")
	void SetDefenseMultiplier(float Multiplier) { DefenseMultiplier = Multiplier; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Health")
	bool HasStatusEffect(EStatusEffect Effect) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Health")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Health")
	float BaseArmorReduction = 0.10f; // 10% default reduction

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Health")
	EArmorTier ArmorTier = EArmorTier::Light;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Health")
	FElementalAffinities ElementalAffinities;

	/** External multiplier, e.g. operative stance defense (Standing 1.0, Crouching 0.75, Prone 0.50). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Health")
	float DefenseMultiplier = 1.0f;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Health")
	FOnHealthChangedSignature OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Health")
	FOnDamagedSignature OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Health")
	FOnDiedSignature OnDied;

	/** Native C++ multicast delegate supporting lambdas. */
	FOnDiedNativeSignature OnDiedNative;

protected:
	void ProcessStatusEffects(float DeltaSeconds);
	void Die(const FString& AttackerSource);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Health")
	float CurrentHealth = 100.0f;

	bool bIsDead = false;

	// Status effect timers (seconds)
	float BurningTimer = 0.0f;
	float BurningTickDamage = 0.0f;
	float BurningTickAccum = 0.0f;
	static constexpr float BurningTickInterval = 0.5f;

	float BleedingTimer = 0.0f;
	float BleedingTickDamage = 0.0f;
	float BleedingTickAccum = 0.0f;
	static constexpr float BleedingTickInterval = 0.5f;

	float FrozenTimer = 0.0f;
	float StaggerTimer = 0.0f;
	float ArmorShredTimer = 0.0f;
};
