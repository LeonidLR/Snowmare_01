#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactables/ActionMenuTypes.h"
#include "Interactables/DeployableRules.h"
#include "Interactables/TrapApproachRules.h"
#include "Data/CombatTypes.h"
#include "Quests/QuestChain.h"
#include "InteractableActor.generated.h"

class AOperativeCharacter;
class UBoxComponent;
struct FOverheadLabel;
class UHeatSourceComponent;
class UStaticMeshComponent;

/** Which Godot texts a trap uses (interactable.gd / barricade.gd / mine.gd). */
enum class ETrapFlavor : uint8
{
	Object,
	Barricade,
	Mine,
	/** loot_crate.gd */
	Crate,
	/** deployables/turret.gd */
	Turret,
	/** A PMC tripwire placed in the level (ATripwireActor, Demo 01 G3; UE-only texts). */
	Tripwire
};

/**
 * Interactive object. Clicking it sends the squad leader to it; on arrival the object's action menu opens
 * (BuildActionMenu) and the confirm button runs ExecuteAction. The base class is the checkpoint quest object
 * (gate terminal, canister, abandoned APC, backup generator, gate) routed through UQuestSubsystem; a generator
 * carries a heat source that turns on when it starts. Subclasses (barrels, deployables) override the menu.
 * Every object can carry a trap (grenade tripwire): confirming the menu of a trapped object is a defusal attempt,
 * a failed one may blow it up (enemies and nearby operatives take blast damage).
 * Godot reference: Scenes/movements/interactable.gd (trap, attempt_defusal, detonate_trap),
 * main.gd `_trigger_menu_for_object` / `_on_action_confirmed` / `_execute_trap_object`.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API AInteractableActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractableActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Runs the quest interaction for the operative that reached the object. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Interactables")
	void Interact(AOperativeCharacter* User);

	/** Acts at once without a menu when it returns true (e.g. opening an intact supply crate). */
	virtual bool HandleDirectInteraction(AOperativeCharacter* Leader) { return false; }

	// --- Generator damage (Godot interactable.gd take_damage / breakdown_generator / repair_generator) ---

	/** Generator durability (Godot max_health 200). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Generator", meta = (ClampMin = "1"))
	float GeneratorMaxHealth = 200.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Generator")
	float GeneratorHealth = 200.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Generator")
	bool bGeneratorBroken = false;

	/** Damage to a generator (enemies); at 0 it breaks down, heat and turret power go off. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Generator")
	void TakeGeneratorDamage(float Amount);

	/** Full repair: heat and turret power back on. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Generator")
	void RepairGenerator();

	/** Save-game load (after the quest chain): generator durability and breakdown; its heat follows (running and intact). */
	void RestoreGeneratorState(float Health, bool bBroken);

	/** Menu (or feed line) for the leader standing at the object. */
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const;

	/** Confirm button of the menu: a defusal attempt while trapped, otherwise PerformAction. */
	virtual void ExecuteAction(AOperativeCharacter* User);

	/** The object's own action (quest step, lighting a barrel...). */
	virtual void PerformAction(AOperativeCharacter* User);

	// --- Trap (Godot "⚠️ explosive trap") ---

	/** Trapped with a tripwire charge: needs defusal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap")
	bool bTrapped = false;

	/** Blast damage of the trap (Godot trap_damage 85; a grenade sets 85). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap", meta = (ClampMin = "0"))
	float TrapDamage = 85.f;

	/** Blast radius, cm (Godot trap_radius 3.5 m; a grenade sets 4 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap", meta = (ClampMin = "0"))
	float TrapRadius = 350.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Trap")
	int32 FailedDefusalAttempts = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Trap")
	bool bDefusalWarned = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Trap")
	bool bDefused = false;

	/** Defusal odds for Operative on this trap (a PMC tripwire: always in its crouched work stance). */
	virtual FDefusalChance GetDefusalChance(const AOperativeCharacter* Operative) const;

	/** Smokes only: forced rolls (0..100) of the next defusal attempts instead of random ones (< 0 = random). */
	float DefusalRollForTesting = -1.f;
	float DefusalExplosionRollForTesting = -1.f;

	/** One defusal attempt: resolves it, posts the Godot line and may detonate. */
	EDefusalResult AttemptDefusal(AOperativeCharacter* Operative);

	/** The trap goes off (failed defusal, enemy contact, shot). */
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty());

	/** Arms a trap with one of Operative's grenades (Godot _execute_trap_object). */
	bool TrapWithGrenade(AOperativeCharacter* Operative);

	/** Can a grenade trap be set here now ("Set trap" button). */
	virtual bool CanReceiveTrap() const { return !bTrapped; }

	// --- Disarm pickup (rev 2026-10-19, game designer: "the mine disappears first, then he bends down") ---

	/**
	 * Disarmed and being picked up: the pickup clip plays and the trap stays (harmless: it can not go off, no menu) until the clip's
	 * grab moment (FOperativeInteractionClip::EffectFraction of AM_Interact_PickupGround), then it goes (CompleteDisarmPickup).
	 */
	bool IsAwaitingPickup() const { return bAwaitingPickup; }

	/** Seconds the last disarm pickup waited for its grab moment (smokes). */
	float GetLastPickupDelaySeconds() const { return LastPickupDelaySeconds; }

	/** World label above the object (Godot overhead Label3D); false = none. The generator shows its state and HP. */
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	virtual ETrapFlavor GetTrapFlavor() const { return ETrapFlavor::Object; }

	// --- Hidden traps (Demo 01 rev 5h, every level): a level mine / PMC tripwire the squad has not spotted yet ---

	/** A live trap the squad does not know of yet (a corner peek can reveal it, AOperativeCharacter::UpdatePeekTrapSpotting). */
	virtual bool IsHiddenTrap() const { return false; }

	/** Reveals a hidden trap as spotted by Spotter's corner peek (the normal spot response; the spotter keeps his pose). */
	virtual void RevealByPeek(AOperativeCharacter* Spotter) {}

	/**
	 * Rev 5h: a group name for tutorial steps / scripts: spotting this trap also raises "TrapSpotted.<tag>" and counts in the
	 * tutorial condition stat "traps_spotted.<tag>" (Demo 01: A3PeekMines = the mines beyond the A3 peek corner). Empty = none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap")
	FName SpotEventTag;

	/**
	 * Rev 5i (game designer 2026-10-15): a tutorial step id - until that step has started, this hidden trap cannot be spotted by
	 * walking near it, only by a corner peek (Demo 01: the A3 lesson mines, "A3_peek"). Ignored without an active tutorial or when
	 * the steps file has no such step. Empty = the normal proximity rule. Set by the layout script (section 5i).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap")
	FName SpotRequiresStep;

	/** The proximity scan may spot this trap now (SpotRequiresStep, TrapSpotRules::AllowsProximitySpot); the peek always may. */
	bool CanBeSpottedByProximity() const;

	/** Blueprint hook for explosion VFX / sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Trap", meta = (DisplayName = "On Exploded"))
	void ReceiveExploded(float Radius);

	/** "Operator: Name (Stance: Crouched) | Success chance: ~70%" (Godot defusal status line). */
	FText DescribeDefusal(const AOperativeCharacter* Operative) const;

