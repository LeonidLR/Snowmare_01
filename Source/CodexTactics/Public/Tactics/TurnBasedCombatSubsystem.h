#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Combat/KnockdownRules.h"
#include "Interactables/DeployableRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tactics/ExposedZones.h"
#include "Tactics/Gorky17Types.h"
#include "Tactics/TurnBasedRules.h"
#include "TurnBasedCombatSubsystem.generated.h"

class AOperativeCharacter;
class AEnemyCharacter;
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
	/** The shot is decided but shown later (the shooter turns / kneels first): wait for !IsShotPending() for its effects. */
	bool bPending = false;
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
	/** Every frame of a fight (the held-enemy guard, relocation glides / hologram), and while anything still moves. */
	virtual bool IsTickable() const override { return IsActive() || !Movers.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsActive() const { return Phase != ETurnPhase::Inactive; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	ETurnPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	AOperativeCharacter* GetActiveUnit() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsUnitMoving() const { return bSquadUnitMoving; }

	/**
	 * Ground speed of Actor while it walks a turn-based path (cell size / step duration, the same for the whole path),
	 * or -1 when it is not walking one. The anim instances use it: the walk plays once over the whole path
	 * (Godot start_tactical_walk before the tween, force_idle after it), not per cell.
	 */
	float GetTacticalMoveSpeed(const AActor* Actor) const;

	/**
	 * Distance travelled and speed at Time on a trapezoid profile: accelerate over FirstLength, cruise, decelerate over
	 * LastLength, Total in TotalTime (the Godot sum of step durations). Pure; used by the movers and the tests.
	 */
	static void SampleWalkProfile(float Total, float FirstLength, float LastLength, float TotalTime, float Time,
		float& OutDistance, float& OutSpeed);

	/** An operative walks or a cinematic shot plays: player orders wait (Godot is_squad_unit_moving / is_dramatic_shot_active). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsBusy() const { return bSquadUnitMoving || bDramaticShotActive || PendingShot.IsSet(); }

	/** A grid shot was ordered and waits for the shooter's turn / kneel before it is shown (TurnAttackTimeline). */
	bool IsShotPending() const { return PendingShot.IsSet(); }

	/** Grid shots shown so far (smokes). */
	int32 GetGridShotsFired() const { return GridShotsFired; }

	/** A grid shot is shown now: fire clip started, tracer / hit / damage follow in the same frame (shooter, target). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGridShotFired, AOperativeCharacter*, AActor*);
	FOnGridShotFired OnGridShotFired;

	/** Sprint 14: the active operative is knocked down (falling / lying / getting up): no move, shot or stance order. */
	bool IsActiveUnitKnockedDown() const;

	/**
	 * An operative died (any cause; user decision 2026-10-08: only the commander's death ends the mission). Drops him
	 * from the turn order, the grid and the unit states; keeps the active index on the same living unit, or passes the
	 * turn on when the active operative himself died (the squad phase ends when he was the last). Idempotent.
	 */
	void NotifyOperativeKilled(AOperativeCharacter* Operative);

	/** A cinematic squad shot or turret volley is playing. */
	bool IsDramaticShotActive() const { return bDramaticShotActive; }

	// --- Attack mode (Godot is_attack_mode: F / the weapon selector; RMB, Esc, "MOVE" or a finished shot leave it) ---

	/** Weapon aim: the dot matrix of the weapon's cells replaces the green walk cells; empty cells do not walk. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	bool IsAttackMode() const { return bAttackMode; }

	/**
	 * AP an attack of the active operative costs now: AttackAPCost, plus StanceAPCost for a sniper rifle while standing (she
	 * kneels first; SniperRules, user request 2026-10-09).
	 */
	int32 GetActiveAttackCost() const;

	/** Godot enter_attack_mode: "🎯 Aim mode: <weapon>". */
	void EnterAttackMode();

	/** Godot exit_attack_mode; Line (if any) goes to the feed as "TACTICS". */
	void ExitAttackMode(const FString& Line = FString());

	/** Godot toggle_attack_mode (F); true when the mode is now on. */
	bool ToggleAttackMode();

	/** Cursor ground point (Godot set_hovered_cell). */
	void SetHoveredPoint(const FVector& WorldPoint, const AActor* HitActor = nullptr);

	/**
	 * Godot _update_hit_chance_label: over a hovered cell of the attack matrix «🎯 N% | 💥 D», 1.6 m above the cell.
	 * False when nothing shows.
	 */
	bool GetHoverHitChance(FVector& OutWorld, FString& OutText) const;

	/** Shots play as camera sequences (off in headless runs, like Godot's can_tween). */
	bool AreCinematicsActive() const { return bCinematics; }

	/** Checks: run the cinematic sequences in a headless run too (read at combat start). */
	bool bForceCinematicsForTesting = false;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|TurnBased")
	int32 GetRound() const { return Round; }
	int32 GetContactHitsThisFight() const { return ContactHitsThisFight; }

	const FTurnUnitState* GetUnitState(const AActor* Actor) const;

	/**
	 * Godot target_squad.take_damage(dmg, name, false, en, true): bypasses dodge / fortitude, floats «-N». Sprint 14: a
	 * knocked-down operative takes the downed modifier of the blow (KnockdownRules::DownedBlowMultiplier: ranged x0.6,
	 * melee x1.5, explosion x1). Returns the damage actually dealt.
	 */
	int32 ApplySquadHit(AActor* Victim, float Amount, const FString& Source, EKnockdownBlow Blow = EKnockdownBlow::Ranged);
	UGorkyGridManager* GetGrid() const { return Grid; }
	int32 GetEnemyCount() const { return Enemies.Num(); }

	/** The grid overlay actor of the fight (tests). */
	const ATurnGridOverlayActor* GetOverlay() const { return Overlay; }
	int32 GetSquadCount() const { return Squad.Num(); }
	/** Meshes currently shown with the stasis material (enemies outside the fight). */
	int32 GetStasisMeshCount() const { return StasisMeshes.Num(); }
	/** How many times the held-enemy guard put an enemy back this fight (MarksmanCloseShotSmoke: 0 once the cause is fixed). */
	int32 GetGuardCorrections() const { return GuardCorrections; }

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
	/** bSkipShake: the cinematic sequence already shook the camera when the shot fired. */
	FTurnAttackResult AttackCell(const FIntPoint& Cell, bool bGuaranteeHit = false, bool bSkipShake = false);

	/**
	 * Godot main.gd _perform_dramatic_tactical_attack: the camera frames shooter and target (0.4 s), the shot fires and
	 * shakes, 0.35 s later it lands (AttackCell), 0.65 s to read it, then the camera glides back to the shooter (1.1 s).
	 * An invalid shot, or cinematics off, goes straight to AttackCell (its warnings).
	 */
	void AttackCellCinematic(const FIntPoint& Cell);
	/** Tab: next operative (the last one ends the squad phase). */
	void EndCurrentUnitTurn();
	/** Enter: the whole squad ends its turn. */
	void PassSquadTurn();
	/**
	 * UE rule (user decision 2026-10-04): a medkit used by the active operative in the squad phase ends his turn (the
	 * next operative takes over). Call after the item was used; false when it does not apply.
	 */
	bool EndTurnAfterMedkit(AOperativeCharacter* Unit);
	/** Godot switch_active_unit_weapon_to: the active operative takes another arsenal weapon (free); attack cells follow. */
	bool SwitchActiveUnitWeapon(const FString& WeaponId);

	/**
	 * Mouse click in turn-based mode (Godot main.gd _handle_gorky17_tactical_click): select an operative, attack an
	 * enemy, walk; a barrel / barricade / turret next to the operative is picked up for relocation. bAttackOrder = Ctrl
	 * held (user request 2026-10-06: Ctrl + click attacks in every mode; it replaced Godot's Shift): an enemy / barrel /
	 * barricade is attacked, never a walk or a selection (TurnClickRules::ResolveClick). While relocating, the click picks
	 * the target cell.
	 */
	void HandleWorldClick(const FVector& WorldPoint, AActor* HitActor, bool bAttackOrder = false);

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

	/**
	 * Hologram of the object being moved over the hovered cell (Godot _create_relocate_ghost_preview + the mouse-motion
	 * branch): green on a valid target cell, red elsewhere; removed when the relocation ends.
	 */
	void SetRelocationHover(const FVector& WorldPoint);
	bool IsRelocationGhostShown() const { return RelocateGhost != nullptr; }
	AActor* GetRelocatingObject() const { return RelocateTarget.Get(); }
	/** Target cells of the running relocation and their AP cost. */
	const TMap<FIntPoint, int32>& GetRelocateCells() const { return RelocateCells; }
	/**
	 * Godot relocate_object: the active operative, standing next to the object (diagonal counts), pushes it along the
	 * grid path to Target; the operative takes the object's previous cells. CustomAPCost < 0 = PushBarrelAPCost per step.
	 */
	bool RelocateObject(const FIntPoint& ObjectCell, const FIntPoint& Target, int32 CustomAPCost = -1);
	/** Godot _try_push_adjacent_barrel (panel "Barrel"): relocation of an orthogonally adjacent barrel. */
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
	/** Godot tactical_enemy_hit_delay / _attack_duration (0 = the clip's length) / _retreat_delay, s. */
	float EnemyHitDelay = 0.45f;
	float EnemyAttackDuration = 0.f;
	float EnemyRetreatDelay = 0.35f;

	/** Something changed (HUD refresh). */
	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|TurnBased")
	FOnTurnBasedStateChanged OnStateChanged;

private:
	/** A relocated object gliding to its new transform (Godot tween, visual only). */
	struct FObjectGlide
	{
		TWeakObjectPtr<AActor> Actor;
		FTransform From;
		FTransform To;
		float Duration = 0.25f;
		float Delay = 0.f;
		float Elapsed = 0.f;
		bool bBackEase = false;
	};
	TArray<FObjectGlide> Glides;

	/** Grow-in of an item set up on the grid (visual only, skipped headless). */
	void StartGrowIn(AActor* Object);

	/** The assembling operative's working-device clip (when the AnimBP has one). */
	void PlayWorkingDevice(AActor* Unit);

	struct FMover
	{
		TWeakObjectPtr<AActor> Actor;
		TArray<FVector> Points;
		TArray<EGorkyFacing> Facings;
		int32 Index = 0;
		float Alpha = 0.f;
		float StepDuration = 0.5f;
		/** Nominal ground speed over the path (cell size / step duration), cm/s. */
		float Speed = 0.f;
		/** Ground speed this frame, cm/s (what the anim instances read). */
		float CurrentSpeed = 0.f;
		/** Yaw the unit turns to (smoothly) for the current step. */
		float TargetYaw = 0.f;
		FVector From = FVector::ZeroVector;
		/**
		 * Paths of 2+ cells (units and pushed objects) follow one continuous speed profile over its whole length: accelerate over the first
		 * cell, cruise, decelerate over the last, in Godot's total time (sum of the step durations).
		 */
		bool bProfile = false;
		FVector Origin = FVector::ZeroVector;
		TArray<float> CumulativeLength;
		float Cruise = 0.f;
		float AccelTime = 0.f;
		float CruiseTime = 0.f;
		float DecelTime = 0.f;
		float Time = 0.f;
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
	/**
	 * Godot occ.take_damage(final_dmg, KINETIC, 0.0, source) on an enemy: the enemy's own armor / affinity cut applies on
	 * top of the grid damage, with the floating number (squad shots, the turret).
	 */
	void ApplyEnemyHit(AActor* Enemy, float Amount, const FString& Source);
	/** Barrel / mine blast on a grid unit (Godot detonation damage rules, see the .cpp). */
	void ApplyBlast(AActor* Victim, bool bSquad, float Amount, const FString& Source);
	bool IsDead(const AActor* Actor) const;
	FString NameOf(const AActor* Actor) const;
	const UWeaponDataAsset* WeaponOf(const AActor* Actor) const;
	void Log(const FString& Message) const;
	/** Godot camera.trigger_weapon_shake from the turn-based shots (the camera ignores it outside turn-based combat). */
	void ShakeCamera(const FString& WeaponType) const;
	/** Feed line from another sender (Godot _on_quest_message("TACTICS", ...)). */
	void Post(const FString& Sender, const FString& Message) const;
	void Highlight(AActor* Target) const;
	class ATacticalCameraPawn* GetCamera() const;
	/** Godot get_squad_overview_center: the mean position of the squad on the grid. */
	FVector GetSquadOverviewCenter() const;
	/** Godot _on_gorky17_turn_changed for an operative: the camera glides to it (0.75 s, 16 m). */
	void FocusSquadTurn(AActor* Unit) const;
	/** The shot itself (AttackCell adds the attack-mode exit around it). */
	FTurnAttackResult ResolveAttackCell(const FIntPoint& Cell, bool bGuaranteeHit, bool bSkipShake);
	/** Godot _on_gorky17_enemy_movement_started: the camera follows the walking enemy (0.35 s, 11.5 m). */
	void FocusMovingEnemy(AActor* Enemy) const;
	/** The shot the cinematic would fire is valid (no warnings; AttackCell repeats the checks with them). */
	bool CanAttackQuietly(const FIntPoint& Cell) const;
	/** Turret volley outcome (damage / miss, feed line, kill); shared by the plain and the cinematic volley. */
	void ResolveTurretShot(AActor* Turret, AActor* Target, bool bHit, float Chance, float Roll);
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
	/**
	 * Enemy tactics (EnemyTurnRules / EnemyTacticsRules, user decisions 2026-10-04): the operative to go for - the
	 * archetype's preferences (wounded, straggler, exposed, turned away), the attackers already sent at each this
	 * phase, the ones melee enemies can reach. nullptr: none left.
	 */
	AActor* ChooseEnemyTarget(AActor* Enemy, bool bRanged, const TSet<FIntPoint>& Fear, FIntPoint& OutTargetPos);
	/** A ranged enemy's turn: walk to the best firing cell and shoot (hit roll), else close in for the next turn. */
	void ExecuteRangedEnemyTurn(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos, const TSet<FIntPoint>& Fear);
	void EnemyRangedAttack(AActor* Enemy, AActor* Target, const FIntPoint& TargetPos);
	/** Walks Enemy along Path as far as its AP pays (stops on a mine and detonates it); OnArrived after the walk. False: no step. */
	bool WalkEnemy(AActor* Enemy, const TArray<FIntPoint>& Path, TFunction<void(AActor*)> OnArrived);
	/** Target stands next to a barricade on Shooter's side. */
	bool IsCoveredFrom(const FIntPoint& TargetCell, const FIntPoint& Shooter) const;
	/** Operatives targeted this enemy phase (the focus cap of the pack). */
	TMap<TWeakObjectPtr<AActor>, int32> EnemyPhaseTargets;
	void FinishEnemyTurn(float Delay);
	TSet<FIntPoint> GetFearCells() const;

	void DetonateBarrel(const FIntPoint& Cell, AActor* Barrel);
	void DetonateMine(const FIntPoint& Cell, AActor* Mine, AActor* Victim);
	void OnEnemyKilled(AActor* Enemy, const FIntPoint& Cell);
	void OnSquadMemberKilled(AActor* Member, const FIntPoint& Cell);
	/** The last operative of the squad phase died on his own turn: end the phase once the running action is over. */
	void EndSquadPhaseAfterDeath();
	bool CheckBattleEnd();

	void StartMover(AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path, float StepDuration, TFunction<bool(int32)> OnStep,
		TFunction<void()> OnDone, bool bFaceSteps = true);
	void After(float Seconds, TFunction<void()> Callback);

	/**
	 * Deviation from Godot (user decision 2026-10-01; Godot crawls from cell to cell): an operative ordered to walk while
	 * prone first rises to crouching (free; the turn state stance follows), and a crouched walk is slower by the
	 * real-time crouch / walk speed ratio. Returns the seconds the walk waits for the rising clip; OutStepDuration is the
	 * squad step duration for the unit's stance.
	 */
	float PrepareSquadWalk(AOperativeCharacter* Unit, float& OutStepDuration);

	/** StartMover after Delay seconds (the squad stays busy meanwhile); now when Delay is 0. */
	void StartMoverAfter(float Delay, AActor* Actor, const FIntPoint& From, const TArray<FIntPoint>& Path, float StepDuration,
		TFunction<void()> OnDone, bool bFaceSteps = true);

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
	/** Every enemy held by this fight (AEnemyCharacter::SetTurnBasedHeld) with its place when frozen (stasis anchor; grid units use their cell). */
	TMap<TWeakObjectPtr<AEnemyCharacter>, FVector> HeldAnchors;
	/** Puts back a held enemy pushed off its cell / place (or outside the grid) — the guard of the 2026-10-06 bug fix. */
	void EnforceHeldEnemies();
	TArray<FMover> Movers;
	FExposedZones Zones;

	UPROPERTY(Transient)
	TArray<FTurnStasisMesh> StasisMeshes;
	TWeakObjectPtr<AActor> RelocateTarget;

	UPROPERTY(Transient)
	TObjectPtr<class ARelocationGhostActor> RelocateGhost;
	TWeakObjectPtr<AActor> RelocateGhostSource;
	FIntPoint RelocateOrigin = FIntPoint(-1, -1);
	TMap<FIntPoint, int32> RelocateCells;
	float RelocateYaw = 0.f;

	/** Sprint 14: the active operative lies after a knockdown — 2 AP get him up, with fewer his turn is skipped. */
	void UpdateKnockedDownTurn();
	ETurnPhase Phase = ETurnPhase::Inactive;
	int32 ActiveIndex = 0;
	int32 Round = 0;
	/** EnforceHeldEnemies corrections this fight. */
	int32 GuardCorrections = 0;
	/** Turn-based barricade contact hits this fight (Sprint 06-C). */
	int32 ContactHitsThisFight = 0;
	bool bSquadUnitMoving = false;
	/** The ordered grid shot waiting for its presentation (user request 2026-10-09). */
	struct FPendingGridShot
	{
		TWeakObjectPtr<AOperativeCharacter> Shooter;
		TWeakObjectPtr<AActor> Target;
		bool bCoverShot = false;
		bool bSkipShake = false;
		bool bHit = false;
		FString ShakeKind;
		TFunction<void()> Effects;
		int32 CombatAtStart = 0;
		float Elapsed = 0.f;
	};
	TOptional<FPendingGridShot> PendingShot;
	int32 GridShotsFired = 0;
	/** Shows the pending shot once the shooter is on target and settled (TurnAttackTimeline). */
	void UpdatePendingShot(float DeltaTime);
	/** Runs Then once no grid shot is pending (polled; dropped when the fight ends). */
	void WhenShotResolved(TFunction<void()> Then);
	bool bDramaticShotActive = false;
	bool bAttackMode = false;
	FIntPoint HoveredCell = FIntPoint(-999, -999);
	TMap<FIntPoint, FTurnBasedAttackCell> AttackCells;
	bool bCinematics = false;
	/** Invalidates pending timers of a finished combat. */
	int32 CombatId = 0;
};
