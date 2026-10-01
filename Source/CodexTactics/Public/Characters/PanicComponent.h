#pragma once

#include "CoreMinimal.h"
#include "Characters/PanicRules.h"
#include "Components/ActorComponent.h"

#include "PanicComponent.generated.h"

class AOperativeCharacter;
class ARadiusRingActor;

/** Godot PanicComponent.PanicPhase. */
UENUM(BlueprintType)
enum class EPanicPhase : uint8
{
	None,
	/** Running away from the danger. */
	Fleeing,
	/** Crouched and frozen in fear. */
	Cowering
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPanicChanged, bool, bPanicking);

/**
 * Stress and panic of an operative in the real-time fight. Stress grows from heavy wounds, cold, an empty clip, nearby
 * monsters and every hit taken (fortitude cuts it), and calms down without threats, faster by a heat source. At 100 the
 * operative panics — unless the squad's limit of panicking members is reached or, as the leader, while others panic: he
 * stops shooting and obeying, runs from the enemies (FleeDistance / FleeMaxTime), then cowers crouched until the panic
 * time is out and the enemies are far, a heat source breaks it, or a calm leader nearby shortens it.
 *
 * Godot reference: Scripts/components/panic_component.gd, Scenes/movements/player.gd _process_panic_movement. Godot
 * ships it disabled (enable_realtime_panic = false); here it is on (user decision 2026-10-01) with one panicking member
 * at a time and a much more resistant core squad (DA_GameBalanceConfig). Not used in the turn-based fight (as Godot).
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UPanicComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPanicComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Godot enable_realtime_panic (false in Godot; on by the user decision). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Panic")
	bool bEnabled = true;

	/** Tuning (OperativeBalance fills it from DA_GameBalanceConfig with the operative's role keys). */
	FPanicConfig Config;

	/** Godot on_damage_taken: a stress jolt from a hit (FinalDamage after armour / stance). */
	void OnDamageTaken(float FinalDamage);

	/** Godot on_low_ammo: a stress jolt when the clip runs dry under fire. */
	void OnLowAmmo();

	/** Godot trigger_panic; bForce also outside the fight (tests / debug). */
	void TriggerPanic(const FString& Reason, bool bForce = false);

	/** Godot recover_from_panic. */
	void RecoverFromPanic(const FString& Reason, bool bSilent = false);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Panic")
	bool IsPanicking() const { return bPanicking; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Panic")
	EPanicPhase GetPhase() const { return Phase; }

	/** 0..100. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Panic")
	float GetStress() const { return Stress; }

	void SetStressForTesting(float Value) { Stress = FMath::Clamp(Value, 0.f, 100.f); }

	/** Real-time wave combat (panic and stress only build up there; Godot is_combat_active). */
	bool IsCombatActive() const;

	bool IsAuraShown() const;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Panic")
	FOnPanicChanged OnPanicChanged;

private:
	void ProcessStress(float DeltaSeconds);
	void ProcessPanicState(float DeltaSeconds);
	void AttemptTriggerPanic();
	void TransitionToCowering();
	void UpdateAura(bool bShow);

	FVector GetFleeDirection();
	float GetNearestEnemyDistance() const;
	const AActor* FindNearestHeatSource(float& OutDistance) const;
	int32 CountPanickedSquadMembers() const;
	AOperativeCharacter* GetOperative() const;

	float Stress = 0.f;
	bool bPanicking = false;
	EPanicPhase Phase = EPanicPhase::None;
	FVector PanicOrigin = FVector::ZeroVector;
	FVector CachedFleeDirection = FVector::ZeroVector;
	float FleePhaseTimer = 0.f;
	float PanicTimer = 0.f;
	float WarmthTimer = 0.f;
	/** Stress shown on the name plate (50+). */
	bool bStressShown = false;

	UPROPERTY(Transient)
	TObjectPtr<ARadiusRingActor> Aura;
};
