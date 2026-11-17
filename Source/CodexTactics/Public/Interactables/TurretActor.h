#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "TurretActor.generated.h"

class UHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTurretFired, ATurretActor*, Turret, AActor*, Target, const FVector&, HitLocation);

/**
 * Whose side a turret is on (G003b; docs/design/TURRET_POWER.md rule 3: monsters always, plus the humans hostile to its owner).
 * Friendly = the squad's (fires at monsters and the PMC). Hostile = a PMC turret (fires at the squad and at monsters). Neutral = no owner
 * (fires at monsters only).
 */
UENUM(BlueprintType)
enum class ETurretFaction : uint8
{
	Friendly,
	Neutral,
	Hostile
};

/**
 * Automatic turret: fires at the nearest visible enemy within AttackRange (a barricade in the line of fire cuts
 * the damage to 60 %, walls block it) while powered and not broken. Powered by default; a generator breakdown cuts
 * the power of every turret, a repair restores it. At 0 HP it breaks (repair: engineer 2 s, others 4 s). A powered
 * turret also warms operatives nearby (Godot heat_sources, 4.5 m). Can be picked up, moved, trapped.
 * G003b: Faction - the squad's turret (Friendly, default: monsters + PMC) or a level turret (Hostile: the squad + monsters; Neutral:
 * monsters only; no squad menu). A level turret's power is its own (SetPowered), the squad's generator powers only friendly turrets.
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

	/**
	 * Sets every FRIENDLY turret of the world on / off (the squad's generator breakdown / repair / start). Hostile / neutral level turrets
	 * keep their own power (SetPowered; the level power mechanic hooks in there).
	 */
	static void SetAllPowered(UWorld* World, bool bNewPowered);

	/** Whose side the turret is on (who it fires at). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	ETurretFaction GetFaction() const { return Faction; }

	/** Changes the side (e.g. a captured level turret); takes effect on the next target search. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Turret")
	void SetFaction(ETurretFaction NewFaction);

	/**
	 * A level turret's start state, set by its spawner before FinishSpawning (UCodexLevelPropsSubsystem, G003b): the side and the power.
	 * No visuals here (BeginPlay applies them).
	 */
	void InitLevelTurret(ETurretFaction InFaction, bool bInPowered);

	/** Shots fired since spawn (smokes, telemetry). */
	int32 GetShotsFired() const { return ShotsFired; }

	/** What the last shot was fired at (null before the first / when it is gone). */
	AActor* GetLastTarget() const { return LastTarget.Get(); }

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

	/** Friendly (default) = the squad's turret; Hostile / Neutral = a level turret (GetFaction). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Turret")
	ETurretFaction Faction = ETurretFaction::Friendly;

	bool bBroken = false;
	float ShotCooldown = 0.f;
	int32 ShotsFired = 0;
	TWeakObjectPtr<AActor> LastTarget;
};
