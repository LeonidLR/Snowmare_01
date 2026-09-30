#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactables/ActionMenuTypes.h"
#include "Interactables/DeployableRules.h"
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
	Turret
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

	/** Menu (or feed line) for the leader standing at the object. */
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const;

	/** Confirm button of the menu: a defusal attempt while trapped, otherwise PerformAction. */
	virtual void ExecuteAction(AOperativeCharacter* User);

	/** The object's own action (quest step, lighting a barrel...). */
	virtual void PerformAction(AOperativeCharacter* User);

	// --- Trap (Godot «⚠️ Взрывная ловушка») ---

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

	/** Defusal odds for Operative on this trap. */
	FDefusalChance GetDefusalChance(const AOperativeCharacter* Operative) const;

	/** One defusal attempt: resolves it, posts the Godot line and may detonate. */
	EDefusalResult AttemptDefusal(AOperativeCharacter* Operative);

	/** The trap goes off (failed defusal, enemy contact, shot). */
	virtual void DetonateTrap(bool bByShot = false, const FText& InstigatorName = FText::GetEmpty());

	/** Arms a trap with one of Operative's grenades (Godot _execute_trap_object). */
	bool TrapWithGrenade(AOperativeCharacter* Operative);

	/** Can a grenade trap be set here now («Заминировать» button). */
	virtual bool CanReceiveTrap() const { return !bTrapped; }

	/** World label above the object (Godot overhead Label3D); false = none. The generator shows its state and HP. */
	virtual bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	virtual ETrapFlavor GetTrapFlavor() const { return ETrapFlavor::Object; }

	/** Blueprint hook for explosion VFX / sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Trap", meta = (DisplayName = "On Exploded"))
	void ReceiveExploded(float Radius);

	/** «Исполнитель: Имя (Поза: Присев) | Шанс успеха: ~70%» (Godot defusal status line). */
	FText DescribeDefusal(const AOperativeCharacter* Operative) const;

protected:
	/**
	 * Blast around the object: enemies take EnemyDamage (EnemyFalloff, DamageType), operatives SquadDamage
	 * (SquadFalloff) and post SquadLine formatted with the HP lost.
	 */
	void ApplyBlast(float EnemyDamage, float SquadDamage, float Radius, float ArmorPenetration, EDamageType DamageType,
		const FText& Source, const FText& SquadLine, EStatusEffect Status = EStatusEffect::None, float StatusDuration = 0.f,
		float StatusTickDamage = 0.f);

	/** Posts a feed line. */
	void PostLine(const FText& Speaker, const FText& Text) const;

public:

	/** Godot can_be_relocated: the object can be pushed / carried to a new spot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Interactables")
	bool bCanBeRelocated = false;

	/** Distance from Location to the object's collision box, cm (0 inside). */
	float GetDistanceTo(const FVector& Location) const;

	/** Nearest reachable point next to the object for an operative coming from FromLocation. */
	FVector GetApproachPoint(const FVector& FromLocation) const;

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
	/** Godot EventBus.generator_state_changed. */
	void BroadcastGeneratorState(bool bPowered) const;

	UFUNCTION()
	void HandleGeneratorStarted();

	void BreakdownGenerator();
	void FinishGeneratorRepair(TWeakObjectPtr<AOperativeCharacter> WeakUser);
	/** Generator is running (quest) and has not broken down. */
	bool IsGeneratorWorking() const;
};
