#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "TurretActor.generated.h"

class UHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTurretFired, ATurretActor*, Turret, AActor*, Target, const FVector&, HitLocation);

/**
 * Automatic turret: fires at the nearest visible enemy within AttackRange (a barricade in the line of fire cuts
 * the damage to 60 %, walls block it) while powered and not broken. Powered by default; a generator breakdown cuts
 * the power of every turret, a repair restores it. At 0 HP it breaks (repair: engineer 2 s, others 4 s). A powered
 * turret also warms operatives nearby (Godot heat_sources, 4.5 m). Can be picked up, moved, trapped.
 * Godot reference: Scenes/movements/deployables/turret.gd, main.gd turret menu / `_execute_repair_object`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ATurretActor : public ADeployableActor
{
	GENERATED_BODY()

public:
	/** Godot turret.gd _update_overhead_ui: broken / unpowered / HP, 1.6 m up. */
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	ATurretActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty()) override;
	virtual ETrapFlavor GetTrapFlavor() const override { return ETrapFlavor::Turret; }

	/** Generator power (Godot set_power_state). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Turret")
	void SetPowered(bool bNewPowered);

	/** Full repair (Godot repair). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Turret")
	void Repair();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	bool IsPowered() const { return bPowered; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	bool IsBroken() const { return bBroken; }

	/** Repair time for Operative: engineer 2 s, others 4 s. */
	float GetRepairSeconds(const AOperativeCharacter* Operative) const;

	/** Sets every turret of the world on / off (generator breakdown / repair / start). */
	static void SetAllPowered(UWorld* World, bool bNewPowered);

	/** Save-game load: power and broken state (health is restored separately); the look follows. */
	void RestoreSaved(bool bInPowered, bool bInBroken);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Turret")
	TObjectPtr<UHealthComponent> Health;

	/** Rotating gun head (placeholder cube). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Turret")
	TObjectPtr<UStaticMeshComponent> Head;

	/** Damage per shot (Godot 16). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret", meta = (ClampMin = "0"))
	float Damage = 16.f;

	/** cm (Godot 12 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret", meta = (ClampMin = "0"))
	float AttackRange = 1200.f;

	/** Seconds between shots (Godot 0.45). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret", meta = (ClampMin = "0.01"))
	float FireInterval = 0.45f;

	/** Damage share through a barricade (Godot cover_mult 0.6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret", meta = (ClampMin = "0", ClampMax = "1"))
	float BarricadeCoverMultiplier = 0.6f;

	/** Enemy distance that sets a trap off, cm (Godot 1.8 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret", meta = (ClampMin = "0"))
	float TrapContactDistance = 180.f;

	/** Fired a shot (tracer / muzzle flash / sound in Blueprints). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Turret")
	FOnTurretFired OnFired;

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Turret", meta = (DisplayName = "On Fired"))
	void ReceiveFired(AActor* Target, const FVector& HitLocation);

protected:
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const override;

private:
	/** Nearest enemy in range with a line of fire; OutCover = 1 or the barricade multiplier. */
	AActor* FindTarget(float& OutCover, FVector& OutAim) const;
	void Fire(AActor* Target, float Cover, const FVector& Aim);
	void FinishRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser);
	void UpdateState();

	UFUNCTION()
	void HandleBroken(AActor* Victim, const FString& AttackerSource);

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Turret")
	bool bPowered = true;

	bool bBroken = false;
	float ShotCooldown = 0.f;
};
