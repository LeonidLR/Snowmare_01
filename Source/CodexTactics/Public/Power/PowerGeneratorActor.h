#pragma once

#include "CoreMinimal.h"
#include "Interactables/InteractableActor.h"
#include "Power/PowerRules.h"
#include "PowerGeneratorActor.generated.h"

class AOperativeCharacter;
class ATurretActor;
class UPointLightComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPowerGeneratorChanged, APowerGeneratorActor*, Generator, EGeneratorState, NewState);

/**
 * A level diesel generator of the turret power network (docs/design/TURRET_POWER.md phase 1, designer decisions 2026-11-17): feeds
 * its 1-4 linked level turrets by cable (max 40 m) while it runs; the owner of the network is whoever started it (the squad =
 * friendly, a PMC team = hostile, nobody = neutral). States Dry / Fuelled / Running / Broken (PowerRules::DeriveState); a running one
 * burns litres per nominal hour (frozen in the tactical pause and turn-based), warns at 60 / 15 s of fuel, stops when dry. The panel
 * (the action menu) offers one action by state: Refuel (a diesel jerrycan), Start (loud: patrols within 30 m investigate), Stop,
 * Seize (stop + restart for the worker's side: the network flips) and Repair. Opening the panel identifies the network's owner.
 * Monsters attack it only while it runs; gunfire / blasts / claws break it at 0 HP (half the fuel lost, owner cleared).
 * Not the checkpoint quest generator (that one stays an AInteractableActor with ObjectType Generator, legacy mode).
 * UE-only; no Godot reference (Godot generators were quest objects only).
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API APowerGeneratorActor : public AInteractableActor
{
	GENERATED_BODY()

public:
	APowerGeneratorActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// --- AInteractableActor ---
	virtual bool IsQuestGenerator() const override { return false; }
	virtual bool IsAttackableGenerator() const override;
	virtual void TakeGeneratorDamage(float Amount) override;
	virtual void RestoreGeneratorState(float Health, bool bBroken) override;
	virtual bool HandleDirectInteraction(AOperativeCharacter* Leader) override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void PerformAction(AOperativeCharacter* User) override;
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const override;

	/** Dry / Fuelled / Running / Broken. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Power")
	EGeneratorState GetState() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Power")
	bool IsRunning() const { return GetState() == EGeneratorState::Running; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Power")
	float GetFuelL() const { return FuelL; }

	/** The team that started it (PowerRules::NoOwner while it does not run). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Power")
	int32 GetOwnerTeam() const { return IsRunning() ? OwnerTeam : PowerRules::NoOwner; }

	/** The network's side seen from the squad. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Power")
	EPowerAllegiance GetAllegiance() const { return PowerRules::AllegianceOf(GetOwnerTeam()); }

	/** Linked turrets that are intact (the burn load while running). */
	int32 GetLiveTurretCount() const;

	/** Every linked turret still in the world. */
	TArray<ATurretActor*> GetLinkedTurrets() const;

	/** World seconds of fuel left at the current load. */
	float GetSecondsLeft() const;

	/** Squad knowledge: the network's owner was read (the panel, or an observed turret of it). */
	bool IsOwnerKnown() const { return bOwnerKnown; }

	/** Links Turret to this network (it leaves any other one); refused beyond MaxTurrets. Its power / side follow at once. */
	bool LinkTurret(ATurretActor* Turret);

	/** Links the nearest unlinked level turrets within the plug range (a hand-placed generator; generated maps link by the props file). */
	int32 AutoLinkNearbyTurrets();

	/** A level start state (the props spawner, before FinishSpawning; smokes may call it any time). */
	void InitLevelGenerator(EGeneratorStart Start, TOptional<float> InFuelL = TOptional<float>());

	/**
	 * The squad reads the network (the panel, an observed turret): every linked turret shows its owner. Posts a feed line when it was
	 * unknown before (bAnnounce).
	 */
	void RevealNetwork(bool bAnnounce);

	// --- Direct control (the timed actions end here; smokes / scripts call them too) ---

	/** Pours one jerrycan in (no carry check). */
	void AddFuel(float Litres);
	/** Starts for Team: the owner, the turrets spin up, the start noise. False when it cannot (broken / dry / running). */
	bool StartBy(int32 Team, const AActor* Starter = nullptr);
	/** Stops it (the turrets wind down); the owner is cleared. */
	void StopEngine(const AActor* Stopper = nullptr, bool bAnnounce = true);
	/** Full repair: intact and off, owner cleared. */
	void RepairNow(AActor* Repairer = nullptr);

	/** The timed action in progress (None when idle). */
	EGeneratorAction GetActionInProgress() const { return ActionInProgress; }

	/** Enemies the last start's noise sent investigating (smokes). */
	int32 GetLastNoiseListeners() const { return LastNoiseListeners; }

	/** Starts this game (smokes / telemetry). */
	int32 GetStartCount() const { return StartCount; }

	/** Squad contact flag (a pre-powered generator burns only after it). */
	bool HasContact() const { return bContact; }

	/** State changes (Blueprint visuals / sound). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Power")
	FOnPowerGeneratorChanged OnPowerStateChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Power", meta = (DisplayName = "On Power State Changed"))
	void ReceivePowerStateChanged(EGeneratorState NewState);

	/** Amber work lamp: on while it runs (placeholder look; no asset). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Power")
	TObjectPtr<UPointLightComponent> WorkLamp;

	/** Start state used at BeginPlay when no spawner set one (a hand-placed generator). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Power")
	EGeneratorStart StartState = EGeneratorStart::Dry;

	/** Link the nearest unlinked turrets at BeginPlay (hand-placed only; a spawner links by the props file and clears it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Power")
	bool bAutoLinkTurrets = true;

private:
	/** Begins a timed action of User (crouched at the panel); it ends in FinishAction unless interrupted. */
	void BeginAction(AOperativeCharacter* User, EGeneratorAction Action);
	void TickAction();
	void FinishAction();
	void CancelAction(const FText& Reason);
	/** Pushes power + side to every linked turret, the lamp, the heat and the broadcast. */
	void RefreshNetwork();
	void Breakdown();
	void EmitStartNoise(const AActor* Source);
	bool IsTurnBased() const;
	FText DescribeNetwork() const;
	void Announce(const FText& Text) const;

	float FuelL = 0.f;
	bool bSwitchOn = false;
	int32 OwnerTeam = PowerRules::NoOwner;
	bool bContact = true;
	bool bOwnerKnown = false;
	bool bInitialised = false;
	EGeneratorState LastState = EGeneratorState::Dry;
	/** Index of the next low-fuel warning to post this run. */
	int32 NextWarning = 0;
	float ContactCheckTimer = 0.f;
	int32 LastNoiseListeners = 0;
	int32 StartCount = 0;

	TArray<TWeakObjectPtr<ATurretActor>> Linked;

	EGeneratorAction ActionInProgress = EGeneratorAction::None;
	TWeakObjectPtr<AOperativeCharacter> Worker;
	float ActionElapsed = 0.f;
	float ActionDuration = 0.f;
	bool bSeizeStopped = false;
	FTimerHandle ActionTimer;
};
