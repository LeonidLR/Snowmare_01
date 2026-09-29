#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Interactables/DeployableRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/ExposedZones.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/TurnBasedRules.h"
#include "TurnBasedCombatSubsystem.generated.h"

class AOperativeCharacter;
class UMaterialInterface;
class UMeshComponent;
class ABarricadeActor;
class ATurnGridOverlayActor;
class UGorkyGridManager;
class UWeaponDataAsset;

/** Whose part of the round is running. */
UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
	Inactive,
	Squad,
	Turrets,
	Enemies
};

/** State of one unit on the grid (Godot unit_states entry). */
struct CODEXTACTICS_API FTurnUnitState
{
	TWeakObjectPtr<AActor> Actor;
	FIntPoint GridPos = FIntPoint::ZeroValue;
	EGorkyFacing Facing = EGorkyFacing::South;
	int32 AP = 0;
	int32 MaxAP = 0;
	float Armor = 0.f;
	float BaseDamage = 0.f;
	EOperativeStance Stance = EOperativeStance::Standing;
	bool bHasAttacked = false;
	bool bSquad = false;
};

/** Outcome of an attack order. */
struct CODEXTACTICS_API FTurnAttackResult
{
	bool bSuccess = false;
	bool bHit = false;
	int32 Damage = 0;
	float HitChance = 0.f;
	bool bBarrelExploded = false;
	FString Reason;
};

/** Godot can_place_tactical_deployable result. */
struct CODEXTACTICS_API FTurnDeployCheck
{
	bool bCanPlace = false;
	/** Walk + assembly (turret / barricade 3, mine 2). */
	int32 APCost = 0;
	TArray<FIntPoint> Path;
	FIntPoint StandCell = FIntPoint(-1, -1);
	FString Reason;
};

/** Original materials of one mesh of an enemy in stasis. */
USTRUCT()
struct FTurnStasisMesh
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UMeshComponent> Mesh;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> Materials;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTurnBasedStateChanged);

/**
 * Gorky 17 turn-based combat on a 14 x 14 grid around the leader. Starts when the game flow enters TurnBased (Space
 * hold), ends on victory / defeat or when the player leaves it. Rounds: squad (each operative 8 AP: move 1 / diagonal
 * 2, stance 1, turn 1, one attack 3), turrets, enemies (walk to an orthogonal neighbour, bite for 2 AP, step back).
 * Everything outside the grid is frozen for the duration; enemies left outside the fight get the stasis look
 * (M_TacticalStasis, Godot _apply_stasis_visuals_to_enemy). Messages go to the feed as «GORKY 17».
 * Godot reference: Scripts/tactics/turn_based_combat_manager.gd (start_combat, move_active_unit_to,
 * set_active_unit_stance, turn_active_unit_facing, attack_target_cell, end_current_unit_turn, _execute_turret_phase,
 * _execute_single_enemy_turn, _enemy_perform_attack / retreat, _detonate_barrel, _detonate_mine, end_combat;
 * exposed zones: _end_squad_phase, _on_zone_warning_updated, _register_reinforcement_enemy + FExposedZones).
 * Not ported yet: companion drone phase, barricade relocation / deployables on the grid,
 * weapon switching, grenades, enemy cold / DoT, cinematic cameras.
 */
UCLASS()
class CODEXTACTICS_API UTurnBasedCombatSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !Movers.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsActive() const { return Phase != ETurnPhase::Inactive; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	ETurnPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	AOperativeCharacter* GetActiveUnit() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsUnitMoving() const { return bSquadUnitMoving; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	int32 GetRound() const { return Round; }

	const FTurnUnitState* GetUnitState(const AActor* Actor) const;
	UGorkyGridManager* GetGrid() const { return Grid; }
	int32 GetEnemyCount() const { return Enemies.Num(); }
	int32 GetSquadCount() const { return Squad.Num(); }
	/** Meshes currently shown with the stasis material (enemies outside the fight). */
	int32 GetStasisMeshCount() const { return StasisMeshes.Num(); }

	/** Material of enemies outside the fight (Godot Shaders/tactical_stasis_enemy.gdshader). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|TurnBased")
	TObjectPtr<UMaterialInterface> StasisMaterial;
	/** Exposed-zone counters of the running fight (quadrant turns, breaches). */
	const FExposedZones& GetExposedZones() const { return Zones; }

	// --- Player orders (active operative) ---

	bool SelectUnit(AOperativeCharacter* Unit);
	bool MoveActiveUnitTo(const FIntPoint& Cell);
	bool SetActiveUnitStance(EOperativeStance NewStance);
	EOperativeStance CycleActiveUnitStance();
	bool TurnActiveUnitFacing(EGorkyFacing NewFacing);
	/** R: turn 90° clockwise (1 AP). */
	bool RotateActiveUnitClockwise();
	FTurnAttackResult AttackCell(const FIntPoint& Cell, bool bGuaranteeHit = false);
	/** Tab: next operative (the last one ends the squad phase). */
	void EndCurrentUnitTurn();
	/** Enter: the whole squad ends its turn. */
	void PassSquadTurn();
	/** Godot switch_active_unit_weapon_to: the active operative takes another arsenal weapon (free); attack cells follow. */
	bool SwitchActiveUnitWeapon(const FString& WeaponId);