protected:
	/**
	 * Blast around the object: enemies take EnemyDamage (EnemyFalloff, DamageType), operatives SquadDamage
	 * (SquadFalloff) and post SquadLine formatted with the HP lost.
	 */
	void ApplyBlast(float EnemyDamage, float SquadDamage, float Radius, float ArmorPenetration, EDamageType DamageType,
		const FText& Source, const FText& SquadLine, EStatusEffect Status = EStatusEffect::None, float StatusDuration = 0.f,
		float StatusTickDamage = 0.f);

	/**
	 * APC P4: the vehicle that set this charge off itself (a mine / tripwire under its wheels): ApplyBlast gives it the
	 * flat mine damage instead of the blast falloff (UVehicleSubsystem::ApplyBlastToVehicles).
	 */
	TWeakObjectPtr<AActor> BlastTriggeredByVehicle;

	/** Posts a feed line. */
	void PostLine(const FText& Speaker, const FText& Text) const;

	/**
	 * Rev 5h: the squad's side of a spotted hidden trap - everyone stops and the Solo switch (USquadSubsystem::HoldForSpottedTrap),
	 * then the events "TrapSpotted" / "TrapSpotted.Peek" / "TrapSpotted.<SpotEventTag>" (Subject = the spotter).
	 */
	void NotifyTrapSpotted(AOperativeCharacter* Spotter, bool bByPeek, const FText& TrapName);

	/**
	 * A successful disarm: User plays the pickup clip by ClipStance (standing: full body, crouched: upper body, prone: none) and
	 * CompleteDisarmPickup runs at its grab moment - at once without a clip, after the release in the tactical pause. A cut clip
	 * (knockdown, an order) does not stop it: the charge was disarmed, it still goes at the grab time.
	 */
	void BeginDisarmPickup(AOperativeCharacter* User, EOperativeStance ClipStance);

	/** The grab moment of the disarm pickup: the trap's effect (kit, feed, events) and its removal. User may be null / down. */
	virtual void CompleteDisarmPickup(AOperativeCharacter* User) {}

	/** While true, AttemptDefusal keeps its success line for PostDeferredDefusalLine (posted at the grab, after the clip). */
	bool bDeferDefusalSuccessLine = false;

	/** Posts the success line AttemptDefusal kept back (no-op when none). */
	void PostDeferredDefusalLine();

