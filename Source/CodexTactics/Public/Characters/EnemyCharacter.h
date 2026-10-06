#pragma once

#include "CoreMinimal.h"
#include "AI/PatrolRouteRules.h"
#include "AI/PerceptionRules.h"
#include "Characters/EnemyAIRules.h"
#include "GameFramework/Character.h"
#include "Data/CombatTypes.h"
#include "EnemyCharacter.generated.h"

class APatrolRouteActor;
class UHealthComponent;
struct FOverheadLabel;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** A loud squad action enemies can hear (PerceptionRules gunshot / grenade radius). */
enum class ESquadNoise : uint8
{
	/** An operative fired (at an enemy or an object). */
	Gunshot,
	/** A grenade thrown by the squad went off. */
	Explosion
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedDynamic, AEnemyCharacter*, Enemy);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEnemyDiedNative, AEnemyCharacter*);

/**
 * Base enemy character in CodexTactics.
 * Tagged "Enemy" for combat queries and targeting.
 * Configurable via EEnemyArchetype (Hound, Spitter, Brute, Frostbitten).
 * Godot reference: Scenes/movements/enemy_base.gd.
 */
UCLASS()
class CODEXTACTICS_API AEnemyCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Configures the enemy according to the archetype stats and visuals. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void InitializeArchetype(EEnemyArchetype InArchetype);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	EEnemyArchetype GetArchetype() const { return Archetype; }

	/** Name used in radio lines (Godot enemy_name). */
	const FString& GetEnemyDisplayName() const { return EnemyDisplayName; }

	/**
	 * Godot apply_balance_config (enemy_base.gd + enemy_frost_hound / spitter / brute.gd): crit chance / multiplier and,
	 * for hounds, spitters and brutes, health / speed / damage / range / cooldown from the imported game_balance_config.
	 * Called at the end of ApplyArchetypeDefaults with the game mode's GameBalanceConfig.
	 */
	void ApplyBalance(const class UGodotBalanceAsset& Config);

	/**
	 * Level wave modifiers (Godot main.gd _spawn_custom_json_wave): max health = CustomHealth * HpMult when the spawn
	 * has custom_stats.health, else max health * HpMult (full health); attack damage * DamageMult; speed * SpeedMult.
	 */
	void ApplyWaveModifiers(float HpMult, float DamageMult, float SpeedMult, float CustomHealth = 0.f);

	/** The wave modifiers plus the spawn's custom_stats (health, damage, speed, attack range / cooldown). */
	void ApplySpawnEntry(const struct FWaveModifiers& Mods, const struct FEnemySpawnEntry& Entry);

	/** Avoids burning barrels (Godot fears_fire; turn-based enemies keep out of the 5 x 5 fire-fear area). */
	bool DoesFearFire() const { return bFearsFire; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }
	/** The pack coordinator may send it at Candidate: no recent dead end there and (fire fearers) not inside a fire zone. */
	bool IsTargetUsableForTactics(const AActor* Candidate) const;
	/** Dev diagnostics: path following, progress timer, fear / braving / fall-back, dead-end targets, status. */
	FString GetDebugState() const;

	/** EXP every squad member gets for this kill. */
	int32 GetKillExpReward() const { return KillExpReward; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsDying() const { return bIsDying; }

	/**
	 * Turn-based hold (bug fix 2026-10-06, MarksmanCloseShotSmoke): UTurnBasedCombatSubsystem sets it on every enemy it
	 * freezes (grid units and the ones in stasis) and clears it when the fight ends. While held the AI controller refuses
	 * navigation moves and the movement is stopped at once; event-driven reactions (a hit, a timer) must not move it —
	 * the grid alone moves its units. The actor tick being off is not enough: the movement component and the path
	 * following keep running.
	 */
	void SetTurnBasedHeld(bool bHeld);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	bool IsTurnBasedHeld() const { return bTurnBasedHeld; }

	/**
	 * Horde member (UHordeSubsystem, user request 2026-10-06): it knows where every operative is (no sight / hearing
	 * needed — UTacticalSightSubsystem::GetBelief answers «perceived»), goes straight for the squad and never falls back.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "CodexTactics|Enemy")
	bool bKnowsSquadPosition = false;

	/**
	 * Godot enemy_base.gd _update_overhead_ui: armor tier marker, name, statuses, HP (hound / cutter 1.15 m, brute 2.4 m,
	 * others 1.8 m up). The status emoji the HUD font lacks are written as words.
	 */
	bool GetOverheadLabel(FOverheadLabel& OutLabel) const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackDamage() const { return AttackDamage; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackRange() const { return AttackRange; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	float GetAttackCooldown() const { return AttackCooldown; }

	/** Attacks target directly (applies damage spec to target's health component). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Enemy")
	void AttackTarget(AActor* Target);

	/** Finds the closest living operative from USquadSubsystem. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Enemy")
	AActor* FindClosestSquadMember() const;

	/**
	 * Godot _find_closest_squad_member: operatives, turrets and the generator weighted by EnemyAIRules::SelectTarget
	 * (small enemies go for the generator / turrets first, large ones for the squad).
	 */
	AActor* FindTarget() const;

	/** Cutter pounce (Godot enemy_cutter.gd): windup, ballistic flight, impact, recovery. */
	enum class ECutterJumpPhase : uint8 { None, Windup, Airborne, Impact };

	/** Godot start_jump_attack: the cutter leaps at Target (3.5-9 m, jump config from DA_EnemyAnim_cutter). */
	bool StartJumpAttack(AActor* Target);
	bool IsJumpAttacking() const { return JumpPhase != ECutterJumpPhase::None; }
	/** Seconds until the next pounce. */
	float GetJumpCooldown() const { return JumpCooldownTimer; }

	/** Blueprint hooks for animations / effects (the enemy's AnimBP can also read UEnemyAnimInstance). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnAttackStarted(AActor* Target);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnHitReaction(float Damage);
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnDeath();

	/** Blueprint hooks for the jump animation (the cutter Blueprint plays its clips). */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackStarted();
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Enemy")
	void OnJumpAttackImpact();

	/** Godot is_fleeing_fire: running from a burning barrel / warm zone. */
	bool IsFleeingFire() const { return bFleeingFire; }

	/**
	 * No usable target (all in fire / heat zones or out of reach): the enemy goes for the nearest operative and ignores
	 * its fear of fire until a usable target appears (user decision 2026-10-02; it used to wait at the zone's edge).
	 */
	bool IsBravingFire() const { return bBravingFire; }

	/** Current victim (operative, turret or generator). */
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	/** Smokes: forces the victim (the behaviour tick may re-pick it; freeze the enemy with CustomTimeDilation 0 first). */
	void SetCurrentTargetForTesting(AActor* Target) { CurrentTarget = Target; }

	/** Shared enemy tuning (Godot enemy_* / spitter_preferred_range keys). */
	FEnemyAIConfig AIConfig;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Enemy")
	FOnEnemyDiedDynamic OnEnemyDied;

	FOnEnemyDiedNative OnEnemyDiedNative;

	// --- Sprint 11 outpost stealth patrols (no Godot reference — Sprint 11 spec by Gemini, docs/port/TANDEM.md) ---

	/**
	 * Spline route this enemy patrols from map start (no preparation delay) until it is alerted: it walks waypoint to
	 * waypoint at PatrolWalkSpeed, pauses at each, then turns towards the next segment. Null: no route (wave enemies
	 * behave as before; a marksman falls back to his legacy PatrolRoute points).
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CodexTactics|Patrol")
	TObjectPtr<APatrolRouteActor> AssignedPatrolRoute;

	/**
	 * Leader this enemy escorts (e.g. a frost hound with a marksman): it stays 200-350 cm from him while he patrols and
	 * breaks off with him. Not BlueprintReadWrite (UHT does not expose weak pointers to Blueprints): Get/SetEscortLeader.
	 */
	UPROPERTY(EditInstanceOnly, Category = "CodexTactics|Patrol")
	TWeakObjectPtr<AEnemyCharacter> EscortLeader;

	/** Walk speed on the patrol route / while escorting, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0"))
	float PatrolWalkSpeed = 190.f;

	/**
	 * Use PerceptionOverride instead of its archetype's row in Content/Data/AI/enemy_perception.json (a special guard on
	 * one map, smokes). Off: the data file decides (user request 2026-10-06; replaces the fixed 30 m PatrolSightRange).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Perception")
	bool bOverridePerception = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Perception", meta = (EditCondition = "bOverridePerception"))
	FEnemyPerceptionParams PerceptionOverride;

	/** Sight / hearing / smell in force (override or the archetype's data; smell only for hounds). */
	FEnemyPerceptionParams GetPerception() const;

	/** A tripwire / mine detonation this close (planar) breaks its patrol, cm (user decision 2026-10-06: 20 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Patrol", meta = (ClampMin = "0"))
	float PatrolTrapAlertRadius = PatrolRouteRules::TrapAlertRadiusCm;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	AEnemyCharacter* GetEscortLeader() const { return EscortLeader.Get(); }

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Patrol")
	void SetEscortLeader(AEnemyCharacter* NewLeader);

	/**
	 * Puts a spawned enemy on patrol at runtime (level-placed ones start from their AssignedPatrolRoute / EscortLeader at
	 * BeginPlay): walks Route, or escorts Leader when one is given. Both null: no patrol.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Patrol")
	virtual void StartPatrol(APatrolRouteActor* Route, AEnemyCharacter* Leader);

	/** Still on its patrol / escort duty (not alerted). Wave enemies without a route or leader: false. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	virtual bool IsOnPatrol() const { return bPatrolActive; }

	/**
	 * Patrol -> Engage: stops the walk, alerts its leader / escorts, remembers AlertLocation (an engaged enemy that knows
	 * of no operative goes there). No-op when not on patrol.
	 */
	virtual void BreakPatrol(EPatrolAlertCause Cause, const FVector& AlertLocation);

	/**
	 * A tripwire / mine / placed charge went off at Location: within PatrolTrapAlertRadius the patrol starts a search
	 * (user amendment 2026-10-06: a trap no longer breaks it into Engage, so it does not start the fight).
	 */
	void NotifyTrapTriggered(const FVector& Location);

	/** Every enemy in World hears a trap that went off at Location (ATripwireActor / AProximityMineActor / ApplyBlast). */
	static void AlertPatrolsNearTrap(UWorld* World, const FVector& Location);

	/**
	 * A squad gunshot / thrown grenade at Location: every patrolling enemy within its hearing radius (x the search
	 * multiplier while searching) breaks into Engage (and the fight starts on an ambush level).
	 */
	static void NotifySquadNoise(UWorld* World, const FVector& Location, ESquadNoise Noise);

	/**
	 * Patrol -> Search: walks to Location (faster than the patrol pace), then sweeps random reachable points around it
	 * with heightened perception for the level's search time (60 s), then returns to the nearest waypoint of its route.
	 * Its leader / escorts search too. A new blast moves the search there and restarts the clock. No-op off patrol.
	 */
	virtual void StartPatrolSearch(const FVector& Location);

	/** Hunting for the squad after a trap (still on patrol duty, not engaged). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Patrol")
	bool IsSearching() const { return bSearching; }

	/** Suspicion meter 0..1 of the patrol sight (1 = detected). */
	float GetSuspicion() const { return PatrolSuspicion; }

	/** True while a trap / placed charge deals its blast damage (AInteractableActor::ApplyBlast): damage = trap event. */
	static bool IsTrapBlastInProgress() { return TrapBlastDepth() > 0; }

	/** Nesting depth of FScopedTrapBlast (game thread only). */
	static int32& TrapBlastDepth();

	/** Marks the damage dealt inside its lifetime as a trap blast (search, not the squad's direct attack). */
	struct FScopedTrapBlast
	{
		FScopedTrapBlast() { ++TrapBlastDepth(); }
		~FScopedTrapBlast() { --TrapBlastDepth(); }
		FScopedTrapBlast(const FScopedTrapBlast&) = delete;
		FScopedTrapBlast& operator=(const FScopedTrapBlast&) = delete;
	};

	/** Index of the waypoint it walks to / waits at (smokes, debugging). */
	int32 GetPatrolWaypointIndex() const { return PatrolWaypointIndex; }

protected:
	/** Patrol state of the route driver (TickPatrolRoute). */
	enum class EPatrolPhase : uint8 { Moving, Waiting, Turning, Finished };

	/** Set at BeginPlay when it has a route or a leader; cleared by BreakPatrol. */
	bool bPatrolActive = false;
	EPatrolPhase PatrolPhase = EPatrolPhase::Moving;
	int32 PatrolWaypointIndex = 0;
	bool bPatrolForward = true;
	float PatrolWaitLeft = 0.f;
	float PatrolTurnTime = 0.f;
	float PatrolStuckTime = 0.f;
	bool bPatrolMoveIssued = false;
	float PatrolSightTimer = 0.f;
	bool bEscortMoving = false;
	/** Last tether point it was sent to (a new move only when the leader walked on). */
	FVector EscortGoal = FVector::ZeroVector;
	/** Where the alarm came from: an engaged enemy with no known operative investigates it. */
	FVector PatrolAlertLocation = FVector::ZeroVector;
	bool bHasPatrolAlertLocation = false;

	/** Walks AssignedPatrolRoute (move, wait, turn to the next segment). */
	void TickPatrolRoute(float DeltaTime);
	/** Patrol / escort tick of the base enemy: sight check, leader mirror, tether or route. */
	void TickPatrolBehavior(float DeltaTime);
	/** Keeps the 200-350 cm tether to Leader. */
	void TickEscort(float DeltaTime, const AEnemyCharacter& Leader);
	/** Moves towards a patrol waypoint / tether point at PatrolWalkSpeed (the marksman walks his own way). */
	virtual void IssuePatrolMove(const FVector& Goal, float Speed);
	/**
	 * The closest living operative it sees now (PerceptionRules::CanSee with the Sprint 08 eye -> profile trace, so a
	 * prone operative behind 60 cm cover stays hidden); OutDistance / OutRange: distance and the stance sight range.
	 */
	AActor* FindVisibleOperative(const FEnemyPerceptionParams& Params, float& OutDistance, float& OutRange) const;
	/**
	 * Sight (suspicion build-up), hearing (squad gait) and smell (hounds) every 0.2 s; a detection breaks the patrol
	 * (BreakPatrol). True when it broke.
	 */
	bool TickPatrolPerception(float DeltaTime);
	/** Perception in force on patrol (x the search multiplier while searching). */
	FEnemyPerceptionParams GetPatrolPerception() const;
	/** Search driver: blast point, then sweep points, until the search time is over. */
	void TickPatrolSearch(float DeltaTime);
	/** Search over: back to the nearest waypoint (route) / the tether (escort). */
	void EndPatrolSearch();
	/** «[Stealth] detection by <sense> at <t> s» log line for the Jev AI coach. */
	void LogStealthDetection(EPatrolAlertCause Cause, bool bWasSearching) const;
	/** World AI held (dialogue, cutscene): stops once and returns true — the behaviour tick does nothing else. */
	bool HoldForWorldAIPause();

	bool bSearching = false;
	FVector SearchOrigin = FVector::ZeroVector;
	FVector SearchGoal = FVector::ZeroVector;
	float SearchElapsed = 0.f;
	float SearchLegTime = 0.f;
	float SearchLookLeft = 0.f;
	float SearchStuckTime = 0.f;
	bool bSearchReachedOrigin = false;
	bool bSearchMoving = false;
	float PatrolSuspicion = 0.f;
	bool bHeldByAIPause = false;
	/** See SetTurnBasedHeld. */
	bool bTurnBasedHeld = false;
	/** Subclass hook of SetTurnBasedHeld (the marksman cancels his get-up / aim). */
	virtual void OnTurnBasedHeldChanged(bool bHeld) {}
	/** Breaks the patrol of its leader and of every enemy escorting it. */
	void PropagatePatrolBreak(const FVector& AlertLocation);
	/** Sets up the patrol from AssignedPatrolRoute / EscortLeader (BeginPlay). */
	void InitPatrol();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	TObjectPtr<UHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Enemy")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	EEnemyArchetype Archetype = EEnemyArchetype::FrostHound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	FString EnemyDisplayName = TEXT("Ледяная гончая");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackDamage = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackRange = 180.0f; // 1.8m

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float AttackCooldown = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritChance = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	float CritMultiplier = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Enemy")
	bool bFearsFire = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	bool bIsDying = false;

	/** EXP every squad member gets for this kill (Godot exp_reward_<type>; ProgressionRules::KillReward). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "CodexTactics|Enemy")
	int32 KillExpReward = 9;

	float AttackTimer = 0.0f;

	/** Seconds the attack clip still plays: the enemy stands and faces AttackLockTarget (Godot is_attacking). */
	float AttackLockTimer = 0.0f;
	TWeakObjectPtr<AActor> AttackLockTarget;

	/** Starts the attack clip and the stand-still lock; the cooldown lasts at least the clip (Godot _play_attack_animation). */
	void StartAttackAnimation(AActor* Target);

	// --- Cutter jump (Godot enemy_cutter.gd; numbers from /Game/Data/Enemies/DA_EnemyAnim_cutter) ---
	bool bJumpAttackEnabled = false;
	float JumpAttackSpeed = 1.85f;
	float JumpMinDistance = 350.f;
	float JumpMaxDistance = 900.f;
	float JumpCooldown = 6.f;
	float JumpAttackDamage = 28.f;
	float JumpDamageRadius = 220.f;
	ECutterJumpPhase JumpPhase = ECutterJumpPhase::None;
	float JumpPhaseTimer = 0.f;
	float JumpCooldownTimer = 0.f;
	float JumpFlightDuration = 0.7f;
	bool bJumpDamageDealt = false;
	bool bAirborneDeath = false;
	TWeakObjectPtr<AActor> JumpTarget;
	void TickJumpAttack(float DeltaTime);
	void ApplyJumpImpactDamage();
	virtual void Landed(const FHitResult& Hit) override;

	/** Walk speed before frost / fear factors (archetype, balance, wave modifiers). */
	float BaseWalkSpeed = 300.f;
	bool bFleeingFire = false;
	/** Set by FindTarget (see IsBravingFire). */
	mutable bool bBravingFire = false;
	/** Godot last_attacker_source (a turret hit pulls large enemies to turrets). */
	FString LastAttackerSource;
	TWeakObjectPtr<AActor> CurrentTarget;

	/** Targets it could not get closer to (no path): skipped until the time (world seconds). */
	TMap<TWeakObjectPtr<AActor>, double> UnreachableUntil;
	/** Progress towards the current target (stuck detection). */
	TWeakObjectPtr<AActor> ProgressTarget;
	FVector ProgressLocation = FVector::ZeroVector;
	float ProgressTimer = 0.f;
	/** Running a morale fall-back (the floating text once per fall-back). */
	bool bFallingBack = false;
	/** The target this enemy already went round (flank done: straight in from now on). */
	TWeakObjectPtr<AActor> FlankDoneTarget;
	/** Seconds standing on a barricade / barrel top (VaultNavigation::IsStandingOnObstacle). */
	float ObstacleTopTime = 0.f;

	/** Nearest burning barrel / active heat source within the fear radius (Godot _find_nearest_active_fire_source). */
	bool FindNearestFire(FVector& OutFire) const;
	/** Nearest burning barrel / active heat zone whose fear radius (OutRadius) reaches within Extra of the feet. */
	bool FindNearestFireZone(float Extra, FVector& OutFire, float& OutRadius) const;
	/** Location inside a burning barrel's / active heat zone's fear radius (the heat of Candidate itself excluded). */
	bool IsInFearZone(const FVector& Location, const AActor* Candidate) const;
	/** Barricade / turret in the way within 2.2 m (Godot _find_blocking_barricade). */
	AActor* FindBlockingObstacle(const AActor* Target) const;
	/** Godot _attack_barricade (brutes hit twice as hard) and melee on turrets / the generator. */
	void AttackObject(AActor* Object);
	/** Godot enemy_frost_spitter.gd _process_enemy_behavior. */
	void TickSpitter(float DeltaTime);

	/**
	 * The behaviour part of Tick (targets, attacks, movement); Tick then turns the body (UpdateMovementFacing).
	 * Archetypes with their own brain (AMarksmanEnemyCharacter) override it.
	 */
	virtual void TickBehavior(float DeltaTime);

	/** Turn towards Yaw this frame (attack lock, aiming, pounce windup): the movement facing then stays out of it. */
	void FaceYaw(float Yaw, float DeltaTime, float InterpSpeed);

	/** Godot enemy_base.gd: face the (smoothed) velocity above 0.2 m/s with lerp_angle(turn_speed), else keep. */
	void UpdateMovementFacing(float DeltaTime);

	/** Godot enemy_base.gd turn_speed (lerp factor, 1/s). */
	float TurnSpeed = 8.f;
	FVector SmoothedVelocity = FVector::ZeroVector;
	bool bFacedThisTick = false;
	/** Godot position of a target: operatives at their centre, objects at their base. */
	static FVector GodotPosition(const AActor* Actor);

	UFUNCTION()
	void HandleDamaged(const FDamageSpec& Spec, float FinalDamage);

	UFUNCTION()
	void HandleDied(AActor* Victim, const FString& AttackerSource);

	void ApplyArchetypeDefaults();
};