	/**
	 * Mouse click in turn-based mode (Godot main.gd _handle_gorky17_tactical_click): select an operative, attack an
	 * enemy / barricade, walk; a barrel next to the operative (or an adjacent turret) is picked up for relocation unless
	 * Shift is held (Shift + barrel = shot). While relocating, the click picks the target cell.
	 */
	void HandleWorldClick(const FVector& WorldPoint, AActor* HitActor, bool bShift = false);

	// --- Object relocation (barrels, turrets, barricades) ---

	/**
	 * Godot _start_tactical_relocate: the object waits for a target cell (shown as the reachable layer, PushBarrelAPCost
	 * each; needs that many AP). Barrels / turrets: free orthogonal neighbours. Barricades: every cell within 2 of the
	 * operative where the barricade fits at the current rotation (RotateRelocation, 45° steps).
	 */
	bool StartRelocate(AActor* Object);
	/** Godot R / E (+45°) and Q (-45°) while relocating; barricades refresh their target cells. */
	void RotateRelocation(int32 Steps);
	float GetRelocateYaw() const { return RelocateYaw; }
	bool IsRelocatingBarricade() const;
	/** Godot _cancel_tactical_relocate (Esc / RMB / click on the object's own cell). */
	void CancelRelocate();
	bool IsRelocating() const { return RelocateTarget.IsValid(); }
	AActor* GetRelocatingObject() const { return RelocateTarget.Get(); }
	/** Target cells of the running relocation and their AP cost. */
	const TMap<FIntPoint, int32>& GetRelocateCells() const { return RelocateCells; }
	/**
	 * Godot relocate_object: the active operative, standing next to the object (diagonal counts), pushes it along the
	 * grid path to Target; the operative takes the object's previous cells. CustomAPCost < 0 = PushBarrelAPCost per step.
	 */
	bool RelocateObject(const FIntPoint& ObjectCell, const FIntPoint& Target, int32 CustomAPCost = -1);
	/** Godot _try_push_adjacent_barrel (panel «Бочка»): relocation of an orthogonally adjacent barrel. */
	bool TryPushAdjacentBarrel();
	/** Godot is_unit_adjacent_to_object: Chebyshev distance 1 to any cell of the object. */
	bool IsUnitAdjacentToObject(const AActor* Unit, const AActor* Object) const;

	/**
	 * Godot get_barricade_cells_at: five samples along the barricade's long axis (±0.48 of its length, halves, centre)
	 * around the centre of Cell, rotated by Yaw.
	 */
	TArray<FIntPoint> GetBarricadeCellsAt(const AActor* Barricade, const FIntPoint& Cell, float Yaw) const;
	/** Godot can_place_barricade_at: a cell of the new footprint next to the operative, none on him or on another occupant. */
	bool CanPlaceBarricadeAt(const AActor* Barricade, const FIntPoint& Cell, float Yaw) const;
	/** Godot relocate_barricade: moves / rotates the barricade in one action (CustomAPCost < 0 = PushBarrelAPCost). */
	bool RelocateBarricade(AActor* Barricade, const FIntPoint& Cell, float Yaw, int32 CustomAPCost = -1);

	// --- Deployables on the grid ---

	/**
	 * Godot can_place_tactical_deployable: free cells (barricade: its rotated footprint), assembly 3 AP (mine 2) and, when
	 * the operative is not next to it yet, the cheapest walk to a free neighbour cell within the AP left.
	 */
	FTurnDeployCheck CanPlaceDeployable(EDeployableType Type, const FIntPoint& Cell, float Yaw) const;
	/** Godot deploy_tactical_object: walk up if needed, then register the spawned object on the grid and pay the AP. */
	bool DeployObject(EDeployableType Type, const FIntPoint& Cell, float Yaw, AActor* Spawned);
	/**
	 * Godot main.gd _handle_tactical_deployable_placement: click while placing a turret / barricade / mine in turn-based
	 * combat. A squad mate hands the item over when the active operative has none; the item is spent on success.
	 * Returns true when the placement mode should end.
	 */
	bool HandleDeployPlacement(EDeployableType Type, const FVector& WorldPoint, float Yaw);

	/** Turn-based balance (Godot tactical_* defaults). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|TurnBased")
	FTurnBasedBalance Balance;

	/** Every attack hits (headless checks; Godot guarantee_all_hits). */
	bool bGuaranteeAllHits = false;

	/** Steps are animated over this many seconds (Godot tactical_step_duration 0.52 / enemy 0.48; x1.414 diagonal). */
	float SquadStepDuration = 0.52f;
	float EnemyStepDuration = 0.48f;

