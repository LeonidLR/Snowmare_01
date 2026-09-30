#pragma once

#include "CoreMinimal.h"
#include "Characters/RageRules.h"
#include "Components/ActorComponent.h"
#include "RageComponent.generated.h"

class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRageChanged, bool, bRaging);

/**
 * Rage of an operative (Godot Scripts/components/rage_component.gd): the second (per role) crit from the same enemy
 * within 25 s, with enough health and rounds, rolls the rage chance (decaying with combat time). Raging: a random
 * enemy within 25 m every 0.55 s is sprayed with fire (fire rate x0.45, damage x1.3, +30 luck for crits, no ammo
 * spent), player orders are refused; 8.5 s (per role) later it wears off.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API URageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URageComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Godot on_incoming_hit: counts crits per attacker, may start the rage. */
	void OnIncomingHit(AActor* Attacker, bool bCrit);

	/** Godot enter_rage: the offender (or any enemy in reach) is the first target; floating text and radio shout. */
	void EnterRage(AActor* Offender = nullptr);

	/** Godot exit_rage: radio line, target dropped. */
	void ExitRage(const FString& Reason = TEXT("Ярость отпустила"));

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Rage")
	bool IsRaging() const { return bRaging; }

	/** The fiery ring under the feet is visible (while raging). */
	bool IsAuraShown() const;

	/** Godot get_chaotic_target: the current random target, re-picked when it died. */
	AActor* GetChaoticTarget();

	float GetRageTimeLeft() const { return RageTimer; }
	float GetCurrentChance() const { return RageRules::GetChance(Config, CombatTimeElapsed); }

	FRageConfig Config;

	/** Forces the next rage roll (smokes): 0 always rages, 1 never. Negative = random. */
	float ForcedRollForTesting = -1.f;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Rage")
	FOnRageChanged OnRageChanged;

private:
	AActor* FindAnyEnemyInReach() const;

	struct FCritRecord
	{
		TWeakObjectPtr<AActor> Enemy;
		int32 Crits = 0;
		double LastTime = 0.0;
	};
	TArray<FCritRecord> CritTracker;

	bool bRaging = false;
	float RageTimer = 0.f;
	float ChaoticSwitchTimer = 0.f;
	/** Godot combat_time_elapsed: counts from the component's start. */
	float CombatTimeElapsed = 0.f;
	TWeakObjectPtr<AActor> ChaoticTarget;

	/** Godot RageAura: fiery torus (0.75..1.05 m) under the feet while raging. */
	void UpdateAura(bool bShow);

	UPROPERTY(Transient)
	TObjectPtr<class ARadiusRingActor> Aura;
};
