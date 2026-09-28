#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/TurnBasedRules.h"
#include "TurnBasedCombatSubsystem.generated.h"

class AOperativeCharacter;
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTurnBasedStateChanged);

/**
 * Gorky 17 turn-based combat on a 14 x 14 grid around the leader. Starts when the game flow enters TurnBased (Space
 * hold), ends on victory / defeat or when the player leaves it. Rounds: squad (each operative 8 AP: move 1 / diagonal
 * 2, stance 1, turn 1, one attack 3), turrets, enemies (walk to an orthogonal neighbour, bite for 2 AP, step back).
 * Everything outside the grid is frozen for the duration. Messages go to the feed as «GORKY 17».
 * Godot reference: Scripts/tactics/turn_based_combat_manager.gd (start_combat, move_active_unit_to,
 * set_active_unit_stance, turn_active_unit_facing, attack_target_cell, end_current_unit_turn, _execute_turret_phase,
 * _execute_single_enemy_turn, _enemy_perform_attack / retreat, _detonate_barrel, _detonate_mine, end_combat).
 * Not ported yet: exposed-zone reinforcements, companion drone phase, barricade relocation / deployables on the grid,
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

	/** Mouse click in turn-based mode: select an operative, attack an enemy / barrel / barricade, or walk. */
	void HandleWorldClick(const FVector& WorldPoint, AActor* HitActor);

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
	void Changed();

	void StartPlayerTurn();
	void RefreshOverlay();
	void EndSquadPhase();
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
		TFunction<void()> OnDone);
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

	ETurnPhase Phase = ETurnPhase::Inactive;
	int32 ActiveIndex = 0;
	int32 Round = 0;
	bool bSquadUnitMoving = false;
	/** Invalidates pending timers of a finished combat. */
	int32 CombatId = 0;
};