	/** Something changed (HUD refresh). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|TurnBased")
	FOnTurnBasedStateChanged OnStateChanged;

private:
	struct FMover
	{
		TWeakObjectPtr<AActor> Actor;
		TArray<FVector> Points;
		TArray<EGorkyFacing> Facings;
		int32 Index = 0;
		float Alpha = 0.f;
		float StepDuration = 0.5f;
		FVector From = FVector::ZeroVector;
		TFunction<bool(int32)> OnStep;
		TFunction<void()> OnDone;
		/** Units turn towards each step; pushed objects keep their rotation. */
		bool bFaceSteps = true;
	};

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase NewPhase, ECodexCombatMode CombatMode);

	void StartCombat();
	void EndCombat(bool bVictory, bool bLeaveFlow);
	void FreezeWorld(const TSet<AActor*>& OnGrid);
	void RestoreWorld();
	void BakeObstacles(const TArray<AActor*>& Ignore);
	void PlaceOnCell(AActor* Actor, const FIntPoint& Cell) const;
	void AlignFacing(AActor* Actor, EGorkyFacing Facing) const;
	float GetHealth(const AActor* Actor) const;
	void ApplyDamage(AActor* Victim, float Amount, const FString& Source);
	bool IsDead(const AActor* Actor) const;
	FString NameOf(const AActor* Actor) const;
	const UWeaponDataAsset* WeaponOf(const AActor* Actor) const;
	void Log(const FString& Message) const;
	/** Feed line from another sender (Godot _on_quest_message("ТАКТИКА", ...)). */
	void Post(const FString& Sender, const FString& Message) const;
	void Highlight(AActor* Target) const;
	void Changed();

	void StartPlayerTurn();
	void RefreshOverlay();
	void EndSquadPhase();
	/** Godot _end_squad_phase zone check: counters, warnings, overlay outlines, reinforcements. */
	void UpdateExposedZones();
	/** Godot _register_barricade_cells: the footprint samples at the barricade's current place and rotation. */
	void RegisterBarricadeCells(AActor* Barricade);
	/** Godot _refresh_tactical_barricade_cost_map. */
	void RefreshBarricadeTargets();
	/** Godot register_tactical_turret / _barricade / _mine. */
	void RegisterDeployable(EDeployableType Type, AActor* Object, const FIntPoint& Cell, float Yaw);
	/** Free cells of a quadrant for a reinforcement group, outer edge first (Godot find_spawn_cells_in_quadrant). */
	TArray<FIntPoint> FindSpawnCells(int32 Quadrant, int32 Count) const;
	/** Godot _register_reinforcement_enemy. */
	void RegisterReinforcement(AActor* Enemy, const FIntPoint& Cell, const FString& QuadrantName);
	void ExecuteTurretPhase();
	void ProcessNextTurret();
	void ExecuteEnemyPhase();
	void ProcessNextEnemy();
	void ExecuteEnemyTurn(AActor* Enemy);
	void EnemyAttack(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos);
	void EnemyRetreat(AActor* Enemy, const FIntPoint& TargetPos);
	void FinishEnemyTurn(float Delay);
	TSet<FIntPoint> GetFearCells() const;

	void DetonateBarrel(const FIntPoint& Cell, AActor* Barrel);
	void DetonateMine(const FIntPoint& Cell, AActor* Mine, AActor* Victim);
	void OnEnemyKilled(AActor* Enemy, const FIntPoint& Cell);
	void OnSquadMemberKilled(AActor* Member, const FIntPoint& Cell);
	bool CheckBattleEnd();

	void StartMover(AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path, float StepDuration, TFunction<bool(int32)> OnStep,
		TFunction<void()> OnDone, bool bFaceSteps = true);
	void After(float Seconds, TFunction<void()> Callback);

	UPROPERTY(Transient)
	TObjectPtr<UGorkyGridManager> Grid;

	UPROPERTY(Transient)
	TObjectPtr<ATurnGridOverlayActor> Overlay;

	TMap<TWeakObjectPtr<AActor>, FTurnUnitState> States;
	TArray<TWeakObjectPtr<AOperativeCharacter>> Squad;
	TArray<TWeakObjectPtr<AActor>> Enemies;
	TArray<TWeakObjectPtr<AActor>> Turrets;
	TArray<TWeakObjectPtr<AActor>> EnemyQueue;
	TArray<TWeakObjectPtr<AActor>> TurretQueue;
	TMap<TWeakObjectPtr<AActor>, int32> BurningBarrels;
	TArray<TWeakObjectPtr<AActor>> FrozenActors;
	TArray<FMover> Movers;
	FExposedZones Zones;

	UPROPERTY(Transient)
	TArray<FTurnStasisMesh> StasisMeshes;
	TWeakObjectPtr<AActor> RelocateTarget;
	FIntPoint RelocateOrigin = FIntPoint(-1, -1);
	TMap<FIntPoint, int32> RelocateCells;
	float RelocateYaw = 0.f;

	ETurnPhase Phase = ETurnPhase::Inactive;
	int32 ActiveIndex = 0;
	int32 Round = 0;
	bool bSquadUnitMoving = false;
	/** Invalidates pending timers of a finished combat. */
	int32 CombatId = 0;
};
