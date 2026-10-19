#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Survival/ColdRules.h"
#include "ColdSurvivalComponent.generated.h"

class AOperativeCharacter;
struct FTacticalHeatConfig;

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

	/**
	 * Sprint 15 chemical heat pack on this operative: cold - HeatPackColdReduction, frostbite cleared (the collapsed operative
	 * gets up to a crouch and can move again), the heat buffer (x0.5 accumulation) restarts. False when it would do nothing.
	 */
	bool ApplyHeatPack(const FTacticalHeatConfig& HeatConfig);

	/**
	 * A sudden cold shock (tutorial "cold_bump" = a scripted wind gust): cold + Amount, at least MinLevel (ColdRules::ApplyColdShock);
	 * the tier / speed / frozen-weapon state follow at once (OnTierChanged fires). Frostbite at 100 % comes with the next cold step.
	 * Returns the new cold; no-op for a dead operative.
	 */
	float AddColdShock(float Amount, float MinLevel = 0.f);

	/** Seconds left of the heat-pack buffer (0 = none); frozen with the cold (turn-based, dialogues), runs on world time. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Cold")
	float GetHeatBufferSecondsLeft() const { return HeatBufferSecondsLeft; }

	/** Save-game load. */
	void RestoreHeatBuffer(float SecondsLeft) { HeatBufferSecondsLeft = FMath::Max(0.f, SecondsLeft); }

	/**
	 * Save-game load (audit 2026-10-19): the sticky cold flags as saved - frostbite (set at the Frostbite tier, cleared only once
	 * warmed) and the frozen weapon (hysteresis band) are not functions of the cold level alone; the tier is re-derived from the
	 * owner's restored ColdLevel at once (speed multiplier), silently.
	 */
	void RestoreColdState(bool bInFrostbitten, bool bInWeaponFrozen);

	/** Strongest WarmthRate of the active heat sources covering the owner (0 = none sets one). */
	float GetHeatSourceWarmthRate() const;

	/** Run telemetry (Godot cold_damage_taken / extreme_cold_time_sec): HP lost to freezing, seconds spent at 100 %. */
	float GetColdDamageTaken() const { return ColdDamageTaken; }
	float GetExtremeColdTime() const { return ExtremeColdTimeSec; }

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

	TWeakObjectPtr<AOperativeCharacter> Operative;
	EColdTier Tier = EColdTier::Normal;
	float HeatBufferSecondsLeft = 0.f;
	bool bWeaponFrozen = false;
	bool bFrostbitten = false;
	float ColdDamageTaken = 0.f;
	float ExtremeColdTimeSec = 0.f;
};
