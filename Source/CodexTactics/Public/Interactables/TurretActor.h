#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "TurretActor.generated.h"

class APowerGeneratorActor;
class UAudioComponent;
class UHealthComponent;
class UPointLightComponent;
class USoundBase;

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
 * TURRET_POWER.md phase 1: a level turret linked to an APowerGeneratorActor (SetPowerSource) is a NETWORK turret - powered only while its
 * generator runs (spin-up 2 s, the gun drops at once when the power goes), its side is the generator owner's (PowerRules::MayTargetHuman:
 * monsters always, humans of any team but the owner's; no owner = no humans), a status lamp shows it is live, the squad learns its owner
 * by observing it (2 s, line of sight, 40 m, standing still) or by the generator's panel; the quest generator never touches it.
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

	// --- Power network (TURRET_POWER.md phase 1) ---

	/** Links the turret to a generator's network (null unlinks: it goes dark). Called by APowerGeneratorActor::LinkTurret. */
	void SetPowerSource(APowerGeneratorActor* Generator);

	/** The generator feeding it (null for the squad's turrets and the legacy level turrets). */
	APowerGeneratorActor* GetPowerSource() const { return PowerSource.Get(); }

	/** Fed by a generator network (its power and side are the generator's). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	bool IsNetworkLinked() const { return PowerSource.IsValid(); }

	/** The network's power and owner (the generator pushes it on every change); the turret spins up / winds down. */
	void ApplyNetworkState(bool bLive, int32 InOwnerTeam);

	/** The team whose humans it never fires at (the network owner; PowerRules::NoOwner = it fires at no human). */
	int32 GetOwnerTeam() const { return OwnerTeam; }

	/** Powered, intact and spun up: it may fire now. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	bool IsLive() const;

	/** Would it fire at Target (side rule only: alive / range / line of fire are not checked). */
	bool MayTarget(const AActor* Target) const;

	/** Monsters treat it as a target: the squad's turrets always, a network turret only while powered (a dark one is scenery). */
	bool IsMonsterTarget() const { return !IsNetworkLinked() || bPowered; }

	/** The squad knows this network turret's owner (observed / the panel / it changed hands by the squad). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Turret")
	bool IsOwnerKnown() const { return bOwnerKnown; }

	/** Marks the owner known (and its whole network: the generator reveals its other turrets); bAnnounce posts the identification line. */
	void RevealOwner(bool bAnnounce);

	/** The owner is unknown again (a save load before the network knowledge is re-applied). */
	void ForgetOwner();

	/** Seconds the squad has observed it without a break (smokes). */
	float GetObserveSeconds() const { return ObserveSeconds; }

	/** The status lamp (placeholder look of a live turret: amber, then the side's colour once identified). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Turret")
	TObjectPtr<UPointLightComponent> StatusLamp;

	/** Optional looping servo hum while a network turret is live (none by default: no audio asset is shipped for it yet). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Turret")
	TObjectPtr<USoundBase> HumSound;

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

	/** Network turrets: the squad's passive observation (5 Hz). */
	void TickObservation(float DeltaSeconds);
	/** Lamp and hum of a network turret. */
	void UpdateNetworkLook();

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Turret")
	bool bPowered = true;

	/** Friendly (default) = the squad's turret; Hostile / Neutral = a level turret (GetFaction). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Turret")
	ETurretFaction Faction = ETurretFaction::Friendly;

	bool bBroken = false;
	TWeakObjectPtr<APowerGeneratorActor> PowerSource;
	int32 OwnerTeam = -1;
	/** The owner while last live (-2 = never live): a new owner that is not the squad hides the side again (knowledge step 3). */
	int32 LastLiveOwner = -2;
	/** The level's own side (InitLevelTurret, or the side when first linked): restored when the turret leaves its cable. */
	ETurretFaction LevelFaction = ETurretFaction::Friendly;
	bool bHasLevelFaction = false;
	/** Deploy / relocate flags before the first link (a network turret is a fixed emplacement). */
	bool bPreLinkDeployable = true;
	bool bPreLinkRelocatable = false;
	bool bPreLinkFlagsStored = false;
	/** Last values handed to the lamp / the head tint (render state only touched on a change). */
	int8 LampShownCache = -1;
	FLinearColor LampColorCache = FLinearColor::Black;
	float LampIntensityCache = -1.f;
	FColor TintCache = FColor::Transparent;
	bool bTintSet = false;
	/** World time the power came / went (spin-up, wind-down). */
	double PowerChangedAt = -1000.0;
	bool bOwnerKnown = false;
	float ObserveSeconds = 0.f;
	float ObserveTimer = 0.f;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HumAudio;
	float ShotCooldown = 0.f;
	int32 ShotsFired = 0;
	TWeakObjectPtr<AActor> LastTarget;
};