public:

	/** Godot can_be_relocated: the object can be pushed / carried to a new spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	bool bCanBeRelocated = false;

	/** Distance from Location to the object's collision box, cm (0 inside). */
	float GetDistanceTo(const FVector& Location) const;

	/** Nearest reachable point next to the object for an operative coming from FromLocation. */
	FVector GetApproachPoint(const FVector& FromLocation) const;

	// --- Disarm approach (game designer 2026-10-15, every level; TrapApproachRules): a click on a live trap walks the operative right
	// up to it (DisarmStandOffCm from the charge / the wire line, on his side - never onto the mine), facing it; the menu opens there. ---

	/** The operative's root ends this far from the charge / the wire line, cm (data per instance / Blueprint). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap", meta = (ClampMin = "0"))
	float DisarmStandOffCm = TrapApproachRules::DefaultStandOffCm;

	/** The disarm spot counts as reached within the stand-off + this, cm (the move's acceptance radius is 25 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Trap", meta = (ClampMin = "0"))
	float DisarmReachToleranceCm = TrapApproachRules::DefaultReachToleranceCm;

	/** A click on this object now means walking up to disarm it (a spotted live mine, a laid wire). */
	virtual bool UsesDisarmApproach() const { return false; }

	/** The point of the charge nearest to From (a mine: its centre; a wire: the closest point of the wire). */
	virtual FVector GetDisarmFocusPoint(const FVector& From) const { return GetActorLocation(); }

	/** Where the operative stands to disarm it (TrapApproachRules::MineApproachPoint around the focus point; Z of From). */
	virtual FVector GetDisarmApproachPoint(const FVector& From) const;

	/** The interaction walk's goal: the disarm spot for a trap, else GetApproachPoint. */
	FVector GetInteractionApproachPoint(const FVector& From) const;

	/**
	 * Close enough for the menu / the action: the disarm spot for a trap (bStillWalking: he walks on until the stand-off + 10 cm, see
	 * TrapApproachRules::IsAtDisarmSpot), else within InteractionDistance of the box.
	 */
	bool IsInInteractionReach(const FVector& Location, bool bStillWalking = false) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	EInteractableType ObjectType = EInteractableType::GateTerminal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	FText DisplayName;

	/** Interaction happens once an operative is this close to the object box, cm (architect spec: 150). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables", meta = (ClampMin = "0"))
	float InteractionDistance = 150.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** Warm zone, used by generators. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Interactables")
	TObjectPtr<UHeatSourceComponent> HeatSource;

private:
	void StartDisarmPickupClip();
	void FinishDisarmPickup();
	/** Posts the defusal success line now or keeps it (bDeferDefusalSuccessLine). */
	void PostDefusalSuccess(const FText& Speaker, const FText& Line);

	UFUNCTION()
	void HandlePickupPauseReleased();

	bool bAwaitingPickup = false;
	bool bPickupWaitsForRelease = false;
	TWeakObjectPtr<AOperativeCharacter> PickupUser;
	EOperativeStance PickupStance = EOperativeStance::Standing;
	FTimerHandle PickupTimer;
	float LastPickupDelaySeconds = 0.f;
	FText DeferredDefusalSpeaker;
	FText DeferredDefusalLine;

	/** Godot EventBus.generator_state_changed. */
	void BroadcastGeneratorState(bool bPowered) const;

	UFUNCTION()
	void HandleGeneratorStarted();

	void BreakdownGenerator();
	void FinishGeneratorRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser);
	/** Generator is running (quest) and has not broken down. */
	bool IsGeneratorWorking() const;
};
