#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Survival/ColdRules.h"
#include "ColdSurvivalComponent.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnColdTierChanged, AOperativeCharacter*, Operative, EColdTier, Tier);

/**
 * Real-time cold of one operative: accumulation in the open, warming near heat sources / in closed zones /
 * during preparation, speed tiers, weapon freezing, frostbite collapse, freezing damage and warm regeneration.
 * Writes AOperativeCharacter::ColdLevel. Paused during turn-based combat (per-round cold comes with it).
 * Godot reference: Scenes/movements/player.gd `_process_cold_system`, `is_in_warm_zone`, `is_near_heat_source`.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UColdSurvivalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UColdSurvivalComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** True if an active heat source covers the owner. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	bool IsNearHeatSource() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	EColdTier GetTier() const { return Tier; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	bool IsWeaponFrozen() const { return bWeaponFrozen; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	bool IsFrostbitten() const { return bFrostbitten; }

	/** Misfire probability of the next shot. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	float GetMisfireChance() const;

	/** Hit-chance penalty from shivering. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	float GetAimPenalty() const;

	/** Advances cold by DeltaSeconds (also used by headless checks). */
	void StepCold(float DeltaSeconds);

	/** Cold resistance of this operative (Godot fortitude: commander 15, engineer 25, medic 20). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cold")
	float Fortitude = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Cold")
	FColdConfig Config;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Cold")
	FOnColdTierChanged OnTierChanged;

private:
	FColdEnvironment GatherEnvironment() const;
	void ApplyTierEffects(EColdTier NewTier);
	void PostOperativeMessage(const FText& Text) const;

	TWeakObjectPtr<AOperativeCharacter> Operative;
	EColdTier Tier = EColdTier::Normal;
	bool bWeaponFrozen = false;
	bool bFrostbitten = false;
};
